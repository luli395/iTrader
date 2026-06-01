#pragma once

#include "itrader/domain.hpp"
#include "itrader/ini.hpp"

#include <atomic>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace itrader {

struct RuntimePositionSnapshot {
    std::string instrument;
    std::string account_id;
    std::string strategy_id;
    int long_today_quantity {0};
    int long_yesterday_quantity {0};
    int long_quantity {0};
    double long_average_price {0.0};
    int short_today_quantity {0};
    int short_yesterday_quantity {0};
    int short_quantity {0};
    double short_average_price {0.0};
    int net {0};
    double average_price {0.0};
};

struct RuntimeOrderSnapshot {
    std::string order_id;
    std::string source_order_id;
    std::string client_order_id;
    std::string account_id;
    std::string strategy_id;
    std::string instrument;
    std::string exchange;
    Side side {Side::Buy};
    Offset offset {Offset::Open};
    int requested_volume {0};
    int filled_volume {0};
    double limit_price {0.0};
    double filled_price {0.0};
    long long signal_time_ms {0};
    OrderStatus status {OrderStatus::Submitted};
    std::string message;
    std::string timestamp;
};

struct StrategyAttachmentSnapshot {
    std::string strategy_id;
    std::string account_id;
    std::vector<RuntimePositionSnapshot> positions;
    std::vector<RuntimeOrderSnapshot> opened_orders;
    std::vector<RuntimeOrderSnapshot> closed_orders;
    std::size_t opened_order_count {0};
    std::size_t closed_order_count {0};
    std::size_t filled_trade_count {0};
    std::vector<std::string> warnings;
};

struct RuntimeChartBarSnapshot {
    long long time {0};
    double open {0.0};
    double high {0.0};
    double low {0.0};
    double close {0.0};
};

struct RuntimeChartIndicatorPointSnapshot {
    long long time {0};
    double value {0.0};
};

struct RuntimeChartIndicatorSeriesSnapshot {
    std::string instrument;
    std::string indicator_id;
    std::string label;
    std::string color;
    std::string strategy_id;
    std::string account_id;
    std::vector<RuntimeChartIndicatorPointSnapshot> points;
};

struct RuntimeChartInstrumentSnapshot {
    std::string instrument;
    std::vector<RuntimeChartBarSnapshot> bars;
    std::vector<RuntimeChartIndicatorSeriesSnapshot> indicator_series;
};

struct RuntimeSnapshotProgress {
    std::string phase;
    std::size_t processed_files {0};
    std::size_t total_files {0};
    std::size_t processed_ticks {0};
};

struct RuntimeSnapshotBuildOptions {
    const std::atomic_bool* cancel_requested {nullptr};
    std::function<void(const RuntimeSnapshotProgress&)> on_progress;
    bool include_chart {true};
    bool include_order_history {true};
    int chart_bar_seconds {1};
};

struct RuntimeSnapshot {
    Mode mode {Mode::Backtest};
    std::vector<AccountSnapshot> accounts;
    std::vector<StrategyAttachmentSnapshot> strategy_attachments;
    std::vector<RuntimeChartInstrumentSnapshot> chart_instruments;
    int chart_bar_seconds {1};
    std::vector<std::string> warnings;
};

RuntimeSnapshot build_runtime_snapshot(
    const std::filesystem::path& config_path,
    const IniFile& ini,
    Mode mode,
    const RuntimeSnapshotBuildOptions& options = {});

} // namespace itrader
