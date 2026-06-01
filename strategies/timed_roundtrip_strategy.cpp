#include "itrader/strategy_api.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string_view>
#include <unordered_map>

namespace {

std::optional<std::chrono::system_clock::time_point> parse_tick_timestamp(std::string_view raw_timestamp) {
    const std::string trimmed = itrader::trim_copy(raw_timestamp);
    if (trimmed.empty()) {
        return std::nullopt;
    }

    auto try_parse = [](std::string_view candidate, const char* format) -> std::optional<std::chrono::system_clock::time_point> {
        std::tm parsed_time {};
        std::istringstream input {std::string(candidate)};
        input >> std::get_time(&parsed_time, format);
        if (input.fail()) {
            return std::nullopt;
        }

        const std::time_t local_time = std::mktime(&parsed_time);
        if (local_time == static_cast<std::time_t>(-1)) {
            return std::nullopt;
        }
        return std::chrono::system_clock::from_time_t(local_time);
    };

    if (trimmed.size() >= 19 && trimmed[4] == '-' && trimmed[7] == '-') {
        return try_parse(trimmed.substr(0, 19), "%Y-%m-%d %H:%M:%S");
    }

    if (trimmed.size() >= 14
        && std::all_of(trimmed.begin(), trimmed.begin() + 14, [](unsigned char ch) { return std::isdigit(ch) != 0; })) {
        return try_parse(trimmed.substr(0, 14), "%Y%m%d%H%M%S");
    }

    if (trimmed.size() >= 17 && std::isdigit(static_cast<unsigned char>(trimmed[0])) != 0 && std::isdigit(static_cast<unsigned char>(trimmed[7])) != 0) {
        return try_parse(trimmed.substr(0, 17), "%Y%m%d %H:%M:%S");
    }

    return std::nullopt;
}

bool is_terminal_status(itrader::OrderStatus status) {
    return status == itrader::OrderStatus::Filled || status == itrader::OrderStatus::Cancelled || status == itrader::OrderStatus::Rejected;
}

class TimedRoundtripStrategy final : public itrader::IStrategy {
public:
    [[nodiscard]] const char* name() const override {
        return "timed_roundtrip";
    }

    void on_init(const std::unordered_map<std::string, std::string>& parameters, itrader::IStrategyContext& context) override {
        if (const auto interval_it = parameters.find("interval_seconds"); interval_it != parameters.end()) {
            interval_seconds_ = std::max(1, std::stoi(interval_it->second));
        }
        if (const auto quantity_it = parameters.find("quantity"); quantity_it != parameters.end()) {
            quantity_ = std::max(1, std::stoi(quantity_it->second));
        }
        if (const auto price_offset_it = parameters.find("price_offset"); price_offset_it != parameters.end()) {
            price_offset_ = std::max(0.0, std::stod(price_offset_it->second));
        }

        context.log("Initialized timed round-trip strategy: interval_seconds=" + std::to_string(interval_seconds_) +
            ", quantity=" + std::to_string(quantity_) +
            ", price_offset=" + std::to_string(price_offset_));
    }

    void on_start(itrader::IStrategyContext& context) override {
        context.log("Timed round-trip strategy started for account " + context.account_id());
    }

