#include "App.h"
#include "Exchange.hpp"
#include "GBMGenerator.hpp"
#include "ThreadSafeQueue.hpp"
#include <nlohmann/json.hpp>
#include <atomic>
#include <thread>
#include <chrono>
#include <iostream>
#include <set>
#include <random>
#include <sstream>

using json = nlohmann::json;

// -------------------------------------------------------------------
// Sanity bounds + per-connection rate limiting.
//
// The simulator has no auth and no persistence, so the cheapest defence
// against a runaway bot (or a hostile client) flooding the order queue or
// submitting nonsensical prices is to reject obviously-bad input at the
// WebSocket boundary, before it ever reaches the matching engine.
// -------------------------------------------------------------------
static constexpr double   MAX_ORDER_PRICE = 10'000'000.0;
static constexpr double   MAX_ORDER_QTY   = 100'000.0;
static constexpr int      RATE_LIMIT_BURST       = 40;  // tokens in the bucket
static constexpr double   RATE_LIMIT_PER_SEC     = 20.0; // refill rate

struct PerSocketData {
    std::string connId;
    double      tokens       = RATE_LIMIT_BURST;
    int64_t     lastRefillMs = 0;
};

// -------------------------------------------------------------------
// Global running flag
// -------------------------------------------------------------------
static std::atomic<bool> g_running{true};

// -------------------------------------------------------------------
// Utility: simple UUID-ish ID
// -------------------------------------------------------------------
static std::string makeId() {
    static std::atomic<uint64_t> ctr{1};
    return "conn_" + std::to_string(ctr++);
}

static int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

// Token-bucket rate limiter. Runs on the single uWS event-loop thread inside
// the .message handler, so no locking is needed even though it mutates
// per-connection state.
static bool allowMessage(PerSocketData* data) {
    int64_t now = nowMs();
    if (data->lastRefillMs == 0) data->lastRefillMs = now;
    double elapsedSec = (now - data->lastRefillMs) / 1000.0;
    if (elapsedSec > 0) {
        data->tokens = std::min<double>(RATE_LIMIT_BURST, data->tokens + elapsedSec * RATE_LIMIT_PER_SEC);
        data->lastRefillMs = now;
    }
    if (data->tokens < 1.0) return false;
    data->tokens -= 1.0;
    return true;
}

// -------------------------------------------------------------------
// Build an order from a parsed JSON message
// -------------------------------------------------------------------
static bool parseOrder(const json& msg,
                       const std::string& connId,
                       ThreadSafeQueue<Order>& queue,
                       std::string& errOut)
{
    try {
        std::string type = msg.at("type").get<std::string>();

        if (type == "limit_order") {
            Order o;
            o.clientId     = connId;
            o.type         = OrderType::LIMIT;
            o.side         = (msg.at("side").get<std::string>() == "BID") ? Side::BID : Side::ASK;
            o.price        = msg.at("price").get<double>();
            o.quantity     = msg.at("qty").get<double>();
            o.remainingQty = o.quantity;
            if (!(o.price > 0) || !(o.price <= MAX_ORDER_PRICE)) { errOut = "price out of bounds"; return false; }
            if (!(o.quantity > 0) || !(o.quantity <= MAX_ORDER_QTY)) { errOut = "quantity out of bounds"; return false; }
            queue.push(o);
            return true;
        }
        if (type == "market_order") {
            Order o;
            o.clientId     = connId;
            o.type         = OrderType::MARKET;
            o.side         = (msg.at("side").get<std::string>() == "BID") ? Side::BID : Side::ASK;
            o.quantity     = msg.at("qty").get<double>();
            o.remainingQty = o.quantity;
            if (!(o.quantity > 0) || !(o.quantity <= MAX_ORDER_QTY)) { errOut = "quantity out of bounds"; return false; }
            queue.push(o);
            return true;
        }
        if (type == "cancel_order") {
            Order o;
            o.type     = OrderType::CANCEL;
            o.id       = msg.at("orderId").get<uint64_t>();
            o.clientId = connId;
            queue.push(o);
            return true;
        }
        errOut = "unknown message type";
    } catch (...) {
        errOut = "malformed order";
    }
    return false;
}

