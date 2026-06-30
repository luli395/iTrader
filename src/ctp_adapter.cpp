#ifdef ITRADER_ENABLE_CTP

#include "itrader/ctp_adapter.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace itrader {
namespace {

constexpr int kOrderRefStrategyCodeDigits = 2;
constexpr int kOrderRefVisibleDigits = 12;
constexpr int kOrderRefSequenceDigits = kOrderRefVisibleDigits - kOrderRefStrategyCodeDigits;
constexpr long long kOrderRefStrategyCodeScale = 100LL;
constexpr long long kOrderRefSequenceMax = 9'999'999'999LL;

bool is_ascii_digits(std::string_view value) {
    return !value.empty()
        && std::all_of(value.begin(), value.end(), [](unsigned char ch) {
            return std::isdigit(ch) != 0;
        });
}

std::optional<long long> parse_order_ref_numeric(std::string_view order_ref) {
    if (!is_ascii_digits(order_ref)) {
        return std::nullopt;
    }

    try {
        return std::stoll(std::string(order_ref));
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::optional<int> parse_order_ref_strategy_code(std::string_view order_ref) {
    if (order_ref.size() < static_cast<std::size_t>(kOrderRefStrategyCodeDigits)) {
        return std::nullopt;
    }

    const auto suffix = order_ref.substr(order_ref.size() - static_cast<std::size_t>(kOrderRefStrategyCodeDigits));
    if (!is_ascii_digits(suffix)) {
        return std::nullopt;
    }

    const int strategy_code = std::stoi(std::string(suffix));
    if (strategy_code < 1 || strategy_code > 99) {
        return std::nullopt;
    }
    return strategy_code;
}

long long seed_order_ref_sequence(std::string_view max_order_ref) {
    const auto numeric = parse_order_ref_numeric(max_order_ref);
    return numeric.has_value() ? (*numeric / kOrderRefStrategyCodeScale) : 0LL;
}

std::string format_order_ref(long long sequence, int strategy_code) {
    std::ostringstream output;
    output << std::setw(kOrderRefSequenceDigits) << std::setfill('0') << sequence
           << std::setw(kOrderRefStrategyCodeDigits) << std::setfill('0') << strategy_code;
    return output.str();
}

std::string rsp_error_message(CThostFtdcRspInfoField* rsp_info) {
    if (rsp_info == nullptr || rsp_info->ErrorID == 0) {
        return {};
    }
    return std::to_string(rsp_info->ErrorID) + ": " + rsp_info->ErrorMsg;
}

bool is_valid_market_price(double value) {
    return std::isfinite(value) && std::abs(value) < 1.0e20;
}

double sanitize_market_price(double value) {
    return is_valid_market_price(value) ? value : 0.0;
}

std::string current_local_date_compact() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t raw_time = std::chrono::system_clock::to_time_t(now);
    std::tm local_time {};
    localtime_s(&local_time, &raw_time);

    std::ostringstream output;
    output << std::put_time(&local_time, "%Y%m%d");
    return output.str();
}

int parse_update_time_hour(std::string_view update_time) {
    if (update_time.size() < 2
        || std::isdigit(static_cast<unsigned char>(update_time[0])) == 0
        || std::isdigit(static_cast<unsigned char>(update_time[1])) == 0) {
        return -1;
    }
    return static_cast<int>((update_time[0] - '0') * 10 + (update_time[1] - '0'));
}

std::string depth_market_timestamp(const CThostFtdcDepthMarketDataField* depth_market_data) {
    if (depth_market_data == nullptr) {
        return {};
    }

    auto action_day = trim_copy(depth_market_data->ActionDay);
    const auto update_time = trim_copy(depth_market_data->UpdateTime);
    if (update_time.empty()) {
        return {};
    }

    const auto local_date = current_local_date_compact();
    if (action_day.size() < 8 || !std::all_of(action_day.begin(), action_day.begin() + 8, [](unsigned char ch) {
            return std::isdigit(ch) != 0;
        })) {
        action_day = local_date;
    } else if (action_day.size() > 8) {
        action_day = action_day.substr(0, 8);
    }

    if (action_day > local_date && parse_update_time_hour(update_time) >= 20) {
        action_day = local_date;
    }

    return action_day + ' ' + update_time;
}

bool should_defer_fill_determination_to_trade_report(OrderStatus status, int filled_volume) {
    return filled_volume > 0
        && (status == OrderStatus::Filled || status == OrderStatus::PartiallyFilled);
}

char to_ctp_direction(Side side) {
    return side == Side::Buy ? THOST_FTDC_D_Buy : THOST_FTDC_D_Sell;
}

char to_ctp_offset(Offset offset) {
    switch (offset) {
    case Offset::Open:
        return THOST_FTDC_OF_Open;
    case Offset::Close:
        return THOST_FTDC_OF_Close;
    case Offset::CloseYesterday:
        return THOST_FTDC_OF_CloseYesterday;
    case Offset::CloseToday:
        return THOST_FTDC_OF_CloseToday;
    }
    return THOST_FTDC_OF_Open;
}

char to_ctp_order_price_type(PriceType price_type) {
    return price_type == PriceType::Market ? THOST_FTDC_OPT_AnyPrice : THOST_FTDC_OPT_LimitPrice;
}

int today_quantity_from_ctp(const CThostFtdcInvestorPositionField* position) {
    if (position == nullptr) {
        return 0;
    }
    if (position->PositionDate == THOST_FTDC_PSD_Today) {
        return position->Position > 0 ? position->Position : std::max(position->TodayPosition, 0);
    }
    if (position->PositionDate == THOST_FTDC_PSD_History) {
        return 0;
    }
    return std::max(position->TodayPosition, 0);
}

int yesterday_quantity_from_ctp(const CThostFtdcInvestorPositionField* position) {
    if (position == nullptr) {
        return 0;
    }
    if (position->PositionDate == THOST_FTDC_PSD_History) {
        return std::max(position->Position, 0);
    }

    const int total_quantity = std::max(position->Position, 0);
    const int today_quantity = std::max(position->TodayPosition, 0);
    return total_quantity > today_quantity ? (total_quantity - today_quantity) : 0;
}

bool ctp_position_snapshot_is_flat(const CtpInstrumentPositionSnapshot& snapshot) {
    return snapshot.long_today_quantity == 0
        && snapshot.long_yesterday_quantity == 0
        && snapshot.short_today_quantity == 0
        && snapshot.short_yesterday_quantity == 0;
}

long long steady_now_millis() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

} // namespace

CtpTraderGateway::CtpTraderGateway(std::string account_id, CtpAccountConfig config)
    : account_id_(std::move(account_id))
    , config_(std::move(config)) {
    configured_user_id_raw_ = config_.user_id;
    if (config_.investor_id.empty()) {
        config_.investor_id = config_.user_id;
    }
    if (config_.md_front.empty()) {
        config_.md_front = config_.front;
    }
    if (config_.md_broker_id.empty()) {
        config_.md_broker_id = config_.broker_id;
    }
    if (config_.md_user_id.empty()) {
        config_.md_user_id = config_.user_id;
    }
    if (config_.md_password.empty()) {
        config_.md_password = config_.password;
    }
}

CtpTraderGateway::~CtpTraderGateway() {
    disconnect();
}

bool CtpTraderGateway::connect(std::string* error_message, int timeout_ms) {
    disconnect();

    std::error_code error_code;
    std::filesystem::create_directories(config_.flow_dir, error_code);
    api_ = CThostFtdcTraderApi::CreateFtdcTraderApi(config_.flow_dir.c_str(), config_.production_mode);
    if (api_ == nullptr) {
        if (error_message != nullptr) {
            *error_message = "CreateFtdcTraderApi returned null";
        }
        return false;
    }

    {
        std::lock_guard lock(mutex_);
        front_connected_ = false;
        auth_complete_ = config_.auth_code.empty() || config_.app_id.empty();
        login_complete_ = false;
        settlement_confirm_complete_ = false;
        ready_ = false;
        connection_stage_ = "connecting_front";
        last_error_.clear();
        market_queries_.clear();
        position_queries_.clear();
        position_detail_queries_.clear();
        working_order_queries_.clear();
        trade_queries_.clear();
        pending_events_.clear();
    }

    api_->RegisterSpi(this);
    api_->SubscribePublicTopic(THOST_TERT_QUICK);
    api_->SubscribePrivateTopic(THOST_TERT_QUICK);

    std::vector<char> front(config_.front.begin(), config_.front.end());
    front.push_back('\0');
    api_->RegisterFront(front.data());
    api_->Init();

    if (!wait_until([this] { return front_connected_; }, timeout_ms)) {
        if (error_message != nullptr) {
            *error_message = last_error_.empty() ? "Timed out waiting for CTP front connection" : last_error_;
        }
        return false;
    }

    if (!auth_complete_) {
        {
            std::lock_guard lock(mutex_);
            connection_stage_ = "authenticating";
        }
        CThostFtdcReqAuthenticateField request {};
        copy_trunc(request.BrokerID, sizeof(request.BrokerID), config_.broker_id);
        copy_trunc(request.UserID, sizeof(request.UserID), config_.user_id);
        copy_trunc(request.UserProductInfo, sizeof(request.UserProductInfo), config_.product_info);
        copy_trunc(request.AuthCode, sizeof(request.AuthCode), config_.auth_code);
        copy_trunc(request.AppID, sizeof(request.AppID), config_.app_id);
        std::cout << "[ctp] ReqAuthenticate fields:\n"
                  << "  BrokerID=" << request.BrokerID << "\n"
                  << "  UserID=[" << request.UserID << "]\n"
                  << "  UserProductInfo=[" << request.UserProductInfo << "]\n"
                  << "  AuthCode=[" << (request.AuthCode[0] == '\0' ? "<empty>" : "<redacted>") << "]\n"
                  << "  AppID=[" << request.AppID << "]\n";
        std::cout.flush();
        api_->ReqAuthenticate(&request, next_request_id());

        if (!wait_until([this] { return auth_complete_; }, timeout_ms)) {
            if (error_message != nullptr) {
                *error_message = last_error_.empty() ? "Timed out waiting for CTP authentication" : last_error_;
            }
            return false;
        }
    }

    {
        std::lock_guard lock(mutex_);
        connection_stage_ = "logging_in";
    }
    CThostFtdcReqUserLoginField login_request {};
    copy_trunc(login_request.BrokerID, sizeof(login_request.BrokerID), config_.broker_id);
    copy_trunc(login_request.UserID, sizeof(login_request.UserID), config_.user_id);
    copy_trunc(login_request.Password, sizeof(login_request.Password), config_.password);
    copy_trunc(login_request.UserProductInfo, sizeof(login_request.UserProductInfo), config_.product_info);
    copy_trunc(login_request.InterfaceProductInfo, sizeof(login_request.InterfaceProductInfo), "iTrader");
    copy_trunc(login_request.ClientIPAddress, sizeof(login_request.ClientIPAddress), "127.0.0.1");
    std::cout << "[ctp] ReqUserLogin fields:\n"
              << "  BrokerID=" << login_request.BrokerID << "\n"
              << "  UserID=[" << login_request.UserID << "]\n"
              << "  Password=[" << (login_request.Password[0] == '\0' ? "<empty>" : "<redacted>") << "]\n"
              << "  UserProductInfo=[" << login_request.UserProductInfo << "]\n"
              << "  InterfaceProductInfo=[" << login_request.InterfaceProductInfo << "]\n"
              << "  MacAddress=[" << login_request.MacAddress << "]\n"
              << "  ProtocolInfo=[" << login_request.ProtocolInfo << "]\n"
              << "  LoginRemark=[" << login_request.LoginRemark << "]\n"
              << "  ClientIPAddress=[" << login_request.ClientIPAddress << "]\n"
              << "  ClientIPPort=" << login_request.ClientIPPort << "\n";
    std::cout.flush();
    api_->ReqUserLogin(&login_request, next_request_id());

    if (!wait_until([this] { return login_complete_; }, timeout_ms)) {
        if (error_message != nullptr) {
            *error_message = last_error_.empty() ? "Timed out waiting for CTP login" : last_error_;
        }
        return false;
    }

    {
        std::lock_guard lock(mutex_);
        if (!last_error_.empty()) {
            if (error_message != nullptr) {
                *error_message = last_error_;
            }
            return false;
        }
    }

    {
        std::lock_guard lock(mutex_);
        connection_stage_ = "settlement_confirming";
    }
    CThostFtdcSettlementInfoConfirmField settlement_confirm {};
    copy_trunc(settlement_confirm.BrokerID, sizeof(settlement_confirm.BrokerID), config_.broker_id);
    copy_trunc(settlement_confirm.InvestorID, sizeof(settlement_confirm.InvestorID), config_.investor_id);
    if (api_->ReqSettlementInfoConfirm(&settlement_confirm, next_request_id()) != 0) {
        if (error_message != nullptr) {
            *error_message = "ReqSettlementInfoConfirm returned non-zero";
        }
        return false;
    }

    const bool success = wait_until([this] { return settlement_confirm_complete_; }, timeout_ms) && ready();
    if (!success && error_message != nullptr) {
        *error_message = last_error_.empty() ? "Timed out waiting for CTP settlement confirmation" : last_error_;
    }

    if (success) {
        std::lock_guard lock(mutex_);
        reconnect_attempts_ = 0;
        connection_stage_ = "ready";
        last_error_.clear();
    }

    return success;
}

bool CtpTraderGateway::ensure_ready(std::string* error_message, int timeout_ms) {
    if (ready()) {
        if (error_message != nullptr) {
            error_message->clear();
        }
        return true;
    }

    if (!config_.reconnect_enabled) {
        if (error_message != nullptr) {
            *error_message = "CTP trader reconnect is disabled";
        }
        return false;
    }

    {
        std::lock_guard lock(mutex_);
        const auto now_ms = steady_now_millis();
        if (config_.reconnect_max_attempts > 0 && reconnect_attempts_ >= config_.reconnect_max_attempts) {
            if (error_message != nullptr) {
                *error_message = "CTP trader reconnect attempt limit reached";
            }
            return false;
        }
        if (last_connect_attempt_ms_ > 0
            && config_.reconnect_retry_interval_ms > 0
            && (now_ms - last_connect_attempt_ms_) < config_.reconnect_retry_interval_ms) {
            if (error_message != nullptr) {
                *error_message = "CTP trader reconnect backoff is active";
            }
            return false;
        }
        last_connect_attempt_ms_ = now_ms;
        ++reconnect_attempts_;
    }

    std::string reconnect_error;
    const bool success = connect(&reconnect_error, timeout_ms);
    if (!success && error_message != nullptr) {
        *error_message = reconnect_error.empty() ? "CTP trader reconnect failed" : reconnect_error;
    } else if (success && error_message != nullptr) {
        error_message->clear();
    }
    return success;
}

void CtpTraderGateway::disconnect() {
    if (api_ != nullptr) {
        api_->RegisterSpi(nullptr);
        api_->Release();
        api_ = nullptr;
    }

    std::lock_guard lock(mutex_);
    ready_ = false;
    login_complete_ = false;
    settlement_confirm_complete_ = false;
    front_connected_ = false;
    connection_stage_ = "disconnected";
}

bool CtpTraderGateway::ready() const {
    std::lock_guard lock(mutex_);
    return ready_;
}

bool CtpTraderGateway::register_strategy_order_ref_code(int strategy_order_ref_code, std::string strategy_id, std::string* error_message) {
    if (strategy_order_ref_code < 1 || strategy_order_ref_code > 99) {
        if (error_message != nullptr) {
            *error_message = "strategy_order_ref_code must be within [1, 99]";
        }
        return false;
    }
    if (strategy_id.empty()) {
        if (error_message != nullptr) {
            *error_message = "strategy_id is empty";
        }
        return false;
    }

    std::lock_guard lock(mutex_);
    if (const auto code_it = strategy_code_to_id_.find(strategy_order_ref_code); code_it != strategy_code_to_id_.end() && code_it->second != strategy_id) {
        if (error_message != nullptr) {
            *error_message = "strategy_order_ref_code " + std::to_string(strategy_order_ref_code)
                + " is already assigned to strategy " + code_it->second;
        }
        return false;
    }
    if (const auto strategy_it = strategy_id_to_code_.find(strategy_id); strategy_it != strategy_id_to_code_.end() && strategy_it->second != strategy_order_ref_code) {
        if (error_message != nullptr) {
            *error_message = "strategy " + strategy_id + " is already registered with strategy_order_ref_code "
                + std::to_string(strategy_it->second);
        }
        return false;
    }

    strategy_code_to_id_[strategy_order_ref_code] = std::move(strategy_id);
    strategy_id_to_code_[strategy_code_to_id_[strategy_order_ref_code]] = strategy_order_ref_code;
    return true;
}

std::optional<MarketTick> CtpTraderGateway::query_tick(const std::string& instrument, const std::string& exchange, int timeout_ms) {
    if (!ready()) {
        return std::nullopt;
    }

    CThostFtdcQryDepthMarketDataField request {};
    copy_trunc(request.InstrumentID, sizeof(request.InstrumentID), instrument);
    copy_trunc(request.ExchangeID, sizeof(request.ExchangeID), exchange);

    const int request_id = next_request_id();
    {
        std::lock_guard lock(mutex_);
        market_queries_.emplace(request_id, MarketQueryState {});
    }

    if (api_->ReqQryDepthMarketData(&request, request_id) != 0) {
        std::lock_guard lock(mutex_);
        market_queries_.erase(request_id);
        return std::nullopt;
    }

    const bool finished = wait_until([this, request_id] {
        const auto it = market_queries_.find(request_id);
        return it != market_queries_.end() && it->second.done;
    }, timeout_ms);

    std::lock_guard lock(mutex_);
    const auto it = market_queries_.find(request_id);
    if (!finished || it == market_queries_.end() || !it->second.tick.has_value()) {
        market_queries_.erase(request_id);
        return std::nullopt;
    }

    auto tick = it->second.tick;
    market_queries_.erase(request_id);
    return tick;
}

std::vector<CtpInstrumentPositionSnapshot> CtpTraderGateway::query_positions(std::string* error_message, int timeout_ms) {
    if (!ready()) {
        if (error_message != nullptr) {
            *error_message = "CTP gateway is not ready";
        }
        return {};
    }

    CThostFtdcQryInvestorPositionField request {};
    copy_trunc(request.BrokerID, sizeof(request.BrokerID), config_.broker_id);
    copy_trunc(request.InvestorID, sizeof(request.InvestorID), config_.investor_id);

    const int request_id = next_request_id();
    {
        std::lock_guard lock(mutex_);
        position_queries_.emplace(request_id, PositionQueryState {});
    }

    if (api_->ReqQryInvestorPosition(&request, request_id) != 0) {
        std::lock_guard lock(mutex_);
        position_queries_.erase(request_id);
        if (error_message != nullptr) {
            *error_message = "ReqQryInvestorPosition returned non-zero";
        }
        return {};
    }

    const bool finished = wait_until([this, request_id] {
        const auto it = position_queries_.find(request_id);
        return it != position_queries_.end() && it->second.done;
    }, timeout_ms);

    std::lock_guard lock(mutex_);
    const auto it = position_queries_.find(request_id);
    if (!finished || it == position_queries_.end()) {
        position_queries_.erase(request_id);
        if (error_message != nullptr) {
            *error_message = "Timed out waiting for QryInvestorPosition";
        }
        return {};
    }

    if (!it->second.error.empty()) {
        if (error_message != nullptr) {
            *error_message = it->second.error;
        }
        position_queries_.erase(request_id);
        return {};
    }

    std::vector<CtpInstrumentPositionSnapshot> positions;
    positions.reserve(it->second.positions_by_key.size());
    for (const auto& [_, snapshot] : it->second.positions_by_key) {
        if (ctp_position_snapshot_is_flat(snapshot)) {
            continue;
        }
        positions.push_back(snapshot);
    }
    position_queries_.erase(request_id);
    return positions;
}

std::vector<CtpPositionDetailSnapshot> CtpTraderGateway::query_position_details(std::string* error_message, int timeout_ms) {
    if (!ready()) {
        if (error_message != nullptr) {
            *error_message = "CTP gateway is not ready";
        }
        return {};
    }

    CThostFtdcQryInvestorPositionDetailField request {};
    copy_trunc(request.BrokerID, sizeof(request.BrokerID), config_.broker_id);
    copy_trunc(request.InvestorID, sizeof(request.InvestorID), config_.investor_id);

    const int request_id = next_request_id();
    {
        std::lock_guard lock(mutex_);
        position_detail_queries_.emplace(request_id, PositionDetailQueryState {});
    }

    if (api_->ReqQryInvestorPositionDetail(&request, request_id) != 0) {
        std::lock_guard lock(mutex_);
        position_detail_queries_.erase(request_id);
        if (error_message != nullptr) {
            *error_message = "ReqQryInvestorPositionDetail returned non-zero";
        }
        return {};
    }

    const bool finished = wait_until([this, request_id] {
        const auto it = position_detail_queries_.find(request_id);
        return it != position_detail_queries_.end() && it->second.done;
    }, timeout_ms);

    std::lock_guard lock(mutex_);
    const auto it = position_detail_queries_.find(request_id);
    if (!finished || it == position_detail_queries_.end()) {
        position_detail_queries_.erase(request_id);
        if (error_message != nullptr) {
            *error_message = "Timed out waiting for QryInvestorPositionDetail";
        }
        return {};
    }

    if (!it->second.error.empty()) {
        if (error_message != nullptr) {
            *error_message = it->second.error;
        }
        position_detail_queries_.erase(request_id);
        return {};
    }

    auto details = it->second.details;
    position_detail_queries_.erase(request_id);
    return details;
}

std::vector<RuntimeOrderSnapshot> CtpTraderGateway::query_working_orders(std::string* error_message, int timeout_ms) {
    if (!ready()) {
        if (error_message != nullptr) {
            *error_message = "CTP gateway is not ready";
        }
        return {};
    }

    CThostFtdcQryOrderField request {};
    copy_trunc(request.BrokerID, sizeof(request.BrokerID), config_.broker_id);
    copy_trunc(request.InvestorID, sizeof(request.InvestorID), config_.investor_id);

    const int request_id = next_request_id();
    {
        std::lock_guard lock(mutex_);
        working_order_queries_.emplace(request_id, WorkingOrderQueryState {});
    }

    if (api_->ReqQryOrder(&request, request_id) != 0) {
        std::lock_guard lock(mutex_);
        working_order_queries_.erase(request_id);
        if (error_message != nullptr) {
            *error_message = "ReqQryOrder returned non-zero";
        }
        return {};
    }

    const bool finished = wait_until([this, request_id] {
        const auto it = working_order_queries_.find(request_id);
        return it != working_order_queries_.end() && it->second.done;
    }, timeout_ms);

    std::lock_guard lock(mutex_);
    const auto it = working_order_queries_.find(request_id);
    if (!finished || it == working_order_queries_.end()) {
        working_order_queries_.erase(request_id);
        if (error_message != nullptr) {
            *error_message = "Timed out waiting for QryOrder";
        }
        return {};
    }

    if (!it->second.error.empty()) {
        if (error_message != nullptr) {
            *error_message = it->second.error;
        }
        working_order_queries_.erase(request_id);
        return {};
    }

    auto orders = it->second.orders;
    working_order_queries_.erase(request_id);
    return orders;
}

std::vector<RuntimeOrderSnapshot> CtpTraderGateway::query_trades(std::string* error_message, int timeout_ms) {
    if (!ready()) {
        if (error_message != nullptr) {
            *error_message = "CTP gateway is not ready";
        }
        return {};
    }

    CThostFtdcQryTradeField request {};
    copy_trunc(request.BrokerID, sizeof(request.BrokerID), config_.broker_id);
    copy_trunc(request.InvestorID, sizeof(request.InvestorID), config_.investor_id);

    const int request_id = next_request_id();
    {
        std::lock_guard lock(mutex_);
        trade_queries_.emplace(request_id, TradeQueryState {});
    }

    if (api_->ReqQryTrade(&request, request_id) != 0) {
        std::lock_guard lock(mutex_);
        trade_queries_.erase(request_id);
        if (error_message != nullptr) {
            *error_message = "ReqQryTrade returned non-zero";
        }
        return {};
    }

    const bool finished = wait_until([this, request_id] {
        const auto it = trade_queries_.find(request_id);
        return it != trade_queries_.end() && it->second.done;
    }, timeout_ms);

    std::lock_guard lock(mutex_);
    const auto it = trade_queries_.find(request_id);
    if (!finished || it == trade_queries_.end()) {
        trade_queries_.erase(request_id);
        if (error_message != nullptr) {
            *error_message = "Timed out waiting for QryTrade";
        }
        return {};
    }

    if (!it->second.error.empty()) {
        if (error_message != nullptr) {
            *error_message = it->second.error;
        }
        trade_queries_.erase(request_id);
        return {};
    }

    auto trades = it->second.trades;
    trade_queries_.erase(request_id);
    return trades;
}

bool CtpTraderGateway::submit_order(const OrderRequest& request, std::string* error_message) {
    if (!ready()) {
        if (error_message != nullptr) {
            *error_message = "CTP gateway is not ready";
        }
        return false;
    }
    if (request.strategy_order_ref_code < 1 || request.strategy_order_ref_code > 99) {
        if (error_message != nullptr) {
            *error_message = "strategy_order_ref_code must be within [1, 99]";
        }
        return false;
    }

    {
        std::lock_guard lock(mutex_);
        const auto strategy_it = strategy_code_to_id_.find(request.strategy_order_ref_code);
        if (strategy_it == strategy_code_to_id_.end()) {
            if (error_message != nullptr) {
                *error_message = "strategy_order_ref_code is not registered for this account";
            }
            return false;
        }
        if (!request.strategy_id.empty() && strategy_it->second != request.strategy_id) {
            if (error_message != nullptr) {
                *error_message = "strategy_order_ref_code does not match request.strategy_id";
            }
            return false;
        }
    }

    const bool submit_as_parked_order = is_ctp_parked_order_request(request);

    CThostFtdcInputOrderField order {};
    copy_trunc(order.BrokerID, sizeof(order.BrokerID), config_.broker_id);
    copy_trunc(order.InvestorID, sizeof(order.InvestorID), config_.investor_id);
    copy_trunc(order.UserID, sizeof(order.UserID), config_.user_id);
    copy_trunc(order.InstrumentID, sizeof(order.InstrumentID), request.instrument);
    copy_trunc(order.ExchangeID, sizeof(order.ExchangeID), request.exchange);
    const std::string order_ref = next_order_ref(request.strategy_order_ref_code);
    if (order_ref.empty()) {
        if (error_message != nullptr) {
            *error_message = "Unable to allocate a 12-digit CTP OrderRef";
        }
        return false;
    }
    copy_trunc(order.OrderRef, sizeof(order.OrderRef), order_ref);
    copy_trunc(order.IPAddress, sizeof(order.IPAddress), "127.0.0.1");

    order.OrderPriceType = to_ctp_order_price_type(request.price_type);
    order.Direction = to_ctp_direction(request.side);
    order.CombOffsetFlag[0] = to_ctp_offset(request.offset);
    order.CombHedgeFlag[0] = THOST_FTDC_HF_Speculation;
    order.LimitPrice = request.price_type == PriceType::Limit ? request.limit_price : 0.0;
    order.VolumeTotalOriginal = request.volume;
    order.TimeCondition = submit_as_parked_order
        ? THOST_FTDC_TC_GFD
        : ((request.price_type == PriceType::Market || request.immediate_or_cancel) ? THOST_FTDC_TC_IOC : THOST_FTDC_TC_GFD);
    order.VolumeCondition = THOST_FTDC_VC_AV;
    order.MinVolume = 1;
    order.ContingentCondition = THOST_FTDC_CC_Immediately;
    order.ForceCloseReason = THOST_FTDC_FCC_NotForceClose;
    order.IsAutoSuspend = 0;
    order.UserForceClose = 0;

    {
        std::lock_guard lock(mutex_);
        if (!request.client_order_id.empty()) {
            order_to_client_id_[order_ref] = request.client_order_id;
            client_id_to_order_ref_[request.client_order_id] = order_ref;
        }
        order_ref_to_exchange_[order_ref] = request.exchange;
        order_ref_to_instrument_[order_ref] = request.instrument;
        if (request.signal_time_ms > 0) {
            order_ref_to_signal_time_ms_[order_ref] = request.signal_time_ms;
        }
        order_ref_to_requested_volume_[order_ref] = request.volume;
        order_ref_to_cumulative_trade_volume_[order_ref] = 0;
        order_ref_to_limit_price_[order_ref] = request.price_type == PriceType::Limit ? request.limit_price : 0.0;
    }

    const int request_id = next_request_id();
    int return_code = 0;
    if (submit_as_parked_order) {
        CThostFtdcParkedOrderField parked_order {};
        copy_trunc(parked_order.BrokerID, sizeof(parked_order.BrokerID), config_.broker_id);
        copy_trunc(parked_order.InvestorID, sizeof(parked_order.InvestorID), config_.investor_id);
        copy_trunc(parked_order.UserID, sizeof(parked_order.UserID), config_.user_id);
        copy_trunc(parked_order.reserve1, sizeof(parked_order.reserve1), request.instrument);
        copy_trunc(parked_order.InstrumentID, sizeof(parked_order.InstrumentID), request.instrument);
        copy_trunc(parked_order.ExchangeID, sizeof(parked_order.ExchangeID), request.exchange);
        copy_trunc(parked_order.OrderRef, sizeof(parked_order.OrderRef), order_ref);
        copy_trunc(parked_order.IPAddress, sizeof(parked_order.IPAddress), "127.0.0.1");
        copy_trunc(parked_order.reserve2, sizeof(parked_order.reserve2), "127.0.0.1");

        parked_order.OrderPriceType = order.OrderPriceType;
        parked_order.Direction = order.Direction;
        parked_order.CombOffsetFlag[0] = order.CombOffsetFlag[0];
        parked_order.CombHedgeFlag[0] = order.CombHedgeFlag[0];
        parked_order.LimitPrice = order.LimitPrice;
        parked_order.VolumeTotalOriginal = order.VolumeTotalOriginal;
        parked_order.TimeCondition = THOST_FTDC_TC_GFD;
        parked_order.VolumeCondition = order.VolumeCondition;
        parked_order.MinVolume = order.MinVolume;
        parked_order.ContingentCondition = THOST_FTDC_CC_ParkedOrder;
        parked_order.ForceCloseReason = order.ForceCloseReason;
        parked_order.IsAutoSuspend = order.IsAutoSuspend;
        parked_order.RequestID = request_id;
        parked_order.UserForceClose = order.UserForceClose;
        parked_order.Status = THOST_FTDC_PAOS_NotSend;
        parked_order.IsSwapOrder = 0;

        return_code = api_->ReqParkedOrderInsert(&parked_order, request_id);
    } else {
        return_code = api_->ReqOrderInsert(&order, request_id);
    }
    if (return_code != 0) {
        {
            std::lock_guard lock(mutex_);
            if (!request.client_order_id.empty()) {
                client_id_to_order_ref_.erase(request.client_order_id);
            }
            order_to_client_id_.erase(order_ref);
            order_ref_to_exchange_.erase(order_ref);
            order_ref_to_instrument_.erase(order_ref);
            order_ref_to_signal_time_ms_.erase(order_ref);
            order_ref_to_requested_volume_.erase(order_ref);
            order_ref_to_cumulative_trade_volume_.erase(order_ref);
            order_ref_to_limit_price_.erase(order_ref);
        }
        if (error_message != nullptr) {
            *error_message = std::string(submit_as_parked_order ? "ReqParkedOrderInsert returned " : "ReqOrderInsert returned ")
                + std::to_string(return_code);
        }
        return false;
    }

    push_event(OrderEvent {
        .order_id = order_ref,
        .source_order_id = {},
        .client_order_id = request.client_order_id.empty() ? order_ref : request.client_order_id,
        .account_id = account_id_,
        .strategy_id = request.strategy_id,
        .instrument = request.instrument,
        .exchange = request.exchange,
        .side = request.side,
        .offset = request.offset,
        .requested_volume = request.volume,
        .filled_volume = 0,
        .limit_price = request.limit_price,
        .filled_price = 0.0,
        .signal_time_ms = request.signal_time_ms,
        .status = OrderStatus::Submitted,
        .message = submit_as_parked_order ? "submitted to CTP parked order queue" : "submitted to CTP",
        .timestamp = now_string(),
    });
    return true;
}

bool CtpTraderGateway::cancel_order(const std::string& client_order_id, std::string* error_message) {
    if (!ready()) {
        if (error_message != nullptr) {
            *error_message = "CTP gateway is not ready";
        }
        return false;
    }
    if (client_order_id.empty()) {
        if (error_message != nullptr) {
            *error_message = "client_order_id is empty";
        }
        return false;
    }

    std::string order_ref;
    std::string exchange;
    std::string instrument;
    {
        std::lock_guard lock(mutex_);
        const auto it = client_id_to_order_ref_.find(client_order_id);
        if (it == client_id_to_order_ref_.end()) {
            if (error_message != nullptr) {
                *error_message = "unknown client_order_id";
            }
            return false;
        }
        order_ref = it->second;
        exchange = order_ref_to_exchange_[order_ref];
        instrument = order_ref_to_instrument_[order_ref];
    }

    CThostFtdcInputOrderActionField action {};
    copy_trunc(action.BrokerID, sizeof(action.BrokerID), config_.broker_id);
    copy_trunc(action.InvestorID, sizeof(action.InvestorID), config_.investor_id);
    copy_trunc(action.UserID, sizeof(action.UserID), config_.user_id);
    copy_trunc(action.ExchangeID, sizeof(action.ExchangeID), exchange);
    copy_trunc(action.InstrumentID, sizeof(action.InstrumentID), instrument);
    copy_trunc(action.OrderRef, sizeof(action.OrderRef), order_ref);
    copy_trunc(action.IPAddress, sizeof(action.IPAddress), "127.0.0.1");
    action.FrontID = front_id_;
    action.SessionID = session_id_;
    action.OrderActionRef = next_request_id();
    action.ActionFlag = THOST_FTDC_AF_Delete;

    const int return_code = api_->ReqOrderAction(&action, next_request_id());
    if (return_code != 0) {
        if (error_message != nullptr) {
            *error_message = "ReqOrderAction returned " + std::to_string(return_code);
        }
        return false;
    }
    return true;
}

void CtpTraderGateway::seed_recovered_order_mapping(const RuntimeOrderSnapshot& order) {
    if (order.order_id.empty()) {
        return;
    }

    std::lock_guard lock(mutex_);
    if (!order.client_order_id.empty()) {
        order_to_client_id_[order.order_id] = order.client_order_id;
        client_id_to_order_ref_[order.client_order_id] = order.order_id;
    }
    if (!order.exchange.empty()) {
        order_ref_to_exchange_[order.order_id] = order.exchange;
    }
    if (!order.instrument.empty()) {
        order_ref_to_instrument_[order.order_id] = order.instrument;
    }
    if (order.signal_time_ms > 0) {
        order_ref_to_signal_time_ms_[order.order_id] = order.signal_time_ms;
    }
    if (order.requested_volume > 0) {
        order_ref_to_requested_volume_[order.order_id] = order.requested_volume;
    }
    order_ref_to_cumulative_trade_volume_[order.order_id] = std::max(0, order.filled_volume);
    order_ref_to_limit_price_[order.order_id] = order.limit_price;
}

std::vector<OrderEvent> CtpTraderGateway::drain_order_events() {
    std::lock_guard lock(mutex_);
    auto events = std::move(pending_events_);
    pending_events_.clear();
    return events;
}

void CtpTraderGateway::set_activity_callback(std::function<void()> callback) {
    std::lock_guard lock(mutex_);
    activity_callback_ = std::move(callback);
}

void CtpTraderGateway::OnFrontConnected() {
    {
        std::lock_guard lock(mutex_);
        front_connected_ = true;
        if (connection_stage_ == "connecting_front") {
            connection_stage_ = "front_connected";
        }
    }
    cv_.notify_all();
}

void CtpTraderGateway::OnFrontDisconnected(int nReason) {
    std::string stage;
    {
        std::lock_guard lock(mutex_);
        stage = connection_stage_;
        front_connected_ = false;
        auth_complete_ = false;
        login_complete_ = false;
        settlement_confirm_complete_ = false;
        ready_ = false;
        connection_stage_ = "disconnected";
    }
    notify_error("CTP front disconnected during " + (stage.empty() ? std::string("unknown") : stage)
        + ", reason=" + std::to_string(nReason));
}

void CtpTraderGateway::OnRspAuthenticate(CThostFtdcRspAuthenticateField*, CThostFtdcRspInfoField* rsp_info, int, bool bIsLast) {
    if (!bIsLast) {
        return;
    }

    {
        std::lock_guard lock(mutex_);
        const auto error = rsp_error_message(rsp_info);
        if (!error.empty()) {
            last_error_ = "CTP authenticate failed: " + error;
        } else {
            connection_stage_ = "authenticated";
        }
        auth_complete_ = true;
    }
    cv_.notify_all();
}

void CtpTraderGateway::OnRspUserLogin(CThostFtdcRspUserLoginField* user_login, CThostFtdcRspInfoField* rsp_info, int, bool bIsLast) {
    if (!bIsLast) {
        return;
    }

    {
        std::lock_guard lock(mutex_);
        const auto error = rsp_error_message(rsp_info);
        if (!error.empty()) {
            last_error_ = "CTP login failed: " + error;
            ready_ = false;
        } else {
            if (user_login != nullptr && user_login->MaxOrderRef[0] != '\0') {
                order_ref_sequence_ = seed_order_ref_sequence(user_login->MaxOrderRef);
            }
            if (user_login != nullptr) {
                front_id_ = user_login->FrontID;
                session_id_ = user_login->SessionID;
            }
            connection_stage_ = "logged_in";
        }
        login_complete_ = true;
    }
    cv_.notify_all();
}

void CtpTraderGateway::OnRspSettlementInfoConfirm(CThostFtdcSettlementInfoConfirmField*, CThostFtdcRspInfoField* rsp_info, int, bool bIsLast) {
    if (!bIsLast) {
        return;
    }

    {
        std::lock_guard lock(mutex_);
        const auto error = rsp_error_message(rsp_info);
        if (!error.empty()) {
            last_error_ = "CTP settlement confirm failed: " + error;
            ready_ = false;
        } else {
            ready_ = true;
            connection_stage_ = "ready";
        }
        settlement_confirm_complete_ = true;
    }
    cv_.notify_all();
}

void CtpTraderGateway::OnRspQryDepthMarketData(CThostFtdcDepthMarketDataField* depth_market_data, CThostFtdcRspInfoField* rsp_info, int request_id, bool bIsLast) {
    std::lock_guard lock(mutex_);
    auto& state = market_queries_[request_id];
    const auto error = rsp_error_message(rsp_info);
    if (!error.empty()) {
        state.error = error;
    }
    if (depth_market_data != nullptr) {
        MarketTick tick;
        tick.timestamp = depth_market_timestamp(depth_market_data);
        tick.trading_day = trim_copy(depth_market_data->TradingDay);
        tick.instrument = depth_market_data->InstrumentID;
        tick.exchange = depth_market_data->ExchangeID;
        tick.last = depth_market_data->LastPrice;
        tick.bid = depth_market_data->BidPrice1;
        tick.ask = depth_market_data->AskPrice1;
        tick.upper_limit_price = sanitize_market_price(depth_market_data->UpperLimitPrice);
        tick.lower_limit_price = sanitize_market_price(depth_market_data->LowerLimitPrice);
        tick.volume = depth_market_data->Volume;
        state.tick = tick;
    }
    if (bIsLast) {
        state.done = true;
        cv_.notify_all();
    }
}

void CtpTraderGateway::OnRspQryInvestorPosition(CThostFtdcInvestorPositionField* investor_position, CThostFtdcRspInfoField* rsp_info, int request_id, bool bIsLast) {
    std::lock_guard lock(mutex_);
    auto& state = position_queries_[request_id];
    const auto error = rsp_error_message(rsp_info);
    if (!error.empty()) {
        state.error = error;
    }

    if (investor_position != nullptr) {
        const std::string instrument = investor_position->InstrumentID;
        if (!instrument.empty()) {
            const std::string exchange = investor_position->ExchangeID;
            const std::string key = exchange + "::" + instrument;
            auto& snapshot = state.positions_by_key[key];
            snapshot.instrument = instrument;
            if (snapshot.exchange.empty()) {
                snapshot.exchange = exchange;
            }

            const int today_quantity = today_quantity_from_ctp(investor_position);
            const int yesterday_quantity = yesterday_quantity_from_ctp(investor_position);
            if (investor_position->PosiDirection == THOST_FTDC_PD_Short) {
                snapshot.short_today_quantity += today_quantity;
                snapshot.short_yesterday_quantity += yesterday_quantity;
            } else if (investor_position->PosiDirection == THOST_FTDC_PD_Long) {
                snapshot.long_today_quantity += today_quantity;
                snapshot.long_yesterday_quantity += yesterday_quantity;
            }
        }
    }

    if (bIsLast) {
        state.done = true;
        cv_.notify_all();
    }
}

void CtpTraderGateway::OnRspQryInvestorPositionDetail(CThostFtdcInvestorPositionDetailField* investor_position_detail, CThostFtdcRspInfoField* rsp_info, int request_id, bool bIsLast) {
    std::lock_guard lock(mutex_);
    auto& state = position_detail_queries_[request_id];
    const auto error = rsp_error_message(rsp_info);
    if (!error.empty()) {
        state.error = error;
    }

    if (investor_position_detail != nullptr) {
        const std::string instrument = investor_position_detail->InstrumentID;
        if (!instrument.empty() && investor_position_detail->Volume > 0) {
            CtpPositionDetailSnapshot detail;
            detail.instrument = instrument;
            detail.exchange = investor_position_detail->ExchangeID;
            detail.open_date = investor_position_detail->OpenDate;
            detail.trading_day = investor_position_detail->TradingDay;
            detail.trade_id = investor_position_detail->TradeID;
            detail.side = investor_position_detail->Direction == THOST_FTDC_D_Sell ? Side::Sell : Side::Buy;
            detail.volume = investor_position_detail->Volume;
            detail.open_price = investor_position_detail->OpenPrice;
            state.details.push_back(std::move(detail));
        }
    }

    if (bIsLast) {
        state.done = true;
        cv_.notify_all();
    }
}

void CtpTraderGateway::OnRspQryOrder(CThostFtdcOrderField* order, CThostFtdcRspInfoField* rsp_info, int request_id, bool bIsLast) {
    std::lock_guard lock(mutex_);
    auto& state = working_order_queries_[request_id];
    const auto error = rsp_error_message(rsp_info);
    if (!error.empty()) {
        state.error = error;
    }

    if (order != nullptr) {
        const auto status = status_from_ctp(order->OrderStatus);
        if (status != OrderStatus::Filled && status != OrderStatus::Cancelled && status != OrderStatus::Rejected) {
            RuntimeOrderSnapshot snapshot;
            snapshot.order_id = order->OrderRef;
            snapshot.source_order_id = order->OrderSysID;
            const auto client_it = order_to_client_id_.find(order->OrderRef);
            if (client_it != order_to_client_id_.end()) {
                snapshot.client_order_id = client_it->second;
            }
            snapshot.account_id = account_id_;
            if (const auto strategy_code = parse_order_ref_strategy_code(order->OrderRef); strategy_code.has_value()) {
                const auto strategy_it = strategy_code_to_id_.find(*strategy_code);
                if (strategy_it != strategy_code_to_id_.end()) {
                    snapshot.strategy_id = strategy_it->second;
                }
            }
            snapshot.instrument = order->InstrumentID;
            snapshot.exchange = order->ExchangeID;
            snapshot.side = side_from_ctp(order->Direction);
            snapshot.offset = offset_from_ctp(order->CombOffsetFlag[0]);
            snapshot.requested_volume = order->VolumeTotalOriginal;
            snapshot.filled_volume = order->VolumeTraded;
            snapshot.limit_price = order->LimitPrice;
            snapshot.filled_price = 0.0;
            snapshot.status = status;
            snapshot.message = order->StatusMsg;
            snapshot.timestamp = std::string(order->InsertDate) + ' ' + order->InsertTime;
            state.orders.push_back(std::move(snapshot));
        }
    }

    if (bIsLast) {
        state.done = true;
        cv_.notify_all();
    }
}

void CtpTraderGateway::OnRspQryTrade(CThostFtdcTradeField* trade, CThostFtdcRspInfoField* rsp_info, int request_id, bool bIsLast) {
    std::lock_guard lock(mutex_);
    auto& state = trade_queries_[request_id];
    const auto error = rsp_error_message(rsp_info);
    if (!error.empty()) {
        state.error = error;
    }

    if (trade != nullptr) {
        RuntimeOrderSnapshot snapshot;
        snapshot.order_id = trade->OrderRef;
        snapshot.source_order_id = trade->TradeID;
        const auto client_it = order_to_client_id_.find(trade->OrderRef);
        if (client_it != order_to_client_id_.end()) {
            snapshot.client_order_id = client_it->second;
        }
        snapshot.account_id = account_id_;
        if (const auto strategy_code = parse_order_ref_strategy_code(trade->OrderRef); strategy_code.has_value()) {
            const auto strategy_it = strategy_code_to_id_.find(*strategy_code);
            if (strategy_it != strategy_code_to_id_.end()) {
                snapshot.strategy_id = strategy_it->second;
            }
        }
        snapshot.instrument = trade->InstrumentID;
        snapshot.exchange = trade->ExchangeID;
        snapshot.side = side_from_ctp(trade->Direction);
        snapshot.offset = offset_from_ctp(trade->OffsetFlag);
        snapshot.requested_volume = trade->Volume;
        snapshot.filled_volume = trade->Volume;
        snapshot.limit_price = 0.0;
        snapshot.filled_price = trade->Price;
        snapshot.status = OrderStatus::Filled;
        snapshot.message = "recovered broker trade";
        snapshot.timestamp = std::string(trade->TradeDate) + ' ' + trade->TradeTime;
        state.trades.push_back(std::move(snapshot));
    }

    if (bIsLast) {
        state.done = true;
        cv_.notify_all();
    }
}

void CtpTraderGateway::OnRspOrderInsert(CThostFtdcInputOrderField* input_order, CThostFtdcRspInfoField* rsp_info, int, bool) {
    const auto error = rsp_error_message(rsp_info);
    if (input_order == nullptr || error.empty()) {
        return;
    }

    std::string strategy_id;
    std::string client_order_id;
    long long signal_time_ms = 0;
    double limit_price = input_order->LimitPrice;
    {
        std::lock_guard lock(mutex_);
        if (const auto strategy_code = parse_order_ref_strategy_code(input_order->OrderRef); strategy_code.has_value()) {
            const auto it = strategy_code_to_id_.find(*strategy_code);
            if (it != strategy_code_to_id_.end()) {
                strategy_id = it->second;
            }
        }
        const auto client_it = order_to_client_id_.find(input_order->OrderRef);
        if (client_it != order_to_client_id_.end()) {
            client_order_id = client_it->second;
        }
        const auto signal_it = order_ref_to_signal_time_ms_.find(input_order->OrderRef);
        if (signal_it != order_ref_to_signal_time_ms_.end()) {
            signal_time_ms = signal_it->second;
        }
        if (limit_price <= 0.0) {
            if (const auto limit_it = order_ref_to_limit_price_.find(input_order->OrderRef); limit_it != order_ref_to_limit_price_.end()) {
                limit_price = limit_it->second;
            }
        }
    }

    push_event(OrderEvent {
        .order_id = input_order->OrderRef,
        .source_order_id = {},
        .client_order_id = client_order_id.empty() ? input_order->OrderRef : client_order_id,
        .account_id = account_id_,
        .strategy_id = strategy_id,
        .instrument = input_order->InstrumentID,
        .exchange = input_order->ExchangeID,
        .side = side_from_ctp(input_order->Direction),
        .offset = offset_from_ctp(input_order->CombOffsetFlag[0]),
        .requested_volume = input_order->VolumeTotalOriginal,
        .filled_volume = 0,
        .limit_price = limit_price,
        .filled_price = 0.0,
        .signal_time_ms = signal_time_ms,
        .status = OrderStatus::Rejected,
        .message = error,
        .timestamp = now_string(),
    });

    {
        std::lock_guard lock(mutex_);
        if (!client_order_id.empty()) {
            client_id_to_order_ref_.erase(client_order_id);
        }
        order_to_client_id_.erase(input_order->OrderRef);
        order_ref_to_exchange_.erase(input_order->OrderRef);
        order_ref_to_instrument_.erase(input_order->OrderRef);
        order_ref_to_signal_time_ms_.erase(input_order->OrderRef);
        order_ref_to_requested_volume_.erase(input_order->OrderRef);
        order_ref_to_cumulative_trade_volume_.erase(input_order->OrderRef);
        order_ref_to_limit_price_.erase(input_order->OrderRef);
    }
}

void CtpTraderGateway::OnRspParkedOrderInsert(CThostFtdcParkedOrderField* parked_order, CThostFtdcRspInfoField* rsp_info, int, bool) {
    if (parked_order == nullptr) {
        return;
    }

    const auto error = rsp_error_message(rsp_info);

    std::string strategy_id;
    std::string client_order_id;
    long long signal_time_ms = 0;
    double limit_price = parked_order->LimitPrice;
    {
        std::lock_guard lock(mutex_);
        if (const auto strategy_code = parse_order_ref_strategy_code(parked_order->OrderRef); strategy_code.has_value()) {
            const auto it = strategy_code_to_id_.find(*strategy_code);
            if (it != strategy_code_to_id_.end()) {
                strategy_id = it->second;
            }
        }
        const auto client_it = order_to_client_id_.find(parked_order->OrderRef);
        if (client_it != order_to_client_id_.end()) {
            client_order_id = client_it->second;
        }
        const auto signal_it = order_ref_to_signal_time_ms_.find(parked_order->OrderRef);
        if (signal_it != order_ref_to_signal_time_ms_.end()) {
            signal_time_ms = signal_it->second;
        }
        if (limit_price <= 0.0) {
            if (const auto limit_it = order_ref_to_limit_price_.find(parked_order->OrderRef); limit_it != order_ref_to_limit_price_.end()) {
                limit_price = limit_it->second;
            }
        }
    }

    push_event(OrderEvent {
        .order_id = parked_order->OrderRef,
        .source_order_id = parked_order->ParkedOrderID,
        .client_order_id = client_order_id.empty() ? parked_order->OrderRef : client_order_id,
        .account_id = account_id_,
        .strategy_id = strategy_id,
        .instrument = parked_order->InstrumentID,
        .exchange = parked_order->ExchangeID,
        .side = side_from_ctp(parked_order->Direction),
        .offset = offset_from_ctp(parked_order->CombOffsetFlag[0]),
        .requested_volume = parked_order->VolumeTotalOriginal,
        .filled_volume = 0,
        .limit_price = limit_price,
        .filled_price = 0.0,
        .signal_time_ms = signal_time_ms,
        .status = error.empty() ? OrderStatus::Accepted : OrderStatus::Rejected,
        .message = error.empty() ? "accepted by CTP parked order queue" : error,
        .timestamp = now_string(),
    });

    if (!error.empty()) {
        std::lock_guard lock(mutex_);
        if (!client_order_id.empty()) {
            client_id_to_order_ref_.erase(client_order_id);
        }
        order_to_client_id_.erase(parked_order->OrderRef);
        order_ref_to_exchange_.erase(parked_order->OrderRef);
        order_ref_to_instrument_.erase(parked_order->OrderRef);
        order_ref_to_signal_time_ms_.erase(parked_order->OrderRef);
        order_ref_to_requested_volume_.erase(parked_order->OrderRef);
        order_ref_to_cumulative_trade_volume_.erase(parked_order->OrderRef);
        order_ref_to_limit_price_.erase(parked_order->OrderRef);
    }
}

void CtpTraderGateway::OnRtnOrder(CThostFtdcOrderField* order) {
    if (order == nullptr) {
        return;
    }

    const auto status = status_from_ctp(order->OrderStatus);
    const bool defer_fill_determination = should_defer_fill_determination_to_trade_report(status, order->VolumeTraded);

    std::string strategy_id;
    std::string client_order_id;
    long long signal_time_ms = 0;
    double limit_price = order->LimitPrice;
    {
        std::lock_guard lock(mutex_);
        if (const auto strategy_code = parse_order_ref_strategy_code(order->OrderRef); strategy_code.has_value()) {
            const auto it = strategy_code_to_id_.find(*strategy_code);
            if (it != strategy_code_to_id_.end()) {
                strategy_id = it->second;
            }
        }
        const auto client_it = order_to_client_id_.find(order->OrderRef);
        if (client_it != order_to_client_id_.end()) {
            client_order_id = client_it->second;
        }
        const auto signal_it = order_ref_to_signal_time_ms_.find(order->OrderRef);
        if (signal_it != order_ref_to_signal_time_ms_.end()) {
            signal_time_ms = signal_it->second;
        }
        if (order->VolumeTotalOriginal > 0) {
            order_ref_to_requested_volume_[order->OrderRef] = order->VolumeTotalOriginal;
        }
        if (limit_price <= 0.0) {
            if (const auto limit_it = order_ref_to_limit_price_.find(order->OrderRef); limit_it != order_ref_to_limit_price_.end()) {
                limit_price = limit_it->second;
            }
        }
        if (order->LimitPrice > 0.0) {
            order_ref_to_limit_price_[order->OrderRef] = order->LimitPrice;
        }
    }

    if (!defer_fill_determination) {
        push_event(OrderEvent {
            .order_id = order->OrderRef,
            .source_order_id = order->OrderSysID,
            .client_order_id = client_order_id.empty() ? order->OrderRef : client_order_id,
            .account_id = account_id_,
            .strategy_id = strategy_id,
            .instrument = order->InstrumentID,
            .exchange = order->ExchangeID,
            .side = side_from_ctp(order->Direction),
            .offset = offset_from_ctp(order->CombOffsetFlag[0]),
            .requested_volume = order->VolumeTotalOriginal,
            .filled_volume = 0,
            .limit_price = limit_price,
            .filled_price = 0.0,
            .signal_time_ms = signal_time_ms,
            .status = status,
            .message = order->StatusMsg,
            .timestamp = std::string(order->InsertDate) + ' ' + order->InsertTime,
        });
    }

    if (status == OrderStatus::Cancelled || status == OrderStatus::Rejected) {
        std::lock_guard lock(mutex_);
        if (!client_order_id.empty()) {
            client_id_to_order_ref_.erase(client_order_id);
        }
        order_to_client_id_.erase(order->OrderRef);
        order_ref_to_exchange_.erase(order->OrderRef);
        order_ref_to_instrument_.erase(order->OrderRef);
        order_ref_to_signal_time_ms_.erase(order->OrderRef);
        order_ref_to_requested_volume_.erase(order->OrderRef);
        order_ref_to_cumulative_trade_volume_.erase(order->OrderRef);
        order_ref_to_limit_price_.erase(order->OrderRef);
    }
}

void CtpTraderGateway::OnErrRtnOrderInsert(CThostFtdcInputOrderField* input_order, CThostFtdcRspInfoField* rsp_info) {
    OnRspOrderInsert(input_order, rsp_info, 0, true);
}

void CtpTraderGateway::OnRtnTrade(CThostFtdcTradeField* trade) {
    if (trade == nullptr) {
        return;
    }

    std::string strategy_id;
    std::string client_order_id;
    long long signal_time_ms = 0;
    int requested_volume = std::max(trade->Volume, 0);
    int cumulative_filled_volume = std::max(trade->Volume, 0);
    double limit_price = 0.0;
    {
        std::lock_guard lock(mutex_);
        if (const auto strategy_code = parse_order_ref_strategy_code(trade->OrderRef); strategy_code.has_value()) {
            const auto it = strategy_code_to_id_.find(*strategy_code);
            if (it != strategy_code_to_id_.end()) {
                strategy_id = it->second;
            }
        }
        const auto client_it = order_to_client_id_.find(trade->OrderRef);
        if (client_it != order_to_client_id_.end()) {
            client_order_id = client_it->second;
        }
        const auto signal_it = order_ref_to_signal_time_ms_.find(trade->OrderRef);
        if (signal_it != order_ref_to_signal_time_ms_.end()) {
            signal_time_ms = signal_it->second;
        }

        if (const auto requested_it = order_ref_to_requested_volume_.find(trade->OrderRef); requested_it != order_ref_to_requested_volume_.end()) {
            requested_volume = std::max(requested_it->second, requested_volume);
        }
        auto& cumulative_trade_volume = order_ref_to_cumulative_trade_volume_[trade->OrderRef];
        cumulative_trade_volume += std::max(trade->Volume, 0);
        cumulative_filled_volume = cumulative_trade_volume;
        if (const auto limit_it = order_ref_to_limit_price_.find(trade->OrderRef); limit_it != order_ref_to_limit_price_.end()) {
            limit_price = limit_it->second;
        }
    }

    const bool order_filled = requested_volume > 0 && cumulative_filled_volume >= requested_volume;

    push_event(OrderEvent {
        .order_id = trade->OrderRef,
        .source_order_id = trade->TradeID,
        .client_order_id = client_order_id.empty() ? trade->OrderRef : client_order_id,
        .account_id = account_id_,
        .strategy_id = strategy_id,
        .instrument = trade->InstrumentID,
        .exchange = trade->ExchangeID,
        .side = side_from_ctp(trade->Direction),
        .offset = offset_from_ctp(trade->OffsetFlag),
        .requested_volume = requested_volume,
        .filled_volume = cumulative_filled_volume,
        .limit_price = limit_price,
        .filled_price = trade->Price,
        .signal_time_ms = signal_time_ms,
        .status = order_filled ? OrderStatus::Filled : OrderStatus::PartiallyFilled,
        .message = "trade filled",
        .timestamp = std::string(trade->TradeDate) + ' ' + trade->TradeTime,
    });

    if (order_filled) {
        std::lock_guard lock(mutex_);
        if (!client_order_id.empty()) {
            client_id_to_order_ref_.erase(client_order_id);
        }
        order_to_client_id_.erase(trade->OrderRef);
        order_ref_to_exchange_.erase(trade->OrderRef);
        order_ref_to_instrument_.erase(trade->OrderRef);
        order_ref_to_signal_time_ms_.erase(trade->OrderRef);
        order_ref_to_requested_volume_.erase(trade->OrderRef);
        order_ref_to_cumulative_trade_volume_.erase(trade->OrderRef);
        order_ref_to_limit_price_.erase(trade->OrderRef);
    }
}

void CtpTraderGateway::OnRspError(CThostFtdcRspInfoField* rsp_info, int, bool) {
    const auto error = rsp_error_message(rsp_info);
    if (!error.empty()) {
        notify_error(error);
    }
}

bool CtpTraderGateway::wait_until(const std::function<bool()>& predicate, int timeout_ms) {
    std::unique_lock lock(mutex_);
    return cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms), predicate);
}

