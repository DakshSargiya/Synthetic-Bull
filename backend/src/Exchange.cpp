#include "Exchange.hpp"
#include <chrono>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cmath>
#include <set>

// --------------------------------------------------------------------------
// Helpers
// --------------------------------------------------------------------------
static int64_t nowSec() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}
static int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}
static std::string dbl(double v) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2) << v;
    return ss.str();
}

// Money amounts (cash, avgCost, realizedPnL) are kept as `double` for
// simplicity, but every mutation is rounded to the nearest cent so tiny
// binary floating-point error can't silently accumulate across thousands of
// fills into a visible drift. This is a mitigation, not a full fixed-point
// rewrite — see README "Known limitations" for the follow-up recommendation.
static double round2(double v) {
    return std::round(v * 100.0) / 100.0;
}

// --------------------------------------------------------------------------
Exchange::Exchange() {
    liveCandl_ = {};
    liveCandleTime_ = nowSec();
}

// --------------------------------------------------------------------------
// Main entry point
// --------------------------------------------------------------------------
std::vector<Exchange::Event> Exchange::processOrder(Order order) {
    std::vector<Event> events;
    std::vector<Trade> trades;
    std::vector<StpCancel> stpCancels;

    // Outcome of a CANCEL request, decided under the lock, acted on after.
    bool isCancel      = (order.type == OrderType::CANCEL);
    bool cancelOk       = false;

    // Outcome of a LIMIT/MARKET submission, for the order_ack event.
    bool wantsAck       = false;
    Order acked;

    {
        std::lock_guard<std::mutex> lock(mu_);

        // Assign an ID to non-simulator orders
        if (order.id == 0) order.id = nextOrderId_++;

        if (isCancel) {
            cancelOk = book_.cancelOrder(order.id, order.clientId);
        } else {
            // Ensure portfolio exists
            getOrCreate(order.clientId);

            if (order.type == OrderType::LIMIT) {
                auto mr = book_.addLimitOrder(order);
                trades      = std::move(mr.trades);
                stpCancels  = std::move(mr.stpCancels);
                wantsAck    = true;
                acked       = order; // remainingQty now reflects what rested (if any)
            } else if (order.type == OrderType::MARKET) {
                auto mr = book_.addMarketOrder(order);
                trades      = std::move(mr.trades);
                stpCancels  = std::move(mr.stpCancels);
                if (order.remainingQty > 1e-10) {
                    // Book ran out of liquidity before the market order filled.
                    wantsAck = true;
                    acked    = order;
                }
            }
        }

        // Update candle + lastPrice on trades
        for (auto& t : trades) {
            lastPrice_.store(t.price);
            updateCandle(t.price, t.quantity);
        }
    }

    if (isCancel) {
        if (cancelOk) {
            events.push_back({"client_" + order.clientId, orderCancelledJson(order.id, "user")});
        } else {
            events.push_back({"client_" + order.clientId,
                               errorJson("cancel rejected: order not found or not owned by you")});
        }
        return events;
    }

    // Emit trade events (on "market" topic)
    for (auto& t : trades) {
        events.push_back({"market", tradeToJson(t)});
    }

    // Self-trade-prevention cancels: tell the affected client their resting
    // order was pulled instead of crossing against their own new order.
    for (auto& c : stpCancels) {
        events.push_back({"client_" + c.clientId, orderCancelledJson(c.orderId, "self_trade")});
    }

    // Let the submitting client know what happened to their own order
    // (rested / filled / partially filled / no liquidity left).
    if (wantsAck) {
        const char* status = (acked.remainingQty > 1e-10)
            ? (acked.type == OrderType::LIMIT ? "resting" : "unfilled_remainder")
            : "filled";
        events.push_back({"client_" + acked.clientId, orderAckJson(acked, status)});
    }

    // Emit portfolio updates for clients involved in trades
    auto portEvents = onTrades(trades);
    for (auto& e : portEvents) events.push_back(e);

    return events;
}

void Exchange::cancelAllOrdersForClient(const std::string& clientId) {
    std::lock_guard<std::mutex> lock(mu_);
    book_.cancelAllForClient(clientId);
    // No event emission needed: the client that owned these orders is the
    // one that just disconnected, so there's nobody left to tell. The next
    // periodic "market" snapshot broadcast will reflect the smaller book.
}

// --------------------------------------------------------------------------
// Portfolio management
// --------------------------------------------------------------------------
Portfolio& Exchange::getOrCreate(const std::string& clientId) {
    auto it = portfolios_.find(clientId);
    if (it != portfolios_.end()) return it->second;
    Portfolio p;
    p.clientId = clientId;
    p.cash     = 100000.0;
    portfolios_[clientId] = p;
    return portfolios_[clientId];
}

