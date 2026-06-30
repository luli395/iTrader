# iTrader 策略编写指南

本文面向以后在 `iTrader` 中新增策略的开发者。

核心原则只有一句话：

- 策略只负责交易逻辑
- 订单和持仓由平台维护
- 策略通过回调接收订单和持仓更新

也就是说，以后写策略时，推荐把代码组织成：

- `on_tick()`：只做行情处理、bar 聚合、信号判断、下单意图生成
- `on_order_update()`：处理订单状态变化、成交进度、在途单清理
- `on_position_update()`：同步当前持仓真相
- `on_order_event()`：保留给日志、兼容、调试

## 1. 相关头文件

以后写策略，最常用的公共接口在这些头文件里：

- `include/itrader/strategy_api.hpp`
- `include/itrader/domain.hpp`
- `include/itrader/runtime_snapshot.hpp`
- `include/itrader/order_slot_state.hpp`

它们分别负责：

- `strategy_api.hpp`
  策略 ABI、生命周期回调、策略上下文接口
- `domain.hpp`
  tick、订单、事件、枚举等基础类型
- `runtime_snapshot.hpp`
  平台推给策略的订单/持仓快照类型
- `order_slot_state.hpp`
  可复用的在途单 slot 小组件

## 2. 策略 ABI

每个策略 DLL 都要实现 `itrader::IStrategy`，并导出两个固定符号：

- `CreateStrategy`
- `DestroyStrategy`

最小骨架如下：

```cpp
#include "itrader/strategy_api.hpp"

class MyStrategy final : public itrader::IStrategy {
public:
    [[nodiscard]] const char* name() const override {
        return "my_strategy";
    }

    void on_init(const std::unordered_map<std::string, std::string>& parameters,
                 itrader::IStrategyContext& context) override {
        (void)parameters;
        context.log("initialized");
    }

    void on_tick(const itrader::MarketTick& tick, itrader::IStrategyContext& context) override {
        (void)tick;
        (void)context;
    }
};

extern "C" ITRADER_STRATEGY_EXPORT itrader::IStrategy* CreateStrategy() {
    return new MyStrategy();
}

extern "C" ITRADER_STRATEGY_EXPORT void DestroyStrategy(itrader::IStrategy* strategy) {
    delete strategy;
}
```

## 3. 生命周期回调

`itrader::IStrategy` 现在有这些回调：

```cpp
[[nodiscard]] virtual const char* name() const = 0;
virtual void on_init(const std::unordered_map<std::string, std::string>& parameters,
                     IStrategyContext& context) = 0;
virtual void on_start(IStrategyContext& context);
virtual void on_tick(const MarketTick& tick, IStrategyContext& context) = 0;
virtual void on_order_update(const RuntimeOrderSnapshot& order, IStrategyContext& context);
virtual void on_position_update(const RuntimePositionSnapshot& position, IStrategyContext& context);
virtual void on_order_event(const OrderEvent& event, IStrategyContext& context);
virtual void on_stop(IStrategyContext& context);
```

推荐职责如下。

### `name()`

返回策略名。

建议：

- 简短
- 稳定
- 不要依赖配置动态变化

### `on_init(parameters, context)`

初始化阶段。

适合做：

- 读取参数
- 初始化本地缓存
- 打印启动参数

不适合做：

- 发单
- 依赖实时行情

### `on_start(context)`

策略实例真正开始运行前的回调。

适合做：

- 打一条启动日志
- 做轻量级状态准备

### `on_tick(tick, context)`

行情主入口。

以后写策略时，应该把这里当成“纯策略逻辑”入口。

适合做：

- 解析 tick 时间
- 维护 bar / 指标 / 信号缓存
- 根据当前 runtime 持仓和在途单判断是否应该发新意图
- 调 `submit_intent(...)`

不建议做：

- 把本地 `position` 当最终真相
- 把本地 `order` 当最终真相
- 在这里自己做完整撮合引擎

### `on_order_update(order, context)`

订单快照更新回调。

这是以后推荐的“订单状态主入口”。

适合做：

- 跟踪某张在途单是否还活着
- 计算本次新增成交量 `fill delta`
- 在订单终态时清 slot
- 更新本地统计数据

典型会收到的状态：

- `submitted`
- `accepted`
- `partially_filled`
- `filled`
- `cancelled`
- `rejected`

