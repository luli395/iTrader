#pragma once

#include "itrader/domain.hpp"
#include "itrader/runtime_snapshot.hpp"

#ifdef ITRADER_ENABLE_CTP

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "ThostFtdcTraderApi.h"

#ifdef ITRADER_ENABLE_CTP_MD
#include "ThostFtdcMdApi.h"
#endif

namespace itrader {

struct CtpAccountConfig {
    std::string front;
    std::string broker_id;
    std::string user_id;
    std::string investor_id;
    std::string password;
    std::string auth_code;
    std::string app_id;
    std::string md_front;
    std::string md_broker_id;
    std::string md_user_id;
    std::string md_password;
    std::string product_info {"iTrader"};
    std::string flow_dir {"runtime/ctp_flow"};
    std::string md_flow_dir {"runtime/ctp_md_flow"};
    bool production_mode {true};
    bool reconnect_enabled {true};
    int reconnect_retry_interval_ms {3000};
    int reconnect_max_attempts {0};
};

struct CtpInstrumentPositionSnapshot {
    std::string instrument;
    std::string exchange;
    int long_today_quantity {0};
    int long_yesterday_quantity {0};
    int short_today_quantity {0};
    int short_yesterday_quantity {0};
};

struct CtpPositionDetailSnapshot {
    std::string instrument;
    std::string exchange;
    std::string open_date;
    std::string trading_day;
    std::string trade_id;
    Side side {Side::Buy};
    int volume {0};
    double open_price {0.0};
};

class CtpTraderGateway final : private CThostFtdcTraderSpi {
public:
    explicit CtpTraderGateway(std::string account_id, CtpAccountConfig config);
    ~CtpTraderGateway();

    bool connect(std::string* error_message = nullptr, int timeout_ms = 10000);
    bool ensure_ready(std::string* error_message = nullptr, int timeout_ms = 10000);
    void disconnect();
    [[nodiscard]] bool ready() const;
    bool register_strategy_order_ref_code(int strategy_order_ref_code, std::string strategy_id, std::string* error_message = nullptr);
    std::optional<MarketTick> query_tick(const std::string& instrument, const std::string& exchange = {}, int timeout_ms = 5000);
    std::vector<CtpInstrumentPositionSnapshot> query_positions(std::string* error_message = nullptr, int timeout_ms = 10000);
    std::vector<CtpPositionDetailSnapshot> query_position_details(std::string* error_message = nullptr, int timeout_ms = 10000);
    std::vector<RuntimeOrderSnapshot> query_working_orders(std::string* error_message = nullptr, int timeout_ms = 10000);
    std::vector<RuntimeOrderSnapshot> query_trades(std::string* error_message = nullptr, int timeout_ms = 10000);
    bool submit_order(const OrderRequest& request, std::string* error_message = nullptr);
    bool cancel_order(const std::string& client_order_id, std::string* error_message = nullptr);
    void seed_recovered_order_mapping(const RuntimeOrderSnapshot& order);
    std::vector<OrderEvent> drain_order_events();
    void set_activity_callback(std::function<void()> callback);

    void OnFrontConnected() override;
    void OnFrontDisconnected(int nReason) override;
    void OnRspAuthenticate(CThostFtdcRspAuthenticateField* pRspAuthenticateField, CThostFtdcRspInfoField* pRspInfo, int nRequestID, bool bIsLast) override;
    void OnRspUserLogin(CThostFtdcRspUserLoginField* pRspUserLogin, CThostFtdcRspInfoField* pRspInfo, int nRequestID, bool bIsLast) override;
    void OnRspSettlementInfoConfirm(CThostFtdcSettlementInfoConfirmField* pSettlementInfoConfirm, CThostFtdcRspInfoField* pRspInfo, int nRequestID, bool bIsLast) override;
    void OnRspQryDepthMarketData(CThostFtdcDepthMarketDataField* pDepthMarketData, CThostFtdcRspInfoField* pRspInfo, int nRequestID, bool bIsLast) override;
    void OnRspQryInvestorPosition(CThostFtdcInvestorPositionField* pInvestorPosition, CThostFtdcRspInfoField* pRspInfo, int nRequestID, bool bIsLast) override;
    void OnRspQryInvestorPositionDetail(CThostFtdcInvestorPositionDetailField* pInvestorPositionDetail, CThostFtdcRspInfoField* pRspInfo, int nRequestID, bool bIsLast) override;
    void OnRspQryOrder(CThostFtdcOrderField* pOrder, CThostFtdcRspInfoField* pRspInfo, int nRequestID, bool bIsLast) override;
    void OnRspQryTrade(CThostFtdcTradeField* pTrade, CThostFtdcRspInfoField* pRspInfo, int nRequestID, bool bIsLast) override;
    void OnRspOrderInsert(CThostFtdcInputOrderField* pInputOrder, CThostFtdcRspInfoField* pRspInfo, int nRequestID, bool bIsLast) override;
    void OnRtnOrder(CThostFtdcOrderField* pOrder) override;
    void OnErrRtnOrderInsert(CThostFtdcInputOrderField* pInputOrder, CThostFtdcRspInfoField* pRspInfo) override;
    void OnRtnTrade(CThostFtdcTradeField* pTrade) override;
    void OnRspError(CThostFtdcRspInfoField* pRspInfo, int nRequestID, bool bIsLast) override;

private:
    struct MarketQueryState {
        bool done {false};
        std::optional<MarketTick> tick;
        std::string error;
    };

