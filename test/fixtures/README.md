The Nasdaq and TradingView JSON fixtures are trimmed public responses captured on
2026-09-23. They exercise provider field formats, missing values, timestamps, and
option expirations. `nasdaq-options.json` deliberately retains both the first
expiry header and later rows without a header to test paginated chains. Counts
refer to the original response, not the trimmed fixture.

The VIX fixtures preserve the Yahoo daily zero-OHLC defect and the corresponding
hourly session data used to test recovery.