void Exchange::updatePortfolio(Portfolio& p, double price, double qty, Side side) {
    if (side == Side::BID) {   // Bought qty units at price
        double cost = price * qty;
        if (p.holdings >= 0) {
            // Increase long position (weighted avg cost)
            double totalCost = p.avgCost * p.holdings + cost;
            p.holdings += qty;
            p.avgCost   = (p.holdings > 0) ? round2(totalCost / p.holdings) : 0.0;
        } else {
            // Covering a short
            double coveredQty = std::min(qty, -p.holdings);
            p.realizedPnL += (p.avgCost - price) * coveredQty;
            p.holdings    += qty;
            if (p.holdings > 1e-10) {
                // Flipped to long
                p.avgCost = price;
            } else if (std::fabs(p.holdings) < 1e-10) {
                p.holdings = 0.0;
                p.avgCost  = 0.0;
            }
        }
        p.cash -= cost;
    } else {   // Sold qty units at price
        double proceeds = price * qty;
        if (p.holdings > 0) {
            double soldQty = std::min(qty, p.holdings);
            p.realizedPnL += (price - p.avgCost) * soldQty;
            p.holdings    -= qty;
            if (p.holdings < -1e-10) {
                // Flipped to short
                p.avgCost = price;
            } else if (std::fabs(p.holdings) < 1e-10) {
                p.holdings = 0.0;
                p.avgCost  = 0.0;
            }
        } else {
            // Increasing short
            double totalCost = p.avgCost * (-p.holdings) + proceeds;
            p.holdings -= qty;
            p.avgCost   = (p.holdings < 0) ? round2(totalCost / (-p.holdings)) : 0.0;
        }
        p.cash += proceeds;
    }
    p.cash        = round2(p.cash);
    p.realizedPnL = round2(p.realizedPnL);
}

std::vector<Exchange::Event> Exchange::onTrades(const std::vector<Trade>& trades) {
    std::vector<Event> events;
    std::lock_guard<std::mutex> lock(mu_);

    std::set<std::string> updated;
    for (auto& t : trades) {
        // Skip simulator-side portfolio updates (performance)
        if (t.buyClientId != "simulator") {
            updatePortfolio(portfolios_[t.buyClientId],  t.price, t.quantity, Side::BID);
            updated.insert(t.buyClientId);
        }
        if (t.sellClientId != "simulator") {
            updatePortfolio(portfolios_[t.sellClientId], t.price, t.quantity, Side::ASK);
            updated.insert(t.sellClientId);
        }
    }
    for (auto& cid : updated) {
        Event e; e.topic = "portfolio_" + cid; e.json = portfolioToJson(portfolios_[cid]); events.push_back(std::move(e));
    }
    return events;
}

// --------------------------------------------------------------------------
// Candle aggregation
// --------------------------------------------------------------------------
void Exchange::updateCandle(double price, double qty) {
    int64_t bucketSec = nowSec();

    if (liveCandleTime_ != bucketSec) {
        // Close old candle, open new one
        if (liveCandl_.open > 0.0) {
            liveCandl_.time = liveCandleTime_;
            candles_.push_back(liveCandl_);
            if (candles_.size() > 300) candles_.pop_front();
        }
        liveCandl_ = {bucketSec, price, price, price, price, 0.0};
        liveCandleTime_ = bucketSec;
    }

    liveCandl_.high   = std::max(liveCandl_.high, price);
    liveCandl_.low    = std::min(liveCandl_.low, price);
    liveCandl_.close  = price;
    liveCandl_.volume += qty;
}

// --------------------------------------------------------------------------
// JSON serialisation
// --------------------------------------------------------------------------
static std::string jsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out;
}

std::string Exchange::tradeToJson(const Trade& t) const {
    std::ostringstream ss;
    ss << R"({"type":"trade","id":)" << t.id
       << R"(,"buyOrderId":)"  << t.buyOrderId
       << R"(,"sellOrderId":)" << t.sellOrderId
       << R"(,"price":)"    << dbl(t.price)
       << R"(,"qty":)"      << std::fixed << std::setprecision(4) << t.quantity
       << R"(,"buyClient":")" << jsonEscape(t.buyClientId)  << "\""
       << R"(,"sellClient":")" << jsonEscape(t.sellClientId) << "\""
       << R"(,"ts":)" << t.timestamp << "}";
    return ss.str();
}

