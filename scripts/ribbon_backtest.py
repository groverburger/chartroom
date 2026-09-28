#!/usr/bin/env python3
"""Simple daily MA-ribbon backtest for SPY, BTC-USD, and gold (GLD).

Run: python3 scripts/ribbon_backtest.py
Requires: pip install pandas matplotlib

Start with $10,000 cash per asset. The ribbon color sets the target allocation:
purple 0%, pink 25%, orange 50%, mint 75%, cyan 100%. Rebalance only when that
target changes, at the NEXT open after the signal close, so a cyan first day
invests fully. Actual exposure drifts between trades. Signals use Yahoo's
split-adjusted closes (as charted); holdings are valued with dividend-adjusted
prices, so dividends are reinvested. Cash earns the 3-month T-bill rate (FRED
DTB3). No fees, slippage, or taxes.
BTC days are UTC days. The end date is exclusive to exclude unfinished candles.
Sharpe/Sortino use a zero risk-free rate/target and 252 ETF or 365 crypto periods
per year. Sortino uses RMS downside returns over ALL days, including cash days.
"""

import argparse
import json
from datetime import datetime, timezone
from pathlib import Path
from urllib.request import Request, urlopen

import matplotlib
matplotlib.use("Agg")
import matplotlib.dates as mdates
import matplotlib.pyplot as plt
from matplotlib.ticker import FuncFormatter, LogLocator, NullFormatter
import pandas as pd

PERIODS = (20, 50, 100, 150, 200)
COLORS = ("purple", "pink", "orange", "mint", "cyan")
CAPITAL = 10_000
ASSETS = {"SPY": 252, "BTC-USD": 365, "GLD": 252}
RATES_URL = "https://fred.stlouisfed.org/graph/fredgraph.csv?id=DTB3"
# Return-accounting variants, cumulative: price only, dividends reinvested, then cash interest.
VARIANTS = {"price": "Price only", "dividends": "+ dividends", "tbills": "+ dividends + T-bill interest"}


def history(symbol, end, folder, refresh=False):
    """Keep the exact provider response so the run can be reproduced offline."""
    cache = folder / f"{symbol}-{end.date()}.json"
    if refresh or not cache.exists():
        url = (f"https://query1.finance.yahoo.com/v8/finance/chart/{symbol}"
               f"?interval=1d&period1=0&period2={int(end.timestamp())}"
               "&includePrePost=false")
        request = Request(url, headers={"User-Agent": "Mozilla/5.0"})
        with urlopen(request, timeout=60) as response:
            raw = json.load(response)
        if raw["chart"].get("error"):
            raise ValueError(raw["chart"]["error"])
        cache.write_text(json.dumps(raw))
    result = json.loads(cache.read_text())["chart"]["result"][0]
    bars = pd.DataFrame(result["indicators"]["quote"][0],
                        index=pd.to_datetime(result["timestamp"], unit="s", utc=True).normalize())
    columns = ["open", "high", "low", "close"]
    if "adjclose" in result["indicators"]:
        bars["adjclose"] = result["indicators"]["adjclose"][0]["adjclose"]
        columns.append("adjclose")
    bars = bars.loc[bars.index < end, columns].dropna().sort_index()
    if (bars.empty or bars.index.has_duplicates or not (bars > 0).all().all()
            or not (bars.high >= bars[["open", "close", "low"]].max(axis=1)).all()
            or not (bars.low <= bars[["open", "close", "high"]].min(axis=1)).all()):
        raise ValueError(f"Invalid OHLC history for {symbol}")
    return bars


def cash_rates(end, folder, refresh=False):
    """Daily 3-month T-bill rates (percent per year), cached like the price data."""
    cache = folder / f"DTB3-{end.date()}.csv"
    if refresh or not cache.exists():
        with urlopen(Request(RATES_URL, headers={"User-Agent": "Mozilla/5.0"}), timeout=60) as response:
            cache.write_bytes(response.read())
    frame = pd.read_csv(cache, na_values=".")
    rates = pd.Series(frame.iloc[:, 1].values, index=pd.to_datetime(frame.iloc[:, 0], utc=True))
    return rates.loc[rates.index < end].dropna().sort_index()


