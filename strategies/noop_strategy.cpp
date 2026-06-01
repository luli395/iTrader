#include "itrader/strategy_api.hpp"

#include <unordered_map>

namespace {

class NoopStrategy final : public itrader::IStrategy {
public:
    [[nodiscard]] const char* name() const override {
        return "noop_strategy";
    }

    void on_init(const std::unordered_map<std::string, std::string>&, itrader::IStrategyContext& context) override {
        context.log("Noop strategy initialized; live probe mode will observe broker state without sending orders.");
    }

    void on_start(itrader::IStrategyContext& context) override {
        context.log("Noop strategy started on account " + context.account_id());
    }

    void on_tick(const itrader::MarketTick&, itrader::IStrategyContext&) override {
    }

    void on_stop(itrader::IStrategyContext& context) override {
        context.log("Noop strategy stopped on account " + context.account_id());
    }
};

const itrader::StrategyParameterSchema kNoopStrategySchema {
    "noop_strategy",
    nullptr,
    0
};

} // namespace

extern "C" ITRADER_STRATEGY_EXPORT itrader::IStrategy* CreateStrategy() {
    return new NoopStrategy();
}

extern "C" ITRADER_STRATEGY_EXPORT void DestroyStrategy(itrader::IStrategy* strategy) {
    delete strategy;
}

extern "C" ITRADER_STRATEGY_EXPORT const itrader::StrategyParameterSchema* GetStrategySchema() {
    return &kNoopStrategySchema;
}