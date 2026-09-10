// Dependency-free tests for the matching engine's core invariants:
// price-time priority, partial fills, cancel ownership, and self-trade
// prevention. No test framework required — just g++.
//
//   g++ -std=c++17 -I../src orderbook_tests.cpp ../src/OrderBook.cpp -o orderbook_tests
//   ./orderbook_tests
//
// or simply: ./run_tests.sh

#include "../src/OrderBook.hpp"
#include <iostream>
#include <cmath>

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, msg)                                                     \
    do {                                                                     \
        if (cond) { ++g_pass; }                                              \
        else {                                                               \
            ++g_fail;                                                        \
            std::cerr << "FAIL: " << msg << "  (" << #cond << ")"            \
                       << "  [" << __FILE__ << ":" << __LINE__ << "]\n";     \
        }                                                                    \
    } while (0)

static bool approxEq(double a, double b, double eps = 1e-9) {
    return std::fabs(a - b) < eps;
}

static Order makeLimit(uint64_t id, const std::string& client, Side side,
                        double price, double qty) {
    Order o;
    o.id           = id;
    o.clientId     = client;
    o.type         = OrderType::LIMIT;
    o.side         = side;
    o.price        = price;
    o.quantity     = qty;
    o.remainingQty = qty;
    return o;
}

static Order makeMarket(uint64_t id, const std::string& client, Side side, double qty) {
    Order o;
    o.id           = id;
    o.clientId     = client;
    o.type         = OrderType::MARKET;
    o.side         = side;
    o.quantity     = qty;
    o.remainingQty = qty;
    return o;
}

// ---------------------------------------------------------------------------
static void test_price_priority() {
    OrderBook book;
    // Two resting asks; a marketable bid should hit the cheaper one first
    // even though it was placed second.
    Order ask1 = makeLimit(1, "maker_a", Side::ASK, 101.0, 1.0);
    Order ask2 = makeLimit(2, "maker_b", Side::ASK, 100.0, 1.0);
    book.addLimitOrder(ask1);
    book.addLimitOrder(ask2);

    Order bid = makeLimit(3, "taker", Side::BID, 101.0, 1.0);
    auto mr = book.addLimitOrder(bid);

    CHECK(mr.trades.size() == 1, "price priority: exactly one trade");
    if (!mr.trades.empty()) {
        CHECK(approxEq(mr.trades[0].price, 100.0), "price priority: filled at the best (lowest) ask");
        CHECK(mr.trades[0].sellClientId == "maker_b", "price priority: matched the cheaper resting order");
    }
}

static void test_time_priority_fifo() {
    OrderBook book;
    // Two resting bids at the SAME price; an incoming ask should fill the
    // one that arrived first.
    Order bid1 = makeLimit(1, "maker_a", Side::BID, 100.0, 1.0);
    Order bid2 = makeLimit(2, "maker_b", Side::BID, 100.0, 1.0);
    book.addLimitOrder(bid1);
    book.addLimitOrder(bid2);

    Order ask = makeLimit(3, "taker", Side::ASK, 100.0, 1.0);
    auto mr = book.addLimitOrder(ask);

    CHECK(mr.trades.size() == 1, "time priority: exactly one trade");
    if (!mr.trades.empty()) {
        CHECK(mr.trades[0].buyClientId == "maker_a", "time priority: FIFO — first order in fills first");
    }
}

static void test_partial_fill_rests_remainder() {
    OrderBook book;
    Order ask = makeLimit(1, "maker", Side::ASK, 100.0, 5.0);
    book.addLimitOrder(ask);

    Order bid = makeLimit(2, "taker", Side::BID, 100.0, 2.0);
    auto mr = book.addLimitOrder(bid);

    CHECK(mr.trades.size() == 1, "partial fill: one trade executes");
    if (!mr.trades.empty()) CHECK(approxEq(mr.trades[0].quantity, 2.0), "partial fill: trade qty == taker qty");

    auto asks = book.getAsks(5);
    CHECK(asks.size() == 1, "partial fill: resting order still present");
    if (!asks.empty()) CHECK(approxEq(asks[0].qty, 3.0), "partial fill: remainder (5 - 2) still resting");
}

static void test_cancel_requires_ownership() {
    OrderBook book;
    Order bid = makeLimit(1, "alice", Side::BID, 100.0, 1.0);
    book.addLimitOrder(bid);

    bool rejectedForWrongOwner = !book.cancelOrder(1, "mallory");
    CHECK(rejectedForWrongOwner, "cancel ownership: a different client cannot cancel alice's order");

    auto bidsAfterRejected = book.getBids(5);
    CHECK(bidsAfterRejected.size() == 1, "cancel ownership: order still resting after rejected cancel");

    bool acceptedForOwner = book.cancelOrder(1, "alice");
    CHECK(acceptedForOwner, "cancel ownership: the actual owner can cancel");

    auto bidsAfterOwnerCancel = book.getBids(5);
    CHECK(bidsAfterOwnerCancel.empty(), "cancel ownership: order removed after owner's cancel");
}