    void on_tick(const itrader::MarketTick& tick, itrader::IStrategyContext& context) override {
        auto& state = instrument_states_[tick.instrument];
        if (state.order_in_flight) {
            return;
        }

        const auto action_time = parse_tick_timestamp(tick.timestamp).value_or(std::chrono::system_clock::now());
        if (state.last_action_time.has_value()) {
            if (action_time <= *state.last_action_time) {
                return;
            }

            const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(action_time - *state.last_action_time);
            if (elapsed.count() < interval_seconds_) {
                return;
            }
        }

        itrader::OrderRequest request;
        request.instrument = tick.instrument;
        request.exchange = tick.exchange;
        request.price_type = itrader::PriceType::Limit;
        request.immediate_or_cancel = true;

        const int net_position = context.net_position(tick.instrument);
        std::string action_description;
        if (net_position > 0) {
            request.side = itrader::Side::Sell;
            request.offset = itrader::Offset::CloseToday;
            request.volume = net_position;
            request.limit_price = (tick.bid > 0.0 ? tick.bid : tick.last) - price_offset_;
            action_description = "submit sell-to-close for " + tick.instrument + " volume=" + std::to_string(request.volume) +
                " IOC @ " + std::to_string(request.limit_price);
        } else if (net_position < 0) {
            request.side = itrader::Side::Buy;
            request.offset = itrader::Offset::CloseToday;
            request.volume = std::abs(net_position);
            request.limit_price = (tick.ask > 0.0 ? tick.ask : tick.last) + price_offset_;
            action_description = "submit buy-to-cover for " + tick.instrument + " volume=" + std::to_string(request.volume) +
                " IOC @ " + std::to_string(request.limit_price);
        } else {
            request.side = itrader::Side::Buy;
            request.offset = itrader::Offset::Open;
            request.volume = quantity_;
            request.limit_price = (tick.ask > 0.0 ? tick.ask : tick.last) + price_offset_;
            action_description = "submit opening buy for " + tick.instrument + " volume=" + std::to_string(request.volume) +
                " IOC @ " + std::to_string(request.limit_price);
        }

        state.last_action_time = action_time;
        const bool accepted = context.send_order(request);
        if (accepted) {
            state.order_in_flight = true;
            context.log("Timed round-trip cycle: " + action_description + " at " + tick.timestamp);
        } else {
            context.log("Timed round-trip cycle skipped because order submission failed for " + tick.instrument);
        }
    }

    void on_order_event(const itrader::OrderEvent& event, itrader::IStrategyContext& context) override {
        const auto state_it = instrument_states_.find(event.instrument);
        if (state_it == instrument_states_.end()) {
            return;
        }

        if (is_terminal_status(event.status)) {
            state_it->second.order_in_flight = false;
            context.log("Order cycle completed for " + event.instrument + ": status=" + itrader::to_string(event.status) +
                ", filled_volume=" + std::to_string(event.filled_volume) +
                ", message=" + event.message);
        }
    }

    void on_stop(itrader::IStrategyContext& context) override {
        context.log("Timed round-trip strategy stopped for account " + context.account_id());
    }

private:
    struct InstrumentState {
        bool order_in_flight {false};
        std::optional<std::chrono::system_clock::time_point> last_action_time;
    };

    int interval_seconds_ {10};
    int quantity_ {1};
    double price_offset_ {2.0};
    std::unordered_map<std::string, InstrumentState> instrument_states_;
};

const itrader::StrategyParameterDescriptor kTimedRoundtripStrategyParameters[] = {
    {"interval_seconds", "int", "10", "Minimum seconds between round-trip actions."},
    {"quantity", "int", "1", "Order quantity for each cycle."},
    {"price_offset", "double", "2.0", "IOC limit-price offset from the touch."},
};

const itrader::StrategyParameterSchema kTimedRoundtripStrategySchema {
    "timed_roundtrip",
    kTimedRoundtripStrategyParameters,
    sizeof(kTimedRoundtripStrategyParameters) / sizeof(kTimedRoundtripStrategyParameters[0])
};

} // namespace

extern "C" ITRADER_STRATEGY_EXPORT itrader::IStrategy* CreateStrategy() {
    return new TimedRoundtripStrategy();
}

extern "C" ITRADER_STRATEGY_EXPORT void DestroyStrategy(itrader::IStrategy* strategy) {
    delete strategy;
}

extern "C" ITRADER_STRATEGY_EXPORT const itrader::StrategyParameterSchema* GetStrategySchema() {
    return &kTimedRoundtripStrategySchema;
}