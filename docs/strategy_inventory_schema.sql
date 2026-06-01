-- Strategy inventory persistence schema for iTrader
--
-- Design goals:
-- 1. Broker remains the authoritative account-level position source.
-- 2. The platform DB owns per-strategy attribution.
-- 3. Manual/external trades are represented through audited adjustments,
--    not by mutating inventory rows in place.
-- 4. Orders are assumed to be invalid after EOD, so cross-day recovery focuses
--    on fills, inventory state, broker snapshots, and reconciliation history.
--
-- Convention:
-- - Use strategy_id = 'external_manual' for operator-managed inventory that does
--   not currently belong to a real strategy.

CREATE TABLE strategy_fill_ledger (
    fill_id                INTEGER PRIMARY KEY,
    account_id             TEXT NOT NULL,
    strategy_id            TEXT NOT NULL,
    instrument             TEXT NOT NULL,
    exchange               TEXT NOT NULL,
    trading_day            TEXT NOT NULL,
    client_order_id        TEXT NOT NULL,
    broker_order_id        TEXT,
    broker_trade_id        TEXT,
    side                   TEXT NOT NULL CHECK (side IN ('buy', 'sell')),
    offset                 TEXT NOT NULL CHECK (offset IN ('open', 'close', 'close_today', 'close_yesterday')),
    fill_quantity          INTEGER NOT NULL CHECK (fill_quantity > 0),
    fill_price             NUMERIC NOT NULL CHECK (fill_price >= 0),
    fill_timestamp         TEXT NOT NULL,
    source                 TEXT NOT NULL DEFAULT 'runtime' CHECK (source IN ('runtime', 'manual_import')),
    created_at             TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX idx_strategy_fill_ledger_account_instrument_time
    ON strategy_fill_ledger(account_id, instrument, fill_timestamp);

CREATE INDEX idx_strategy_fill_ledger_strategy_time
    ON strategy_fill_ledger(strategy_id, fill_timestamp);

CREATE TABLE strategy_inventory_state (
    inventory_id                 INTEGER PRIMARY KEY,
    account_id                   TEXT NOT NULL,
    strategy_id                  TEXT NOT NULL,
    instrument                   TEXT NOT NULL,
    exchange                     TEXT NOT NULL,
    last_reconciled_trading_day  TEXT,
    long_today_quantity          INTEGER NOT NULL DEFAULT 0 CHECK (long_today_quantity >= 0),
    long_today_avg_price         NUMERIC NOT NULL DEFAULT 0 CHECK (long_today_avg_price >= 0),
    long_yesterday_quantity      INTEGER NOT NULL DEFAULT 0 CHECK (long_yesterday_quantity >= 0),
    long_yesterday_avg_price     NUMERIC NOT NULL DEFAULT 0 CHECK (long_yesterday_avg_price >= 0),
    short_today_quantity         INTEGER NOT NULL DEFAULT 0 CHECK (short_today_quantity >= 0),
    short_today_avg_price        NUMERIC NOT NULL DEFAULT 0 CHECK (short_today_avg_price >= 0),
    short_yesterday_quantity     INTEGER NOT NULL DEFAULT 0 CHECK (short_yesterday_quantity >= 0),
    short_yesterday_avg_price    NUMERIC NOT NULL DEFAULT 0 CHECK (short_yesterday_avg_price >= 0),
    realized_pnl                 NUMERIC NOT NULL DEFAULT 0,
    position_version             INTEGER NOT NULL DEFAULT 0,
    last_fill_id                 INTEGER,
    last_adjustment_id           INTEGER,
    updated_at                   TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    UNIQUE(account_id, strategy_id, instrument)
);

CREATE INDEX idx_strategy_inventory_state_account_instrument
    ON strategy_inventory_state(account_id, instrument);

CREATE TABLE inventory_adjustments (
    adjustment_id                INTEGER PRIMARY KEY,
    account_id                   TEXT NOT NULL,
    strategy_id                  TEXT NOT NULL,
    instrument                   TEXT NOT NULL,
    exchange                     TEXT NOT NULL,
    reason_code                  TEXT NOT NULL,
    reason_text                  TEXT,
    operator_id                  TEXT NOT NULL,
    long_today_delta             INTEGER NOT NULL DEFAULT 0,
    long_today_avg_price         NUMERIC,
    long_yesterday_delta         INTEGER NOT NULL DEFAULT 0,
    long_yesterday_avg_price     NUMERIC,
    short_today_delta            INTEGER NOT NULL DEFAULT 0,
    short_today_avg_price        NUMERIC,
    short_yesterday_delta        INTEGER NOT NULL DEFAULT 0,
    short_yesterday_avg_price    NUMERIC,
    realized_pnl_delta           NUMERIC NOT NULL DEFAULT 0,
    effective_trading_day        TEXT,
    approval_note                TEXT,
    created_at                   TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX idx_inventory_adjustments_account_instrument_time
    ON inventory_adjustments(account_id, instrument, created_at);

CREATE TABLE broker_position_snapshots (
    broker_snapshot_id           INTEGER PRIMARY KEY,
    reconciliation_run_id        INTEGER,
    account_id                   TEXT NOT NULL,
    instrument                   TEXT NOT NULL,
    exchange                     TEXT NOT NULL,
    trading_day                  TEXT,
    long_today_quantity          INTEGER NOT NULL DEFAULT 0 CHECK (long_today_quantity >= 0),
    long_yesterday_quantity      INTEGER NOT NULL DEFAULT 0 CHECK (long_yesterday_quantity >= 0),
    short_today_quantity         INTEGER NOT NULL DEFAULT 0 CHECK (short_today_quantity >= 0),
    short_yesterday_quantity     INTEGER NOT NULL DEFAULT 0 CHECK (short_yesterday_quantity >= 0),
    long_avg_price               NUMERIC NOT NULL DEFAULT 0 CHECK (long_avg_price >= 0),
    short_avg_price              NUMERIC NOT NULL DEFAULT 0 CHECK (short_avg_price >= 0),
    snapshot_timestamp           TEXT NOT NULL,
    source                       TEXT NOT NULL DEFAULT 'qry_investor_position' CHECK (source IN ('qry_investor_position', 'qry_investor_position_detail', 'manual_import'))
);

CREATE INDEX idx_broker_position_snapshots_account_instrument_time
    ON broker_position_snapshots(account_id, instrument, snapshot_timestamp);

CREATE TABLE reconciliation_runs (
    reconciliation_run_id        INTEGER PRIMARY KEY,
    account_id                   TEXT NOT NULL,
    trading_day                  TEXT,
    trigger_type                 TEXT NOT NULL CHECK (trigger_type IN ('startup', 'eod', 'manual', 'scheduled')),
    broker_snapshot_timestamp    TEXT NOT NULL,
    db_snapshot_timestamp        TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    aggregate_match              INTEGER NOT NULL CHECK (aggregate_match IN (0, 1)),
    mismatch_summary             TEXT,
    operator_id                  TEXT,
    created_at                   TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE VIEW strategy_inventory_account_aggregate AS
SELECT
    account_id,
    instrument,
    exchange,
    SUM(long_today_quantity)       AS long_today_quantity,
    SUM(long_yesterday_quantity)   AS long_yesterday_quantity,
    SUM(short_today_quantity)      AS short_today_quantity,
    SUM(short_yesterday_quantity)  AS short_yesterday_quantity,
    SUM(realized_pnl)              AS realized_pnl
FROM strategy_inventory_state
GROUP BY account_id, instrument, exchange;

-- Reconciliation rule (performed in application logic):
-- For every (account_id, instrument, exchange), the aggregate of strategy rows,
-- including the optional 'external_manual' pseudo-strategy row, must equal the
-- broker_position_snapshots buckets for the latest reconciliation run.
