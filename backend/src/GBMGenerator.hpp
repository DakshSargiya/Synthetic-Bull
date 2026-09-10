#pragma once
#include "Types.hpp"
#include "ThreadSafeQueue.hpp"
#include <random>
#include <deque>
#include <atomic>
#include <cstdint>

class GBMGenerator {
public:
    explicit GBMGenerator(ThreadSafeQueue<Order>& queue, double s0 = 50000.0);

    // Called every 100ms from the GBM thread.
    // Generates new price and submits ~10 limit orders to queue.
    void tick();

    double currentPrice() const { return price_; }

private:
    ThreadSafeQueue<Order>& queue_;
    double price_;

    // GBM parameters (per-tick, not annualised)
    static constexpr double MU    = 0.0;
    static constexpr double SIGMA = 0.0006;  // ~0.06% per tick → ±$30 on $50k

    std::mt19937_64 rng_;
    std::normal_distribution<double> normalDist_{0.0, 1.0};
    std::uniform_real_distribution<double> spreadDist_;
    std::uniform_real_distribution<double> qtyDist_;

    std::atomic<uint64_t> nextId_{100'000'000};

    // Ring-buffer of simulator order IDs so we can cancel old ones
    struct SimOrder { uint64_t id; int tickNum; };
    std::deque<SimOrder> active_;
    int tickNum_ = 0;
    static constexpr int MAX_TICK_AGE = 8; // cancel after 8 ticks (~800ms)

    Order makeOrder(Side side, double price, double qty);
    uint64_t newId() { return nextId_++; }
};