int CtpTraderGateway::next_request_id() {
    std::lock_guard lock(mutex_);
    return ++request_id_;
}

std::string CtpTraderGateway::next_order_ref(int strategy_order_ref_code) {
    if (strategy_order_ref_code < 1 || strategy_order_ref_code > 99) {
        return {};
    }

    std::lock_guard lock(mutex_);
    if (order_ref_sequence_ >= kOrderRefSequenceMax) {
        return {};
    }

    ++order_ref_sequence_;
    return format_order_ref(order_ref_sequence_, strategy_order_ref_code);
}

void CtpTraderGateway::push_event(OrderEvent event) {
    std::function<void()> activity_callback;
    {
        std::lock_guard lock(mutex_);
        pending_events_.push_back(std::move(event));
        activity_callback = activity_callback_;
    }
    cv_.notify_all();
    if (activity_callback) {
        activity_callback();
    }
}

void CtpTraderGateway::notify_error(const std::string& message) {
    std::function<void()> activity_callback;
    {
        std::lock_guard lock(mutex_);
        last_error_ = message;
        activity_callback = activity_callback_;
    }
    cv_.notify_all();
    if (activity_callback) {
        activity_callback();
    }
}

void CtpTraderGateway::copy_trunc(char* destination, std::size_t destination_size, const std::string& value) {
    if (destination == nullptr || destination_size == 0) {
        return;
    }

    const auto copy_size = std::min(destination_size - 1, value.size());
    std::fill(destination, destination + destination_size, '\0');
    std::copy_n(value.begin(), static_cast<std::ptrdiff_t>(copy_size), destination);
}

