"""
Session Analyzer
=================
Turns the CSVs written by `portfolio_logger.py` into an equity-curve chart
plus a small set of standard risk/performance stats:

  - Total return ($, %)
  - Max drawdown ($, %) on the equity (totalValue) curve
  - Sharpe ratio, annualized from per-snapshot returns
    (snapshots are irregular — event-driven, one row per trade/fill — so we
    resample the equity curve to a fixed grid before computing returns; see
    RESAMPLE_SECONDS below)
  - Win rate: fraction of realized-P&L-changing events that were profitable
    (a "trade" here = any row where realizedPnL moved vs. the previous row)

Usage:
    python3 analyze_session.py logs/market_maker_portfolio.csv logs/alpha_bot_portfolio.csv

Writes:
    logs/equity_curve.png   - overlaid equity curves for every CSV passed in
    logs/session_report.md  - the numbers, as a markdown table, ready to
                               paste into BENCHMARKS.md
"""

import sys
import os
import math
import pandas as pd
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

RESAMPLE_SECONDS = 1.0
RISK_FREE = 0.0  # paper-trading sandbox; no meaningful risk-free rate to net out


def load(path: str) -> pd.DataFrame:
    df = pd.read_csv(path)
    df["t_seconds"] = df["t_seconds"].astype(float)
    return df


def stats_for(df: pd.DataFrame, label: str) -> dict:
    if len(df) < 2:
        return {"label": label, "note": "not enough data points"}

    equity = df.set_index("t_seconds")["totalValue"]
    equity = equity.groupby(level=0).last()  # a client-side "portfolio" and
    # the server's own broadcast of the same update can share a timestamp;
    # keep the later value for that instant.

    # --- resample onto a regular grid (last-observation-carried-forward) ---
    grid = np.arange(0, equity.index.max() + RESAMPLE_SECONDS, RESAMPLE_SECONDS)
    resampled = equity.reindex(equity.index.union(grid)).sort_index().ffill().reindex(grid)
    resampled = resampled.dropna()

    start_val = resampled.iloc[0]
    end_val   = resampled.iloc[-1]
    total_return_usd = end_val - start_val
    total_return_pct = (end_val / start_val - 1.0) * 100.0 if start_val else float("nan")

    # --- max drawdown on the resampled equity curve ---
    running_max = resampled.cummax()
    drawdown = (resampled - running_max)
    drawdown_pct = (resampled / running_max - 1.0) * 100.0
    max_dd_usd = drawdown.min()
    max_dd_pct = drawdown_pct.min()

    # --- Sharpe ---
    # Two numbers, deliberately not one:
    #   sharpe_session   = mean(returns) / std(returns) * sqrt(N), N = number
    #                       of 1s periods actually observed in THIS session.
    #                       This is the honest, interpretable one: "was the
    #                       risk-adjusted return over the session we actually
    #                       ran positive or negative, and by how much".
    #   sharpe_annualized= the textbook formula, scaling by sqrt(periods in a
    #                       YEAR) instead of sqrt(periods actually observed).
    #                       Included because it's the number people expect to
    #                       see, but for a several-minute paper session it is
    #                       not a meaningful forecast of annual risk-adjusted
    #                       return — 1-second returns aren't IID at that
    #                       horizon, and projecting ~400 seconds of data out
    #                       to a full year massively amplifies noise. Treat
    #                       it as illustrative only; sharpe_session is the
    #                       number to actually reason about.
    rets = resampled.pct_change().dropna()
    duration_s = grid[-1] - grid[0] if len(grid) > 1 else 1.0
    periods_per_year = (365.25 * 24 * 3600) / RESAMPLE_SECONDS
    if rets.std() > 0:
        sharpe_session = (rets.mean() - RISK_FREE) / rets.std() * math.sqrt(len(rets))
        sharpe_annualized = ((rets.mean() - RISK_FREE) / rets.std()) * math.sqrt(periods_per_year)
    else:
        sharpe_session = float("nan")
        sharpe_annualized = float("nan")

    # --- win rate: events where realizedPnL changed ---
    pnl_diff = df["realizedPnL"].diff().dropna()
    closing_events = pnl_diff[pnl_diff.abs() > 1e-9]
    wins = (closing_events > 0).sum()
    losses = (closing_events < 0).sum()
    win_rate = wins / (wins + losses) * 100.0 if (wins + losses) > 0 else float("nan")

    return {
        "label": label,
        "duration_s": duration_s,
        "snapshots": len(df),
        "start_equity": start_val,
        "end_equity": end_val,
        "total_return_usd": total_return_usd,
        "total_return_pct": total_return_pct,
        "max_drawdown_usd": max_dd_usd,
        "max_drawdown_pct": max_dd_pct,
        "sharpe_session": sharpe_session,
        "sharpe_annualized": sharpe_annualized,
        "closing_events": int(wins + losses),
        "wins": int(wins),
        "losses": int(losses),
        "win_rate_pct": win_rate,
        "final_realized_pnl": df["realizedPnL"].iloc[-1],
        "final_unrealized_pnl": df["unrealizedPnL"].iloc[-1],
    }, resampled