### `on_position_update(position, context)`

持仓快照更新回调。

这是以后推荐的“持仓真相主入口”。

适合做：

- 更新当前策略关注标的的 runtime 持仓视图
- 在持仓被平台更新后，刷新策略内部状态
- 根据 `net / average_price / today / yesterday` 做逻辑判断

### `on_order_event(event, context)`

订单事件回调。

它比 `on_order_update()` 更接近原始事件。

建议：

- 主要用于日志和兼容
- 不再把它当唯一的订单状态入口

### `on_stop(context)`

停止回调。

适合做：

- 打印统计结果
- 释放本地资源

## 4. 策略上下文接口

`itrader::IStrategyContext` 目前提供这些函数：

```cpp
virtual void log(const std::string& message) = 0;
[[nodiscard]] virtual std::string account_id() const = 0;
[[nodiscard]] virtual Mode mode() const = 0;
[[nodiscard]] virtual bool submit_intent(const OrderIntent& intent) = 0;
[[nodiscard]] virtual bool cancel_order(const std::string& client_order_id) = 0;
[[nodiscard]] virtual bool send_order(const OrderRequest& request) = 0;
[[nodiscard]] virtual std::vector<RuntimeOrderSnapshot> open_orders(const std::string& instrument = {}) const = 0;
[[nodiscard]] virtual int net_position(const std::string& instrument) const = 0;
```

### 推荐使用顺序

优先使用：

- `submit_intent(...)`
- `cancel_order(...)`
- `on_order_update(...)`
- `on_position_update(...)`

兼容/补充用途：

- `send_order(...)`
- `open_orders(...)`
- `net_position(...)`

### `log(message)`

打印策略日志。

适合：

- 初始化参数
- 关键状态变化
- 拒单原因

### `account_id()`

返回当前策略实例绑定的账户 ID。

### `mode()`

返回当前运行模式：

- `itrader::Mode::Backtest`
- `itrader::Mode::Live`

建议：

- 以后新策略尽量不要写大量 `if (context.mode() == ...)`
- 把 backtest/live 差异尽量留给平台处理

### `submit_intent(intent)`

推荐的下单接口。

策略提交的是“意图”，平台决定如何执行。

在 backtest/live 下都应该优先用它，而不是自己区分两套代码。

### `cancel_order(client_order_id)`

撤掉某张在途单。

一般和 `OrderSlotState` 配套使用。

### `send_order(request)`

更底层的订单接口。

建议：

- 新策略优先用 `submit_intent(...)`
- `send_order(...)` 只在确实需要直接构造底层请求时用

### `open_orders(instrument)`

获取当前策略可见的开放订单快照。

用途：

- 调试
- 状态校验
- 少量兼容逻辑

### `net_position(instrument)`

兼容性接口，返回净持仓摘要。

新策略如果已经实现 `on_position_update()`，通常不应该再把它当唯一持仓来源。

## 5. 常用数据结构

### `MarketTick`

来自 `domain.hpp`。

常用字段：

- `timestamp`
- `timestamp_ms`
- `trading_day`
- `instrument`
- `exchange`
- `last`
- `bid`
- `ask`
- `volume`
- `turnover`
- `bid_size`
- `ask_size`

### `OrderIntent`

推荐的策略发单结构。

常用字段：

- `client_order_id`
- `instrument`
- `exchange`
- `side`
- `offset`
- `price_type`
- `immediate_or_cancel`
- `limit_price`
- `volume`
- `activate_at_ms`
- `execution_policy`
- `expected_fill_price`

### `RuntimeOrderSnapshot`

平台推给策略的订单状态快照。

常用字段：

- `order_id`
- `client_order_id`
- `instrument`
- `exchange`
- `side`
- `offset`
- `requested_volume`
- `filled_volume`
- `limit_price`
- `filled_price`
- `status`
- `message`
- `timestamp`

### `RuntimePositionSnapshot`

平台推给策略的持仓快照。

常用字段：

- `instrument`
- `long_today_quantity`
- `long_yesterday_quantity`
- `long_quantity`
- `long_average_price`
- `short_today_quantity`
- `short_yesterday_quantity`
- `short_quantity`
- `short_average_price`
- `net`
- `average_price`

## 6. 常用枚举

来自 `domain.hpp`。

### `Mode`

- `Backtest`
- `Live`

