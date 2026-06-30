# iTrader

`iTrader` is a C++ quant trading platform scaffold built around the local CTP Trader API SDK in `sdk/traderapi/20251103_traderapi64_se_windows`.

It currently provides:

- Multi-account orchestration
- Multi-strategy execution
- Two runtime modes: `backtest` and `live`
- C++ strategy plugins compiled as DLLs and loaded at runtime
- A working sample strategy DLL (`sample_strategy`)
- A timed round-trip toy strategy DLL (`timed_roundtrip_strategy`) that alternates between opening a 1-lot long and flattening it on the next interval
- An intraday breakout strategy DLL (`breakout_strategy`) adapted from a TBQuant-style bar breakout system to the iTrader plugin ABI
- A backtest engine driven by CSV tick data or recursively loaded tick-data folders
- A live CTP trader gateway that logs in and sends orders
- A live CTP market-data gateway that streams quotes when the MD SDK is configured
- A standalone `itrader_ctp_md_recorder` executable that persists every streaming MD tick into AGTICK-compatible CSV shards for restart warmup / replay
- Automatic live gateway reconnect / resubscribe safeguards for transient CTP disconnects
- A centralized live risk gate that can reject unsafe orders before they reach `ReqOrderInsert`
- A browser-based control-center UI in `ui/index.html` for editing sample account/strategy setups and generating INI files

## Architecture

- `include/itrader/strategy_api.hpp` defines the shared strategy ABI
- `src/platform.cpp` implements config loading, plugin loading, backtest orchestration, and live orchestration
- `src/ctp_adapter.cpp` wraps the CTP Trader API callbacks and order submission
- `strategies/sample_strategy.cpp` shows how to write a DLL strategy in C++
- `strategies/timed_roundtrip_strategy.cpp` shows a timer-gated toy execution strategy that buys when flat and sells to flatten on the next cycle
- `strategies/breakout_strategy.cpp` ports the provided `intraDayBreakout` idea into iTrader using internally aggregated bars plus IOC limit orders

## Live market data behavior

If the CTP market-data SDK is configured, live mode uses the dedicated MD front and streams quotes through `OnRtnDepthMarketData`.

If the MD SDK is unavailable, the platform can still fall back to snapshot polling through the trader API.

## Build

Open the workspace in Visual Studio or VS Code with CMake support and build the project.

By default, the build looks for the market-data SDK under:

- `sdk/6.7.11_P4_mduserapi_20251125/v6.7.11_P4_20251125_winApi/mduserapi/20251125_mduserapi64_se_windows`

Expected outputs:

- `itrader.exe`
- `itrader_ui_api.exe`
- `itrader_ctp_md_recorder.exe` when the MD SDK is available
- `sample_strategy.dll`
- `timed_roundtrip_strategy.dll`
- `breakout_strategy.dll`
- `thosttraderapi_se.dll` copied into the build configuration directory during CMake configure
- `thostmduserapi_se.dll` copied into the build configuration directory when the MD SDK is available

## Run

### Control Center UI

You can either:

- open `ui/index.html` directly in a browser, or
- run `itrader_ui_api.exe` and open `http://127.0.0.1:8080/` for config-backed UI data

The unified local entrypoint is now `itrader.exe` with **no arguments**: it launches `itrader_ui_api.exe` for you and opens the Control Center in the browser.

If you set `ITRADER_UI_PASSWORD` in the workspace `.env` (or the process environment), the UI/API server requires HTTP Basic authentication before serving either the dashboard or `/api/*` endpoints. Browsers will prompt for credentials automatically; the password must match `ITRADER_UI_PASSWORD`, while the username can be any non-empty value.

The UI provides:

