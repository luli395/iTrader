#include "itrader/platform.hpp"

#include "itrader/domain.hpp"
#include "itrader/ini.hpp"
#include "itrader/strategy_api.hpp"
#include "itrader/runtime_snapshot.hpp"
#include "itrader/runtime_paths.hpp"

#ifdef ITRADER_ENABLE_CTP
#include "itrader/ctp_adapter.hpp"
#endif

#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <initializer_list>
#include <map>
#include <memory>
#include <limits>
#include <optional>
#include <queue>
#include <set>
#include <sstream>
#include <stdexcept>
#include <streambuf>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace itrader {
namespace {

std::atomic_bool g_stop_requested {false};

void on_interrupt(int) {
    g_stop_requested = true;
}

struct SignalInstaller {
    SignalInstaller() {
        std::signal(SIGINT, on_interrupt);
    }
} g_signal_installer;

std::string lower_copy(std::string value) {
    for (char& ch : value) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return value;
}

std::string upper_copy(std::string_view raw) {
    std::string value(raw);
    for (char& ch : value) {
        ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    }
    return value;
}

std::string normalize_exchange_code(std::string_view raw) {
    const auto value = upper_copy(trim_copy(raw));
    if (value == "XSGE") {
        return "SHFE";
    }
    if (value == "XDCE") {
        return "DCE";
    }
    if (value == "XZCE") {
        return "CZCE";
    }
    if (value == "XCFFEX") {
        return "CFFEX";
    }
    if (value == "XINE") {
        return "INE";
    }
    if (value == "XGFEX") {
        return "GFEX";
    }
    return value;
}

std::pair<std::string, std::string> split_symbol_and_exchange(std::string_view raw_symbol) {
    const auto trimmed = trim_copy(raw_symbol);
    const auto delimiter = trimmed.find('.');
    if (delimiter == std::string::npos) {
        return {upper_copy(trimmed), {}};
    }
    return {
        upper_copy(trimmed.substr(0, delimiter)),
        normalize_exchange_code(trimmed.substr(delimiter + 1))
    };
}

std::string canonical_instrument_for_filter(std::string_view raw_instrument, const std::set<std::string>* instrument_filter) {
    const auto normalized = upper_copy(trim_copy(raw_instrument));
    if (instrument_filter == nullptr || instrument_filter->empty()) {
        return normalized;
    }

    for (const auto& candidate : *instrument_filter) {
        if (upper_copy(candidate) == normalized) {
            return candidate;
        }
    }

    return {};
}

std::string instrument_alpha_prefix(std::string_view instrument) {
    std::string prefix;
    for (const char ch : instrument) {
        if (!std::isalpha(static_cast<unsigned char>(ch))) {
            break;
        }
        prefix.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
    }
    return prefix;
}

bool prefix_matches_any(std::string_view prefix, std::initializer_list<std::string_view> candidates) {
    return std::find(candidates.begin(), candidates.end(), prefix) != candidates.end();
}

Mode parse_mode(std::string value) {
    value = lower_copy(trim_copy(value));
    if (value == "live") {
        return Mode::Live;
    }
    return Mode::Backtest;
}

std::filesystem::path resolve_path(const std::filesystem::path& base_dir, const std::string& value) {
    std::filesystem::path candidate(value);
    if (candidate.is_relative()) {
        candidate = base_dir / candidate;
    }
    return std::filesystem::weakly_canonical(candidate);
}

std::string last_windows_error() {
    const DWORD code = GetLastError();
    if (code == 0) {
        return "Unknown Windows error";
    }

    LPSTR buffer = nullptr;
    const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
    const DWORD size = FormatMessageA(flags, nullptr, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), reinterpret_cast<LPSTR>(&buffer), 0, nullptr);
    std::string message = size == 0 ? "Windows error code " + std::to_string(code) : trim_copy(buffer);
    if (buffer != nullptr) {
        LocalFree(buffer);
    }
    return message;
}

std::filesystem::path current_process_path() {
    std::array<wchar_t, 4096> buffer {};
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length == buffer.size()) {
        throw std::runtime_error("Unable to determine current process path (" + last_windows_error() + ")");
    }
    return std::filesystem::path(std::wstring(buffer.data(), buffer.data() + static_cast<std::size_t>(length)));
}

std::optional<std::string> detect_build_flavor_from_path(const std::filesystem::path& path) {
    const auto normalized = lower_copy(path.generic_string());
    if (normalized.find("/debug/") != std::string::npos) {
        return std::string {"Debug"};
    }
    if (normalized.find("/release/") != std::string::npos) {
        return std::string {"Release"};
    }
    if (normalized.find("/relwithdebinfo/") != std::string::npos) {
        return std::string {"RelWithDebInfo"};
    }
    if (normalized.find("/minsizerel/") != std::string::npos) {
        return std::string {"MinSizeRel"};
    }
    return std::nullopt;
}

std::filesystem::path normalize_existing_path(const std::filesystem::path& path) {
    std::error_code error_code;
    const auto normalized = std::filesystem::weakly_canonical(path, error_code);
    if (!error_code) {
        return normalized;
    }
    return path.lexically_normal();
}

bool is_shared_strategy_bin_path(const std::filesystem::path& path) {
    const auto parent = lower_copy(path.parent_path().filename().generic_string());
    const auto grandparent = lower_copy(path.parent_path().parent_path().filename().generic_string());
    return parent == "bin" && grandparent == "strategies";
}

std::filesystem::path resolve_strategy_dll_for_host_build(
    const std::filesystem::path& requested_dll_path,
    const std::filesystem::path& host_path) {

    const auto normalized_requested = normalize_existing_path(std::filesystem::absolute(requested_dll_path));
    const auto host_flavor = detect_build_flavor_from_path(host_path);
    if (!host_flavor.has_value() || !is_shared_strategy_bin_path(normalized_requested)) {
        return normalized_requested;
    }

    const auto workspace_root = normalized_requested.parent_path().parent_path().parent_path();
    const auto candidate = (workspace_root / "build" / *host_flavor / normalized_requested.filename()).lexically_normal();
    std::error_code error_code;
    if (std::filesystem::exists(candidate, error_code) && std::filesystem::is_regular_file(candidate, error_code)) {
        return normalize_existing_path(candidate);
    }

    throw std::runtime_error(
        "Strategy DLL path " + normalized_requested.generic_string()
        + " points at the shared strategies/bin output, which is ambiguous for the running host build flavor "
        + *host_flavor
        + ". Expected a build-specific DLL such as " + candidate.generic_string()
        + ".");
}

void validate_strategy_binary_compatibility(const std::filesystem::path& host_path, const std::filesystem::path& dll_path) {
    const auto host_flavor = detect_build_flavor_from_path(host_path);
    const auto dll_flavor = detect_build_flavor_from_path(dll_path);
    if (!host_flavor.has_value() || !dll_flavor.has_value() || *host_flavor == *dll_flavor) {
        return;
    }

    throw std::runtime_error(
        "Strategy DLL build flavor mismatch: host executable " + host_path.generic_string()
        + " is running as " + *host_flavor
        + " but strategy DLL " + dll_path.generic_string()
        + " is built under " + *dll_flavor
        + ". Use a DLL from the same build configuration as the running host process.");
}

struct PositionState {
    int long_today_quantity {0};
    double long_today_average_price {0.0};
    int long_yesterday_quantity {0};
    double long_yesterday_average_price {0.0};
    int short_today_quantity {0};
    double short_today_average_price {0.0};
    int short_yesterday_quantity {0};
    double short_yesterday_average_price {0.0};
};

struct PendingOrder {
    OrderRequest request;
    std::string order_id;
    int remaining_volume {0};
    int filled_volume {0};
    int queue_ahead {0};
    bool activated {false};
    bool synthetic_runtime_match {false};
};

struct BacktestCostModel {
    double contract_multiplier {1.0};
    double commission_per_lot {0.0};
    double tick_size {1.0};
    double queue_ahead_ratio {1.0};
    int matching_mode {2};
    int price_scale {100};
};

struct SimAttachmentState {
    std::map<std::string, PositionState> positions;
    std::vector<PendingOrder> scheduled_orders;
    std::vector<PendingOrder> pending_orders;
    std::map<std::string, RuntimeOrderSnapshot> opened_orders_by_id;
    std::vector<RuntimeOrderSnapshot> closed_orders;
    std::size_t closed_order_count {0};
    std::size_t filled_trade_count {0};
    std::size_t filled_trade_volume {0};
    std::size_t filled_open_volume {0};
    double realized_pnl {0.0};
    BacktestCostModel cost_model;
};

struct SimAccountState {
    AccountSnapshot snapshot;
    std::map<std::string, SimAttachmentState> attachments;
    int next_order_id {1};
};

struct LiveAccountState {
#ifdef ITRADER_ENABLE_CTP
    CtpAccountConfig config;
    std::unique_ptr<CtpTraderGateway> gateway;
#ifdef ITRADER_ENABLE_CTP_MD
    std::unique_ptr<CtpMarketDataGateway> market_data_gateway;
#endif
#endif
    AccountSnapshot snapshot;
    std::map<std::string, PositionState> positions;
    std::unordered_map<std::string, int> applied_filled_volume_by_order;
    std::unordered_map<std::string, MarketTick> latest_ticks;
    bool trader_connected {false};
    bool market_data_connected {false};
    bool trading_enabled {true};
    std::string trading_disable_reason;
};

struct PreviousBacktestTick {
    long long timestamp_ms {0};
    double last {0.0};
    double bid {0.0};
    double ask {0.0};
    int volume {0};
    double turnover {0.0};
    int bid_size {0};
    int ask_size {0};
};

template <typename TickLike>
double best_bid(const TickLike& tick) {
    return tick.bid > 0.0 ? tick.bid : tick.last;
}

template <typename TickLike>
double best_ask(const TickLike& tick) {
    return tick.ask > 0.0 ? tick.ask : tick.last;
}

constexpr double kBacktestPriceScale = 10000.0;
constexpr double kBacktestPriceEpsilon = 1e-9;

long long price_key(double price);

struct InferredTradeFlow {
    int traded_volume {0};
    double vwap {0.0};
    std::map<long long, int> traded_volume_by_price;
};

void allocate_traded_volume_at_price(InferredTradeFlow& flow, double price, int& remaining_volume, int requested_volume) {
    if (price <= 0.0 || remaining_volume <= 0 || requested_volume <= 0) {
        return;
    }

    const int allocated_volume = std::min(remaining_volume, requested_volume);
    flow.traded_volume_by_price[price_key(price)] += allocated_volume;
    remaining_volume -= allocated_volume;
}

long long price_key(double price) {
    return static_cast<long long>(std::llround(price * kBacktestPriceScale));
}

bool price_greater_or_equal(double left, double right) {
    return left > right || std::fabs(left - right) <= kBacktestPriceEpsilon;
}

bool price_less_or_equal(double left, double right) {
    return left < right || std::fabs(left - right) <= kBacktestPriceEpsilon;
}

template <typename TickLike>
double last_trade_reference(const TickLike& tick) {
    if (tick.last > 0.0) {
        return tick.last;
    }

    const double bid = best_bid(tick);
    const double ask = best_ask(tick);
    if (bid > 0.0 && ask > 0.0) {
        return (bid + ask) / 2.0;
    }

    return ask > 0.0 ? ask : bid;
}

InferredTradeFlow infer_trade_flow(const std::optional<PreviousBacktestTick>& previous_tick, const MarketTick& tick) {
    InferredTradeFlow flow;
    flow.vwap = last_trade_reference(tick);

    if (!previous_tick.has_value()) {
        return flow;
    }

    const int delta_volume = std::max(0, tick.volume - previous_tick->volume);
    flow.traded_volume = delta_volume;
    if (delta_volume <= 0) {
        return flow;
    }

    if (tick.turnover > 0.0 && previous_tick->turnover > 0.0 && tick.turnover >= previous_tick->turnover) {
        const double delta_turnover = tick.turnover - previous_tick->turnover;
        if (delta_turnover > 0.0) {
            flow.vwap = delta_turnover / static_cast<double>(delta_volume);
        }
    }

    int remaining_volume = delta_volume;
    if (previous_tick->ask_size > 0 && best_ask(*previous_tick) > 0.0) {
        if (best_ask(tick) > best_ask(*previous_tick)) {
            allocate_traded_volume_at_price(flow, best_ask(*previous_tick), remaining_volume, previous_tick->ask_size);
        } else if (std::fabs(best_ask(tick) - best_ask(*previous_tick)) <= kBacktestPriceEpsilon && tick.ask_size < previous_tick->ask_size) {
            allocate_traded_volume_at_price(flow, best_ask(tick), remaining_volume, previous_tick->ask_size - tick.ask_size);
        }
    }

    if (previous_tick->bid_size > 0 && best_bid(*previous_tick) > 0.0) {
        if (best_bid(tick) < best_bid(*previous_tick)) {
            allocate_traded_volume_at_price(flow, best_bid(*previous_tick), remaining_volume, previous_tick->bid_size);
        } else if (std::fabs(best_bid(tick) - best_bid(*previous_tick)) <= kBacktestPriceEpsilon && tick.bid_size < previous_tick->bid_size) {
            allocate_traded_volume_at_price(flow, best_bid(tick), remaining_volume, previous_tick->bid_size - tick.bid_size);
        }
    }

    const double representative_price = flow.vwap > 0.0 ? flow.vwap : last_trade_reference(tick);
    allocate_traded_volume_at_price(flow, representative_price, remaining_volume, remaining_volume);
    return flow;
}

int traded_volume_at_price(const InferredTradeFlow& flow, double price) {
    const auto it = flow.traded_volume_by_price.find(price_key(price));
    return it == flow.traded_volume_by_price.end() ? 0 : it->second;
}

bool can_fill_immediately(
    const OrderRequest& request,
    const MarketTick& tick,
    const InferredTradeFlow& flow) {

    if (request.instrument != tick.instrument) {
        return false;
    }

    if (request.price_type == PriceType::Market) {
        return true;
    }

    if (request.side == Side::Buy) {
        if (price_greater_or_equal(request.limit_price, best_ask(tick))) {
            return true;
        }
        return flow.traded_volume > 0
            && ((request.limit_price > last_trade_reference(tick) || request.limit_price > flow.vwap)
                || (request.limit_price > best_bid(tick) && traded_volume_at_price(flow, best_bid(tick)) > 0));
    }

    if (price_less_or_equal(request.limit_price, best_bid(tick))) {
        return true;
    }
    return flow.traded_volume > 0
        && ((request.limit_price < last_trade_reference(tick) || request.limit_price < flow.vwap)
            || (request.limit_price < best_ask(tick) && traded_volume_at_price(flow, best_ask(tick)) > 0));
}

double fill_price(
    const OrderRequest& request,
    const MarketTick& tick,
    const std::optional<PreviousBacktestTick>& previous_tick) {

    if (request.price_type == PriceType::Market) {
        return request.side == Side::Buy ? best_ask(tick) : best_bid(tick);
    }

    if (request.side == Side::Buy) {
        if (request.limit_price > best_ask(tick)) {
            if (previous_tick.has_value() && request.limit_price > best_ask(*previous_tick)) {
                return std::max(best_ask(tick), best_ask(*previous_tick));
            }
            return request.limit_price;
        }
        return request.limit_price;
    }

    if (request.limit_price < best_bid(tick)) {
        if (previous_tick.has_value() && request.limit_price < best_bid(*previous_tick)) {
            return std::min(best_bid(tick), best_bid(*previous_tick));
        }
        return request.limit_price;
    }
    return request.limit_price;
}

int estimate_queue_ahead(const PendingOrder& pending, const MarketTick& tick) {
    if (pending.request.price_type != PriceType::Limit) {
        return 0;
    }

    if (pending.request.side == Side::Buy
        && tick.bid_size > 0
        && std::fabs(pending.request.limit_price - best_bid(tick)) <= kBacktestPriceEpsilon) {
        return tick.bid_size;
    }

    if (pending.request.side == Side::Sell
        && tick.ask_size > 0
        && std::fabs(pending.request.limit_price - best_ask(tick)) <= kBacktestPriceEpsilon) {
        return tick.ask_size;
    }

    return 0;
}

int visible_queue_at_order_price(const PendingOrder& pending, const MarketTick& tick) {
    if (pending.request.price_type != PriceType::Limit) {
        return 0;
    }

    if (pending.request.side == Side::Buy
        && tick.bid_size > 0
        && std::fabs(pending.request.limit_price - best_bid(tick)) <= kBacktestPriceEpsilon) {
        return tick.bid_size;
    }

    if (pending.request.side == Side::Sell
        && tick.ask_size > 0
        && std::fabs(pending.request.limit_price - best_ask(tick)) <= kBacktestPriceEpsilon) {
        return tick.ask_size;
    }

    return 0;
}

void refresh_queue_ahead(PendingOrder& pending, const MarketTick& tick) {
    const int visible_queue = visible_queue_at_order_price(pending, tick);
    if (visible_queue > 0) {
        pending.queue_ahead = std::min(pending.queue_ahead, visible_queue);
        return;
    }

    if (pending.request.side == Side::Buy
        && pending.request.limit_price > best_bid(tick)
        && pending.request.limit_price < best_ask(tick)) {
        pending.queue_ahead = 0;
        return;
    }

    if (pending.request.side == Side::Sell
        && pending.request.limit_price < best_ask(tick)
        && pending.request.limit_price > best_bid(tick)) {
        pending.queue_ahead = 0;
    }
}

struct SyntheticTradeFlowSnapshot {
    std::unordered_map<long long, int> traded_volume_by_price;
    double vwap {0.0};
    int delta_volume {0};
};

long long synthetic_price_key(double price, int price_scale) {
    return static_cast<long long>(std::llround(price * static_cast<double>(std::max(price_scale, 1))));
}

SyntheticTradeFlowSnapshot infer_synthetic_trade_flow(
    const std::optional<PreviousBacktestTick>& previous_tick,
    const MarketTick& tick,
    const BacktestCostModel& model) {

    SyntheticTradeFlowSnapshot flow;
    flow.vwap = tick.last;

    if (!previous_tick.has_value()) {
        return flow;
    }

    flow.delta_volume = std::max(0, tick.volume - previous_tick->volume);
    if (flow.delta_volume <= 0) {
        flow.delta_volume = 0;
        flow.vwap = tick.last;
        return flow;
    }

    const double delta_turnover = tick.turnover - previous_tick->turnover;
    if (delta_turnover > 0.0 && model.contract_multiplier > 0.0) {
        flow.vwap = delta_turnover / static_cast<double>(flow.delta_volume) / model.contract_multiplier;
    } else {
        flow.vwap = tick.last;
    }

    auto add_volume = [&flow, &model](double price, int volume) {
        if (price <= 0.0 || volume <= 0) {
            return;
        }
        flow.traded_volume_by_price[synthetic_price_key(price, model.price_scale)] += volume;
    };

    auto populate_mode0_distribution = [&](int volume, double last_price) {
        const double tick_size = std::max(model.tick_size, 1e-9);
        const double rounded = std::round(flow.vwap / tick_size) * tick_size;
        double lower = rounded;
        double upper = rounded;
        if (rounded < flow.vwap) {
            upper = rounded + tick_size;
        } else if (rounded > flow.vwap) {
            lower = rounded - tick_size;
        }

        if (std::fabs(upper - lower) < kBacktestPriceEpsilon) {
            add_volume(rounded, volume);
            return;
        }

        int lower_quantity = static_cast<int>(std::llround(((upper - flow.vwap) / (upper - lower)) * volume));
        lower_quantity = std::clamp(lower_quantity, 0, volume);
        const int upper_quantity = volume - lower_quantity;
        add_volume(lower, lower_quantity);
        add_volume(upper, upper_quantity);

        const auto last_key = synthetic_price_key(last_price, model.price_scale);
        if (!flow.traded_volume_by_price.contains(last_key)) {
            const auto lower_key = synthetic_price_key(lower, model.price_scale);
            const auto upper_key = synthetic_price_key(upper, model.price_scale);
            if (std::fabs(last_price - lower) <= std::fabs(last_price - upper)
                && flow.traded_volume_by_price[lower_key] > 0) {
                flow.traded_volume_by_price[lower_key] -= 1;
                flow.traded_volume_by_price[last_key] += 1;
            } else if (flow.traded_volume_by_price[upper_key] > 0) {
                flow.traded_volume_by_price[upper_key] -= 1;
                flow.traded_volume_by_price[last_key] += 1;
            }
        }
    };

    if (flow.delta_volume == 1) {
        add_volume(tick.last, 1);
        return flow;
    }

    if (model.matching_mode == 0) {
        populate_mode0_distribution(flow.delta_volume, tick.last);
        return flow;
    }

    const double tick_size = std::max(model.tick_size, 1e-9);
    const long long step = std::max(1LL, static_cast<long long>(std::llround(tick_size * static_cast<double>(model.price_scale))));
    const double rounded = std::round(flow.vwap / tick_size) * tick_size;
    const double lower = rounded > flow.vwap ? (rounded - tick_size) : rounded;
    const double upper = rounded < flow.vwap ? (rounded + tick_size) : rounded;

    long long minimum_price = std::min({synthetic_price_key(lower, model.price_scale), synthetic_price_key(previous_tick->bid, model.price_scale), synthetic_price_key(tick.bid, model.price_scale)});
    long long maximum_price = std::max({synthetic_price_key(upper, model.price_scale), synthetic_price_key(previous_tick->ask, model.price_scale), synthetic_price_key(tick.ask, model.price_scale)});
    if (minimum_price > maximum_price) {
        std::swap(minimum_price, maximum_price);
    }

    std::vector<long long> prices;
    for (long long price = minimum_price; price <= maximum_price; price += step) {
        prices.push_back(price);
    }
    if (prices.empty()) {
        add_volume(tick.last, flow.delta_volume);
        return flow;
    }

    const long long average_price = static_cast<long long>(std::llround(flow.vwap * static_cast<double>(model.price_scale)));
    int center = 0;
    long long best_distance = std::llabs(prices[0] - average_price);
    for (int index = 1; index < static_cast<int>(prices.size()); ++index) {
        const auto distance = std::llabs(prices[static_cast<std::size_t>(index)] - average_price);
        if (distance < best_distance) {
            best_distance = distance;
            center = index;
        }
    }

    std::vector<int> quantity_by_price(prices.size(), 0);
    quantity_by_price[static_cast<std::size_t>(center)] = flow.delta_volume;

    const long long total_amount = model.contract_multiplier > 0.0
        ? static_cast<long long>(std::llround(delta_turnover / model.contract_multiplier * static_cast<double>(model.price_scale)))
        : prices[static_cast<std::size_t>(center)] * static_cast<long long>(flow.delta_volume);
    const long long current_amount = prices[static_cast<std::size_t>(center)] * static_cast<long long>(flow.delta_volume);
    long long difference = total_amount - current_amount;

    if (difference > 0) {
        long long needed_steps = static_cast<long long>(std::llround(static_cast<double>(difference) / static_cast<double>(step)));
        int movable = quantity_by_price[static_cast<std::size_t>(center)];
        for (int distance = static_cast<int>(prices.size()) - 1 - center; distance >= 1 && needed_steps > 0 && movable > 0; --distance) {
            const int move_quantity = static_cast<int>(std::min<long long>(movable, needed_steps / distance));
            if (move_quantity <= 0) {
                continue;
            }
            quantity_by_price[static_cast<std::size_t>(center)] -= move_quantity;
            quantity_by_price[static_cast<std::size_t>(center + distance)] += move_quantity;
            movable -= move_quantity;
            needed_steps -= static_cast<long long>(move_quantity) * distance;
        }
        if (needed_steps != 0) {
            flow.traded_volume_by_price.clear();
            populate_mode0_distribution(flow.delta_volume, tick.last);
            return flow;
        }
    } else if (difference < 0) {
        long long needed_steps = static_cast<long long>(std::llround(static_cast<double>(-difference) / static_cast<double>(step)));
        int movable = quantity_by_price[static_cast<std::size_t>(center)];
        for (int distance = center; distance >= 1 && needed_steps > 0 && movable > 0; --distance) {
            const int move_quantity = static_cast<int>(std::min<long long>(movable, needed_steps / distance));
            if (move_quantity <= 0) {
                continue;
            }
            quantity_by_price[static_cast<std::size_t>(center)] -= move_quantity;
            quantity_by_price[static_cast<std::size_t>(center - distance)] += move_quantity;
            movable -= move_quantity;
            needed_steps -= static_cast<long long>(move_quantity) * distance;
        }
        if (needed_steps != 0) {
            flow.traded_volume_by_price.clear();
            populate_mode0_distribution(flow.delta_volume, tick.last);
            return flow;
        }
    }

    for (std::size_t index = 0; index < prices.size(); ++index) {
        if (quantity_by_price[index] > 0) {
            flow.traded_volume_by_price[prices[index]] += quantity_by_price[index];
        }
    }

    return flow;
}

int synthetic_traded_volume_at_price(const SyntheticTradeFlowSnapshot& flow, double price, int price_scale) {
    const auto it = flow.traded_volume_by_price.find(synthetic_price_key(price, price_scale));
    return it == flow.traded_volume_by_price.end() ? 0 : it->second;
}

double synthetic_fill_price(const PendingOrder& pending, const MarketTick& tick, const std::optional<PreviousBacktestTick>& previous_tick) {
    if (pending.request.side == Side::Buy) {
        if (pending.request.limit_price > tick.ask && previous_tick.has_value() && pending.request.limit_price > previous_tick->ask) {
            return std::min(pending.request.limit_price, std::max(previous_tick->ask, tick.ask));
        }
        return pending.request.limit_price;
    }

    if (pending.request.limit_price < tick.bid && previous_tick.has_value() && pending.request.limit_price < previous_tick->bid) {
        return std::max(pending.request.limit_price, std::min(previous_tick->bid, tick.bid));
    }
    return pending.request.limit_price;
}

int synthetic_visible_queue_at_order_price(const PendingOrder& pending, const MarketTick& tick) {
    if (pending.request.side == Side::Buy && std::fabs(pending.request.limit_price - tick.bid) <= kBacktestPriceEpsilon) {
        return tick.bid_size;
    }
    if (pending.request.side == Side::Sell && std::fabs(pending.request.limit_price - tick.ask) <= kBacktestPriceEpsilon) {
        return tick.ask_size;
    }
    return 0;
}

bool synthetic_order_crossed(
    const PendingOrder& pending,
    const MarketTick& tick,
    const SyntheticTradeFlowSnapshot& flow,
    const BacktestCostModel& model) {

    if (pending.request.side == Side::Buy) {
        const bool traded_through = flow.delta_volume > 0 && (pending.request.limit_price > tick.last || pending.request.limit_price > flow.vwap);
        const bool crosses_ask = pending.request.limit_price >= tick.ask;
        const bool inside_fill = pending.request.limit_price > tick.bid
            && synthetic_traded_volume_at_price(flow, tick.bid, model.price_scale) > 0;
        return traded_through || crosses_ask || inside_fill;
    }

    const bool traded_through = flow.delta_volume > 0 && (pending.request.limit_price < tick.last || pending.request.limit_price < flow.vwap);
    const bool crosses_bid = pending.request.limit_price <= tick.bid;
    const bool inside_fill = pending.request.limit_price < tick.ask
        && synthetic_traded_volume_at_price(flow, tick.ask, model.price_scale) > 0;
    return traded_through || crosses_bid || inside_fill;
}

int synthetic_resting_fill_volume(
    PendingOrder& pending,
    const MarketTick& tick,
    const std::optional<PreviousBacktestTick>& previous_tick,
    const BacktestCostModel& model,
    double& trade_price) {

    trade_price = 0.0;
    if (pending.request.instrument != tick.instrument || pending.remaining_volume <= 0) {
        return 0;
    }
    if (tick.timestamp_ms < pending.request.activate_at_ms) {
        return 0;
    }
    if (!previous_tick.has_value()) {
        return 0;
    }

    const auto flow = infer_synthetic_trade_flow(previous_tick, tick, model);
    if (synthetic_order_crossed(pending, tick, flow, model)) {
        trade_price = synthetic_fill_price(pending, tick, previous_tick);
        return pending.remaining_volume;
    }

    const int traded_at_limit = synthetic_traded_volume_at_price(flow, pending.request.limit_price, model.price_scale);
    if (pending.activated) {
        if (pending.queue_ahead < traded_at_limit) {
            const int available = traded_at_limit - pending.queue_ahead;
            if (available >= pending.remaining_volume) {
                trade_price = pending.request.limit_price;
                return pending.remaining_volume;
            }
        }
        const int current_volume = synthetic_visible_queue_at_order_price(pending, tick);
        pending.queue_ahead = std::min(current_volume, std::max(0, pending.queue_ahead - traded_at_limit));
        return 0;
    }

    const int current_volume = synthetic_visible_queue_at_order_price(pending, tick);
    const int queue_estimate = static_cast<int>(std::ceil(
        model.queue_ahead_ratio * std::max(0, current_volume + traded_at_limit - pending.queue_ahead)
        - traded_at_limit + pending.queue_ahead));
    pending.queue_ahead = std::min(current_volume, std::max(0, queue_estimate));
    pending.activated = true;
    return 0;
}

int resting_fill_volume(PendingOrder& pending, const MarketTick& tick, const std::optional<PreviousBacktestTick>& previous_tick) {
    if (pending.request.instrument != tick.instrument || pending.remaining_volume <= 0) {
        return 0;
    }
    if (pending.request.activate_at_ms > 0 && tick.timestamp_ms > 0 && tick.timestamp_ms < pending.request.activate_at_ms) {
        return 0;
    }

    const auto flow = infer_trade_flow(previous_tick, tick);
    if (can_fill_immediately(pending.request, tick, flow)) {
        return pending.remaining_volume;
    }

    if (!pending.activated) {
        pending.queue_ahead = estimate_queue_ahead(pending, tick);
        pending.activated = true;
        return 0;
    }

    refresh_queue_ahead(pending, tick);

    if (flow.traded_volume <= 0) {
        return 0;
    }

    const int traded_at_limit = traded_volume_at_price(flow, pending.request.limit_price);
    if (traded_at_limit <= 0) {
        return 0;
    }

    if (pending.queue_ahead >= traded_at_limit) {
        pending.queue_ahead = std::max(0, pending.queue_ahead - traded_at_limit);
        return 0;
    }

    const int executable_volume = traded_at_limit - pending.queue_ahead;
    pending.queue_ahead = 0;
    return std::min(pending.remaining_volume, executable_volume);
}

double weighted_average_price(int first_quantity, double first_price, int second_quantity, double second_price) {
    const int total_quantity = first_quantity + second_quantity;
    if (total_quantity <= 0) {
        return 0.0;
    }

    const double gross_cost = (static_cast<double>(first_quantity) * first_price)
        + (static_cast<double>(second_quantity) * second_price);
    return gross_cost / static_cast<double>(total_quantity);
}

int total_long_quantity(const PositionState& position) {
    return position.long_today_quantity + position.long_yesterday_quantity;
}

int total_short_quantity(const PositionState& position) {
    return position.short_today_quantity + position.short_yesterday_quantity;
}

double aggregate_long_average_price(const PositionState& position) {
    return weighted_average_price(
        position.long_today_quantity,
        position.long_today_average_price,
        position.long_yesterday_quantity,
        position.long_yesterday_average_price);
}

double aggregate_short_average_price(const PositionState& position) {
    return weighted_average_price(
        position.short_today_quantity,
        position.short_today_average_price,
        position.short_yesterday_quantity,
        position.short_yesterday_average_price);
}

bool exchange_uses_close_as_close_yesterday(std::string_view exchange) {
    const auto normalized = lower_copy(trim_copy(exchange));
    return normalized == "shfe" || normalized == "ine";
}

std::string shift_trading_day_label(std::string_view raw_date, bool dashed_format) {
    std::tm parsed_time {};
    std::istringstream input {std::string(raw_date)};
    input >> std::get_time(&parsed_time, dashed_format ? "%Y-%m-%d" : "%Y%m%d");
    if (input.fail()) {
        return std::string(raw_date);
    }

    parsed_time.tm_hour = 0;
    parsed_time.tm_min = 0;
    parsed_time.tm_sec = 0;
    const std::time_t local_time = std::mktime(&parsed_time);
    if (local_time == static_cast<std::time_t>(-1)) {
        return std::string(raw_date);
    }

    const auto shifted_time = local_time + 24 * 60 * 60;
    std::tm shifted_local_time {};
    localtime_s(&shifted_local_time, &shifted_time);

    std::ostringstream output;
    output << std::put_time(&shifted_local_time, dashed_format ? "%Y-%m-%d" : "%Y%m%d");
    return output.str();
}

std::string trading_day_label(std::string_view raw_timestamp) {
    const std::string trimmed = trim_copy(raw_timestamp);
    if (trimmed.size() >= 10 && trimmed[4] == '-' && trimmed[7] == '-') {
        const std::string date = trimmed.substr(0, 10);
        if (trimmed.size() >= 13
            && std::isdigit(static_cast<unsigned char>(trimmed[11])) != 0
            && std::isdigit(static_cast<unsigned char>(trimmed[12])) != 0) {
            const int hour = std::stoi(trimmed.substr(11, 2));
            if (hour >= 20) {
                return shift_trading_day_label(date, true);
            }
        }
        return date;
    }
    if (trimmed.size() >= 8
        && std::isdigit(static_cast<unsigned char>(trimmed[0])) != 0
        && std::isdigit(static_cast<unsigned char>(trimmed[7])) != 0) {
        const std::string date = trimmed.substr(0, 8);
        if (trimmed.size() >= 10
            && std::isdigit(static_cast<unsigned char>(trimmed[8])) != 0
            && std::isdigit(static_cast<unsigned char>(trimmed[9])) != 0) {
            const int hour = std::stoi(trimmed.substr(8, 2));
            if (hour >= 20) {
                return shift_trading_day_label(date, false);
            }
        }
        return date;
    }
    return {};
}

std::string trading_day_label_from_tick(const MarketTick& tick) {
    if (!tick.trading_day.empty()) {
        return tick.trading_day;
    }
    return trading_day_label(tick.timestamp);
}

std::string canonical_trading_day_label(std::string_view raw_label) {
    std::string digits;
    digits.reserve(raw_label.size());
    for (const char ch : raw_label) {
        if (std::isdigit(static_cast<unsigned char>(ch)) != 0) {
            digits.push_back(ch);
        }
    }

    if (digits.size() == 8) {
        return digits;
    }

    return trim_copy(raw_label);
}

std::string trading_day_label_from_file_path(const std::filesystem::path& file_path) {
    const std::string stem = file_path.stem().string();
    for (std::size_t index = 0; index + 8 <= stem.size(); ++index) {
        bool all_digits = true;
        for (std::size_t offset = 0; offset < 8; ++offset) {
            if (std::isdigit(static_cast<unsigned char>(stem[index + offset])) == 0) {
                all_digits = false;
                break;
            }
        }
        if (all_digits) {
            return stem.substr(index, 8);
        }
    }
    return {};
}

void roll_position_to_next_trading_day(PositionState& position) {
    position.long_yesterday_average_price = weighted_average_price(
        position.long_yesterday_quantity,
        position.long_yesterday_average_price,
        position.long_today_quantity,
        position.long_today_average_price);
    position.long_yesterday_quantity += position.long_today_quantity;
    position.long_today_quantity = 0;
    position.long_today_average_price = 0.0;

    position.short_yesterday_average_price = weighted_average_price(
        position.short_yesterday_quantity,
        position.short_yesterday_average_price,
        position.short_today_quantity,
        position.short_today_average_price);
    position.short_yesterday_quantity += position.short_today_quantity;
    position.short_today_quantity = 0;
    position.short_today_average_price = 0.0;
}

double close_long_bucket(int& quantity_bucket, double& average_price_bucket, int quantity, double trade_price) {
    const int closing_quantity = std::min(quantity_bucket, quantity);
    const double realized_pnl = average_price_bucket > 0.0
        ? static_cast<double>(closing_quantity) * (trade_price - average_price_bucket)
        : 0.0;
    quantity_bucket -= closing_quantity;
    if (quantity_bucket == 0) {
        average_price_bucket = 0.0;
    }
    return realized_pnl;
}

double close_short_bucket(int& quantity_bucket, double& average_price_bucket, int quantity, double trade_price) {
    const int closing_quantity = std::min(quantity_bucket, quantity);
    const double realized_pnl = average_price_bucket > 0.0
        ? static_cast<double>(closing_quantity) * (average_price_bucket - trade_price)
        : 0.0;
    quantity_bucket -= closing_quantity;
    if (quantity_bucket == 0) {
        average_price_bucket = 0.0;
    }
    return realized_pnl;
}

int closeable_quantity(const PositionState& position, const OrderRequest& request) {
    if (request.offset == Offset::Open) {
        return std::numeric_limits<int>::max();
    }

    const bool closes_short = request.side == Side::Buy;
    const int today_quantity = closes_short ? position.short_today_quantity : position.long_today_quantity;
    const int yesterday_quantity = closes_short ? position.short_yesterday_quantity : position.long_yesterday_quantity;

    switch (request.offset) {
    case Offset::Open:
        return std::numeric_limits<int>::max();
    case Offset::CloseToday:
        return today_quantity;
    case Offset::CloseYesterday:
        return yesterday_quantity;
    case Offset::Close:
        return exchange_uses_close_as_close_yesterday(request.exchange)
            ? yesterday_quantity
            : (today_quantity + yesterday_quantity);
    }

    return 0;
}

bool close_request_is_valid(const PositionState& position, const OrderRequest& request) {
    if (request.offset == Offset::Open) {
        return true;
    }
    if (request.volume <= 0) {
        return false;
    }
    return closeable_quantity(position, request) >= request.volume;
}

int net_quantity(const PositionState& position) {
    return total_long_quantity(position) - total_short_quantity(position);
}

double compatibility_average_price(const PositionState& position) {
    if (total_long_quantity(position) > 0 && total_short_quantity(position) == 0) {
        return aggregate_long_average_price(position);
    }
    if (total_short_quantity(position) > 0 && total_long_quantity(position) == 0) {
        return aggregate_short_average_price(position);
    }
    return 0.0;
}

BacktestCostModel read_backtest_cost_model(const std::unordered_map<std::string, std::string>& parameters) {
    BacktestCostModel model;

    auto read_double = [&parameters](std::string_view key) -> std::optional<double> {
        const auto it = parameters.find(std::string(key));
        if (it == parameters.end() || it->second.empty()) {
            return std::nullopt;
        }
        return std::stod(it->second);
    };

    if (const auto multiplier = read_double("multiplier"); multiplier.has_value()) {
        model.contract_multiplier = std::max(*multiplier, 1e-9);
    } else if (const auto contract_multiplier = read_double("contract_multiplier"); contract_multiplier.has_value()) {
        model.contract_multiplier = std::max(*contract_multiplier, 1e-9);
    }

    if (const auto commission = read_double("commission"); commission.has_value()) {
        model.commission_per_lot = std::max(*commission, 0.0);
    } else if (const auto commission_per_lot = read_double("commission_per_lot"); commission_per_lot.has_value()) {
        model.commission_per_lot = std::max(*commission_per_lot, 0.0);
    }

    if (const auto tick_size = read_double("tick_size"); tick_size.has_value()) {
        model.tick_size = std::max(*tick_size, 1e-9);
    }
    if (const auto queue_ratio = read_double("queue_ratio"); queue_ratio.has_value()) {
        model.queue_ahead_ratio = std::max(*queue_ratio, 0.0);
    }
    if (const auto matching_mode = parameters.find("matching_mode"); matching_mode != parameters.end() && !matching_mode->second.empty()) {
        model.matching_mode = std::max(std::stoi(matching_mode->second), 0);
    }
    if (const auto price_scale = parameters.find("price_scale"); price_scale != parameters.end() && !price_scale->second.empty()) {
        model.price_scale = std::max(std::stoi(price_scale->second), 1);
    }

    return model;
}