    struct PositionQueryState {
        bool done {false};
        std::string error;
        std::unordered_map<std::string, CtpInstrumentPositionSnapshot> positions_by_key;
    };

    struct PositionDetailQueryState {
        bool done {false};
        std::string error;
        std::vector<CtpPositionDetailSnapshot> details;
    };

    struct WorkingOrderQueryState {
        bool done {false};
        std::string error;
        std::vector<RuntimeOrderSnapshot> orders;
    };

    struct TradeQueryState {
        bool done {false};
        std::string error;
        std::vector<RuntimeOrderSnapshot> trades;
    };

    bool wait_until(const std::function<bool()>& predicate, int timeout_ms);
    int next_request_id();
    std::string next_order_ref(int strategy_order_ref_code);
    void push_event(OrderEvent event);
    void notify_error(const std::string& message);
    static void copy_trunc(char* destination, std::size_t destination_size, const std::string& value);
    static std::string now_string();
    static Side side_from_ctp(char value);
    static Offset offset_from_ctp(char value);
    static OrderStatus status_from_ctp(char value);

    std::string account_id_;
    CtpAccountConfig config_;
    std::string configured_user_id_raw_;
    CThostFtdcTraderApi* api_ {nullptr};

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    int request_id_ {0};
    long long order_ref_sequence_ {0};
    int front_id_ {0};
    int session_id_ {0};
    bool front_connected_ {false};
    bool auth_complete_ {false};
    bool login_complete_ {false};
    bool settlement_confirm_complete_ {false};
    bool ready_ {false};
    std::string connection_stage_ {"idle"};
    std::string last_error_;
    std::unordered_map<int, MarketQueryState> market_queries_;
    std::unordered_map<int, PositionQueryState> position_queries_;
    std::unordered_map<int, PositionDetailQueryState> position_detail_queries_;
    std::unordered_map<int, WorkingOrderQueryState> working_order_queries_;
    std::unordered_map<int, TradeQueryState> trade_queries_;
    std::vector<OrderEvent> pending_events_;
    std::unordered_map<std::string, std::string> order_to_client_id_;
    std::unordered_map<std::string, std::string> client_id_to_order_ref_;
    std::unordered_map<std::string, std::string> order_ref_to_exchange_;
    std::unordered_map<std::string, std::string> order_ref_to_instrument_;
    std::unordered_map<std::string, long long> order_ref_to_signal_time_ms_;
    std::unordered_map<std::string, int> order_ref_to_requested_volume_;
    std::unordered_map<std::string, int> order_ref_to_cumulative_trade_volume_;
    std::unordered_map<std::string, double> order_ref_to_limit_price_;
    std::unordered_map<int, std::string> strategy_code_to_id_;
    std::unordered_map<std::string, int> strategy_id_to_code_;
    std::function<void()> activity_callback_;
    int reconnect_attempts_ {0};
    long long last_connect_attempt_ms_ {0};
};

#ifdef ITRADER_ENABLE_CTP_MD
class CtpMarketDataGateway final : private CThostFtdcMdSpi {
public:
    explicit CtpMarketDataGateway(std::string account_id, CtpAccountConfig config);
    ~CtpMarketDataGateway();

    bool connect(std::string* error_message = nullptr, int timeout_ms = 10000);
    bool ensure_ready(std::string* error_message = nullptr, int timeout_ms = 10000);
    void disconnect();
    [[nodiscard]] bool ready() const;
    bool subscribe_market_data(const std::vector<std::string>& instruments, std::string* error_message = nullptr);
    std::vector<MarketTick> drain_ticks();
    void set_activity_callback(std::function<void()> callback);

    void OnFrontConnected() override;
    void OnFrontDisconnected(int nReason) override;
    void OnRspUserLogin(CThostFtdcRspUserLoginField* pRspUserLogin, CThostFtdcRspInfoField* pRspInfo, int nRequestID, bool bIsLast) override;
    void OnRspSubMarketData(CThostFtdcSpecificInstrumentField* pSpecificInstrument, CThostFtdcRspInfoField* pRspInfo, int nRequestID, bool bIsLast) override;
    void OnRspError(CThostFtdcRspInfoField* pRspInfo, int nRequestID, bool bIsLast) override;
    void OnRtnDepthMarketData(CThostFtdcDepthMarketDataField* pDepthMarketData) override;

private:
    bool wait_until(const std::function<bool()>& predicate, int timeout_ms);
    int next_request_id();
    void notify_error(const std::string& message);
    static void copy_trunc(char* destination, std::size_t destination_size, const std::string& value);

    std::string account_id_;
    CtpAccountConfig config_;
    CThostFtdcMdApi* api_ {nullptr};

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    int request_id_ {0};
    bool front_connected_ {false};
    bool login_complete_ {false};
    bool ready_ {false};
    std::string last_error_;
    std::unordered_map<std::string, MarketTick> latest_ticks_;
    std::vector<std::string> subscribed_instruments_;
    std::function<void()> activity_callback_;
    int reconnect_attempts_ {0};
    long long last_connect_attempt_ms_ {0};
};
#endif

} // namespace itrader

#endif