- a mode switch for `backtest` and `live`
- editable account and strategy forms
- per-account strategy attachment from the strategy catalog
- strategy-catalog cards that focus on strategy definition, with account bindings managed from the account panels
- a TradingView Lightweight Charts panel that renders candlesticks plus trade-signal markers in the Activity view
- instrument tabs plus an account filter so operators can switch chart symbols and isolate markers for one account at a time
- strategy tabs beneath the account filter so the same instrument/account view can be narrowed to one strategy
- save-to-workspace persistence back to `configs/backtest.ini` or `configs/live.ini`
- generated INI preview and download/copy actions
- a launch-command preview for the CLI executable

The local API currently exposes:

- `GET /api/health`
- `GET /api/state?mode=backtest`
- `GET /api/state?mode=live`
- `POST /api/config?mode=backtest|live`

The local API currently serves config-backed UI state from the sample INI files and the static dashboard assets. It does not yet stream live engine state from the trading runtime.

Live runtime files are now isolated per config name under `runtime/<config-stem>/`, for example:

- `configs/live.ini` -> `runtime/live/`
- `configs/live_probe.ini` -> `runtime/live_probe/`
- `configs/live_prod.ini` -> `runtime/live_prod/`

In live mode, the runtime now also persists strategy semantic state to `runtime/<config-stem>/strategy_state_store.ini`. The first concrete consumer is `ag_breakout_strict_strategy`, which restores entry anchors, reentry locks, and unfinished exit-plan intent after a restart (after warmup + broker position hydration). Live strategy plugins can also opt into a second recovery phase via `IStrategy::replay_live_recovered_trades(...)`, which replays broker-recovered fills back into strategy-local semantics after state restore.

Live startup now also queries broker working orders through `QryOrder`, rebuilds attachment-level open-order snapshots, and reattaches any order refs that can be matched back to persisted strategy state. That means same-process reconnects and cold restarts can recover more of the broker-side order lifecycle before the next live tick arrives.

Live startup/reconnect now also queries broker trade history through `QryTrade`. Recovered fills are deduplicated against the persisted `strategy_fill_ledger.*` entries in `runtime/<config-stem>/strategy_inventory_store.ini`, then routed back to attachments via `OrderRef` suffix strategy codes plus any persisted `broker_order_id -> client_order_id` mappings. This backfills fill history / closed-order telemetry after a cold restart or reconnect. For strategies that implement `replay_live_recovered_trades(...)` (currently `ag_breakout_strict_strategy`), those recovered fills are also replayed into strategy-local semantic state after broker position hydration + state restore, so local trade counters / entry-exit slot lifecycle can heal without double-applying broker positions. Broker-only pre-start realized PnL is still not reconstructed.

Live CTP order routing no longer relies on `OrderMemo` for strategy attribution. Instead, each live strategy must declare `order_ref_strategy_code=01..99`, and the trader gateway encodes that value into the **last two digits** of the numeric `OrderRef`.

Because the local CTP SDK defines `OrderRef` as `char[13]`, only **12 visible characters** fit safely. The runtime therefore uses this fixed-width format:

- `10-digit` per-account monotonically increasing sequence
- `2-digit` strategy code suffix

For example:

- sequence `1`, strategy code `01` -> `000000000101`
- sequence `2`, strategy code `02` -> `000000000202`
- sequence `3`, strategy code `01` -> `000000000301`

The `10-digit` sequence is maintained independently inside each trader gateway / account session and is seeded from that account's `MaxOrderRef` after login. It does **not** need to be globally increasing across all accounts.

Per account, `order_ref_strategy_code` values must stay unique, otherwise broker callbacks would become ambiguous.

For charting, `GET /api/state` now includes a `chart` payload:

- in `backtest` mode, candles are reconstructed from the configured replay CSV or tick-data folder for each configured strategy instrument
- the dashboard surfaces those instruments as chart tabs; if your config only trades one instrument, you will naturally see one tab
- trade markers are derived from filled runtime orders and can be filtered by account and strategy in the UI
- in `live` mode, the runtime now publishes rolling candle telemetry into `runtime/live_telemetry.ini`; once the live engine has produced fresh telemetry, the UI API can serve those candles back to the dashboard

