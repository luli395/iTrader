#pragma once

#include <algorithm>
#include <cctype>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace itrader {

enum class Mode {
    Backtest,
    Live,
};

enum class Side {
    Buy,
    Sell,
};

enum class Offset {
    Open,
    Close,
    CloseYesterday,
    CloseToday,
};

enum class PriceType {
    Market,
    Limit,
};

enum class IntentExecutionPolicy {
    NativeOrder,
    RuntimeSyntheticFill,
};

enum class OrderStatus {
    Submitted,
    Accepted,
    PartiallyFilled,
    Filled,
    Cancelled,
    Rejected,
};

struct MarketTick {
    std::string timestamp;
    std::string trading_day;
    std::string instrument;
    std::string exchange;
    long long timestamp_ms {0};
    double last {0.0};
    double bid {0.0};
    double ask {0.0};
    int volume {0};
    double turnover {0.0};
    int bid_size {0};
    int ask_size {0};
    double upper_limit_price {0.0};
    double lower_limit_price {0.0};
};

struct OrderRequest {
    std::string account_id;
    std::string strategy_id;
    std::string client_order_id;
    int strategy_order_ref_code {0};
    std::string instrument;
    std::string exchange;
    Side side {Side::Buy};
    Offset offset {Offset::Open};
    PriceType price_type {PriceType::Market};
    bool immediate_or_cancel {false};
    double limit_price {0.0};
    int volume {0};
    long long activate_at_ms {0};
    long long signal_time_ms {0};
    bool backtest_force_fill {false};
    double backtest_fill_price {0.0};
};

struct OrderIntent {
    std::string client_order_id;
    std::string instrument;
    std::string exchange;
    Side side {Side::Buy};
    Offset offset {Offset::Open};
    PriceType price_type {PriceType::Limit};
    bool immediate_or_cancel {false};
    double limit_price {0.0};
    int volume {0};
    long long activate_at_ms {0};
    long long signal_time_ms {0};
    IntentExecutionPolicy execution_policy {IntentExecutionPolicy::NativeOrder};
    double expected_fill_price {0.0};
    std::string tag;
};

struct OrderEvent {
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

struct AccountSnapshot {
    std::string account_id;
    double initial_cash {0.0};
    double cash {0.0};
    double realized_pnl {0.0};
    std::map<std::string, int> net_positions;
};

inline std::string trim_copy(std::string_view value) {
    std::size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
        ++begin;
    }

    std::size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }

    return std::string(value.substr(begin, end - begin));
}

inline std::vector<std::string> split_csv(std::string_view raw) {
    std::vector<std::string> parts;
    std::string current;

    for (char ch : raw) {
        if (ch == ',') {
            parts.push_back(trim_copy(current));
            current.clear();
            continue;
        }
        current.push_back(ch);
    }

    parts.push_back(trim_copy(current));
    parts.erase(
        std::remove_if(parts.begin(), parts.end(), [](const std::string& item) { return item.empty(); }),
        parts.end());
    return parts;
}

inline std::string to_string(Mode mode) {
    return mode == Mode::Backtest ? "backtest" : "live";
}

inline std::string to_string(Side side) {
    return side == Side::Buy ? "buy" : "sell";
}

inline std::string to_string(Offset offset) {
    switch (offset) {
    case Offset::Open:
        return "open";
    case Offset::Close:
        return "close";
    case Offset::CloseYesterday:
        return "close_yesterday";
    case Offset::CloseToday:
        return "close_today";
    }
    return "open";
}

inline std::string to_string(PriceType type) {
    return type == PriceType::Market ? "market" : "limit";
}

inline std::string to_string(IntentExecutionPolicy policy) {
    return policy == IntentExecutionPolicy::RuntimeSyntheticFill ? "runtime_synthetic_fill" : "native_order";
}

inline std::string to_string(OrderStatus status) {
    switch (status) {
    case OrderStatus::Submitted:
        return "submitted";
    case OrderStatus::Accepted:
        return "accepted";
    case OrderStatus::PartiallyFilled:
        return "partially_filled";
    case OrderStatus::Filled:
        return "filled";
    case OrderStatus::Cancelled:
        return "cancelled";
    case OrderStatus::Rejected:
        return "rejected";
    }
    return "unknown";
}

} // namespace itrader