std::string CtpTraderGateway::now_string() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t raw_time = std::chrono::system_clock::to_time_t(now);
    std::tm local_time {};
    localtime_s(&local_time, &raw_time);

    std::ostringstream output;
    output << std::put_time(&local_time, "%F %T");
    return output.str();
}

Side CtpTraderGateway::side_from_ctp(char value) {
    return value == THOST_FTDC_D_Sell ? Side::Sell : Side::Buy;
}

Offset CtpTraderGateway::offset_from_ctp(char value) {
    if (value == THOST_FTDC_OF_CloseYesterday) {
        return Offset::CloseYesterday;
    }
    if (value == THOST_FTDC_OF_CloseToday) {
        return Offset::CloseToday;
    }
    if (value == THOST_FTDC_OF_Close) {
        return Offset::Close;
    }
    return Offset::Open;
}

OrderStatus CtpTraderGateway::status_from_ctp(char value) {
    switch (value) {
    case THOST_FTDC_OST_AllTraded:
        return OrderStatus::Filled;
    case THOST_FTDC_OST_PartTradedQueueing:
    case THOST_FTDC_OST_PartTradedNotQueueing:
        return OrderStatus::PartiallyFilled;
    case THOST_FTDC_OST_NoTradeQueueing:
    case THOST_FTDC_OST_NoTradeNotQueueing:
        return OrderStatus::Accepted;
    case THOST_FTDC_OST_Canceled:
        return OrderStatus::Cancelled;
    default:
        return OrderStatus::Submitted;
    }
}