std::string Exchange::candleToJson(const Candle& c, bool isLive) const {
    std::ostringstream ss;
    ss << R"({"type":"candle")"
       << R"(,"time":)"   << c.time
       << R"(,"open":)"   << dbl(c.open)
       << R"(,"high":)"   << dbl(c.high)
       << R"(,"low":)"    << dbl(c.low)
       << R"(,"close":)"  << dbl(c.close)
       << R"(,"volume":)" << std::fixed << std::setprecision(4) << c.volume
       << R"(,"live":)"   << (isLive ? "true" : "false") << "}";
    return ss.str();
}

std::string Exchange::portfolioToJson(const Portfolio& p) const {
    double lp  = lastPrice_.load();
    double unrl = (p.holdings != 0.0 && p.avgCost > 0.0)
        ? (lp - p.avgCost) * p.holdings
        : 0.0;
    std::ostringstream ss;
    ss << R"({"type":"portfolio")"
       << R"(,"cash":)"        << dbl(p.cash)
       << R"(,"holdings":)"    << std::fixed << std::setprecision(4) << p.holdings
       << R"(,"avgCost":)"     << dbl(p.avgCost)
       << R"(,"realizedPnL":)" << dbl(p.realizedPnL)
       << R"(,"unrealizedPnL":)" << dbl(unrl)
       << R"(,"totalValue":)"  << dbl(p.cash + p.holdings * lp) << "}";
    return ss.str();
}

std::string Exchange::bookLevelToJson(const OrderBookLevel& l) const {
    std::ostringstream ss;
    ss << "[" << dbl(l.price) << "," << std::fixed << std::setprecision(4) << l.qty << "]";
    return ss.str();
}

std::string Exchange::orderAckJson(const Order& order, const char* status) const {
    std::ostringstream ss;
    ss << R"({"type":"order_ack")"
       << R"(,"orderId":)"     << order.id
       << R"(,"side":")"       << (order.side == Side::BID ? "BID" : "ASK") << "\""
       << R"(,"orderType":")"  << (order.type == OrderType::LIMIT ? "LIMIT" : "MARKET") << "\""
       << R"(,"price":)"       << dbl(order.price)
       << R"(,"qty":)"         << std::fixed << std::setprecision(4) << order.quantity
       << R"(,"remainingQty":)"<< std::fixed << std::setprecision(4) << order.remainingQty
       << R"(,"status":")"     << status << "\"}";
    return ss.str();
}

std::string Exchange::orderCancelledJson(uint64_t orderId, const char* reason) const {
    std::ostringstream ss;
    ss << R"({"type":"order_cancelled","orderId":)" << orderId
       << R"(,"reason":")" << reason << "\"}";
    return ss.str();
}

std::string Exchange::errorJson(const std::string& message) const {
    std::ostringstream ss;
    ss << R"({"type":"error","message":")" << jsonEscape(message) << "\"}";
    return ss.str();
}

// --------------------------------------------------------------------------
// Public JSON endpoints
// --------------------------------------------------------------------------
std::string Exchange::getSnapshotJson() {
    std::lock_guard<std::mutex> lock(mu_);

    auto bids = book_.getBids(20);
    auto asks = book_.getAsks(20);
    double mid = book_.getMidPrice();

    std::ostringstream ss;
    ss << R"({"type":"snapshot","mid":)" << dbl(mid) << R"(,"ts":)" << nowMs();

    ss << R"(,"bids":[)";
    for (size_t i = 0; i < bids.size(); ++i) {
        if (i) ss << ",";
        ss << bookLevelToJson(bids[i]);
    }
    ss << "]";

    ss << R"(,"asks":[)";
    for (size_t i = 0; i < asks.size(); ++i) {
        if (i) ss << ",";
        ss << bookLevelToJson(asks[i]);
    }
    ss << "]}";
    return ss.str();
}

std::string Exchange::getPortfolioJson(const std::string& clientId) {
    std::lock_guard<std::mutex> lock(mu_);
    return portfolioToJson(getOrCreate(clientId));
}

std::string Exchange::getCandleHistoryJson() {
    std::lock_guard<std::mutex> lock(mu_);
    std::ostringstream ss;
    ss << R"({"type":"candle_history","candles":[)";
    bool first = true;
    for (auto& c : candles_) {
        if (!first) ss << ",";
        ss << candleToJson(c, false);
        first = false;
    }
    // Also push live candle
    if (liveCandl_.open > 0.0) {
        if (!first) ss << ",";
        Candle live = liveCandl_;
        live.time = liveCandleTime_;
        ss << candleToJson(live, true);
    }
    ss << "]}";
    return ss.str();
}

std::string Exchange::getLiveCandleJson() {
    std::lock_guard<std::mutex> lock(mu_);
    if (liveCandl_.open <= 0.0) return "";
    Candle live = liveCandl_;
    live.time = liveCandleTime_;
    return candleToJson(live, true);
}