def fmt(v):
    if isinstance(v, float):
        if math.isnan(v):
            return "n/a"
        return f"{v:,.2f}"
    return str(v)


def main():
    paths = sys.argv[1:]
    if not paths:
        print("usage: analyze_session.py <csv> [<csv> ...]")
        sys.exit(1)

    all_stats = []
    plt.figure(figsize=(10, 5.5))

    for path in paths:
        label = os.path.splitext(os.path.basename(path))[0].replace("_portfolio", "")
        df = load(path)
        result = stats_for(df, label)
        if isinstance(result, dict):
            all_stats.append(result)
            continue
        s, curve = result
        all_stats.append(s)
        plt.plot(curve.index, curve.values, label=f"{label}  (final ${curve.iloc[-1]:,.0f})", linewidth=1.8)

    plt.axhline(100_000, color="#888", linestyle="--", linewidth=1, label="starting capital ($100,000)")
    plt.title("Live session equity curve — Synthetic-Bull bots vs. real matching engine")
    plt.xlabel("seconds since session start")
    plt.ylabel("portfolio total value (USD)")
    plt.legend(loc="best", fontsize=9)
    plt.grid(alpha=0.3)
    plt.tight_layout()

    out_dir = os.path.dirname(paths[0]) or "."
    png_path = os.path.join(out_dir, "equity_curve.png")
    plt.savefig(png_path, dpi=150)
    print(f"Wrote {png_path}")

    md_path = os.path.join(out_dir, "session_report.md")
    with open(md_path, "w") as f:
        f.write("| Bot | Duration | Snapshots | Start | End | Return | Max Drawdown | Sharpe (session) | Sharpe (naive annualized*) | Closing events | Win rate |\n")
        f.write("|---|---|---|---|---|---|---|---|---|---|---|\n")
        for s in all_stats:
            if "note" in s:
                f.write(f"| {s['label']} | — | — | — | — | — | — | — | — | — | {s['note']} |\n")
                continue
            f.write(
                f"| {s['label']} | {s['duration_s']:.0f}s | {s['snapshots']} "
                f"| ${fmt(s['start_equity'])} | ${fmt(s['end_equity'])} "
                f"| ${fmt(s['total_return_usd'])} ({fmt(s['total_return_pct'])}%) "
                f"| ${fmt(s['max_drawdown_usd'])} ({fmt(s['max_drawdown_pct'])}%) "
                f"| {fmt(s['sharpe_session'])} "
                f"| {fmt(s['sharpe_annualized'])} "
                f"| {s['closing_events']} ({s['wins']}W / {s['losses']}L) "
                f"| {fmt(s['win_rate_pct'])}% |\n"
            )
        f.write("\n\\* naive annualization of a several-minute session — not a meaningful forecast, see analyze_session.py docstring. Use the session Sharpe.\n")
    print(f"Wrote {md_path}")

    for s in all_stats:
        print(s)


if __name__ == "__main__":
    main()
