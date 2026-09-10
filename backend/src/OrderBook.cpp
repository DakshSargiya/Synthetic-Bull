#include "OrderBook.hpp"
#include <algorithm>
#include <chrono>

uint64_t OrderBook::nextTradeId_ = 1;

int64_t OrderBook::nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

// ---------- bookkeeping helpers ----------

void OrderBook::trackResting(const Order& order) {
    idx_[order.id] = {order.side, order.price, order.clientId};
    byClient_[order.clientId].insert(order.id);
}

void OrderBook::untrackResting(uint64_t orderId, const std::string& clientId) {
    idx_.erase(orderId);
    auto it = byClient_.find(clientId);
    if (it != byClient_.end()) {
        it->second.erase(orderId);
        if (it->second.empty()) byClient_.erase(it);
    }
}

// ---------- matching helpers ----------
//
// Self-trade prevention: the synthetic-liquidity "simulator" client is
// exempt (its own crossing orders are how the GBM feed discovers price),
// but a real client (human or bot) can never trade against its own resting
// order. Instead of matching, the resting order is cancelled ("STP: cancel
// resting") and matching continues against the next order in the queue, so
// one self-crossing order can't block the book for everyone else.

std::vector<Trade> OrderBook::matchBidAgainstAsks(Order& bid, std::vector<StpCancel>& stpOut) {
    std::vector<Trade> trades;
    while (!asks_.empty() && bid.remainingQty > 1e-10) {
        auto it = asks_.begin();
        if (bid.type == OrderType::LIMIT && it->first > bid.price) break;

        auto& q = it->second;
        auto qit = q.begin();
        while (qit != q.end() && bid.remainingQty > 1e-10) {
            Order& passive = *qit;

            bool selfTrade = !bid.clientId.empty() && bid.clientId != "simulator"
                              && passive.clientId == bid.clientId;
            if (selfTrade) {
                stpOut.push_back({passive.id, passive.clientId});
                untrackResting(passive.id, passive.clientId);
                qit = q.erase(qit);
                continue;
            }

            double qty = std::min(bid.remainingQty, passive.remainingQty);

            Trade t;
            t.id           = nextTradeId_++;
            t.buyOrderId   = bid.id;
            t.sellOrderId  = passive.id;
            t.buyClientId  = bid.clientId;
            t.sellClientId = passive.clientId;
            t.price        = it->first;  // passive (resting) price
            t.quantity     = qty;
            t.timestamp    = nowMs();
            trades.push_back(t);

            bid.remainingQty     -= qty;
            passive.remainingQty -= qty;

            if (passive.remainingQty <= 1e-10) {
                untrackResting(passive.id, passive.clientId);
                qit = q.erase(qit);
            } else {
                ++qit;
            }
        }
        if (q.empty()) asks_.erase(it);
    }
    return trades;
}

std::vector<Trade> OrderBook::matchAskAgainstBids(Order& ask, std::vector<StpCancel>& stpOut) {
    std::vector<Trade> trades;
    while (!bids_.empty() && ask.remainingQty > 1e-10) {
        auto it = bids_.begin();
        if (ask.type == OrderType::LIMIT && it->first < ask.price) break;

        auto& q = it->second;
        auto qit = q.begin();
        while (qit != q.end() && ask.remainingQty > 1e-10) {
            Order& passive = *qit;

            bool selfTrade = !ask.clientId.empty() && ask.clientId != "simulator"
                              && passive.clientId == ask.clientId;
            if (selfTrade) {
                stpOut.push_back({passive.id, passive.clientId});
                untrackResting(passive.id, passive.clientId);
                qit = q.erase(qit);
                continue;
            }

            double qty = std::min(ask.remainingQty, passive.remainingQty);

            Trade t;
            t.id           = nextTradeId_++;
            t.buyOrderId   = passive.id;
            t.sellOrderId  = ask.id;
            t.buyClientId  = passive.clientId;
            t.sellClientId = ask.clientId;
            t.price        = it->first;
            t.quantity     = qty;
            t.timestamp    = nowMs();
            trades.push_back(t);

            ask.remainingQty     -= qty;
            passive.remainingQty -= qty;

            if (passive.remainingQty <= 1e-10) {
                untrackResting(passive.id, passive.clientId);
                qit = q.erase(qit);
            } else {
                ++qit;
            }
        }
        if (q.empty()) bids_.erase(it);
    }
    return trades;
}

