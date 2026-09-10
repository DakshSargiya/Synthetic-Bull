# Synthetic-Bull
 
> Full-Stack Exchange Simulator & Algorithmic Trading Terminal
---

## Quick Start

```bash
git clone <repo-url>
cd synthetic-bull
```

**Terminal 1 — C++ Backend** (requires g++17, git, libssl-dev, zlib1g-dev):
```bash
cd backend
./build.sh        # fetches deps + compiles on first run
./synthetic-bull
```

**Terminal 2 — React Frontend:**
```bash
cd frontend
npm install && npm run dev
```

**Terminal 3 — Python Bots:**
```bash
cd bots
python3 -m venv .venv && source .venv/bin/activate
pip install websockets
python market_maker.py &
python alpha_bot.py &
```

Open **http://localhost:3000** — the terminal will connect automatically.

### Docker (all services)

```bash
docker compose up --build           # backend :9001 + frontend :3000
docker compose --profile bots up --build   # also start the two bots
```

The frontend image is built with `VITE_WS_URL=/ws`; `frontend/nginx.conf`
proxies that path to the `backend` container, so the browser never needs a
direct route to `:9001`. If you build the frontend image yourself outside
docker-compose, set that build arg/env var or the WebSocket connection will
try (and fail) to reach `localhost:9001` directly from the browser.

---

## Architecture

```
┌─────────────────────────────────────────────────────┐
│              synthetic-bull services                │
│                                                     │
│  ┌───────────────────────────────────────────────┐  │
│  │   C++ Backend  :9001                          │  │
│  │                                               │  │
│  │  ┌────────────┐    ┌───────────────────────┐  │  │
│  │  │ GBM Thread │───▶│  Thread-safe Queue    │  │  │
│  │  │ 10 ticks/s │    └──────────┬────────────┘  │  │
│  │  └────────────┘               │               │  │
│  │                               ▼               │  │
│  │              ┌────────────────────────────┐   │  │
│  │              │   Matching Engine (LOB)    │   │  │
│  │              │   Price-Time Priority      │   │  │
│  │              └──────────┬─────────────────┘   │  │
│  │                         │                     │  │
│  │          ┌──────────────┼──────────────┐      │  │
│  │          ▼              ▼              ▼      │  │
│  │   Portfolio Mgr   Candle Agg    Broadcast Hub │  │
│  └───────────────────────────────────────────────┘  │
│           │ WebSocket (topic pub/sub)               │
│    ┌──────┼──────────┬────────────────┐             │
│    ▼      ▼          ▼                ▼             │
│  React  Market     Alpha            ...             │
│  :3000  Maker      Bot                              │
└─────────────────────────────────────────────────────┘
```

### Module 1 — Core Matching Engine (C++)

**Files:** `backend/src/OrderBook.{hpp,cpp}`, `Exchange.{hpp,cpp}`, `main.cpp`

The LOB is implemented as:
- `std::map<double, std::deque<Order>, std::greater<double>>` for bids (highest-first)
- `std::map<double, std::deque<Order>>` for asks (lowest-first)
- `std::unordered_map<uint64_t, {side, price}>` index for O(1) cancel lookup

**Price-Time priority:** `map` iteration always visits the best price level first; within a level, `deque` maintains FIFO (time) order.

**Supported message types:**
| Type | Behaviour |
|------|-----------|
| `LIMIT` | Rests in book if not immediately matchable; partial fills leave remainder |
| `MARKET` | Sweeps opposite side until filled or book exhausted |
| `CANCEL` | O(1) lookup via index map + **ownership check**, then O(k) removal from deque |

**Self-trade prevention:** a client can never fill against its own resting
order. If a new order would cross one of the same client's own resting
orders, that resting order is cancelled ("cancel resting" policy) instead of
trading, and matching continues against the next order in the book. The
synthetic-liquidity `simulator` client is exempt — its own crossing orders
are how the GBM feed discovers price.

**Disconnects:** when a WebSocket connection closes, every resting order it
still owned is cancelled automatically, so a dropped client can't leave
permanently-orphaned liquidity in the book.