### `Side`

- `Buy`
- `Sell`

### `Offset`

- `Open`
- `Close`
- `CloseToday`
- `CloseYesterday`

如果你写的是中国期货策略，建议优先显式使用：

- `CloseToday`
- `CloseYesterday`

不要把所有平仓都粗暴写成 `Close`。

### `PriceType`

- `Market`
- `Limit`

### `IntentExecutionPolicy`

- `NativeOrder`
- `RuntimeSyntheticFill`

对“以后新策略”的建议是：

- 尽量不要在策略里主动依赖 backtest-only 语义
- 能让平台自己决定执行方式时，就不要把策略写死到某种撮合路径

### `OrderStatus`

- `Submitted`
- `Accepted`
- `PartiallyFilled`
- `Filled`
- `Cancelled`
- `Rejected`

## 7. 推荐的策略状态分层

以后写策略时，建议把状态分成三层。

### 1. 信号状态

例如：

- bar 缓存
- 指标窗口
- breakout 上下沿
- re-entry lock
- 时段过滤

这部分应该由策略自己维护。

### 2. 执行适配状态

例如：

- 当前 entry slot 是否有在途单
- 当前 exit slot 是否有在途单
- 在途单已成交多少

这部分建议用 `itrader::OrderSlotState` 这类小组件维护。

### 3. 执行真相

例如：

- 当前真实持仓
- 当前订单状态

这部分应该由平台维护，并通过：

- `on_order_update()`
- `on_position_update()`

推给策略。

## 8. `OrderSlotState` 怎么用

现在公共层已经有：

- `include/itrader/order_slot_state.hpp`

它是一个轻量级“在途单 slot”组件。

它不关心你是 entry、exit、stop-loss 还是 take-profit。

策略自己决定 slot 的业务含义。

### 结构

```cpp
itrader::OrderSlotState
```

字段含义：

- `client_order_id`
  当前 slot 绑定的订单 id
- `signal_bar_index`
  这张单由哪根信号 bar 生成
- `direction`
  可选方向信息
- `filled_volume`
  当前累计已成交量

### helper

公共 helper 包括：

- `itrader::has_active_order(slot)`
- `itrader::matches_order_update(slot, order)`
- `itrader::filled_volume_delta(slot, order)`
- `itrader::record_order_update(slot, order)`
- `itrader::mark_order_submitted(slot, client_order_id, signal_bar_index, direction)`
- `itrader::clear_order_slot(slot)`
- `itrader::cancel_order_slot(slot, cancel_fn)`

### 推荐写法

```cpp
itrader::OrderSlotState entry_order_;
itrader::OrderSlotState exit_order_;

if (!runtime_position_.is_open && !itrader::has_active_order(entry_order_)) {
    const std::string client_order_id = next_client_order_id(...);
    if (context.submit_intent(intent)) {
        itrader::mark_order_submitted(entry_order_, client_order_id, signal_bar_index, 1);
    }
}

if (itrader::matches_order_update(entry_order_, order)) {
    const int fill_delta = itrader::filled_volume_delta(entry_order_, order);
    itrader::record_order_update(entry_order_, order);
    if (order.status == itrader::OrderStatus::Filled ||
        order.status == itrader::OrderStatus::Cancelled ||
        order.status == itrader::OrderStatus::Rejected) {
        itrader::clear_order_slot(entry_order_);
    }
}
```

## 9. 推荐写法：把策略写成“纯信号函数”

以后新策略推荐遵循下面的模式。

### 在 `on_tick()` 里

- 聚合 bar
- 计算指标
- 看 `runtime_position`
- 看 `entry_order` / `exit_order`
- 决定要不要发新的 `OrderIntent`

### 在 `on_order_update()` 里

- 识别是不是自己关心的 slot
- 计算 fill delta
- 更新本地 trade 统计
- 清理终态 slot

### 在 `on_position_update()` 里

- 更新 `runtime_position`
- 如有必要，把本地信号状态和 runtime 持仓同步

## 10. 不推荐的写法

以后尽量避免这些模式：

- 在策略里自己维护一份独立的“最终持仓真相”
- 在策略里写 backtest-only 本地撮合
- 在策略里到处写 `if (context.mode() == ...)`
- 在策略里写平台级别的 session/file-boundary 强平语义
- 同时用很多散落的 `active_xxx_order_id` 字段，而不是收成 slot

