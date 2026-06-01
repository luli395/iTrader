# iTrader

[![CI](https://github.com/luli395/iTrader/actions/workflows/ci.yml/badge.svg)](https://github.com/luli395/iTrader/actions/workflows/ci.yml)

iTrader is a Windows/C++20 trading runtime scaffold with:

- pluggable C++ strategy DLLs
- backtest and live runtime modes
- file-backed INI configuration
- an optional browser control center
- optional CTP trader and market-data adapters

This public tree intentionally includes only the platform/runtime code and three example strategies:

- `strategies/sample_strategy.cpp`
- `strategies/timed_roundtrip_strategy.cpp`
- `strategies/noop_strategy.cpp`

Private production strategies, credentials, runtime state, market data, backtest outputs, and deployment notes are not included.

## Why iTrader Exists

Trading infrastructure is safety-sensitive and often hard to review because runtime code, private strategies, credentials, market data, and deployment scripts get mixed together. iTrader separates the public platform surface from private strategy logic so the reusable parts can be inspected, tested, documented, and improved in the open.

The project focuses on:

- a stable C++ strategy plugin ABI
- reproducible backtest/runtime configuration
- clear separation between public framework code and private trading logic
- safer handling of live-mode credentials and generated runtime state
- practical tooling for Windows and CTP-oriented workflows

## License

Licensed under the Apache License, Version 2.0. See `LICENSE` and `NOTICE`.

## Releases

See `CHANGELOG.md` for public release notes.

## Layout

- `include/itrader/` - public runtime and strategy ABI headers
- `src/` - platform, CLI, local UI API, and optional CTP adapter sources
- `strategies/` - example strategy DLL implementations
- `ui/` - browser control center assets
- `configs/` - sanitized example INI files
- `scripts/` - helper scripts for strategy scaffolding and recorder operation
- `docs/` - general architecture and strategy-authoring notes

## Build

Use Visual Studio or another CMake-capable Windows C++20 toolchain:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
```

By default, CTP support is disabled so the repository can build without shipping vendor SDK files.

To enable CTP trader or market-data support, install the vendor SDK locally and configure:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
  -DITRADER_ENABLE_CTP=ON `
  -DCTP_SDK_DIR=C:/path/to/traderapi `
  -DITRADER_ENABLE_CTP_MD=ON `
  -DCTP_MD_SDK_DIR=C:/path/to/mduserapi
```

Expected Debug outputs include:

- `build/Debug/itrader.exe`
- `build/Debug/itrader_ui_api.exe`
- `build/Debug/sample_strategy.dll`
- `build/Debug/timed_roundtrip_strategy.dll`
- `build/Debug/noop_strategy.dll`

When market-data support is enabled, `itrader_ctp_md_recorder.exe` is also built.

## Run

Backtest example:

```powershell
.\build\Debug\itrader.exe --mode backtest --config .\configs\backtest.ini --output-dir .\reports\sample_run
```

The sample config expects AGTICK-style CSV data under `data/ticks` unless you edit the INI.

Control Center UI:

```powershell
.\build\Debug\itrader_ui_api.exe
```

Then open:

```text
http://127.0.0.1:8080/
```

You can also open `ui/index.html` directly for the static sample UI.

## Configuration

The repository includes sanitized templates:

- `configs/backtest.ini`
- `configs/live.example.ini`
- `configs/ctp_md_recorder.example.ini`
- `.env.example`

Keep real credentials in your local environment or an untracked `.env` file. Do not commit account IDs, passwords, auth codes, private fronts, runtime state, or generated backtest outputs.

## Strategy Development

Strategy DLLs implement the ABI in `include/itrader/strategy_api.hpp` and export:

- `CreateStrategy`
- `DestroyStrategy`
- optionally `GetStrategySchema`

See `docs/strategy-authoring.md` and `strategies/sample_strategy.cpp` for a minimal pattern.

You can create a new local strategy skeleton with:

```powershell
python scripts/generate_strategy_template.py --name my_strategy
```

Private strategies should live outside this public repository or remain ignored until you intentionally publish them.
