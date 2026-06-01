#pragma once

#include "itrader/domain.hpp"
#include "itrader/order_slot_state.hpp"
#include "itrader/runtime_snapshot.hpp"

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
    #ifdef ITRADER_BUILD_STRATEGY
        #define ITRADER_STRATEGY_EXPORT __declspec(dllexport)
    #else
        #define ITRADER_STRATEGY_EXPORT __declspec(dllimport)
    #endif
#else
    #define ITRADER_STRATEGY_EXPORT
#endif

namespace itrader {

using StrategyStateMap = std::unordered_map<std::string, std::string>;

class IStrategyContext {
public:
    virtual ~IStrategyContext() = default;

    virtual void log(const std::string& message) = 0;
    [[nodiscard]] virtual std::string account_id() const = 0;
    [[nodiscard]] virtual Mode mode() const = 0;
    [[nodiscard]] virtual bool submit_intent(const OrderIntent& intent) = 0;
    [[nodiscard]] virtual bool cancel_order(const std::string& client_order_id) = 0;
    [[nodiscard]] virtual bool send_order(const OrderRequest& request) = 0;
    [[nodiscard]] virtual std::vector<RuntimeOrderSnapshot> open_orders(const std::string& instrument = {}) const = 0;
    [[nodiscard]] virtual int net_position(const std::string& instrument) const = 0;
    virtual void plot_indicator(
        const std::string& instrument,
        const std::string& indicator_id,
        long long timestamp_ms,
        double value,
        const std::string& label,
        const std::string& color) = 0;
};

class IStrategy {
public:
    virtual ~IStrategy() = default;

    [[nodiscard]] virtual const char* name() const = 0;
    virtual void on_init(const std::unordered_map<std::string, std::string>& parameters, IStrategyContext& context) = 0;
    virtual void on_start(IStrategyContext& context) { (void)context; }
    virtual void on_tick(const MarketTick& tick, IStrategyContext& context) = 0;
    virtual void on_order_update(const RuntimeOrderSnapshot& order, IStrategyContext& context) { (void)order; (void)context; }
    virtual void on_position_update(const RuntimePositionSnapshot& position, IStrategyContext& context) { (void)position; (void)context; }
    virtual void on_order_event(const OrderEvent& event, IStrategyContext& context) { (void)event; (void)context; }
    virtual void replay_live_recovered_trades(const std::vector<RuntimeOrderSnapshot>& trades, IStrategyContext& context) { (void)trades; (void)context; }
    [[nodiscard]] virtual StrategyStateMap capture_live_state(IStrategyContext& context) const { (void)context; return {}; }
    virtual void restore_live_state(const StrategyStateMap& state, IStrategyContext& context) { (void)state; (void)context; }
    virtual void on_stop(IStrategyContext& context) { (void)context; }
};

struct StrategyParameterDescriptor {
    const char* key;
    const char* type;
    const char* default_value;
    const char* description;
};

struct StrategyParameterSchema {
    const char* strategy_name;
    const StrategyParameterDescriptor* parameters;
    std::size_t parameter_count;
};

using CreateStrategyFn = IStrategy* (*)();
using DestroyStrategyFn = void (*)(IStrategy*);
using GetStrategySchemaFn = const StrategyParameterSchema* (*)();

inline constexpr const char* kCreateStrategySymbol = "CreateStrategy";
inline constexpr const char* kDestroyStrategySymbol = "DestroyStrategy";
inline constexpr const char* kGetStrategySchemaSymbol = "GetStrategySchema";

} // namespace itrader
