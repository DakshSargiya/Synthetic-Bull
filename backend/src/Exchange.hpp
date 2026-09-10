#pragma once
#include "Types.hpp"
#include "OrderBook.hpp"
#include <map>
#include <vector>
#include <string>
#include <mutex>
#include <deque>
#include <atomic>

class Exchange {
public:
    Exchange();

    struct Event {
        std::string topic;   // "market", "portfolio_<connId>" or "client_<connId>"
        std::string json;
    };

    // Process one order (limit / market / cancel).
    // Returns events to broadcast via WebSocket.
    std::vector<Event> processOrder(Order order);

    // Cancels every resting order owned by clientId (e.g. on WS disconnect).
    // Safe to call from any thread — takes the exchange lock itself.
    void cancelAllOrdersForClient(const std::string& clientId);

    // Full snapshot (order book + last price). Safe to call anytime.
    std::string getSnapshotJson();

    // Portfolio for a given client (creates $100k account on first call).
    std::string getPortfolioJson(const std::string& clientId);

    // Historic candles (up to 300) for frontend init.
    std::string getCandleHistoryJson();

    // Live candle JSON for real-time broadcast.
    std::string getLiveCandleJson();

    double getLastPrice() const { return lastPrice_.load(); }

private:
    OrderBook book_;
    std::mutex mu_;

    std::map<std::string, Portfolio> portfolios_;
    std::deque<Candle>               candles_;   // last 300
    Candle                           liveCandl_; // current open candle
    int64_t                          liveCandleTime_ = 0;

    std::atomic<double>   lastPrice_{50000.0};
    std::atomic<uint64_t> nextOrderId_{1};

    // Trade → portfolio update → portfolio event
    std::vector<Event> onTrades(const std::vector<Trade>& trades);
    void updatePortfolio(Portfolio& p, double price, double qty, Side side);

    // Candle accumulation
    void updateCandle(double price, double qty);
    std::string candleToJson(const Candle& c, bool isLive) const;

    // JSON helpers
    std::string tradeToJson(const Trade& t) const;
    std::string portfolioToJson(const Portfolio& p) const;
    std::string bookLevelToJson(const OrderBookLevel& l) const;
    std::string orderAckJson(const Order& order, const char* status) const;
    std::string orderCancelledJson(uint64_t orderId, const char* reason) const;
    std::string errorJson(const std::string& message) const;

    Portfolio& getOrCreate(const std::string& clientId);
};
