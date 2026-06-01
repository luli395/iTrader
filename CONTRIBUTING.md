# Contributing to iTrader

Thanks for your interest in iTrader. This repository is a public, sanitized version of the platform/runtime code, plus a small set of example strategies.

## Scope

Good contributions include:

- bug fixes in the runtime, UI API, or example strategies
- CMake and Windows build improvements
- safer configuration handling
- documentation improvements
- tests, fixtures, and small reproducible examples
- strategy ABI improvements that remain backward-compatible when possible

Please do not contribute:

- private trading strategies
- real account credentials, auth codes, broker account IDs, or production front addresses
- proprietary market data
- runtime state, backtest outputs, or deployment-specific files
- vendor SDK binaries unless their license explicitly permits redistribution

## Development Setup

iTrader currently targets Windows because it loads strategy DLLs and can optionally link against the Windows CTP SDK.

Basic build without CTP SDK files:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
```

CTP support is disabled by default. Enable it only after installing the vendor SDK locally:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
  -DITRADER_ENABLE_CTP=ON `
  -DCTP_SDK_DIR=C:/path/to/traderapi `
  -DITRADER_ENABLE_CTP_MD=ON `
  -DCTP_MD_SDK_DIR=C:/path/to/mduserapi
```

## Pull Requests

Before opening a pull request:

1. Keep the change focused.
2. Run the relevant build or explain why you could not.
3. Avoid unrelated formatting churn.
4. Make sure `git status` is clean except for intentional changes.
5. Check that no credentials or generated artifacts are included.

Useful local checks:

```powershell
git status --short
git grep -n -i "password\\|auth_code\\|secret\\|token\\|credential"
```

## Style

- Prefer clear C++20 over clever abstractions.
- Keep strategy ABI changes deliberate and documented.
- Keep generated/runtime files out of git.
- Use environment-variable placeholders in examples.
- Treat live trading paths as safety-sensitive.

## License

By contributing, you agree that your contribution is licensed under the Apache License, Version 2.0.