#ifdef ITRADER_ENABLE_CTP_MD
CtpMarketDataGateway::CtpMarketDataGateway(std::string account_id, CtpAccountConfig config)
    : account_id_(std::move(account_id))
    , config_(std::move(config)) {
    if (config_.md_front.empty()) {
        config_.md_front = config_.front;
    }
    if (config_.md_broker_id.empty()) {
        config_.md_broker_id = config_.broker_id;
    }
    if (config_.md_user_id.empty()) {
        config_.md_user_id = config_.user_id;
    }
    if (config_.md_password.empty()) {
        config_.md_password = config_.password;
    }
}

CtpMarketDataGateway::~CtpMarketDataGateway() {
    disconnect();
}

bool CtpMarketDataGateway::connect(std::string* error_message, int timeout_ms) {
    disconnect();

    std::error_code error_code;
    std::filesystem::create_directories(config_.md_flow_dir, error_code);
    api_ = CThostFtdcMdApi::CreateFtdcMdApi(config_.md_flow_dir.c_str(), false, false, config_.production_mode);
    if (api_ == nullptr) {
        if (error_message != nullptr) {
            *error_message = "CreateFtdcMdApi returned null";
        }
        return false;
    }

    {
        std::lock_guard lock(mutex_);
        front_connected_ = false;
        login_complete_ = false;
        ready_ = false;
        last_error_.clear();
        latest_ticks_.clear();
    }

    api_->RegisterSpi(this);

    std::vector<char> front(config_.md_front.begin(), config_.md_front.end());
    front.push_back('\0');
    api_->RegisterFront(front.data());
    api_->Init();

    if (!wait_until([this] { return front_connected_; }, timeout_ms)) {
        if (error_message != nullptr) {
            *error_message = last_error_.empty() ? "Timed out waiting for CTP market-data front connection" : last_error_;
        }
        return false;
    }

    CThostFtdcReqUserLoginField login_request {};
    copy_trunc(login_request.BrokerID, sizeof(login_request.BrokerID), config_.md_broker_id);
    copy_trunc(login_request.UserID, sizeof(login_request.UserID), config_.md_user_id);
    copy_trunc(login_request.Password, sizeof(login_request.Password), config_.md_password);
    copy_trunc(login_request.UserProductInfo, sizeof(login_request.UserProductInfo), config_.product_info);
    copy_trunc(login_request.InterfaceProductInfo, sizeof(login_request.InterfaceProductInfo), "iTraderMD");
    copy_trunc(login_request.ClientIPAddress, sizeof(login_request.ClientIPAddress), "127.0.0.1");
    api_->ReqUserLogin(&login_request, next_request_id());

    const bool success = wait_until([this] { return login_complete_; }, timeout_ms) && ready();
    if (!success && error_message != nullptr) {
        *error_message = last_error_.empty() ? "Timed out waiting for CTP market-data login" : last_error_;
    }

    if (!success) {
        return false;
    }

    std::vector<std::string> instruments;
    {
        std::lock_guard lock(mutex_);
        reconnect_attempts_ = 0;
        last_error_.clear();
        instruments = subscribed_instruments_;
    }

    if (!instruments.empty()) {
        std::vector<char*> instrument_ptrs;
        instrument_ptrs.reserve(instruments.size());
        for (auto& instrument : instruments) {
            instrument_ptrs.push_back(instrument.data());
        }

        const int return_code = api_->SubscribeMarketData(instrument_ptrs.data(), static_cast<int>(instrument_ptrs.size()));
        if (return_code != 0) {
            if (error_message != nullptr) {
                *error_message = "SubscribeMarketData returned " + std::to_string(return_code) + " during reconnect";
            }
            return false;
        }
    }

    return success;
}

