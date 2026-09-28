#!/usr/bin/env python3
"""Equity curves of annually rebalanced ribbon portfolios under each accounting variant.

Run: python3 scripts/ribbon_equity_curves.py
Reuses the cached inputs of the 1996 same-close report and the 2016 next-open study.
"""
import argparse
from pathlib import Path

import matplotlib
matplotlib.use('Agg')
import matplotlib.dates as mdates
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd

from ribbon_backtest import VARIANTS, backtest, cash_rates, history, metrics, variant
from ribbon_portfolio_backtest import combine, gold_history, save, simulate, style, valuation_grid

# Ordered one-hue ramp: each variant adds to the one before it.
SHADES = {'price': '#86b6ef', 'dividends': '#2a78d6', 'tbills': '#104281'}
INK, MUTED = '#0b0b0b', '#52514e'


def three_assets(bars, rates, start, name):
    """SPY/GLD/BTC ribbon sleeves at next-open, valued on SPY dates, 1/3 each, reset every January."""
    raw = {}
    for key, frame in bars.items():
        data, cash = variant(frame, rates, name)
        raw[key] = backtest(data, start, rates=cash)
    dates = raw['SPY'].index
    values = pd.DataFrame({key: valuation_grid(r, dates).strategy_value for key, r in raw.items()})
    common = set(bars['SPY'].index.intersection(bars['GLD'].index).intersection(bars['BTC-USD'].index))
    return combine(values, annual=True, eligible=common)


def chart(curves, stats, title, note, path):
    fig, ax = plt.subplots(figsize=(12, 6.5), layout='constrained')
    for name, curve in curves.items():
        s = stats[name]
        ax.plot(curve.index, curve.strategy_value, color=SHADES[name], lw=2, solid_capstyle='round',
                label=f"{VARIANTS[name]}: CAGR {s['cagr_pct']:.2f}% · SD {s['annualized_std_pct']:.1f}% · "
                      f"Sharpe {s['sharpe']:.2f} · Sortino {s['sortino']:.2f}")
        ax.plot(curve.index[-1], curve.strategy_value.iloc[-1], 'o', ms=8, color=SHADES[name], mec='white', mew=2)
    style(ax, money=True)
    ax.margins(x=.06)
    if max(c.index[-1] for c in curves.values()).year - min(c.index[0] for c in curves.values()).year <= 12:
        ax.xaxis.set_major_locator(mdates.YearLocator(1))
    # End labels, pushed apart (in log space) where the curves finish close together.
    ends = sorted((c.strategy_value.iloc[-1], c.index[-1]) for c in curves.values())
    low, high = np.log10(ax.get_ylim())
    gap, placed = .045 * (high - low), []
    for value, day in ends:
        y = max(np.log10(value), placed[-1] + gap) if placed else np.log10(value)
        placed.append(y)
    overflow = max(0, placed[-1] - (high - gap / 2))  # keep the top label inside the axes
    for (value, day), y in zip(ends, placed):
        ax.annotate(f'${value:,.0f}', (day, 10 ** (y - overflow)), xytext=(10, 0), textcoords='offset points',
                    va='center', fontsize=10, color=INK)
    ax.set_ylabel('Equity · $10,000 initial · log scale', color=MUTED)
    ax.legend(loc='upper left', fontsize=10, frameon=False, labelcolor=INK)
    ax.set_title(title, fontsize=15, loc='left', color=INK)
    fig.supxlabel(note, fontsize=9, color=MUTED)
    save(fig, path.parent, path.name)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--end', default='2026-09-25', help='Exclusive; must match the cached inputs')
    parser.add_argument('--output', type=Path, default=Path('reports/ribbon-equity-curves'))
    args = parser.parse_args()
    end = pd.Timestamp(args.end, tz='UTC')
    args.output.mkdir(parents=True, exist_ok=True)

    folder = Path('reports/ribbon-backtest-1996-same-close/data')
    start = pd.Timestamp('1996-01-01', tz='UTC')
    rates = cash_rates(end, folder)
    spy, (gold, _) = history('SPY', end, folder), gold_history(end, folder)
    last = min(spy.index[-1], gold.index[-1])
    curves = {name: simulate(spy.loc[:last], gold.loc[:last], rates, start, 0, name)[1]['ANNUAL']
              for name in VARIANTS}
    stats = {name: metrics(c, start, 252, rates) for name, c in curves.items()}
    pd.DataFrame({VARIANTS[n]: c.strategy_value for n, c in curves.items()}).to_csv(
        args.output / 'spy-gold-1996.csv', index_label='date')
    chart(curves, stats, 'S&P 500 + gold ribbon strategies · 50/50, rebalanced annually · 1996–2026',
          'Same-day close trades · gold is USD spot reference · each variant adds to the one above it · '
          'Sharpe/Sortino vs. daily 3-month T-bills · no trading costs',
          args.output / 'spy-gold-1996')

    folder = Path('reports/ribbon-backtest/data')
    start = pd.Timestamp('2016-01-01', tz='UTC')
    rates = cash_rates(end, folder)
    bars = {symbol: history(symbol, end, folder) for symbol in ('SPY', 'GLD', 'BTC-USD')}
    curves = {name: three_assets(bars, rates, start, name) for name in VARIANTS}
    stats = {name: metrics(c, start, 252, rates) for name, c in curves.items()}
    pd.DataFrame({VARIANTS[n]: c.strategy_value for n, c in curves.items()}).to_csv(
        args.output / 'spy-gld-btc-2016.csv', index_label='date')
    chart(curves, stats, 'SPY + GLD + BTC ribbon strategies · ⅓ each, rebalanced annually · 2016–2026',
          'Next-open trades · valued and rebalanced on SPY trading days (BTC weekend moves land on the next session) · '
          'Sharpe/Sortino vs. daily 3-month T-bills · no trading costs',
          args.output / 'spy-gld-btc-2016')

    for label, path in (('1996 SPY/gold 50/50', 'spy-gold-1996.csv'), ('2016 SPY/GLD/BTC ⅓ each', 'spy-gld-btc-2016.csv')):
        final = pd.read_csv(args.output / path, index_col='date').iloc[-1]
        print(label, final.round(0).to_dict())
    print(f'Artifacts: {args.output.resolve()}')


if __name__ == '__main__':
    main()
