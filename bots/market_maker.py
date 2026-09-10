"""
Market Maker Bot
================
Strategy:
  - Quotes a two-sided limit market continuously.
  - Spread = BASE_SPREAD + inventory_skew * SKEW_FACTOR
  - Inventory skew: if long, lower ask / raise bid to shed position; vice versa.
  - Cancels and re-quotes every QUOTE_INTERVAL seconds.
  - Hard stop if |inventory| > MAX_INVENTORY.

Capital: $100,000 (enforced by exchange)
"""

import asyncio
import json
import logging
import random
import websockets

from portfolio_logger import PortfolioLogger

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [MM-BOT] %(message)s",
    datefmt="%H:%M:%S",
)
log = logging.getLogger(__name__)

import os

WS_URL       = os.environ.get("WS_URL", "ws://localhost:9001")
QUOTE_INTERVAL  = 0.8   # re-quote every 800ms
BASE_SPREAD     = 8.0   # $8 half-spread around mid
SKEW_FACTOR     = 4.0   # per unit of inventory
MAX_INVENTORY   = 5.0   # units — stop quoting if exceeded
ORDER_QTY       = 0.5   # units per side


class MarketMakerBot:
    def __init__(self):
        self.ws          = None
        self.client_id   = ""
        self.mid         = 0.0
        self.inventory   = 0.0   # units (positive = long)
        self.cash        = 100_000.0
        self.active_bids: list[int] = []
        self.active_asks: list[int] = []
        self._order_id_ctr = 0
        self.logger = PortfolioLogger("market_maker")

    # ------------------------------------------------------------------ #
    # WebSocket helpers
    # ------------------------------------------------------------------ #
    async def send(self, msg: dict):
        await self.ws.send(json.dumps(msg))

    async def cancel_all(self):
        for oid in self.active_bids + self.active_asks:
            await self.send({"type": "cancel_order", "orderId": oid})
        self.active_bids.clear()
        self.active_asks.clear()

    # ------------------------------------------------------------------ #
    # Quoting logic
    # ------------------------------------------------------------------ #
    def _skewed_spread(self) -> tuple[float, float]:
        """Returns (bid_price, ask_price) adjusted for inventory skew."""
        skew     = self.inventory * SKEW_FACTOR   # positive inventory → widen ask
        half_bid = BASE_SPREAD + max(skew, 0)
        half_ask = BASE_SPREAD + max(-skew, 0)
        bid_px   = self.mid - half_bid
        ask_px   = self.mid + half_ask
        return round(bid_px, 2), round(ask_px, 2)

    async def requote(self):
        if self.mid <= 0:
            return

        await self.cancel_all()

        bid_px, ask_px = self._skewed_spread()

        # At/over the hard inventory cap: quote ONLY the side that reduces
        # the position, not both. (Previously this returned early and quoted
        # nothing at all once the cap was hit, which meant the bot could
        # never work its way back within limits — a long position that
        # reached MAX_INVENTORY stayed there, dead, for the rest of the
        # session, permanently exposed to unrealised P&L swings with zero
        # ability to trade out. Found live while capturing benchmark data:
        # see BENCHMARKS.md.)
        at_cap    = abs(self.inventory) >= MAX_INVENTORY
        quote_bid = not (at_cap and self.inventory > 0)   # long & at cap → stop buying more
        quote_ask = not (at_cap and self.inventory < 0)   # short & at cap → stop selling more

        if at_cap:
            log.warning("Inventory at cap (%.4f) — quoting only the reducing side", self.inventory)

        if quote_bid:
            await self.send({
                "type": "limit_order",
                "side": "BID",
                "price": bid_px,
                "qty":   ORDER_QTY,
            })
        if quote_ask:
            await self.send({
                "type": "limit_order",
                "side": "ASK",
                "price": ask_px,
                "qty":   ORDER_QTY,
            })
        log.info("Quoted  bid=%s  ask=%s  inv=%.4f  skew=%.2f",
                 f"{bid_px:.2f}" if quote_bid else "—",
                 f"{ask_px:.2f}" if quote_ask else "—",
                 self.inventory, self.inventory * SKEW_FACTOR)

    # ------------------------------------------------------------------ #
    # Message handling
    # ------------------------------------------------------------------ #
    def on_message(self, raw: str):
        msg = json.loads(raw)
        t   = msg.get("type", "")

        if t == "init":
            self.client_id = msg.get("clientId", "")
            log.info("Connected as %s", self.client_id)
            if "portfolio" in msg:
                self._apply_portfolio(msg["portfolio"])

        elif t == "snapshot":
            self.mid = msg.get("mid", self.mid)

        elif t == "portfolio":
            self._apply_portfolio(msg)

        elif t == "order_ack":
            # Record our own resting order IDs so cancel_all() can actually
            # cancel them next requote (previously these lists were never
            # populated, so every requote just piled up new orders on top
            # of the old ones instead of replacing them).
            if msg.get("status") == "resting":
                target = self.active_bids if msg.get("side") == "BID" else self.active_asks
                target.append(msg["orderId"])

        elif t == "order_cancelled":
            oid = msg.get("orderId")
            if oid in self.active_bids: self.active_bids.remove(oid)
            if oid in self.active_asks: self.active_asks.remove(oid)

        elif t == "trade":
            # Track own fills
            if msg.get("buyClient") == self.client_id:
                self.inventory += msg["qty"]
            if msg.get("sellClient") == self.client_id:
                self.inventory -= msg["qty"]

    def _apply_portfolio(self, p: dict):
        self.cash      = p.get("cash", self.cash)
        self.inventory = p.get("holdings", self.inventory)
        self.logger.log(p)

    # ------------------------------------------------------------------ #
    # Main loop
    # ------------------------------------------------------------------ #
    async def run(self):
        while True:
            try:
                async with websockets.connect(WS_URL) as ws:
                    self.ws = ws
                    log.info("Connected to exchange at %s", WS_URL)

                    # Start quote loop
                    async def quote_loop():
                        while True:
                            await asyncio.sleep(QUOTE_INTERVAL)
                            await self.requote()

                    quote_task = asyncio.create_task(quote_loop())

                    async for raw in ws:
                        self.on_message(raw)

                    quote_task.cancel()

            except Exception as e:
                log.warning("Disconnected: %s — reconnecting in 3s", e)
                await asyncio.sleep(3)


if __name__ == "__main__":
    asyncio.run(MarketMakerBot().run())
