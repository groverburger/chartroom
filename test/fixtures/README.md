The Nasdaq and TradingView JSON fixtures are trimmed public responses captured on
2026-09-23. They exercise provider field formats, missing values, timestamps, and
option expirations. `nasdaq-options.json` deliberately retains both the first
expiry header and later rows without a header to test paginated chains. Counts
refer to the original response, not the trimmed fixture.

The VIX fixtures preserve the Yahoo daily zero-OHLC defect and the corresponding
hourly session data used to test recovery.

`sp500-constituents.csv` (symbol and name columns of the datasets/s-and-p-500-companies
constituents file) and `open8585-list.json` (the published 85-85 list trimmed to three
stocks) were captured on 2026-09-30 for the live watchlist parsers.
`yahoo-search-aapl.json` and `yahoo-search-xlk.json` are Yahoo symbol-search responses
trimmed to their first two quotes and display fields, captured on 2026-10-01.