def risk_free(index, rates):
    """Per-bar T-bill return: the rate known at the previous bar, compounded over the calendar days since."""
    if rates is None:
        return pd.Series(0.0, index=index)
    rate = rates.reindex(rates.index.union(index)).ffill().reindex(index)
    if rate.isna().any():
        raise ValueError("Cash rates must start before the backtest")
    days = index.to_series().diff().dt.days
    out = (1 + rate.shift(1) / 100) ** (days / 365) - 1
    return out.fillna(0.0)


def variant(bars, rates, name):
    """Bars and cash rates for one return-accounting variant."""
    if name == "price":
        return bars.drop(columns="adjclose", errors="ignore"), None
    return bars, (rates if name == "tbills" else None)


def ribbon(close):
    """Same EMA ordering as Chartroom: purple < pink < orange < mint < cyan."""
    emas = [close.ewm(span=n, adjust=False).mean() for n in PERIODS]
    return sum((a > b).astype(int) for a, b in zip(emas, emas[1:]))


def backtest(bars, start, execution="open", signal_lag=1, rates=None):
    if execution not in ("open", "close"):
        raise ValueError("Execution must be open or close")
    if signal_lag not in (0, 1) or (signal_lag == 0 and execution != "close"):
        raise ValueError("Same-day signals require close execution; signal lag must be 0 or 1")
    bars = bars.copy()
    bars["rank"] = ribbon(bars.close)
    # A zero lag assumes a fill at the exact close that finalizes the signal.
    # Existing holdings earn today's move; new holdings only earn subsequent moves.
    bars["target"] = bars["rank"].shift(signal_lag) / (len(PERIODS) - 1)
    # Holdings trade and are valued at dividend-adjusted prices; signals stay on raw closes.
    factor = bars.adjclose / bars.close if "adjclose" in bars else 1.0
    prices = pd.DataFrame({column: bars[column] * factor for column in ("open", "close") if column in bars})
    if (bars.index < start).sum() < 200:
        raise ValueError("Choose a start date with at least 200 earlier daily candles for EMA warm-up")
    sample = bars.loc[bars.index >= start]
    if len(sample) < 2:
        raise ValueError("Need at least two backtest candles")
    interest = risk_free(sample.index, rates)
    cash, shares, target = CAPITAL, 0.0, 0.0
    rows = []
    for day, bar in sample.iterrows():
        cash *= 1 + interest[day]
        traded = bar.target != target
        if traded:
            target = bar.target
            trade_price = prices.at[day, execution]
            trading_value = cash + shares * trade_price
            shares = trading_value * target / trade_price
            cash = trading_value * (1 - target)
        value = cash + shares * prices.at[day, "close"]
        rows.append({"date": day, "close": bar.close, "color": COLORS[int(bar["rank"])],
                     "traded": traded, "target_exposure": target,
                     "actual_exposure": shares * prices.at[day, "close"] / value,
                     "cash_return": interest[day], "strategy_value": value})
    result = pd.DataFrame(rows).set_index("date")
    result["daily_return"] = result.strategy_value.pct_change()
    result.iloc[0, result.columns.get_loc("daily_return")] = result.strategy_value.iloc[0] / CAPITAL - 1
    return result


