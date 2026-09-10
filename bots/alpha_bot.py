"""
Alpha Bot — Directional Trading
================================
Strategy:
  1. Tracks a rolling window of trade prices.
  2. Computes SMA(10) and SMA(30) at each new trade.
  3. Generates signal on crossover:
       SMA10 crosses above SMA30 → BUY signal
       SMA10 crosses below SMA30 → SELL signal
  4. RSI(14) filter: only BUY when RSI < 65, only SELL when RSI > 35.
     (Avoids chasing overbought/oversold extremes.)
  5. On signal: flatten current position then open new one with MARKET order.
  6. Hard stop-loss: exit if unrealised P&L < -STOP_LOSS_USD.

Capital: $100,000 (enforced by exchange)
"""

import asyncio
import json
import logging
import math
import websockets
from collections import deque

from portfolio_logger import PortfolioLogger

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [ALPHA-BOT] %(message)s",
    datefmt="%H:%M:%S",
)
log = logging.getLogger(__name__)

import os

WS_URL        = os.environ.get("WS_URL", "ws://localhost:9001")
SMA_FAST      = 10
SMA_SLOW      = 30
RSI_PERIOD    = 14
ORDER_QTY     = 0.3       # units per trade
STOP_LOSS_USD = 500.0     # exit if unrealised loss > $500
MIN_PRICES    = SMA_SLOW + RSI_PERIOD + 5  # need enough history


# ------------------------------------------------------------------ #
# Indicators
# ------------------------------------------------------------------ #
def sma(prices: deque, n: int) -> float:
    if len(prices) < n:
        return 0.0
    return sum(list(prices)[-n:]) / n


def rsi(prices: deque, n: int = RSI_PERIOD) -> float:
    """Wilder RSI over last n+1 prices."""
    if len(prices) < n + 1:
        return 50.0
    pts = list(prices)[-(n + 1):]
    gains = [max(pts[i] - pts[i - 1], 0) for i in range(1, len(pts))]
    losses = [max(pts[i - 1] - pts[i], 0) for i in range(1, len(pts))]
    avg_gain = sum(gains) / n
    avg_loss = sum(losses) / n
    if avg_loss < 1e-10:
        return 100.0
    rs = avg_gain / avg_loss
    return 100.0 - 100.0 / (1.0 + rs)


# ------------------------------------------------------------------ #
class AlphaBot:
    def __init__(self):
        self.ws            = None
        self.client_id     = ""
        self.prices        = deque(maxlen=200)
        self.position      = 0.0      # units  (neg = short)
        self.avg_cost      = 0.0
        self.unrealised    = 0.0
        self.last_signal   = None     # "BUY" | "SELL" | None
        self.prev_fast     = 0.0
        self.prev_slow     = 0.0
        self.logger        = PortfolioLogger("alpha_bot")

    # ------------------------------------------------------------------ #
    async def send(self, msg: dict):
        await self.ws.send(json.dumps(msg))

    async def market_order(self, side: str, qty: float):
        log.info("→ MARKET %s %.4f", side, qty)
        await self.send({"type": "market_order", "side": side, "qty": round(qty, 6)})

    async def flatten(self):
        """Close any open position."""
        if abs(self.position) < 1e-6:
            return
        side = "ASK" if self.position > 0 else "BID"
        qty  = abs(self.position)
        log.info("FLATTEN position=%.4f via %s", self.position, side)
        await self.market_order(side, qty)

    # ------------------------------------------------------------------ #
    def _check_stoploss(self) -> bool:
        return self.unrealised < -STOP_LOSS_USD

    async def evaluate(self):
        """Called after each new price; computes indicators and acts."""
        if len(self.prices) < MIN_PRICES:
            return

        fast = sma(self.prices, SMA_FAST)
        slow = sma(self.prices, SMA_SLOW)
        r    = rsi(self.prices)

        # Detect crossover (need previous values)
        cross_up   = self.prev_fast <= self.prev_slow and fast > slow
        cross_down = self.prev_fast >= self.prev_slow and fast < slow

        self.prev_fast = fast
        self.prev_slow = slow

        # Stop-loss check
        if self._check_stoploss():
            log.warning("Stop-loss triggered! Unrealised P&L = %.2f", self.unrealised)
            await self.flatten()
            self.last_signal = None
            return

        # Signal logic
        if cross_up and r < 65 and self.last_signal != "BUY":
            log.info("BUY signal  fast=%.2f slow=%.2f RSI=%.1f", fast, slow, r)
            await self.flatten()
            await self.market_order("BID", ORDER_QTY)
            self.last_signal = "BUY"

        elif cross_down and r > 35 and self.last_signal != "SELL":
            log.info("SELL signal fast=%.2f slow=%.2f RSI=%.1f", fast, slow, r)
            await self.flatten()
            await self.market_order("ASK", ORDER_QTY)
            self.last_signal = "SELL"

    # ------------------------------------------------------------------ #
    def on_message(self, raw: str):
        msg = json.loads(raw)
        t   = msg.get("type", "")

        if t == "init":
            self.client_id = msg.get("clientId", "")
            log.info("Connected as %s", self.client_id)
            if "portfolio" in msg:
                self._apply_portfolio(msg["portfolio"])

        elif t == "trade":
            price = msg.get("price", 0.0)
            if price > 0:
                self.prices.append(price)

        elif t == "portfolio":
            self._apply_portfolio(msg)

    def _apply_portfolio(self, p: dict):
        self.position   = p.get("holdings",     self.position)
        self.avg_cost   = p.get("avgCost",       self.avg_cost)
        self.unrealised = p.get("unrealizedPnL", self.unrealised)
        self.logger.log(p)

    # ------------------------------------------------------------------ #
    async def run(self):
        while True:
            try:
                async with websockets.connect(WS_URL) as ws:
                    self.ws = ws
                    log.info("Connected to exchange at %s", WS_URL)

                    async def eval_loop():
                        while True:
                            await asyncio.sleep(0.5)
                            await self.evaluate()

                    eval_task = asyncio.create_task(eval_loop())

                    async for raw in ws:
                        self.on_message(raw)

                    eval_task.cancel()

            except Exception as e:
                log.warning("Disconnected: %s — reconnecting in 3s", e)
                await asyncio.sleep(3)


if __name__ == "__main__":
    asyncio.run(AlphaBot().run())