RuntimePositionSnapshot make_runtime_position_snapshot(
    const std::string& instrument,
    const std::string& account_id,
    const std::string& strategy_id,
    const PositionState& position_state) {

    RuntimePositionSnapshot position;
    position.instrument = instrument;
    position.account_id = account_id;
    position.strategy_id = strategy_id;
    position.long_today_quantity = position_state.long_today_quantity;
    position.long_yesterday_quantity = position_state.long_yesterday_quantity;
    position.long_quantity = total_long_quantity(position_state);
    position.long_average_price = aggregate_long_average_price(position_state);
    position.short_today_quantity = position_state.short_today_quantity;
    position.short_yesterday_quantity = position_state.short_yesterday_quantity;
    position.short_quantity = total_short_quantity(position_state);
    position.short_average_price = aggregate_short_average_price(position_state);
    position.net = net_quantity(position_state);
    position.average_price = compatibility_average_price(position_state);
    return position;
}

double apply_fill(
    PositionState& position,
    const OrderRequest& request,
    double trade_price,
    int quantity,
    const BacktestCostModel& cost_model = {}) {

    if (quantity <= 0) {
        return 0.0;
    }

    if (request.offset == Offset::Open) {
        if (request.side == Side::Buy) {
            const int new_quantity = position.long_today_quantity + quantity;
            const double gross_cost = (static_cast<double>(position.long_today_quantity) * position.long_today_average_price) + (static_cast<double>(quantity) * trade_price);
            position.long_today_quantity = new_quantity;
            position.long_today_average_price = new_quantity > 0 ? gross_cost / static_cast<double>(new_quantity) : 0.0;
        } else {
            const int new_quantity = position.short_today_quantity + quantity;
            const double gross_cost = (static_cast<double>(position.short_today_quantity) * position.short_today_average_price) + (static_cast<double>(quantity) * trade_price);
            position.short_today_quantity = new_quantity;
            position.short_today_average_price = new_quantity > 0 ? gross_cost / static_cast<double>(new_quantity) : 0.0;
        }
        return -cost_model.commission_per_lot * static_cast<double>(quantity);
    }

    if (request.side == Side::Buy) {
        if (request.offset == Offset::CloseToday) {
            return close_short_bucket(position.short_today_quantity, position.short_today_average_price, quantity, trade_price) * cost_model.contract_multiplier
                - (cost_model.commission_per_lot * static_cast<double>(quantity));
        }
        if (request.offset == Offset::CloseYesterday || exchange_uses_close_as_close_yesterday(request.exchange)) {
            return close_short_bucket(position.short_yesterday_quantity, position.short_yesterday_average_price, quantity, trade_price) * cost_model.contract_multiplier
                - (cost_model.commission_per_lot * static_cast<double>(quantity));
        }

        int remaining_quantity = quantity;
        double gross_realized = 0.0;
        const int close_yesterday_quantity = std::min(position.short_yesterday_quantity, remaining_quantity);
        gross_realized += close_short_bucket(position.short_yesterday_quantity, position.short_yesterday_average_price, close_yesterday_quantity, trade_price);
        remaining_quantity -= close_yesterday_quantity;
        if (remaining_quantity > 0) {
            gross_realized += close_short_bucket(position.short_today_quantity, position.short_today_average_price, remaining_quantity, trade_price);
        }
        return gross_realized * cost_model.contract_multiplier - (cost_model.commission_per_lot * static_cast<double>(quantity));
    }

    if (request.offset == Offset::CloseToday) {
        return close_long_bucket(position.long_today_quantity, position.long_today_average_price, quantity, trade_price) * cost_model.contract_multiplier
            - (cost_model.commission_per_lot * static_cast<double>(quantity));
    }
    if (request.offset == Offset::CloseYesterday || exchange_uses_close_as_close_yesterday(request.exchange)) {
        return close_long_bucket(position.long_yesterday_quantity, position.long_yesterday_average_price, quantity, trade_price) * cost_model.contract_multiplier
            - (cost_model.commission_per_lot * static_cast<double>(quantity));
    }

    int remaining_quantity = quantity;
    double gross_realized = 0.0;
    const int close_yesterday_quantity = std::min(position.long_yesterday_quantity, remaining_quantity);
    gross_realized += close_long_bucket(position.long_yesterday_quantity, position.long_yesterday_average_price, close_yesterday_quantity, trade_price);
    remaining_quantity -= close_yesterday_quantity;
    if (remaining_quantity > 0) {
        gross_realized += close_long_bucket(position.long_today_quantity, position.long_today_average_price, remaining_quantity, trade_price);
    }
    return gross_realized * cost_model.contract_multiplier - (cost_model.commission_per_lot * static_cast<double>(quantity));
}

std::string join_csv(const std::vector<std::string>& values) {
    std::ostringstream output;
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index > 0) {
            output << ',';
        }
        output << values[index];
    }
    return output.str();
}

std::string format_price(double value) {
    std::ostringstream output;
    output << std::fixed << std::setprecision(2) << value;
    return output.str();
}

std::string current_timestamp() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t raw_time = std::chrono::system_clock::to_time_t(now);
    std::tm local_time {};
    localtime_s(&local_time, &raw_time);

    std::ostringstream output;
    output << std::put_time(&local_time, "%F %T");
    return output.str();
}

std::string csv_escape(std::string_view raw) {
    const bool needs_quotes = raw.find_first_of(",\"\r\n") != std::string_view::npos;
    if (!needs_quotes) {
        return std::string(raw);
    }

    std::string escaped;
    escaped.reserve(raw.size() + 2);
    escaped.push_back('"');
    for (const char ch : raw) {
        if (ch == '"') {
            escaped += "\"\"";
            continue;
        }
        escaped.push_back(ch);
    }
    escaped.push_back('"');
    return escaped;
}

std::string svg_escape(std::string_view raw) {
    std::string escaped;
    escaped.reserve(raw.size());
    for (const char ch : raw) {
        switch (ch) {
        case '&':
            escaped += "&amp;";
            break;
        case '<':
            escaped += "&lt;";
            break;
        case '>':
            escaped += "&gt;";
            break;
        case '"':
            escaped += "&quot;";
            break;
        default:
            escaped.push_back(ch);
            break;
        }
    }
    return escaped;
}

std::string display_trading_day_label(std::string_view raw_label) {
    const auto canonical = canonical_trading_day_label(raw_label);
    if (canonical.size() == 8
        && std::all_of(canonical.begin(), canonical.end(), [](unsigned char ch) { return std::isdigit(ch) != 0; })) {
        return canonical.substr(0, 4) + "-" + canonical.substr(4, 2) + "-" + canonical.substr(6, 2);
    }
    return trim_copy(raw_label);
}

class TeeStreamBuffer final : public std::streambuf {
public:
    TeeStreamBuffer(std::streambuf* first, std::streambuf* second)
        : first_(first)
        , second_(second) {}

protected:
    int overflow(int value) override {
        if (value == traits_type::eof()) {
            return traits_type::not_eof(value);
        }

        const char ch = static_cast<char>(value);
        const auto first_result = first_ == nullptr ? ch : first_->sputc(ch);
        const auto second_result = second_ == nullptr ? ch : second_->sputc(ch);
        if (traits_type::eq_int_type(first_result, traits_type::eof())
            || traits_type::eq_int_type(second_result, traits_type::eof())) {
            return traits_type::eof();
        }
        return value;
    }

    std::streamsize xsputn(const char* data, std::streamsize count) override {
        const auto first_count = first_ == nullptr ? count : first_->sputn(data, count);
        const auto second_count = second_ == nullptr ? count : second_->sputn(data, count);
        return std::min(first_count, second_count);
    }

    int sync() override {
        const int first_result = first_ == nullptr ? 0 : first_->pubsync();
        const int second_result = second_ == nullptr ? 0 : second_->pubsync();
        return first_result == 0 && second_result == 0 ? 0 : -1;
    }

private:
    std::streambuf* first_ {nullptr};
    std::streambuf* second_ {nullptr};
};

struct BacktestDailyPnlRow {
    std::string trading_day;
    std::string date;
    double pnl {0.0};
    double cumulative_pnl {0.0};
};

double total_account_initial_cash(const std::map<std::string, SimAccountState>& accounts) {
    double total = 0.0;
    for (const auto& [_, account] : accounts) {
        total += account.snapshot.initial_cash;
    }
    return total;
}

double total_account_cash(const std::map<std::string, SimAccountState>& accounts) {
    double total = 0.0;
    for (const auto& [_, account] : accounts) {
        total += account.snapshot.cash;
    }
    return total;
}

double total_account_realized_pnl(const std::map<std::string, SimAccountState>& accounts) {
    double total = 0.0;
    for (const auto& [_, account] : accounts) {
        total += account.snapshot.realized_pnl;
    }
    return total;
}

std::size_t total_backtest_closed_order_count(const std::map<std::string, SimAccountState>& accounts) {
    std::size_t count = 0;
    for (const auto& [_, account] : accounts) {
        for (const auto& [__, attachment] : account.attachments) {
            count += attachment.closed_order_count;
        }
    }
    return count;
}

std::size_t total_backtest_filled_order_count(const std::map<std::string, SimAccountState>& accounts) {
    std::size_t count = 0;
    for (const auto& [_, account] : accounts) {
        for (const auto& [__, attachment] : account.attachments) {
            count += attachment.filled_trade_count;
        }
    }
    return count;
}

std::size_t total_backtest_filled_trade_volume(const std::map<std::string, SimAccountState>& accounts) {
    std::size_t volume = 0;
    for (const auto& [_, account] : accounts) {
        for (const auto& [__, attachment] : account.attachments) {
            volume += attachment.filled_trade_volume;
        }
    }
    return volume;
}

std::size_t total_backtest_filled_open_volume(const std::map<std::string, SimAccountState>& accounts) {
    std::size_t volume = 0;
    for (const auto& [_, account] : accounts) {
        for (const auto& [__, attachment] : account.attachments) {
            volume += attachment.filled_open_volume;
        }
    }
    return volume;
}

double daily_curve_max_drawdown(const std::vector<BacktestDailyPnlRow>& rows) {
    double peak = 0.0;
    double max_drawdown = 0.0;
    for (const auto& row : rows) {
        peak = std::max(peak, row.cumulative_pnl);
        max_drawdown = std::max(max_drawdown, peak - row.cumulative_pnl);
    }
    return max_drawdown;
}

std::size_t daily_curve_stagnation_days(const std::vector<BacktestDailyPnlRow>& rows) {
    double peak = 0.0;
    std::size_t current = 0;
    std::size_t longest = 0;
    for (const auto& row : rows) {
        if (row.cumulative_pnl > peak) {
            peak = row.cumulative_pnl;
            current = 0;
        } else {
            ++current;
            longest = std::max(longest, current);
        }
    }
    return longest;
}

void append_metric_row(std::ofstream& output, std::string_view metric, std::string_view value) {
    output << csv_escape(metric) << ',' << csv_escape(value) << '\n';
}

void write_daily_pnl_csv(const std::filesystem::path& path, const std::vector<BacktestDailyPnlRow>& rows) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
        throw std::runtime_error("Unable to write daily PnL CSV: " + path.string());
    }

    output << "trading_day,date,pnl,cumulative_pnl\n";
    for (const auto& row : rows) {
        output << csv_escape(row.trading_day) << ','
               << csv_escape(row.date) << ','
               << format_price(row.pnl) << ','
               << format_price(row.cumulative_pnl) << '\n';
    }
}

void write_backtest_closed_orders_csv(const std::filesystem::path& path, const std::map<std::string, SimAccountState>& accounts) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
        throw std::runtime_error("Unable to write backtest closed orders CSV: " + path.string());
    }

    output << "account_id,strategy_id,order_id,source_order_id,client_order_id,instrument,exchange,"
           << "side,offset,requested_volume,filled_volume,limit_price,filled_price,signal_time_ms,status,timestamp,message\n";

    for (const auto& [account_id, account] : accounts) {
        for (const auto& [strategy_id, attachment] : account.attachments) {
            for (const auto& order : attachment.closed_orders) {
                output << csv_escape(account_id) << ','
                       << csv_escape(strategy_id) << ','
                       << csv_escape(order.order_id) << ','
                       << csv_escape(order.source_order_id) << ','
                       << csv_escape(order.client_order_id) << ','
                       << csv_escape(order.instrument) << ','
                       << csv_escape(order.exchange) << ','
                       << itrader::to_string(order.side) << ','
                       << itrader::to_string(order.offset) << ','
                       << order.requested_volume << ','
                       << order.filled_volume << ','
                       << format_price(order.limit_price) << ','
                       << format_price(order.filled_price) << ','
                       << order.signal_time_ms << ','
                       << itrader::to_string(order.status) << ','
                       << csv_escape(order.timestamp) << ','
                       << csv_escape(order.message) << '\n';
            }
        }
    }
}

void write_performance_metrics_csv(
    const std::filesystem::path& path,
    const std::filesystem::path& config_path,
    const std::map<std::string, SimAccountState>& accounts,
    const std::vector<BacktestDailyPnlRow>& daily_rows) {

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
        throw std::runtime_error("Unable to write performance metrics CSV: " + path.string());
    }

    const double initial_cash = total_account_initial_cash(accounts);
    const double ending_cash = total_account_cash(accounts);
    const double realized_pnl = total_account_realized_pnl(accounts);
    const double return_pct = initial_cash == 0.0 ? 0.0 : (realized_pnl / initial_cash) * 100.0;
    const double max_drawdown = daily_curve_max_drawdown(daily_rows);
    const std::size_t stagnation_days = daily_curve_stagnation_days(daily_rows);
    const double pnl_mdd_ratio = max_drawdown > 0.0 ? realized_pnl / max_drawdown : 0.0;
    const std::size_t filled_trade_volume = total_backtest_filled_trade_volume(accounts);
    const std::size_t filled_open_volume = total_backtest_filled_open_volume(accounts);
    const double avg_pnl_per_unit_trade_vol = filled_trade_volume == 0
        ? 0.0
        : realized_pnl / static_cast<double>(filled_trade_volume);
    const double avg_pnl_per_open_vol = filled_open_volume == 0
        ? 0.0
        : realized_pnl / static_cast<double>(filled_open_volume);

    int winning_days = 0;
    int losing_days = 0;
    int flat_days = 0;
    double max_daily_profit = 0.0;
    double max_daily_loss = 0.0;
    for (const auto& row : daily_rows) {
        if (row.pnl > 0.0) {
            ++winning_days;
        } else if (row.pnl < 0.0) {
            ++losing_days;
        } else {
            ++flat_days;
        }
        max_daily_profit = std::max(max_daily_profit, row.pnl);
        max_daily_loss = std::min(max_daily_loss, row.pnl);
    }

    const double avg_daily_pnl = daily_rows.empty()
        ? 0.0
        : realized_pnl / static_cast<double>(daily_rows.size());

    output << "metric,value\n";
    append_metric_row(output, "generated_at", current_timestamp());
    append_metric_row(output, "config_path", config_path.string());
    append_metric_row(output, "account_count", std::to_string(accounts.size()));
    append_metric_row(output, "initial_cash", format_price(initial_cash));
    append_metric_row(output, "ending_cash", format_price(ending_cash));
    append_metric_row(output, "realized_pnl", format_price(realized_pnl));
    append_metric_row(output, "return_pct", format_price(return_pct));
    append_metric_row(output, "closed_order_count", std::to_string(total_backtest_closed_order_count(accounts)));
    append_metric_row(output, "filled_order_count", std::to_string(total_backtest_filled_order_count(accounts)));
    append_metric_row(output, "filled_trade_volume", std::to_string(filled_trade_volume));
    append_metric_row(output, "avg_pnl_per_unit_trade_vol", format_price(avg_pnl_per_unit_trade_vol));
    append_metric_row(output, "filled_open_volume", std::to_string(filled_open_volume));
    append_metric_row(output, "avg_pnl_per_open_vol", format_price(avg_pnl_per_open_vol));
    append_metric_row(output, "trading_day_count", std::to_string(daily_rows.size()));
    append_metric_row(output, "winning_day_count", std::to_string(winning_days));
    append_metric_row(output, "losing_day_count", std::to_string(losing_days));
    append_metric_row(output, "flat_day_count", std::to_string(flat_days));
    append_metric_row(output, "avg_daily_pnl", format_price(avg_daily_pnl));
    append_metric_row(output, "max_daily_profit", format_price(max_daily_profit));
    append_metric_row(output, "max_daily_loss", format_price(max_daily_loss));
    append_metric_row(output, "max_drawdown", format_price(max_drawdown));
    append_metric_row(output, "stagnation(days)", std::to_string(stagnation_days));
    append_metric_row(output, "pnl_mdd_ratio", format_price(pnl_mdd_ratio));

    for (const auto& [account_id, account] : accounts) {
        append_metric_row(output, "account." + account_id + ".initial_cash", format_price(account.snapshot.initial_cash));
        append_metric_row(output, "account." + account_id + ".ending_cash", format_price(account.snapshot.cash));
        append_metric_row(output, "account." + account_id + ".realized_pnl", format_price(account.snapshot.realized_pnl));
    }
}

void write_daily_cumulative_pnl_svg(const std::filesystem::path& path, const std::vector<BacktestDailyPnlRow>& rows) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
        throw std::runtime_error("Unable to write daily cumulative PnL SVG: " + path.string());
    }

    constexpr int width = 1200;
    constexpr int height = 720;
    constexpr int left = 86;
    constexpr int right = 48;
    constexpr int top = 78;
    constexpr int bottom = 92;
    constexpr int plot_width = width - left - right;
    constexpr int plot_height = height - top - bottom;

    output << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << width
           << "\" height=\"" << height << "\" viewBox=\"0 0 " << width << ' ' << height << "\">\n"
           << "<rect width=\"100%\" height=\"100%\" fill=\"#ffffff\"/>\n"
           << "<text x=\"" << (width / 2) << "\" y=\"38\" text-anchor=\"middle\" "
           << "font-family=\"Segoe UI, Arial, sans-serif\" font-size=\"22\" font-weight=\"600\" fill=\"#111827\">"
           << "Daily Cumulative PnL</text>\n";

    if (rows.empty()) {
        output << "<text x=\"" << (width / 2) << "\" y=\"" << (height / 2)
               << "\" text-anchor=\"middle\" font-family=\"Segoe UI, Arial, sans-serif\" "
               << "font-size=\"18\" fill=\"#6b7280\">No daily PnL rows</text>\n</svg>\n";
        return;
    }

    double min_value = 0.0;
    double max_value = 0.0;
    for (const auto& row : rows) {
        min_value = std::min(min_value, row.cumulative_pnl);
        max_value = std::max(max_value, row.cumulative_pnl);
    }
    double span = max_value - min_value;
    if (span < 1e-9) {
        span = 1.0;
        min_value -= 0.5;
        max_value += 0.5;
    } else {
        const double padding = span * 0.08;
        min_value -= padding;
        max_value += padding;
        span = max_value - min_value;
    }

    auto x_for_index = [&](std::size_t index) {
        if (rows.size() == 1) {
            return static_cast<double>(left + plot_width / 2);
        }
        return static_cast<double>(left) + (static_cast<double>(index) / static_cast<double>(rows.size() - 1)) * plot_width;
    };
    auto y_for_value = [&](double value) {
        return static_cast<double>(top) + ((max_value - value) / span) * plot_height;
    };

    output << "<line x1=\"" << left << "\" y1=\"" << top << "\" x2=\"" << left
           << "\" y2=\"" << (top + plot_height) << "\" stroke=\"#9ca3af\" stroke-width=\"1\"/>\n"
           << "<line x1=\"" << left << "\" y1=\"" << (top + plot_height)
           << "\" x2=\"" << (left + plot_width) << "\" y2=\"" << (top + plot_height)
           << "\" stroke=\"#9ca3af\" stroke-width=\"1\"/>\n";

    constexpr int y_ticks = 5;
    for (int tick = 0; tick <= y_ticks; ++tick) {
        const double ratio = static_cast<double>(tick) / static_cast<double>(y_ticks);
        const double value = max_value - ratio * span;
        const double y = static_cast<double>(top) + ratio * plot_height;
        output << "<line x1=\"" << left << "\" y1=\"" << y << "\" x2=\"" << (left + plot_width)
               << "\" y2=\"" << y << "\" stroke=\"#e5e7eb\" stroke-width=\"1\"/>\n"
               << "<text x=\"" << (left - 10) << "\" y=\"" << (y + 4)
               << "\" text-anchor=\"end\" font-family=\"Segoe UI, Arial, sans-serif\" "
               << "font-size=\"12\" fill=\"#4b5563\">" << format_price(value) << "</text>\n";
    }

    const double zero_y = y_for_value(0.0);
    if (zero_y >= top && zero_y <= top + plot_height) {
        output << "<line x1=\"" << left << "\" y1=\"" << zero_y
               << "\" x2=\"" << (left + plot_width) << "\" y2=\"" << zero_y
               << "\" stroke=\"#94a3b8\" stroke-width=\"1.2\" stroke-dasharray=\"6 5\"/>\n";
    }

    output << "<polyline fill=\"none\" stroke=\"#0f766e\" stroke-width=\"3\" stroke-linejoin=\"round\" stroke-linecap=\"round\" points=\"";
    for (std::size_t index = 0; index < rows.size(); ++index) {
        if (index > 0) {
            output << ' ';
        }
        output << x_for_index(index) << ',' << y_for_value(rows[index].cumulative_pnl);
    }
    output << "\"/>\n";

    const std::size_t max_labels = 8;
    const std::size_t label_step = std::max<std::size_t>(1, (rows.size() + max_labels - 1) / max_labels);
    for (std::size_t index = 0; index < rows.size(); index += label_step) {
        const double x = x_for_index(index);
        output << "<text x=\"" << x << "\" y=\"" << (top + plot_height + 28)
               << "\" text-anchor=\"middle\" font-family=\"Segoe UI, Arial, sans-serif\" "
               << "font-size=\"12\" fill=\"#4b5563\" transform=\"rotate(35 " << x << ' ' << (top + plot_height + 28)
               << ")\">" << svg_escape(rows[index].date) << "</text>\n";
    }
    if ((rows.size() - 1) % label_step != 0) {
        const auto& row = rows.back();
        const double x = x_for_index(rows.size() - 1);
        output << "<text x=\"" << x << "\" y=\"" << (top + plot_height + 28)
               << "\" text-anchor=\"middle\" font-family=\"Segoe UI, Arial, sans-serif\" "
               << "font-size=\"12\" fill=\"#4b5563\" transform=\"rotate(35 " << x << ' ' << (top + plot_height + 28)
               << ")\">" << svg_escape(row.date) << "</text>\n";
    }

    const auto& final_row = rows.back();
    const double final_x = x_for_index(rows.size() - 1);
    const double final_y = y_for_value(final_row.cumulative_pnl);
    output << "<circle cx=\"" << final_x << "\" cy=\"" << final_y << "\" r=\"4.5\" fill=\"#0f766e\"/>\n"
           << "<text x=\"" << (left + plot_width) << "\" y=\"58\" text-anchor=\"end\" "
           << "font-family=\"Segoe UI, Arial, sans-serif\" font-size=\"14\" fill=\"#374151\">"
           << "Final: " << format_price(final_row.cumulative_pnl) << "</text>\n"
           << "</svg>\n";
}

class BacktestCliOutputSession {
public:
    BacktestCliOutputSession(const std::filesystem::path& config_path, const std::filesystem::path& requested_output_dir) {
        output_dir_ = std::filesystem::absolute(requested_output_dir).lexically_normal();

        std::error_code error_code;
        std::filesystem::create_directories(output_dir_, error_code);
        if (error_code) {
            throw std::runtime_error("Unable to create backtest output directory: " + output_dir_.string());
        }

        const auto normalized_output_dir = std::filesystem::weakly_canonical(output_dir_, error_code);
        if (!error_code) {
            output_dir_ = normalized_output_dir;
        }

        log_path_ = output_dir_ / "trader_backtest.log";
        log_stream_.open(log_path_, std::ios::binary | std::ios::trunc);
        if (!log_stream_.is_open()) {
            throw std::runtime_error("Unable to open backtest log file: " + log_path_.string());
        }

        install_tee();
        copy_config_file(config_path);
    }

    ~BacktestCliOutputSession() {
        restore_streams();
    }

    BacktestCliOutputSession(const BacktestCliOutputSession&) = delete;
    BacktestCliOutputSession& operator=(const BacktestCliOutputSession&) = delete;

    const std::filesystem::path& output_dir() const {
        return output_dir_;
    }

    const std::filesystem::path& log_path() const {
        return log_path_;
    }

    const std::filesystem::path& copied_config_path() const {
        return copied_config_path_;
    }

    void export_backtest_results(
        const std::filesystem::path& config_path,
        const std::map<std::string, SimAccountState>& accounts,
        const std::vector<BacktestDailyPnlRow>& daily_rows) const {

        const auto metrics_csv_path = output_dir_ / "performance_metrics.csv";
        const auto metrics_cvv_path = output_dir_ / "performance_metrics.cvv";
        const auto daily_csv_path = output_dir_ / "daily_cumulative_pnl.csv";
        const auto daily_svg_path = output_dir_ / "daily_cumulative_pnl.svg";
        const auto closed_orders_csv_path = output_dir_ / "closed_orders.csv";

        write_performance_metrics_csv(metrics_csv_path, config_path, accounts, daily_rows);
        write_performance_metrics_csv(metrics_cvv_path, config_path, accounts, daily_rows);
        write_daily_pnl_csv(daily_csv_path, daily_rows);
        write_daily_cumulative_pnl_svg(daily_svg_path, daily_rows);
        write_backtest_closed_orders_csv(closed_orders_csv_path, accounts);
    }

private:
    void install_tee() {
        old_cout_buffer_ = std::cout.rdbuf();
        old_cerr_buffer_ = std::cerr.rdbuf();
        cout_tee_buffer_ = std::make_unique<TeeStreamBuffer>(old_cout_buffer_, log_stream_.rdbuf());
        cerr_tee_buffer_ = std::make_unique<TeeStreamBuffer>(old_cerr_buffer_, log_stream_.rdbuf());
        std::cout.rdbuf(cout_tee_buffer_.get());
        std::cerr.rdbuf(cerr_tee_buffer_.get());
    }

    void restore_streams() {
        if (old_cout_buffer_ != nullptr) {
            std::cout.rdbuf(old_cout_buffer_);
            old_cout_buffer_ = nullptr;
        }
        if (old_cerr_buffer_ != nullptr) {
            std::cerr.rdbuf(old_cerr_buffer_);
            old_cerr_buffer_ = nullptr;
        }
        if (log_stream_.is_open()) {
            log_stream_.flush();
        }
    }

    void copy_config_file(const std::filesystem::path& config_path) {
        auto filename = config_path.filename();
        if (filename.empty()) {
            filename = "backtest_config.ini";
        }

        copied_config_path_ = output_dir_ / filename;
        std::error_code error_code;
        if (std::filesystem::equivalent(config_path, copied_config_path_, error_code) && !error_code) {
            return;
        }
        error_code.clear();
        std::filesystem::copy_file(config_path, copied_config_path_, std::filesystem::copy_options::overwrite_existing, error_code);
        if (error_code) {
            throw std::runtime_error(
                "Unable to copy backtest config to output directory: " + copied_config_path_.string());
        }
    }

    std::filesystem::path output_dir_;
    std::filesystem::path log_path_;
    std::filesystem::path copied_config_path_;
    std::ofstream log_stream_;
    std::unique_ptr<TeeStreamBuffer> cout_tee_buffer_;
    std::unique_ptr<TeeStreamBuffer> cerr_tee_buffer_;
    std::streambuf* old_cout_buffer_ {nullptr};
    std::streambuf* old_cerr_buffer_ {nullptr};
};

std::vector<std::string> split_tokenized(std::string_view value, char delimiter) {
    std::vector<std::string> parts;
    std::string current;
    for (const char ch : value) {
        if (ch == delimiter) {
            parts.push_back(current);
            current.clear();
            continue;
        }
        current.push_back(ch);
    }
    parts.push_back(current);
    return parts;
}

std::filesystem::path live_telemetry_path(const std::filesystem::path& config_path) {
    return itrader::live_telemetry_path(config_path);
}

std::string telemetry_attachment_key(std::string_view strategy_id, std::string_view account_id) {
    return std::string(strategy_id) + "::" + std::string(account_id);
}

std::string encode_telemetry_order_section(std::string_view prefix, std::string_view strategy_id, std::string_view account_id, std::size_t index) {
    return std::string(prefix) + '.' + std::string(strategy_id) + '.' + std::string(account_id) + '.' + std::to_string(index);
}

bool is_leap_year_for_timestamp(int year) {
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

int days_in_month_for_timestamp(int year, int month) {
    static const int days[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 2 && is_leap_year_for_timestamp(year)) {
        return 29;
    }
    return days[month];
}

int parse_fixed_width_int(std::string_view text, std::size_t offset, std::size_t width) {
    if (offset + width > text.size()) {
        return -1;
    }

    int value = 0;
    for (std::size_t index = offset; index < offset + width; ++index) {
        if (std::isdigit(static_cast<unsigned char>(text[index])) == 0) {
            return -1;
        }
        value = value * 10 + (text[index] - '0');
    }
    return value;
}

int parse_fractional_millis(std::string_view text, std::size_t offset) {
    if (offset >= text.size() || std::isdigit(static_cast<unsigned char>(text[offset])) == 0) {
        return 0;
    }

    int value = 0;
    int digits = 0;
    for (std::size_t index = offset; index < text.size() && digits < 3; ++index) {
        if (std::isdigit(static_cast<unsigned char>(text[index])) == 0) {
            break;
        }
        value = value * 10 + (text[index] - '0');
        ++digits;
    }

    while (digits < 3) {
        value *= 10;
        ++digits;
    }

    return value;
}

std::optional<long long> parse_timestamp_to_millis(std::string_view raw_timestamp) {
    std::size_t begin = 0;
    while (begin < raw_timestamp.size() && std::isspace(static_cast<unsigned char>(raw_timestamp[begin])) != 0) {
        ++begin;
    }

    std::size_t end = raw_timestamp.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(raw_timestamp[end - 1])) != 0) {
        --end;
    }

    const auto trimmed = raw_timestamp.substr(begin, end - begin);
    if (trimmed.empty()) {
        return std::nullopt;
    }

    int year = -1;
    int month = -1;
    int day = -1;
    int hour = -1;
    int minute = -1;
    int second = -1;
    int millis = 0;

    if (trimmed.size() >= 19 && trimmed[4] == '-' && trimmed[7] == '-') {
        year = parse_fixed_width_int(trimmed, 0, 4);
        month = parse_fixed_width_int(trimmed, 5, 2);
        day = parse_fixed_width_int(trimmed, 8, 2);
        hour = parse_fixed_width_int(trimmed, 11, 2);
        minute = parse_fixed_width_int(trimmed, 14, 2);
        second = parse_fixed_width_int(trimmed, 17, 2);
        if (trimmed.size() > 20 && trimmed[19] == '.') {
            millis = parse_fractional_millis(trimmed, 20);
        }
    } else if (trimmed.size() >= 14
        && std::all_of(trimmed.begin(), trimmed.begin() + 14, [](unsigned char ch) { return std::isdigit(ch) != 0; })) {
        year = parse_fixed_width_int(trimmed, 0, 4);
        month = parse_fixed_width_int(trimmed, 4, 2);
        day = parse_fixed_width_int(trimmed, 6, 2);
        hour = parse_fixed_width_int(trimmed, 8, 2);
        minute = parse_fixed_width_int(trimmed, 10, 2);
        second = parse_fixed_width_int(trimmed, 12, 2);
        if (trimmed.size() > 15 && trimmed[14] == '.' && std::isdigit(static_cast<unsigned char>(trimmed[15])) != 0) {
            millis = parse_fractional_millis(trimmed, 15);
        }
    } else if (trimmed.size() >= 17 && trimmed[8] == ' ') {
        year = parse_fixed_width_int(trimmed, 0, 4);
        month = parse_fixed_width_int(trimmed, 4, 2);
        day = parse_fixed_width_int(trimmed, 6, 2);
        hour = parse_fixed_width_int(trimmed, 9, 2);
        minute = parse_fixed_width_int(trimmed, 12, 2);
        second = parse_fixed_width_int(trimmed, 15, 2);
        if (trimmed.size() > 18 && trimmed[17] == '.') {
            millis = parse_fractional_millis(trimmed, 18);
        }
    }

    if (year < 0 || month < 1 || month > 12 || day < 1 || day > days_in_month_for_timestamp(year, month)
        || hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 59) {
        return std::nullopt;
    }

    long long days = 0;
    for (int current_year = 1970; current_year < year; ++current_year) {
        days += is_leap_year_for_timestamp(current_year) ? 366 : 365;
    }
    for (int current_month = 1; current_month < month; ++current_month) {
        days += days_in_month_for_timestamp(year, current_month);
    }
    days += day - 1;

    return (((days * 24LL + hour) * 60LL + minute) * 60LL + second) * 1000LL + millis;
}

std::optional<long long> parse_timestamp_to_epoch(std::string_view raw_timestamp) {
    const auto millis = parse_timestamp_to_millis(raw_timestamp);
    if (!millis.has_value()) {
        return std::nullopt;
    }
    return *millis / 1000LL;
}

std::optional<long long> parse_local_timestamp_to_epoch(std::string_view raw_timestamp) {
    const std::string trimmed = trim_copy(raw_timestamp);
    if (trimmed.empty()) {
        return std::nullopt;
    }

    auto try_parse = [](std::string_view candidate, const char* format) -> std::optional<long long> {
        std::tm parsed_time {};
        parsed_time.tm_isdst = -1;
        std::istringstream input {std::string(candidate)};
        input >> std::get_time(&parsed_time, format);
        if (input.fail()) {
            return std::nullopt;
        }

        const std::time_t local_time = std::mktime(&parsed_time);
        if (local_time == static_cast<std::time_t>(-1)) {
            return std::nullopt;
        }
        return static_cast<long long>(local_time);
    };

    if (trimmed.size() >= 19 && trimmed[4] == '-' && trimmed[7] == '-') {
        return try_parse(trimmed.substr(0, 19), "%Y-%m-%d %H:%M:%S");
    }

    if (trimmed.size() >= 14
        && std::all_of(trimmed.begin(), trimmed.begin() + 14, [](unsigned char ch) { return std::isdigit(ch) != 0; })) {
        return try_parse(trimmed.substr(0, 14), "%Y%m%d%H%M%S");
    }

    if (trimmed.size() >= 17 && trimmed[8] == ' ') {
        return try_parse(trimmed.substr(0, 17), "%Y%m%d %H:%M:%S");
    }

    return std::nullopt;
}

std::string format_epoch_timestamp(long long epoch_seconds) {
    const std::time_t time_value = static_cast<std::time_t>(epoch_seconds);
    std::tm local_time {};
    localtime_s(&local_time, &time_value);

    std::ostringstream output;
    output << std::put_time(&local_time, "%Y-%m-%d %H:%M:%S");
    return output.str();
}

long long chart_bucket_start(long long epoch_seconds, int chart_bar_seconds) {
    const long long bucket_seconds = std::max(1, chart_bar_seconds);
    return epoch_seconds - (epoch_seconds % bucket_seconds);
}

enum class ChartNightSession {
    None,
    EndsAt2300,
    EndsAt0100,
    EndsAt0230,
};

bool is_commodity_exchange_for_chart(std::string_view exchange) {
    const auto normalized_exchange = normalize_exchange_code(exchange);
    return normalized_exchange.empty()
        || normalized_exchange == "SHFE"
        || normalized_exchange == "DCE"
        || normalized_exchange == "CZCE"
        || normalized_exchange == "INE"
        || normalized_exchange == "GFEX";
}

ChartNightSession commodity_night_session_for_chart(std::string_view instrument, std::string_view exchange) {
    const auto normalized_exchange = normalize_exchange_code(exchange);
    const auto prefix = instrument_alpha_prefix(instrument);

    if (normalized_exchange.empty() || normalized_exchange == "SHFE") {
        if (prefix_matches_any(prefix, {"AU", "AG"})) {
            return ChartNightSession::EndsAt0230;
        }
        if (prefix_matches_any(prefix, {"CU", "AL", "ZN", "PB", "NI", "SN", "SS"})) {
            return ChartNightSession::EndsAt0100;
        }
        if (prefix_matches_any(prefix, {"RB", "HC", "FU", "BU", "RU", "BR", "SP"})) {
            return ChartNightSession::EndsAt2300;
        }
        return ChartNightSession::None;
    }

    if (normalized_exchange == "INE") {
        if (prefix_matches_any(prefix, {"SC"})) {
            return ChartNightSession::EndsAt0230;
        }
        if (prefix_matches_any(prefix, {"BC"})) {
            return ChartNightSession::EndsAt0100;
        }
        return ChartNightSession::None;
    }

    if (normalized_exchange == "DCE") {
        if (prefix_matches_any(prefix, {"A", "B", "M", "Y", "P", "C", "CS", "JD", "L", "V", "PP", "J", "JM", "I", "EG", "EB", "PG", "RR", "LH"})) {
            return ChartNightSession::EndsAt2300;
        }
        return ChartNightSession::None;
    }

    if (normalized_exchange == "CZCE") {
        if (prefix_matches_any(prefix, {"CF", "CY", "SR", "TA", "MA", "FG", "RM", "ZC", "OI", "SA", "PF", "PX", "SH", "SM", "SF"})) {
            return ChartNightSession::EndsAt2300;
        }
        return ChartNightSession::None;
    }

    return ChartNightSession::None;
}

bool is_stock_index_future_for_chart(std::string_view instrument, std::string_view exchange) {
    if (normalize_exchange_code(exchange) != "CFFEX") {
        return false;
    }
    const auto prefix = instrument_alpha_prefix(instrument);
    return prefix == "IF" || prefix == "IH" || prefix == "IC" || prefix == "IM";
}

bool is_treasury_future_for_chart(std::string_view instrument, std::string_view exchange) {
    if (normalize_exchange_code(exchange) != "CFFEX") {
        return false;
    }
    const auto prefix = instrument_alpha_prefix(instrument);
    return prefix == "TS" || prefix == "TF" || prefix == "T" || prefix == "TL";
}

std::optional<long long> previous_minute_bucket(long long raw_minute_epoch) {
    if (raw_minute_epoch < 60) {
        return std::nullopt;
    }
    return raw_minute_epoch - 60;
}

