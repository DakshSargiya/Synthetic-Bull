#include "GBMGenerator.hpp"
#include <chrono>
#include <cmath>

GBMGenerator::GBMGenerator(ThreadSafeQueue<Order>& queue, double s0)
    : queue_(queue)
    , price_(s0)
    , rng_(std::random_device{}())
    , spreadDist_(0.5, 60.0)
    , qtyDist_(0.01, 3.0)
{}

// ---------------------------------------------------------------------------
// Discrete GBM step:
//   S(t+dt) = S(t) * exp( (mu - sigma^2/2) + sigma * Z )
//   Z ~ N(0,1)
// ---------------------------------------------------------------------------
void GBMGenerator::tick() {
    ++tickNum_;

    // --- 1. Step the GBM price ---
    double z     = normalDist_(rng_);
    double drift = MU - 0.5 * SIGMA * SIGMA;
    price_ *= std::exp(drift + SIGMA * z);

    // Soft mean-reversion: nudge toward $50k if we drift too far
    constexpr double S0 = 50000.0;
    price_ += 0.001 * (S0 - price_);

    // --- 2. Cancel orders older than MAX_TICK_AGE ticks ---
    while (!active_.empty() && active_.front().tickNum < tickNum_ - MAX_TICK_AGE) {
        Order cancel;
        cancel.type     = OrderType::CANCEL;
        cancel.id       = active_.front().id;
        cancel.clientId = "simulator";
        queue_.push(cancel);
        active_.pop_front();
    }

    // --- 3. Submit fresh limit orders around current price ---
    //   5 bids + 5 asks per tick  →  100 msgs/sec at 10 ticks/sec
    static constexpr int ORDERS_PER_SIDE = 5;

    for (int i = 0; i < ORDERS_PER_SIDE; ++i) {
        double spread = spreadDist_(rng_);
        double qty    = qtyDist_(rng_);

        // Bid below mid
        Order bid = makeOrder(Side::BID, price_ - spread, qty);
        active_.push_back({bid.id, tickNum_});
        queue_.push(bid);

        // Ask above mid
        Order ask = makeOrder(Side::ASK, price_ + spread, qty);
        active_.push_back({ask.id, tickNum_});
        queue_.push(ask);
    }
}

Order GBMGenerator::makeOrder(Side side, double price, double qty) {
    Order o;
    o.id           = newId();
    o.clientId     = "simulator";
    o.side         = side;
    o.type         = OrderType::LIMIT;
    o.price        = price > 0.0 ? price : 1.0;
    o.quantity     = qty;
    o.remainingQty = qty;
    o.timestamp    = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::system_clock::now().time_since_epoch()
                     ).count();
    o.status       = OrderStatus::PENDING;
    return o;
}
