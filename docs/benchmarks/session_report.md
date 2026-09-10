| Bot | Duration | Snapshots | Start | End | Return | Max Drawdown | Sharpe (session) | Sharpe (naive annualized*) | Closing events | Win rate |
|---|---|---|---|---|---|---|---|---|---|---|
| market_maker | 389s | 637 | $100,000.01 | $88,785.82 | $-11,214.19 (-11.21%) | $-11,883.39 (-11.88%) | -2.79 | -797.02 | 325 (93W / 232L) | 28.62% |
| alpha_bot | 379s | 32 | $100,000.00 | $99,795.73 | $-204.27 (-0.20%) | $-557.70 (-0.56%) | -0.46 | -132.52 | 15 (9W / 6L) | 60.00% |

\* naive annualization of a several-minute session — not a meaningful forecast, see analyze_session.py docstring. Use the session Sharpe.