## 11. 配置参数怎么传进策略

INI 中 `[strategy.xxx]` 节下，除了保留字段外，其他键值都会传进 `on_init(parameters, context)`。

保留字段一般包括：

- `dll`
- `accounts`
- `account`
- `instruments`
- `order_ref_strategy_code`（live 模式下用于编码进 `OrderRef` 后两位；同一绑定账户内需唯一，但不要求跨账户全局唯一）
- `strategy_code`（`order_ref_strategy_code` 的兼容别名，建议优先写前者）

其他例如：

- `quantity`
- `lookback`
- `threshold`
- `tick_size`

都可以由策略自行解析。

## 12. CMake 和 DLL 构建

新增策略时，一般需要：

1. 新建 `strategies/<name>.cpp`
2. 在 `CMakeLists.txt` 里增加一个 `add_library(... SHARED ...)`
3. 导出 `CreateStrategy` / `DestroyStrategy`

典型 CMake 片段：

```cmake
add_library(my_strategy SHARED strategies/my_strategy.cpp)
target_include_directories(my_strategy PRIVATE "${CMAKE_SOURCE_DIR}/include")
target_compile_definitions(my_strategy PRIVATE ITRADER_BUILD_STRATEGY NOMINMAX WIN32_LEAN_AND_MEAN)
set_target_properties(my_strategy PROPERTIES OUTPUT_NAME "my_strategy")
```

## 13. 用脚本自动生成策略模板

项目里现在已经带了一个生成器：

- `scripts/generate_strategy_template.py`

它可以自动生成一份可编译的策略 `.cpp` 模板，并支持顺手把 DLL target 加进 `CMakeLists.txt`。

### 最简单的用法

```powershell
python scripts/generate_strategy_template.py --name my_strategy
```

默认会生成：

- `strategies/my_strategy.cpp`

类名默认会自动推导成：

- `MyStrategyStrategy`

如果你想自定义类名：

```powershell
python scripts/generate_strategy_template.py --name my_strategy --class-name MyStrategy
```

### 自定义参数

可以重复传 `--param`：

```powershell
python scripts/generate_strategy_template.py `
  --name mean_reversion_alpha `
  --class-name MeanReversionAlphaStrategy `
  --param quantity:int=1 `
  --param lookback:int=30 `
  --param threshold:double=1.5 `
  --param enabled:bool=true `
  --param venue:string=SHFE
```

支持的参数类型：

- `int`
- `double`
- `bool`
- `string`

这些参数会自动体现在：

- 类成员
- `on_init(...)` 里的参数解析
- 初始化日志

### 直接把 DLL target 加进 CMake

```powershell
python scripts/generate_strategy_template.py --name my_strategy --add-cmake
```

这会在 `CMakeLists.txt` 里追加：

- `add_library(my_strategy SHARED strategies/my_strategy.cpp)`

如果 target 已存在，脚本不会重复追加。

### 覆盖已有文件

```powershell
python scripts/generate_strategy_template.py --name my_strategy --force
```

### 输出到别的目录

```powershell
python scripts/generate_strategy_template.py `
  --name demo_strategy `
  --output E:/iTrader/temp/demo_strategy.cpp
```

### 生成出来的模板长什么样

生成器默认产出的是“平台驱动型”模板，也就是已经带上：

- `on_tick()`
- `on_order_update()`
- `on_position_update()`
- `on_order_event()`
- `itrader::OrderSlotState`

也就是说，它默认就是按当前推荐架构生成，而不是旧式只靠 `on_tick()` 的模板。

## 14. 推荐参考文件

如果你准备新写策略，建议优先参考：

- `include/itrader/strategy_api.hpp`
- `include/itrader/domain.hpp`
- `include/itrader/runtime_snapshot.hpp`
- `include/itrader/order_slot_state.hpp`
- `strategies/sample_strategy.cpp`
- `strategies/ag_breakout_strict_strategy.cpp`

## 15. 一句话总结

以后在 `iTrader` 里写策略，推荐遵循这个模型：

- 用 `on_tick()` 写信号
- 用 `submit_intent()` 发意图
- 用 `on_order_update()` 接订单状态
- 用 `on_position_update()` 接持仓状态
- 用 `OrderSlotState` 管在途单 slot
- 不把本地 order/position 当执行真相
