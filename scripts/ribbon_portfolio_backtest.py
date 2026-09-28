#!/usr/bin/env python3
"""1996 SPY/gold ribbon study; same- or next-close trades and two 50/50 portfolios.

Run: python3 scripts/ribbon_portfolio_backtest.py
Requires pandas and matplotlib. Raw inputs and daily accounting are archived.
"""
import argparse
import shutil
from datetime import datetime, timezone
from pathlib import Path
from urllib.request import Request, urlopen

import matplotlib.dates as mdates
import matplotlib.pyplot as plt
from matplotlib.ticker import FuncFormatter, LogLocator, NullFormatter
import pandas as pd

from ribbon_backtest import CAPITAL, VARIANTS, backtest, cash_rates, history, metrics, variant

GOLD_URL = 'https://www.goldprice.com/gold-price-history.csv'
LABELS = {'SPY': 'S&P 500 (SPY)', 'GOLD': 'Gold (USD spot reference)',
          'DRIFT': '50/50 initially; drift', 'ANNUAL': '50/50; annual rebalance'}
COLORS = {'SPY': '#087ca7', 'GOLD': '#b17b15', 'DRIFT': '#087ca7', 'ANNUAL': '#9462a3'}


def gold_history(end, folder, refresh=False):
    path = folder / f'gold-price-history-{end.date()}.csv'
    if refresh or not path.exists():
        with urlopen(Request(GOLD_URL, headers={'User-Agent': 'Mozilla/5.0'}), timeout=60) as response:
            path.write_bytes(response.read())
    frame = pd.read_csv(path, comment='#')
    frame['date'] = pd.to_datetime(frame.date, utc=True)
    frame = frame.set_index('date').rename(columns={'close_usd_per_troy_oz': 'close'}).sort_index()
    if frame.index.has_duplicates or not frame.close.notna().all() or not (frame.close > 0).all():
        raise ValueError('Gold history contains missing, duplicate or nonpositive observations')
    # The recent feed includes carried-forward weekend quotes; they are not trading bars.
    weekends = int((frame.index.dayofweek >= 5).sum())
    frame = frame.loc[(frame.index < end) & (frame.index.dayofweek < 5), ['close']]
    if frame.index[0] > pd.Timestamp('1995-01-01', tz='UTC'):
        raise ValueError('Insufficient pre-1996 warmup')
    return frame, weekends


def valuation_grid(result, dates):
    """Mark strategy NAV on US sessions; accumulate holiday moves at the next mark."""
    out = result.reindex(result.index.union(dates)).ffill().reindex(dates).copy()
    out['strategy_value'] = out.strategy_value.fillna(CAPITAL)
    out['daily_return'] = out.strategy_value.pct_change()
    out.iloc[0, out.columns.get_loc('daily_return')] = out.strategy_value.iloc[0] / CAPITAL - 1
    return out


def combine(values, annual=False, eligible=None):
    """Unitized strategy sleeves; capital flows preserve their existing asset/cash mix."""
    units = pd.Series({column: CAPITAL / len(values.columns) / CAPITAL for column in values})
    last_year = values.index[0].year
    rows = []
    for day, navs in values.iterrows():
        sleeve_values = units * navs
        total = float(sleeve_values.sum())
        rebalance = bool(annual and day.year != last_year and (eligible is None or day in eligible))
        if rebalance:
            units = total / len(values.columns) / navs
            sleeve_values = units * navs
            last_year = day.year
        rows.append({'date': day, 'strategy_value': total,
                     **{f'{key.lower()}_sleeve_weight': float(sleeve_values[key] / total) for key in values},
                     'rebalanced': rebalance})
    out = pd.DataFrame(rows).set_index('date')
    out['daily_return'] = out.strategy_value.pct_change()
    out.iloc[0, out.columns.get_loc('daily_return')] = out.strategy_value.iloc[0] / CAPITAL - 1
    return out