Unassigned strategies are allowed in the UI/config now. They remain in the INI, but the runtime skips them until they are attached to at least one account. A single strategy can also be attached to multiple accounts at the same time via `accounts=acct_a,acct_b`.

In the dashboard, strategy attachment is intentionally handled from each account panel. The Strategy catalog stays strategy-centric: expanded cards edit the DLL path, instruments, and parameters, while folded cards show only a compact runtime/parameter summary instead of repeating account bindings.

### Backtest

After building the desired configuration, run the executable from the workspace root with:

- mode: `backtest`
- config: `configs/backtest.ini`

If you launch with `--mode backtest` but omit `--config`, the CLI now also defaults to `configs/backtest.ini`.

For CLI backtests, pass `--output-dir <dir>` or `--backtest-output-dir <dir>` to collect the run artifacts in one directory:

```powershell
.\build\Release\itrader.exe --mode backtest --config .\configs\backtest_breakout.ini --output-dir .\reports\breakout_run_001
```

The output directory is created automatically and receives:

- `trader_backtest.log`
- a copy of the INI file used for the run
- `performance_metrics.csv`
- `performance_metrics.cvv`
- `daily_cumulative_pnl.csv`
- `daily_cumulative_pnl.svg`

If you launch `itrader.exe` with no arguments, it now opens the Control Center UI instead of starting a backtest immediately.

### Live

Edit `configs/live.ini` first:

- set `front`
- set `md_front`
- set `broker_id`
- set `user_id`
- set `password`
- optionally set `app_id` and `auth_code`

Then run in `live` mode with `configs/live.ini`.

If you launch with `--mode live` but omit `--config`, the CLI now defaults to `configs/live.ini`.

For safer separation there is also a production template at `configs/live_prod.ini`.

Live configs now support environment-variable placeholders such as `${ITRADER_SIMNOW_USER_ID}` or `${ITRADER_PROD_PASSWORD}`. The runtime resolves them from:

- the current process environment, and then
- the workspace `.env` file if present

An `.env.example` file is included as a template, but you should keep real production credentials in your local `.env` or deployment environment, not in git-tracked INI files.

Recommended live config sections are now:

- `[live]`
	- `environment=simnow|probe|prod`
	- `poll_interval_ms=...`
	- `iterations=0` for long-running live services (`probe` configs may intentionally keep a finite iteration count)
	- `dry_run=true|false` to keep the full live stack connected while rejecting order submissions at the platform layer before they reach the broker
	- optional historical warmup settings for cold-starting intraday strategies:
		- `warmup_enabled=true`
		- `warmup_data_dir=...` or `warmup_csv=...`
		- `warmup_trading_day=auto|YYYYMMDD`
- `[risk]`
	- `enabled=true`
	- `allow_market_orders=false`
	- `flatten_only=false`
	- `max_order_volume=...`
	- `max_abs_net_position=...`
	- `max_long_position=...`
	- `max_short_position=...`
	- `max_outstanding_orders=...`
	- `max_daily_loss=...`
	- `max_quote_staleness_ms=...`
	- `max_price_deviation_ratio=...`
- `[account.<id>]`
	- `reconnect_enabled=true`
	- `reconnect_retry_interval_ms=3000`
	- `reconnect_max_attempts=0` (`0` means unlimited recovery attempts)
- `[strategy.<id>]`
	- `order_ref_strategy_code=01..99` (required in `live` mode; unique per bound account)

When `environment=prod`, the live runtime now enforces a stricter baseline:

- `iterations` must stay at `0`
- `[risk]` must be enabled
- `production_mode` must remain `true`
- obvious SimNow-style broker/app/account settings are rejected before startup

When live warmup is enabled, the runtime replays historical AGTICK data into each strategy before `on_start()` so indicator/bar caches can be rebuilt from the current trading day. Order submission is disabled during this replay phase, which is especially useful for `ag_breakout_strict_strategy` and other strategies that build minute bars internally.

