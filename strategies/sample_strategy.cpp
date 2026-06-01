#include "itrader/strategy_api.hpp"

#include <algorithm>
#include <deque>
#include <numeric>
#include <unordered_map>

namespace {

class MovingAverageStrategy final : public itrader::IStrategy {
public:
    [[nodiscard]] const char* name() const override {
        return "sample_moving_average";
    }

    void on_init(const std::unordered_map<std::string, std::string>& parameters, itrader::IStrategyContext& context) override {
        if (const auto fast_it = parameters.find("fast_window"); fast_it != parameters.end()) {
            fast_window_ = std::max(2, std::stoi(fast_it->second));
        }
        if (const auto slow_it = parameters.find("slow_window"); slow_it != parameters.end()) {
            slow_window_ = std::max(fast_window_ + 1, std::stoi(slow_it->second));
        }
        if (const auto quantity_it = parameters.find("quantity"); quantity_it != parameters.end()) {
            quantity_ = std::max(1, std::stoi(quantity_it->second));
        }

        context.log("Initialized sample moving-average strategy: fast=" + std::to_string(fast_window_) +
            ", slow=" + std::to_string(slow_window_) +
            ", quantity=" + std::to_string(quantity_));
    }

    void on_tick(const itrader::MarketTick& tick, itrader::IStrategyContext& context) override {
        auto& history = history_[tick.instrument];
        history.push_back(tick.last);
        while (static_cast<int>(history.size()) > slow_window_) {
            history.pop_front();
        }

        if (static_cast<int>(history.size()) < slow_window_) {
            return;
        }

        const double fast_average = average(history, history.size() - static_cast<std::size_t>(fast_window_));
        const double slow_average = average(history, 0);

        int desired_signal = 0;
        if (fast_average > slow_average) {
            desired_signal = 1;
        } else if (fast_average < slow_average) {
            desired_signal = -1;
        }

        auto& last_signal = last_signal_[tick.instrument];
        if (desired_signal == 0 || desired_signal == last_signal) {
            return;
        }

        const int position = context.net_position(tick.instrument);
        if (desired_signal > 0) {
            if (position < 0) {
                itrader::OrderRequest request;
                request.instrument = tick.instrument;
                request.exchange = tick.exchange;
                request.side = itrader::Side::Buy;
                request.offset = itrader::Offset::Close;
                request.price_type = itrader::PriceType::Market;
                request.volume = std::abs(position);
                (void)context.send_order(request);
            } else if (position == 0) {
                itrader::OrderRequest request;
                request.instrument = tick.instrument;
                request.exchange = tick.exchange;
                request.side = itrader::Side::Buy;
                request.offset = itrader::Offset::Open;
                request.price_type = itrader::PriceType::Market;
                request.volume = quantity_;
                (void)context.send_order(request);
            }
        } else {
            if (position > 0) {
                itrader::OrderRequest request;
                request.instrument = tick.instrument;
                request.exchange = tick.exchange;
                request.side = itrader::Side::Sell;
                request.offset = itrader::Offset::Close;
                request.price_type = itrader::PriceType::Market;
                request.volume = position;
                (void)context.send_order(request);
            } else if (position == 0) {
                itrader::OrderRequest request;
                request.instrument = tick.instrument;
                request.exchange = tick.exchange;
                request.side = itrader::Side::Sell;
                request.offset = itrader::Offset::Open;
                request.price_type = itrader::PriceType::Market;
                request.volume = quantity_;
                (void)context.send_order(request);
            }
        }

        last_signal = desired_signal;
    }

private:
    static double average(const std::deque<double>& values, std::size_t begin_index) {
        const auto first = values.begin() + static_cast<std::ptrdiff_t>(begin_index);
        const auto sum = std::accumulate(first, values.end(), 0.0);
        return sum / static_cast<double>(values.end() - first);
    }

    int fast_window_ {5};
    int slow_window_ {12};
    int quantity_ {1};
    std::unordered_map<std::string, std::deque<double>> history_;
    std::unordered_map<std::string, int> last_signal_;
};

const itrader::StrategyParameterDescriptor kSampleStrategyParameters[] = {
    {"fast_window", "int", "5", "Fast moving-average window."},
    {"slow_window", "int", "12", "Slow moving-average window."},
    {"quantity", "int", "1", "Order quantity for each signal."},
};

const itrader::StrategyParameterSchema kSampleStrategySchema {
    "sample_moving_average",
    kSampleStrategyParameters,
    sizeof(kSampleStrategyParameters) / sizeof(kSampleStrategyParameters[0])
};

} // namespace

extern "C" ITRADER_STRATEGY_EXPORT itrader::IStrategy* CreateStrategy() {
    return new MovingAverageStrategy();
}

extern "C" ITRADER_STRATEGY_EXPORT void DestroyStrategy(itrader::IStrategy* strategy) {
    delete strategy;
}

extern "C" ITRADER_STRATEGY_EXPORT const itrader::StrategyParameterSchema* GetStrategySchema() {
    return &kSampleStrategySchema;
}