def simulate(spy, gold, rates, start, signal_lag, name):
    """Both strategies traded on their own calendars, valued on SPY dates, plus both 50/50 portfolios."""
    raw = {}
    for key, bars in (('SPY', spy), ('GOLD', gold)):
        data, cash = variant(bars, rates, name)
        raw[key] = backtest(data, start, execution='close', signal_lag=signal_lag, rates=cash)
    dates = raw['SPY'].index
    results = {key: valuation_grid(result, dates) for key, result in raw.items()}
    values = pd.DataFrame({key: r.strategy_value for key, r in results.items()})
    common = set(spy.index.intersection(gold.index))
    results['DRIFT'] = combine(values)
    results['ANNUAL'] = combine(values, annual=True, eligible=common)
    return raw, results


def stat_line(stats):
    return (f"CAGR {stats['cagr_pct']:.2f}%  |  Sharpe {stats['sharpe']:.2f}  |  "
            f"Sortino {stats['sortino']:.2f}\n"
            f"Daily std. dev. {stats['daily_std_pct']:.3f}%  |  Annualized std. dev. {stats['annualized_std_pct']:.2f}%")


def style(ax, money=False):
    ax.grid(alpha=.20)
    ax.spines[['top', 'right']].set_visible(False)
    ax.xaxis.set_major_locator(mdates.YearLocator(5))
    ax.xaxis.set_major_formatter(mdates.DateFormatter('%Y'))
    if money:
        ax.set_yscale('log')
        ax.yaxis.set_major_locator(LogLocator(base=10, subs=(1, 2, 5)))
        ax.yaxis.set_major_formatter(FuncFormatter(lambda x, _: f'${x/1000:g}k'))
        ax.yaxis.set_minor_formatter(NullFormatter())