// -------------------------------------------------------------------
int main() {
    ThreadSafeQueue<Order> orderQueue;
    Exchange exchange;
    GBMGenerator gbm(orderQueue, 50000.0);

    // Grab the event-loop pointer BEFORE starting background threads
    // (Loop::get() is thread-local; must be called from the uWS thread)
    uWS::Loop* loop = uWS::Loop::get();

    // ---------------------------------------------------------------
    // GBM thread: tick every 100 ms → ~100 order messages / sec
    // ---------------------------------------------------------------
    std::thread gbmThread([&]() {
        using namespace std::chrono;
        while (g_running) {
            auto t0 = steady_clock::now();
            gbm.tick();
            auto elapsed = steady_clock::now() - t0;
            auto sleep   = milliseconds(100) - duration_cast<milliseconds>(elapsed);
            if (sleep > milliseconds(0))
                std::this_thread::sleep_for(sleep);
        }
    });

    // ---------------------------------------------------------------
    // uWS app (declared before processor thread captures a reference)
    // ---------------------------------------------------------------
    uWS::App app;

    // ---------------------------------------------------------------
    // Processor thread: drain order queue, emit WebSocket events
    // ---------------------------------------------------------------
    std::thread processorThread([&]() {
        int batchCount = 0;
        while (g_running) {
            Order order;
            if (orderQueue.pop(order, std::chrono::milliseconds(50))) {
                auto events = exchange.processOrder(order);

                for (auto& ev : events) {
                    std::string topic = ev.topic;
                    std::string body  = ev.json;
                    loop->defer([&app, topic, body]() mutable {
                        app.publish(topic, body, uWS::OpCode::TEXT);
                    });
                }
                ++batchCount;
            }

            // Broadcast order-book snapshot every ~20 processed orders
            if (batchCount >= 20) {
                batchCount = 0;
                std::string snap = exchange.getSnapshotJson();
                loop->defer([&app, snap]() mutable {
                    app.publish("market", snap, uWS::OpCode::TEXT);
                });
                std::string candle = exchange.getLiveCandleJson();
                if (!candle.empty()) {
                    loop->defer([&app, candle]() mutable {
                        app.publish("market", candle, uWS::OpCode::TEXT);
                    });
                }
            }
        }
    });

    // ---------------------------------------------------------------
    // WebSocket route
    // ---------------------------------------------------------------
    app.ws<PerSocketData>("/*", {
        .compression      = uWS::SHARED_COMPRESSOR,
        .maxPayloadLength = 64 * 1024,

        .open = [&exchange](auto* ws) {
            std::string connId = makeId();
            ws->getUserData()->connId = connId;
            ws->getUserData()->tokens = RATE_LIMIT_BURST;
            ws->getUserData()->lastRefillMs = 0;
            ws->subscribe("market");
            ws->subscribe("portfolio_" + connId);
            ws->subscribe("client_" + connId);

            // Send welcome + initial state
            try {
                json welcome;
                welcome["type"]     = "init";
                welcome["clientId"] = connId;
                welcome["portfolio"]= json::parse(exchange.getPortfolioJson(connId));
                welcome["history"]  = json::parse(exchange.getCandleHistoryJson());
                ws->send(welcome.dump(), uWS::OpCode::TEXT);

                // Also send current book snapshot
                ws->send(exchange.getSnapshotJson(), uWS::OpCode::TEXT);
            } catch (...) {}

            std::cout << "[open]  " << connId << "\n";
        },

        .message = [&orderQueue](auto* ws, std::string_view raw, uWS::OpCode opCode) {
            if (opCode != uWS::OpCode::TEXT) return;

            auto* data = ws->getUserData();
            if (!allowMessage(data)) {
                json err = {{"type","error"},{"message","rate limit exceeded — slow down"}};
                ws->send(err.dump(), uWS::OpCode::TEXT);
                return;
            }

            try {
                json msg = json::parse(raw);
                const std::string& connId = data->connId;
                std::string parseErr;
                if (!parseOrder(msg, connId, orderQueue, parseErr)) {
                    json err = {{"type","error"},{"message", parseErr.empty() ? "invalid order" : parseErr}};
                    ws->send(err.dump(), uWS::OpCode::TEXT);
                }
            } catch (const std::exception& e) {
                json err = {{"type","error"},{"message", e.what()}};
                ws->send(err.dump(), uWS::OpCode::TEXT);
            }
        },

        .close = [&exchange](auto* ws, int /*code*/, std::string_view /*reason*/) {
            const std::string& connId = ws->getUserData()->connId;
            // Don't leave this client's resting orders in the book forever —
            // nobody else could ever cancel them once the connection is gone.
            exchange.cancelAllOrdersForClient(connId);
            std::cout << "[close] " << connId << "\n";
        }
    });

    // ---------------------------------------------------------------
    // HTTP health check
    // ---------------------------------------------------------------
    app.get("/health", [](auto* res, auto* /*req*/) {
        res->writeHeader("Content-Type", "application/json")
           ->end(R"({"status":"ok"})");
    });

    // ---------------------------------------------------------------
    // Start listening — this blocks until the process is killed
    // ---------------------------------------------------------------
    app.listen("0.0.0.0", 9001, [](auto* token) {
        if (token)
            std::cout << "Synthetic-Bull exchange listening on :9001\n";
        else
            std::cerr << "ERROR: Failed to bind port 9001\n";
    }).run();

    // Cleanup
    g_running = false;
    gbmThread.join();
    processorThread.join();
    return 0;
}
