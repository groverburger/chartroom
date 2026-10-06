# Chartroom

Chartroom is a minimal charting application for stocks, futures, and crypto. Explore price history, compare symbols across multiple charts, and add technical indicators in a workspace that stays as you left it.

This repository contains the C++20 application, which runs on desktop and in the browser through WebAssembly. It uses Dear ImGui, GLFW, and OpenGL; no Julia runtime is required.

![Chartroom showing SPY and Bitcoin charts with technical indicators](preview.png)

## Features

- **Multiple charts:** independent symbols, timeframes, and indicators, with grid, column, row, and tabbed layouts.
- **Symbol arithmetic:** chart or watch ratios and spreads such as `RSP/SPY`, `(AAPL+MSFT)/2`, or `SPY - QQQ` (spaces around minus, since tickers like `BTC-USD` contain dashes). Bars are matched by date; volume is not shown.
- **Interactive navigation:** pan through history, zoom around the cursor, and inspect prices with a crosshair that snaps to bars. The price axis automatically fits visible data.
- **Chart styles:** candlesticks, OHLC bars, or a line chart, with volume and round-number price levels.
- **Point and figure:** logarithmic (percent) or arithmetic (price, with the traditional box scale as the default) boxes, any reversal, high/low or close-only. Month markers, shaded double-top and double-bottom breakouts, and a tooltip naming the bar that filled each box. Choose it under **View → Chart style**.
- **Drawings and measurement:** a dockable tools window with saved horizontal levels, trendlines, rays, rectangles, text notes, and Fibonacci retracements, with locking and undo/redo. Shift-drag to measure price changes and elapsed time.
- **Technical indicators:** SMA, EMA, VWMA, Bollinger Bands, Donchian and Keltner channels, swing pivots, RSI, Stochastic, MACD, ATR, rate of change, on-balance volume, the open8585 accumulation/distribution rating (A+ to E on daily charts), and an MA ribbon with trend-colored bars.
- **Options chains:** Nasdaq calls and puts by expiry, with bid/ask, last price, volume, open interest, and in-the-money shading.
- **Stock screener:** largest companies, gainers, losers, and most active US stocks, with price, capitalization, volume, and sector filters.
- **Watchlists:** open several independent windows, edit your At a glance favorites, and explore starter lists across assets, sectors, and company groups. Live lists track the S&P 500 constituents and the published [open8585](https://groverburger.github.io/open8585/) 85-85 list, refreshing hourly while open. Hover a ticker for its full name and sector (or fund, index, and futures type). Create your own lists too.
- **Clear refresh state:** a chart refreshing old cached data shows a corner badge; a manual Refresh covers the chart with a spinner until new data arrives.
- **Event-driven rendering:** redraws for input, data changes, and UI timers, then sleeps when idle. Minimized windows and hidden browser tabs skip rendering.
- **Saved workspaces:** charts, indicators, colors, layouts, watchlists, and chart positions persist between sessions. Cached history is available offline.

SPY opens on first launch. Chartroom uses public market-data endpoints from Nasdaq, TradingView, Yahoo Finance, Binance, and Stooq. Open charts and market windows update approximately once per minute.

## Build and run

Desktop builds require CMake 3.20 or newer, a C++20 compiler, and libcurl development files. Dear ImGui, GLFW, the JSON library, and the Roboto font are included in the repository.

Run the commands below from the repository root. macOS and Chromium have been tested; build configurations for Windows and Linux are also included. The [CI workflow](../.github/workflows/build.yml) defines builds for all three desktop platforms and WebAssembly.

### macOS

Install Xcode Command Line Tools and CMake. The macOS SDK provides curl and OpenGL.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
open build/chartroom.app
```

### Windows

Install Visual Studio 2022 with C++ development tools, CMake, and vcpkg. Set `VCPKG_ROOT` to the path of the vcpkg checkout, then run in PowerShell:

```powershell
vcpkg install curl:x64-windows
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build build --config Release --parallel
.\build\Release\chartroom.exe
```

When distributing a Windows build, include the dependency DLLs copied beside the executable and the third-party license notices.

The app icon's master artwork is `assets/icon/chartroom.svg`. After editing it, run `python3 scripts/make_icons.py` (needs `rsvg-convert` and Pillow; the macOS `.icns` also needs `iconutil`) to regenerate the macOS, Windows, window and browser icons.

### Linux

On Debian or Ubuntu, install the build dependencies:

```sh
sudo apt-get install cmake g++ libcurl4-openssl-dev libgl1-mesa-dev xorg-dev libwayland-dev libxkbcommon-dev
```

Then build and launch:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
./build/chartroom
```

### Browser

The browser build requires Emscripten 4.0.23 and Python 3 for the local preview server. With the Emscripten SDK installed:

```sh
source /path/to/emsdk/emsdk_env.sh
emcmake cmake -S . -B build-web -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build build-web --parallel
python3 scripts/serve_web.py --directory build-web --port 8080
```

Open <http://localhost:8080/chartroom.html> in a browser with WebGL2 support. The canvas supports high-DPI displays, including Retina screens.

The preview server serves the application and proxies a fixed set of provider routes, including POST requests for the stock screener. A public deployment needs an equivalent data proxy because these endpoints do not support direct browser access under CORS. Hosting only the HTML, JavaScript, and Wasm files will not provide market data. The included server binds to localhost and is intended for development.

## Using Chartroom

### Charts and navigation

Click a chart to select it, then click a symbol in the sidebar to change its symbol. To load a symbol without adding it to a watchlist, click the symbol name at the top left of a chart, type a ticker, and press **Enter**. Ctrl-click or Cmd-click a symbol to open another chart. Use **+ Add panel** to open a chart, watchlist, options chain, or screener. The **Panels** menu lists open windows and offers duplication and closing; **Layout** arranges charts, and dragging tabs docks them.

Restore a closed chart through **Panels → Reopen closed chart**, **Recently closed**, or **Ctrl/Cmd+Shift+T**. The last 20 closed charts retain their symbols, views, indicators, and settings across restarts. Chartroom keeps at least one chart open. **Workspace → Save workspace** saves immediately; changes also save automatically.

| Action | Control |
| --- | --- |
| Pan through history | Drag horizontally, swipe left/right with two fingers on a trackpad, or Shift-scroll |
| Zoom around the cursor | Scroll vertically over a chart |
| Pan with the keyboard | Left/Right arrows or A/D while the pointer is over a chart |
| Jump to the oldest data | Home |
| Return to the latest 220 bars with 25% future space | End, double-click, or **Latest** |
| Change chart style | **View → Chart style** |
| Use a logarithmic price axis | **View → Price scale → Logarithmic** |
| Measure a move | Shift-drag between two points |
| Undo / redo a drawing edit | Ctrl/Cmd+Z / Ctrl/Cmd+Shift+Z (or Ctrl+Y) |

The time axis uses local calendar boundaries: hours or days when zoomed in, then weeks, months, quarters, and years as the range grows. Month labels use full names such as **March**; January boundaries show the year. Vertical gridlines follow those boundaries. Labels cover the full visible range, including future space, with spacing that prevents overlap. Outside loaded history, dates follow the nominal chart interval rather than a forecast exchange calendar.

New charts place the latest candle three-quarters of the way across the plot, leaving 25% for future dates. This position follows new bars as data refreshes; saved historical views retain their position. Panning can extend beyond the available history. Vertical scaling stays automatic when panning or zooming. Over loaded history, the crosshair snaps horizontally to each bar's center and displays its OHLCV values; in empty space it follows the pointer freely.

Trackpad scrolling follows the gesture's initial direction: horizontal swipes pan, while vertical swipes zoom. Small diagonal movements and momentum do not switch between the two.

Each chart shows **Updated** with the local time of its last successful data refresh. Hover over the timestamp for the provider’s price timestamp and cache/refresh status. Failed refreshes retain the last successful time.

Logarithmic mode gives equal percentage moves equal vertical distance. The setting is saved per chart and copied when duplicating a chart. Candles, price overlays, crosshairs, and drawings share the scale; volume and oscillator panes retain their own axes. Auto-fit remains active. If visible price data or an overlay includes zero or negative values, the chart temporarily uses linear scaling and shows a notice. Fibonacci levels retain their configured arithmetic price ratios.

OHLC bars use a left tick for the opening price and a right tick for the closing price.

### Drawings and measurement

Open **Draw** on a chart to show the drawing tools window. Drag its title bar to move or dock it. Floating panels stay inside the main application window on desktop and inside the canvas in the browser. The window stays open between drawings, and its position and open/closed state are saved. Use the chart selector at the top to choose which chart receives the tool.

Choose a tool:

- **Horizontal level** and **Text note:** click to place.
- **Trend line**, **Ray**, and **Rectangle:** drag between two points.
- **Fibonacci retracement:** click the start and end of a move, or drag between them.
- **Measure:** drag to see price change, percentage change, bar count, and elapsed time. **Shift-drag** is a shortcut that works without choosing a tool.

After placing a drawing, the chart returns to **Select / pan**. Click a drawing to select it, drag its body to move it, or drag an endpoint to reshape it. Right-click or double-click a drawing to edit its color, prices, text, lock state, or timeframe visibility. Choose **Apply** to commit settings. Locked drawings remain selectable but cannot be dragged or deleted until unlocked.

**Delete** removes the selected unlocked drawing. **Esc** cancels an unfinished drawing or drag, clears a measurement, or deselects a drawing. Measurements are temporary and do not create saved objects. Dragging empty chart space continues to pan.

Drawings are saved by symbol and shared across charts of that symbol. Each drawing can appear on all timeframes or just one, and **Draw → Show drawings** controls visibility for the current chart. Anchors use timestamps and prices, so loading earlier history does not shift annotations onto different bars. Drawings do not expand the automatic price range beyond the visible market data and indicators.

Fibonacci retracements start with levels 0, 0.236, 0.382, 0.5, 0.618, 0.786, and 1. Level 0 sits at the second anchor and level 1 at the first; **Reverse** swaps them. Right-click the drawing or choose **Edit selected** in the tools window to customize up to 24 levels, including negative ratios and extensions beyond 1. Each level has its own visibility and color. Settings also include left/right extensions, ratio or percentage labels, prices, a baseline, and optional background shading.

Undo and redo cover drawing creation, movement, settings, locking, and deletion across symbols. Each completed drag is one edit. The most recent 200 edits are available during the current session; saved drawings persist after restarting, while undo history resets.

### Indicators and colors

Open **Indicators** to search for and add an indicator. Expand an entry under **On this chart** to edit its settings.

SMA, EMA, and VWMA indicators each have a **Line color** setting. New moving averages automatically cycle through a color palette to help distinguish them.

**Swing highs / lows (pivot points)** marks a high strictly above the preceding N and following N highs, or a low strictly below the corresponding lows (N = 5 by default). Pink triangles mark highs; cyan triangles mark lows. Each can be toggled and colored separately. A marker is placed on the pivot bar only after the following N bars have closed: it was not available on the pivot day itself. Equal highs/lows are excluded. Confirmation uses provider session times where available, otherwise the interval boundary; weekly pivots conservatively wait for the week boundary or the next weekly bar.

**Donchian channel** is a separate indicator. It plots the highest high and lowest low over the last N bars (20 by default), with an optional midpoint. Enable **Exclude current bar** to compare price against the previous N bars. It uses the chart’s timeframe.

**Keltner channels** combine an EMA with configurable ATR bands. **Stochastic** provides separate lookback, %K smoothing, and %D smoothing settings; **rate of change** shows the percentage move over N bars. **VWMA** weights closing prices by volume. **OBV** accumulates signed volume from zero; unavailable volume leaves gaps rather than implying no trading.

**MA ribbon colored bars** combines the 20, 50, 100, 150, and 200 EMAs. Their ordering determines the bar color, from strong bearish to strong bullish. Its settings include:

- Independent toggles for colored bars, moving average lines, and bullish/bearish background shading.
- The chart's timeframe or a custom source timeframe: 1h, 4h, 1D, or 1W.
- Separate colors for all five moving averages.

Historical ribbon values are aligned without looking ahead to future source bars. Values based on an unfinished source bar can change as new data arrives. Indicator settings are saved per chart and copied when a chart is duplicated.

### Watchlists

**At a glance** is an editable favorites list, initially containing SPY, BTC, GLD, VIX, QQQ, and RSP. Starter lists cover crypto, metals and miners, US sectors, futures, bonds and credit, currencies, global markets, investment styles, and major company groups. **Macro & economic proxies** collects tradable instruments and indices for rates, credit, inflation protection, commodities, and volatility; it does not contain economic releases such as CPI or employment reports.

Open **+ Add panel → Watchlist** and choose a list to open another independent window. Each window has its own list selection, can dock or float inside the app, and can be closed and reopened. **Lists → Open list in another window** opens a second view of the same list. Windows and their selections persist across restarts. Edits to a list appear in every window displaying it.

Use **Lists** to create, rename, or delete lists, then enter symbols to add them. Right-click a symbol to reorder, remove, or copy it to another list. Existing custom lists are preserved when starter lists are added; deleted starters stay deleted.

Watchlist percentages show the regular-session change, including the last completed session during pre/post-market trading. Nasdaq supplies separate regular and extended quotes; Yahoo regular-session metadata is the fallback. They refresh approximately every minute for symbols in open watchlist windows and charts; unopened starter lists do not trigger downloads. Requests are staggered, and stale or failed quotes appear muted; hover for the quote time or error. Chart refreshes also request a fresh sidebar quote.

Fresh extended-hours quotes appear as an amber **Pre** or **Post** indicator in the watchlist and a dotted horizontal price line on the chart. Hover for the quote time and extended-hours change versus the regular close. These prices do not enter candles or the sidebar percentage. When the price is outside the visible scale, an edge tag points toward it; the automatic price range stays based on candles and indicators. Extended quotes come from Nasdaq, with Yahoo's recent extended-session minutes as a fallback. Quotes older than 30 minutes, or snapshots not refreshed for 10 minutes, lose the live marker; cached values remain identified as stale in the tooltip. Provider delays still apply.

### Options and screener

Use **+ Add panel → Options chain** or **Screener** in the menu bar. Both are movable, dockable panels that stay inside the main application window. Their open state, position, and settings are saved.

The options window starts with the active chart's symbol. Enter another underlying and press **Load**, or select **Use active chart**. Choose an **Expiry** and switch between **Calls** and **Puts**. Only one side is displayed at a time, with higher strikes at the top. Muted cyan and purple row shading, drawn from the MA ribbon palette, distinguish **ITM** and **OTM**. A subtle neutral highlight marks the nearest listed strike (**ATM**). Moneyness uses the underlying price in the chain snapshot; a tie between nearest strikes marks the lower strike as ATM. New selections scroll to ATM automatically; `--` means the provider did not supply a value. Open interest and volume are contract counts. **Open underlying chart** returns to analysis of the underlying; this view does not chart option contracts or place trades.

Choose a **Linked chart** of the same underlying, then hover an option row to preview its strike and expiry on that chart. Click to pin the marker; the mouse crosshair remains independent. The pin survives restarts. **Clear option** removes it, and **Show expiry** pans the chart to its date. Off-screen markers point toward the relevant edge. Expiry anchors use the date at midnight UTC, not a settlement cutoff; future spacing uses the chart interval. Markers leave automatic price fitting unchanged.

Enable **Strike ladder** to show open interest or volume beside the linked chart's price axis. Bar lengths compare contract counts for the selected expiry and side; hover for the exact value and snapshot time. The ladder follows the options window's underlying, expiry, and Calls/Puts selection. Its selected expiry continues refreshing when the options window is closed. Pins, ladder visibility, and metric choices are saved.

Opening Options performs a one-row expiry lookup, then loads contracts only for the selected expiry. Opening the expiry picker discovers the remaining dates from Nasdaq's near-money rows; the date list is cached for a day. Each expiry has its own cache. Calls and puts for that expiry arrive together, so switching sides needs no request. Revisiting a recently loaded expiry uses its cache immediately. Requests paginate within that expiry if necessary, with a notice if the provider's 20,000-row limit is reached. This view supports Nasdaq's US stock and ETF options, without calculated Greeks or implied volatility.

The screener covers common stocks listed on Nasdaq, NYSE, and AMEX. Choose **Largest companies**, **Top gainers**, **Top losers**, or **Most active**. Expand **Filters** to set minimum price, market capitalization in billions of dollars, share volume, or an exact provider sector name, then **Apply filters**. Results are paginated in groups of 100; **Find on this page** searches only the current page.

Click a result to load its chart, or Ctrl/Cmd-click to open a new chart. Right-click for options or to choose a watchlist to add the symbol to. Screeners and chains refresh every 60 seconds while open (or while a linked strike ladder is enabled), stop polling when no longer displayed, and retain the last successful snapshot on errors. The last chain for each underlying/expiry and the last screener page are cached for offline use.

## Company fundamentals and earnings

Choose **Facts** on a chart, or **Add panel → Fundamentals**, for company profiles, sector/industry, market cap, dividend information, and recent earnings. The window follows the selected chart by default; enter a symbol to inspect another company independently.

US company charts display **E** markers beside the date axis. Hover for reported EPS, consensus, and surprise; click for company facts. Nearby events combine into a **+** marker when zoomed out. Upcoming dates appear in gold and are estimates, not confirmed schedules. Toggle markers in **View → Earnings markers**.

Enable **EPS line (reported earnings)** in **Indicators** or the Fundamentals window. The separate pane can show quarterly reported EPS or the sum of four consecutive quarters. Date-only reports become available on the following day, because their release time is unknown. The line never starts at the fiscal quarter end. This is reported EPS, not an IBD rating; the provider's EPS basis may differ from GAAP.

Nasdaq supplies a short recent earnings history, typically four quarters. Chartroom caches reports and retains older ones as new reports arrive; it cannot reconstruct a long historical EPS curve from the initial snapshot. Fundamentals refresh every six hours while in use, with a manual **Refresh** button. Unsupported symbols show no invented events or values.

All displayed timestamps and chart time boundaries use the computer's or browser's local timezone, including daylight-saving changes. Published date-only earnings and expiry dates retain their calendar date. Stored timestamps and provider requests remain UTC; four-hour candle aggregation is unchanged.

## Market data

Chartroom uses the public endpoints identified in [OpenTerminal](https://github.com/ErTasselli/OpenTerminal), accessed directly without running its server or requiring API keys:

| Data | Provider behavior |
| --- | --- |
| US stock/ETF daily history | Yahoo chart OHLC (two years first, then the full history in the background); Nasdaq daily chart fallback, then Stooq end-of-day CSV |
| US stock/ETF sidebar quotes | Nasdaq quote info, falling back to Yahoo minute metadata |
| Company facts, earnings dates and EPS | Nasdaq summary, profile, earnings surprise and earnings calendar |
| Hourly/weekly history, futures, indices, USD crypto composites | Yahoo chart endpoint |
| Explicit crypto pairs such as `BTCUSDT` | Binance klines and 24-hour ticker |
| Options chains | Nasdaq option-chain endpoint |
| US stock screener | TradingView America scanner |

The chart status identifies the actual provider and quote currency. `BTC`, `ETH`, and `SOL` retain their Yahoo USD composites. Binance pairs must be entered explicitly: USDT prices are not silently substituted for USD prices, and their sidebar percentages represent a rolling 24-hour change. Binance currently loads up to 1,000 candles per interval and accumulates updates in its cache. Access depends on region; if Binance is unavailable, use a USD composite for a separate chart.

Supported timeframes are 1h, 4h, 1D, and 1W. Yahoo hourly requests cover up to 729 days; other history is limited to what the provider returns. Four-hour candles are aggregated in UTC buckets. Provider changes replace the cached series instead of mixing historical price adjustments. Nasdaq fallback candles represent completed sessions only, and a provider notice says so. Chart history requests go ahead of queued quote and fundamentals requests, and earnings data loads once the chart is shown. Line-only responses are never turned into artificial OHLC candles.

Open charts and custom indicator source series refresh approximately every 60 seconds. Charts positioned at the latest candle follow new bars; historical views retain their position. Downloads run asynchronously. An empty chart shows a centered loading state; failed initial downloads offer **Retry**. The loading illustration is static to preserve idle rendering. A failed refresh retains cached data and retries later.

Invalid zero-price VIX candles are rejected, including ones saved by older builds. Recent daily VIX candles can be rebuilt from hourly data when every expected hourly bucket is available, using Yahoo’s session boundaries. Rebuilt candles are identified in the chart status; incomplete coverage leaves the candle unavailable or retains a previously valid cached candle.

These public endpoints may return delayed data, change without notice, restrict access by region, or rate-limit requests. Stooq may require browser verification and is used only when valid CSV is returned. Chartroom does not substitute synthetic prices when data is unavailable. Prices use the provider's OHLC values rather than adjusted close. Futures use Yahoo's `=F` symbols without additional contract-roll adjustments.

## Saved state and offline use

Workspace settings and cached market data are stored locally:

| Platform | Location |
| --- | --- |
| macOS | `~/Library/Application Support/ChartroomCpp` |
| Windows | `%LOCALAPPDATA%/ChartroomCpp` |
| Linux | `$XDG_DATA_HOME/chartroom-cpp`, or `~/.local/share/chartroom-cpp` |
| Browser | IndexedDB for the site's origin |

Browser storage is separate from desktop storage and is subject to browser quotas and site-data clearing. Changing the site's hostname or port creates a separate workspace.

Desktop command-line options include:

| Option | Purpose |
| --- | --- |
| `--data-dir PATH` | Use a different workspace and cache directory |
| `--offline` | Open using cached history without downloading updates |
| `--fetch SYMBOL` | Check the market-data connection without opening a window |
| `--import-julia PATH` | Import a workspace from the Julia version on first launch |
| `--version` | Print the build version and local build time |
| `--help` | List all available options |

On macOS, pass options to `build/chartroom.app/Contents/MacOS/chartroom` rather than to `open`.

### Importing from the Julia version

To migrate an existing workspace, pass the path to its `.chartroom` directory on the first launch. For example, on macOS:

```sh
build/chartroom.app/Contents/MacOS/chartroom --import-julia /path/to/chartroom/.chartroom
```

Import runs only when there is no saved C++ workspace. It copies charts, indicators, watchlists, time ranges, and cached data without modifying the source files. The panel arrangement is recreated, and the price axis uses automatic fitting.

## Idle behavior

Chartroom is designed to stay open. It renders in response to input, data updates, window changes, and UI timers such as text-cursor blinking. Once the interface settles, it stops issuing frames until another event needs one. Data refreshes and workspace saves are scheduled independently of rendering.

Minimized desktop windows and hidden browser tabs skip rendering and redraw when shown again. Background work continues on its own schedule; browser background timer throttling can delay refreshes in hidden tabs.

## Build versions

The sidebar shows the version of the running application, such as `v0.1.0+12`. Hover over it for the local build time and update status. Every application build automatically advances the build number, including rebuilds with no source changes. Native and browser builds share a counter in `.build-sequence` within the checkout; keep that file to retain the sequence. Separate fresh checkouts start their own sequence.

After a successful link, the build publishes `build-info.json`. Once a minute, a running app checks the manifest for its installation: desktop builds read the file beside the executable (inside Resources on macOS), and browser builds fetch it from the server. A newer build shows **Restart to update** on desktop or **Reload** in the browser. The Reload button saves the workspace before reloading. These checks compare against the installed or served build, not a remote release catalog.

Distribute `build-info.json` with the executable or web files. Web hosts should revalidate HTML, JavaScript, and Wasm files when reloading so an older cached bundle cannot outlive its manifest.

## Development

Run the core and persistence tests after a desktop build:

```sh
ctest --test-dir build -C Release --output-on-failure
```

The `chartroom_gui_tests` executable checks native UI behavior and requires a desktop with an OpenGL context. It is located in `build/` for single-configuration builds or `build/Release/` for a Visual Studio Release build.

The optional [browser integration test](../test/browser_test.cjs) uses Node.js and Playwright. With the preview server running on port 8080:

```sh
CHARTROOM_URL=http://localhost:8080/chartroom.html node test/browser_test.cjs
CHARTROOM_URL=http://localhost:8080/chartroom.html node test/browser_drawing_test.cjs
CHARTROOM_URL=http://localhost:8080/chartroom.html node test/browser_idle_test.cjs
CHARTROOM_URL=http://localhost:8080/chartroom.html node test/browser_build_test.cjs
CHARTROOM_URL=http://localhost:8080/chartroom.html node test/browser_vix_test.cjs
CHARTROOM_URL=http://localhost:8080/chartroom.html node test/browser_quote_test.cjs
CHARTROOM_URL=http://localhost:8080/chartroom.html node test/browser_log_test.cjs
CHARTROOM_URL=http://localhost:8080/chartroom.html node test/browser_market_test.cjs
CHARTROOM_URL=http://localhost:8080/chartroom.html node test/browser_options_test.cjs
CHARTROOM_URL=http://localhost:8080/chartroom.html node test/browser_sessions_test.cjs
CHARTROOM_URL=http://localhost:8080/chartroom.html node test/browser_watchlists_test.cjs
```

Set `PLAYWRIGHT_MODULE` if Playwright is outside the normal Node module path, or `CHROMIUM_EXECUTABLE` to use a specific Chromium binary. The tests cover data loading, navigation, drawing gestures, undo/redo, measurement, multiple charts, persistence, resizing, pixel-density changes, Fibonacci settings, floating-window persistence, build update notices, idle rendering, and background refresh deadlines.

For a bounded desktop idle check, run the executable with `--idle-check 10`. It prints frame and wake counts after ten seconds, including startup. Add `--offline` to exclude network updates and use `--data-dir PATH` for an isolated workspace. In the browser, `Module.chartroomStats` exposes frame and wake counts in the developer console.

Run `python3 test/proxy_test.py` to check the browser proxy route validation without network access.

The main source files are:

| Files | Responsibility |
| --- | --- |
| [core.cpp](../src/core.cpp), [core.hpp](../src/core.hpp) | Chart navigation, market-data parsing, indicator calculations, and serialization |
| [drawings.cpp](../src/drawings.cpp), [drawing_ui.cpp](../src/drawing_ui.cpp) | Drawing anchors, edit history, measurement, and chart interactions |
| [state.cpp](../src/state.cpp), [state.hpp](../src/state.hpp) | Data updates, caches, watchlists, and workspace persistence |
| [net.cpp](../src/net.cpp), [net.hpp](../src/net.hpp) | Desktop and browser HTTP requests |
| [market.cpp](../src/market.cpp), [providers.cpp](../src/providers.cpp), [market_ui.cpp](../src/market_ui.cpp) | Provider schemas, fallback routing, options chains, and screener |
| [app.cpp](../src/app.cpp) | Interface and chart rendering |
| [main.cpp](../src/main.cpp), [render_schedule.cpp](../src/render_schedule.cpp), [scheduler.js](../web/scheduler.js) | Platform startup, event scheduling, UI deadlines, and command-line options |

Dependency and font licenses are listed in [Third-party notices](../THIRD_PARTY_NOTICES.md).