static void test_self_trade_prevention() {
    OrderBook book;
    // alice has a resting ask; her own incoming bid must NOT trade against
    // it — the resting order should be cancelled instead, and matching
    // should continue to bob's resting ask behind it at the same price.
    Order aliceAsk = makeLimit(1, "alice", Side::ASK, 100.0, 1.0);
    Order bobAsk   = makeLimit(2, "bob",   Side::ASK, 100.0, 1.0);
    book.addLimitOrder(aliceAsk);
    book.addLimitOrder(bobAsk);

    Order aliceBid = makeLimit(3, "alice", Side::BID, 100.0, 1.0);
    auto mr = book.addLimitOrder(aliceBid);

    CHECK(mr.stpCancels.size() == 1, "self-trade prevention: alice's own resting ask was pulled");
    if (!mr.stpCancels.empty()) {
        CHECK(mr.stpCancels[0].orderId == 1, "self-trade prevention: the cancelled order is alice's ask");
        CHECK(mr.stpCancels[0].clientId == "alice", "self-trade prevention: cancel event names the right client");
    }
    CHECK(mr.trades.size() == 1, "self-trade prevention: alice's bid still fills against bob");
    if (!mr.trades.empty()) {
        CHECK(mr.trades[0].sellClientId == "bob", "self-trade prevention: matched bob, not alice");
    }

    auto remaining = book.getAsks(5);
    CHECK(remaining.empty(), "self-trade prevention: no resting asks left (alice's cancelled, bob's filled)");
}

static void test_simulator_can_cross_itself() {
    // The synthetic liquidity feed is exempt from STP — its own crossing
    // orders are how price discovery happens.
    OrderBook book;
    Order simAsk = makeLimit(1, "simulator", Side::ASK, 100.0, 1.0);
    book.addLimitOrder(simAsk);

    Order simBid = makeLimit(2, "simulator", Side::BID, 100.0, 1.0);
    auto mr = book.addLimitOrder(simBid);

    CHECK(mr.trades.size() == 1, "simulator exemption: simulator orders can still trade against each other");
    CHECK(mr.stpCancels.empty(), "simulator exemption: no STP cancel for the simulator's own liquidity");
}

static void test_cancel_all_for_client() {
    OrderBook book;
    Order a1 = makeLimit(1, "alice", Side::BID, 99.0,  1.0);
    Order a2 = makeLimit(2, "alice", Side::ASK, 105.0, 1.0);
    Order b1 = makeLimit(3, "bob",   Side::BID, 98.0,  1.0);
    book.addLimitOrder(a1);
    book.addLimitOrder(a2);
    book.addLimitOrder(b1);

    auto cancelled = book.cancelAllForClient("alice");
    CHECK(cancelled.size() == 2, "cancelAllForClient: both of alice's orders cancelled");

    auto bids = book.getBids(10);
    auto asks = book.getAsks(10);
    CHECK(asks.empty(), "cancelAllForClient: alice's ask is gone");
    CHECK(bids.size() == 1, "cancelAllForClient: bob's bid untouched");
    if (!bids.empty()) CHECK(approxEq(bids[0].price, 98.0), "cancelAllForClient: bob's bid price unchanged");
}

static void test_market_order_sweeps_book() {
    OrderBook book;
    Order ask1 = makeLimit(1, "maker_a", Side::ASK, 100.0, 1.0);
    Order ask2 = makeLimit(2, "maker_b", Side::ASK, 101.0, 1.0);
    book.addLimitOrder(ask1);
    book.addLimitOrder(ask2);

    Order mkt = makeMarket(3, "taker", Side::BID, 1.5);
    auto mr = book.addMarketOrder(mkt);

    double filled = 0;
    for (auto& t : mr.trades) filled += t.quantity;
    CHECK(approxEq(filled, 1.5), "market order: sweeps across price levels until filled");
    CHECK(approxEq(mkt.remainingQty, 0.0), "market order: fully filled, nothing left over");
}

// ---------------------------------------------------------------------------
int main() {
    test_price_priority();
    test_time_priority_fifo();
    test_partial_fill_rests_remainder();
    test_cancel_requires_ownership();
    test_self_trade_prevention();
    test_simulator_can_cross_itself();
    test_cancel_all_for_client();
    test_market_order_sweeps_book();

    std::cout << g_pass << " passed, " << g_fail << " failed\n";
    return g_fail == 0 ? 0 : 1;
}