// ---------- public API ----------

OrderBook::MatchResult OrderBook::addLimitOrder(Order& order) {
    MatchResult result;
    if (order.side == Side::BID) {
        result.trades = matchBidAgainstAsks(order, result.stpCancels);
        if (order.remainingQty > 1e-10) {
            bids_[order.price].push_back(order);
            trackResting(order);
        }
    } else {
        result.trades = matchAskAgainstBids(order, result.stpCancels);
        if (order.remainingQty > 1e-10) {
            asks_[order.price].push_back(order);
            trackResting(order);
        }
    }
    return result;
}

OrderBook::MatchResult OrderBook::addMarketOrder(Order& order) {
    MatchResult result;
    if (order.side == Side::BID) result.trades = matchBidAgainstAsks(order, result.stpCancels);
    else                          result.trades = matchAskAgainstBids(order, result.stpCancels);
    return result;
}

bool OrderBook::removeOrderInternal(uint64_t orderId, const std::string& clientId) {
    auto it = idx_.find(orderId);
    if (it == idx_.end()) return false;
    if (it->second.clientId != clientId) return false;  // ownership check

    auto [side, price, owner] = it->second;

    auto removeFrom = [&](auto& sideMap) {
        auto mapIt = sideMap.find(price);
        if (mapIt == sideMap.end()) return;
        auto& q = mapIt->second;
        q.erase(std::remove_if(q.begin(), q.end(),
            [orderId](const Order& o){ return o.id == orderId; }), q.end());
        if (q.empty()) sideMap.erase(mapIt);
    };

    if (side == Side::BID) removeFrom(bids_);
    else                    removeFrom(asks_);

    untrackResting(orderId, owner);
    return true;
}

bool OrderBook::cancelOrder(uint64_t orderId, const std::string& clientId) {
    return removeOrderInternal(orderId, clientId);
}

std::vector<uint64_t> OrderBook::cancelAllForClient(const std::string& clientId) {
    std::vector<uint64_t> cancelled;
    auto it = byClient_.find(clientId);
    if (it == byClient_.end()) return cancelled;

    // Copy first: removeOrderInternal mutates byClient_[clientId] as it goes.
    std::vector<uint64_t> ids(it->second.begin(), it->second.end());
    for (uint64_t id : ids) {
        if (removeOrderInternal(id, clientId)) cancelled.push_back(id);
    }
    return cancelled;
}

std::vector<OrderBookLevel> OrderBook::getBids(int depth) const {
    std::vector<OrderBookLevel> out;
    for (auto& [price, q] : bids_) {
        if ((int)out.size() >= depth) break;
        double total = 0;
        for (auto& o : q) total += o.remainingQty;
        out.push_back({price, total});
    }
    return out;
}

std::vector<OrderBookLevel> OrderBook::getAsks(int depth) const {
    std::vector<OrderBookLevel> out;
    for (auto& [price, q] : asks_) {
        if ((int)out.size() >= depth) break;
        double total = 0;
        for (auto& o : q) total += o.remainingQty;
        out.push_back({price, total});
    }
    return out;
}

double OrderBook::getBestBid() const {
    return bids_.empty() ? 0.0 : bids_.begin()->first;
}

double OrderBook::getBestAsk() const {
    return asks_.empty() ? 0.0 : asks_.begin()->first;
}

double OrderBook::getMidPrice() const {
    double b = getBestBid(), a = getBestAsk();
    if (b <= 0.0 || a <= 0.0) return 0.0;
    return (b + a) / 2.0;
}