bool CtpMarketDataGateway::ensure_ready(std::string* error_message, int timeout_ms) {
    if (ready()) {
        if (error_message != nullptr) {
            error_message->clear();
        }
        return true;
    }

    if (!config_.reconnect_enabled) {
        if (error_message != nullptr) {
            *error_message = "CTP market-data reconnect is disabled";
        }
        return false;
    }

    {
        std::lock_guard lock(mutex_);
        const auto now_ms = steady_now_millis();
        if (config_.reconnect_max_attempts > 0 && reconnect_attempts_ >= config_.reconnect_max_attempts) {
            if (error_message != nullptr) {
                *error_message = "CTP market-data reconnect attempt limit reached";
            }
            return false;
        }
        if (last_connect_attempt_ms_ > 0
            && config_.reconnect_retry_interval_ms > 0
            && (now_ms - last_connect_attempt_ms_) < config_.reconnect_retry_interval_ms) {
            if (error_message != nullptr) {
                *error_message = "CTP market-data reconnect backoff is active";
            }
            return false;
        }
        last_connect_attempt_ms_ = now_ms;
        ++reconnect_attempts_;
    }

    std::string reconnect_error;
    const bool success = connect(&reconnect_error, timeout_ms);
    if (!success && error_message != nullptr) {
        *error_message = reconnect_error.empty() ? "CTP market-data reconnect failed" : reconnect_error;
    } else if (success && error_message != nullptr) {
        error_message->clear();
    }
    return success;
}