def metrics(result, start, periods, rates=None):
    """Calendar CAGR; Sharpe and full-series Sortino on returns in excess of daily T-bills."""
    years = ((result.index[-1] + pd.Timedelta(days=1) - start).total_seconds()
             / (365.25 * 86400))
    returns = result.daily_return
    excess = returns - risk_free(result.index, rates)
    volatility = returns.std(ddof=1)
    excess_volatility = excess.std(ddof=1)
    downside = (excess.clip(upper=0).pow(2).mean()) ** .5
    return {"cagr_pct": ((result.strategy_value.iloc[-1] / CAPITAL) ** (1 / years) - 1) * 100,
            "sharpe": excess.mean() / excess_volatility * periods ** .5 if excess_volatility > 0 else float("nan"),
            "sortino": excess.mean() / downside * periods ** .5 if downside > 0 else float("nan"),
            "daily_std_pct": volatility * 100,
            "annualized_std_pct": volatility * periods ** .5 * 100}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--start", default="2016-01-01")
    parser.add_argument("--end", default=datetime.now(timezone.utc).date().isoformat(), help="Exclusive date")
    parser.add_argument("--output", type=Path, default=Path("reports/ribbon-backtest"))
    parser.add_argument("--refresh", action="store_true", help="Replace cached Yahoo responses")
    args = parser.parse_args()
    start, end = pd.Timestamp(args.start, tz="UTC"), pd.Timestamp(args.end, tz="UTC")
    if not start < end <= pd.Timestamp.now(tz="UTC").normalize():
        parser.error("Require start < end <= today (UTC)")
    args.output.mkdir(parents=True, exist_ok=True)
    cache = args.output / "data"
    cache.mkdir(exist_ok=True)
    rates = cash_rates(end, cache, args.refresh)
    bars = {symbol: history(symbol, end, cache, args.refresh) for symbol in ASSETS}
    variants = []
    for symbol in ASSETS:
        for name, label in VARIANTS.items():
            data, cash = variant(bars[symbol], rates, name)
            variants.append({"asset": symbol, "variant": label,
                             **metrics(backtest(data, start, rates=cash), start, ASSETS[symbol], rates)})
    pd.DataFrame(variants).to_csv(args.output / "variants.csv", index=False)
    results = {symbol: backtest(bars[symbol], start, rates=rates) for symbol in ASSETS}
    fig, axes = plt.subplots(2, len(results), figsize=(18, 8), sharex="col", layout="constrained")
    summary = []
    for col, (symbol, result) in enumerate(results.items()):
        result.to_csv(args.output / f"{symbol}.csv", index_label="date")
        stats = metrics(result, start, ASSETS[symbol], rates)
        top, bottom = axes[:, col]
        top.plot(result.index, result.strategy_value, label="Ribbon strategy", color="#009fcf")
        title = f"{symbol} (gold)" if symbol == "GLD" else symbol
        top.set(title=f"{title}\nCAGR {stats['cagr_pct']:.2f}% · Sharpe {stats['sharpe']:.2f} · Sortino {stats['sortino']:.2f}",
                ylabel="Portfolio value ($, log scale)", yscale="log")
        money = FuncFormatter(lambda value, _: f"${value / 1e6:g}m" if value >= 1e6 else f"${value / 1e3:g}k")
        top.yaxis.set_major_locator(LogLocator(base=10, subs=(1, 2, 5)))
        top.yaxis.set_major_formatter(money)
        top.yaxis.set_minor_formatter(NullFormatter())
        bottom.plot(result.index, result.actual_exposure * 100, color="#009fcf", lw=.8, label="Actual at close")
        bottom.step(result.index, result.target_exposure * 100, where="post", color="#880088",
                    lw=.6, alpha=.6, label="Target")
        bottom.set(ylabel="Exposure (%)", ylim=(-3, 103), yticks=range(0, 101, 25))
        bottom.legend(loc="upper left")
        bottom.xaxis.set_major_locator(mdates.YearLocator(2))
        bottom.xaxis.set_major_formatter(mdates.DateFormatter("%Y"))
        for ax in (top, bottom):
            ax.grid(alpha=.2)
            ax.spines[["top", "right"]].set_visible(False)
        summary.append({"asset": symbol, "first_day": str(result.index[0].date()),
                        "last_day": str(result.index[-1].date()), "trades": int(result.traded.sum()),
                        "strategy_final": round(result.strategy_value.iloc[-1], 2),
                        **stats})
    fig.suptitle(f"Daily MA ribbon: target exposure set by color (0/25/50/75/100%)\n"
                 f"{start.date()} to {(end - pd.Timedelta(days=1)).date()}", fontsize=16)
    fig.supxlabel("$10,000 each · next-open trades · dividends reinvested · cash earns 3-month T-bills · Sharpe/Sortino vs. T-bills · no costs", fontsize=10)
    fig.savefig(args.output / "backtest.png", dpi=180, bbox_inches="tight", pad_inches=.15)
    fig.savefig(args.output / "backtest.svg", bbox_inches="tight", pad_inches=.15)
    plt.close(fig)
    pd.DataFrame(summary).to_csv(args.output / "summary.csv", index=False)
    (args.output / "methodology.md").write_text(
        f"# Daily MA-ribbon backtest\n\nRequested period: {start.date()} through "
        f"{(end - pd.Timedelta(days=1)).date()} (inclusive).\n\n"
        "SPY and GLD use ETF prices; BTC-USD uses daily UTC prices. GLD is the gold proxy, "
        "not spot gold or a futures contract. Full Yahoo daily history warms up the EMAs. "
        "Exact source responses are cached in `data/`.\n\n"
        "Each asset starts with $10,000 cash. The 20/50/100/150/200 EMA ribbon color sets "
        "the target exposure: purple 0%, pink 25%, orange 50%, mint 75%, cyan 100%. "
        "Trades execute at the next open, only when the target changes, so the first "
        "trade reflects the color already showing when the test starts. Exposure drifts "
        "between trades.\n\n"
        "Signals use split-adjusted closes, matching the chart. Holdings trade and are "
        "valued at Yahoo's dividend-adjusted prices (adjclose / close applied to the open "
        "and close), so dividends are reinvested. Cash earns the FRED 3-month Treasury "
        "bill rate (DTB3, used as an annual yield), compounded per calendar day at the "
        "previous bar's rate.\n\n"
        "Daily portfolio returns include the entire portfolio (cash and holdings), "
        "including cash days. The first return is measured from initial capital. "
        "No fees, slippage, or taxes are modeled.\n\n"
        "- CAGR = (final value / initial capital)^(1 / years) − 1. Years use "
        "calendar time from the requested start through the last completed candle day, "
        "divided by 365.25. Initial ETF holidays remain uninvested.\n"
        "- Standard deviation = sample standard deviation of daily returns; annualized × sqrt(N).\n"
        "- Excess return = daily return − that day's T-bill return (the same period-appropriate "
        "DTB3 accrual cash earns), in every variant.\n"
        "- Sharpe = mean(excess returns) / sample standard deviation of excess returns × sqrt(N).\n"
        "- Sortino = mean(excess returns) / sqrt(mean(min(excess return, 0)^2)) × sqrt(N): "
        "the T-bill return is the minimum acceptable return. The downside mean includes ALL days, "
        "not just below-target days.\n"
        "- N = 252 for SPY/GLD and 365 for BTC. Zero denominators yield undefined ratios.\n\n"
        "Formula references: [Sharpe](https://web.stanford.edu/~wfsharpe/art/sr/sr.htm), "
        "[full-series downside deviation](https://github.com/braverock/PerformanceAnalytics/blob/master/R/DownsideDeviation.R).\n\n"
        "`variants.csv` repeats the metrics for three cumulative accounting variants: price only "
        "(no dividends, cash earns nothing), + dividends reinvested, and + T-bill interest on cash "
        "(the headline results). Signals and trades are identical across variants.\n"
    )
    print(pd.DataFrame(variants).to_string(index=False))
    print(pd.DataFrame(summary).to_string(index=False))
    print(f"\nCharts and daily results: {args.output.resolve()}")


if __name__ == "__main__":
    main()
