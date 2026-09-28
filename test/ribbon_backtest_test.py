"""Accounting checks for next-close signals and allocation between strategy sleeves."""
import sys
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import pandas as pd
from ribbon_backtest import backtest, metrics, risk_free
from ribbon_portfolio_backtest import combine, valuation_grid


class RibbonAccounting(unittest.TestCase):
    def test_next_close_signal_delay(self):
        dates = pd.date_range('1995-01-01', periods=204, tz='UTC')
        bars = pd.DataFrame({'close': [100.] * 200 + [100., 200., 300., 100.],
                             'open': [100.] * 204}, index=dates)
        ranks = pd.Series([0] * 200 + [1, 2, 1, 0], index=dates)
        with patch('ribbon_backtest.ribbon', return_value=ranks):
            result = backtest(bars, dates[200], execution='close')
        self.assertEqual(result.target_exposure.tolist(), [0, .25, .5, .25])
        self.assertEqual(result.strategy_value.iloc[1], 10000)  # no return earned before buying
        self.assertEqual(result.strategy_value.iloc[2], 11250)
        self.assertEqual(result.strategy_value.iloc[3], 7500)
        # Default next-open execution used by the earlier report stays unchanged.
        with patch('ribbon_backtest.ribbon', return_value=ranks):
            earlier = backtest(bars, dates[200])
        self.assertEqual(earlier.strategy_value.iloc[1], 12500)

    def test_same_close_earns_only_subsequent_moves(self):
        dates = pd.date_range('1995-01-01', periods=204, tz='UTC')
        bars = pd.DataFrame({'close': [100.] * 200 + [100., 200., 300., 100.]}, index=dates)
        ranks = pd.Series([0] * 200 + [1, 2, 1, 0], index=dates)
        with patch('ribbon_backtest.ribbon', return_value=ranks):
            result = backtest(bars, dates[200], execution='close', signal_lag=0)
        self.assertEqual(result.target_exposure.tolist(), [.25, .5, .25, 0])
        self.assertEqual(result.strategy_value.tolist()[:3], [10000, 12500, 15625])
        self.assertAlmostEqual(result.strategy_value.iloc[3], 13020.833333333334)
        # A signal-day jump must not be credited to a position bought at that close.
        bars.iloc[200, 0] = 1000
        with patch('ribbon_backtest.ribbon', return_value=ranks):
            jumped = backtest(bars, dates[200], execution='close', signal_lag=0)
        self.assertEqual(jumped.strategy_value.iloc[0], 10000)
        with self.assertRaises(ValueError):
            backtest(bars, dates[200], execution='open', signal_lag=0)

    def test_color_sets_target_from_first_day(self):
        dates = pd.date_range('1995-01-01', periods=203, tz='UTC')
        bars = pd.DataFrame({'close': [100.] * 200 + [100., 110., 121.]}, index=dates)
        ranks = pd.Series([4] * 201 + [2, 4], index=dates)  # cyan when the test starts
        with patch('ribbon_backtest.ribbon', return_value=ranks):
            result = backtest(bars, dates[200], execution='close', signal_lag=0)
        self.assertEqual(result.target_exposure.tolist(), [1, .5, 1])  # multi-color jumps move fully
        self.assertEqual(result.strategy_value.tolist(), [10000, 11000, 11550])

    def test_dividends_reinvested_without_moving_signals(self):
        dates = pd.date_range('1995-01-01', periods=202, tz='UTC')
        # A $2 dividend goes ex on the last day: price drops 100 -> 98, adjusted history scales by .98.
        close = [100.] * 201 + [98.]
        bars = pd.DataFrame({'close': close, 'adjclose': [98.] * 201 + [98.]}, index=dates)
        ranks = pd.Series([4] * 202, index=dates)
        with patch('ribbon_backtest.ribbon', return_value=ranks):
            result = backtest(bars, dates[200], execution='close', signal_lag=0)
            price_only = backtest(bars.drop(columns='adjclose'), dates[200], execution='close', signal_lag=0)
        self.assertAlmostEqual(result.strategy_value.iloc[1], 10000)
        self.assertAlmostEqual(price_only.strategy_value.iloc[1], 9800)
        self.assertEqual(result.close.iloc[1], 98)  # reported/charted close stays unadjusted

    def test_cash_earns_previous_rate_per_calendar_day(self):
        # Friday then Monday: three calendar days accrue at Friday's 10% rate, not Monday's 50%.
        dates = pd.bdate_range('1995-01-06', periods=202, tz='UTC')
        bars = pd.DataFrame({'close': [100.] * 202}, index=dates)
        ranks = pd.Series([0] * 202, index=dates)
        rates = pd.Series([5., 10., 50.], index=[dates[0], dates[200], dates[201]])
        self.assertEqual((dates[201] - dates[200]).days, 3)
        with patch('ribbon_backtest.ribbon', return_value=ranks):
            result = backtest(bars, dates[200], execution='close', signal_lag=0, rates=rates)
            self.assertEqual(result.strategy_value.iloc[0], 10000)
            self.assertAlmostEqual(result.strategy_value.iloc[1], 10000 * 1.1 ** (3 / 365))
            with self.assertRaises(ValueError):
                backtest(bars, dates[200], execution='close', signal_lag=0, rates=rates.iloc[2:])

    def test_portfolio_flows_do_not_create_returns(self):
        dates = pd.to_datetime(['1996-12-30', '1996-12-31', '1997-01-02', '1997-01-03'], utc=True)
        navs = pd.DataFrame({'SPY': [10000, 20000, 30000, 30000],
                             'GOLD': [10000, 10000, 10000, 20000]}, index=dates)
        drift = combine(navs)
        pd.testing.assert_series_equal(drift.strategy_value, navs.mean(axis=1), check_names=False)
        annual = combine(navs, annual=True)
        self.assertEqual(annual.strategy_value.tolist(), [10000, 15000, 20000, 30000])
        self.assertEqual(annual.spy_sleeve_weight.iloc[2], .5)
        self.assertEqual(int(annual.rebalanced.sum()), 1)
        self.assertAlmostEqual(annual.daily_return.iloc[2], 1/3)  # old weights before transfer
        self.assertEqual(annual.daily_return.iloc[3], .5)  # new weights afterward
        delayed = combine(navs, annual=True, eligible={dates[0], dates[3]})
        self.assertFalse(delayed.rebalanced.iloc[2])
        self.assertEqual(delayed.strategy_value.iloc[3], 25000)

    def test_calendar_marks_never_look_forward(self):
        dates = pd.date_range('1996-01-02', periods=3, tz='UTC')
        result = pd.DataFrame({'strategy_value': [10000, 11000]}, index=dates[[0, 2]])
        marked = valuation_grid(result, dates)
        self.assertEqual(marked.strategy_value.tolist(), [10000, 10000, 11000])
        self.assertAlmostEqual(marked.daily_return.iloc[2], .1)

    def test_ratios_use_excess_over_period_tbills(self):
        dates = pd.date_range('1996-01-02', periods=4, tz='UTC')
        returns = pd.Series([0, .1, -.05, .02], index=dates)
        r = pd.DataFrame({'daily_return': returns, 'strategy_value': 10000*(1+returns).cumprod()})
        # 20% then 0%: daily T-bill returns are 0 on the first day, then (1.2)^(1/365)-1, then 0, 0.
        rates = pd.Series([20., 0.], index=dates[:2])
        rf = pd.Series([0, 1.2 ** (1 / 365) - 1, 0, 0], index=dates)
        pd.testing.assert_series_equal(risk_free(dates, rates), rf, check_names=False, check_freq=False)
        excess = returns - rf
        stats = metrics(r, dates[0], 252, rates)
        self.assertAlmostEqual(stats['daily_std_pct']/100, returns.std(ddof=1))
        self.assertAlmostEqual(stats['annualized_std_pct'], stats['daily_std_pct']*252**.5)
        self.assertAlmostEqual(stats['sharpe'], excess.mean()/excess.std(ddof=1)*252**.5)
        self.assertAlmostEqual(stats['sortino'], excess.mean()/(.05**2/4)**.5*252**.5)
        # Without rates the ratios fall back to a zero risk-free rate.
        self.assertAlmostEqual(metrics(r, dates[0], 252)['sharpe'], returns.mean()/returns.std(ddof=1)*252**.5)

if __name__ == '__main__':
    unittest.main()
