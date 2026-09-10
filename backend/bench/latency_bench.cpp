// Dependency-free micro-benchmark for the matching engine.
//
// Measures per-order processing latency (time inside OrderBook::addLimitOrder
// / addMarketOrder, single-threaded, no network/JSON/mutex overhead — i.e.
// the actual matching-logic cost) and sustained throughput, against an
// order flow shaped like the real GBM simulator + bot traffic: mostly
// resting limit orders clustered around a random-walking mid price from
// ~50 synthetic clients, with a market-order fraction that sweeps the book
// (as the alpha bot's flatten-then-market-order behaviour does) and a
// cancel fraction (as the market maker's re-quote cycle does).
//
// Build: g++ -std=c++17 -O2 -I../src latency_bench.cpp ../src/OrderBook.cpp -o latency_bench
// Run:   ./latency_bench [numOrders]
//
// This is a pure library-level benchmark of OrderBook — it does not spin up
// the WebSocket server, so numbers exclude network I/O, JSON
// (de)serialization, and the per-connection mutex in Exchange. Treat it as
// an upper bound on matching throughput, not an end-to-end figure.

#include "../src/OrderBook.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>

using Clock = std::chrono::steady_clock;

static double percentile(std::vector<double>& sorted, double p) {
    if (sorted.empty()) return 0.0;
    size_t idx = static_cast<size_t>(p * (sorted.size() - 1));
    return sorted[idx];
}

int main(int argc, char** argv) {
    const int N = argc > 1 ? std::atoi(argv[1]) : 200000;

    std::mt19937 rng(42);
    std::normal_distribution<double> walk(0.0, 3.0);
    std::uniform_real_distribution<double> spreadDist(0.5, 40.0);
    std::uniform_real_distribution<double> qtyDist(0.01, 2.0);
    std::uniform_int_distribution<int> clientDist(0, 49);      // 50 synthetic clients
    std::uniform_real_distribution<double> unit(0.0, 1.0);

    std::vector<std::string> clients;
    for (int i = 0; i < 50; ++i) clients.push_back("client_" + std::to_string(i));

    OrderBook book;
    std::vector<uint64_t> restingIds;   // for the cancel fraction
    restingIds.reserve(N);

    std::vector<double> latenciesUs;
    latenciesUs.reserve(N);

    double mid = 50000.0;
    uint64_t nextId = 1;

    auto t0 = Clock::now();

    for (int i = 0; i < N; ++i) {
        mid += walk(rng);
        if (mid < 100.0) mid = 100.0;

        double roll = unit(rng);
        Order order;
        order.id         = nextId++;
        order.clientId   = clients[clientDist(rng)];
        order.timestamp  = 0;

        Clock::time_point opStart;

        if (roll < 0.08 && !restingIds.empty()) {
            // 8%: cancel a previously-resting order (mirrors MM re-quote churn)
            uint64_t victim = restingIds[rng() % restingIds.size()];
            opStart = Clock::now();
            book.cancelOrder(victim, order.clientId);  // ownership may not match; timed regardless
            auto opEnd = Clock::now();
            latenciesUs.push_back(std::chrono::duration<double, std::micro>(opEnd - opStart).count());
            continue;
        } else if (roll < 0.20) {
            // 12%: market order sweeping the book (mirrors alpha-bot flatten/enter)
            order.type = OrderType::MARKET;
            order.side = (unit(rng) < 0.5) ? Side::BID : Side::ASK;
            order.quantity = order.remainingQty = qtyDist(rng) * 2.0;

            opStart = Clock::now();
            book.addMarketOrder(order);
            auto opEnd = Clock::now();
            latenciesUs.push_back(std::chrono::duration<double, std::micro>(opEnd - opStart).count());
            continue;
        }

        // 80%: resting limit order around mid (mirrors GBM feed + MM quotes)
        order.type = OrderType::LIMIT;
        bool isBid = unit(rng) < 0.5;
        order.side = isBid ? Side::BID : Side::ASK;
        double spread = spreadDist(rng);
        order.price = isBid ? mid - spread : mid + spread;
        order.quantity = order.remainingQty = qtyDist(rng);

        opStart = Clock::now();
        auto result = book.addLimitOrder(order);
        auto opEnd = Clock::now();
        latenciesUs.push_back(std::chrono::duration<double, std::micro>(opEnd - opStart).count());

        if (order.remainingQty > 1e-10) restingIds.push_back(order.id);  // it rested
    }

    auto t1 = Clock::now();
    double wallSeconds = std::chrono::duration<double>(t1 - t0).count();

    std::vector<double> sorted = latenciesUs;
    std::sort(sorted.begin(), sorted.end());
    double sum = 0.0;
    for (double v : sorted) sum += v;
    double mean = sorted.empty() ? 0.0 : sum / sorted.size();

    std::printf("Synthetic-Bull matching-engine benchmark\n");
    std::printf("=========================================\n");
    std::printf("Orders processed : %d\n", N);
    std::printf("Wall time        : %.3f s\n", wallSeconds);
    std::printf("Throughput       : %.0f ops/sec (single-threaded, library-level)\n", N / wallSeconds);
    std::printf("\nPer-operation latency (limit add / market sweep / cancel combined):\n");
    std::printf("  mean : %.3f us\n", mean);
    std::printf("  p50  : %.3f us\n", percentile(sorted, 0.50));
    std::printf("  p95  : %.3f us\n", percentile(sorted, 0.95));
    std::printf("  p99  : %.3f us\n", percentile(sorted, 0.99));
    std::printf("  p99.9: %.3f us\n", percentile(sorted, 0.999));
    std::printf("  max  : %.3f us\n", sorted.empty() ? 0.0 : sorted.back());
    std::printf("\nFinal book: best bid=%.2f best ask=%.2f mid=%.2f\n",
                 book.getBestBid(), book.getBestAsk(), book.getMidPrice());
    return 0;
}