**Input limits:** `main.cpp` rejects orders with price/qty outside sane
bounds (`price ∈ (0, 10,000,000]`, `qty ∈ (0, 100,000]`) and applies a
per-connection token-bucket rate limit (burst 40, refill 20 msg/s) before a
message ever reaches the matching engine.

### Module 2 — Synthetic Market Generator (GBM)

**File:** `backend/src/GBMGenerator.{hpp,cpp}`

The discrete GBM step applied every 100 ms:

```
S(t+dt) = S(t) * exp( (μ - σ²/2)·dt + σ·Z )
Z ~ N(0,1)
```

Parameters:
| Symbol | Value | Meaning |
|--------|-------|---------|
| μ      | 0.0   | Zero drift (fair price) |
| σ      | 0.0006 | ~0.06% per tick (≈$30 on $50k) |
| soft mean-reversion | 0.1% | Nudge toward $50k to prevent runaway |

Each tick submits **5 bids + 5 asks** = **100 limit orders/second** into the matching engine.  
Old simulator orders are cancelled after 8 ticks (~800 ms) to prevent book clutter.

### Module 3 — Quant Bots (Python / asyncio)

#### Market Maker Bot (`bots/market_maker.py`)

**Objective:** Capture the bid-ask spread while managing inventory risk.

```
spread_half = BASE_SPREAD + |inventory| × SKEW_FACTOR
bid = mid - spread_half_bid    (widened if long, to shed position)
ask = mid + spread_half_ask    (widened if short, to shed position)
```

