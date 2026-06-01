# Manual Inventory Adjustments

This document describes the file-backed operator entry for manually adjusting per-strategy live inventory before broker reconciliation.

## Files

- Inventory store: `runtime/strategy_inventory_store.ini`
- Manual adjustment input: `runtime/strategy_inventory_adjustments.ini`

The live runtime creates `runtime/strategy_inventory_adjustments.ini` automatically if it does not exist.

## Workflow

1. Stop the live runtime.
2. Add one or more `[inventory_adjustment.<id>]` sections to `runtime/strategy_inventory_adjustments.ini`.
3. Start the live runtime.
4. The runtime:
   - reads the persisted inventory store,
   - applies any adjustment ids that have not been applied before,
   - records applied ids back into `runtime/strategy_inventory_store.ini`,
   - seeds strategy inventory from the adjusted store,
   - reconciles the aggregate against broker account positions.
5. After an adjustment id has been applied once, the runtime will not apply the same id again on later restarts.

## Section format

```ini
[inventory_adjustment.example_external_manual_seed]
enabled=true
account_id=simnow
strategy_id=external_manual
instrument=ag2606
exchange=SHFE
operator_id=alice
reason_code=manual_trade_import
reason_text=Seed broker-held overnight inventory before reconciliation
long_yesterday_delta=1
long_yesterday_average_price=5120
```

## Required fields

- `account_id`
- `strategy_id`
- `instrument`
- `operator_id`
- `reason_code`

## Supported delta fields

- `long_today_delta`
- `long_yesterday_delta`
- `short_today_delta`
- `short_yesterday_delta`

Matching optional average-price fields:

- `long_today_average_price`
- `long_yesterday_average_price`
- `short_today_average_price`
- `short_yesterday_average_price`

## Rules

- Positive deltas that increase a bucket should provide the matching `*_average_price`.
- Negative deltas reduce an existing bucket and must not make the resulting quantity negative.
- Use `strategy_id=external_manual` for inventory that is not currently attributed to a running strategy.
- If the persisted inventory store contains non-flat inventory for a detached strategy id other than `external_manual`, live startup fails fast.

## Reconciliation output

The runtime writes reconciliation results into `runtime/strategy_inventory_store.ini` under `reconciliation_runs.<account_id>`.

Relevant fields include:

- `aggregate_match`
- `applied_adjustment_count`
- `mismatch_summary`
- `manual_adjustments_path`
