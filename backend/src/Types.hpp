#pragma once
#include <cstdint>
#include <string>

enum class Side { BID, ASK };
enum class OrderType { LIMIT, MARKET, CANCEL };
enum class OrderStatus { PENDING, PARTIAL, FILLED, CANCELLED };

struct Order {
    uint64_t id        = 0;
    std::string clientId;
    Side side          = Side::BID;
    OrderType type     = OrderType::LIMIT;
    double price       = 0.0;
    double quantity    = 0.0;
    double remainingQty= 0.0;
    int64_t timestamp  = 0;   // ms since epoch
    OrderStatus status = OrderStatus::PENDING;
};

struct Trade {
    uint64_t id          = 0;
    uint64_t buyOrderId  = 0;
    uint64_t sellOrderId = 0;
    std::string buyClientId;
    std::string sellClientId;
    double price    = 0.0;
    double quantity = 0.0;
    int64_t timestamp = 0;
};

struct Candle {
    int64_t time   = 0;   // unix seconds (bucket start)
    double open    = 0.0;
    double high    = 0.0;
    double low     = 0.0;
    double close   = 0.0;
    double volume  = 0.0;
};

struct Portfolio {
    std::string clientId;
    double cash        = 100000.0;
    double holdings    = 0.0;   // negative = short
    double avgCost     = 0.0;
    double realizedPnL = 0.0;
};

struct OrderBookLevel {
    double price = 0.0;
    double qty   = 0.0;
};