Parameters: `BASE_SPREAD=8`, `SKEW_FACTOR=4`, `ORDER_QTY=0.5`, `MAX_INVENTORY=5`  
Re-quotes every 800 ms after cancelling previous quotes. At/over
`MAX_INVENTORY` it quotes only the inventory-reducing side rather than
going silent (see [`BENCHMARKS.md`](BENCHMARKS.md) for why that fix exists
and what the bot's real P&L looks like against this exchange).

#### Alpha Bot (`bots/alpha_bot.py`)

**Objective:** Directional trend-following via crossover + RSI filter.

```
Signal BUY:  SMA(10) crosses above SMA(30) AND RSI(14) < 65
Signal SELL: SMA(10) crosses below SMA(30) AND RSI(14) > 35
```

On signal: flatten existing position, then open new position via market order.  
Hard stop-loss: exit if unrealised P&L < −$500.

---

## Service Map

| Service | Port | Tech |
|---------|------|------|
| `backend` | 9001 | C++17 + uWebSockets + nlohmann/json |
| `frontend` | 3000 | Vite + React 18 + TypeScript + lightweight-charts v4 |
| `market-maker` | — | Python 3.12 + websockets |
| `alpha-bot` | — | Python 3.12 + websockets |

---

## WebSocket Protocol

All messages are JSON over a single persistent WebSocket connection to `:9001`.

**Client → Server:**
```json
{ "type": "limit_order",  "side": "BID", "price": 50000.0, "qty": 0.1 }
{ "type": "market_order", "side": "ASK", "qty": 0.5 }
{ "type": "cancel_order", "orderId": 12345 }
```

**Server → Client (topics):**
| Topic | Message types |
|-------|--------------|
| `market` | `snapshot`, `trade`, `candle` |
| `portfolio_{connId}` | `portfolio` |
| `client_{connId}` | `order_ack`, `order_cancelled`, `error` (this client's own order lifecycle) |
| _(on connect)_ | `init` (includes history + portfolio) |

**`client_{connId}` messages** — every connection is auto-subscribed to its
own topic on open, so the frontend can track its own resting orders without
polling:

```json
{ "type": "order_ack", "orderId": 5, "side": "BID", "orderType": "LIMIT",
  "price": 50000.0, "qty": 0.1, "remainingQty": 0.1, "status": "resting" }
// status is one of: "resting" | "filled" | "unfilled_remainder" (market
// order that swept the book without fully filling)

{ "type": "order_cancelled", "orderId": 5, "reason": "user" }
// reason is one of: "user" (client-initiated cancel) | "self_trade" (self-trade prevention)

{ "type": "error", "message": "cancel rejected: order not found or not owned by you" }
```

`trade` messages also carry `buyOrderId`/`sellOrderId` so the client can
match a fill back to one of its own tracked orders.

---

## Capital Rules

- All accounts start with **$100,000 USD**
- **Short selling permitted** (negative holdings)
- Portfolio tracks: cash, holdings, avg cost, realised P&L, unrealised P&L, total value

---

## Development

**Backend** (requires g++17, git, libssl-dev, zlib1g-dev):
```bash
cd backend
./build.sh        # fetches deps + compiles on first run
./synthetic-bull
```

**Frontend:**
```bash
cd frontend && npm install && npm run dev
```

**Bots** (connects to `ws://localhost:9001` by default):
```bash
cd bots
python3 -m venv .venv && source .venv/bin/activate
pip install websockets
python market_maker.py &
python alpha_bot.py &
```

---

## Testing

Dependency-free unit tests for the matching engine (price-time priority,
partial fills, cancel ownership, self-trade prevention, market-order
sweeps):

```bash
cd backend/tests
./run_tests.sh
```

These test `OrderBook` in isolation (no WebSocket, no GBM feed), which is
also the only reliable way to assert on self-trade prevention deterministically —
against the live server, a manually-placed order competes with the GBM
simulator's own continuous order flow.

Runs automatically on every push/PR via GitHub Actions —
[`.github/workflows/ci.yml`](.github/workflows/ci.yml) builds the backend,
runs this suite, and type-checks + builds the frontend.

## Performance & Benchmarks

Matching-engine latency/throughput (dependency-free micro-benchmark) and
real P&L/risk stats captured from a live bot session — equity curve, Sharpe,
max drawdown, win rate, and a bug the benchmark itself caught — are in
[`BENCHMARKS.md`](BENCHMARKS.md). Short version: the matching engine handles
~1.35M single-threaded ops/sec at sub-microsecond median latency; the
momentum bot was roughly flat over a 6.5-minute session, and the market
maker lost 11% to adverse selection at its default (too-tight, too-slow)
quoting parameters — both reported as measured, not tuned to look good.

## Known Limitations

- **Money is `double`, not fixed-point.** Every mutation to cash / avgCost /
  realizedPnL is rounded to the nearest cent to bound floating-point drift,
  but this is a mitigation, not a fix. A real rewrite to integer cents (or a
  decimal type) would also touch the order book's price-map keys, the wire
  JSON precision, the GBM generator, and both bot scripts — a larger,
  higher-risk change than fits alongside the fixes above.
- **No authentication or persistence.** Portfolios are keyed by an ephemeral
  per-connection ID; refreshing the page starts a new $100k account. Adding
  real accounts is a product decision (login flow, storage, session
  semantics) rather than a bug fix, so it's left as a recommendation.
- **Single instrument, in-memory only.** No multi-symbol support, and all
  state is lost on restart — acceptable for a simulator, but worth knowing
  before treating this as a template for anything real.
- **The market maker's default parameters lose money against this exchange's
  order flow** (see `BENCHMARKS.md`) — `BASE_SPREAD=8` / `QUOTE_INTERVAL=0.8s`
  aren't wide/fast enough to cover adverse selection from the GBM feed and
  the momentum bot's market orders. The inventory-cap deadlock this
  benchmarking work found has been fixed; the underlying profitability gap
  is a strategy-tuning problem, not a bug, and is left open intentionally as
  a next step rather than hand-tuned away.

## Evaluation Checklist

- [x] In-memory LOB with Price-Time priority
- [x] GBM price simulator — 100% locally generated, zero external APIs
- [x] 50–100 order messages/second sustained load
- [x] WebSocket real-time streaming (uWebSockets pub/sub)
- [x] Candlestick chart (1-second buckets, lightweight-charts)
- [x] Order book depth visualisation (bid/ask bars)
- [x] Human order panel (limit + market, quick-fill buttons)
- [x] Portfolio widget (cash, holdings, realised + unrealised P&L)
- [x] Market maker bot (spread capture, inventory skew)
- [x] Alpha bot (SMA crossover + RSI filter + stop-loss)
- [x] Easy multi-service launch
- [x] All capital rules: $100k start, shorts permitted
- [x] Matching-engine latency/throughput benchmark + CI build (see `BENCHMARKS.md`, `.github/workflows/ci.yml`)
