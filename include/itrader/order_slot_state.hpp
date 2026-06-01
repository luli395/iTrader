#pragma once

#include "itrader/runtime_snapshot.hpp"

#include <algorithm>
#include <optional>
#include <string>
#include <utility>

namespace itrader {

struct OrderSlotState {
    std::optional<std::string> client_order_id;
    std::optional<std::string> broker_order_id;
    std::optional<int> signal_bar_index;
    std::optional<int> direction;
    int filled_volume {0};
};

[[nodiscard]] inline bool has_active_order(const OrderSlotState& state) {
    return state.client_order_id.has_value();
}

[[nodiscard]] inline bool matches_order_update(const OrderSlotState& state, const RuntimeOrderSnapshot& order) {
    return (state.client_order_id.has_value() && order.client_order_id == *state.client_order_id)
        || (state.broker_order_id.has_value() && order.order_id == *state.broker_order_id);
}

[[nodiscard]] inline int filled_volume_delta(const OrderSlotState& state, const RuntimeOrderSnapshot& order) {
    return std::max(0, order.filled_volume - state.filled_volume);
}

inline void record_order_update(OrderSlotState& state, const RuntimeOrderSnapshot& order) {
    if (!order.order_id.empty()) {
        state.broker_order_id = order.order_id;
    }
    if (order.filled_price > 0.0) {
        state.filled_volume = std::max(state.filled_volume, order.filled_volume);
    }
}

inline void clear_order_slot(OrderSlotState& state) {
    state = OrderSlotState {};
}

inline void mark_order_submitted(
    OrderSlotState& state,
    std::string client_order_id,
    int signal_bar_index,
    std::optional<int> direction = std::nullopt) {

    state.client_order_id = std::move(client_order_id);
    state.broker_order_id.reset();
    state.signal_bar_index = signal_bar_index;
    state.direction = direction;
    state.filled_volume = 0;
}

template <typename CancelOrderFn>
bool cancel_order_slot(OrderSlotState& state, CancelOrderFn&& cancel_order) {
    bool success = true;
    if (state.client_order_id.has_value()) {
        success = static_cast<bool>(cancel_order(*state.client_order_id));
    }
    clear_order_slot(state);
    return success;
}

} // namespace itrader
