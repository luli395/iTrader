# Security Policy

## Supported Versions

The public repository is currently pre-1.0. Security fixes are applied to the `main` branch.

| Version | Supported |
| --- | --- |
| `main` | Yes |
| `0.1.x` | Yes |
| `< 0.1.0` | No |

## Reporting a Vulnerability

Please do not open a public issue for vulnerabilities involving credential handling, live trading safety, order routing, or configuration exposure.

Report security concerns privately by opening a GitHub security advisory for this repository when available. If advisories are unavailable, contact the maintainer through the GitHub profile associated with this repository and include:

- affected files or component
- reproduction steps
- expected impact
- whether credentials, account identifiers, or production trading paths may be exposed

Please do not include real broker credentials, auth codes, private front addresses, proprietary market data, or production account identifiers in reports.

## Security Scope

In scope:

- credential leakage risks
- unsafe default configuration
- accidental tracking of runtime state or generated trading artifacts
- path traversal or local file exposure in the UI/API server
- order-routing or dry-run bypasses
- recovery logic that could duplicate live order intent

Out of scope:

- vulnerabilities in third-party broker SDKs
- attacks requiring modification of local source code before build
- issues in private strategies not present in this repository
- proprietary market-data quality problems

## Disclosure

The maintainer will try to acknowledge reports within 7 days and provide a remediation plan or status update when enough detail is available.