std::optional<long long> session_aligned_minute_bucket_epoch(const MarketTick& tick) {
    const auto timestamp_ms = tick.timestamp_ms > 0
        ? std::optional<long long>(tick.timestamp_ms)
        : parse_timestamp_to_millis(tick.timestamp);
    if (!timestamp_ms.has_value() || *timestamp_ms <= 0) {
        return std::nullopt;
    }

    const long long raw_minute_epoch = (*timestamp_ms / 60'000LL) * 60LL;
    const int seconds_since_midnight = static_cast<int>(((*timestamp_ms / 1000LL) % 86'400LL + 86'400LL) % 86'400LL);
    const int hour = seconds_since_midnight / 3600;
    const int minute = (seconds_since_midnight % 3600) / 60;
    const int hhmm = hour * 100 + minute;

    if (hhmm == 859 || hhmm == 2059) {
        return raw_minute_epoch + 60;
    }

    if (is_stock_index_future_for_chart(tick.instrument, tick.exchange)) {
        if (hhmm == 929) {
            return raw_minute_epoch + 60;
        }
        if (hhmm == 1130 || hhmm == 1500) {
            return previous_minute_bucket(raw_minute_epoch);
        }
        if ((hhmm >= 930 && hhmm <= 1129) || (hhmm >= 1300 && hhmm <= 1459)) {
            return raw_minute_epoch;
        }
        return std::nullopt;
    }

    if (is_treasury_future_for_chart(tick.instrument, tick.exchange)) {
        if (hhmm == 929) {
            return raw_minute_epoch + 60;
        }
        if (hhmm == 1130 || hhmm == 1515) {
            return previous_minute_bucket(raw_minute_epoch);
        }
        if ((hhmm >= 930 && hhmm <= 1129) || (hhmm >= 1300 && hhmm <= 1514)) {
            return raw_minute_epoch;
        }
        return std::nullopt;
    }

    if (!is_commodity_exchange_for_chart(tick.exchange)) {
        return raw_minute_epoch;
    }

    const auto night_session = commodity_night_session_for_chart(tick.instrument, tick.exchange);
    if (hhmm == 1015 || hhmm == 1130 || hhmm == 1500
        || (night_session == ChartNightSession::EndsAt2300 && hhmm == 2300)
        || (night_session == ChartNightSession::EndsAt0100 && hhmm == 100)
        || (night_session == ChartNightSession::EndsAt0230 && hhmm == 230)) {
        return previous_minute_bucket(raw_minute_epoch);
    }

    if ((hhmm >= 900 && hhmm <= 1014)
        || (hhmm >= 1030 && hhmm <= 1129)
        || (hhmm >= 1330 && hhmm <= 1459)) {
        return raw_minute_epoch;
    }

    if (night_session == ChartNightSession::EndsAt2300 && hhmm >= 2100 && hhmm <= 2259) {
        return raw_minute_epoch;
    }
    if (night_session == ChartNightSession::EndsAt0100
        && ((hhmm >= 2100 && hhmm <= 2359) || (hhmm >= 0 && hhmm <= 59))) {
        return raw_minute_epoch;
    }
    if (night_session == ChartNightSession::EndsAt0230
        && ((hhmm >= 2100 && hhmm <= 2359) || (hhmm >= 0 && hhmm <= 229))) {
        return raw_minute_epoch;
    }

    return std::nullopt;
}

std::optional<long long> session_aligned_chart_bucket_epoch(const MarketTick& tick, int chart_bar_seconds) {
    const auto minute_bucket = session_aligned_minute_bucket_epoch(tick);
    if (!minute_bucket.has_value()) {
        return std::nullopt;
    }
    return chart_bucket_start(*minute_bucket, chart_bar_seconds);
}

struct LiveChartBar {
    std::string instrument;
    long long bucket_epoch {0};
    std::string timestamp;
    double open {0.0};
    double high {0.0};
    double low {0.0};
    double close {0.0};
};

struct LiveAttachmentTelemetry {
    StrategyAttachmentSnapshot snapshot;
    std::map<std::string, PositionState> positions;
    std::map<std::string, RuntimeOrderSnapshot> opened_orders_by_id;
    std::vector<RuntimeOrderSnapshot> closed_orders;
};

bool is_terminal_status(OrderStatus status);
itrader::RuntimeOrderSnapshot make_runtime_order_snapshot(const OrderEvent& event);

bool is_trade_fill_event(const OrderEvent& event) {
    return event.filled_volume > 0
        && (event.status == OrderStatus::Filled || event.status == OrderStatus::PartiallyFilled)
        && event.filled_price > 0.0;
}

std::string live_fill_tracking_key(const OrderEvent& event) {
    if (!event.order_id.empty()) {
        return event.order_id;
    }
    if (!event.client_order_id.empty()) {
        return "client:" + event.client_order_id;
    }
    if (!event.source_order_id.empty()) {
        return "trade:" + event.source_order_id;
    }
    return {};
}

int consume_live_fill_delta(LiveAccountState& account, const OrderEvent& event) {
    if (!is_trade_fill_event(event)) {
        return 0;
    }

    const int cumulative_filled_volume = std::max(0, event.filled_volume);
    const auto key = live_fill_tracking_key(event);
    if (key.empty()) {
        return cumulative_filled_volume;
    }

    auto& previous_filled_volume = account.applied_filled_volume_by_order[key];
    const int fill_delta = std::max(0, cumulative_filled_volume - previous_filled_volume);
    previous_filled_volume = std::max(previous_filled_volume, cumulative_filled_volume);
    return fill_delta;
}

void refresh_attachment_positions(LiveAttachmentTelemetry& telemetry) {
    telemetry.snapshot.positions.clear();
    for (const auto& [instrument, position_state] : telemetry.positions) {
        telemetry.snapshot.positions.push_back(make_runtime_position_snapshot(
            instrument,
            telemetry.snapshot.account_id,
            telemetry.snapshot.strategy_id,
            position_state));
    }
}

void refresh_attachment_orders(LiveAttachmentTelemetry& telemetry) {
    telemetry.snapshot.opened_orders.clear();
    for (const auto& [_, order] : telemetry.opened_orders_by_id) {
        telemetry.snapshot.opened_orders.push_back(order);
    }
    telemetry.snapshot.closed_orders = telemetry.closed_orders;
    telemetry.snapshot.opened_order_count = telemetry.snapshot.opened_orders.size();
    telemetry.snapshot.closed_order_count = telemetry.snapshot.closed_orders.size();
    telemetry.snapshot.filled_trade_count = static_cast<std::size_t>(std::count_if(
        telemetry.snapshot.closed_orders.begin(),
        telemetry.snapshot.closed_orders.end(),
        [](const RuntimeOrderSnapshot& order) {
            return order.filled_volume > 0 && order.filled_price > 0.0;
        }));
}

void update_live_attachment(LiveAttachmentTelemetry& telemetry, const OrderEvent& event, int fill_delta) {
    RuntimeOrderSnapshot order = make_runtime_order_snapshot(event);

    if (fill_delta > 0) {
        auto& position = telemetry.positions[event.instrument];
        OrderRequest synthetic_request;
        synthetic_request.account_id = event.account_id;
        synthetic_request.strategy_id = event.strategy_id;
        synthetic_request.client_order_id = event.client_order_id;
        synthetic_request.instrument = event.instrument;
        synthetic_request.exchange = event.exchange;
        synthetic_request.side = event.side;
        synthetic_request.offset = event.offset;
        synthetic_request.price_type = PriceType::Market;
        synthetic_request.immediate_or_cancel = false;
        synthetic_request.limit_price = event.limit_price;
        synthetic_request.volume = fill_delta;
        apply_fill(position, synthetic_request, event.filled_price, fill_delta);
        refresh_attachment_positions(telemetry);
    }

    if (is_terminal_status(event.status)) {
        telemetry.opened_orders_by_id.erase(order.order_id);
        telemetry.closed_orders.push_back(order);
    } else {
        telemetry.opened_orders_by_id[order.order_id] = order;
    }

    refresh_attachment_orders(telemetry);
}

void update_live_chart_bars(
    std::map<std::string, std::vector<LiveChartBar>>& live_chart_bars,
    const MarketTick& tick,
    int bar_seconds,
    std::size_t max_bars_per_instrument) {

    if (tick.instrument.empty() || tick.last <= 0.0) {
        return;
    }

    const int normalized_bar_seconds = std::max(bar_seconds, 1);
    const auto aligned_bucket_epoch = session_aligned_chart_bucket_epoch(tick, normalized_bar_seconds);
    if (!aligned_bucket_epoch.has_value()) {
        return;
    }

    const long long bucket_epoch = *aligned_bucket_epoch;
    auto& bars = live_chart_bars[tick.instrument];
    if (!bars.empty()) {
        auto& current_bar = bars.back();
        if (bucket_epoch < current_bar.bucket_epoch) {
            return;
        }
        if (bucket_epoch == current_bar.bucket_epoch) {
            current_bar.high = std::max(current_bar.high, tick.last);
            current_bar.low = std::min(current_bar.low, tick.last);
            current_bar.close = tick.last;
            return;
        }
    }

    LiveChartBar bar;
    bar.instrument = tick.instrument;
    bar.bucket_epoch = bucket_epoch;
    bar.timestamp = format_epoch_timestamp(bucket_epoch);
    bar.open = tick.last;
    bar.high = tick.last;
    bar.low = tick.last;
    bar.close = tick.last;
    bars.push_back(std::move(bar));

    if (bars.size() > max_bars_per_instrument) {
        bars.erase(bars.begin(), bars.begin() + static_cast<std::ptrdiff_t>(bars.size() - max_bars_per_instrument));
    }
}

double live_chart_bar_range(const LiveChartBar& bar) {
    return std::max(0.0, bar.high - bar.low);
}

bool live_chart_bar_has_valid_prices(const LiveChartBar& bar) {
    return bar.open > 0.0 && bar.high > 0.0 && bar.low > 0.0 && bar.close > 0.0
        && bar.high >= bar.low
        && bar.high >= bar.open
        && bar.high >= bar.close
        && bar.low <= bar.open
        && bar.low <= bar.close;
}

double live_chart_bar_wide_range_threshold(const LiveChartBar& left, const LiveChartBar& right) {
    const double reference_price = std::max({
        std::abs(left.open),
        std::abs(left.close),
        std::abs(right.open),
        std::abs(right.close),
        1.0,
    });
    return std::max(100.0, reference_price * 0.01);
}

bool should_replace_live_chart_bar(const LiveChartBar& existing, const LiveChartBar& incoming) {
    if (!live_chart_bar_has_valid_prices(existing)) {
        return live_chart_bar_has_valid_prices(incoming);
    }
    if (!live_chart_bar_has_valid_prices(incoming)) {
        return false;
    }

    const double threshold = live_chart_bar_wide_range_threshold(existing, incoming);
    return live_chart_bar_range(existing) > threshold
        && live_chart_bar_range(incoming) <= threshold;
}

bool should_merge_live_chart_bar(const LiveChartBar& existing, const LiveChartBar& incoming) {
    if (!live_chart_bar_has_valid_prices(existing) || !live_chart_bar_has_valid_prices(incoming)) {
        return false;
    }

    const double threshold = live_chart_bar_wide_range_threshold(existing, incoming);
    if (live_chart_bar_range(incoming) > threshold && live_chart_bar_range(existing) <= threshold) {
        return false;
    }

    const bool disjoint_price_ranges = incoming.high < existing.low || incoming.low > existing.high;
    return !disjoint_price_ranges;
}

void merge_live_chart_bars(
    std::map<std::string, std::vector<LiveChartBar>>& live_chart_bars,
    const std::map<std::string, std::vector<LiveChartBar>>& incoming_bars,
    std::size_t max_bars_per_instrument) {

    for (const auto& [instrument, incoming] : incoming_bars) {
        if (instrument.empty() || incoming.empty()) {
            continue;
        }

        auto& target = live_chart_bars[instrument];
        std::map<long long, LiveChartBar> bars_by_time;
        for (const auto& bar : target) {
            if (bar.bucket_epoch > 0 && live_chart_bar_has_valid_prices(bar)) {
                bars_by_time[bar.bucket_epoch] = bar;
            }
        }
        for (const auto& bar : incoming) {
            if (bar.bucket_epoch <= 0 || !live_chart_bar_has_valid_prices(bar)) {
                continue;
            }
            auto it = bars_by_time.find(bar.bucket_epoch);
            if (it == bars_by_time.end()) {
                bars_by_time.emplace(bar.bucket_epoch, bar);
                continue;
            }

            auto& merged = it->second;
            if (should_replace_live_chart_bar(merged, bar)) {
                merged = bar;
                continue;
            }
            if (!should_merge_live_chart_bar(merged, bar)) {
                continue;
            }
            if (merged.open <= 0.0) {
                merged.open = bar.open;
            }
            merged.high = std::max(merged.high, bar.high);
            merged.low = merged.low > 0.0 ? std::min(merged.low, bar.low) : bar.low;
            merged.close = bar.close;
            merged.timestamp = bar.timestamp.empty() ? format_epoch_timestamp(bar.bucket_epoch) : bar.timestamp;
        }

        target.clear();
        target.reserve(bars_by_time.size());
        for (auto& [_, bar] : bars_by_time) {
            target.push_back(std::move(bar));
        }

        if (max_bars_per_instrument > 0 && target.size() > max_bars_per_instrument) {
            target.erase(target.begin(), target.begin() + static_cast<std::ptrdiff_t>(target.size() - max_bars_per_instrument));
        }
    }
}

std::map<std::string, std::vector<LiveChartBar>> read_persisted_live_chart_bars(
    const std::filesystem::path& config_path,
    std::size_t max_bars_per_instrument) {

    std::map<std::string, std::vector<LiveChartBar>> live_chart_bars;
    const auto telemetry_path = live_telemetry_path(config_path);
    if (!std::filesystem::exists(telemetry_path)) {
        return live_chart_bars;
    }

    try {
        const auto telemetry = IniFile::parse(telemetry_path);
        for (const auto& section : telemetry.sections_with_prefix("telemetry_chart_bar.")) {
            const auto instrument = trim_copy(telemetry.get(section, "instrument"));
            const auto timestamp = trim_copy(telemetry.get(section, "timestamp"));
            std::optional<long long> epoch;
            const auto raw_bucket_epoch = trim_copy(telemetry.get(section, "bucket_epoch"));
            if (!raw_bucket_epoch.empty()) {
                try {
                    epoch = std::stoll(raw_bucket_epoch);
                } catch (const std::exception&) {
                    epoch = std::nullopt;
                }
            }
            if (!epoch.has_value()) {
                epoch = parse_local_timestamp_to_epoch(timestamp);
            }
            if (!epoch.has_value()) {
                epoch = parse_timestamp_to_epoch(timestamp);
            }
            if (instrument.empty() || !epoch.has_value()) {
                continue;
            }

            LiveChartBar bar;
            bar.instrument = instrument;
            bar.bucket_epoch = *epoch;
            bar.timestamp = timestamp.empty() ? format_epoch_timestamp(*epoch) : timestamp;
            bar.open = telemetry.get_double(section, "open", 0.0);
            bar.high = telemetry.get_double(section, "high", bar.open);
            bar.low = telemetry.get_double(section, "low", bar.open);
            bar.close = telemetry.get_double(section, "close", bar.open);
            if (bar.open <= 0.0 || bar.high <= 0.0 || bar.low <= 0.0 || bar.close <= 0.0) {
                continue;
            }
            live_chart_bars[instrument].push_back(std::move(bar));
        }

        std::map<std::string, std::vector<LiveChartBar>> normalized;
        merge_live_chart_bars(normalized, live_chart_bars, max_bars_per_instrument);
        return normalized;
    } catch (const std::exception& ex) {
        std::cout << "Unable to seed live chart bars from " << telemetry_path.string() << ": " << ex.what() << '\n';
        return {};
    }
}

void write_live_telemetry_file(
    const std::filesystem::path& config_path,
    const std::map<std::string, LiveAccountState>& accounts,
    const std::map<std::string, LiveAttachmentTelemetry>& attachments,
    const std::map<std::string, std::vector<LiveChartBar>>& live_chart_bars,
    const std::vector<RuntimeChartIndicatorSeriesSnapshot>& live_chart_indicator_series,
    int live_bar_seconds,
    const std::vector<std::string>& warnings) {

    const auto telemetry_path = live_telemetry_path(config_path);
    std::error_code error_code;
    std::filesystem::create_directories(telemetry_path.parent_path(), error_code);

    std::ostringstream output;
    output << "[telemetry]\n";
    output << "mode=live\n";
    output << "updated_at=" << current_timestamp() << "\n\n";

    for (std::size_t index = 0; index < warnings.size(); ++index) {
        output << "[telemetry_warning." << (index + 1) << "]\n";
        output << "text=" << warnings[index] << "\n\n";
    }

    for (const auto& [account_id, account] : accounts) {
        output << "[telemetry_account." << account_id << "]\n";
        output << "initial_cash=" << format_price(account.snapshot.initial_cash) << "\n";
        output << "cash=" << format_price(account.snapshot.cash) << "\n";
        output << "realized_pnl=" << format_price(account.snapshot.realized_pnl) << "\n\n";
        output << "trader_connected=" << (account.trader_connected ? "true" : "false") << "\n";
        output << "market_data_connected=" << (account.market_data_connected ? "true" : "false") << "\n\n";

        std::size_t position_index = 1;
        for (const auto& [instrument, net] : account.snapshot.net_positions) {
            output << "[telemetry_account_position." << account_id << '.' << position_index++ << "]\n";
            output << "instrument=" << instrument << "\n";
            output << "net=" << net << "\n\n";
        }
    }

    output << "[telemetry_chart]\n";
    output << "bar_seconds=" << std::max(live_bar_seconds, 1) << "\n\n";

    for (const auto& [instrument, bars] : live_chart_bars) {
        for (std::size_t bar_index = 0; bar_index < bars.size(); ++bar_index) {
            const auto& bar = bars[bar_index];
            output << "[telemetry_chart_bar." << instrument << '.' << (bar_index + 1) << "]\n";
            output << "instrument=" << instrument << "\n";
            output << "bucket_epoch=" << bar.bucket_epoch << "\n";
            output << "timestamp=" << bar.timestamp << "\n";
            output << "open=" << format_price(bar.open) << "\n";
            output << "high=" << format_price(bar.high) << "\n";
            output << "low=" << format_price(bar.low) << "\n";
            output << "close=" << format_price(bar.close) << "\n\n";
        }
    }

    std::size_t indicator_entry_index = 1;
    for (const auto& series : live_chart_indicator_series) {
        if (series.instrument.empty() || series.indicator_id.empty()) {
            continue;
        }

        for (const auto& point : series.points) {
            if (point.time <= 0 || !std::isfinite(point.value)) {
                continue;
            }

            output << "[telemetry_chart_indicator." << indicator_entry_index++ << "]\n";
            output << "instrument=" << series.instrument << "\n";
            output << "indicator_id=" << series.indicator_id << "\n";
            output << "label=" << (series.label.empty() ? series.indicator_id : series.label) << "\n";
            output << "color=" << series.color << "\n";
            output << "strategy_id=" << series.strategy_id << "\n";
            output << "account_id=" << series.account_id << "\n";
            output << "timestamp=" << format_epoch_timestamp(point.time) << "\n";
            output << "value=" << format_price(point.value) << "\n\n";
        }
    }

    for (const auto& [_, attachment] : attachments) {
        output << "[telemetry_attachment." << attachment.snapshot.strategy_id << '.' << attachment.snapshot.account_id << "]\n";
        output << "strategy_id=" << attachment.snapshot.strategy_id << "\n";
        output << "account_id=" << attachment.snapshot.account_id << "\n\n";

        for (std::size_t warning_index = 0; warning_index < attachment.snapshot.warnings.size(); ++warning_index) {
            output << "[telemetry_attachment_warning." << attachment.snapshot.strategy_id << '.' << attachment.snapshot.account_id << '.' << (warning_index + 1) << "]\n";
            output << "text=" << attachment.snapshot.warnings[warning_index] << "\n\n";
        }

        for (std::size_t position_index = 0; position_index < attachment.snapshot.positions.size(); ++position_index) {
            const auto& position = attachment.snapshot.positions[position_index];
            output << "[telemetry_position." << attachment.snapshot.strategy_id << '.' << attachment.snapshot.account_id << '.' << (position_index + 1) << "]\n";
            output << "instrument=" << position.instrument << "\n";
            output << "long_today_quantity=" << position.long_today_quantity << "\n";
            output << "long_yesterday_quantity=" << position.long_yesterday_quantity << "\n";
            output << "long_quantity=" << position.long_quantity << "\n";
            output << "long_average_price=" << format_price(position.long_average_price) << "\n";
            output << "short_today_quantity=" << position.short_today_quantity << "\n";
            output << "short_yesterday_quantity=" << position.short_yesterday_quantity << "\n";
            output << "short_quantity=" << position.short_quantity << "\n";
            output << "short_average_price=" << format_price(position.short_average_price) << "\n";
            output << "net=" << position.net << "\n";
            output << "average_price=" << format_price(position.average_price) << "\n\n";
        }

        for (std::size_t order_index = 0; order_index < attachment.snapshot.opened_orders.size(); ++order_index) {
            const auto& order = attachment.snapshot.opened_orders[order_index];
            output << '[' << encode_telemetry_order_section("telemetry_opened_order", attachment.snapshot.strategy_id, attachment.snapshot.account_id, order_index + 1) << "]\n";
            output << "order_id=" << order.order_id << "\n";
            output << "source_order_id=" << order.source_order_id << "\n";
            output << "instrument=" << order.instrument << "\n";
            output << "exchange=" << order.exchange << "\n";
            output << "side=" << to_string(order.side) << "\n";
            output << "offset=" << to_string(order.offset) << "\n";
            output << "requested_volume=" << order.requested_volume << "\n";
            output << "filled_volume=" << order.filled_volume << "\n";
            output << "limit_price=" << format_price(order.limit_price) << "\n";
            output << "filled_price=" << format_price(order.filled_price) << "\n";
            output << "signal_time_ms=" << order.signal_time_ms << "\n";
            output << "status=" << to_string(order.status) << "\n";
            output << "message=" << order.message << "\n";
            output << "timestamp=" << order.timestamp << "\n\n";
        }

        for (std::size_t order_index = 0; order_index < attachment.snapshot.closed_orders.size(); ++order_index) {
            const auto& order = attachment.snapshot.closed_orders[order_index];
            output << '[' << encode_telemetry_order_section("telemetry_closed_order", attachment.snapshot.strategy_id, attachment.snapshot.account_id, order_index + 1) << "]\n";
            output << "order_id=" << order.order_id << "\n";
            output << "source_order_id=" << order.source_order_id << "\n";
            output << "instrument=" << order.instrument << "\n";
            output << "exchange=" << order.exchange << "\n";
            output << "side=" << to_string(order.side) << "\n";
            output << "offset=" << to_string(order.offset) << "\n";
            output << "requested_volume=" << order.requested_volume << "\n";
            output << "filled_volume=" << order.filled_volume << "\n";
            output << "limit_price=" << format_price(order.limit_price) << "\n";
            output << "filled_price=" << format_price(order.filled_price) << "\n";
            output << "signal_time_ms=" << order.signal_time_ms << "\n";
            output << "status=" << to_string(order.status) << "\n";
            output << "message=" << order.message << "\n";
            output << "timestamp=" << order.timestamp << "\n\n";
        }
    }

    std::ofstream file(telemetry_path, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        throw std::runtime_error("Unable to write live telemetry file: " + telemetry_path.string());
    }
    file << output.str();
}

OrderStatus order_status_from_text(std::string value) {
    value = lower_copy(trim_copy(value));
    if (value == "accepted") {
        return OrderStatus::Accepted;
    }
    if (value == "partially_filled") {
        return OrderStatus::PartiallyFilled;
    }
    if (value == "filled") {
        return OrderStatus::Filled;
    }
    if (value == "cancelled") {
        return OrderStatus::Cancelled;
    }
    if (value == "rejected") {
        return OrderStatus::Rejected;
    }
    return OrderStatus::Submitted;
}

Side side_from_text(std::string value) {
    value = lower_copy(trim_copy(value));
    return value == "sell" ? Side::Sell : Side::Buy;
}

Offset offset_from_text(std::string value) {
    value = lower_copy(trim_copy(value));
    if (value == "close_yesterday") {
        return Offset::CloseYesterday;
    }
    if (value == "close_today") {
        return Offset::CloseToday;
    }
    if (value == "close") {
        return Offset::Close;
    }
    return Offset::Open;
}

RuntimeSnapshot read_live_telemetry_snapshot(const std::filesystem::path& config_path) {
    const auto telemetry_path = live_telemetry_path(config_path);
    if (!std::filesystem::exists(telemetry_path)) {
        throw std::runtime_error("Live telemetry file not found at " + telemetry_path.string());
    }

    const auto telemetry = IniFile::parse(telemetry_path);
    RuntimeSnapshot snapshot;
    snapshot.mode = Mode::Live;
    snapshot.chart_bar_seconds = std::max(1, telemetry.get_int("telemetry_chart", "bar_seconds", 60));

    std::map<std::string, StrategyAttachmentSnapshot> attachments;
    for (const auto& section : telemetry.sections_with_prefix("telemetry_attachment.")) {
        const auto tail = section.substr(std::string("telemetry_attachment.").size());
        const auto parts = split_tokenized(tail, '.');
        if (parts.size() < 2) {
            continue;
        }
        StrategyAttachmentSnapshot attachment;
        attachment.strategy_id = parts[0];
        attachment.account_id = parts[1];
        attachments.emplace(telemetry_attachment_key(attachment.strategy_id, attachment.account_id), std::move(attachment));
    }

    for (const auto& section : telemetry.sections_with_prefix("telemetry_warning.")) {
        snapshot.warnings.push_back(telemetry.get(section, "text"));
    }

    for (const auto& section : telemetry.sections_with_prefix("telemetry_account.")) {
        const auto account_id = section.substr(std::string("telemetry_account.").size());
        AccountSnapshot account;
        account.account_id = account_id;
        account.initial_cash = telemetry.get_double(section, "initial_cash", 0.0);
        account.cash = telemetry.get_double(section, "cash", account.initial_cash);
        account.realized_pnl = telemetry.get_double(section, "realized_pnl", 0.0);
        snapshot.accounts.push_back(std::move(account));
    }

    for (const auto& section : telemetry.sections_with_prefix("telemetry_account_position.")) {
        const auto tail = section.substr(std::string("telemetry_account_position.").size());
        const auto parts = split_tokenized(tail, '.');
        if (parts.size() < 2) {
            continue;
        }
        const auto account_it = std::find_if(snapshot.accounts.begin(), snapshot.accounts.end(), [&parts](const AccountSnapshot& account) {
            return account.account_id == parts[0];
        });
        if (account_it == snapshot.accounts.end()) {
            continue;
        }
        account_it->net_positions[telemetry.get(section, "instrument")] = telemetry.get_int(section, "net", 0);
    }

    auto append_attachment_warning = [&attachments, &telemetry](const std::string& section) {
        const auto tail = section.substr(std::string("telemetry_attachment_warning.").size());
        const auto parts = split_tokenized(tail, '.');
        if (parts.size() < 3) {
            return;
        }
        const auto key = telemetry_attachment_key(parts[0], parts[1]);
        auto& attachment = attachments[key];
        attachment.strategy_id = parts[0];
        attachment.account_id = parts[1];
        attachment.warnings.push_back(telemetry.get(section, "text"));
    };

    for (const auto& section : telemetry.sections_with_prefix("telemetry_attachment_warning.")) {
        append_attachment_warning(section);
    }

    auto append_position = [&attachments, &telemetry](const std::string& section) {
        const auto tail = section.substr(std::string("telemetry_position.").size());
        const auto parts = split_tokenized(tail, '.');
        if (parts.size() < 3) {
            return;
        }
        const auto key = telemetry_attachment_key(parts[0], parts[1]);
        auto& attachment = attachments[key];
        attachment.strategy_id = parts[0];
        attachment.account_id = parts[1];
        RuntimePositionSnapshot position;
        position.instrument = telemetry.get(section, "instrument");
        position.account_id = parts[1];
        position.strategy_id = parts[0];
        position.long_today_quantity = telemetry.get_int(section, "long_today_quantity", 0);
        position.long_yesterday_quantity = telemetry.get_int(section, "long_yesterday_quantity", 0);
        position.long_quantity = telemetry.get_int(section, "long_quantity", 0);
        position.long_average_price = telemetry.get_double(section, "long_average_price", 0.0);
        position.short_today_quantity = telemetry.get_int(section, "short_today_quantity", 0);
        position.short_yesterday_quantity = telemetry.get_int(section, "short_yesterday_quantity", 0);
        position.short_quantity = telemetry.get_int(section, "short_quantity", 0);
        position.short_average_price = telemetry.get_double(section, "short_average_price", 0.0);
        if (position.long_today_quantity == 0 && position.long_yesterday_quantity == 0 && position.long_quantity > 0) {
            position.long_yesterday_quantity = position.long_quantity;
        }
        if (position.short_today_quantity == 0 && position.short_yesterday_quantity == 0 && position.short_quantity > 0) {
            position.short_yesterday_quantity = position.short_quantity;
        }
        if (position.long_quantity == 0) {
            position.long_quantity = position.long_today_quantity + position.long_yesterday_quantity;
        }
        if (position.short_quantity == 0) {
            position.short_quantity = position.short_today_quantity + position.short_yesterday_quantity;
        }
        if (position.long_quantity == 0 && position.short_quantity == 0) {
            const int legacy_net = telemetry.get_int(section, "net", 0);
            const double legacy_average_price = telemetry.get_double(section, "average_price", 0.0);
            if (legacy_net > 0) {
                position.long_yesterday_quantity = legacy_net;
                position.long_quantity = legacy_net;
                position.long_average_price = legacy_average_price;
            } else if (legacy_net < 0) {
                position.short_yesterday_quantity = -legacy_net;
                position.short_quantity = -legacy_net;
                position.short_average_price = legacy_average_price;
            }
        }
        position.net = position.long_quantity - position.short_quantity;
        position.average_price = position.long_quantity > 0 && position.short_quantity == 0
            ? position.long_average_price
            : (position.short_quantity > 0 && position.long_quantity == 0 ? position.short_average_price : 0.0);
        attachment.positions.push_back(std::move(position));
    };

    for (const auto& section : telemetry.sections_with_prefix("telemetry_position.")) {
        append_position(section);
    }

    auto parse_order = [&telemetry](const std::string& section, const std::string& strategy_id, const std::string& account_id) {
        RuntimeOrderSnapshot order;
        order.order_id = telemetry.get(section, "order_id");
        order.source_order_id = telemetry.get(section, "source_order_id");
        order.account_id = account_id;
        order.strategy_id = strategy_id;
        order.instrument = telemetry.get(section, "instrument");
        order.exchange = telemetry.get(section, "exchange");
        order.side = side_from_text(telemetry.get(section, "side"));
        order.offset = offset_from_text(telemetry.get(section, "offset"));
        order.requested_volume = telemetry.get_int(section, "requested_volume", 0);
        order.filled_volume = telemetry.get_int(section, "filled_volume", 0);
        order.limit_price = telemetry.get_double(section, "limit_price", 0.0);
        order.filled_price = telemetry.get_double(section, "filled_price", 0.0);
        const auto raw_signal_time_ms = trim_copy(telemetry.get(section, "signal_time_ms"));
        if (!raw_signal_time_ms.empty()) {
            try {
                order.signal_time_ms = std::stoll(raw_signal_time_ms);
            } catch (const std::exception&) {
                order.signal_time_ms = 0;
            }
        }
        order.status = order_status_from_text(telemetry.get(section, "status"));
        order.message = telemetry.get(section, "message");
        order.timestamp = telemetry.get(section, "timestamp");
        return order;
    };

    auto append_orders = [&attachments, &parse_order](const IniFile& telemetry_ini, std::string_view prefix, bool opened) {
        for (const auto& section : telemetry_ini.sections_with_prefix(std::string(prefix))) {
            const auto tail = section.substr(std::string(prefix).size());
            const auto parts = split_tokenized(tail, '.');
            if (parts.size() < 3) {
                continue;
            }
            const auto key = telemetry_attachment_key(parts[0], parts[1]);
            auto& attachment = attachments[key];
            attachment.strategy_id = parts[0];
            attachment.account_id = parts[1];
            auto order = parse_order(section, parts[0], parts[1]);
            if (opened) {
                attachment.opened_orders.push_back(std::move(order));
            } else {
                attachment.closed_orders.push_back(std::move(order));
            }
        }
    };

    append_orders(telemetry, "telemetry_opened_order.", true);
    append_orders(telemetry, "telemetry_closed_order.", false);

    for (auto& [_, attachment] : attachments) {
        snapshot.strategy_attachments.push_back(std::move(attachment));
    }

    if (snapshot.strategy_attachments.empty()) {
        snapshot.warnings.push_back("Live telemetry file is present but does not yet contain any strategy attachment snapshots.");
    }

    return snapshot;
}

std::string live_closed_order_history_key(const RuntimeOrderSnapshot& order) {
    return order.order_id + '|' + order.source_order_id + '|' + order.timestamp + '|'
        + order.strategy_id + '|' + order.account_id + '|' + order.instrument + '|'
        + itrader::to_string(order.side) + '|' + itrader::to_string(order.offset);
}

void trim_live_closed_order_history(
    LiveAttachmentTelemetry& telemetry,
    std::size_t max_closed_orders_per_attachment) {

    if (max_closed_orders_per_attachment == 0
        || telemetry.closed_orders.size() <= max_closed_orders_per_attachment) {
        return;
    }

    telemetry.closed_orders.erase(
        telemetry.closed_orders.begin(),
        telemetry.closed_orders.begin()
            + static_cast<std::ptrdiff_t>(telemetry.closed_orders.size() - max_closed_orders_per_attachment));
}

void seed_live_closed_order_history_from_telemetry(
    const std::filesystem::path& config_path,
    std::map<std::string, LiveAttachmentTelemetry>& attachment_telemetry,
    std::size_t max_closed_orders_per_attachment) {

    if (max_closed_orders_per_attachment == 0) {
        return;
    }

    const auto telemetry_path = live_telemetry_path(config_path);
    if (!std::filesystem::exists(telemetry_path)) {
        return;
    }

    RuntimeSnapshot persisted_snapshot;
    try {
        persisted_snapshot = read_live_telemetry_snapshot(config_path);
    } catch (const std::exception& ex) {
        std::cout << "Unable to seed live closed order history from "
                  << telemetry_path.string() << ": " << ex.what() << '\n';
        return;
    }

    for (const auto& persisted_attachment : persisted_snapshot.strategy_attachments) {
        const auto attachment_it = attachment_telemetry.find(
            telemetry_attachment_key(persisted_attachment.strategy_id, persisted_attachment.account_id));
        if (attachment_it == attachment_telemetry.end()) {
            continue;
        }

        auto& telemetry = attachment_it->second;
        std::set<std::string> seen_orders;
        for (const auto& order : telemetry.closed_orders) {
            seen_orders.insert(live_closed_order_history_key(order));
        }

        for (const auto& order : persisted_attachment.closed_orders) {
            if (order.instrument.empty()) {
                continue;
            }
            const auto key = live_closed_order_history_key(order);
            if (!seen_orders.insert(key).second) {
                continue;
            }
            telemetry.closed_orders.push_back(order);
        }

        trim_live_closed_order_history(telemetry, max_closed_orders_per_attachment);
        refresh_attachment_orders(telemetry);
    }
}

std::vector<std::string> read_strategy_accounts(const IniFile& ini, const std::string& section_name) {
    std::vector<std::string> accounts = ini.get_list(section_name, "accounts");
    if (accounts.empty()) {
        accounts = ini.get_list(section_name, "account");
    }

    std::vector<std::string> normalized;
    for (const auto& account : accounts) {
        const auto trimmed = trim_copy(account);
        if (trimmed.empty()) {
            continue;
        }
        if (std::find(normalized.begin(), normalized.end(), trimmed) == normalized.end()) {
            normalized.push_back(trimmed);
        }
    }
    return normalized;
}

std::optional<int> read_strategy_order_ref_strategy_code(const IniFile& ini, const std::string& section_name) {
    const auto raw_value = trim_copy(ini.get(section_name, "order_ref_strategy_code", ini.get(section_name, "strategy_code")));
    if (raw_value.empty()) {
        return std::nullopt;
    }

    try {
        const int strategy_code = std::stoi(raw_value);
        if (strategy_code < 1 || strategy_code > 99) {
            throw std::runtime_error("range");
        }
        return strategy_code;
    } catch (const std::exception&) {
        throw std::runtime_error(
            "Strategy section " + section_name + " must set order_ref_strategy_code to an integer in [1, 99].");
    }
}

bool is_plausible_signal_time_ms(long long signal_time_ms) {
    // Treat anything before 2000-01-01 as invalid for this runtime. Values like
    // 1 can appear when an older strategy DLL and runtime disagree on OrderIntent layout.
    return signal_time_ms >= 946'684'800'000LL;
}

long long normalize_event_signal_time_ms(const OrderRequest& request, const std::string& timestamp) {
    if (is_plausible_signal_time_ms(request.signal_time_ms)) {
        return request.signal_time_ms;
    }
    if (is_plausible_signal_time_ms(request.activate_at_ms)) {
        return request.activate_at_ms;
    }

    const auto timestamp_ms = parse_timestamp_to_millis(timestamp);
    if (timestamp_ms.has_value() && is_plausible_signal_time_ms(*timestamp_ms)) {
        return *timestamp_ms;
    }

    return 0;
}

OrderEvent make_event(const OrderRequest& request, const std::string& order_id, OrderStatus status, int filled_volume, double filled_price, const std::string& message, const std::string& timestamp, const std::string& source_order_id = {}) {
    OrderEvent event;
    event.order_id = order_id;
    event.source_order_id = source_order_id;
    event.client_order_id = request.client_order_id.empty() ? order_id : request.client_order_id;
    event.account_id = request.account_id;
    event.strategy_id = request.strategy_id;
    event.instrument = request.instrument;
    event.exchange = request.exchange;
    event.side = request.side;
    event.offset = request.offset;
    event.requested_volume = request.volume;
    event.filled_volume = filled_volume;
    event.limit_price = request.limit_price;
    event.filled_price = filled_price;
    event.signal_time_ms = normalize_event_signal_time_ms(request, timestamp);
    event.status = status;
    event.message = message;
    event.timestamp = timestamp;
    return event;
}

bool is_terminal_status(OrderStatus status) {
    return status == OrderStatus::Filled || status == OrderStatus::Cancelled || status == OrderStatus::Rejected;
}

itrader::RuntimeOrderSnapshot make_runtime_order_snapshot(const OrderEvent& event) {
    itrader::RuntimeOrderSnapshot snapshot;
    snapshot.order_id = event.order_id;
    snapshot.source_order_id = event.source_order_id;
    snapshot.client_order_id = event.client_order_id;
    snapshot.account_id = event.account_id;
    snapshot.strategy_id = event.strategy_id;
    snapshot.instrument = event.instrument;
    snapshot.exchange = event.exchange;
    snapshot.side = event.side;
    snapshot.offset = event.offset;
    snapshot.requested_volume = event.requested_volume;
    snapshot.filled_volume = event.filled_volume;
    snapshot.limit_price = event.limit_price;
    snapshot.filled_price = event.filled_price;
    snapshot.signal_time_ms = event.signal_time_ms;
    snapshot.status = event.status;
    snapshot.message = event.message;
    snapshot.timestamp = event.timestamp;
    return snapshot;
}

itrader::RuntimeOrderSnapshot make_runtime_order_snapshot(const PendingOrder& pending) {
    itrader::RuntimeOrderSnapshot snapshot;
    snapshot.order_id = pending.order_id;
    snapshot.client_order_id = pending.request.client_order_id.empty() ? pending.order_id : pending.request.client_order_id;
    snapshot.account_id = pending.request.account_id;
    snapshot.strategy_id = pending.request.strategy_id;
    snapshot.instrument = pending.request.instrument;
    snapshot.exchange = pending.request.exchange;
    snapshot.side = pending.request.side;
    snapshot.offset = pending.request.offset;
    snapshot.requested_volume = pending.request.volume;
    snapshot.filled_volume = pending.filled_volume;
    snapshot.limit_price = pending.request.limit_price;
    snapshot.signal_time_ms = pending.request.signal_time_ms;
    snapshot.status = pending.filled_volume > 0
        ? OrderStatus::PartiallyFilled
        : (pending.request.activate_at_ms > 0 ? OrderStatus::Submitted : OrderStatus::Accepted);
    snapshot.message = pending.filled_volume > 0
        ? "partially filled resting limit order"
        : (pending.request.activate_at_ms > 0
            ? "scheduled order awaiting activation"
            : (pending.request.price_type == PriceType::Limit ? "resting limit order" : "pending order"));
    return snapshot;
}

std::vector<MarketTick> load_ticks_from_csv_filtered(
    const std::filesystem::path& file_path,
    const std::set<std::string>* instrument_filter,
    const std::string& trading_day_override = {}) {

    std::ifstream input(file_path);
    if (!input.is_open()) {
        throw std::runtime_error("Unable to open backtest CSV: " + file_path.string());
    }

    std::vector<MarketTick> ticks;
    std::string line;
    while (std::getline(input, line)) {
        const auto trimmed = trim_copy(line);
        if (trimmed.empty() || trimmed.starts_with('#')) {
            continue;
        }

        const auto fields = split_csv(trimmed);
        if (fields.empty()) {
            continue;
        }

        const auto first_column = lower_copy(fields[0]);
        if (first_column == "timestamp" || first_column == "time") {
            continue;
        }

        MarketTick tick;
        tick.trading_day = trading_day_override;
        if (fields.size() < 12 || fields[1].find('.') == std::string::npos) {
            throw std::runtime_error(
                "Backtest CSV must use AGTICK format "
                "(time,symbol,current,high,low,volume,money,position,a1_v,a1_p,b1_v,b1_p[,upper_limit_price,lower_limit_price]): "
                + file_path.string());
        }

        tick.timestamp = fields[0];
        tick.timestamp_ms = parse_timestamp_to_millis(tick.timestamp).value_or(0);
        const auto [parsed_instrument, parsed_exchange] = split_symbol_and_exchange(fields[1]);
        const auto canonical_instrument = canonical_instrument_for_filter(parsed_instrument, instrument_filter);
        if (instrument_filter != nullptr && !instrument_filter->empty() && canonical_instrument.empty()) {
            continue;
        }

        tick.instrument = canonical_instrument.empty() ? parsed_instrument : canonical_instrument;
        tick.exchange = parsed_exchange;
        tick.last = std::stod(fields[2]);
        tick.volume = static_cast<int>(std::llround(std::stod(fields[5])));
        tick.turnover = std::stod(fields[6]);
        tick.ask_size = static_cast<int>(std::llround(std::stod(fields[8])));
        tick.ask = std::stod(fields[9]);
        tick.bid_size = static_cast<int>(std::llround(std::stod(fields[10])));
        tick.bid = std::stod(fields[11]);
        if (fields.size() > 12 && !fields[12].empty()) {
            tick.upper_limit_price = std::stod(fields[12]);
        }
        if (fields.size() > 13 && !fields[13].empty()) {
            tick.lower_limit_price = std::stod(fields[13]);
        }
        if (tick.ask <= 0.0 || tick.bid <= 0.0 || tick.ask_size <= 0 || tick.bid_size <= 0) {
            continue;
        }
        ticks.push_back(std::move(tick));
    }

    return ticks;
}

std::vector<MarketTick> load_ticks_from_csv(const std::filesystem::path& file_path) {
    return load_ticks_from_csv_filtered(file_path, nullptr, trading_day_label_from_file_path(file_path));
}

std::set<std::string> collect_backtest_instruments(const IniFile& ini, const std::vector<std::string>& strategy_sections) {
    std::set<std::string> instruments;
    for (const auto& section : strategy_sections) {
        for (const auto& instrument : ini.get_list(section, "instruments")) {
            const auto trimmed = trim_copy(instrument);
            if (!trimmed.empty()) {
                instruments.insert(trimmed);
            }
        }
    }
    return instruments;
}

void sort_ticks_chronologically(std::vector<MarketTick>& ticks) {
    std::stable_sort(ticks.begin(), ticks.end(), [](const MarketTick& left, const MarketTick& right) {
        if (left.timestamp_ms != 0 && right.timestamp_ms != 0 && left.timestamp_ms != right.timestamp_ms) {
            return left.timestamp_ms < right.timestamp_ms;
        }
        const auto left_epoch = left.timestamp_ms != 0 ? std::optional<long long>(left.timestamp_ms / 1000LL) : parse_timestamp_to_epoch(left.timestamp);
        const auto right_epoch = right.timestamp_ms != 0 ? std::optional<long long>(right.timestamp_ms / 1000LL) : parse_timestamp_to_epoch(right.timestamp);
        if (left_epoch.has_value() && right_epoch.has_value() && left_epoch != right_epoch) {
            return *left_epoch < *right_epoch;
        }
        if (left.timestamp != right.timestamp) {
            return left.timestamp < right.timestamp;
        }
        if (left.instrument != right.instrument) {
            return left.instrument < right.instrument;
        }
        return left.exchange < right.exchange;
    });
}

std::vector<MarketTick> load_ticks_from_directory(const std::filesystem::path& directory_path, const std::set<std::string>& instrument_filter) {
    if (!std::filesystem::exists(directory_path)) {
        throw std::runtime_error("Backtest data directory does not exist: " + directory_path.string());
    }
    if (!std::filesystem::is_directory(directory_path)) {
        throw std::runtime_error("Backtest data path is not a directory: " + directory_path.string());
    }

    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(directory_path, std::filesystem::directory_options::skip_permission_denied)) {
        if (entry.is_regular_file() && lower_copy(entry.path().extension().string()) == ".csv") {
            files.push_back(entry.path());
        }
    }

    std::sort(files.begin(), files.end());

    std::vector<MarketTick> ticks;
    for (const auto& file_path : files) {
        auto file_ticks = load_ticks_from_csv_filtered(file_path, &instrument_filter, trading_day_label_from_file_path(file_path));
        ticks.insert(ticks.end(), std::make_move_iterator(file_ticks.begin()), std::make_move_iterator(file_ticks.end()));
    }

    sort_ticks_chronologically(ticks);
    return ticks;
}

std::set<std::string> collect_strategy_instruments(const IniFile& ini, const std::string& section_name) {
    std::set<std::string> instruments;
    for (const auto& instrument : ini.get_list(section_name, "instruments")) {
        const auto trimmed = trim_copy(instrument);
        if (!trimmed.empty()) {
            instruments.insert(trimmed);
        }
    }
    return instruments;
}

void dedupe_ticks(std::vector<MarketTick>& ticks) {
    ticks.erase(std::unique(ticks.begin(), ticks.end(), [](const MarketTick& left, const MarketTick& right) {
        return left.trading_day == right.trading_day
            && left.timestamp == right.timestamp
            && left.instrument == right.instrument
            && left.exchange == right.exchange
            && left.last == right.last
            && left.bid == right.bid
            && left.ask == right.ask
            && left.volume == right.volume
            && left.turnover == right.turnover
            && left.bid_size == right.bid_size
            && left.ask_size == right.ask_size;
    }), ticks.end());
}

std::vector<MarketTick> load_backtest_ticks(
    const std::filesystem::path& base_dir,
    const IniFile& ini,
    const std::vector<std::string>& strategy_sections) {

    std::vector<MarketTick> ticks;
    std::set<std::string> default_instruments;
    const auto global_data_dir = trim_copy(ini.get("backtest", "data_dir"));
    const auto csv_value = trim_copy(ini.get("backtest", "csv"));

    bool loaded_strategy_specific_source = false;
    for (const auto& section_name : strategy_sections) {
        const auto instruments = collect_strategy_instruments(ini, section_name);
        if (instruments.empty()) {
            continue;
        }

        const auto strategy_data_dir = trim_copy(ini.get(section_name, "backtest_data_dir"));
        if (strategy_data_dir.empty()) {
            default_instruments.insert(instruments.begin(), instruments.end());
            continue;
        }

        const auto strategy_data_dir_path = resolve_path(base_dir, strategy_data_dir);
        auto strategy_ticks = load_ticks_from_directory(strategy_data_dir_path, instruments);
        if (strategy_ticks.empty()) {
            throw std::runtime_error(
                "Strategy section " + section_name + " backtest_data_dir did not yield any tick rows for configured instruments: "
                + join_csv(std::vector<std::string>(instruments.begin(), instruments.end()))
                + " in " + strategy_data_dir_path.string());
        }
        loaded_strategy_specific_source = true;
        ticks.insert(ticks.end(), std::make_move_iterator(strategy_ticks.begin()), std::make_move_iterator(strategy_ticks.end()));
    }

    if (!default_instruments.empty() || !loaded_strategy_specific_source) {
        if (!global_data_dir.empty()) {
            const auto data_dir_path = resolve_path(base_dir, global_data_dir);
            auto shared_ticks = load_ticks_from_directory(data_dir_path, default_instruments);
            if (shared_ticks.empty()) {
                throw std::runtime_error(
                    "Backtest data directory did not yield any tick rows for configured instruments: "
                    + (default_instruments.empty() ? std::string("<all>") : join_csv(std::vector<std::string>(default_instruments.begin(), default_instruments.end())))
                    + " in " + data_dir_path.string());
            }
            ticks.insert(ticks.end(), std::make_move_iterator(shared_ticks.begin()), std::make_move_iterator(shared_ticks.end()));
        } else {
            if (csv_value.empty()) {
                throw std::runtime_error("[backtest] must set either data_dir=<AGTICK folder> or csv=<AGTICK file>, or each strategy must set backtest_data_dir=<AGTICK folder>");
            }

            const auto csv_path = resolve_path(base_dir, csv_value);
            auto shared_ticks = load_ticks_from_csv_filtered(
                csv_path,
                default_instruments.empty() ? nullptr : &default_instruments,
                trading_day_label_from_file_path(csv_path));
            if (shared_ticks.empty()) {
                throw std::runtime_error("Backtest data file did not yield any tick rows: " + csv_path.string());
            }
            ticks.insert(ticks.end(), std::make_move_iterator(shared_ticks.begin()), std::make_move_iterator(shared_ticks.end()));
        }
    }

    sort_ticks_chronologically(ticks);
    dedupe_ticks(ticks);
    if (ticks.empty()) {
        throw std::runtime_error("Backtest data source did not yield any tick rows after merging strategy-specific and shared sources.");
    }

    return ticks;
}

double fast_parse_double(const char* text, int length) {
    if (length <= 0) {
        return 0.0;
    }

    bool negative = false;
    int index = 0;
    if (text[0] == '-') {
        negative = true;
        index = 1;
    }

    double integer_part = 0.0;
    double fractional_part = 0.0;
    double divisor = 1.0;
    bool seen_decimal = false;
    for (; index < length; ++index) {
        const char ch = text[index];
        if (ch == '.') {
            seen_decimal = true;
            continue;
        }
        if (ch < '0' || ch > '9') {
            break;
        }
        if (!seen_decimal) {
            integer_part = integer_part * 10.0 + static_cast<double>(ch - '0');
        } else {
            divisor *= 10.0;
            fractional_part += static_cast<double>(ch - '0') / divisor;
        }
    }

    const double value = integer_part + fractional_part;
    return negative ? -value : value;
}

struct BacktestSourceFile {
    std::filesystem::path path;
    std::string trading_day;
    std::set<std::string> instrument_filter;
    std::size_t source_index {0};
};

struct BacktestSourcePlan {
    std::vector<BacktestSourceFile> directory_files;
    std::optional<std::filesystem::path> csv_path;
    std::set<std::string> csv_instrument_filter;
    std::set<std::string> allowed_trading_days;
};

struct StrategyWarmupConfig {
    bool enabled {false};
    std::optional<std::filesystem::path> data_dir;
    std::optional<std::filesystem::path> csv_path;
    std::string trading_day;
};

std::vector<std::filesystem::path> enumerate_directory_files_sorted(const std::filesystem::path& directory_path) {
    if (!std::filesystem::exists(directory_path)) {
        throw std::runtime_error("Backtest data directory does not exist: " + directory_path.string());
    }
    if (!std::filesystem::is_directory(directory_path)) {
        throw std::runtime_error("Backtest data path is not a directory: " + directory_path.string());
    }

    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(directory_path, std::filesystem::directory_options::skip_permission_denied)) {
        if (entry.is_regular_file() && lower_copy(entry.path().extension().string()) == ".csv") {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

BacktestSourcePlan collect_backtest_source_plan(
    const std::filesystem::path& base_dir,
    const IniFile& ini,
    const std::vector<std::string>& strategy_sections) {

    BacktestSourcePlan plan;
    std::set<std::string> default_instruments;
    const auto global_data_dir = trim_copy(ini.get("backtest", "data_dir"));
    const auto csv_value = trim_copy(ini.get("backtest", "csv"));
    bool loaded_strategy_specific_source = false;
    std::size_t next_source_index = 0;

    for (const auto& section_name : strategy_sections) {
        const auto instruments = collect_strategy_instruments(ini, section_name);
        if (instruments.empty()) {
            continue;
        }

        const auto strategy_data_dir = trim_copy(ini.get(section_name, "backtest_data_dir"));
        if (strategy_data_dir.empty()) {
            default_instruments.insert(instruments.begin(), instruments.end());
            continue;
        }

        const auto strategy_data_dir_path = resolve_path(base_dir, strategy_data_dir);
        const auto files = enumerate_directory_files_sorted(strategy_data_dir_path);
        if (files.empty()) {
            throw std::runtime_error(
                "Strategy section " + section_name + " backtest_data_dir did not contain any files: "
                + strategy_data_dir_path.string());
        }

        for (const auto& file_path : files) {
            plan.directory_files.push_back(BacktestSourceFile {
                .path = file_path,
                .trading_day = trading_day_label_from_file_path(file_path),
                .instrument_filter = instruments,
                .source_index = next_source_index++,
            });
        }
        loaded_strategy_specific_source = true;
    }

    if (!default_instruments.empty() || !loaded_strategy_specific_source) {
        if (!global_data_dir.empty()) {
            const auto data_dir_path = resolve_path(base_dir, global_data_dir);
            const auto files = enumerate_directory_files_sorted(data_dir_path);
            if (files.empty()) {
                throw std::runtime_error("Backtest data directory did not contain any files: " + data_dir_path.string());
            }
            for (const auto& file_path : files) {
                plan.directory_files.push_back(BacktestSourceFile {
                    .path = file_path,
                    .trading_day = trading_day_label_from_file_path(file_path),
                    .instrument_filter = default_instruments,
                    .source_index = next_source_index++,
                });
            }
        } else {
            if (csv_value.empty()) {
                throw std::runtime_error("[backtest] must set either data_dir=<AGTICK folder> or csv=<AGTICK file>, or each strategy must set backtest_data_dir=<AGTICK folder>");
            }
            plan.csv_path = resolve_path(base_dir, csv_value);
            plan.csv_instrument_filter = default_instruments;
        }
    }

    std::stable_sort(plan.directory_files.begin(), plan.directory_files.end(), [](const BacktestSourceFile& left, const BacktestSourceFile& right) {
        if (left.trading_day != right.trading_day) {
            return left.trading_day < right.trading_day;
        }
        return left.source_index < right.source_index;
    });

    return plan;
}

class BacktestCsvReader {
public:
    explicit BacktestCsvReader(BacktestSourceFile source)
        : source_(std::move(source))
        , input_(source_.path) {

        if (!input_.is_open()) {
            throw std::runtime_error("Unable to open backtest CSV: " + source_.path.string());
        }
    }

    bool advance() {
        MarketTick next_tick;
        while (read_next_tick(next_tick)) {
            current_tick_ = std::move(next_tick);
            ++row_sequence_;
            return true;
        }
        return false;
    }

    [[nodiscard]] const MarketTick& current_tick() const {
        return current_tick_;
    }

    MarketTick take_current_tick() {
        return std::move(current_tick_);
    }

    [[nodiscard]] std::size_t source_index() const {
        return source_.source_index;
    }

    [[nodiscard]] std::size_t row_sequence() const {
        return row_sequence_;
    }

private:
    struct SymbolCacheEntry {
        std::string raw_symbol;
        std::string instrument;
        std::string exchange;
        bool allowed {false};
    };

    const SymbolCacheEntry* find_symbol_cache(std::string_view raw_symbol) const {
        for (const auto& entry : symbol_cache_) {
            if (entry.raw_symbol.size() == raw_symbol.size()
                && std::equal(entry.raw_symbol.begin(), entry.raw_symbol.end(), raw_symbol.begin(), raw_symbol.end())) {
                return &entry;
            }
        }
        return nullptr;
    }

    const SymbolCacheEntry& cache_symbol(std::string_view raw_symbol) {
        SymbolCacheEntry entry;
        entry.raw_symbol.assign(raw_symbol);
        const auto [parsed_instrument, parsed_exchange] = split_symbol_and_exchange(raw_symbol);
        const auto canonical_instrument = canonical_instrument_for_filter(
            parsed_instrument,
            source_.instrument_filter.empty() ? nullptr : &source_.instrument_filter);
        entry.allowed = source_.instrument_filter.empty() || !canonical_instrument.empty();
        if (entry.allowed) {
            entry.instrument = canonical_instrument.empty() ? parsed_instrument : canonical_instrument;
            entry.exchange = parsed_exchange;
        }

        symbol_cache_.push_back(std::move(entry));
        return symbol_cache_.back();
    }

    bool read_next_tick(MarketTick& tick) {
        std::string line;
        while (std::getline(input_, line)) {
            if (line.empty()) {
                continue;
            }
            if (line[0] == '#' || line[0] == ';') {
                continue;
            }

            const char* raw = line.c_str();
            const int length = static_cast<int>(line.size());
            int starts[16] {};
            int lengths[16] {};
            int field_count = 0;
            int field_start = 0;
            for (int index = 0; index <= length && field_count < 16; ++index) {
                if (index == length || raw[index] == ',') {
                    starts[field_count] = field_start;
                    lengths[field_count] = index - field_start;
                    ++field_count;
                    field_start = index + 1;
                }
            }

            if (field_count == 0) {
                continue;
            }

            const std::string_view first_field(raw + starts[0], static_cast<std::size_t>(lengths[0]));
            if (lower_copy(std::string(first_field)) == "timestamp" || lower_copy(std::string(first_field)) == "time") {
                continue;
            }

            tick = MarketTick {};
            tick.trading_day = source_.trading_day;

            if (field_count >= 12) {
                const std::string_view symbol_field(raw + starts[1], static_cast<std::size_t>(lengths[1]));
                if (symbol_field.find('.') != std::string_view::npos) {
                    const std::string_view timestamp_field(raw + starts[0], static_cast<std::size_t>(lengths[0]));
                    tick.timestamp.assign(timestamp_field);
                    tick.timestamp_ms = parse_timestamp_to_millis(timestamp_field).value_or(0);

                    const auto* cached_symbol = find_symbol_cache(symbol_field);
                    if (cached_symbol == nullptr) {
                        cached_symbol = &cache_symbol(symbol_field);
                    }
                    if (!cached_symbol->allowed) {
                        continue;
                    }

                    tick.instrument = cached_symbol->instrument;
                    tick.exchange = cached_symbol->exchange;
                    tick.last = fast_parse_double(raw + starts[2], lengths[2]);
                    tick.volume = static_cast<int>(std::llround(fast_parse_double(raw + starts[5], lengths[5])));
                    tick.turnover = fast_parse_double(raw + starts[6], lengths[6]);
                    tick.ask_size = static_cast<int>(std::llround(fast_parse_double(raw + starts[8], lengths[8])));
                    tick.ask = fast_parse_double(raw + starts[9], lengths[9]);
                    tick.bid_size = static_cast<int>(std::llround(fast_parse_double(raw + starts[10], lengths[10])));
                    tick.bid = fast_parse_double(raw + starts[11], lengths[11]);
                    if (field_count > 12 && lengths[12] > 0) {
                        tick.upper_limit_price = fast_parse_double(raw + starts[12], lengths[12]);
                    }
                    if (field_count > 13 && lengths[13] > 0) {
                        tick.lower_limit_price = fast_parse_double(raw + starts[13], lengths[13]);
                    }
                    if (tick.ask <= 0.0 || tick.bid <= 0.0 || tick.ask_size <= 0 || tick.bid_size <= 0) {
                        continue;
                    }
                    return true;
                }
            }

            if (field_count < 7) {
                continue;
            }

            const std::string_view timestamp_field(raw + starts[0], static_cast<std::size_t>(lengths[0]));
            tick.timestamp.assign(timestamp_field);
            tick.timestamp_ms = parse_timestamp_to_millis(timestamp_field).value_or(0);
            {
                const std::string_view instrument_field(raw + starts[1], static_cast<std::size_t>(lengths[1]));
                const auto canonical_instrument = canonical_instrument_for_filter(instrument_field, source_.instrument_filter.empty() ? nullptr : &source_.instrument_filter);
                if (!source_.instrument_filter.empty() && canonical_instrument.empty()) {
                    continue;
                }
                tick.instrument = canonical_instrument.empty() ? trim_copy(instrument_field) : canonical_instrument;
            }
            tick.exchange = normalize_exchange_code(std::string_view(raw + starts[2], static_cast<std::size_t>(lengths[2])));
            tick.last = fast_parse_double(raw + starts[3], lengths[3]);
            tick.bid = fast_parse_double(raw + starts[4], lengths[4]);
            tick.ask = fast_parse_double(raw + starts[5], lengths[5]);
            tick.volume = static_cast<int>(std::llround(fast_parse_double(raw + starts[6], lengths[6])));
            if (field_count > 7) {
                tick.turnover = fast_parse_double(raw + starts[7], lengths[7]);
            }
            if (field_count > 8) {
                tick.bid_size = static_cast<int>(std::llround(fast_parse_double(raw + starts[8], lengths[8])));
            }
            if (field_count > 9) {
                tick.ask_size = static_cast<int>(std::llround(fast_parse_double(raw + starts[9], lengths[9])));
            }
            return true;
        }

        return false;
    }

    BacktestSourceFile source_;
    std::ifstream input_;
    MarketTick current_tick_;
    std::size_t row_sequence_ {0};
    std::vector<SymbolCacheEntry> symbol_cache_;
};

bool identical_market_ticks(const MarketTick& left, const MarketTick& right) {
    return left.trading_day == right.trading_day
        && left.timestamp == right.timestamp
        && left.instrument == right.instrument
        && left.exchange == right.exchange
        && left.timestamp_ms == right.timestamp_ms
        && left.last == right.last
        && left.bid == right.bid
        && left.ask == right.ask
        && left.volume == right.volume
        && left.turnover == right.turnover
        && left.bid_size == right.bid_size
        && left.ask_size == right.ask_size;
}

class BacktestDirectoryTickStream {
public:
    explicit BacktestDirectoryTickStream(BacktestSourcePlan plan)
        : plan_(std::move(plan))
        , heap_compare_ {&readers_}
        , heap_(heap_compare_) {
    }

    bool next(MarketTick& tick) {
        while (true) {
            if (heap_.empty()) {
                if (!load_next_group()) {
                    return false;
                }
            }

            const std::size_t reader_index = heap_.top();
            heap_.pop();

            auto candidate = readers_[reader_index].take_current_tick();
            if (readers_[reader_index].advance()) {
                heap_.push(reader_index);
            }

            if (last_emitted_.has_value() && identical_market_ticks(*last_emitted_, candidate)) {
                continue;
            }

            last_emitted_ = candidate;
            tick = std::move(candidate);
            emitted_any_ = true;
            return true;
        }
    }

    [[nodiscard]] bool emitted_any() const {
        return emitted_any_;
    }

    [[nodiscard]] std::size_t loaded_source_files() const {
        return next_source_index_;
    }

    [[nodiscard]] std::size_t total_source_files() const {
        return plan_.directory_files.size();
    }

private:
    struct HeapCompare {
        const std::vector<BacktestCsvReader>* readers {nullptr};

        bool operator()(std::size_t left_index, std::size_t right_index) const {
            const auto& left_reader = (*readers)[left_index];
            const auto& right_reader = (*readers)[right_index];
            const auto& left = left_reader.current_tick();
            const auto& right = right_reader.current_tick();

            if (left.timestamp_ms != right.timestamp_ms) {
                return left.timestamp_ms > right.timestamp_ms;
            }
            if (left.timestamp != right.timestamp) {
                return left.timestamp > right.timestamp;
            }
            if (left.instrument != right.instrument) {
                return left.instrument > right.instrument;
            }
            if (left.exchange != right.exchange) {
                return left.exchange > right.exchange;
            }
            if (left_reader.source_index() != right_reader.source_index()) {
                return left_reader.source_index() > right_reader.source_index();
            }
            return left_reader.row_sequence() > right_reader.row_sequence();
        }
    };

    bool load_next_group() {
        while (next_source_index_ < plan_.directory_files.size()) {
            readers_.clear();
            while (!heap_.empty()) {
                heap_.pop();
            }

            const std::string group_trading_day = plan_.directory_files[next_source_index_].trading_day;
            while (next_source_index_ < plan_.directory_files.size()
                && plan_.directory_files[next_source_index_].trading_day == group_trading_day) {

                readers_.push_back(BacktestCsvReader(plan_.directory_files[next_source_index_]));
                if (readers_.back().advance()) {
                    heap_.push(readers_.size() - 1);
                }
                ++next_source_index_;
            }

            if (!heap_.empty()) {
                return true;
            }
        }

        return false;
    }

    BacktestSourcePlan plan_;
    std::vector<BacktestCsvReader> readers_;
    HeapCompare heap_compare_;
    std::priority_queue<std::size_t, std::vector<std::size_t>, HeapCompare> heap_;
    std::size_t next_source_index_ {0};
    bool emitted_any_ {false};
    std::optional<MarketTick> last_emitted_;
};

void report_runtime_snapshot_progress(
    const RuntimeSnapshotBuildOptions& options,
    const std::string& phase,
    std::size_t processed_files,
    std::size_t total_files,
    std::size_t processed_ticks) {

    if (!options.on_progress) {
        return;
    }

    options.on_progress(RuntimeSnapshotProgress {
        .phase = phase,
        .processed_files = processed_files,
        .total_files = total_files,
        .processed_ticks = processed_ticks,
    });
}

void throw_if_runtime_snapshot_cancelled(const RuntimeSnapshotBuildOptions& options) {
    if (options.cancel_requested != nullptr && options.cancel_requested->load()) {
        throw std::runtime_error("Backtest replay cancelled by request.");
    }
}

RuntimeChartInstrumentSnapshot& ensure_runtime_snapshot_chart_instrument(RuntimeSnapshot& snapshot, std::string_view instrument) {
    auto chart_it = std::find_if(snapshot.chart_instruments.begin(), snapshot.chart_instruments.end(), [instrument](const RuntimeChartInstrumentSnapshot& entry) {
        return entry.instrument == instrument;
    });
    if (chart_it == snapshot.chart_instruments.end()) {
        snapshot.chart_instruments.push_back(RuntimeChartInstrumentSnapshot {.instrument = std::string(instrument)});
        chart_it = std::prev(snapshot.chart_instruments.end());
    }
    return *chart_it;
}

void update_runtime_snapshot_chart(RuntimeSnapshot& snapshot, const MarketTick& tick, int chart_bar_seconds) {
    if (tick.instrument.empty() || tick.last <= 0.0) {
        return;
    }

    const auto aligned_bucket_epoch = session_aligned_chart_bucket_epoch(tick, chart_bar_seconds);
    if (!aligned_bucket_epoch.has_value()) {
        return;
    }
    const auto tick_time = *aligned_bucket_epoch;

    auto& bars = ensure_runtime_snapshot_chart_instrument(snapshot, tick.instrument).bars;
    if (!bars.empty() && bars.back().time == tick_time) {
        auto& bar = bars.back();
        bar.high = std::max({bar.high, tick.last, tick.ask > 0.0 ? tick.ask : tick.last});
        bar.low = std::min({bar.low, tick.last, tick.bid > 0.0 ? tick.bid : tick.last});
        bar.close = tick.last;
        return;
    }

    RuntimeChartBarSnapshot bar;
    bar.time = tick_time;
    bar.open = bars.empty() ? tick.last : bars.back().close;
    bar.high = std::max({bar.open, tick.last, tick.ask > 0.0 ? tick.ask : tick.last});
    bar.low = std::min({bar.open, tick.last, tick.bid > 0.0 ? tick.bid : tick.last});
    bar.close = tick.last;
    bars.push_back(bar);
}

class StrategyPlugin {
public:
    StrategyPlugin() = default;

    explicit StrategyPlugin(const std::filesystem::path& dll_path) {
        load(dll_path);
    }

    StrategyPlugin(const StrategyPlugin&) = delete;
    StrategyPlugin& operator=(const StrategyPlugin&) = delete;

    StrategyPlugin(StrategyPlugin&& other) noexcept {
        *this = std::move(other);
    }

    StrategyPlugin& operator=(StrategyPlugin&& other) noexcept {
        if (this == &other) {
            return *this;
        }

        unload();
        dll_path_ = std::move(other.dll_path_);
        module_ = other.module_;
        create_ = other.create_;
        destroy_ = other.destroy_;
        other.module_ = nullptr;
        other.create_ = nullptr;
        other.destroy_ = nullptr;
        return *this;
    }

    ~StrategyPlugin() {
        unload();
    }

    std::unique_ptr<IStrategy, std::function<void(IStrategy*)>> create_instance() const {
        if (create_ == nullptr || destroy_ == nullptr) {
            throw std::runtime_error("Strategy factory symbols are not loaded for " + dll_path_.string());
        }

        IStrategy* strategy = create_();
        if (strategy == nullptr) {
            throw std::runtime_error("CreateStrategy returned null for " + dll_path_.string());
        }

        return std::unique_ptr<IStrategy, std::function<void(IStrategy*)>>(strategy, [destroy = destroy_](IStrategy* ptr) {
            if (ptr != nullptr) {
                destroy(ptr);
            }
        });
    }

private:
    void load(const std::filesystem::path& dll_path) {
        const auto host_path = current_process_path();
        dll_path_ = resolve_strategy_dll_for_host_build(dll_path, host_path);
        validate_strategy_binary_compatibility(host_path, dll_path_);
        module_ = LoadLibraryExW(dll_path_.wstring().c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (module_ == nullptr) {
            throw std::runtime_error("Unable to load strategy DLL: " + dll_path_.string() + " (" + last_windows_error() + ")");
        }

        create_ = reinterpret_cast<CreateStrategyFn>(GetProcAddress(module_, kCreateStrategySymbol));
        destroy_ = reinterpret_cast<DestroyStrategyFn>(GetProcAddress(module_, kDestroyStrategySymbol));
        if (create_ == nullptr || destroy_ == nullptr) {
            unload();
            throw std::runtime_error("Strategy DLL is missing CreateStrategy/DestroyStrategy exports: " + dll_path_.string());
        }
    }

    void unload() {
        if (module_ != nullptr) {
            FreeLibrary(module_);
            module_ = nullptr;
        }
    }

    std::filesystem::path dll_path_;
    HMODULE module_ {nullptr};
    CreateStrategyFn create_ {nullptr};
    DestroyStrategyFn destroy_ {nullptr};
};

class StrategyContextImpl final : public IStrategyContext {
public:
    StrategyContextImpl(
        std::string strategy_id,
        std::string account_id,
        Mode mode,
        int strategy_order_ref_code,
        std::function<bool(const OrderRequest&)> submit_order,
        std::function<bool(const std::string&)> cancel_order,
        std::function<std::vector<RuntimeOrderSnapshot>(const std::string&)> open_orders_lookup,
        std::function<int(const std::string&)> position_lookup)
        : strategy_id_(std::move(strategy_id))
        , account_id_(std::move(account_id))
        , mode_(mode)
        , strategy_order_ref_code_(strategy_order_ref_code)
        , submit_order_(std::move(submit_order))
        , cancel_order_(std::move(cancel_order))
        , open_orders_lookup_(std::move(open_orders_lookup))
        , position_lookup_(std::move(position_lookup)) {
    }

    void set_order_flow_enabled(bool enabled) {
        order_flow_enabled_ = enabled;
    }

    void set_max_chart_series_points(std::size_t max_points) {
        max_chart_series_points_ = max_points;
        for (auto& [_, series] : chart_indicator_series_) {
            trim_chart_indicator_series(series);
        }
    }

    [[nodiscard]] bool order_flow_enabled() const {
        return order_flow_enabled_;
    }

    void log(const std::string& message) override {
        std::cout << '[' << strategy_id_ << "] " << message << '\n';
    }

    [[nodiscard]] std::string account_id() const override {
        return account_id_;
    }

    [[nodiscard]] Mode mode() const override {
        return mode_;
    }

    [[nodiscard]] bool submit_intent(const OrderIntent& intent) override {
        if (!order_flow_enabled_) {
            return false;
        }

        OrderRequest request;
        request.account_id = account_id_;
        request.strategy_id = strategy_id_;
        request.client_order_id = intent.client_order_id.empty() ? next_client_order_id() : intent.client_order_id;
        request.strategy_order_ref_code = strategy_order_ref_code_;
        request.instrument = intent.instrument;
        request.exchange = intent.exchange;
        request.side = intent.side;
        request.offset = intent.offset;
        request.price_type = intent.price_type;
        request.immediate_or_cancel = intent.immediate_or_cancel;
        request.limit_price = intent.limit_price;
        request.volume = intent.volume;
        request.activate_at_ms = intent.activate_at_ms;
        request.signal_time_ms = intent.signal_time_ms;
        if (mode_ == Mode::Backtest && intent.execution_policy == IntentExecutionPolicy::RuntimeSyntheticFill) {
            request.backtest_force_fill = true;
            request.backtest_fill_price = intent.expected_fill_price > 0.0 ? intent.expected_fill_price : intent.limit_price;
        }
        return send_order(request);
    }

    [[nodiscard]] bool cancel_order(const std::string& client_order_id) override {
        if (!order_flow_enabled_) {
            return false;
        }
        if (client_order_id.empty()) {
            return false;
        }
        return cancel_order_(client_order_id);
    }

    [[nodiscard]] bool send_order(const OrderRequest& request) override {
        if (!order_flow_enabled_) {
            return false;
        }

        OrderRequest normalized = request;
        if (normalized.account_id.empty()) {
            normalized.account_id = account_id_;
        }
        normalized.strategy_id = strategy_id_;
        if (normalized.client_order_id.empty()) {
            normalized.client_order_id = next_client_order_id();
        }
        normalized.strategy_order_ref_code = strategy_order_ref_code_;
        if (normalized.volume <= 0 || normalized.instrument.empty()) {
            return false;
        }
        return submit_order_(normalized);
    }

    [[nodiscard]] std::vector<RuntimeOrderSnapshot> open_orders(const std::string& instrument) const override {
        return open_orders_lookup_(instrument);
    }

    [[nodiscard]] int net_position(const std::string& instrument) const override {
        return position_lookup_(instrument);
    }

    void plot_indicator(
        const std::string& instrument,
        const std::string& indicator_id,
        long long timestamp_ms,
        double value,
        const std::string& label,
        const std::string& color) override {

        const auto normalized_instrument = trim_copy(instrument);
        const auto normalized_indicator_id = trim_copy(indicator_id);
        if (normalized_instrument.empty() || normalized_indicator_id.empty() || timestamp_ms <= 0 || !std::isfinite(value)) {
            return;
        }

        const long long epoch_seconds = timestamp_ms / 1000LL;
        if (epoch_seconds <= 0) {
            return;
        }

        auto& series = chart_indicator_series_[chart_indicator_series_key(normalized_instrument, normalized_indicator_id)];
        series.instrument = normalized_instrument;
        series.indicator_id = normalized_indicator_id;
        if (series.label.empty()) {
            series.label = normalized_indicator_id;
        }
        if (!label.empty()) {
            series.label = label;
        }
        if (!color.empty()) {
            series.color = color;
        }

        const auto point_it = std::lower_bound(series.points.begin(), series.points.end(), epoch_seconds, [](const RuntimeChartIndicatorPointSnapshot& point, long long target_time) {
            return point.time < target_time;
        });
        if (point_it != series.points.end() && point_it->time == epoch_seconds) {
            point_it->value = value;
        } else {
            series.points.insert(point_it, RuntimeChartIndicatorPointSnapshot {.time = epoch_seconds, .value = value});
        }

        trim_chart_indicator_series(series);
    }

    [[nodiscard]] std::vector<RuntimeChartIndicatorSeriesSnapshot> chart_indicator_snapshots() const {
        std::vector<RuntimeChartIndicatorSeriesSnapshot> snapshots;
        snapshots.reserve(chart_indicator_series_.size());
        for (const auto& [_, series] : chart_indicator_series_) {
            if (series.instrument.empty() || series.indicator_id.empty() || series.points.empty()) {
                continue;
            }
            RuntimeChartIndicatorSeriesSnapshot snapshot;
            snapshot.instrument = series.instrument;
            snapshot.indicator_id = series.indicator_id;
            snapshot.label = series.label.empty() ? series.indicator_id : series.label;
            snapshot.color = series.color;
            snapshot.strategy_id = strategy_id_;
            snapshot.account_id = account_id_;
            snapshot.points = series.points;
            snapshots.push_back(std::move(snapshot));
        }

        std::sort(snapshots.begin(), snapshots.end(), [](const RuntimeChartIndicatorSeriesSnapshot& left, const RuntimeChartIndicatorSeriesSnapshot& right) {
            if (left.strategy_id != right.strategy_id) {
                return left.strategy_id < right.strategy_id;
            }
            if (left.account_id != right.account_id) {
                return left.account_id < right.account_id;
            }
            return left.indicator_id < right.indicator_id;
        });

        return snapshots;
    }

private:
    struct ChartIndicatorSeriesState {
        std::string instrument;
        std::string indicator_id;
        std::string label;
        std::string color;
        std::vector<RuntimeChartIndicatorPointSnapshot> points;
    };

    [[nodiscard]] static std::string chart_indicator_series_key(std::string_view instrument, std::string_view indicator_id) {
        return trim_copy(instrument) + '|' + trim_copy(indicator_id);
    }

    void trim_chart_indicator_series(ChartIndicatorSeriesState& series) const {
        if (max_chart_series_points_ == 0 || series.points.size() <= max_chart_series_points_) {
            return;
        }

        series.points.erase(
            series.points.begin(),
            series.points.begin() + static_cast<std::ptrdiff_t>(series.points.size() - max_chart_series_points_));
    }

    [[nodiscard]] std::string next_client_order_id() {
        return strategy_id_ + "-" + std::to_string(++next_client_order_id_);
    }

    std::string strategy_id_;
    std::string account_id_;
    Mode mode_;
    int strategy_order_ref_code_ {0};
    std::function<bool(const OrderRequest&)> submit_order_;
    std::function<bool(const std::string&)> cancel_order_;
    std::function<std::vector<RuntimeOrderSnapshot>(const std::string&)> open_orders_lookup_;
    std::function<int(const std::string&)> position_lookup_;
    int next_client_order_id_ {0};
    bool order_flow_enabled_ {true};
    std::unordered_map<std::string, ChartIndicatorSeriesState> chart_indicator_series_;
    std::size_t max_chart_series_points_ {0};
};

struct StrategyRuntime {
    std::string section_name;
    std::string strategy_id;
    std::string account_id;
    std::optional<int> order_ref_strategy_code;
    std::vector<std::string> instruments;
    std::unordered_map<std::string, std::string> parameters;
    StrategyWarmupConfig warmup;
    StrategyPlugin plugin;
    std::unique_ptr<IStrategy, std::function<void(IStrategy*)>> instance;
    std::unique_ptr<StrategyContextImpl> context;

    [[nodiscard]] bool subscribed_to(const std::string& instrument) const {
        return std::find(instruments.begin(), instruments.end(), instrument) != instruments.end();
    }
};

std::set<std::string> collect_strategy_runtime_instruments(const std::vector<StrategyRuntime>& strategies) {
    std::set<std::string> instruments;
    for (const auto& strategy : strategies) {
        instruments.insert(strategy.instruments.begin(), strategy.instruments.end());
    }
    return instruments;
}

std::vector<std::string> filter_live_chart_bars_to_instruments(
    std::map<std::string, std::vector<LiveChartBar>>& live_chart_bars,
    const std::set<std::string>& allowed_instruments) {

    std::vector<std::string> removed_instruments;
    if (allowed_instruments.empty()) {
        return removed_instruments;
    }

    for (auto it = live_chart_bars.begin(); it != live_chart_bars.end();) {
        if (allowed_instruments.contains(it->first)) {
            ++it;
            continue;
        }
        removed_instruments.push_back(it->first);
        it = live_chart_bars.erase(it);
    }
    return removed_instruments;
}

std::string join_string_values(const std::vector<std::string>& values, std::string_view delimiter) {
    std::ostringstream output;
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index > 0) {
            output << delimiter;
        }
        output << values[index];
    }
    return output.str();
}

void append_strategy_chart_indicator_series(RuntimeSnapshot& snapshot, const StrategyRuntime& strategy) {
    if (strategy.context == nullptr) {
        return;
    }

    for (const auto& indicator_series : strategy.context->chart_indicator_snapshots()) {
        if (indicator_series.instrument.empty() || indicator_series.points.empty()) {
            continue;
        }

        auto& instrument_snapshot = ensure_runtime_snapshot_chart_instrument(snapshot, indicator_series.instrument);
        instrument_snapshot.indicator_series.push_back(indicator_series);
    }
}

std::vector<RuntimeChartIndicatorSeriesSnapshot> collect_strategy_chart_indicator_series(const std::vector<StrategyRuntime>& strategies) {
    std::vector<RuntimeChartIndicatorSeriesSnapshot> series;
    for (const auto& strategy : strategies) {
        if (strategy.context == nullptr) {
            continue;
        }

        const auto snapshots = strategy.context->chart_indicator_snapshots();
        series.insert(series.end(), snapshots.begin(), snapshots.end());
    }

    std::sort(series.begin(), series.end(), [](const RuntimeChartIndicatorSeriesSnapshot& left, const RuntimeChartIndicatorSeriesSnapshot& right) {
        if (left.instrument != right.instrument) {
            return left.instrument < right.instrument;
        }
        if (left.strategy_id != right.strategy_id) {
            return left.strategy_id < right.strategy_id;
        }
        if (left.account_id != right.account_id) {
            return left.account_id < right.account_id;
        }
        return left.indicator_id < right.indicator_id;
    });

    return series;
}

struct BacktestTickDispatchPlan {
    StrategyRuntime* direct_strategy {nullptr};
    StrategyRuntime* single_strategy {nullptr};
    std::string direct_instrument;
    std::unordered_map<std::string, std::vector<StrategyRuntime*>> strategies_by_instrument;
};

BacktestTickDispatchPlan build_backtest_tick_dispatch_plan(std::vector<StrategyRuntime>& strategies) {
    BacktestTickDispatchPlan plan;
    if (strategies.size() == 1 && strategies.front().instruments.size() == 1) {
        plan.direct_strategy = &strategies.front();
        plan.direct_instrument = strategies.front().instruments.front();
        return plan;
    }

    if (strategies.size() == 1) {
        plan.single_strategy = &strategies.front();
        return plan;
    }

    for (auto& strategy : strategies) {
        for (const auto& instrument : strategy.instruments) {
            plan.strategies_by_instrument[instrument].push_back(&strategy);
        }
    }

    return plan;
}

void dispatch_backtest_tick(const MarketTick& tick, const BacktestTickDispatchPlan& plan) {
    if (plan.direct_strategy != nullptr) {
        if (tick.instrument == plan.direct_instrument) {
            plan.direct_strategy->instance->on_tick(tick, *plan.direct_strategy->context);
        }
        return;
    }

    if (plan.single_strategy != nullptr) {
        if (plan.single_strategy->subscribed_to(tick.instrument)) {
            plan.single_strategy->instance->on_tick(tick, *plan.single_strategy->context);
        }
        return;
    }

    const auto instrument_it = plan.strategies_by_instrument.find(tick.instrument);
    if (instrument_it == plan.strategies_by_instrument.end()) {
        return;
    }

    for (auto* strategy : instrument_it->second) {
        strategy->instance->on_tick(tick, *strategy->context);
    }
}

itrader::StrategyAttachmentSnapshot make_backtest_attachment_snapshot(
    const StrategyRuntime& strategy,
    const SimAttachmentState& attachment_state,
    bool include_order_history) {

    itrader::StrategyAttachmentSnapshot snapshot;
    snapshot.strategy_id = strategy.strategy_id;
    snapshot.account_id = strategy.account_id;

    std::set<std::string> instruments(strategy.instruments.begin(), strategy.instruments.end());
    for (const auto& instrument : instruments) {
        const auto position_it = attachment_state.positions.find(instrument);
        snapshot.positions.push_back(position_it != attachment_state.positions.end()
            ? make_runtime_position_snapshot(instrument, strategy.account_id, strategy.strategy_id, position_it->second)
            : make_runtime_position_snapshot(instrument, strategy.account_id, strategy.strategy_id, PositionState {}));
    }

    snapshot.opened_order_count = attachment_state.opened_orders_by_id.size();
    snapshot.closed_order_count = attachment_state.closed_order_count;
    snapshot.filled_trade_count = attachment_state.filled_trade_count;

    if (include_order_history) {
        for (const auto& [_, order] : attachment_state.opened_orders_by_id) {
            snapshot.opened_orders.push_back(order);
        }

        snapshot.closed_orders = attachment_state.closed_orders;
    }

    return snapshot;
}

void record_backtest_closed_order(
    SimAttachmentState& attachment,
    const OrderEvent& event,
    bool retain_closed_order_snapshots) {

    ++attachment.closed_order_count;
    if (event.filled_volume > 0) {
        ++attachment.filled_trade_count;
    }

    if (retain_closed_order_snapshots) {
        attachment.closed_orders.push_back(make_runtime_order_snapshot(event));
    }
}

void record_backtest_fill_volume(SimAttachmentState& attachment, const OrderRequest& request, int fill_volume) {
    if (fill_volume <= 0) {
        return;
    }
    const auto volume = static_cast<std::size_t>(fill_volume);
    attachment.filled_trade_volume += volume;
    if (request.offset == Offset::Open) {
        attachment.filled_open_volume += volume;
    }
}

void initialize_backtest_attachment(SimAccountState& account, const StrategyRuntime& strategy) {
    auto& attachment = account.attachments[strategy.strategy_id];
    attachment.cost_model = read_backtest_cost_model(strategy.parameters);
    for (const auto& instrument : strategy.instruments) {
        attachment.positions.emplace(instrument, PositionState {});
    }
}

void refresh_backtest_account_snapshot(SimAccountState& account) {
    account.snapshot.realized_pnl = 0.0;
    account.snapshot.net_positions.clear();

    for (const auto& [_, attachment] : account.attachments) {
        account.snapshot.realized_pnl += attachment.realized_pnl;
        for (const auto& [instrument, position] : attachment.positions) {
            account.snapshot.net_positions[instrument] += net_quantity(position);
        }
    }

    account.snapshot.cash = account.snapshot.initial_cash + account.snapshot.realized_pnl;
}

void roll_position_map_to_next_trading_day(std::map<std::string, PositionState>& positions) {
    for (auto& [_, position] : positions) {
        roll_position_to_next_trading_day(position);
    }
}

void roll_backtest_accounts_to_next_trading_day(std::map<std::string, SimAccountState>& accounts) {
    for (auto& [_, account] : accounts) {
        for (auto& [__, attachment] : account.attachments) {
            roll_position_map_to_next_trading_day(attachment.positions);
        }
        refresh_backtest_account_snapshot(account);
    }
}

void roll_live_accounts_to_next_trading_day(
    std::map<std::string, LiveAccountState>& accounts,
    std::map<std::string, LiveAttachmentTelemetry>& attachments) {

    for (auto& [_, account] : accounts) {
        roll_position_map_to_next_trading_day(account.positions);
        account.snapshot.net_positions.clear();
        for (const auto& [instrument, position] : account.positions) {
            account.snapshot.net_positions[instrument] = net_quantity(position);
        }
    }

    for (auto& [_, attachment] : attachments) {
        roll_position_map_to_next_trading_day(attachment.positions);
        refresh_attachment_positions(attachment);
    }
}

LiveAttachmentTelemetry make_live_attachment_telemetry(const StrategyRuntime& strategy) {
    LiveAttachmentTelemetry telemetry;
    telemetry.snapshot.strategy_id = strategy.strategy_id;
    telemetry.snapshot.account_id = strategy.account_id;

    for (const auto& instrument : strategy.instruments) {
        telemetry.positions.emplace(instrument, PositionState {});
    }

    refresh_attachment_positions(telemetry);
    refresh_attachment_orders(telemetry);
    return telemetry;
}

bool is_warmup_config_key(std::string_view key) {
    return key == "warmup_enabled"
        || key == "warmup_data_dir"
        || key == "warmup_csv"
        || key == "warmup_trading_day";
}

StrategyWarmupConfig read_strategy_warmup_config(
    const IniFile& ini,
    const std::string& section_name,
    const std::filesystem::path& base_dir,
    Mode mode) {

    StrategyWarmupConfig config;

    const std::string global_data_dir = mode == Mode::Live ? trim_copy(ini.get("live", "warmup_data_dir")) : std::string {};
    const std::string global_csv = mode == Mode::Live ? trim_copy(ini.get("live", "warmup_csv")) : std::string {};
    const std::string global_trading_day = mode == Mode::Live ? trim_copy(ini.get("live", "warmup_trading_day")) : std::string {};

    const auto data_dir_value = trim_copy(ini.get(section_name, "warmup_data_dir", global_data_dir));
    const auto csv_value = trim_copy(ini.get(section_name, "warmup_csv", global_csv));
    const bool default_enabled = !data_dir_value.empty() || !csv_value.empty();
    const bool global_enabled = mode == Mode::Live ? ini.get_bool("live", "warmup_enabled", default_enabled) : default_enabled;
    config.enabled = ini.get_bool(section_name, "warmup_enabled", global_enabled);

    if (!config.enabled) {
        return config;
    }

    if (!data_dir_value.empty() && !csv_value.empty()) {
        throw std::runtime_error("Strategy section " + section_name + " must set either warmup_data_dir or warmup_csv, not both.");
    }
    if (data_dir_value.empty() && csv_value.empty()) {
        throw std::runtime_error("Strategy section " + section_name + " enabled live warmup but did not provide warmup_data_dir or warmup_csv.");
    }

    if (!data_dir_value.empty()) {
        config.data_dir = resolve_path(base_dir, data_dir_value);
    }
    if (!csv_value.empty()) {
        config.csv_path = resolve_path(base_dir, csv_value);
    }

    auto trading_day = trim_copy(ini.get(section_name, "warmup_trading_day", global_trading_day));
    const auto normalized_trading_day = lower_copy(trading_day);
    if (normalized_trading_day == "auto" || normalized_trading_day == "current") {
        trading_day.clear();
    }
    config.trading_day = canonical_trading_day_label(trading_day);
    return config;
}

StrategyRuntime load_strategy_runtime(
    const IniFile& ini,
    const std::string& section_name,
    const std::string& account_id,
    const std::filesystem::path& base_dir,
    Mode mode,
    std::function<bool(const OrderRequest&)> submit_order,
    std::function<bool(const std::string&)> cancel_order,
    std::function<std::vector<RuntimeOrderSnapshot>(const std::string&)> open_orders_lookup,
    std::function<int(const std::string&)> position_lookup) {

    const auto dll_path = resolve_path(base_dir, ini.get(section_name, "dll"));
    const auto instruments = ini.get_list(section_name, "instruments");
    const auto order_ref_strategy_code = read_strategy_order_ref_strategy_code(ini, section_name);
    if (account_id.empty()) {
        throw std::runtime_error("Strategy section " + section_name + " is missing a bound account instance");
    }
    if (instruments.empty()) {
        throw std::runtime_error("Strategy section " + section_name + " must declare at least one instrument");
    }

    StrategyPlugin plugin(dll_path);
    auto instance = plugin.create_instance();
    auto context = std::make_unique<StrategyContextImpl>(
        section_name.substr(section_name.find('.') + 1),
        account_id,
        mode,
        order_ref_strategy_code.value_or(0),
        std::move(submit_order),
        std::move(cancel_order),
        std::move(open_orders_lookup),
        std::move(position_lookup));

    auto warmup = read_strategy_warmup_config(ini, section_name, base_dir, mode);

    auto parameters = std::unordered_map<std::string, std::string> {};
    for (const auto& [key, value] : ini.section(section_name)) {
        if (key == "dll" || key == "account" || key == "accounts" || key == "instruments"
            || key == "order_ref_strategy_code" || key == "strategy_code"
            || is_warmup_config_key(key)) {
            continue;
        }
        parameters.emplace(key, value);
    }

    instance->on_init(parameters, *context);

    StrategyRuntime runtime;
    runtime.section_name = section_name;
    runtime.strategy_id = section_name.substr(section_name.find('.') + 1);
    runtime.account_id = account_id;
    runtime.order_ref_strategy_code = order_ref_strategy_code;
    runtime.instruments = instruments;
    runtime.parameters = std::move(parameters);
    runtime.warmup = std::move(warmup);
    runtime.plugin = std::move(plugin);
    runtime.instance = std::move(instance);
    runtime.context = std::move(context);
    return runtime;
}

void dispatch_event_to_strategies(const OrderEvent& event, std::vector<StrategyRuntime>& strategies) {
    const auto order_snapshot = make_runtime_order_snapshot(event);
    for (auto& strategy : strategies) {
        if (strategy.strategy_id == event.strategy_id && strategy.account_id == event.account_id) {
            strategy.instance->on_order_event(event, *strategy.context);
            strategy.instance->on_order_update(order_snapshot, *strategy.context);
        }
    }
}

void dispatch_position_update_to_strategies(const RuntimePositionSnapshot& position, std::vector<StrategyRuntime>& strategies) {
    for (auto& strategy : strategies) {
        if (strategy.strategy_id == position.strategy_id && strategy.account_id == position.account_id) {
            strategy.instance->on_position_update(position, *strategy.context);
        }
    }
}

void dispatch_backtest_attachment_position_updates(
    const SimAccountState& account,
    const std::string& strategy_id,
    const SimAttachmentState& attachment,
    std::vector<StrategyRuntime>& strategies) {

    for (const auto& [instrument, position_state] : attachment.positions) {
        dispatch_position_update_to_strategies(
            make_runtime_position_snapshot(instrument, account.snapshot.account_id, strategy_id, position_state),
            strategies);
    }
}

void dispatch_live_attachment_position_updates(const LiveAttachmentTelemetry& attachment, std::vector<StrategyRuntime>& strategies) {
    for (const auto& position : attachment.snapshot.positions) {
        dispatch_position_update_to_strategies(position, strategies);
    }
}

void dispatch_tick_to_strategies(const MarketTick& tick, const std::optional<std::string>& account_id, std::vector<StrategyRuntime>& strategies) {
    for (auto& strategy : strategies) {
        if (account_id.has_value() && strategy.account_id != *account_id) {
            continue;
        }
        if (strategy.subscribed_to(tick.instrument)) {
            strategy.instance->on_tick(tick, *strategy.context);
        }
    }
}

void service_backtest_pending_orders(
    SimAccountState& account,
    const MarketTick& tick,
    const std::optional<PreviousBacktestTick>& previous_tick,
    std::vector<OrderEvent>& events,
    bool retain_closed_order_snapshots) {

    bool snapshot_dirty = false;

    for (auto& [_, attachment] : account.attachments) {
        std::vector<PendingOrder> still_pending;
        still_pending.reserve(attachment.pending_orders.size());

        for (auto pending : attachment.pending_orders) {
            double trade_price = 0.0;
            const int fill_volume = pending.synthetic_runtime_match
                ? synthetic_resting_fill_volume(pending, tick, previous_tick, attachment.cost_model, trade_price)
                : resting_fill_volume(pending, tick, previous_tick);
            if (fill_volume <= 0) {
                still_pending.push_back(pending);
                continue;
            }

            auto& position = attachment.positions[pending.request.instrument];
            OrderRequest remaining_request = pending.request;
            remaining_request.volume = pending.remaining_volume;
            if (!close_request_is_valid(position, remaining_request)) {
                const auto rejected_event = make_event(
                    pending.request,
                    pending.order_id,
                    OrderStatus::Rejected,
                    0,
                    0.0,
                    "pending close order no longer matches current strategy today/yesterday position",
                    tick.timestamp);
                attachment.opened_orders_by_id.erase(pending.order_id);
                record_backtest_closed_order(attachment, rejected_event, retain_closed_order_snapshots);
                events.push_back(rejected_event);
                snapshot_dirty = true;
                continue;
            }

            if (!pending.synthetic_runtime_match) {
                trade_price = fill_price(pending.request, tick, previous_tick);
            }
            const double realized = apply_fill(position, pending.request, trade_price, fill_volume, attachment.cost_model);
            pending.remaining_volume -= fill_volume;
            pending.filled_volume += fill_volume;
            record_backtest_fill_volume(attachment, pending.request, fill_volume);
            attachment.realized_pnl += realized;
            snapshot_dirty = true;

            const auto status = pending.remaining_volume == 0 ? OrderStatus::Filled : OrderStatus::PartiallyFilled;
            const auto message = pending.remaining_volume == 0
                ? (realized == 0.0 ? "filled by Uft-style backtest matcher" : "filled by Uft-style backtest matcher, realized_pnl=" + format_price(realized))
                : (realized == 0.0 ? "partially filled by Uft-style backtest matcher" : "partially filled by Uft-style backtest matcher, realized_pnl=" + format_price(realized));
            const auto fill_event = make_event(
                pending.request,
                pending.order_id,
                status,
                fill_volume,
                trade_price,
                message,
                tick.timestamp);
            events.push_back(fill_event);

            if (pending.remaining_volume == 0) {
                attachment.opened_orders_by_id.erase(pending.order_id);
                record_backtest_closed_order(attachment, fill_event, retain_closed_order_snapshots);
            } else {
                auto opened_snapshot = make_runtime_order_snapshot(pending);
                opened_snapshot.filled_price = trade_price;
                opened_snapshot.timestamp = tick.timestamp;
                attachment.opened_orders_by_id[pending.order_id] = std::move(opened_snapshot);
                still_pending.push_back(std::move(pending));
            }
        }

        attachment.pending_orders = std::move(still_pending);
    }

    if (snapshot_dirty) {
        refresh_backtest_account_snapshot(account);
    }
}

bool submit_backtest_order(
    SimAccountState& account,
    const OrderRequest& request,
    const MarketTick& tick,
    const std::optional<PreviousBacktestTick>& previous_tick,
    std::vector<OrderEvent>& events,
    bool retain_closed_order_snapshots) {

    const std::string order_id = account.snapshot.account_id + '-' + std::to_string(account.next_order_id++);
    events.push_back(make_event(request, order_id, OrderStatus::Submitted, 0, 0.0, "accepted by backtest engine", tick.timestamp));

    const auto attachment_it = account.attachments.find(request.strategy_id);
    if (attachment_it == account.attachments.end()) {
        const auto rejected_event = make_event(request, order_id, OrderStatus::Rejected, 0, 0.0, "unknown strategy attachment for account", tick.timestamp);
        events.push_back(rejected_event);
        return false;
    }

    auto& attachment = attachment_it->second;
    auto& position = attachment.positions[request.instrument];
    if (!close_request_is_valid(position, request)) {
        const auto rejected_event = make_event(request, order_id, OrderStatus::Rejected, 0, 0.0, "close order does not match current strategy long/short position", tick.timestamp);
        record_backtest_closed_order(attachment, rejected_event, retain_closed_order_snapshots);
        events.push_back(rejected_event);
        return false;
    }

    if (request.backtest_force_fill && request.activate_at_ms > 0) {
        PendingOrder scheduled;
        scheduled.request = request;
        scheduled.order_id = order_id;
        scheduled.remaining_volume = request.volume;
        scheduled.filled_volume = 0;
        scheduled.queue_ahead = 0;
        scheduled.activated = false;
        scheduled.synthetic_runtime_match = true;

        if (request.activate_at_ms > tick.timestamp_ms) {
            attachment.scheduled_orders.push_back(scheduled);
            attachment.opened_orders_by_id[order_id] = make_runtime_order_snapshot(scheduled);
            const auto accepted_event = make_event(request, order_id, OrderStatus::Accepted, 0, 0.0, "scheduled order awaiting activation", tick.timestamp);
            events.push_back(accepted_event);
            return true;
        }

        double trade_price = 0.0;
        const int fill_volume = synthetic_resting_fill_volume(scheduled, tick, previous_tick, attachment.cost_model, trade_price);
        if (fill_volume > 0) {
            const double realized = apply_fill(position, request, trade_price, fill_volume, attachment.cost_model);
            record_backtest_fill_volume(attachment, request, fill_volume);
            attachment.realized_pnl += realized;
            refresh_backtest_account_snapshot(account);
            const auto filled_event = make_event(
                request,
                order_id,
                OrderStatus::Filled,
                fill_volume,
                trade_price,
                realized == 0.0 ? "filled by runtime synthetic matcher" : "filled by runtime synthetic matcher, realized_pnl=" + format_price(realized),
                tick.timestamp);
            record_backtest_closed_order(attachment, filled_event, retain_closed_order_snapshots);
            events.push_back(filled_event);
            return true;
        }

        attachment.pending_orders.push_back(std::move(scheduled));
        const auto accepted_event = make_event(request, order_id, OrderStatus::Accepted, 0, 0.0, "runtime synthetic order", tick.timestamp);
        attachment.opened_orders_by_id[order_id] = make_runtime_order_snapshot(attachment.pending_orders.back());
        events.push_back(accepted_event);
        return true;
    }

    if (request.backtest_force_fill) {
        const double trade_price = request.backtest_fill_price > 0.0
            ? request.backtest_fill_price
            : (request.price_type == PriceType::Market
                ? (request.side == Side::Buy ? best_ask(tick) : best_bid(tick))
                : request.limit_price);
        if (trade_price <= 0.0) {
            const auto rejected_event = make_event(request, order_id, OrderStatus::Rejected, 0, 0.0, "backtest force-fill requested without a valid fill price", tick.timestamp);
            record_backtest_closed_order(attachment, rejected_event, retain_closed_order_snapshots);
            events.push_back(rejected_event);
            return false;
        }

        const double realized = apply_fill(position, request, trade_price, request.volume, attachment.cost_model);
        record_backtest_fill_volume(attachment, request, request.volume);
        attachment.realized_pnl += realized;
        refresh_backtest_account_snapshot(account);
        const auto filled_event = make_event(
            request,
            order_id,
            OrderStatus::Filled,
            request.volume,
            trade_price,
            realized == 0.0 ? "filled by strategy backtest override" : "filled by strategy backtest override, realized_pnl=" + format_price(realized),
            tick.timestamp);
        record_backtest_closed_order(attachment, filled_event, retain_closed_order_snapshots);
        events.push_back(filled_event);
        return true;
    }

    const auto flow = infer_trade_flow(previous_tick, tick);
    if (can_fill_immediately(request, tick, flow)) {
        const double trade_price = fill_price(request, tick, previous_tick);
        const double realized = apply_fill(position, request, trade_price, request.volume, attachment.cost_model);
        record_backtest_fill_volume(attachment, request, request.volume);
        attachment.realized_pnl += realized;
        refresh_backtest_account_snapshot(account);
        const auto filled_event = make_event(
            request,
            order_id,
            OrderStatus::Filled,
            request.volume,
            trade_price,
            realized == 0.0 ? "filled immediately" : "filled immediately, realized_pnl=" + format_price(realized),
            tick.timestamp);
        record_backtest_closed_order(attachment, filled_event, retain_closed_order_snapshots);
        events.push_back(filled_event);
        return true;
    }

    PendingOrder pending;
    pending.request = request;
    pending.order_id = order_id;
    pending.remaining_volume = request.volume;
    pending.filled_volume = 0;
    pending.queue_ahead = estimate_queue_ahead(pending, tick);
    pending.activated = true;
    attachment.pending_orders.push_back(pending);
    const auto accepted_event = make_event(request, order_id, OrderStatus::Accepted, 0, 0.0, "resting limit order", tick.timestamp);
    attachment.opened_orders_by_id[order_id] = make_runtime_order_snapshot(pending);
    events.push_back(accepted_event);
    return true;
}

bool cancel_backtest_order(
    SimAccountState& account,
    const std::string& strategy_id,
    const std::string& client_order_id,
    const std::string& timestamp,
    std::vector<OrderEvent>& events,
    bool retain_closed_order_snapshots) {

    const auto attachment_it = account.attachments.find(strategy_id);
    if (attachment_it == account.attachments.end()) {
        return false;
    }

    auto& attachment = attachment_it->second;
    for (auto it = attachment.scheduled_orders.begin(); it != attachment.scheduled_orders.end(); ++it) {
        if (it->request.client_order_id != client_order_id) {
            continue;
        }

        const auto cancelled_event = make_event(
            it->request,
            it->order_id,
            OrderStatus::Cancelled,
            0,
            0.0,
            "scheduled order cancelled by strategy",
            timestamp,
            it->order_id);
        attachment.opened_orders_by_id.erase(it->order_id);
        record_backtest_closed_order(attachment, cancelled_event, retain_closed_order_snapshots);
        attachment.scheduled_orders.erase(it);
        events.push_back(cancelled_event);
        return true;
    }

    for (auto it = attachment.pending_orders.begin(); it != attachment.pending_orders.end(); ++it) {
        if (it->request.client_order_id != client_order_id) {
            continue;
        }

        const auto cancelled_event = make_event(
            it->request,
            it->order_id,
            OrderStatus::Cancelled,
            0,
            0.0,
            "cancelled by strategy",
            timestamp,
            it->order_id);
        attachment.opened_orders_by_id.erase(it->order_id);
        record_backtest_closed_order(attachment, cancelled_event, retain_closed_order_snapshots);
        attachment.pending_orders.erase(it);
        events.push_back(cancelled_event);
        return true;
    }

    return false;
}

void activate_backtest_scheduled_orders(
    SimAccountState& account,
    const MarketTick& tick,
    [[maybe_unused]] std::vector<OrderEvent>& events) {

    for (auto& [_, attachment] : account.attachments) {
        std::vector<PendingOrder> still_scheduled;
        still_scheduled.reserve(attachment.scheduled_orders.size());

        for (auto scheduled : attachment.scheduled_orders) {
            if (scheduled.request.instrument != tick.instrument
                || scheduled.request.activate_at_ms <= 0
                || tick.timestamp_ms < scheduled.request.activate_at_ms) {
                still_scheduled.push_back(std::move(scheduled));
                continue;
            }

            scheduled.request.activate_at_ms = 0;
            attachment.pending_orders.push_back(std::move(scheduled));
        }

        attachment.scheduled_orders = std::move(still_scheduled);
    }
}

int run_backtest(
    const std::filesystem::path& config_path,
    const IniFile& ini,
    const BacktestCliOutputSession* cli_output = nullptr) {
    const auto base_dir = config_path.parent_path();
    const auto account_sections = ini.sections_with_prefix("account.");
    const auto strategy_sections = ini.sections_with_prefix("strategy.");
    if (account_sections.empty()) {
        throw std::runtime_error("Backtest config needs at least one [account.*] section");
    }
    if (strategy_sections.empty()) {
        throw std::runtime_error("Backtest config needs at least one [strategy.*] section");
    }

    std::map<std::string, SimAccountState> accounts;
    for (const auto& section : account_sections) {
        const std::string account_id = section.substr(section.find('.') + 1);
        SimAccountState state;
        state.snapshot.account_id = account_id;
        state.snapshot.initial_cash = ini.get_double(section, "initial_cash", 1'000'000.0);
        state.snapshot.cash = state.snapshot.initial_cash;
        accounts.emplace(account_id, std::move(state));
    }

    std::vector<OrderRequest> order_queue;
    std::vector<OrderEvent> events;
    constexpr bool retain_closed_order_snapshots = true;
    std::string current_action_timestamp;
    std::vector<StrategyRuntime> strategies;
    for (const auto& section : strategy_sections) {
        const auto bound_accounts = read_strategy_accounts(ini, section);
        if (bound_accounts.empty()) {
            std::cout << "Skipping unbound strategy section " << section << " in backtest mode.\n";
            continue;
        }
        for (const auto& account_id : bound_accounts) {
            const auto strategy_id = section.substr(section.find('.') + 1);
            auto runtime = load_strategy_runtime(
                ini,
                section,
                account_id,
                base_dir,
                Mode::Backtest,
                [&order_queue](const OrderRequest& request) {
                    order_queue.push_back(request);
                    return true;
                },
                [&accounts, &events, &current_action_timestamp, account_id, strategy_id, retain_closed_order_snapshots](const std::string& client_order_id) {
                    const auto account_it = accounts.find(account_id);
                    if (account_it == accounts.end()) {
                        return false;
                    }
                    return cancel_backtest_order(
                        account_it->second,
                        strategy_id,
                        client_order_id,
                        current_action_timestamp.empty() ? current_timestamp() : current_action_timestamp,
                        events,
                        retain_closed_order_snapshots);
                },
                [&accounts, account_id, strategy_id](const std::string& instrument) {
                    std::vector<RuntimeOrderSnapshot> orders;
                    const auto account_it = accounts.find(account_id);
                    if (account_it == accounts.end()) {
                        return orders;
                    }
                    const auto attachment_it = account_it->second.attachments.find(strategy_id);
                    if (attachment_it == account_it->second.attachments.end()) {
                        return orders;
                    }
                    orders.reserve(attachment_it->second.opened_orders_by_id.size());
                    for (const auto& [_, order] : attachment_it->second.opened_orders_by_id) {
                        if (!instrument.empty() && order.instrument != instrument) {
                            continue;
                        }
                        orders.push_back(order);
                    }
                    return orders;
                },
                [&accounts, account_id, strategy_id](const std::string& instrument) {
                    const auto account_it = accounts.find(account_id);
                    if (account_it == accounts.end()) {
                        return 0;
                    }
                    const auto attachment_it = account_it->second.attachments.find(strategy_id);
                    if (attachment_it == account_it->second.attachments.end()) {
                        return 0;
                    }
                    const auto position_it = attachment_it->second.positions.find(instrument);
                    return position_it == attachment_it->second.positions.end() ? 0 : net_quantity(position_it->second);
                });
            initialize_backtest_attachment(accounts[account_id], runtime);
            refresh_backtest_account_snapshot(accounts[account_id]);
            strategies.push_back(std::move(runtime));
        }
    }

    if (strategies.empty()) {
        std::cout << "No assigned strategies loaded for backtest mode; the engine will replay data without active strategy execution.\n";
    }

    for (auto& strategy : strategies) {
        strategy.instance->on_start(*strategy.context);
    }
    for (const auto& [_, account] : accounts) {
        for (const auto& [strategy_id, attachment] : account.attachments) {
            dispatch_backtest_attachment_position_updates(account, strategy_id, attachment, strategies);
        }
    }

    const auto tick_dispatch_plan = build_backtest_tick_dispatch_plan(strategies);

    std::string current_trading_day;
    std::vector<BacktestDailyPnlRow> daily_pnl_rows;
    std::unordered_map<std::string, PreviousBacktestTick> previous_ticks;

    auto append_daily_pnl_row = [&accounts, &daily_pnl_rows](const std::string& trading_day) {
        const auto canonical_day = canonical_trading_day_label(trading_day);
        if (canonical_day.empty()) {
            return;
        }

        const double cumulative_pnl = total_account_realized_pnl(accounts);
        const double prior_cumulative_pnl = daily_pnl_rows.empty() ? 0.0 : daily_pnl_rows.back().cumulative_pnl;
        if (!daily_pnl_rows.empty() && daily_pnl_rows.back().trading_day == canonical_day) {
            const double previous_row_cumulative = daily_pnl_rows.size() >= 2
                ? daily_pnl_rows[daily_pnl_rows.size() - 2].cumulative_pnl
                : 0.0;
            daily_pnl_rows.back().pnl = cumulative_pnl - previous_row_cumulative;
            daily_pnl_rows.back().cumulative_pnl = cumulative_pnl;
            return;
        }

        daily_pnl_rows.push_back(BacktestDailyPnlRow {
            .trading_day = canonical_day,
            .date = display_trading_day_label(canonical_day),
            .pnl = cumulative_pnl - prior_cumulative_pnl,
            .cumulative_pnl = cumulative_pnl,
        });
    };

    auto refresh_pending_orders_flag = [&accounts]() {
        for (const auto& [_, account] : accounts) {
            for (const auto& [__, attachment] : account.attachments) {
                if (!attachment.pending_orders.empty() || !attachment.scheduled_orders.empty()) {
                    return true;
                }
            }
        }
        return false;
    };

    bool any_pending_orders = false;
    auto process_tick = [&](const MarketTick& tick) {
        current_action_timestamp = tick.timestamp;
        const auto previous_tick_it = previous_ticks.find(tick.instrument);
        const std::optional<PreviousBacktestTick> previous_tick = previous_tick_it == previous_ticks.end()
            ? std::nullopt
            : std::optional<PreviousBacktestTick> {previous_tick_it->second};
        const auto next_trading_day = trading_day_label_from_tick(tick);
        const bool trading_day_changed = !next_trading_day.empty()
            && !current_trading_day.empty()
            && next_trading_day != current_trading_day;
        if (trading_day_changed) {
            append_daily_pnl_row(current_trading_day);
            roll_backtest_accounts_to_next_trading_day(accounts);
            for (const auto& [_, account] : accounts) {
                for (const auto& [strategy_id, attachment] : account.attachments) {
                    dispatch_backtest_attachment_position_updates(account, strategy_id, attachment, strategies);
                }
            }
        }
        if (!next_trading_day.empty()) {
            current_trading_day = next_trading_day;
        }

        bool tick_dispatched = false;
        if (trading_day_changed) {
            dispatch_backtest_tick(tick, tick_dispatch_plan);
            tick_dispatched = true;
            if (!events.empty()) {
                for (const auto& event : events) {
                    dispatch_event_to_strategies(event, strategies);
                }
                events.clear();
                any_pending_orders = refresh_pending_orders_flag();
            }
        }

        if (any_pending_orders) {
            for (auto& [_, account] : accounts) {
                activate_backtest_scheduled_orders(account, tick, events);
            }
            for (auto& [_, account] : accounts) {
                service_backtest_pending_orders(account, tick, previous_tick, events, retain_closed_order_snapshots);
            }
        }

        if (!events.empty()) {
            for (const auto& event : events) {
                dispatch_event_to_strategies(event, strategies);
                if (is_trade_fill_event(event)) {
                    const auto account_it = accounts.find(event.account_id);
                    if (account_it == accounts.end()) {
                        continue;
                    }
                    const auto attachment_it = account_it->second.attachments.find(event.strategy_id);
                    if (attachment_it == account_it->second.attachments.end()) {
                        continue;
                    }
                    const auto position_it = attachment_it->second.positions.find(event.instrument);
                    if (position_it == attachment_it->second.positions.end()) {
                        continue;
                    }
                    dispatch_position_update_to_strategies(
                        make_runtime_position_snapshot(event.instrument, event.account_id, event.strategy_id, position_it->second),
                        strategies);
                }
            }
            events.clear();
            any_pending_orders = refresh_pending_orders_flag();
        }

        if (!tick_dispatched) {
            dispatch_backtest_tick(tick, tick_dispatch_plan);
        }

        if (!order_queue.empty()) {
            auto queued_orders = std::move(order_queue);
            order_queue.clear();
            for (const auto& request : queued_orders) {
                const auto account_it = accounts.find(request.account_id);
                if (account_it == accounts.end()) {
                    events.push_back(make_event(request, "unknown", OrderStatus::Rejected, 0, 0.0, "unknown account", tick.timestamp));
                    continue;
                }
                submit_backtest_order(account_it->second, request, tick, previous_tick, events, retain_closed_order_snapshots);
            }
            if (!events.empty()) {
                for (const auto& event : events) {
                    dispatch_event_to_strategies(event, strategies);
                    if (is_trade_fill_event(event)) {
                        const auto account_it = accounts.find(event.account_id);
                        if (account_it == accounts.end()) {
                            continue;
                        }
                        const auto attachment_it = account_it->second.attachments.find(event.strategy_id);
                        if (attachment_it == account_it->second.attachments.end()) {
                            continue;
                        }
                        const auto position_it = attachment_it->second.positions.find(event.instrument);
                        if (position_it == attachment_it->second.positions.end()) {
                            continue;
                        }
                        dispatch_position_update_to_strategies(
                            make_runtime_position_snapshot(event.instrument, event.account_id, event.strategy_id, position_it->second),
                            strategies);
                    }
                }
                events.clear();
            }
            any_pending_orders = refresh_pending_orders_flag();
        }

        previous_ticks[tick.instrument] = PreviousBacktestTick {
            .timestamp_ms = tick.timestamp_ms,
            .last = tick.last,
            .bid = tick.bid,
            .ask = tick.ask,
            .volume = tick.volume,
            .turnover = tick.turnover,
            .bid_size = tick.bid_size,
            .ask_size = tick.ask_size,
        };
    };

    const auto source_plan = collect_backtest_source_plan(base_dir, ini, strategy_sections);
    if (!source_plan.directory_files.empty() && !source_plan.csv_path.has_value()) {
        BacktestDirectoryTickStream tick_stream(source_plan);
        MarketTick tick;
        while (tick_stream.next(tick)) {
            process_tick(tick);
            if (g_stop_requested) {
                std::cout << "Interrupt received; stopping backtest loop.\n";
                break;
            }
        }
        if (!tick_stream.emitted_any()) {
            throw std::runtime_error("Backtest data source did not yield any tick rows after streaming directory files.");
        }
    } else {
        const auto ticks = load_backtest_ticks(base_dir, ini, strategy_sections);
        for (const auto& tick : ticks) {
            process_tick(tick);
            if (g_stop_requested) {
                std::cout << "Interrupt received; stopping backtest loop.\n";
                break;
            }
        }
    }

    for (auto& strategy : strategies) {
        strategy.instance->on_stop(*strategy.context);
    }
    append_daily_pnl_row(current_trading_day);

    std::cout << "\nBacktest summary\n";
    for (const auto& [account_id, account] : accounts) {
        std::cout << "- account=" << account_id
                  << ", realized_pnl=" << format_price(account.snapshot.realized_pnl)
                  << ", cash=" << format_price(account.snapshot.cash)
                  << ", net_positions=";

        bool first = true;
        for (const auto& [instrument, net] : account.snapshot.net_positions) {
            if (!first) {
                std::cout << ';';
            }
            first = false;
            std::cout << instrument << ':' << net;
        }
        if (first) {
            std::cout << "flat";
        }
        std::cout << '\n';
    }

    if (cli_output != nullptr) {
        cli_output->export_backtest_results(config_path, accounts, daily_pnl_rows);
        std::cout << "\nBacktest output files\n"
                  << "- log=" << cli_output->log_path().string() << '\n'
                  << "- metrics=" << (cli_output->output_dir() / "performance_metrics.csv").string() << '\n'
                  << "- metrics_alias=" << (cli_output->output_dir() / "performance_metrics.cvv").string() << '\n'
                  << "- daily_pnl_csv=" << (cli_output->output_dir() / "daily_cumulative_pnl.csv").string() << '\n'
                  << "- daily_pnl_svg=" << (cli_output->output_dir() / "daily_cumulative_pnl.svg").string() << '\n'
                  << "- closed_orders_csv=" << (cli_output->output_dir() / "closed_orders.csv").string() << '\n'
                  << "- config=" << cli_output->copied_config_path().string() << '\n';
    }

    return 0;
}

#ifdef ITRADER_ENABLE_CTP
void append_unique_warning(std::vector<std::string>& warnings, const std::string& warning) {
    if (std::find(warnings.begin(), warnings.end(), warning) == warnings.end()) {
        warnings.push_back(warning);
    }
}

struct BrokerPositionDetailAggregate {
    int long_today_quantity {0};
    double long_today_price_sum {0.0};
    int long_yesterday_quantity {0};
    double long_yesterday_price_sum {0.0};
    int short_today_quantity {0};
    double short_today_price_sum {0.0};
    int short_yesterday_quantity {0};
    double short_yesterday_price_sum {0.0};
};

std::string broker_position_key(std::string_view exchange, std::string_view instrument) {
    return trim_copy(exchange) + "::" + trim_copy(instrument);
}

bool broker_position_detail_is_today(const CtpPositionDetailSnapshot& detail) {
    const auto open_date = trim_copy(detail.open_date);
    const auto trading_day = trim_copy(detail.trading_day);
    return !open_date.empty() && !trading_day.empty() && open_date == trading_day;
}

void accumulate_broker_position_detail(BrokerPositionDetailAggregate& aggregate, const CtpPositionDetailSnapshot& detail) {
    if (detail.volume <= 0 || detail.open_price <= 0.0) {
        return;
    }

    const bool is_today = broker_position_detail_is_today(detail);
    if (detail.side == Side::Sell) {
        if (is_today) {
            aggregate.short_today_quantity += detail.volume;
            aggregate.short_today_price_sum += detail.open_price * static_cast<double>(detail.volume);
        } else {
            aggregate.short_yesterday_quantity += detail.volume;
            aggregate.short_yesterday_price_sum += detail.open_price * static_cast<double>(detail.volume);
        }
        return;
    }

    if (is_today) {
        aggregate.long_today_quantity += detail.volume;
        aggregate.long_today_price_sum += detail.open_price * static_cast<double>(detail.volume);
    } else {
        aggregate.long_yesterday_quantity += detail.volume;
        aggregate.long_yesterday_price_sum += detail.open_price * static_cast<double>(detail.volume);
    }
}

void assign_bucket_average_from_broker_details(
    double& average_price_bucket,
    int bucket_quantity,
    int detail_quantity,
    double detail_price_sum,
    std::string_view account_id,
    std::string_view instrument,
    std::string_view bucket_label,
    std::vector<std::string>& warnings) {

    if (bucket_quantity <= 0) {
        average_price_bucket = 0.0;
        return;
    }

    if (detail_quantity != bucket_quantity || detail_price_sum <= 0.0) {
        append_unique_warning(
            warnings,
            "QryInvestorPositionDetail could not fully recover average price for account " + std::string(account_id)
                + ", instrument " + std::string(instrument)
                + ", bucket " + std::string(bucket_label)
                + " (broker quantity=" + std::to_string(bucket_quantity)
                + ", detail quantity=" + std::to_string(detail_quantity) + ").");
        return;
    }

    average_price_bucket = detail_price_sum / static_cast<double>(detail_quantity);
}

void seed_position_average_prices_from_broker_details(
    PositionState& position,
    const CtpInstrumentPositionSnapshot& broker_position,
    const BrokerPositionDetailAggregate* detail_aggregate,
    std::string_view account_id,
    std::vector<std::string>& warnings) {

    if (detail_aggregate == nullptr) {
        return;
    }

    assign_bucket_average_from_broker_details(
        position.long_today_average_price,
        broker_position.long_today_quantity,
        detail_aggregate->long_today_quantity,
        detail_aggregate->long_today_price_sum,
        account_id,
        broker_position.instrument,
        "long_today",
        warnings);
    assign_bucket_average_from_broker_details(
        position.long_yesterday_average_price,
        broker_position.long_yesterday_quantity,
        detail_aggregate->long_yesterday_quantity,
        detail_aggregate->long_yesterday_price_sum,
        account_id,
        broker_position.instrument,
        "long_yesterday",
        warnings);
    assign_bucket_average_from_broker_details(
        position.short_today_average_price,
        broker_position.short_today_quantity,
        detail_aggregate->short_today_quantity,
        detail_aggregate->short_today_price_sum,
        account_id,
        broker_position.instrument,
        "short_today",
        warnings);
    assign_bucket_average_from_broker_details(
        position.short_yesterday_average_price,
        broker_position.short_yesterday_quantity,
        detail_aggregate->short_yesterday_quantity,
        detail_aggregate->short_yesterday_price_sum,
        account_id,
        broker_position.instrument,
        "short_yesterday",
        warnings);
}

void seed_position_from_broker_snapshot(PositionState& position, const CtpInstrumentPositionSnapshot& broker_position) {
    position.long_today_quantity = broker_position.long_today_quantity;
    position.long_today_average_price = 0.0;
    position.long_yesterday_quantity = broker_position.long_yesterday_quantity;
    position.long_yesterday_average_price = 0.0;
    position.short_today_quantity = broker_position.short_today_quantity;
    position.short_today_average_price = 0.0;
    position.short_yesterday_quantity = broker_position.short_yesterday_quantity;
    position.short_yesterday_average_price = 0.0;
}

void refresh_live_account_net_positions(LiveAccountState& account) {
    account.snapshot.net_positions.clear();
    for (const auto& [instrument, position] : account.positions) {
        account.snapshot.net_positions[instrument] = net_quantity(position);
    }
}

std::filesystem::path strategy_inventory_store_path(const std::filesystem::path& config_path) {
    return itrader::strategy_inventory_store_path(config_path);
}

std::filesystem::path strategy_state_store_path(const std::filesystem::path& config_path) {
    return itrader::strategy_state_store_path(config_path);
}

std::filesystem::path strategy_inventory_adjustments_path(const std::filesystem::path& config_path) {
    return itrader::strategy_inventory_adjustments_path(config_path);
}

struct InventoryBucketAdjustment {
    int delta {0};
    std::optional<double> average_price;
};

struct ManualInventoryAdjustment {
    std::string adjustment_id;
    std::string account_id;
    std::string strategy_id;
    std::string instrument;
    std::string exchange;
    std::string operator_id;
    std::string reason_code;
    std::string reason_text;
    InventoryBucketAdjustment long_today;
    InventoryBucketAdjustment long_yesterday;
    InventoryBucketAdjustment short_today;
    InventoryBucketAdjustment short_yesterday;
};

struct AppliedInventoryAdjustment {
    std::string adjustment_id;
    std::string account_id;
    std::string strategy_id;
    std::string instrument;
    std::string exchange;
    std::string operator_id;
    std::string reason_code;
    std::string reason_text;
    std::string applied_at;
};

struct PersistedInventoryStore {
    std::map<std::string, std::map<std::string, PositionState>> attachment_positions;
    std::map<std::string, std::vector<RuntimeOrderSnapshot>> fill_history_by_attachment;
    std::map<std::string, std::map<std::string, PositionState>> broker_positions_by_account;
    std::map<std::string, AppliedInventoryAdjustment> applied_adjustments;
};

using PersistedStrategyStateStore = std::map<std::string, IniFile::Section>;

std::string_view attachment_key_strategy_id(std::string_view attachment_key) {
    const auto delimiter = attachment_key.rfind("::");
    return delimiter == std::string_view::npos ? attachment_key : attachment_key.substr(0, delimiter);
}

std::string_view attachment_key_account_id(std::string_view attachment_key) {
    const auto delimiter = attachment_key.rfind("::");
    return delimiter == std::string_view::npos ? std::string_view {} : attachment_key.substr(delimiter + 2);
}

std::string strategy_state_section_name(std::string_view strategy_id, std::string_view account_id) {
    return "strategy_state." + telemetry_attachment_key(strategy_id, account_id);
}

PersistedStrategyStateStore read_strategy_state_store(const std::filesystem::path& config_path) {
    PersistedStrategyStateStore store;
    const auto store_path = strategy_state_store_path(config_path);
    if (!std::filesystem::exists(store_path)) {
        return store;
    }

    const auto ini = IniFile::parse(store_path);
    for (const auto& section : ini.sections_with_prefix("strategy_state.")) {
        auto state = ini.section(section);
        for (auto it = state.begin(); it != state.end();) {
            if (it->first.empty() || it->first.rfind("__", 0) == 0) {
                it = state.erase(it);
            } else {
                ++it;
            }
        }

        const auto attachment_key = section.substr(std::string("strategy_state.").size());
        if (attachment_key.empty()) {
            continue;
        }
        store[attachment_key] = std::move(state);
    }

    return store;
}

void restore_live_strategy_state(
    const PersistedStrategyStateStore& store,
    std::vector<StrategyRuntime>& strategies) {

    for (auto& strategy : strategies) {
        if (strategy.instance == nullptr || strategy.context == nullptr || strategy.context->mode() != Mode::Live) {
            continue;
        }

        const auto store_it = store.find(telemetry_attachment_key(strategy.strategy_id, strategy.account_id));
        if (store_it == store.end() || store_it->second.empty()) {
            continue;
        }

        strategy.instance->restore_live_state(
            StrategyStateMap(store_it->second.begin(), store_it->second.end()),
            *strategy.context);
    }
}

void write_strategy_state_store(
    const std::filesystem::path& config_path,
    const std::vector<StrategyRuntime>& strategies) {

    const auto store_path = strategy_state_store_path(config_path);
    std::error_code error_code;
    std::filesystem::create_directories(store_path.parent_path(), error_code);

    std::ostringstream output;
    output << "[strategy_state_store]\n";
    output << "updated_at=" << current_timestamp() << "\n\n";

    for (const auto& strategy : strategies) {
        if (strategy.instance == nullptr || strategy.context == nullptr || strategy.context->mode() != Mode::Live) {
            continue;
        }

        const auto state = strategy.instance->capture_live_state(*strategy.context);
        if (state.empty()) {
            continue;
        }

        output << '[' << strategy_state_section_name(strategy.strategy_id, strategy.account_id) << "]\n";
        output << "__updated_at=" << current_timestamp() << "\n";
        output << "__strategy_name=" << strategy.instance->name() << "\n";

        const std::map<std::string, std::string> ordered_state(state.begin(), state.end());
        for (const auto& [key, value] : ordered_state) {
            if (key.empty() || key.rfind("__", 0) == 0) {
                continue;
            }
            output << key << '=' << value << "\n";
        }
        output << "\n";
    }

    std::ofstream file(store_path, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        throw std::runtime_error("Unable to write strategy state store: " + store_path.string());
    }
    file << output.str();
}

struct RecoveredWorkingOrderBinding {
    std::string attachment_key;
    std::string client_order_id;
};

using RecoveredWorkingOrderLookup = std::map<std::string, std::map<std::string, RecoveredWorkingOrderBinding>>;

RecoveredWorkingOrderLookup build_recovered_working_order_lookup(const PersistedStrategyStateStore& store) {
    RecoveredWorkingOrderLookup lookup;
    constexpr std::string_view broker_order_suffix {".broker_order_id"};

    for (const auto& [attachment_key, state] : store) {
        const auto account_id = std::string(attachment_key_account_id(attachment_key));
        if (account_id.empty()) {
            continue;
        }

        for (const auto& [key, broker_order_id] : state) {
            if (broker_order_id.empty()
                || key.size() <= broker_order_suffix.size()
                || key.rfind(broker_order_suffix) != key.size() - broker_order_suffix.size()) {
                continue;
            }

            const auto base_key = key.substr(0, key.size() - broker_order_suffix.size());
            const auto client_order_it = state.find(base_key + ".client_order_id");
            if (client_order_it == state.end() || client_order_it->second.empty()) {
                continue;
            }

            lookup[account_id][broker_order_id] = RecoveredWorkingOrderBinding {
                .attachment_key = attachment_key,
                .client_order_id = client_order_it->second,
            };
        }
    }

    return lookup;
}

std::string recovered_trade_dedupe_key(const RuntimeOrderSnapshot& trade) {
    return trade.order_id + '|' + trade.source_order_id + '|' + trade.timestamp + '|' + trade.strategy_id + '|' + trade.account_id;
}

using RecoveredTradeReplayMap = std::map<std::string, std::vector<RuntimeOrderSnapshot>>;

RecoveredWorkingOrderLookup build_recovered_trade_binding_lookup(
    const PersistedStrategyStateStore& state_store,
    const PersistedInventoryStore& inventory_store) {

    auto lookup = build_recovered_working_order_lookup(state_store);
    for (const auto& [attachment_key, fills] : inventory_store.fill_history_by_attachment) {
        const auto account_id = std::string(attachment_key_account_id(attachment_key));
        if (account_id.empty()) {
            continue;
        }

        for (const auto& fill : fills) {
            if (fill.order_id.empty() || fill.client_order_id.empty()) {
                continue;
            }

            lookup[account_id][fill.order_id] = RecoveredWorkingOrderBinding {
                .attachment_key = attachment_key,
                .client_order_id = fill.client_order_id,
            };
        }
    }

    return lookup;
}

std::set<std::string> build_known_recovered_trade_keys(
    const PersistedInventoryStore& inventory_store,
    const std::map<std::string, LiveAttachmentTelemetry>& attachment_telemetry,
    std::string_view account_id) {

    std::set<std::string> keys;
    for (const auto& [attachment_key, fills] : inventory_store.fill_history_by_attachment) {
        if (attachment_key_account_id(attachment_key) != account_id) {
            continue;
        }

        for (const auto& fill : fills) {
            keys.insert(recovered_trade_dedupe_key(fill));
        }
    }

    for (const auto& [_, attachment] : attachment_telemetry) {
        if (attachment.snapshot.account_id != account_id) {
            continue;
        }

        for (const auto& order : attachment.closed_orders) {
            if (order.filled_volume <= 0 || order.filled_price <= 0.0) {
                continue;
            }

            keys.insert(recovered_trade_dedupe_key(order));
        }
    }

    return keys;
}

RecoveredTradeReplayMap recover_live_trades_from_broker(
    std::map<std::string, LiveAccountState>& accounts,
    const PersistedStrategyStateStore& persisted_strategy_state_store,
    const PersistedInventoryStore& persisted_inventory_store,
    std::map<std::string, LiveAttachmentTelemetry>& attachment_telemetry,
    std::vector<std::string>& telemetry_warnings) {

    RecoveredTradeReplayMap recovered_trades_by_attachment;

    const auto recovered_binding_lookup = build_recovered_trade_binding_lookup(
        persisted_strategy_state_store,
        persisted_inventory_store);

    for (auto& [account_id, account] : accounts) {
        if (account.gateway == nullptr) {
            continue;
        }

        auto known_trade_keys = build_known_recovered_trade_keys(
            persisted_inventory_store,
            attachment_telemetry,
            account_id);

        std::string error_message;
        const auto broker_trades = account.gateway->query_trades(&error_message, 15000);
        if (!error_message.empty()) {
            append_unique_warning(telemetry_warnings, "QryTrade failed for account " + account_id + ": " + error_message);
            continue;
        }

        std::size_t recovered_count = 0;
        std::size_t reattached_count = 0;
        for (auto trade : broker_trades) {
            std::string attachment_key;
            if (!trade.strategy_id.empty()) {
                attachment_key = telemetry_attachment_key(trade.strategy_id, account_id);
            }

            const RecoveredWorkingOrderBinding* recovered_binding = nullptr;
            const auto recovered_account_it = recovered_binding_lookup.find(account_id);
            if (recovered_account_it != recovered_binding_lookup.end()) {
                const auto recovered_order_it = recovered_account_it->second.find(trade.order_id);
                if (recovered_order_it != recovered_account_it->second.end()) {
                    recovered_binding = &recovered_order_it->second;
                }
            }

            if (recovered_binding != nullptr) {
                if (attachment_key.empty()) {
                    attachment_key = recovered_binding->attachment_key;
                }
                if (trade.strategy_id.empty()) {
                    trade.strategy_id = std::string(attachment_key_strategy_id(recovered_binding->attachment_key));
                }
                if (trade.client_order_id.empty()) {
                    trade.client_order_id = recovered_binding->client_order_id;
                    if (!trade.client_order_id.empty()) {
                        ++reattached_count;
                    }
                }
            }

            if (attachment_key.empty()) {
                append_unique_warning(
                    telemetry_warnings,
                    "Recovered broker trade " + trade.source_order_id + " for account " + account_id
                        + " but could not map it back to a live strategy attachment.");
                continue;
            }

            const auto telemetry_it = attachment_telemetry.find(attachment_key);
            if (telemetry_it == attachment_telemetry.end()) {
                append_unique_warning(
                    telemetry_warnings,
                    "Recovered broker trade " + trade.source_order_id + " for detached attachment " + attachment_key + '.');
                continue;
            }

            if (trade.account_id.empty()) {
                trade.account_id = account_id;
            }
            if (trade.strategy_id.empty()) {
                trade.strategy_id = telemetry_it->second.snapshot.strategy_id;
            }
            const auto open_order_it = telemetry_it->second.opened_orders_by_id.find(trade.order_id);
            if (trade.client_order_id.empty()) {
                if (open_order_it != telemetry_it->second.opened_orders_by_id.end()) {
                    trade.client_order_id = open_order_it->second.client_order_id;
                }
            }
            trade.status = open_order_it != telemetry_it->second.opened_orders_by_id.end()
                ? OrderStatus::PartiallyFilled
                : OrderStatus::Filled;

            const auto trade_key = recovered_trade_dedupe_key(trade);
            if (known_trade_keys.contains(trade_key)) {
                continue;
            }

            telemetry_it->second.closed_orders.push_back(trade);
            recovered_trades_by_attachment[attachment_key].push_back(trade);
            refresh_attachment_orders(telemetry_it->second);
            known_trade_keys.insert(trade_key);
            ++recovered_count;
        }

        if (recovered_count > 0) {
            append_unique_warning(
                telemetry_warnings,
                "Recovered " + std::to_string(recovered_count) + " broker trade fill(s) for account " + account_id
                    + "; " + std::to_string(reattached_count) + " fill(s) were reattached to a persisted client_order_id mapping.");
        }
    }

    return recovered_trades_by_attachment;
}

void replay_recovered_live_trades_to_strategies(
    const RecoveredTradeReplayMap& recovered_trades_by_attachment,
    std::vector<StrategyRuntime>& strategies) {

    for (auto& strategy : strategies) {
        const auto recovered_it = recovered_trades_by_attachment.find(
            telemetry_attachment_key(strategy.strategy_id, strategy.account_id));
        if (recovered_it == recovered_trades_by_attachment.end() || recovered_it->second.empty()) {
            continue;
        }

        auto recovered_trades = recovered_it->second;
        std::stable_sort(recovered_trades.begin(), recovered_trades.end(), [](const RuntimeOrderSnapshot& left, const RuntimeOrderSnapshot& right) {
            if (left.timestamp != right.timestamp) {
                return left.timestamp < right.timestamp;
            }
            if (left.order_id != right.order_id) {
                return left.order_id < right.order_id;
            }
            return left.source_order_id < right.source_order_id;
        });

        strategy.instance->replay_live_recovered_trades(recovered_trades, *strategy.context);
    }
}

void hydrate_live_working_orders_from_broker(
    std::map<std::string, LiveAccountState>& accounts,
    const PersistedStrategyStateStore& persisted_strategy_state_store,
    std::map<std::string, LiveAttachmentTelemetry>& attachment_telemetry,
    std::vector<std::string>& telemetry_warnings) {

    const auto recovered_lookup = build_recovered_working_order_lookup(persisted_strategy_state_store);

    for (auto& [account_id, account] : accounts) {
        if (account.gateway == nullptr) {
            continue;
        }

        std::string error_message;
        const auto working_orders = account.gateway->query_working_orders(&error_message, 15000);
        if (!error_message.empty()) {
            append_unique_warning(telemetry_warnings, "QryOrder failed for account " + account_id + ": " + error_message);
            continue;
        }

        for (auto& [attachment_key, attachment] : attachment_telemetry) {
            if (attachment.snapshot.account_id == account_id) {
                attachment.opened_orders_by_id.clear();
                refresh_attachment_orders(attachment);
            }
        }

        std::size_t recovered_count = 0;
        for (auto order : working_orders) {
            std::string attachment_key;
            if (!order.strategy_id.empty()) {
                attachment_key = telemetry_attachment_key(order.strategy_id, account_id);
            }

            const auto recovered_account_it = recovered_lookup.find(account_id);
            if (recovered_account_it != recovered_lookup.end()) {
                const auto recovered_order_it = recovered_account_it->second.find(order.order_id);
                if (recovered_order_it != recovered_account_it->second.end()) {
                    if (attachment_key.empty()) {
                        attachment_key = recovered_order_it->second.attachment_key;
                    }
                    if (order.strategy_id.empty()) {
                        order.strategy_id = std::string(attachment_key_strategy_id(recovered_order_it->second.attachment_key));
                    }
                    if (order.client_order_id.empty()) {
                        order.client_order_id = recovered_order_it->second.client_order_id;
                    }
                }
            }

            if (attachment_key.empty()) {
                append_unique_warning(
                    telemetry_warnings,
                    "Recovered broker working order " + order.order_id + " for account " + account_id
                        + " but could not map it back to a live strategy attachment.");
                continue;
            }

            const auto telemetry_it = attachment_telemetry.find(attachment_key);
            if (telemetry_it == attachment_telemetry.end()) {
                append_unique_warning(
                    telemetry_warnings,
                    "Recovered broker working order " + order.order_id + " for detached attachment " + attachment_key + '.');
                continue;
            }

            telemetry_it->second.opened_orders_by_id[order.order_id] = order;
            refresh_attachment_orders(telemetry_it->second);

            if (!order.client_order_id.empty()) {
                account.gateway->seed_recovered_order_mapping(order);
                ++recovered_count;
            }
        }

        if (!working_orders.empty()) {
            append_unique_warning(
                telemetry_warnings,
                "Recovered " + std::to_string(working_orders.size()) + " broker working order(s) for account " + account_id
                    + "; " + std::to_string(recovered_count) + " order(s) were reattached to persisted client_order_id mappings.");
        }
    }
}

[[nodiscard]] bool position_state_is_flat(const PositionState& position) {
    return position.long_today_quantity == 0
        && position.long_yesterday_quantity == 0
        && position.short_today_quantity == 0
        && position.short_yesterday_quantity == 0;
}

void prune_flat_positions(std::map<std::string, PositionState>& positions) {
    for (auto it = positions.begin(); it != positions.end();) {
        if (position_state_is_flat(it->second)) {
            it = positions.erase(it);
        } else {
            ++it;
        }
    }
}

void accumulate_position_state(PositionState& target, const PositionState& source) {
    target.long_today_average_price = weighted_average_price(
        target.long_today_quantity,
        target.long_today_average_price,
        source.long_today_quantity,
        source.long_today_average_price);
    target.long_today_quantity += source.long_today_quantity;

    target.long_yesterday_average_price = weighted_average_price(
        target.long_yesterday_quantity,
        target.long_yesterday_average_price,
        source.long_yesterday_quantity,
        source.long_yesterday_average_price);
    target.long_yesterday_quantity += source.long_yesterday_quantity;

    target.short_today_average_price = weighted_average_price(
        target.short_today_quantity,
        target.short_today_average_price,
        source.short_today_quantity,
        source.short_today_average_price);
    target.short_today_quantity += source.short_today_quantity;

    target.short_yesterday_average_price = weighted_average_price(
        target.short_yesterday_quantity,
        target.short_yesterday_average_price,
        source.short_yesterday_quantity,
        source.short_yesterday_average_price);
    target.short_yesterday_quantity += source.short_yesterday_quantity;
}

[[nodiscard]] bool position_quantities_match(const PositionState& left, const PositionState& right) {
    return left.long_today_quantity == right.long_today_quantity
        && left.long_yesterday_quantity == right.long_yesterday_quantity
        && left.short_today_quantity == right.short_today_quantity
        && left.short_yesterday_quantity == right.short_yesterday_quantity;
}

std::map<std::string, PositionState> aggregate_inventory_store_positions_for_account(
    const PersistedInventoryStore& store,
    std::string_view account_id) {

    std::map<std::string, PositionState> aggregate;
    for (const auto& [attachment_key, positions] : store.attachment_positions) {
        if (attachment_key_account_id(attachment_key) != account_id) {
            continue;
        }
        for (const auto& [instrument, position] : positions) {
            accumulate_position_state(aggregate[instrument], position);
        }
    }
    return aggregate;
}

std::map<std::string, PositionState> aggregate_attachment_positions_for_account(
    const std::map<std::string, LiveAttachmentTelemetry>& attachment_telemetry,
    std::string_view account_id) {

    std::map<std::string, PositionState> aggregate;
    for (const auto& [_, attachment] : attachment_telemetry) {
        if (attachment.snapshot.account_id != account_id) {
            continue;
        }
        for (const auto& [instrument, position] : attachment.positions) {
            accumulate_position_state(aggregate[instrument], position);
        }
    }
    return aggregate;
}

[[nodiscard]] bool aggregate_positions_match(
    const std::map<std::string, PositionState>& left,
    const std::map<std::string, PositionState>& right) {

    std::set<std::string> instruments;
    for (const auto& [instrument, _] : left) {
        instruments.insert(instrument);
    }
    for (const auto& [instrument, _] : right) {
        instruments.insert(instrument);
    }

    for (const auto& instrument : instruments) {
        const auto left_it = left.find(instrument);
        const auto right_it = right.find(instrument);
        const PositionState empty_state {};
        const auto& left_position = left_it == left.end() ? empty_state : left_it->second;
        const auto& right_position = right_it == right.end() ? empty_state : right_it->second;
        if (!position_quantities_match(left_position, right_position)) {
            return false;
        }
    }

    return true;
}

std::string format_position_bucket_summary(const PositionState& position) {
    std::ostringstream output;
    output << "LT=" << position.long_today_quantity
           << ",LY=" << position.long_yesterday_quantity
           << ",ST=" << position.short_today_quantity
           << ",SY=" << position.short_yesterday_quantity;
    return output.str();
}

std::string build_aggregate_mismatch_summary(
    const std::map<std::string, PositionState>& persisted_positions,
    const std::map<std::string, PositionState>& broker_positions) {

    std::set<std::string> instruments;
    for (const auto& [instrument, _] : persisted_positions) {
        instruments.insert(instrument);
    }
    for (const auto& [instrument, _] : broker_positions) {
        instruments.insert(instrument);
    }

    std::ostringstream output;
    bool first = true;
    const PositionState empty_state {};
    for (const auto& instrument : instruments) {
        const auto persisted_it = persisted_positions.find(instrument);
        const auto broker_it = broker_positions.find(instrument);
        const auto& persisted = persisted_it == persisted_positions.end() ? empty_state : persisted_it->second;
        const auto& broker = broker_it == broker_positions.end() ? empty_state : broker_it->second;
        if (position_quantities_match(persisted, broker)) {
            continue;
        }

        if (!first) {
            output << " | ";
        }
        first = false;
        output << instrument << " persisted{" << format_position_bucket_summary(persisted)
               << "} broker{" << format_position_bucket_summary(broker) << '}';
    }

    return output.str();
}

std::string inventory_state_section_name(std::string_view strategy_id, std::string_view account_id, std::string_view instrument) {
    return "strategy_inventory_state." + std::string(strategy_id) + '.' + std::string(account_id) + '.' + std::string(instrument);
}

std::string inventory_adjustment_section_name(std::string_view adjustment_id) {
    return "inventory_adjustment." + std::string(adjustment_id);
}

std::string applied_inventory_adjustment_section_name(std::string_view adjustment_id) {
    return "inventory_adjustments_applied." + std::string(adjustment_id);
}

std::string broker_position_section_name(std::string_view account_id, std::string_view instrument) {
    return "broker_position_snapshots." + std::string(account_id) + '.' + std::string(instrument);
}

std::string strategy_fill_section_name(std::string_view strategy_id, std::string_view account_id, std::size_t index) {
    return "strategy_fill_ledger." + std::string(strategy_id) + '.' + std::string(account_id) + '.' + std::to_string(index);
}

std::string reconciliation_run_section_name(std::string_view account_id) {
    return "reconciliation_runs." + std::string(account_id);
}

std::optional<double> read_optional_double(const IniFile& ini, const std::string& section, const std::string& key) {
    const auto value = ini.get(section, key);
    if (value.empty()) {
        return std::nullopt;
    }
    return std::stod(value);
}

void ensure_strategy_inventory_adjustments_template(const std::filesystem::path& config_path) {
    const auto adjustments_path = strategy_inventory_adjustments_path(config_path);
    if (std::filesystem::exists(adjustments_path)) {
        return;
    }

    std::error_code error_code;
    std::filesystem::create_directories(adjustments_path.parent_path(), error_code);

    std::ofstream file(adjustments_path, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        return;
    }

    file
        << "; Manual inventory adjustments are applied once per section id.\n"
        << "; After an adjustment is applied, the runtime records it in strategy_inventory_store.ini\n"
        << "; and will not apply the same adjustment id again on the next restart.\n"
        << "; Positive deltas that increase a bucket should also provide the matching *_average_price.\n"
        << "; Example:\n"
        << "; [inventory_adjustment.example_external_manual_seed]\n"
        << "; enabled=false\n"
        << "; account_id=simnow\n"
        << "; strategy_id=external_manual\n"
        << "; instrument=ag2606\n"
        << "; exchange=SHFE\n"
        << "; operator_id=alice\n"
        << "; reason_code=manual_trade_import\n"
        << "; reason_text=Seed broker-held overnight inventory before reconciliation\n"
        << "; long_yesterday_delta=1\n"
        << "; long_yesterday_average_price=5120\n";
}

std::vector<ManualInventoryAdjustment> read_manual_inventory_adjustments(const std::filesystem::path& config_path) {
    ensure_strategy_inventory_adjustments_template(config_path);

    const auto adjustments_path = strategy_inventory_adjustments_path(config_path);
    if (!std::filesystem::exists(adjustments_path)) {
        return {};
    }

    const auto ini = IniFile::parse(adjustments_path);
    std::vector<ManualInventoryAdjustment> adjustments;
    for (const auto& section : ini.sections_with_prefix("inventory_adjustment.")) {
        if (!ini.get_bool(section, "enabled", true)) {
            continue;
        }

        ManualInventoryAdjustment adjustment;
        adjustment.adjustment_id = trim_copy(section.substr(std::string("inventory_adjustment.").size()));
        adjustment.account_id = trim_copy(ini.get(section, "account_id"));
        adjustment.strategy_id = trim_copy(ini.get(section, "strategy_id"));
        adjustment.instrument = upper_copy(trim_copy(ini.get(section, "instrument")));
        adjustment.exchange = normalize_exchange_code(ini.get(section, "exchange"));
        adjustment.operator_id = trim_copy(ini.get(section, "operator_id"));
        adjustment.reason_code = trim_copy(ini.get(section, "reason_code"));
        adjustment.reason_text = trim_copy(ini.get(section, "reason_text"));

        if (adjustment.adjustment_id.empty() || adjustment.account_id.empty() || adjustment.strategy_id.empty()
            || adjustment.instrument.empty() || adjustment.operator_id.empty() || adjustment.reason_code.empty()) {
            throw std::runtime_error(
                "Inventory adjustment section [" + section + "] is missing one of the required fields: account_id, strategy_id, instrument, operator_id, reason_code.");
        }

        adjustment.long_today.delta = ini.get_int(section, "long_today_delta", 0);
        adjustment.long_today.average_price = read_optional_double(ini, section, "long_today_average_price");
        adjustment.long_yesterday.delta = ini.get_int(section, "long_yesterday_delta", 0);
        adjustment.long_yesterday.average_price = read_optional_double(ini, section, "long_yesterday_average_price");
        adjustment.short_today.delta = ini.get_int(section, "short_today_delta", 0);
        adjustment.short_today.average_price = read_optional_double(ini, section, "short_today_average_price");
        adjustment.short_yesterday.delta = ini.get_int(section, "short_yesterday_delta", 0);
        adjustment.short_yesterday.average_price = read_optional_double(ini, section, "short_yesterday_average_price");

        if (adjustment.long_today.delta == 0
            && adjustment.long_yesterday.delta == 0
            && adjustment.short_today.delta == 0
            && adjustment.short_yesterday.delta == 0) {
            continue;
        }

        adjustments.push_back(std::move(adjustment));
    }

    return adjustments;
}

std::string find_snapshot_exchange_for_instrument(const StrategyAttachmentSnapshot& snapshot, std::string_view instrument) {
    const auto matches_instrument = [instrument](const RuntimeOrderSnapshot& order) {
        return order.instrument == instrument && !trim_copy(order.exchange).empty();
    };

    const auto opened_it = std::find_if(snapshot.opened_orders.begin(), snapshot.opened_orders.end(), matches_instrument);
    if (opened_it != snapshot.opened_orders.end()) {
        return opened_it->exchange;
    }

    const auto closed_it = std::find_if(snapshot.closed_orders.begin(), snapshot.closed_orders.end(), matches_instrument);
    if (closed_it != snapshot.closed_orders.end()) {
        return closed_it->exchange;
    }

    return {};
}

void append_position_state_section(std::ostringstream& output,
                                   std::string_view section_name,
                                   std::string_view account_id,
                                   std::string_view strategy_id,
                                   std::string_view instrument,
                                   std::string_view exchange,
                                   const PositionState& position) {
    output << '[' << section_name << "]\n";
    output << "account_id=" << account_id << "\n";
    if (!strategy_id.empty()) {
        output << "strategy_id=" << strategy_id << "\n";
    }
    output << "instrument=" << instrument << "\n";
    output << "exchange=" << exchange << "\n";
    output << "long_today_quantity=" << position.long_today_quantity << "\n";
    output << "long_today_average_price=" << format_price(position.long_today_average_price) << "\n";
    output << "long_yesterday_quantity=" << position.long_yesterday_quantity << "\n";
    output << "long_yesterday_average_price=" << format_price(position.long_yesterday_average_price) << "\n";
    output << "short_today_quantity=" << position.short_today_quantity << "\n";
    output << "short_today_average_price=" << format_price(position.short_today_average_price) << "\n";
    output << "short_yesterday_quantity=" << position.short_yesterday_quantity << "\n";
    output << "short_yesterday_average_price=" << format_price(position.short_yesterday_average_price) << "\n\n";
}

PersistedInventoryStore read_strategy_inventory_store(const std::filesystem::path& config_path) {
    PersistedInventoryStore store;
    const auto store_path = strategy_inventory_store_path(config_path);
    if (!std::filesystem::exists(store_path)) {
        return store;
    }

    const auto ini = IniFile::parse(store_path);
    for (const auto& section : ini.sections_with_prefix("strategy_inventory_state.")) {
        const auto tail = section.substr(std::string("strategy_inventory_state.").size());
        const auto parts = split_tokenized(tail, '.');
        if (parts.size() < 3) {
            continue;
        }

        PositionState position;
        position.long_today_quantity = ini.get_int(section, "long_today_quantity", 0);
        position.long_today_average_price = ini.get_double(section, "long_today_average_price", 0.0);
        position.long_yesterday_quantity = ini.get_int(section, "long_yesterday_quantity", 0);
        position.long_yesterday_average_price = ini.get_double(section, "long_yesterday_average_price", 0.0);
        position.short_today_quantity = ini.get_int(section, "short_today_quantity", 0);
        position.short_today_average_price = ini.get_double(section, "short_today_average_price", 0.0);
        position.short_yesterday_quantity = ini.get_int(section, "short_yesterday_quantity", 0);
        position.short_yesterday_average_price = ini.get_double(section, "short_yesterday_average_price", 0.0);
        store.attachment_positions[telemetry_attachment_key(parts[0], parts[1])][parts[2]] = position;
    }

    for (auto& [_, positions] : store.attachment_positions) {
        prune_flat_positions(positions);
    }
    for (auto it = store.attachment_positions.begin(); it != store.attachment_positions.end();) {
        if (it->second.empty()) {
            it = store.attachment_positions.erase(it);
        } else {
            ++it;
        }
    }

    for (const auto& section : ini.sections_with_prefix("strategy_fill_ledger.")) {
        const auto tail = section.substr(std::string("strategy_fill_ledger.").size());
        const auto parts = split_tokenized(tail, '.');
        if (parts.size() < 3) {
            continue;
        }

        RuntimeOrderSnapshot order;
        order.account_id = trim_copy(ini.get(section, "account_id", parts[1]));
        order.strategy_id = trim_copy(ini.get(section, "strategy_id", parts[0]));
        order.instrument = trim_copy(ini.get(section, "instrument"));
        order.exchange = normalize_exchange_code(ini.get(section, "exchange"));
        order.client_order_id = trim_copy(ini.get(section, "client_order_id"));
        order.order_id = trim_copy(ini.get(section, "broker_order_id"));
        order.source_order_id = trim_copy(ini.get(section, "broker_trade_id"));
        order.side = side_from_text(ini.get(section, "side"));
        order.offset = offset_from_text(ini.get(section, "offset"));
        order.requested_volume = ini.get_int(section, "fill_quantity", 0);
        order.filled_volume = ini.get_int(section, "fill_quantity", 0);
        order.filled_price = ini.get_double(section, "fill_price", 0.0);
        order.status = OrderStatus::Filled;
        order.message = "persisted fill ledger";
        order.timestamp = trim_copy(ini.get(section, "fill_timestamp"));
        store.fill_history_by_attachment[telemetry_attachment_key(order.strategy_id, order.account_id)].push_back(std::move(order));
    }

    for (const auto& section : ini.sections_with_prefix("broker_position_snapshots.")) {
        const auto tail = section.substr(std::string("broker_position_snapshots.").size());
        const auto parts = split_tokenized(tail, '.');
        if (parts.size() < 2) {
            continue;
        }

        PositionState position;
        position.long_today_quantity = ini.get_int(section, "long_today_quantity", 0);
        position.long_today_average_price = ini.get_double(section, "long_today_average_price", 0.0);
        position.long_yesterday_quantity = ini.get_int(section, "long_yesterday_quantity", 0);
        position.long_yesterday_average_price = ini.get_double(section, "long_yesterday_average_price", 0.0);
        position.short_today_quantity = ini.get_int(section, "short_today_quantity", 0);
        position.short_today_average_price = ini.get_double(section, "short_today_average_price", 0.0);
        position.short_yesterday_quantity = ini.get_int(section, "short_yesterday_quantity", 0);
        position.short_yesterday_average_price = ini.get_double(section, "short_yesterday_average_price", 0.0);
        store.broker_positions_by_account[parts[0]][parts[1]] = position;
    }

    for (const auto& section : ini.sections_with_prefix("inventory_adjustments_applied.")) {
        AppliedInventoryAdjustment adjustment;
        adjustment.adjustment_id = trim_copy(ini.get(section, "adjustment_id", section.substr(std::string("inventory_adjustments_applied.").size())));
        if (adjustment.adjustment_id.empty()) {
            continue;
        }
        adjustment.account_id = trim_copy(ini.get(section, "account_id"));
        adjustment.strategy_id = trim_copy(ini.get(section, "strategy_id"));
        adjustment.instrument = upper_copy(trim_copy(ini.get(section, "instrument")));
        adjustment.exchange = normalize_exchange_code(ini.get(section, "exchange"));
        adjustment.operator_id = trim_copy(ini.get(section, "operator_id"));
        adjustment.reason_code = trim_copy(ini.get(section, "reason_code"));
        adjustment.reason_text = trim_copy(ini.get(section, "reason_text"));
        adjustment.applied_at = trim_copy(ini.get(section, "applied_at"));
        store.applied_adjustments[adjustment.adjustment_id] = std::move(adjustment);
    }

    return store;
}

void apply_inventory_bucket_adjustment(
    int& quantity,
    double& average_price,
    const std::string& adjustment_id,
    std::string_view bucket_label,
    const InventoryBucketAdjustment& adjustment) {

    if (adjustment.delta == 0) {
        return;
    }

    if (adjustment.delta > 0) {
        if (!adjustment.average_price.has_value() || *adjustment.average_price < 0.0) {
            throw std::runtime_error(
                "Inventory adjustment [" + adjustment_id + "] increases " + std::string(bucket_label)
                + " but does not provide a valid *_average_price.");
        }
        average_price = weighted_average_price(quantity, average_price, adjustment.delta, *adjustment.average_price);
        quantity += adjustment.delta;
        return;
    }

    const int next_quantity = quantity + adjustment.delta;
    if (next_quantity < 0) {
        throw std::runtime_error(
            "Inventory adjustment [" + adjustment_id + "] would make " + std::string(bucket_label)
            + " negative.");
    }
    quantity = next_quantity;
    if (quantity == 0) {
        average_price = 0.0;
    }
}

std::size_t apply_manual_inventory_adjustments(
    const std::filesystem::path& config_path,
    PersistedInventoryStore& store,
    const std::vector<ManualInventoryAdjustment>& adjustments,
    std::vector<std::string>& telemetry_warnings) {

    std::size_t applied_count = 0;
    for (const auto& adjustment : adjustments) {
        if (store.applied_adjustments.contains(adjustment.adjustment_id)) {
            continue;
        }

        auto& positions = store.attachment_positions[telemetry_attachment_key(adjustment.strategy_id, adjustment.account_id)];
        const auto position_it = std::find_if(positions.begin(), positions.end(), [&adjustment](const auto& entry) {
            return upper_copy(entry.first) == upper_copy(adjustment.instrument);
        });
        auto resolved_position_it = position_it;
        if (resolved_position_it == positions.end()) {
            resolved_position_it = positions.emplace(adjustment.instrument, PositionState {}).first;
        }
        auto& position = resolved_position_it->second;
        apply_inventory_bucket_adjustment(
            position.long_today_quantity,
            position.long_today_average_price,
            adjustment.adjustment_id,
            "long_today",
            adjustment.long_today);
        apply_inventory_bucket_adjustment(
            position.long_yesterday_quantity,
            position.long_yesterday_average_price,
            adjustment.adjustment_id,
            "long_yesterday",
            adjustment.long_yesterday);
        apply_inventory_bucket_adjustment(
            position.short_today_quantity,
            position.short_today_average_price,
            adjustment.adjustment_id,
            "short_today",
            adjustment.short_today);
        apply_inventory_bucket_adjustment(
            position.short_yesterday_quantity,
            position.short_yesterday_average_price,
            adjustment.adjustment_id,
            "short_yesterday",
            adjustment.short_yesterday);

        if (position_state_is_flat(position)) {
            positions.erase(resolved_position_it);
        }
        if (positions.empty()) {
            store.attachment_positions.erase(telemetry_attachment_key(adjustment.strategy_id, adjustment.account_id));
        }

        store.applied_adjustments[adjustment.adjustment_id] = AppliedInventoryAdjustment {
            .adjustment_id = adjustment.adjustment_id,
            .account_id = adjustment.account_id,
            .strategy_id = adjustment.strategy_id,
            .instrument = adjustment.instrument,
            .exchange = adjustment.exchange,
            .operator_id = adjustment.operator_id,
            .reason_code = adjustment.reason_code,
            .reason_text = adjustment.reason_text,
            .applied_at = current_timestamp(),
        };
        ++applied_count;
    }

    if (applied_count > 0) {
        append_unique_warning(
            telemetry_warnings,
            "Applied " + std::to_string(applied_count) + " manual inventory adjustment(s) from "
                + strategy_inventory_adjustments_path(config_path).string() + ".");
    }

    return applied_count;
}

std::size_t count_applied_inventory_adjustments_for_account(const PersistedInventoryStore& store, std::string_view account_id) {
    std::size_t count = 0;
    for (const auto& [_, adjustment] : store.applied_adjustments) {
        if (adjustment.account_id == account_id) {
            ++count;
        }
    }
    return count;
}

void sync_inventory_store_from_live_attachments(
    PersistedInventoryStore& store,
    const std::map<std::string, LiveAttachmentTelemetry>& attachments) {

    for (const auto& [attachment_key, attachment] : attachments) {
        auto& positions = store.attachment_positions[attachment_key];
        positions = attachment.positions;
        prune_flat_positions(positions);
        if (positions.empty()) {
            store.attachment_positions.erase(attachment_key);
        }
    }
}

void validate_detached_persisted_inventory(
    const PersistedInventoryStore& store,
    const std::map<std::string, LiveAttachmentTelemetry>& attachment_telemetry) {

    for (const auto& [attachment_key, positions] : store.attachment_positions) {
        if (positions.empty()) {
            continue;
        }

        const auto strategy_id = attachment_key_strategy_id(attachment_key);
        if (strategy_id == "external_manual") {
            continue;
        }
        if (attachment_telemetry.contains(std::string(attachment_key))) {
            continue;
        }

        throw std::runtime_error(
            "The persisted inventory store contains non-flat positions for detached strategy "
            + std::string(strategy_id) + " on account " + std::string(attachment_key_account_id(attachment_key))
            + ". Reattach that strategy or move the inventory to strategy_id=external_manual before starting live mode.");
    }
}

void seed_attachment_telemetry_from_inventory_store(
    const PersistedInventoryStore& store,
    std::map<std::string, LiveAttachmentTelemetry>& attachment_telemetry,
    std::vector<std::string>& telemetry_warnings) {

    bool seeded_any = false;
    for (auto& [attachment_key, attachment] : attachment_telemetry) {
        const auto store_it = store.attachment_positions.find(attachment_key);
        if (store_it == store.attachment_positions.end()) {
            continue;
        }

        attachment.positions = store_it->second;
        refresh_attachment_positions(attachment);
        append_unique_warning(attachment.snapshot.warnings, "Recovered per-strategy inventory from the persisted inventory store before broker reconciliation.");
        seeded_any = true;
    }

    if (seeded_any) {
        append_unique_warning(telemetry_warnings, "Recovered per-strategy inventory from the persisted inventory store before broker reconciliation.");
    }
}

void write_strategy_inventory_store(
    const std::filesystem::path& config_path,
    const PersistedInventoryStore& store,
    const std::map<std::string, LiveAccountState>& accounts,
    const std::map<std::string, LiveAttachmentTelemetry>& attachments) {

    const auto store_path = strategy_inventory_store_path(config_path);
    std::error_code error_code;
    std::filesystem::create_directories(store_path.parent_path(), error_code);

    std::ostringstream output;
    output << "[strategy_inventory_store]\n";
    output << "updated_at=" << current_timestamp() << "\n\n";

    for (const auto& [attachment_key, positions] : store.attachment_positions) {
        const auto account_id = attachment_key_account_id(attachment_key);
        const auto strategy_id = attachment_key_strategy_id(attachment_key);
        const auto attachment_it = attachments.find(std::string(attachment_key));
        for (const auto& [instrument, position] : positions) {
            append_position_state_section(
                output,
                inventory_state_section_name(strategy_id, account_id, instrument),
                account_id,
                strategy_id,
                instrument,
                attachment_it == attachments.end() ? std::string {} : find_snapshot_exchange_for_instrument(attachment_it->second.snapshot, instrument),
                position);
        }
    }

    for (const auto& [adjustment_id, adjustment] : store.applied_adjustments) {
        output << '[' << applied_inventory_adjustment_section_name(adjustment_id) << "]\n";
        output << "adjustment_id=" << adjustment.adjustment_id << "\n";
        output << "account_id=" << adjustment.account_id << "\n";
        output << "strategy_id=" << adjustment.strategy_id << "\n";
        output << "instrument=" << adjustment.instrument << "\n";
        output << "exchange=" << adjustment.exchange << "\n";
        output << "operator_id=" << adjustment.operator_id << "\n";
        output << "reason_code=" << adjustment.reason_code << "\n";
        output << "reason_text=" << adjustment.reason_text << "\n";
        output << "applied_at=" << adjustment.applied_at << "\n\n";
    }

    for (const auto& [_, attachment] : attachments) {
        std::size_t fill_index = 1;
        for (const auto& order : attachment.snapshot.closed_orders) {
            if (order.filled_volume <= 0 || order.filled_price <= 0.0) {
                continue;
            }

            output << '[' << strategy_fill_section_name(attachment.snapshot.strategy_id, attachment.snapshot.account_id, fill_index++) << "]\n";
            output << "account_id=" << attachment.snapshot.account_id << "\n";
            output << "strategy_id=" << attachment.snapshot.strategy_id << "\n";
            output << "instrument=" << order.instrument << "\n";
            output << "exchange=" << order.exchange << "\n";
            output << "client_order_id=" << order.client_order_id << "\n";
            output << "broker_order_id=" << order.order_id << "\n";
            output << "broker_trade_id=" << order.source_order_id << "\n";
            output << "side=" << to_string(order.side) << "\n";
            output << "offset=" << to_string(order.offset) << "\n";
            output << "fill_quantity=" << order.filled_volume << "\n";
            output << "fill_price=" << format_price(order.filled_price) << "\n";
            output << "fill_timestamp=" << order.timestamp << "\n\n";
        }
    }

    for (const auto& [account_id, account] : accounts) {
        for (const auto& [instrument, position] : account.positions) {
            append_position_state_section(
                output,
                broker_position_section_name(account_id, instrument),
                account_id,
                std::string_view {},
                instrument,
                std::string_view {},
                position);
        }

        const auto inventory_aggregate = aggregate_inventory_store_positions_for_account(store, account_id);
        const bool aggregate_match = aggregate_positions_match(inventory_aggregate, account.positions);
        output << '[' << reconciliation_run_section_name(account_id) << "]\n";
        output << "account_id=" << account_id << "\n";
        output << "broker_snapshot_timestamp=" << current_timestamp() << "\n";
        output << "aggregate_match=" << (aggregate_match ? "true" : "false") << "\n";
        output << "applied_adjustment_count=" << count_applied_inventory_adjustments_for_account(store, account_id) << "\n";
        const auto mismatch_summary = build_aggregate_mismatch_summary(inventory_aggregate, account.positions);
        if (!mismatch_summary.empty()) {
            output << "mismatch_summary=" << mismatch_summary << "\n";
        }
        output << "manual_adjustments_path=" << strategy_inventory_adjustments_path(config_path).string() << "\n\n";
    }

    std::ofstream file(store_path, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        throw std::runtime_error("Unable to write strategy inventory store: " + store_path.string());
    }
    file << output.str();
}

void hydrate_live_positions_from_broker(
    std::map<std::string, LiveAccountState>& accounts,
    const std::vector<StrategyRuntime>& strategies,
    std::map<std::string, LiveAttachmentTelemetry>& attachment_telemetry,
    std::vector<std::string>& telemetry_warnings,
    const PersistedInventoryStore& persisted_inventory_store) {

    std::map<std::string, std::vector<std::string>> strategies_by_account;
    for (const auto& strategy : strategies) {
        strategies_by_account[strategy.account_id].push_back(strategy.strategy_id);
    }

    for (auto& [account_id, account] : accounts) {
        if (account.gateway == nullptr) {
            continue;
        }

        std::string error_message;
        const auto broker_positions = account.gateway->query_positions(&error_message, 15000);
        if (!error_message.empty()) {
            append_unique_warning(telemetry_warnings, "QryInvestorPosition failed for account " + account_id + ": " + error_message);
            continue;
        }

        if (broker_positions.empty()) {
            account.positions.clear();
            refresh_live_account_net_positions(account);
            const auto persisted_aggregate = aggregate_inventory_store_positions_for_account(persisted_inventory_store, account_id);
            if (!persisted_aggregate.empty()) {
                throw std::runtime_error(
                    "Broker reported no live positions for account " + account_id
                    + ", but the persisted per-strategy inventory store is non-flat. Repair the inventory store before restarting live strategies.");
            }
            continue;
        }

        std::string detail_error_message;
        const auto broker_position_details = account.gateway->query_position_details(&detail_error_message, 15000);
        std::unordered_map<std::string, BrokerPositionDetailAggregate> detail_aggregates;
        if (detail_error_message.empty()) {
            for (const auto& detail : broker_position_details) {
                accumulate_broker_position_detail(detail_aggregates[broker_position_key(detail.exchange, detail.instrument)], detail);
            }
        }

        account.positions.clear();
        std::vector<std::string> position_detail_warnings;
        for (const auto& broker_position : broker_positions) {
            auto& position = account.positions[broker_position.instrument];
            seed_position_from_broker_snapshot(position, broker_position);
            const auto detail_it = detail_aggregates.find(broker_position_key(broker_position.exchange, broker_position.instrument));
            seed_position_average_prices_from_broker_details(
                position,
                broker_position,
                detail_it == detail_aggregates.end() ? nullptr : &detail_it->second,
                account_id,
                position_detail_warnings);
        }
        refresh_live_account_net_positions(account);

        for (const auto& warning : position_detail_warnings) {
            append_unique_warning(telemetry_warnings, warning);
        }

        const auto strategy_ids_it = strategies_by_account.find(account_id);
        const std::size_t strategy_count = strategy_ids_it == strategies_by_account.end() ? 0 : strategy_ids_it->second.size();
        const auto persisted_aggregate = aggregate_inventory_store_positions_for_account(persisted_inventory_store, account_id);
        if (!persisted_aggregate.empty()) {
            if (aggregate_positions_match(persisted_aggregate, account.positions)) {
                const std::string recovery_summary = "Recovered per-strategy live inventory for account " + account_id
                    + " from the persisted inventory store/manual adjustments and reconciled it against broker account totals.";
                append_unique_warning(telemetry_warnings, recovery_summary);
                if (strategy_ids_it != strategies_by_account.end()) {
                    for (const auto& strategy_id : strategy_ids_it->second) {
                        const auto telemetry_it = attachment_telemetry.find(telemetry_attachment_key(strategy_id, account_id));
                        if (telemetry_it != attachment_telemetry.end()) {
                            append_unique_warning(telemetry_it->second.snapshot.warnings, recovery_summary);
                            for (const auto& warning : position_detail_warnings) {
                                append_unique_warning(telemetry_it->second.snapshot.warnings, warning);
                            }
                            refresh_attachment_positions(telemetry_it->second);
                        }
                    }
                }
                continue;
            }

            throw std::runtime_error(
                "Broker positions for account " + account_id
                + " do not reconcile with the persisted per-strategy inventory store. "
                + build_aggregate_mismatch_summary(persisted_aggregate, account.positions)
                + ". Repair the inventory store or apply manual adjustments before starting live strategies on this account.");
        }

        if (strategy_count != 1) {
            throw std::runtime_error(
                "Broker positions for shared account " + account_id
                + " are non-flat, but no reconciled persisted per-strategy inventory was available. Seed runtime/strategy_inventory_store.ini or add runtime/strategy_inventory_adjustments.ini before starting live strategies on this account.");
        }

        const auto& strategy_id = strategy_ids_it->second.front();
        const auto telemetry_it = attachment_telemetry.find(telemetry_attachment_key(strategy_id, account_id));
        if (!detail_error_message.empty()) {
            const std::string detail_warning = "QryInvestorPositionDetail failed for account " + account_id + ": " + detail_error_message;
            append_unique_warning(telemetry_warnings, detail_warning);
            if (telemetry_it != attachment_telemetry.end()) {
                append_unique_warning(telemetry_it->second.snapshot.warnings, detail_warning);
            }
        }

        if (telemetry_it == attachment_telemetry.end()) {
            continue;
        }

        for (auto& [_, position] : telemetry_it->second.positions) {
            position = PositionState {};
        }
        for (const auto& [instrument, position] : account.positions) {
            telemetry_it->second.positions[instrument] = position;
        }
        for (const auto& warning : position_detail_warnings) {
            append_unique_warning(telemetry_it->second.snapshot.warnings, warning);
        }
        refresh_attachment_positions(telemetry_it->second);

        const std::string seed_summary = detail_error_message.empty()
            ? "Initial live positions were seeded from broker queries. Today/yesterday quantities came from QryInvestorPosition, and average price was backfilled from QryInvestorPositionDetail when available. Closed-order history and realized PnL prior to process start are still not backfilled."
            : "Initial live positions were seeded from QryInvestorPosition. Today/yesterday quantities are available, but average price from broker-held positions prior to process start could not be fully recovered because QryInvestorPositionDetail was unavailable.";
        append_unique_warning(telemetry_warnings, seed_summary);
        append_unique_warning(telemetry_it->second.snapshot.warnings, seed_summary);
    }
}

void update_live_snapshot(LiveAccountState& account, const OrderEvent& event, int fill_delta) {
    if (fill_delta <= 0) {
        return;
    }

    auto& position = account.positions[event.instrument];
    OrderRequest synthetic_request;
    synthetic_request.account_id = event.account_id;
    synthetic_request.strategy_id = event.strategy_id;
    synthetic_request.client_order_id = event.client_order_id;
    synthetic_request.instrument = event.instrument;
    synthetic_request.exchange = event.exchange;
    synthetic_request.side = event.side;
    synthetic_request.offset = event.offset;
    synthetic_request.price_type = PriceType::Market;
    synthetic_request.immediate_or_cancel = false;
    synthetic_request.limit_price = event.limit_price;
    synthetic_request.volume = fill_delta;
    const double realized = apply_fill(position, synthetic_request, event.filled_price, fill_delta);
    account.snapshot.realized_pnl += realized;
    account.snapshot.cash = account.snapshot.initial_cash + account.snapshot.realized_pnl;
    account.snapshot.net_positions[event.instrument] = net_quantity(position);
}

void append_warning_to_account_attachments(
    std::string_view account_id,
    std::string_view warning,
    std::map<std::string, LiveAttachmentTelemetry>& attachment_telemetry) {

    for (auto& [_, attachment] : attachment_telemetry) {
        if (attachment.snapshot.account_id == account_id) {
            append_unique_warning(attachment.snapshot.warnings, std::string(warning));
        }
    }
}

long long live_tick_timestamp_millis(const MarketTick& tick);

bool looks_like_retryable_market_data_connect_error(std::string_view error_message) {
    const auto normalized = lower_copy(trim_copy(error_message));
    return normalized.find("timed out waiting for ctp market-data front connection") != std::string::npos
        || normalized.find("timed out waiting for ctp market-data login") != std::string::npos
        || normalized.find("ctp market-data front disconnected") != std::string::npos
        || normalized.find("ctp front disconnected") != std::string::npos;
}

bool wait_for_initial_live_market_data(
    std::map<std::string, LiveAccountState>& accounts,
    const std::map<std::string, std::set<std::string>>& instruments_by_account,
    std::map<std::string, MarketTick>& initial_ticks_by_account,
    std::string& current_live_trading_day) {

    std::set<std::string> pending_accounts;
    for (const auto& [account_id, instruments] : instruments_by_account) {
        if (!instruments.empty()) {
            pending_accounts.insert(account_id);
        }
    }

    while (!g_stop_requested && !pending_accounts.empty()) {
        std::vector<std::string> accounts_with_first_tick;
        for (const auto& account_id : pending_accounts) {
            auto account_it = accounts.find(account_id);
            if (account_it == accounts.end()) {
                continue;
            }

            auto& account = account_it->second;
#ifdef ITRADER_ENABLE_CTP_MD
            if (account.market_data_gateway != nullptr) {
                if (!account.market_data_gateway->ready()) {
                    account.market_data_connected = false;
                    std::string error_message;
                    if (!account.market_data_gateway->ensure_ready(&error_message, 15000)) {
                        const auto trimmed_error = trim_copy(error_message);
                        if (!trimmed_error.empty()
                            && trimmed_error != "CTP market-data reconnect backoff is active"
                            && looks_like_retryable_market_data_connect_error(trimmed_error)) {
                            continue;
                        }
                        if (!trimmed_error.empty() && trimmed_error != "CTP market-data reconnect backoff is active") {
                            throw std::runtime_error("Failed to connect CTP market-data account " + account_id + ": " + trimmed_error);
                        }
                        continue;
                    }
                }
                account.market_data_connected = true;

                for (auto tick : account.market_data_gateway->drain_ticks()) {
                    tick.timestamp_ms = live_tick_timestamp_millis(tick);
                    account.latest_ticks[tick.instrument] = tick;
                    initial_ticks_by_account[account_id] = tick;
                    const auto next_trading_day = trading_day_label_from_tick(tick);
                    if (!next_trading_day.empty()) {
                        current_live_trading_day = next_trading_day;
                    }
                }

                if (initial_ticks_by_account.find(account_id) != initial_ticks_by_account.end()) {
                    accounts_with_first_tick.push_back(account_id);
                }
                continue;
            }
#endif

            const auto instruments_it = instruments_by_account.find(account_id);
            if (account.gateway == nullptr || instruments_it == instruments_by_account.end()) {
                continue;
            }
            account.market_data_connected = account.gateway->ready();

            for (const auto& instrument : instruments_it->second) {
                auto tick = account.gateway->query_tick(instrument);
                if (!tick.has_value()) {
                    continue;
                }

                tick->timestamp_ms = live_tick_timestamp_millis(*tick);
                account.latest_ticks[tick->instrument] = *tick;
                initial_ticks_by_account[account_id] = *tick;
                const auto next_trading_day = trading_day_label_from_tick(*tick);
                if (!next_trading_day.empty()) {
                    current_live_trading_day = next_trading_day;
                }
                accounts_with_first_tick.push_back(account_id);
                break;
            }
        }

        for (const auto& account_id : accounts_with_first_tick) {
            pending_accounts.erase(account_id);
            std::cout << "Received the first live tick for account " << account_id
                      << "; continuing strategy startup.\n";
        }

        if (!pending_accounts.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    }

    return pending_accounts.empty();
}

std::string live_order_event_timestamp(const LiveAccountState& account, std::string_view instrument) {
    const auto latest_tick_it = account.latest_ticks.find(std::string(instrument));
    if (latest_tick_it != account.latest_ticks.end()) {
        const auto timestamp = trim_copy(latest_tick_it->second.timestamp);
        if (!timestamp.empty()) {
            return timestamp;
        }
    }

    return current_timestamp();
}

double dry_run_fill_price(const LiveAccountState& account, const OrderRequest& request) {
    if (request.limit_price > 0.0) {
        return request.limit_price;
    }

    const auto latest_tick_it = account.latest_ticks.find(request.instrument);
    if (latest_tick_it != account.latest_ticks.end() && latest_tick_it->second.last > 0.0) {
        return latest_tick_it->second.last;
    }

    return 0.0;
}

OrderEvent make_live_dry_run_fill_event(
    const LiveAccountState& account,
    const OrderRequest& request,
    std::string_view message) {

    const auto synthetic_id_seed = parse_timestamp_to_millis(current_timestamp()).value_or(0LL);
    const std::string client_order_id = request.client_order_id.empty()
        ? ("dryrun-" + request.account_id + "-" + request.strategy_id + "-" + std::to_string(synthetic_id_seed))
        : request.client_order_id;
    const std::string synthetic_order_id = "dryrun-fill-" + client_order_id;

    OrderRequest synthetic_request = request;
    synthetic_request.client_order_id = client_order_id;
    return make_event(
        synthetic_request,
        synthetic_order_id,
        OrderStatus::Filled,
        request.volume,
        dry_run_fill_price(account, request),
        std::string(message),
        live_order_event_timestamp(account, request.instrument));
}

void apply_live_order_event(
    LiveAccountState& account,
    const OrderEvent& event,
    std::map<std::string, LiveAttachmentTelemetry>& attachment_telemetry,
    std::vector<StrategyRuntime>& strategies) {

    const int fill_delta = consume_live_fill_delta(account, event);
    update_live_snapshot(account, event, fill_delta);
    const auto telemetry_it = attachment_telemetry.find(telemetry_attachment_key(event.strategy_id, event.account_id));
    if (telemetry_it != attachment_telemetry.end()) {
        update_live_attachment(telemetry_it->second, event, fill_delta);
    }
    dispatch_event_to_strategies(event, strategies);
    if (fill_delta > 0 && telemetry_it != attachment_telemetry.end()) {
        const auto position_it = std::find_if(
            telemetry_it->second.snapshot.positions.begin(),
            telemetry_it->second.snapshot.positions.end(),
            [&event](const RuntimePositionSnapshot& position) {
                return position.instrument == event.instrument;
            });
        if (position_it != telemetry_it->second.snapshot.positions.end()) {
            dispatch_position_update_to_strategies(*position_it, strategies);
        }
    }
}

std::string detect_live_environment(const IniFile& ini, const std::filesystem::path& config_path) {
    const auto configured = lower_copy(trim_copy(ini.get("live", "environment")));
    if (!configured.empty()) {
        return configured;
    }

    const auto config_name = lower_copy(config_path.stem().generic_string());
    if (config_name.find("prod") != std::string::npos) {
        return "prod";
    }
    if (config_name.find("probe") != std::string::npos) {
        return "probe";
    }
    if (config_name.find("simnow") != std::string::npos) {
        return "simnow";
    }
    return "live";
}

struct LiveRiskConfig {
    bool enabled {false};
    bool allow_market_orders {false};
    bool flatten_only {false};
    int max_order_volume {0};
    int max_abs_net_position {0};
    int max_long_position {0};
    int max_short_position {0};
    int max_outstanding_orders {0};
    double max_daily_loss {0.0};
    int max_quote_staleness_ms {0};
    double max_price_deviation_ratio {0.0};
};

struct LiveExecutionConfig {
    bool dry_run {false};
};

LiveRiskConfig read_live_risk_config(const IniFile& ini) {
    LiveRiskConfig config;
    config.enabled = ini.has_section("risk") && ini.get_bool("risk", "enabled", true);
    config.allow_market_orders = ini.get_bool("risk", "allow_market_orders", false);
    config.flatten_only = ini.get_bool("risk", "flatten_only", false);
    config.max_order_volume = std::max(0, ini.get_int("risk", "max_order_volume", 0));
    config.max_abs_net_position = std::max(0, ini.get_int("risk", "max_abs_net_position", 0));
    config.max_long_position = std::max(0, ini.get_int("risk", "max_long_position", 0));
    config.max_short_position = std::max(0, ini.get_int("risk", "max_short_position", 0));
    config.max_outstanding_orders = std::max(0, ini.get_int("risk", "max_outstanding_orders", 0));
    config.max_daily_loss = std::max(0.0, ini.get_double("risk", "max_daily_loss", 0.0));
    config.max_quote_staleness_ms = std::max(0, ini.get_int("risk", "max_quote_staleness_ms", 0));
    config.max_price_deviation_ratio = std::max(0.0, ini.get_double("risk", "max_price_deviation_ratio", 0.0));
    return config;
}

LiveExecutionConfig read_live_execution_config(const IniFile& ini) {
    LiveExecutionConfig config;
    config.dry_run = ini.get_bool("live", "dry_run", false);
    return config;
}

long long system_now_millis() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

long long live_tick_timestamp_millis(const MarketTick& tick) {
    if (tick.timestamp_ms > 0) {
        return tick.timestamp_ms;
    }
    return parse_timestamp_to_millis(tick.timestamp).value_or(0);
}

std::string current_local_timestamp_label() {
    return format_epoch_timestamp(system_now_millis() / 1000LL);
}

long long current_local_timestamp_cutoff_millis() {
    return parse_timestamp_to_millis(current_local_timestamp_label()).value_or(0);
}

std::string current_local_trading_day_label() {
    return canonical_trading_day_label(trading_day_label(current_local_timestamp_label()));
}

bool warmup_file_matches_trading_day(const std::filesystem::path& file_path, std::string_view target_trading_day) {
    if (target_trading_day.empty()) {
        return true;
    }

    const auto file_trading_day = canonical_trading_day_label(trading_day_label_from_file_path(file_path));
    return file_trading_day.empty() || file_trading_day == target_trading_day;
}

bool should_replay_warmup_tick(
    const MarketTick& tick,
    const std::set<std::string>& allowed_trading_days,
    long long cutoff_timestamp_ms) {

    if (!allowed_trading_days.empty()
        && !allowed_trading_days.contains(canonical_trading_day_label(trading_day_label_from_tick(tick)))) {
        return false;
    }

    const auto timestamp_ms = live_tick_timestamp_millis(tick);
    if (cutoff_timestamp_ms > 0 && timestamp_ms > 0 && timestamp_ms > cutoff_timestamp_ms) {
        return false;
    }

    return true;
}

BacktestSourcePlan collect_strategy_warmup_source_plan(const StrategyRuntime& strategy, std::string_view target_trading_day) {
    BacktestSourcePlan plan;
    std::set<std::string> instrument_filter(strategy.instruments.begin(), strategy.instruments.end());

    if (strategy.warmup.data_dir.has_value()) {
        const auto files = enumerate_directory_files_sorted(*strategy.warmup.data_dir);
        if (files.empty()) {
            throw std::runtime_error("Warmup data directory did not contain any CSV files: " + strategy.warmup.data_dir->string());
        }

        std::size_t next_source_index = 0;
        std::set<std::string> context_trading_days;
        if (!target_trading_day.empty()) {
            const std::string target_trading_day_string(target_trading_day);
            for (const auto& file_path : files) {
                const auto file_trading_day = canonical_trading_day_label(trading_day_label_from_file_path(file_path));
                if (!file_trading_day.empty() && file_trading_day < target_trading_day_string) {
                    context_trading_days.insert(file_trading_day);
                }
            }
            if (!context_trading_days.empty()) {
                const auto latest_prior_day = *context_trading_days.rbegin();
                context_trading_days.clear();
                context_trading_days.insert(latest_prior_day);
            }
            context_trading_days.insert(target_trading_day_string);
            plan.allowed_trading_days = context_trading_days;
        }

        for (const auto& file_path : files) {
            const auto file_trading_day = canonical_trading_day_label(trading_day_label_from_file_path(file_path));
            const bool include_file = target_trading_day.empty()
                ? warmup_file_matches_trading_day(file_path, target_trading_day)
                : context_trading_days.contains(file_trading_day);
            if (!include_file) {
                continue;
            }

            plan.directory_files.push_back(BacktestSourceFile {
                .path = file_path,
                .trading_day = file_trading_day,
                .instrument_filter = instrument_filter,
                .source_index = next_source_index++,
            });
        }

        if (plan.directory_files.empty()) {
            throw std::runtime_error(
                "Warmup data directory did not contain any CSV files for trading_day="
                + std::string(target_trading_day) + ": " + strategy.warmup.data_dir->string());
        }

        std::stable_sort(plan.directory_files.begin(), plan.directory_files.end(), [](const BacktestSourceFile& left, const BacktestSourceFile& right) {
            if (left.trading_day != right.trading_day) {
                return left.trading_day < right.trading_day;
            }
            return left.source_index < right.source_index;
        });
    } else if (strategy.warmup.csv_path.has_value()) {
        plan.csv_path = strategy.warmup.csv_path;
        plan.csv_instrument_filter = std::move(instrument_filter);
        if (!target_trading_day.empty()) {
            plan.allowed_trading_days.insert(std::string(target_trading_day));
        }
    }

    return plan;
}

std::size_t replay_strategy_warmup_history(
    StrategyRuntime& strategy,
    std::string_view startup_trading_day,
    const std::function<void(const MarketTick&)>& on_replayed_tick = {}) {

    if (!strategy.warmup.enabled || strategy.instance == nullptr || strategy.context == nullptr) {
        return 0;
    }

    const auto target_trading_day = !strategy.warmup.trading_day.empty()
        ? strategy.warmup.trading_day
        : (!trim_copy(startup_trading_day).empty()
            ? canonical_trading_day_label(startup_trading_day)
            : current_local_trading_day_label());
    const auto cutoff_timestamp_ms = current_local_timestamp_cutoff_millis();
    const auto source_plan = collect_strategy_warmup_source_plan(strategy, target_trading_day);

    strategy.context->log(
        "Starting live warmup replay for trading_day="
        + (target_trading_day.empty() ? std::string("<all>") : target_trading_day));

    strategy.context->set_order_flow_enabled(false);
    std::size_t replayed_ticks = 0;
    try {
        auto replay_tick = [&](const MarketTick& tick) {
            if (!should_replay_warmup_tick(tick, source_plan.allowed_trading_days, cutoff_timestamp_ms)) {
                return;
            }

            if (on_replayed_tick) {
                on_replayed_tick(tick);
            }
            strategy.instance->on_tick(tick, *strategy.context);
            ++replayed_ticks;
        };

        if (!source_plan.directory_files.empty() && !source_plan.csv_path.has_value()) {
            BacktestDirectoryTickStream stream(source_plan);
            MarketTick tick;
            while (stream.next(tick)) {
                replay_tick(tick);
            }
        } else if (source_plan.csv_path.has_value()) {
            BacktestCsvReader reader(BacktestSourceFile {
                .path = *source_plan.csv_path,
                .trading_day = canonical_trading_day_label(trading_day_label_from_file_path(*source_plan.csv_path)),
                .instrument_filter = source_plan.csv_instrument_filter,
                .source_index = 0,
            });

            while (reader.advance()) {
                replay_tick(reader.current_tick());
            }
        }
    } catch (...) {
        strategy.context->set_order_flow_enabled(true);
        throw;
    }
    strategy.context->set_order_flow_enabled(true);

    if (replayed_ticks == 0) {
        throw std::runtime_error(
            "Warmup replay for strategy " + strategy.strategy_id
            + " yielded no historical ticks before the live startup cutoff.");
    }

    strategy.context->log("Completed live warmup replay with " + std::to_string(replayed_ticks) + " historical ticks.");
    return replayed_ticks;
}

int count_open_orders_for_account(
    const std::map<std::string, LiveAttachmentTelemetry>& attachment_telemetry,
    std::string_view account_id) {

    int count = 0;
    for (const auto& [_, attachment] : attachment_telemetry) {
        if (attachment.snapshot.account_id == account_id) {
            count += static_cast<int>(attachment.opened_orders_by_id.size());
        }
    }
    return count;
}

bool looks_like_simnow_account_config(std::string_view account_id, const CtpAccountConfig& config) {
    const auto normalized_account_id = lower_copy(trim_copy(account_id));
    const auto normalized_broker_id = lower_copy(trim_copy(config.broker_id));
    const auto normalized_app_id = lower_copy(trim_copy(config.app_id));
    const auto normalized_user_id = lower_copy(trim_copy(config.user_id));

    return normalized_account_id.find("simnow") != std::string::npos
        || normalized_broker_id == "9999"
        || normalized_app_id.find("simnow") != std::string::npos
        || normalized_user_id == "000000";
}

void validate_live_runtime_configuration(
    std::string_view environment,
    const IniFile& ini,
    const LiveRiskConfig& risk_config) {

    if (environment != "prod") {
        return;
    }

    if (ini.get_int("live", "iterations", 0) != 0) {
        throw std::runtime_error("Production live configs must set [live] iterations=0 so the runtime does not auto-exit.");
    }

    if (!risk_config.enabled) {
        throw std::runtime_error("Production live configs must enable a [risk] section before real trading is allowed.");
    }
}

void validate_live_account_configuration(
    std::string_view environment,
    std::string_view account_id,
    const CtpAccountConfig& config) {

    if (environment != "prod") {
        return;
    }

    if (!config.production_mode) {
        throw std::runtime_error("Production live accounts must set production_mode=true.");
    }
    if (!config.reconnect_enabled) {
        throw std::runtime_error("Production live accounts must keep reconnect_enabled=true.");
    }
    if (config.reconnect_retry_interval_ms <= 0) {
        throw std::runtime_error("Production live accounts must use a positive reconnect_retry_interval_ms.");
    }
    if (looks_like_simnow_account_config(account_id, config)) {
        throw std::runtime_error("Production live configs must not use SimNow-style broker/app/account settings.");
    }
}

std::string evaluate_live_order_risk(
    const LiveRiskConfig& risk_config,
    const LiveAccountState& account,
    const OrderRequest& request,
    const std::map<std::string, LiveAttachmentTelemetry>& attachment_telemetry,
    bool require_trading_enabled = true) {

    if (require_trading_enabled && !account.trading_enabled) {
        return account.trading_disable_reason.empty()
            ? "Live trading is currently paused for this account."
            : account.trading_disable_reason;
    }

    if (request.volume <= 0) {
        return "Risk manager rejected a non-positive order volume.";
    }
    if (request.price_type == PriceType::Limit) {
        if (!std::isfinite(request.limit_price) || request.limit_price <= 0.0) {
            return "Risk manager rejected the live limit order because it does not have a positive finite limit price.";
        }
        if (const auto latest_tick_it = account.latest_ticks.find(request.instrument); latest_tick_it != account.latest_ticks.end()) {
            const double reference_price = request.side == Side::Buy
                ? best_ask(latest_tick_it->second)
                : best_bid(latest_tick_it->second);
            if (reference_price > 0.0
                && (request.limit_price < reference_price * 0.5 || request.limit_price > reference_price * 1.5)) {
                return "Risk manager rejected the live limit order because the limit price is implausibly far from the touch.";
            }
        }
    }

    if (!risk_config.enabled) {
        return {};
    }

    if (risk_config.flatten_only && request.offset == Offset::Open) {
        return "Risk manager is in flatten-only mode and rejected a new opening order.";
    }
    if (!risk_config.allow_market_orders && request.price_type == PriceType::Market) {
        return "Risk manager rejected a market order in live mode.";
    }
    if (risk_config.max_order_volume > 0 && request.volume > risk_config.max_order_volume) {
        return "Risk manager rejected the order because volume exceeds max_order_volume.";
    }
    if (risk_config.max_outstanding_orders > 0
        && count_open_orders_for_account(attachment_telemetry, request.account_id) >= risk_config.max_outstanding_orders) {
        return "Risk manager rejected the order because max_outstanding_orders has been reached.";
    }
    if (risk_config.max_daily_loss > 0.0 && account.snapshot.realized_pnl <= -risk_config.max_daily_loss) {
        return "Risk manager rejected the order because max_daily_loss has been breached.";
    }

    const auto current_position_it = account.positions.find(request.instrument);
    const PositionState current_position = current_position_it == account.positions.end()
        ? PositionState {}
        : current_position_it->second;
    if (!close_request_is_valid(current_position, request)) {
        return "Risk manager rejected the close request because the broker-recovered position is insufficient.";
    }

    if (risk_config.max_quote_staleness_ms > 0 || risk_config.max_price_deviation_ratio > 0.0) {
        const auto latest_tick_it = account.latest_ticks.find(request.instrument);
        if (latest_tick_it == account.latest_ticks.end()) {
            return "Risk manager requires a fresh quote before live orders can be submitted.";
        }

        const auto quote_time_ms = live_tick_timestamp_millis(latest_tick_it->second);
        if (risk_config.max_quote_staleness_ms > 0 && quote_time_ms <= 0) {
            return "Risk manager requires timestamped market data before live orders can be submitted.";
        }
        if (risk_config.max_quote_staleness_ms > 0 && (system_now_millis() - quote_time_ms) > risk_config.max_quote_staleness_ms) {
            return "Risk manager rejected the order because the latest quote is stale.";
        }

        if (risk_config.max_price_deviation_ratio > 0.0 && request.price_type == PriceType::Limit) {
            const double reference_price = request.side == Side::Buy
                ? best_ask(latest_tick_it->second)
                : best_bid(latest_tick_it->second);
            if (reference_price <= 0.0) {
                return "Risk manager requires a valid top-of-book quote before live limit orders can be submitted.";
            }
            if (request.limit_price > reference_price * (1.0 + risk_config.max_price_deviation_ratio)
                || request.limit_price < reference_price * (1.0 - risk_config.max_price_deviation_ratio)) {
                return "Risk manager rejected the order because the limit price is too far from the touch.";
            }
        }
    }

    auto projected_position = current_position;
    const double projection_price = request.limit_price > 0.0 ? request.limit_price : compatibility_average_price(current_position);
    apply_fill(projected_position, request, projection_price, request.volume);

    if (risk_config.max_long_position > 0 && total_long_quantity(projected_position) > risk_config.max_long_position) {
        return "Risk manager rejected the order because projected long exposure exceeds max_long_position.";
    }
    if (risk_config.max_short_position > 0 && total_short_quantity(projected_position) > risk_config.max_short_position) {
        return "Risk manager rejected the order because projected short exposure exceeds max_short_position.";
    }
    if (risk_config.max_abs_net_position > 0 && std::abs(net_quantity(projected_position)) > risk_config.max_abs_net_position) {
        return "Risk manager rejected the order because projected net exposure exceeds max_abs_net_position.";
    }

    return {};
}

CtpAccountConfig read_ctp_config(const IniFile& ini, const std::string& section, const std::filesystem::path& config_path) {
    CtpAccountConfig config;
    const auto account_id = section.substr(section.find('.') + 1);
    config.front = ini.get(section, "front");
    config.broker_id = ini.get(section, "broker_id");
    config.user_id = ini.get(section, "user_id");
    config.investor_id = ini.get(section, "investor_id", config.user_id);
    config.password = ini.get(section, "password");
    config.auth_code = ini.get(section, "auth_code");
    config.app_id = ini.get(section, "app_id");
    config.md_front = ini.get(section, "md_front");
    config.md_broker_id = ini.get(section, "md_broker_id", config.broker_id);
    config.md_user_id = ini.get(section, "md_user_id", config.user_id);
    config.md_password = ini.get(section, "md_password", config.password);
    config.product_info = ini.get(section, "product_info", "iTrader");
    config.flow_dir = ini.get(section, "flow_dir", itrader::default_ctp_flow_dir(config_path, account_id).generic_string());
    config.md_flow_dir = ini.get(section, "md_flow_dir", itrader::default_ctp_md_flow_dir(config_path, account_id).generic_string());
    config.production_mode = ini.get_bool(section, "production_mode", true);
    config.reconnect_enabled = ini.get_bool(section, "reconnect_enabled", true);
    config.reconnect_retry_interval_ms = std::max(0, ini.get_int(section, "reconnect_retry_interval_ms", 3000));
    config.reconnect_max_attempts = std::max(0, ini.get_int(section, "reconnect_max_attempts", 0));
    return config;
}

int run_live(const std::filesystem::path& config_path, const IniFile& ini) {
    const auto base_dir = config_path.parent_path();
    const auto account_sections = ini.sections_with_prefix("account.");
    const auto strategy_sections = ini.sections_with_prefix("strategy.");
    const auto live_environment = detect_live_environment(ini, config_path);
    const auto live_risk_config = read_live_risk_config(ini);
    const auto live_execution_config = read_live_execution_config(ini);
    std::mutex live_activity_mutex;
    std::condition_variable live_activity_cv;
    bool live_activity_pending = false;
    const auto notify_live_activity = [&live_activity_mutex, &live_activity_cv, &live_activity_pending]() {
        {
            std::lock_guard lock(live_activity_mutex);
            live_activity_pending = true;
        }
        live_activity_cv.notify_one();
    };
    if (account_sections.empty()) {
        throw std::runtime_error("Live config needs at least one [account.*] section");
    }
    if (strategy_sections.empty()) {
        throw std::runtime_error("Live config needs at least one [strategy.*] section");
    }

    validate_live_runtime_configuration(live_environment, ini, live_risk_config);

    std::map<std::string, LiveAccountState> accounts;
    for (const auto& section : account_sections) {
        const auto account_id = section.substr(section.find('.') + 1);
        LiveAccountState state;
        state.snapshot.account_id = account_id;
        state.snapshot.initial_cash = ini.get_double(section, "initial_cash", 0.0);
        state.snapshot.cash = state.snapshot.initial_cash;
        state.config = read_ctp_config(ini, section, config_path);
        validate_live_account_configuration(live_environment, account_id, state.config);
        state.gateway = std::make_unique<CtpTraderGateway>(account_id, state.config);
        state.gateway->set_activity_callback(notify_live_activity);

        std::string error_message;
        if (!state.gateway->connect(&error_message, 15000)) {
            throw std::runtime_error("Failed to connect CTP account " + account_id + ": " + error_message);
        }
        state.trader_connected = state.gateway->ready();
        state.trading_enabled = state.trader_connected;

        accounts.emplace(account_id, std::move(state));
    }

    std::vector<StrategyRuntime> strategies;
    std::map<std::string, LiveAttachmentTelemetry> attachment_telemetry;
    std::vector<std::string> telemetry_warnings;
    std::vector<OrderEvent> platform_order_events;
    for (const auto& section : strategy_sections) {
        const auto bound_accounts = read_strategy_accounts(ini, section);
        if (bound_accounts.empty()) {
            std::cout << "Skipping unbound strategy section " << section << " in live mode.\n";
            continue;
        }
        for (const auto& account_id : bound_accounts) {
            auto runtime = load_strategy_runtime(
                ini,
                section,
                account_id,
                base_dir,
                Mode::Live,
                [&accounts, &attachment_telemetry, &live_risk_config, &live_execution_config, &platform_order_events](const OrderRequest& request) {
                    const auto account_it = accounts.find(request.account_id);
                    if (account_it == accounts.end() || account_it->second.gateway == nullptr) {
                        return false;
                    }

                    const auto risk_error = evaluate_live_order_risk(
                        live_risk_config,
                        account_it->second,
                        request,
                        attachment_telemetry,
                        !live_execution_config.dry_run);
                    if (!risk_error.empty()) {
                        std::cerr << "Order rejected by live risk manager: " << risk_error << '\n';
                        return false;
                    }

                    if (live_execution_config.dry_run) {
                        static constexpr std::string_view kDryRunFillMessage =
                            "Dry run simulated fill at the platform layer; broker submission was skipped before ReqOrderInsert.";
                        platform_order_events.push_back(make_live_dry_run_fill_event(
                            account_it->second,
                            request,
                            kDryRunFillMessage));
                        std::cout << "[dry-run] simulated fill for account=" << request.account_id
                                  << ", strategy=" << request.strategy_id
                                  << ", instrument=" << request.instrument
                                  << ", client_order_id=" << request.client_order_id
                                  << '\n';
                        return true;
                    }

                    std::string error_message;
                    const bool success = account_it->second.gateway->submit_order(request, &error_message);
                    if (!success && !error_message.empty()) {
                        std::cerr << "Order rejected before submission: " << error_message << '\n';
                    }
                    return success;
                },
                [&accounts, account_id](const std::string& client_order_id) {
                    const auto account_it = accounts.find(account_id);
                    if (account_it == accounts.end() || account_it->second.gateway == nullptr) {
                        return false;
                    }

                    std::string error_message;
                    const bool success = account_it->second.gateway->cancel_order(client_order_id, &error_message);
                    if (!success && !error_message.empty()) {
                        std::cerr << "Cancel rejected before submission: " << error_message << '\n';
                    }
                    return success;
                },
                [&attachment_telemetry, account_id, strategy_id = section.substr(section.find('.') + 1)](const std::string& instrument) {
                    std::vector<RuntimeOrderSnapshot> orders;
                    const auto telemetry_it = attachment_telemetry.find(telemetry_attachment_key(strategy_id, account_id));
                    if (telemetry_it == attachment_telemetry.end()) {
                        return orders;
                    }
                    orders.reserve(telemetry_it->second.opened_orders_by_id.size());
                    for (const auto& [_, order] : telemetry_it->second.opened_orders_by_id) {
                        if (!instrument.empty() && order.instrument != instrument) {
                            continue;
                        }
                        orders.push_back(order);
                    }
                    return orders;
                },
                [&accounts, &attachment_telemetry, account_id, strategy_id = section.substr(section.find('.') + 1)](const std::string& instrument) {
                    const auto telemetry_it = attachment_telemetry.find(telemetry_attachment_key(strategy_id, account_id));
                    if (telemetry_it != attachment_telemetry.end()) {
                        const auto position_it = telemetry_it->second.positions.find(instrument);
                        return position_it == telemetry_it->second.positions.end() ? 0 : net_quantity(position_it->second);
                    }

                    const auto account_it = accounts.find(account_id);
                    if (account_it == accounts.end()) {
                        return 0;
                    }
                    const auto position_it = account_it->second.snapshot.net_positions.find(instrument);
                    return position_it == account_it->second.snapshot.net_positions.end() ? 0 : position_it->second;
                });

            if (!runtime.order_ref_strategy_code.has_value()) {
                throw std::runtime_error(
                    "Live strategy section " + section
                    + " must set order_ref_strategy_code=01..99 because CTP order routing now uses OrderRef suffixes instead of OrderMemo.");
            }

            const auto account_it = accounts.find(runtime.account_id);
            if (account_it == accounts.end() || account_it->second.gateway == nullptr) {
                throw std::runtime_error("Live strategy section " + section + " is bound to unknown account " + runtime.account_id + '.');
            }

            std::string register_error;
            if (!account_it->second.gateway->register_strategy_order_ref_code(
                    *runtime.order_ref_strategy_code,
                    runtime.strategy_id,
                    &register_error)) {
                throw std::runtime_error(
                    "Failed to register OrderRef strategy code for " + runtime.strategy_id + " on account " + runtime.account_id + ": " + register_error);
            }

            attachment_telemetry.emplace(telemetry_attachment_key(runtime.strategy_id, runtime.account_id), make_live_attachment_telemetry(runtime));
            strategies.push_back(std::move(runtime));
        }
    }

    if (strategies.empty()) {
        std::cout << "No assigned strategies loaded for live mode; accounts will connect, but no strategy logic will execute until a strategy is bound to an account.\n";
        telemetry_warnings.push_back("No assigned strategies are active in the live engine, so live telemetry only contains account-level snapshots.");
    }

    const int live_bar_seconds = ini.get_int("live", "chart_bar_seconds", 60);
    const std::size_t max_live_chart_bars = static_cast<std::size_t>(std::max(ini.get_int("live", "chart_max_bars", 240), 32));
    const std::size_t max_live_chart_signals = static_cast<std::size_t>(std::max(ini.get_int("live", "chart_max_signals", 1000), 0));
    seed_live_closed_order_history_from_telemetry(config_path, attachment_telemetry, max_live_chart_signals);
    for (auto& strategy : strategies) {
        if (strategy.context != nullptr) {
            strategy.context->set_max_chart_series_points(max_live_chart_bars);
        }
    }

    std::map<std::string, std::set<std::string>> instruments_by_account;
    for (const auto& strategy : strategies) {
        for (const auto& instrument : strategy.instruments) {
            instruments_by_account[strategy.account_id].insert(instrument);
        }
    }

#ifdef ITRADER_ENABLE_CTP_MD
    for (auto& [account_id, account] : accounts) {
        account.market_data_gateway = std::make_unique<CtpMarketDataGateway>(account_id, account.config);
        account.market_data_gateway->set_activity_callback(notify_live_activity);

        const auto& instrument_set = instruments_by_account[account_id];
        const std::vector<std::string> instruments(instrument_set.begin(), instrument_set.end());
        account.market_data_gateway->subscribe_market_data(instruments, nullptr);

        std::string error_message;
        if (!account.market_data_gateway->connect(&error_message, 15000)) {
            account.market_data_connected = false;
            const auto trimmed_error = trim_copy(error_message);
            if (!looks_like_retryable_market_data_connect_error(trimmed_error)) {
                throw std::runtime_error("Failed to connect CTP market-data account " + account_id + ": " + trimmed_error);
            }

            std::cout << "Initial CTP market-data connect is pending for account " << account_id
                      << ": " << trimmed_error << "\n";
        } else {
            account.market_data_connected = account.market_data_gateway->ready();
        }
    }
#endif

    std::map<std::string, MarketTick> initial_live_ticks_by_account;
    std::string current_live_trading_day;
    if (!wait_for_initial_live_market_data(accounts, instruments_by_account, initial_live_ticks_by_account, current_live_trading_day)) {
#ifdef ITRADER_ENABLE_CTP_MD
        for (auto& [_, account] : accounts) {
            if (account.market_data_gateway != nullptr) {
                account.market_data_gateway->disconnect();
            }
        }
#endif
        for (auto& [_, account] : accounts) {
            if (account.gateway != nullptr) {
                account.gateway->disconnect();
            }
        }
        std::cout << "Live startup stopped before the first live tick arrived.\n";
        return 0;
    }

    if (live_execution_config.dry_run) {
        const std::string warning = "Live dry run is enabled: strategies, warmup, broker recovery, and market-data flow stay active; broker submissions are skipped before ReqOrderInsert, and the platform records local paper fills so entry/exit logic can run end-to-end.";
        append_unique_warning(telemetry_warnings, warning);
        for (auto& [_, attachment] : attachment_telemetry) {
            append_unique_warning(attachment.snapshot.warnings, warning);
        }
        std::cout << warning << '\n';
    }

    std::map<std::string, std::vector<LiveChartBar>> live_chart_bars;
    for (auto& strategy : strategies) {
        if (!strategy.warmup.enabled) {
            continue;
        }
        replay_strategy_warmup_history(
            strategy,
            current_live_trading_day,
            [&live_chart_bars, live_bar_seconds, max_live_chart_bars](const MarketTick& tick) {
                update_live_chart_bars(live_chart_bars, tick, live_bar_seconds, max_live_chart_bars);
            });
    }
    const auto configured_live_chart_instruments = collect_strategy_runtime_instruments(strategies);
    auto removed_warmup_chart_instruments = filter_live_chart_bars_to_instruments(live_chart_bars, configured_live_chart_instruments);

    auto persisted_live_chart_bars = read_persisted_live_chart_bars(config_path, max_live_chart_bars);
    auto removed_persisted_chart_instruments = filter_live_chart_bars_to_instruments(persisted_live_chart_bars, configured_live_chart_instruments);
    merge_live_chart_bars(live_chart_bars, persisted_live_chart_bars, max_live_chart_bars);
    auto removed_merged_chart_instruments = filter_live_chart_bars_to_instruments(live_chart_bars, configured_live_chart_instruments);

    removed_persisted_chart_instruments.insert(
        removed_persisted_chart_instruments.end(),
        removed_warmup_chart_instruments.begin(),
        removed_warmup_chart_instruments.end());
    removed_persisted_chart_instruments.insert(
        removed_persisted_chart_instruments.end(),
        removed_merged_chart_instruments.begin(),
        removed_merged_chart_instruments.end());
    std::sort(removed_persisted_chart_instruments.begin(), removed_persisted_chart_instruments.end());
    removed_persisted_chart_instruments.erase(
        std::unique(removed_persisted_chart_instruments.begin(), removed_persisted_chart_instruments.end()),
        removed_persisted_chart_instruments.end());
    if (!removed_persisted_chart_instruments.empty()) {
        const std::string warning = "Ignored persisted live chart telemetry for instruments no longer configured: "
            + join_string_values(removed_persisted_chart_instruments, ", ") + '.';
        append_unique_warning(telemetry_warnings, warning);
        std::cout << warning << '\n';
    }

    const auto persisted_strategy_state_store = read_strategy_state_store(config_path);
    auto persisted_inventory_store = read_strategy_inventory_store(config_path);
    const auto manual_inventory_adjustments = read_manual_inventory_adjustments(config_path);
    apply_manual_inventory_adjustments(config_path, persisted_inventory_store, manual_inventory_adjustments, telemetry_warnings);
    validate_detached_persisted_inventory(persisted_inventory_store, attachment_telemetry);
    seed_attachment_telemetry_from_inventory_store(persisted_inventory_store, attachment_telemetry, telemetry_warnings);

    hydrate_live_positions_from_broker(accounts, strategies, attachment_telemetry, telemetry_warnings, persisted_inventory_store);
    hydrate_live_working_orders_from_broker(accounts, persisted_strategy_state_store, attachment_telemetry, telemetry_warnings);
    const auto recovered_live_trades = recover_live_trades_from_broker(
        accounts,
        persisted_strategy_state_store,
        persisted_inventory_store,
        attachment_telemetry,
        telemetry_warnings);

    for (const auto& [_, attachment] : attachment_telemetry) {
        dispatch_live_attachment_position_updates(attachment, strategies);
    }
    restore_live_strategy_state(persisted_strategy_state_store, strategies);
    replay_recovered_live_trades_to_strategies(recovered_live_trades, strategies);
    for (auto& strategy : strategies) {
        strategy.instance->on_start(*strategy.context);
    }

    for (const auto& [account_id, tick] : initial_live_ticks_by_account) {
        update_live_chart_bars(live_chart_bars, tick, live_bar_seconds, max_live_chart_bars);
        dispatch_tick_to_strategies(tick, account_id, strategies);
    }

    auto drain_platform_order_events = [&accounts, &attachment_telemetry, &platform_order_events, &strategies]() {
        auto queued_events = std::move(platform_order_events);
        platform_order_events.clear();
        for (const auto& event : queued_events) {
            const auto account_it = accounts.find(event.account_id);
            if (account_it == accounts.end()) {
                continue;
            }
            apply_live_order_event(account_it->second, event, attachment_telemetry, strategies);
        }
    };

    drain_platform_order_events();

    write_live_telemetry_file(
        config_path,
        accounts,
        attachment_telemetry,
        live_chart_bars,
        collect_strategy_chart_indicator_series(strategies),
        live_bar_seconds,
        telemetry_warnings);
    if (!live_execution_config.dry_run) {
        sync_inventory_store_from_live_attachments(persisted_inventory_store, attachment_telemetry);
        write_strategy_inventory_store(config_path, persisted_inventory_store, accounts, attachment_telemetry);
    }
    write_strategy_state_store(config_path, strategies);

    const int poll_interval_ms = ini.get_int("live", "poll_interval_ms", 1000);
    const int iterations = ini.get_int("live", "iterations", 0);
    const auto reset_live_activity = [&live_activity_mutex, &live_activity_pending]() {
        std::lock_guard lock(live_activity_mutex);
        live_activity_pending = false;
    };
    const auto wait_for_live_activity = [&live_activity_mutex, &live_activity_cv, &live_activity_pending, poll_interval_ms]() {
        if (poll_interval_ms <= 0 || g_stop_requested) {
            return;
        }

        std::unique_lock lock(live_activity_mutex);
        if (!live_activity_pending) {
            live_activity_cv.wait_for(
                lock,
                std::chrono::milliseconds(poll_interval_ms),
                [&live_activity_pending]() {
                    return live_activity_pending || g_stop_requested.load();
                });
        }
        live_activity_pending = false;
    };

    int completed_iterations = 0;
    while (!g_stop_requested && (iterations == 0 || completed_iterations < iterations)) {
        reset_live_activity();
        bool reconnect_requires_rehydrate = false;
        for (auto& [account_id, account] : accounts) {
            if (account.gateway != nullptr) {
                if (!account.gateway->ready()) {
                    account.trader_connected = false;
                    std::string error_message;
                    if (account.gateway->ensure_ready(&error_message, 15000)) {
                        account.trader_connected = true;
                        account.trading_enabled = true;
                        account.trading_disable_reason.clear();
                        const std::string warning = "CTP trader gateway reconnected for account " + account_id + '.';
                        append_unique_warning(telemetry_warnings, warning);
                        append_warning_to_account_attachments(account_id, warning, attachment_telemetry);
                        reconnect_requires_rehydrate = true;
                    } else {
                        account.trading_enabled = false;
                        account.trading_disable_reason = error_message.empty()
                            ? "Live trading is paused because the trader gateway is disconnected."
                            : ("Live trading is paused because the trader gateway is disconnected: " + error_message);
                        append_unique_warning(telemetry_warnings, account.trading_disable_reason);
                        append_warning_to_account_attachments(account_id, account.trading_disable_reason, attachment_telemetry);
                    }
                } else {
                    account.trader_connected = true;
                }
            }
#ifdef ITRADER_ENABLE_CTP_MD
            if (account.market_data_gateway != nullptr) {
                if (!account.market_data_gateway->ready()) {
                    account.market_data_connected = false;
                    std::string error_message;
                    if (account.market_data_gateway->ensure_ready(&error_message, 15000)) {
                        account.market_data_connected = true;
                        const std::string warning = "CTP market-data gateway reconnected for account " + account_id + '.';
                        append_unique_warning(telemetry_warnings, warning);
                        append_warning_to_account_attachments(account_id, warning, attachment_telemetry);
                    } else {
                        const std::string warning = error_message.empty()
                            ? ("CTP market-data reconnect is still pending for account " + account_id + '.')
                            : ("CTP market-data reconnect is pending for account " + account_id + ": " + error_message);
                        append_unique_warning(telemetry_warnings, warning);
                        append_warning_to_account_attachments(account_id, warning, attachment_telemetry);
                    }
                } else {
                    account.market_data_connected = true;
                }
            }
#endif
        }

        if (reconnect_requires_rehydrate) {
            try {
                hydrate_live_positions_from_broker(accounts, strategies, attachment_telemetry, telemetry_warnings, persisted_inventory_store);
                hydrate_live_working_orders_from_broker(accounts, persisted_strategy_state_store, attachment_telemetry, telemetry_warnings);
                const auto recovered_live_trades_after_reconnect = recover_live_trades_from_broker(
                    accounts,
                    persisted_strategy_state_store,
                    persisted_inventory_store,
                    attachment_telemetry,
                    telemetry_warnings);
                for (const auto& [_, attachment] : attachment_telemetry) {
                    dispatch_live_attachment_position_updates(attachment, strategies);
                }
                replay_recovered_live_trades_to_strategies(recovered_live_trades_after_reconnect, strategies);
            } catch (const std::exception& ex) {
                const std::string warning = std::string("Live reconnect reconciliation failed: ") + ex.what();
                append_unique_warning(telemetry_warnings, warning);
                for (auto& [account_id, account] : accounts) {
                    account.trading_enabled = false;
                    account.trading_disable_reason = warning;
                    append_warning_to_account_attachments(account_id, warning, attachment_telemetry);
                }
            }
        }

        for (auto& [account_id, account] : accounts) {
#ifdef ITRADER_ENABLE_CTP_MD
            bool used_streaming_ticks = false;
            if (account.market_data_gateway != nullptr && account.market_data_gateway->ready()) {
                used_streaming_ticks = true;
                for (auto tick : account.market_data_gateway->drain_ticks()) {
                    tick.timestamp_ms = live_tick_timestamp_millis(tick);
                    account.latest_ticks[tick.instrument] = tick;
                    const auto next_trading_day = trading_day_label_from_tick(tick);
                    if (!next_trading_day.empty() && !current_live_trading_day.empty() && next_trading_day != current_live_trading_day) {
                        roll_live_accounts_to_next_trading_day(accounts, attachment_telemetry);
                        for (const auto& [__, attachment] : attachment_telemetry) {
                            dispatch_live_attachment_position_updates(attachment, strategies);
                        }
                    }
                    if (!next_trading_day.empty()) {
                        current_live_trading_day = next_trading_day;
                    }
                    update_live_chart_bars(live_chart_bars, tick, live_bar_seconds, max_live_chart_bars);
                    dispatch_tick_to_strategies(tick, account_id, strategies);
                }
            }
            if (used_streaming_ticks) {
                continue;
            }
#endif
            for (const auto& instrument : instruments_by_account[account_id]) {
                if (account.gateway == nullptr) {
                    continue;
                }
                auto tick = account.gateway->query_tick(instrument);
                if (tick.has_value()) {
                    tick->timestamp_ms = live_tick_timestamp_millis(*tick);
                    account.latest_ticks[tick->instrument] = *tick;
                    const auto next_trading_day = trading_day_label_from_tick(*tick);
                    if (!next_trading_day.empty() && !current_live_trading_day.empty() && next_trading_day != current_live_trading_day) {
                        roll_live_accounts_to_next_trading_day(accounts, attachment_telemetry);
                        for (const auto& [__, attachment] : attachment_telemetry) {
                            dispatch_live_attachment_position_updates(attachment, strategies);
                        }
                    }
                    if (!next_trading_day.empty()) {
                        current_live_trading_day = next_trading_day;
                    }
                    update_live_chart_bars(live_chart_bars, *tick, live_bar_seconds, max_live_chart_bars);
                    dispatch_tick_to_strategies(*tick, account_id, strategies);
                }
            }
        }

        for (auto& [_, account] : accounts) {
            if (account.gateway == nullptr) {
                continue;
            }
            for (auto event : account.gateway->drain_order_events()) {
                apply_live_order_event(account, event, attachment_telemetry, strategies);
            }
        }

        drain_platform_order_events();

        write_live_telemetry_file(
            config_path,
            accounts,
            attachment_telemetry,
            live_chart_bars,
            collect_strategy_chart_indicator_series(strategies),
            live_bar_seconds,
            telemetry_warnings);
        if (!live_execution_config.dry_run) {
            sync_inventory_store_from_live_attachments(persisted_inventory_store, attachment_telemetry);
            write_strategy_inventory_store(config_path, persisted_inventory_store, accounts, attachment_telemetry);
        }
        write_strategy_state_store(config_path, strategies);

        ++completed_iterations;
        wait_for_live_activity();
    }

    write_strategy_state_store(config_path, strategies);

    for (auto& strategy : strategies) {
        strategy.instance->on_stop(*strategy.context);
    }

    write_live_telemetry_file(
        config_path,
        accounts,
        attachment_telemetry,
        live_chart_bars,
        collect_strategy_chart_indicator_series(strategies),
        live_bar_seconds,
        telemetry_warnings);
    if (!live_execution_config.dry_run) {
        sync_inventory_store_from_live_attachments(persisted_inventory_store, attachment_telemetry);
        write_strategy_inventory_store(config_path, persisted_inventory_store, accounts, attachment_telemetry);
    }

    for (auto& [_, account] : accounts) {
#ifdef ITRADER_ENABLE_CTP_MD
        if (account.market_data_gateway != nullptr) {
            account.market_data_gateway->disconnect();
        }
#endif
        if (account.gateway != nullptr) {
            account.gateway->disconnect();
        }
    }

    std::cout << "\nLive summary\n";
    for (const auto& [account_id, account] : accounts) {
        std::cout << "- account=" << account_id
                  << ", realized_pnl(tracked from fills)=" << format_price(account.snapshot.realized_pnl)
                  << ", net_positions=";

        bool first = true;
        for (const auto& [instrument, net] : account.snapshot.net_positions) {
            if (!first) {
                std::cout << ';';
            }
            first = false;
            std::cout << instrument << ':' << net;
        }
        if (first) {
            std::cout << "flat";
        }
        std::cout << '\n';
    }

    return 0;
}
#endif

} // namespace

RuntimeSnapshot build_runtime_snapshot(
    const std::filesystem::path& config_path,
    const IniFile& ini,
    Mode mode,
    const RuntimeSnapshotBuildOptions& options) {

    RuntimeSnapshot snapshot;
    snapshot.mode = mode;
    snapshot.chart_bar_seconds = std::max(1, options.chart_bar_seconds);

    const auto account_sections = ini.sections_with_prefix("account.");
    const auto strategy_sections = ini.sections_with_prefix("strategy.");

    for (const auto& section : account_sections) {
        AccountSnapshot account_snapshot;
        account_snapshot.account_id = section.substr(section.find('.') + 1);
        account_snapshot.initial_cash = ini.get_double(section, "initial_cash", mode == Mode::Backtest ? 1'000'000.0 : 0.0);
        account_snapshot.cash = account_snapshot.initial_cash;
        snapshot.accounts.push_back(std::move(account_snapshot));
    }

    if (mode == Mode::Live) {
        try {
            return read_live_telemetry_snapshot(config_path);
        } catch (const std::exception& ex) {
            snapshot.warnings.push_back(std::string("Live runtime detail telemetry is not yet available from the detached UI API server. ") + ex.what());
        }
        for (const auto& section : strategy_sections) {
            for (const auto& account_id : read_strategy_accounts(ini, section)) {
                StrategyAttachmentSnapshot attachment;
                attachment.strategy_id = section.substr(section.find('.') + 1);
                attachment.account_id = account_id;
                attachment.warnings = snapshot.warnings;
                snapshot.strategy_attachments.push_back(std::move(attachment));
            }
        }
        return snapshot;
    }

    const auto base_dir = config_path.parent_path();
    std::map<std::string, SimAccountState> accounts;
    for (const auto& section : account_sections) {
        const std::string account_id = section.substr(section.find('.') + 1);
        SimAccountState state;
        state.snapshot.account_id = account_id;
        state.snapshot.initial_cash = ini.get_double(section, "initial_cash", 1'000'000.0);
        state.snapshot.cash = state.snapshot.initial_cash;
        accounts.emplace(account_id, std::move(state));
    }

    std::vector<OrderRequest> order_queue;
    std::vector<OrderEvent> events;
    const bool retain_closed_order_snapshots = options.include_order_history || options.include_chart;
    std::string current_action_timestamp;
    std::vector<StrategyRuntime> strategies;
    for (const auto& section : strategy_sections) {
        const auto bound_accounts = read_strategy_accounts(ini, section);
        if (bound_accounts.empty()) {
            continue;
        }
        for (const auto& account_id : bound_accounts) {
            const auto strategy_id = section.substr(section.find('.') + 1);
            auto runtime = load_strategy_runtime(
                ini,
                section,
                account_id,
                base_dir,
                Mode::Backtest,
                [&order_queue](const OrderRequest& request) {
                    order_queue.push_back(request);
                    return true;
                },
                [&accounts, &events, &current_action_timestamp, account_id, strategy_id, retain_closed_order_snapshots](const std::string& client_order_id) {
                    const auto account_it = accounts.find(account_id);
                    if (account_it == accounts.end()) {
                        return false;
                    }
                    return cancel_backtest_order(
                        account_it->second,
                        strategy_id,
                        client_order_id,
                        current_action_timestamp.empty() ? current_timestamp() : current_action_timestamp,
                        events,
                        retain_closed_order_snapshots);
                },
                [&accounts, account_id, strategy_id](const std::string& instrument) {
                    std::vector<RuntimeOrderSnapshot> orders;
                    const auto account_it = accounts.find(account_id);
                    if (account_it == accounts.end()) {
                        return orders;
                    }
                    const auto attachment_it = account_it->second.attachments.find(strategy_id);
                    if (attachment_it == account_it->second.attachments.end()) {
                        return orders;
                    }
                    orders.reserve(attachment_it->second.opened_orders_by_id.size());
                    for (const auto& [_, order] : attachment_it->second.opened_orders_by_id) {
                        if (!instrument.empty() && order.instrument != instrument) {
                            continue;
                        }
                        orders.push_back(order);
                    }
                    return orders;
                },
                [&accounts, account_id, strategy_id](const std::string& instrument) {
                    const auto account_it = accounts.find(account_id);
                    if (account_it == accounts.end()) {
                        return 0;
                    }
                    const auto attachment_it = account_it->second.attachments.find(strategy_id);
                    if (attachment_it == account_it->second.attachments.end()) {
                        return 0;
                    }
                    const auto position_it = attachment_it->second.positions.find(instrument);
                    return position_it == attachment_it->second.positions.end() ? 0 : net_quantity(position_it->second);
                });
            initialize_backtest_attachment(accounts[account_id], runtime);
            refresh_backtest_account_snapshot(accounts[account_id]);
            strategies.push_back(std::move(runtime));
        }
    }

    for (auto& strategy : strategies) {
        strategy.instance->on_start(*strategy.context);
    }
    for (const auto& [_, account] : accounts) {
        for (const auto& [strategy_id, attachment] : account.attachments) {
            dispatch_backtest_attachment_position_updates(account, strategy_id, attachment, strategies);
        }
    }

    const auto tick_dispatch_plan = build_backtest_tick_dispatch_plan(strategies);

    std::string current_trading_day;
    std::unordered_map<std::string, PreviousBacktestTick> previous_ticks;
    const auto source_plan = collect_backtest_source_plan(base_dir, ini, strategy_sections);
    const std::size_t total_source_files = !source_plan.directory_files.empty()
        ? source_plan.directory_files.size()
        : (source_plan.csv_path.has_value() ? 1 : 0);
    std::size_t processed_ticks = 0;

    auto refresh_pending_orders_flag = [&accounts]() {
        for (const auto& [_, account] : accounts) {
            for (const auto& [__, attachment] : account.attachments) {
                if (!attachment.pending_orders.empty() || !attachment.scheduled_orders.empty()) {
                    return true;
                }
            }
        }
        return false;
    };

    bool any_pending_orders = false;

    auto process_tick = [&](const MarketTick& tick, std::size_t processed_files_hint, std::size_t total_files_hint) {
        throw_if_runtime_snapshot_cancelled(options);
        current_action_timestamp = tick.timestamp;
        if (options.include_chart) {
            update_runtime_snapshot_chart(snapshot, tick, snapshot.chart_bar_seconds);
        }
        const auto previous_tick_it = previous_ticks.find(tick.instrument);
        const std::optional<PreviousBacktestTick> previous_tick = previous_tick_it == previous_ticks.end()
            ? std::nullopt
            : std::optional<PreviousBacktestTick> {previous_tick_it->second};
        const auto next_trading_day = trading_day_label_from_tick(tick);
        if (!next_trading_day.empty() && !current_trading_day.empty() && next_trading_day != current_trading_day) {
            roll_backtest_accounts_to_next_trading_day(accounts);
            for (const auto& [_, account] : accounts) {
                for (const auto& [strategy_id, attachment] : account.attachments) {
                    dispatch_backtest_attachment_position_updates(account, strategy_id, attachment, strategies);
                }
            }
        }
        if (!next_trading_day.empty()) {
            current_trading_day = next_trading_day;
        }

        if (any_pending_orders) {
            for (auto& [_, account] : accounts) {
                activate_backtest_scheduled_orders(account, tick, events);
            }
            for (auto& [_, account] : accounts) {
                service_backtest_pending_orders(account, tick, previous_tick, events, retain_closed_order_snapshots);
            }
        }
        if (!events.empty()) {
            for (const auto& event : events) {
                dispatch_event_to_strategies(event, strategies);
                if (is_trade_fill_event(event)) {
                    const auto account_it = accounts.find(event.account_id);
                    if (account_it == accounts.end()) {
                        continue;
                    }
                    const auto attachment_it = account_it->second.attachments.find(event.strategy_id);
                    if (attachment_it == account_it->second.attachments.end()) {
                        continue;
                    }
                    const auto position_it = attachment_it->second.positions.find(event.instrument);
                    if (position_it == attachment_it->second.positions.end()) {
                        continue;
                    }
                    dispatch_position_update_to_strategies(
                        make_runtime_position_snapshot(event.instrument, event.account_id, event.strategy_id, position_it->second),
                        strategies);
                }
            }
            events.clear();
            any_pending_orders = refresh_pending_orders_flag();
        }

        dispatch_backtest_tick(tick, tick_dispatch_plan);

        auto queued_orders = std::move(order_queue);
        order_queue.clear();
        for (const auto& request : queued_orders) {
            const auto account_it = accounts.find(request.account_id);
            if (account_it == accounts.end()) {
                const auto rejected = make_event(request, "unknown", OrderStatus::Rejected, 0, 0.0, "unknown account", tick.timestamp);
                events.push_back(rejected);
                continue;
            }
            submit_backtest_order(account_it->second, request, tick, previous_tick, events, retain_closed_order_snapshots);
        }
        for (const auto& event : events) {
            dispatch_event_to_strategies(event, strategies);
            if (is_trade_fill_event(event)) {
                const auto account_it = accounts.find(event.account_id);
                if (account_it == accounts.end()) {
                    continue;
                }
                const auto attachment_it = account_it->second.attachments.find(event.strategy_id);
                if (attachment_it == account_it->second.attachments.end()) {
                    continue;
                }
                const auto position_it = attachment_it->second.positions.find(event.instrument);
                if (position_it == attachment_it->second.positions.end()) {
                    continue;
                }
                dispatch_position_update_to_strategies(
                    make_runtime_position_snapshot(event.instrument, event.account_id, event.strategy_id, position_it->second),
                    strategies);
            }
        }
        events.clear();
        if (!queued_orders.empty()) {
            any_pending_orders = refresh_pending_orders_flag();
        }

        previous_ticks[tick.instrument] = PreviousBacktestTick {
            .timestamp_ms = tick.timestamp_ms,
            .last = tick.last,
            .bid = tick.bid,
            .ask = tick.ask,
            .volume = tick.volume,
            .turnover = tick.turnover,
            .bid_size = tick.bid_size,
            .ask_size = tick.ask_size,
        };

        ++processed_ticks;
        if (processed_ticks == 1 || processed_ticks % 50000 == 0) {
            report_runtime_snapshot_progress(options, "replaying_ticks", processed_files_hint, total_files_hint, processed_ticks);
        }
    };

    try {
        if (!source_plan.directory_files.empty() && !source_plan.csv_path.has_value()) {
            report_runtime_snapshot_progress(options, "opening_backtest_files", 0, total_source_files, 0);
            BacktestDirectoryTickStream tick_stream(source_plan);
            MarketTick tick;
            while (tick_stream.next(tick)) {
                process_tick(tick, tick_stream.loaded_source_files(), tick_stream.total_source_files());
            }
            if (!tick_stream.emitted_any()) {
                throw std::runtime_error("Backtest data source did not yield any tick rows after streaming directory files.");
            }
        } else {
            report_runtime_snapshot_progress(options, "loading_backtest_ticks", 0, total_source_files, 0);
            const auto ticks = load_backtest_ticks(base_dir, ini, strategy_sections);
            report_runtime_snapshot_progress(options, "replaying_ticks", total_source_files, total_source_files, 0);
            for (const auto& tick : ticks) {
                process_tick(tick, total_source_files, total_source_files);
            }
        }
    } catch (...) {
        for (auto& strategy : strategies) {
            strategy.instance->on_stop(*strategy.context);
        }
        throw;
    }

    for (auto& strategy : strategies) {
        strategy.instance->on_stop(*strategy.context);
    }

    report_runtime_snapshot_progress(options, "finalizing_snapshot", total_source_files, total_source_files, processed_ticks);

    snapshot.accounts.clear();
    for (const auto& [_, account] : accounts) {
        snapshot.accounts.push_back(account.snapshot);
    }

    for (const auto& strategy : strategies) {
        const auto account_it = accounts.find(strategy.account_id);
        if (account_it == accounts.end()) {
            continue;
        }
        const auto attachment_it = account_it->second.attachments.find(strategy.strategy_id);
        if (attachment_it == account_it->second.attachments.end()) {
            continue;
        }
        snapshot.strategy_attachments.push_back(make_backtest_attachment_snapshot(
            strategy,
            attachment_it->second,
            options.include_order_history));

        if (options.include_chart) {
            append_strategy_chart_indicator_series(snapshot, strategy);
        }
    }

    return snapshot;
}

int run_application(
    const std::string& mode_text,
    const std::filesystem::path& config_path,
    const std::filesystem::path& backtest_output_dir) {
    std::unique_ptr<BacktestCliOutputSession> backtest_output;
    try {
        const auto normalized_config = std::filesystem::absolute(config_path);
        const auto ini = IniFile::parse(normalized_config);
        const Mode mode = mode_text.empty() ? parse_mode(ini.get("platform", "mode", "backtest")) : parse_mode(mode_text);

        if (mode == Mode::Backtest && !backtest_output_dir.empty()) {
            backtest_output = std::make_unique<BacktestCliOutputSession>(normalized_config, backtest_output_dir);
        }

        std::cout << "Running iTrader in " << to_string(mode) << " mode using " << normalized_config.string() << "\n";
        if (mode == Mode::Backtest) {
            return run_backtest(normalized_config, ini, backtest_output.get());
        }

#ifdef ITRADER_ENABLE_CTP
        return run_live(normalized_config, ini);
#else
        std::cerr << "Live mode is not available in this build because the CTP SDK was not enabled.\n";
        return 2;
#endif
    } catch (const std::exception& ex) {
        std::cerr << "Fatal error: " << ex.what() << '\n';
        return 1;
    }
}

} // namespace itrader
