# Changelog

All notable public changes to iTrader will be documented in this file.

## [0.1.0] - 2026-06-01

Initial public release.

Included:

- C++20 trading runtime scaffold
- strategy DLL ABI headers
- backtest and live runtime entrypoints
- browser control center assets
- sanitized example configs
- Apache-2.0 license and NOTICE
- example strategies:
  - `sample_strategy`
  - `timed_roundtrip_strategy`
  - `noop_strategy`

Excluded by design:

- private production strategies
- credentials and account identifiers
- market data
- runtime state
- generated backtest outputs
- deployment-specific notes