def save(fig, output, name):
    fig.savefig(output / f'{name}.png', dpi=180, bbox_inches='tight')
    fig.savefig(output / f'{name}.svg', bbox_inches='tight')
    plt.close(fig)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--start', default='1996-01-01')
    parser.add_argument('--end', default=datetime.now(timezone.utc).date().isoformat(), help='Exclusive')
    parser.add_argument('--execution', choices=['same-close', 'next-close'], default='same-close')
    parser.add_argument('--output', type=Path)
    parser.add_argument('--refresh', action='store_true')
    args = parser.parse_args()
    same_close = args.execution == 'same-close'
    if args.output is None:
        args.output = Path('reports/ribbon-backtest-1996' + ('-same-close' if same_close else ''))
    execution_label = 'same-day close' if same_close else 'next-day close'
    execution_note = (
        "Both strategies trade at the **signal day's close**, using that same day's completed ribbon change. "
        "The fill uses the exact closing/reference price that finalizes the signal, an optimistic execution assumption. "
        "Existing holdings earn the signal day's price move; the new allocation earns only subsequent moves."
        if same_close else
        "Both strategies trade at the **following available trading day's close**, using the previous day's ribbon change."
    )
    start, end = pd.Timestamp(args.start, tz='UTC'), pd.Timestamp(args.end, tz='UTC')
    if not start < end <= pd.Timestamp.now(tz='UTC').normalize():
        parser.error('Require start < end <= today UTC')
    folder = args.output / 'data'; folder.mkdir(parents=True, exist_ok=True)
    previous = Path('reports/ribbon-backtest/data') / f'SPY-{end.date()}.json'
    target = folder / previous.name
    if previous.exists() and not target.exists() and not args.refresh:
        shutil.copyfile(previous, target)
    gold_cache = Path('reports/ribbon-backtest-1996/data') / f'gold-price-history-{end.date()}.csv'
    gold_target = folder / gold_cache.name
    if gold_cache.exists() and not gold_target.exists() and not args.refresh:
        shutil.copyfile(gold_cache, gold_target)
    spy = history('SPY', end, folder, args.refresh)
    rates = cash_rates(end, folder, args.refresh)
    gold, weekends = gold_history(end, folder, args.refresh)
    last = min(spy.index[-1], gold.index[-1])
    spy, gold = spy.loc[:last], gold.loc[:last]
    signal_lag = 0 if same_close else 1
    variants = []
    for name, label in VARIANTS.items():
        _, runs = simulate(spy, gold, rates, start, signal_lag, name)
        variants += [{'strategy': LABELS[key], 'variant': label, 'execution': args.execution,
                      **metrics(r, start, 252, rates)} for key, r in runs.items()]
    variants = pd.DataFrame(variants)
    variants.to_csv(args.output / 'variants.csv', index=False)
    raw, results = simulate(spy, gold, rates, start, signal_lag, 'tbills')
    dates = raw['SPY'].index
    stats = {key: metrics(r, start, 252, rates) for key, r in results.items()}
    summary = []
    for key, r in results.items():
        r.to_csv(args.output / f'{key}.csv', index_label='date')
        summary.append({'strategy': LABELS[key], 'execution': args.execution, 'first_day': str(r.index[0].date()),
                        'last_day': str(r.index[-1].date()), 'final_value': float(r.strategy_value.iloc[-1]),
                        **stats[key]})
    for key, r in raw.items():
        r.to_csv(args.output / f'{key}-trading-days.csv', index_label='date')
    summary = pd.DataFrame(summary)
    summary.to_csv(args.output / 'summary.csv', index=False)
    period = f'{start.date()} – {last.date()}'
    fig, axes = plt.subplots(2, 2, figsize=(14, 8.5), sharex='col', layout='constrained',
                             gridspec_kw={'height_ratios': [2, 1]})
    for i, key in enumerate(('SPY', 'GOLD')):
        r = results[key]
        top, bottom = axes[:, i]
        top.plot(r.index, r.strategy_value, color=COLORS[key], lw=1.4)
        top.set_title(LABELS[key] + '\n' + stat_line(stats[key]), fontsize=11, pad=12)
        top.set_ylabel('Strategy equity · $10,000 initial · log scale')
        bottom.plot(r.index, r.actual_exposure * 100, color=COLORS[key], lw=.7)
        bottom.set(ylabel='Asset exposure (%)', ylim=(-3, 103), yticks=[0, 25, 50, 75, 100])
        style(top, money=True); style(bottom)
    fig.suptitle('Daily MA ribbon · individual strategies\n' + period, fontsize=16)
    fig.supxlabel(f'Target exposure set by color: 0/25/50/75/100% · {execution_label} execution\nSPY dividends reinvested · cash earns 3-month T-bills · Sharpe/Sortino vs. T-bills · no trading costs', fontsize=10)
    save(fig, args.output, 'assets')
    fig, (top, bottom) = plt.subplots(2, 1, figsize=(14, 9), sharex=True, layout='constrained',
                                     gridspec_kw={'height_ratios': [3, 1]})
    for key in ('DRIFT', 'ANNUAL'):
        r = results[key]
        top.plot(r.index, r.strategy_value, color=COLORS[key], lw=1.5,
                 label=LABELS[key] + '\n' + stat_line(stats[key]))
        bottom.plot(r.index, r.spy_sleeve_weight * 100, color=COLORS[key], lw=1, label=LABELS[key])
    top.set_ylabel('Combined equity · $10,000 initial · log scale')
    top.legend(loc='upper left', fontsize=10, framealpha=.95)
    bottom.axhline(50, color='#777777', lw=.6, linestyle='--')
    bottom.set(ylabel='S&P strategy weight (%)', ylim=(0, 100), yticks=[0, 25, 50, 75, 100])
    style(top, money=True); style(bottom)
    fig.suptitle('Daily MA ribbon · combined S&P 500 + gold portfolios\n' + period, fontsize=16)
    fig.supxlabel(f'$5,000 per strategy initially · annual reset at first joint trading-day close\nWeights refer to strategy sleeves, including their cash · {execution_label} trades · dividends reinvested · cash earns T-bills · Sharpe/Sortino vs. T-bills · no costs', fontsize=10)
    save(fig, args.output, 'combined')
    (args.output / 'methodology.md').write_text(f'''# MA-ribbon portfolios from 1996

Period: {start.date()} through {last.date()}. $10,000 initial capital per reported curve.

## Data and execution

- S&P 500: Yahoo SPY daily prices. Signals use split-adjusted closes (as charted); holdings trade and are valued at Yahoo's dividend-adjusted closes, so dividends are reinvested. Exact response saved in `data/`.
- Gold: [GoldPrice.com daily CSV]({GOLD_URL}), attributed under CC BY 4.0. The provider describes [LBMA fixes before 2015, then Metals.dev closes](https://www.goldprice.com/gold-price-history). USD per troy ounce. This is spot-reference gold, not GLD (which started in November 2004), and not a rolled futures contract.
- Removed {weekends} weekend gold observations, which are carried-forward quotes rather than trading bars. No missing opening prices were fabricated. No daily prices were backfilled from future observations.
- {execution_note} Historical fixing prices are indicative execution proxies; this is an idealized price-only simulation.
- Full earlier history warms up the 20/50/100/150/200 EMAs. Both strategies start in cash and take the position implied by the ribbon color on the first trade.
- The ribbon color sets the target exposure: purple 0%, pink 25%, orange 50%, mint 75%, cyan 100%. Rebalance only on a target change; actual exposure drifts otherwise.
- Uninvested cash earns the [FRED 3-month Treasury bill rate](https://fred.stlouisfed.org/series/DTB3) (DTB3, used as an annual yield), compounded per calendar day at the previous observation's rate.
- No leverage, shorting, transaction costs, spreads, taxes, or gold custody costs.

## Combining strategies

Each combined portfolio starts with $5,000 in each strategy, including that strategy's cash. Drift leaves the number of strategy units unchanged. Annual rebalancing resets strategy weights to 50/50 at the close of the first day each calendar year when both instruments have a price; that day's return uses the old weights, and the new weights apply afterward. Flows scale each strategy's existing cash and holdings proportionally, without resetting its ribbon target. Rebalancing uses same-date closing/reference NAVs as an idealized execution assumption; the two data sources have different intraday marking times.

Indicators and individual trades use each asset's own weekday observations. For comparable risk statistics and portfolio accounting, all four curves are valued on SPY trading dates ({len(dates)} observations). Gold strategy NAV is carried forward only when no new mark is available; intervening gold-session gains/losses accumulate in the next US-session return. `{ 'GOLD-trading-days.csv' }` preserves the original gold trading calendar. The combined portfolios run through the last date available in both datasets.

## Metrics

- CAGR = (final / 10,000)^(1 / calendar years) − 1, with years = ({last.date()} + one day − {start.date()}) / 365.25.
- Daily standard deviation = sample standard deviation of simple daily **strategy portfolio returns**, ddof=1, including cash days.
- Annualized standard deviation = daily standard deviation × sqrt(252).
- Excess return = daily return − that SPY-date interval's T-bill return: the same period-appropriate DTB3 accrual that cash earns, used as the risk-free rate in every variant.
- Sharpe = mean(excess returns) / sample standard deviation of excess returns × sqrt(252).
- Sortino = mean(excess returns) / sqrt(mean(min(excess return, 0)^2)) × sqrt(252). The T-bill return is the minimum acceptable return; the downside mean includes all observation days.
- Initial return is measured from the initial $10,000; annual portfolio rebalances are internal transfers, not contributions or returns.

## Accounting variants

`variants.csv` repeats every metric for three cumulative variants with identical signals and trades: price only (SPY dividends excluded, cash earns nothing), + SPY dividends reinvested, and + T-bill interest on cash. The last is the headline result used in `summary.csv` and the charts. Gold pays no dividends, so its first two variants match.

Run: `python3 scripts/ribbon_portfolio_backtest.py --start {start.date()} --end {end.date()} --execution {args.execution} --output {args.output}`. Cached files make the run reproducible; `--refresh` replaces inputs. Individual strategy results and both combined portfolios are in CSV files alongside PNG and SVG charts.
''')
    print(summary.to_string(index=False, float_format=lambda x: f'{x:.6f}'))
    print(variants.to_string(index=False, float_format=lambda x: f'{x:.4f}'))
    print('Individual target changes:', {key: int(r.traded.sum()) for key, r in raw.items()})
    print('Annual rebalances:', int(results['ANNUAL'].rebalanced.sum()))
    print(f'Artifacts: {args.output.resolve()}')


if __name__ == '__main__':
    main()
