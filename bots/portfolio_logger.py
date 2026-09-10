"""
Portfolio Logger
================
Shared by both bots. Appends one row per `portfolio` update to a CSV so a
live trading session can be turned into an equity curve + risk stats after
the fact (see `analyze_session.py`).

Columns: wall_clock_iso, t_seconds, cash, holdings, avgCost, realizedPnL,
         unrealizedPnL, totalValue

Enabled by default; disable with LOG_PORTFOLIO=0. Output path defaults to
logs/<bot_name>_portfolio.csv (relative to cwd) and can be overridden with
LOG_PATH.
"""

import csv
import os
import time
from datetime import datetime, timezone

_ENABLED = os.environ.get("LOG_PORTFOLIO", "1") != "0"

_HEADER = [
    "wall_clock_iso", "t_seconds", "cash", "holdings", "avgCost",
    "realizedPnL", "unrealizedPnL", "totalValue",
]


class PortfolioLogger:
    def __init__(self, bot_name: str):
        self.enabled = _ENABLED
        self._t0 = time.monotonic()
        self._path = os.environ.get("LOG_PATH", f"logs/{bot_name}_portfolio.csv")
        if not self.enabled:
            return
        os.makedirs(os.path.dirname(self._path) or ".", exist_ok=True)
        self._fh = open(self._path, "w", newline="")
        self._writer = csv.writer(self._fh)
        self._writer.writerow(_HEADER)
        self._fh.flush()

    def log(self, p: dict):
        """p is the raw portfolio dict as received from the exchange."""
        if not self.enabled:
            return
        row = [
            datetime.now(timezone.utc).isoformat(),
            round(time.monotonic() - self._t0, 3),
            p.get("cash", ""),
            p.get("holdings", ""),
            p.get("avgCost", ""),
            p.get("realizedPnL", ""),
            p.get("unrealizedPnL", ""),
            p.get("totalValue", ""),
        ]
        self._writer.writerow(row)
        self._fh.flush()  # session runs are short-lived; durability > throughput here

    def close(self):
        if self.enabled and not self._fh.closed:
            self._fh.close()