void CtpMarketDataGateway::disconnect() {
    if (api_ != nullptr) {
        api_->RegisterSpi(nullptr);
        api_->Release();
        api_ = nullptr;
    }

    std::lock_guard lock(mutex_);
    ready_ = false;
    login_complete_ = false;
    front_connected_ = false;
}

bool CtpMarketDataGateway::ready() const {
    std::lock_guard lock(mutex_);
    return ready_;
}

bool CtpMarketDataGateway::subscribe_market_data(const std::vector<std::string>& instruments, std::string* error_message) {
    std::vector<std::string> normalized_instruments;
    normalized_instruments.reserve(instruments.size());
    for (const auto& instrument : instruments) {
        if (instrument.empty()) {
            continue;
        }
        if (std::find(normalized_instruments.begin(), normalized_instruments.end(), instrument) == normalized_instruments.end()) {
            normalized_instruments.push_back(instrument);
        }
    }

    if (normalized_instruments.empty()) {
        return true;
    }

    {
        std::lock_guard lock(mutex_);
        for (const auto& instrument : normalized_instruments) {
            if (std::find(subscribed_instruments_.begin(), subscribed_instruments_.end(), instrument) == subscribed_instruments_.end()) {
                subscribed_instruments_.push_back(instrument);
            }
        }
    }

    if (!ready()) {
        if (error_message != nullptr) {
            *error_message = "CTP market-data gateway is not ready";
        }
        return false;
    }

    auto instrument_copies = normalized_instruments;
    std::vector<char*> instrument_ptrs;
    instrument_ptrs.reserve(instrument_copies.size());
    for (auto& instrument : instrument_copies) {
        instrument_ptrs.push_back(instrument.data());
    }

    const int return_code = api_->SubscribeMarketData(instrument_ptrs.data(), static_cast<int>(instrument_ptrs.size()));
    if (return_code != 0) {
        if (error_message != nullptr) {
            *error_message = "SubscribeMarketData returned " + std::to_string(return_code);
        }
        return false;
    }
    return true;
}

