#pragma once
#include "Types.hpp"
#include <map>
#include <deque>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <mutex>
#include <functional>

// Emitted when a resting order is removed without trading because it would
// have crossed against another resting order from the *same* client
// (self-trade prevention).
struct StpCancel {
    uint64_t    orderId;
    std::string clientId;
};

class OrderBook {
public:
    struct MatchResult {
        std::vector<Trade>     trades;
        std::vector<StpCancel> stpCancels;
    };

    // Returns executed trades (may be empty) plus any self-trade cancels.
    MatchResult addLimitOrder(Order& order);
    MatchResult addMarketOrder(Order& order);

    // Cancels an order, but only if `clientId` actually owns it.
    // Returns false if the order doesn't exist or belongs to someone else.
    bool cancelOrder(uint64_t orderId, const std::string& clientId);

    // Cancels every resting order owned by `clientId` (e.g. on disconnect).
    // Returns the IDs that were cancelled.
    std::vector<uint64_t> cancelAllForClient(const std::string& clientId);

    // Snapshot accessors (thread-safe via external lock)
    std::vector<OrderBookLevel> getBids(int depth = 20) const;
    std::vector<OrderBookLevel> getAsks(int depth = 20) const;

    double getBestBid() const;
    double getBestAsk() const;
    double getMidPrice() const;

    mutable std::mutex mu;

private:
    // Bids: highest price first; Asks: lowest price first
    std::map<double, std::deque<Order>, std::greater<double>> bids_;
    std::map<double, std::deque<Order>>                       asks_;

    // orderId -> (side, price, owning clientId) for O(1) cancel lookup +
    // ownership verification.
    struct OrderLoc { Side side; double price; std::string clientId; };
    std::unordered_map<uint64_t, OrderLoc> idx_;

    // clientId -> set of resting order IDs, so we can cancel everything for
    // one client in O(open orders) instead of scanning the whole book.
    std::unordered_map<std::string, std::unordered_set<uint64_t>> byClient_;

    static uint64_t nextTradeId_;

    static int64_t nowMs();

    void trackResting(const Order& order);
    void untrackResting(uint64_t orderId, const std::string& clientId);
    bool removeOrderInternal(uint64_t orderId, const std::string& clientId);

    std::vector<Trade> matchBidAgainstAsks(Order& bid, std::vector<StpCancel>& stpOut);
    std::vector<Trade> matchAskAgainstBids(Order& ask, std::vector<StpCancel>& stpOut);
};