When `dry_run=true`, the runtime still connects trader + market-data sessions, performs warmup, recovers broker state, and runs strategy logic normally, but it emits a synthetic rejected order event at the platform layer instead of calling `ReqOrderInsert`. This is the preferred rehearsal mode when you want a full live-path smoke test without routing orders to the broker.

### Standalone CTP market-data recorder

The repo now includes a dedicated executable, `itrader_ctp_md_recorder.exe`, for the abnormal-restart recovery path:

- config file: `configs/ctp_md_recorder.ini`
- default MD front fallback: `tcp://101.230.198.41:56213`
- default output directory: `runtime/ctp_md_recorder/agtick/`
- output format: AGTICK-compatible CSV files named like `YYYYMMDD_ag2606.SHFE.csv`

The recorder is intentionally separate from `itrader.exe`, so you can keep it running as a lightweight quote-archive process even if the main strategy runtime is stopped or restarted.

Recommended recovery workflow:

1. keep `itrader_ctp_md_recorder.exe --config configs/ctp_md_recorder.ini` running during the trading session
2. point live strategy warmup to `warmup_data_dir=../runtime/ctp_md_recorder/agtick`
3. after an iTrader / strategy restart, the live runtime replays the recorder output up to the current restart moment before calling `on_start()`

Each CSV row is written in AGTICK layout (`time,symbol,current,high,low,volume,money,position,a1_v,a1_p,b1_v,b1_p`), so it can be consumed directly by the existing backtest/live warmup loaders.

The sample configs reference `build/Debug/sample_strategy.dll` directly so they work without post-build copy steps.

There is also a sample timer-driven backtest config at `configs/backtest_timed_roundtrip.ini` that references `build/Debug/timed_roundtrip_strategy.dll`.

There is a sample AG strict breakout backtest config at `configs/backtest_breakout.ini` that references `build/Release/ag_breakout_strict_strategy.dll` and replays AGTICK data from the configured tick-data folder.

There is a dedicated live SimNow breakout config at `configs/live_breakout_safe.ini`, prewired to the verified `30003/30013` fronts and `build/Debug/breakout_strategy.dll` with conservative fixed-lot settings.

## Strategy plugin contract

For a step-by-step guide on writing, building, and loading a strategy DLL, see:

- `docs/strategy-authoring.md`

Operational docs for day-to-day live maintenance and deployment:

- `docs/manual_inventory_adjustments.md`
- `docs/recorder-safe-deployment-checklist.md`
- `docs/remote-deployment-sop-14.103.137.127.md`

A strategy DLL must export two functions:

- `CreateStrategy`
- `DestroyStrategy`

The strategy class implements `itrader::IStrategy`.

Because the host and strategy exchange C++ objects, they should be built with the **same compiler toolchain and CRT mode**. This project sets MSVC to `/MD` or `/MDd` accordingly.

## Config format

### Backtest example

- `[account.<id>]` defines a simulated account
- `[strategy.<id>]` defines a strategy DLL, target account bindings, instruments, and custom parameters
- `[backtest]` defines the replay source; use either `csv=<AGTICK file>` or `data_dir=<AGTICK folder>`

### Live example

- `[account.<id>]` defines a CTP account and gateway settings
- `[strategy.<id>]` can bind a DLL strategy to one or more accounts using `accounts=...`
- `[strategy.<id>]` must also set `order_ref_strategy_code=01..99` in `live` mode so callbacks can be routed from `OrderRef`
- `[live]` defines the loop interval and iteration controls

## Next extension points

Useful follow-ups you can add on top of this scaffold:

- Persistent order/trade/account storage
- A REST or desktop UI layer
- Strategy hot-reload supervision
- Strategy semantic-state persistence on top of the new streaming MD recorder / replay engine
- More realistic backtest matching, fees, slippage, and margin simulation