std::vector<MarketTick> CtpMarketDataGateway::drain_ticks() {
    std::lock_guard lock(mutex_);
    std::vector<MarketTick> ticks;
    ticks.reserve(latest_ticks_.size());
    for (auto& [_, tick] : latest_ticks_) {
        ticks.push_back(std::move(tick));
    }
    latest_ticks_.clear();
    return ticks;
}

void CtpMarketDataGateway::set_activity_callback(std::function<void()> callback) {
    std::lock_guard lock(mutex_);
    activity_callback_ = std::move(callback);
}

void CtpMarketDataGateway::OnFrontConnected() {
    {
        std::lock_guard lock(mutex_);
        front_connected_ = true;
    }
    cv_.notify_all();
}

void CtpMarketDataGateway::OnFrontDisconnected(int nReason) {
    notify_error("CTP market-data front disconnected, reason=" + std::to_string(nReason));
    std::lock_guard lock(mutex_);
    front_connected_ = false;
    login_complete_ = false;
    ready_ = false;
}

void CtpMarketDataGateway::OnRspUserLogin(CThostFtdcRspUserLoginField*, CThostFtdcRspInfoField* rsp_info, int, bool bIsLast) {
    if (!bIsLast) {
        return;
    }

    {
        std::lock_guard lock(mutex_);
        const auto error = rsp_error_message(rsp_info);
        if (!error.empty()) {
            last_error_ = error;
            ready_ = false;
        } else {
            ready_ = true;
        }
        login_complete_ = true;
    }
    cv_.notify_all();
}

void CtpMarketDataGateway::OnRspSubMarketData(CThostFtdcSpecificInstrumentField*, CThostFtdcRspInfoField* rsp_info, int, bool bIsLast) {
    if (!bIsLast) {
        return;
    }

    const auto error = rsp_error_message(rsp_info);
    if (!error.empty()) {
        notify_error(error);
    }
}

void CtpMarketDataGateway::OnRspError(CThostFtdcRspInfoField* rsp_info, int, bool) {
    const auto error = rsp_error_message(rsp_info);
    if (!error.empty()) {
        notify_error(error);
    }
}

void CtpMarketDataGateway::OnRtnDepthMarketData(CThostFtdcDepthMarketDataField* depth_market_data) {
    if (depth_market_data == nullptr) {
        return;
    }

    MarketTick tick;
    tick.timestamp = depth_market_timestamp(depth_market_data);
    tick.trading_day = trim_copy(depth_market_data->TradingDay);
    tick.instrument = depth_market_data->InstrumentID;
    tick.exchange = depth_market_data->ExchangeID;
    tick.last = depth_market_data->LastPrice;
    tick.bid = depth_market_data->BidPrice1;
    tick.ask = depth_market_data->AskPrice1;
    tick.upper_limit_price = sanitize_market_price(depth_market_data->UpperLimitPrice);
    tick.lower_limit_price = sanitize_market_price(depth_market_data->LowerLimitPrice);
    tick.volume = depth_market_data->Volume;

    std::function<void()> activity_callback;
    {
        std::lock_guard lock(mutex_);
        latest_ticks_[tick.instrument] = std::move(tick);
        activity_callback = activity_callback_;
    }
    cv_.notify_all();
    if (activity_callback) {
        activity_callback();
    }
}

bool CtpMarketDataGateway::wait_until(const std::function<bool()>& predicate, int timeout_ms) {
    std::unique_lock lock(mutex_);
    return cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms), predicate);
}

int CtpMarketDataGateway::next_request_id() {
    std::lock_guard lock(mutex_);
    return ++request_id_;
}

void CtpMarketDataGateway::notify_error(const std::string& message) {
    std::function<void()> activity_callback;
    {
        std::lock_guard lock(mutex_);
        last_error_ = message;
        activity_callback = activity_callback_;
    }
    cv_.notify_all();
    if (activity_callback) {
        activity_callback();
    }
}

void CtpMarketDataGateway::copy_trunc(char* destination, std::size_t destination_size, const std::string& value) {
    if (destination == nullptr || destination_size == 0) {
        return;
    }

    const auto copy_size = std::min(destination_size - 1, value.size());
    std::fill(destination, destination + destination_size, '\0');
    std::copy_n(value.begin(), static_cast<std::ptrdiff_t>(copy_size), destination);
}
#endif

} // namespace itrader

#endif
