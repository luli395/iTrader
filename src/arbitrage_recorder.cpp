#include "itrader/domain.hpp"
#include "itrader/ini.hpp"
#include "itrader/runtime_paths.hpp"

#include "ThostFtdcTraderApi.h"
#include "ThostFtdcMdApi.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstddef>
#include <cctype>
#include <ctime>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

std::atomic_bool g_stop_requested {false};

void on_interrupt(int) {
    g_stop_requested = true;
}

struct SignalInstaller {
    SignalInstaller() {
        std::signal(SIGINT, on_interrupt);
    }
} g_signal_installer;

struct RecorderAccountConfig {
    std::string front;
    std::string broker_id;
    std::string user_id;
    std::string password;
    std::string auth_code;
    std::string app_id;
    std::string md_front;
    std::string md_broker_id;
    std::string md_user_id;
    std::string md_password;
    std::string product_info {"iTrader"};
    std::filesystem::path flow_dir;
    std::filesystem::path md_flow_dir;
    bool production_mode {true};
    bool reconnect_enabled {true};
    int reconnect_retry_interval_ms {3000};
    int reconnect_max_attempts {0};
};

struct RecorderConfig {
    std::string account_id;
    RecorderAccountConfig account;
    std::filesystem::path output_dir;
    std::string discovery_mode {"configured"};
    std::vector<std::string> configured_instruments;
    std::filesystem::path instrument_file;
    std::vector<std::string> exchanges;
    bool use_classified_query {true};
    bool active_only {true};
    int connect_timeout_ms {15000};
    int discovery_query_timeout_ms {60000};
    int discovery_query_interval_ms {1000};
    int subscribe_batch_size {500};
    int subscribe_interval_ms {100};
    int flush_interval_ms {1000};
    int status_interval_ms {30000};
    int idle_sleep_ms {250};
};

struct ArbitrageInstrument {
    std::string instrument_id;
    std::string exchange_id;
    std::string exchange_inst_id;
    std::string product_id;
    std::string instrument_name;
    char product_class {'\0'};
    char combination_type {'\0'};
    bool is_trading {false};
};

struct RawMarketDataRecord {
    std::string trading_day;
    CThostFtdcDepthMarketDataField depth_market_data {};
};

std::string rsp_error_message(CThostFtdcRspInfoField* rsp_info) {
    if (rsp_info == nullptr || rsp_info->ErrorID == 0) {
        return {};
    }
    return std::to_string(rsp_info->ErrorID) + ": " + rsp_info->ErrorMsg;
}

long long steady_now_millis() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        Clock::now().time_since_epoch()).count();
}

std::string upper_copy(std::string_view raw) {
    std::string value(raw);
    for (char& ch : value) {
        ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    }
    return value;
}

template <std::size_t Size>
std::string ctp_string(const char (&value)[Size]) {
    const auto end = std::find(value, value + Size, '\0');
    return itrader::trim_copy(std::string(value, end));
}

std::string normalize_exchange_code(std::string_view raw) {
    const auto value = upper_copy(itrader::trim_copy(raw));
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

std::vector<std::string> normalize_exchanges(const std::vector<std::string>& raw_exchanges) {
    std::vector<std::string> exchanges;
    exchanges.reserve(raw_exchanges.size());
    for (const auto& raw_exchange : raw_exchanges) {
        const auto exchange = normalize_exchange_code(raw_exchange);
        if (exchange.empty()) {
            continue;
        }
        if (std::find(exchanges.begin(), exchanges.end(), exchange) == exchanges.end()) {
            exchanges.push_back(exchange);
        }
    }
    return exchanges;
}

std::string canonical_trading_day(std::string_view raw) {
    std::string digits;
    digits.reserve(raw.size());
    for (const char ch : raw) {
        if (std::isdigit(static_cast<unsigned char>(ch)) != 0) {
            digits.push_back(ch);
        }
    }
    return digits.size() == 8 ? digits : std::string {};
}

std::string local_calendar_day() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t raw_time = std::chrono::system_clock::to_time_t(now);
    std::tm local_time {};
    localtime_s(&local_time, &raw_time);

    std::ostringstream output;
    output << std::put_time(&local_time, "%Y%m%d");
    return output.str();
}

std::string normalize_front_address(std::string raw) {
    raw = itrader::trim_copy(raw);
    if (raw.empty() || raw.find("://") != std::string::npos) {
        return raw;
    }
    return "tcp://" + raw;
}

std::filesystem::path resolve_path(const std::filesystem::path& base_dir, std::string_view raw_path) {
    const auto trimmed = itrader::trim_copy(raw_path);
    if (trimmed.empty()) {
        return {};
    }

    std::filesystem::path candidate(trimmed);
    if (candidate.is_absolute()) {
        return candidate.lexically_normal();
    }
    return (base_dir / candidate).lexically_normal();
}

void copy_trunc(char* destination, std::size_t destination_size, const std::string& value) {
    if (destination == nullptr || destination_size == 0) {
        return;
    }

    const auto copy_size = std::min(destination_size - 1, value.size());
    std::fill(destination, destination + destination_size, '\0');
    std::copy_n(value.begin(), static_cast<std::ptrdiff_t>(copy_size), destination);
}

bool looks_like_retryable_md_connect_error(std::string_view error_message) {
    const auto normalized = upper_copy(itrader::trim_copy(error_message));
    return normalized.find("TIMED OUT WAITING FOR CTP MD FRONT CONNECTION") != std::string::npos
        || normalized.find("TIMED OUT WAITING FOR CTP MD LOGIN") != std::string::npos
        || normalized.find("CTP MD FRONT DISCONNECTED") != std::string::npos;
}

std::string instrument_key(const ArbitrageInstrument& instrument) {
    return upper_copy(instrument.exchange_id) + "::" + instrument.instrument_id;
}

ArbitrageInstrument make_arbitrage_instrument(
    const CThostFtdcInstrumentField& instrument,
    std::string_view requested_exchange) {
    ArbitrageInstrument result;
    result.instrument_id = ctp_string(instrument.InstrumentID);
    result.exchange_id = normalize_exchange_code(ctp_string(instrument.ExchangeID));
    if (result.exchange_id.empty()) {
        result.exchange_id = normalize_exchange_code(requested_exchange);
    }
    result.exchange_inst_id = ctp_string(instrument.ExchangeInstID);
    result.product_id = ctp_string(instrument.ProductID);
    result.instrument_name = ctp_string(instrument.InstrumentName);
    result.product_class = instrument.ProductClass;
    result.combination_type = instrument.CombinationType;
    result.is_trading = instrument.IsTrading != 0;
    return result;
}

bool should_keep_arbitrage_instrument(const RecorderConfig& config, const ArbitrageInstrument& instrument) {
    if (instrument.instrument_id.empty() || instrument.exchange_id.empty()) {
        return false;
    }
    if (instrument.product_class != THOST_FTDC_PC_Combination) {
        return false;
    }
    if (config.active_only && !instrument.is_trading) {
        return false;
    }
    return std::find(config.exchanges.begin(), config.exchanges.end(), normalize_exchange_code(instrument.exchange_id))
        != config.exchanges.end();
}

std::string normalize_discovery_mode(std::string raw) {
    raw = upper_copy(itrader::trim_copy(raw));
    if (raw.empty() || raw == "CONFIG" || raw == "CONFIGURED" || raw == "FILE") {
        return "configured";
    }
    if (raw == "TRADER" || raw == "TRADER_QUERY" || raw == "CTP_TRADER") {
        return "trader_query";
    }
    return raw;
}

std::optional<ArbitrageInstrument> parse_configured_instrument(
    std::string_view raw,
    const std::vector<std::string>& known_exchanges) {
    auto line = itrader::trim_copy(raw);
    if (line.empty() || line.starts_with('#') || line.starts_with(';')) {
        return std::nullopt;
    }

    const auto fields = itrader::split_csv(line);
    if (fields.empty()) {
        return std::nullopt;
    }

    auto instrument_id = itrader::trim_copy(fields.front());
    if (upper_copy(instrument_id) == "INSTRUMENT" || upper_copy(instrument_id) == "INSTRUMENTID") {
        return std::nullopt;
    }

    std::string exchange_id;
    if (fields.size() >= 2) {
        exchange_id = normalize_exchange_code(fields[1]);
    } else {
        for (const char delimiter : {'@', '|', ':'}) {
            const auto delimiter_pos = instrument_id.rfind(delimiter);
            if (delimiter_pos == std::string::npos || delimiter_pos + 1 >= instrument_id.size()) {
                continue;
            }
            exchange_id = normalize_exchange_code(instrument_id.substr(delimiter_pos + 1));
            instrument_id = itrader::trim_copy(instrument_id.substr(0, delimiter_pos));
            break;
        }

        if (exchange_id.empty()) {
            const auto dot_pos = instrument_id.rfind('.');
            if (dot_pos != std::string::npos && dot_pos + 1 < instrument_id.size()) {
                const auto suffix = normalize_exchange_code(instrument_id.substr(dot_pos + 1));
                if (std::find(known_exchanges.begin(), known_exchanges.end(), suffix) != known_exchanges.end()) {
                    exchange_id = suffix;
                    instrument_id = itrader::trim_copy(instrument_id.substr(0, dot_pos));
                }
            }
        }
    }

    if (instrument_id.empty()) {
        return std::nullopt;
    }

    ArbitrageInstrument instrument;
    instrument.instrument_id = std::move(instrument_id);
    instrument.exchange_id = std::move(exchange_id);
    instrument.product_class = THOST_FTDC_PC_Combination;
    instrument.is_trading = true;
    return instrument;
}

std::vector<ArbitrageInstrument> load_configured_instruments(const RecorderConfig& config) {
    std::map<std::string, ArbitrageInstrument> instruments_by_key;

    const auto add_instrument = [&instruments_by_key](ArbitrageInstrument instrument) {
        const auto key = instrument.exchange_id.empty()
            ? upper_copy(instrument.instrument_id)
            : instrument_key(instrument);
        instruments_by_key[key] = std::move(instrument);
    };

    for (const auto& raw_instrument : config.configured_instruments) {
        if (auto instrument = parse_configured_instrument(raw_instrument, config.exchanges); instrument.has_value()) {
            add_instrument(std::move(*instrument));
        }
    }

    if (!config.instrument_file.empty()) {
        std::ifstream input(config.instrument_file);
        if (!input.is_open()) {
            throw std::runtime_error("Unable to open arbitrage instrument file: " + config.instrument_file.string());
        }

        std::string line;
        while (std::getline(input, line)) {
            if (auto instrument = parse_configured_instrument(line, config.exchanges); instrument.has_value()) {
                add_instrument(std::move(*instrument));
            }
        }
    }

    std::vector<ArbitrageInstrument> instruments;
    instruments.reserve(instruments_by_key.size());
    for (auto& [_, instrument] : instruments_by_key) {
        instruments.push_back(std::move(instrument));
    }
    return instruments;
}

RecorderAccountConfig read_account_config(
    const itrader::IniFile& ini,
    const std::string& section,
    const std::filesystem::path& config_path) {
    RecorderAccountConfig config;
    const auto account_id = section.substr(section.find('.') + 1);
    const auto base_dir = config_path.parent_path();

    config.front = normalize_front_address(ini.get(section, "front"));
    config.broker_id = ini.get(section, "broker_id");
    config.user_id = ini.get(section, "user_id");
    config.password = ini.get(section, "password");
    config.auth_code = ini.get(section, "auth_code");
    config.app_id = ini.get(section, "app_id");

    config.md_front = normalize_front_address(ini.get(section, "md_front", config.front));
    config.md_broker_id = ini.get(section, "md_broker_id", config.broker_id);
    config.md_user_id = ini.get(section, "md_user_id", config.user_id);
    config.md_password = ini.get(section, "md_password", config.password);
    config.product_info = ini.get(section, "product_info", "iTrader");

    const auto flow_dir = ini.get(
        section,
        "flow_dir",
        itrader::default_ctp_flow_dir(config_path, account_id).generic_string());
    config.flow_dir = resolve_path(base_dir, ini.get(section, "trader_flow_dir", flow_dir));
    config.md_flow_dir = resolve_path(
        base_dir,
        ini.get(
            section,
            "md_flow_dir",
            itrader::default_ctp_md_flow_dir(config_path, account_id).generic_string()));

    config.production_mode = ini.get_bool(section, "production_mode", true);
    config.reconnect_enabled = ini.get_bool(section, "reconnect_enabled", true);
    config.reconnect_retry_interval_ms = std::max(0, ini.get_int(section, "reconnect_retry_interval_ms", 3000));
    config.reconnect_max_attempts = std::max(0, ini.get_int(section, "reconnect_max_attempts", 0));
    return config;
}

RecorderConfig read_recorder_config(const std::filesystem::path& config_path, const itrader::IniFile& ini, bool connect_test = false) {
    RecorderConfig config;
    const auto account_sections = ini.sections_with_prefix("account.");
    if (account_sections.empty()) {
        throw std::runtime_error("Arbitrage recorder config needs exactly one [account.*] section.");
    }
    if (account_sections.size() > 1) {
        throw std::runtime_error("Arbitrage recorder currently supports exactly one [account.*] section.");
    }

    config.account_id = account_sections.front().substr(account_sections.front().find('.') + 1);
    config.account = read_account_config(ini, account_sections.front(), config_path);
    config.output_dir = resolve_path(
        config_path.parent_path(),
        ini.get(
            "arbitrage_recorder",
            "output_dir",
            (itrader::runtime_namespace_directory(config_path) / "ctpmd").generic_string()));
    config.discovery_mode = normalize_discovery_mode(ini.get("arbitrage_recorder", "discovery_mode", "configured"));
    config.configured_instruments = ini.get_list("arbitrage_recorder", "instruments");
    config.instrument_file = resolve_path(config_path.parent_path(), ini.get("arbitrage_recorder", "instrument_file"));
    config.exchanges = normalize_exchanges(ini.get_list("arbitrage_recorder", "exchanges"));
    if (config.exchanges.empty()) {
        config.exchanges = {"SHFE", "DCE", "CZCE", "CFFEX", "INE", "GFEX"};
    }

    config.use_classified_query = ini.get_bool("arbitrage_recorder", "use_classified_query", true);
    config.active_only = ini.get_bool("arbitrage_recorder", "active_only", true);
    config.connect_timeout_ms = std::max(1000, ini.get_int("arbitrage_recorder", "connect_timeout_ms", 15000));
    config.discovery_query_timeout_ms = std::max(1000, ini.get_int("arbitrage_recorder", "discovery_query_timeout_ms", 60000));
    config.discovery_query_interval_ms = std::max(0, ini.get_int("arbitrage_recorder", "discovery_query_interval_ms", 1000));
    config.subscribe_batch_size = std::max(1, ini.get_int("arbitrage_recorder", "subscribe_batch_size", 500));
    config.subscribe_interval_ms = std::max(0, ini.get_int("arbitrage_recorder", "subscribe_interval_ms", 100));
    config.flush_interval_ms = std::max(0, ini.get_int("arbitrage_recorder", "flush_interval_ms", 1000));
    config.status_interval_ms = std::max(0, ini.get_int("arbitrage_recorder", "status_interval_ms", 30000));
    config.idle_sleep_ms = std::max(50, ini.get_int("arbitrage_recorder", "idle_sleep_ms", 250));

    if (config.discovery_mode != "configured" && config.discovery_mode != "trader_query") {
        throw std::runtime_error("[arbitrage_recorder] discovery_mode must be configured or trader_query.");
    }
    if (config.account.md_front.empty()) {
        throw std::runtime_error("Arbitrage recorder account must set md_front.");
    }
    if (!connect_test
        && config.discovery_mode == "configured"
        && config.configured_instruments.empty()
        && config.instrument_file.empty()) {
        throw std::runtime_error("[arbitrage_recorder] configured discovery needs instruments=... or instrument_file=...");
    }
    if (config.discovery_mode == "trader_query") {
        if (config.account.front.empty()) {
            throw std::runtime_error("trader_query discovery must set front.");
        }
        if (config.account.broker_id.empty() || config.account.user_id.empty() || config.account.password.empty()) {
            throw std::runtime_error("trader_query discovery must set broker_id, user_id, and password.");
        }
    }
    if (config.output_dir.empty()) {
        throw std::runtime_error("[arbitrage_recorder] output_dir resolved to an empty path.");
    }

    return config;
}

class ArbitrageInstrumentDiscoverer final : private CThostFtdcTraderSpi {
public:
    explicit ArbitrageInstrumentDiscoverer(RecorderConfig config)
        : config_(std::move(config)) {}

    ~ArbitrageInstrumentDiscoverer() {
        disconnect();
    }

    bool connect(std::string* error_message, int timeout_ms) {
        disconnect();

        std::error_code error_code;
        std::filesystem::create_directories(config_.account.flow_dir, error_code);
        if (error_code) {
            if (error_message != nullptr) {
                *error_message = "Unable to create CTP trader flow directory: " + config_.account.flow_dir.string();
            }
            return false;
        }

        const auto flow_dir = config_.account.flow_dir.string();
        CThostFtdcTraderApi* api = CThostFtdcTraderApi::CreateFtdcTraderApi(flow_dir.c_str(), config_.account.production_mode);
        if (api == nullptr) {
            if (error_message != nullptr) {
                *error_message = "CreateFtdcTraderApi returned null.";
            }
            return false;
        }

        {
            std::lock_guard lock(mutex_);
            api_ = api;
            request_id_ = 0;
            front_connected_ = false;
            auth_complete_ = config_.account.auth_code.empty() || config_.account.app_id.empty();
            login_complete_ = false;
            ready_ = false;
            last_error_.clear();
            query_states_.clear();
        }

        api_->RegisterSpi(this);
        api_->SubscribePublicTopic(THOST_TERT_QUICK);
        api_->SubscribePrivateTopic(THOST_TERT_QUICK);
        std::vector<char> front(config_.account.front.begin(), config_.account.front.end());
        front.push_back('\0');
        api_->RegisterFront(front.data());
        api_->Init();

        if (!wait_until([this] { return front_connected_; }, timeout_ms)) {
            if (error_message != nullptr) {
                *error_message = last_error().empty()
                    ? "Timed out waiting for CTP trader front connection."
                    : last_error();
            }
            return false;
        }

        if (!is_auth_complete()) {
            CThostFtdcReqAuthenticateField request {};
            copy_trunc(request.BrokerID, sizeof(request.BrokerID), config_.account.broker_id);
            copy_trunc(request.UserID, sizeof(request.UserID), config_.account.user_id);
            copy_trunc(request.UserProductInfo, sizeof(request.UserProductInfo), config_.account.product_info);
            copy_trunc(request.AuthCode, sizeof(request.AuthCode), config_.account.auth_code);
            copy_trunc(request.AppID, sizeof(request.AppID), config_.account.app_id);
            api_->ReqAuthenticate(&request, next_request_id());

            if (!wait_until([this] { return auth_complete_; }, timeout_ms)) {
                if (error_message != nullptr) {
                    *error_message = last_error().empty()
                        ? "Timed out waiting for CTP trader authentication."
                        : last_error();
                }
                return false;
            }
            if (!last_error().empty()) {
                if (error_message != nullptr) {
                    *error_message = last_error();
                }
                return false;
            }
        }

        CThostFtdcReqUserLoginField login_request {};
        copy_trunc(login_request.BrokerID, sizeof(login_request.BrokerID), config_.account.broker_id);
        copy_trunc(login_request.UserID, sizeof(login_request.UserID), config_.account.user_id);
        copy_trunc(login_request.Password, sizeof(login_request.Password), config_.account.password);
        copy_trunc(login_request.UserProductInfo, sizeof(login_request.UserProductInfo), config_.account.product_info);
        copy_trunc(login_request.InterfaceProductInfo, sizeof(login_request.InterfaceProductInfo), "iTraderArbDiscovery");
        copy_trunc(login_request.ClientIPAddress, sizeof(login_request.ClientIPAddress), "127.0.0.1");
        api_->ReqUserLogin(&login_request, next_request_id());

        const bool login_success = wait_until([this] { return login_complete_; }, timeout_ms) && ready();
        if (!login_success) {
            if (error_message != nullptr) {
                *error_message = last_error().empty() ? "Timed out waiting for CTP trader login." : last_error();
            }
            return false;
        }

        if (error_message != nullptr) {
            error_message->clear();
        }
        return true;
    }

    void disconnect() {
        CThostFtdcTraderApi* api = nullptr;
        {
            std::lock_guard lock(mutex_);
            api = api_;
            api_ = nullptr;
            ready_ = false;
            login_complete_ = false;
            auth_complete_ = false;
            front_connected_ = false;
            query_states_.clear();
        }

        if (api != nullptr) {
            api->RegisterSpi(nullptr);
            api->Release();
        }
        cv_.notify_all();
    }

    [[nodiscard]] bool ready() const {
        std::lock_guard lock(mutex_);
        return ready_;
    }

    std::vector<ArbitrageInstrument> query_all(std::string* error_message) {
        std::map<std::string, ArbitrageInstrument> instruments_by_key;

        for (std::size_t index = 0; index < config_.exchanges.size(); ++index) {
            const auto& exchange = config_.exchanges[index];
            std::vector<ArbitrageInstrument> instruments = config_.use_classified_query
                ? query_classified_exchange(exchange, error_message)
                : query_instrument_exchange(exchange, error_message);
            if (error_message != nullptr && !error_message->empty()) {
                return {};
            }

            std::cout << "[arbitrage_recorder] discovered " << instruments.size()
                      << " combination instrument(s) on " << exchange << "\n";
            for (auto& instrument : instruments) {
                instruments_by_key[instrument_key(instrument)] = std::move(instrument);
            }

            if (index + 1 < config_.exchanges.size() && config_.discovery_query_interval_ms > 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(config_.discovery_query_interval_ms));
            }
        }

        std::vector<ArbitrageInstrument> result;
        result.reserve(instruments_by_key.size());
        for (auto& [_, instrument] : instruments_by_key) {
            result.push_back(std::move(instrument));
        }
        if (error_message != nullptr) {
            error_message->clear();
        }
        return result;
    }

private:
    struct QueryState {
        std::string exchange;
        bool done {false};
        std::string error;
        std::vector<ArbitrageInstrument> instruments;
    };

    std::vector<ArbitrageInstrument> query_classified_exchange(
        const std::string& exchange,
        std::string* error_message) {
        CThostFtdcTraderApi* api = nullptr;
        {
            std::lock_guard lock(mutex_);
            api = api_;
            if (!ready_ || api == nullptr) {
                if (error_message != nullptr) {
                    *error_message = "CTP trader API is not ready for classified instrument query.";
                }
                return {};
            }
        }

        const int request_id = next_request_id();
        {
            std::lock_guard lock(mutex_);
            query_states_[request_id].exchange = exchange;
        }

        CThostFtdcQryClassifiedInstrumentField request {};
        copy_trunc(request.ExchangeID, sizeof(request.ExchangeID), exchange);
        request.TradingType = config_.active_only ? THOST_FTDC_TD_TRADE : THOST_FTDC_TD_ALL;
        request.ClassType = THOST_FTDC_INS_COMB;
        const int return_code = api->ReqQryClassifiedInstrument(&request, request_id);
        if (return_code != 0) {
            erase_query_state(request_id);
            if (error_message != nullptr) {
                *error_message = "ReqQryClassifiedInstrument(" + exchange + ") returned " + std::to_string(return_code) + ".";
            }
            return {};
        }

        return wait_for_query_result(request_id, "ReqQryClassifiedInstrument(" + exchange + ")", error_message);
    }

    std::vector<ArbitrageInstrument> query_instrument_exchange(
        const std::string& exchange,
        std::string* error_message) {
        CThostFtdcTraderApi* api = nullptr;
        {
            std::lock_guard lock(mutex_);
            api = api_;
            if (!ready_ || api == nullptr) {
                if (error_message != nullptr) {
                    *error_message = "CTP trader API is not ready for instrument query.";
                }
                return {};
            }
        }

        const int request_id = next_request_id();
        {
            std::lock_guard lock(mutex_);
            query_states_[request_id].exchange = exchange;
        }

        CThostFtdcQryInstrumentField request {};
        copy_trunc(request.ExchangeID, sizeof(request.ExchangeID), exchange);
        const int return_code = api->ReqQryInstrument(&request, request_id);
        if (return_code != 0) {
            erase_query_state(request_id);
            if (error_message != nullptr) {
                *error_message = "ReqQryInstrument(" + exchange + ") returned " + std::to_string(return_code) + ".";
            }
            return {};
        }

        return wait_for_query_result(request_id, "ReqQryInstrument(" + exchange + ")", error_message);
    }

    std::vector<ArbitrageInstrument> wait_for_query_result(
        int request_id,
        const std::string& label,
        std::string* error_message) {
        const bool query_done = wait_until([this, request_id] {
            const auto it = query_states_.find(request_id);
            return it != query_states_.end() && it->second.done;
        }, config_.discovery_query_timeout_ms);

        QueryState state;
        {
            std::lock_guard lock(mutex_);
            const auto it = query_states_.find(request_id);
            if (it != query_states_.end()) {
                state = std::move(it->second);
                query_states_.erase(it);
            }
        }

        if (!query_done) {
            if (error_message != nullptr) {
                *error_message = label + " timed out.";
            }
            return {};
        }
        if (!state.error.empty()) {
            if (error_message != nullptr) {
                *error_message = label + " failed: " + state.error;
            }
            return {};
        }

        if (error_message != nullptr) {
            error_message->clear();
        }
        return state.instruments;
    }

    void erase_query_state(int request_id) {
        std::lock_guard lock(mutex_);
        query_states_.erase(request_id);
    }

    bool wait_until(const std::function<bool()>& predicate, int timeout_ms) {
        std::unique_lock lock(mutex_);
        return cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms), predicate);
    }

    int next_request_id() {
        std::lock_guard lock(mutex_);
        return ++request_id_;
    }

    [[nodiscard]] std::string last_error() const {
        std::lock_guard lock(mutex_);
        return last_error_;
    }

    [[nodiscard]] bool is_auth_complete() const {
        std::lock_guard lock(mutex_);
        return auth_complete_;
    }

    void set_error(std::string message) {
        {
            std::lock_guard lock(mutex_);
            last_error_ = std::move(message);
        }
        cv_.notify_all();
    }

    void collect_instrument(
        CThostFtdcInstrumentField* instrument,
        CThostFtdcRspInfoField* rsp_info,
        int request_id,
        bool is_last) {
        std::lock_guard lock(mutex_);
        auto& state = query_states_[request_id];
        const auto error = rsp_error_message(rsp_info);
        if (!error.empty() && state.error.empty()) {
            state.error = error;
        }

        if (instrument != nullptr) {
            auto candidate = make_arbitrage_instrument(*instrument, state.exchange);
            if (should_keep_arbitrage_instrument(config_, candidate)) {
                state.instruments.push_back(std::move(candidate));
            }
        }

        if (is_last) {
            state.done = true;
            cv_.notify_all();
        }
    }

    void OnFrontConnected() override {
        {
            std::lock_guard lock(mutex_);
            front_connected_ = true;
        }
        cv_.notify_all();
        std::cout << "[arbitrage_recorder] CTP trader front connected for discovery.\n";
    }

    void OnFrontDisconnected(int reason) override {
        {
            std::lock_guard lock(mutex_);
            front_connected_ = false;
            auth_complete_ = false;
            login_complete_ = false;
            ready_ = false;
            last_error_ = "CTP trader front disconnected, reason=" + std::to_string(reason);
        }
        cv_.notify_all();
        std::cerr << "[arbitrage_recorder] discovery trader front disconnected, reason=" << reason << "\n";
    }

    void OnRspAuthenticate(CThostFtdcRspAuthenticateField*, CThostFtdcRspInfoField* rsp_info, int, bool is_last) override {
        if (!is_last) {
            return;
        }

        const auto error = rsp_error_message(rsp_info);
        {
            std::lock_guard lock(mutex_);
            if (!error.empty()) {
                last_error_ = "CTP trader authentication failed: " + error;
            }
            auth_complete_ = true;
        }
        cv_.notify_all();
    }

    void OnRspUserLogin(CThostFtdcRspUserLoginField*, CThostFtdcRspInfoField* rsp_info, int, bool is_last) override {
        if (!is_last) {
            return;
        }

        const auto error = rsp_error_message(rsp_info);
        {
            std::lock_guard lock(mutex_);
            if (!error.empty()) {
                last_error_ = "CTP trader login failed: " + error;
                ready_ = false;
            } else {
                ready_ = true;
            }
            login_complete_ = true;
        }
        cv_.notify_all();

        if (!error.empty()) {
            std::cerr << "[arbitrage_recorder] CTP trader login failed: " << error << "\n";
        } else {
            std::cout << "[arbitrage_recorder] CTP trader login succeeded for discovery.\n";
        }
    }

    void OnRspQryClassifiedInstrument(
        CThostFtdcInstrumentField* instrument,
        CThostFtdcRspInfoField* rsp_info,
        int request_id,
        bool is_last) override {
        collect_instrument(instrument, rsp_info, request_id, is_last);
    }

    void OnRspQryInstrument(
        CThostFtdcInstrumentField* instrument,
        CThostFtdcRspInfoField* rsp_info,
        int request_id,
        bool is_last) override {
        collect_instrument(instrument, rsp_info, request_id, is_last);
    }

    void OnRspError(CThostFtdcRspInfoField* rsp_info, int, bool) override {
        const auto error = rsp_error_message(rsp_info);
        if (!error.empty()) {
            set_error(error);
            std::cerr << "[arbitrage_recorder] CTP trader response error: " << error << "\n";
        }
    }

    RecorderConfig config_;
    CThostFtdcTraderApi* api_ {nullptr};

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    int request_id_ {0};
    bool front_connected_ {false};
    bool auth_complete_ {false};
    bool login_complete_ {false};
    bool ready_ {false};
    std::string last_error_;
    std::unordered_map<int, QueryState> query_states_;
};

class ArbitrageBinaryMarketDataRecorder final : private CThostFtdcMdSpi {
public:
    ArbitrageBinaryMarketDataRecorder(RecorderConfig config, std::vector<ArbitrageInstrument> instruments)
        : config_(std::move(config))
        , instruments_(std::move(instruments)) {}

    ~ArbitrageBinaryMarketDataRecorder() {
        stop();
    }

    bool start(std::string* error_message) {
        std::error_code error_code;
        std::filesystem::create_directories(config_.output_dir, error_code);
        if (error_code) {
            if (error_message != nullptr) {
                *error_message = "Unable to create arbitrage recorder output directory: " + config_.output_dir.string();
            }
            return false;
        }

        std::filesystem::create_directories(config_.account.md_flow_dir, error_code);
        if (error_code) {
            if (error_message != nullptr) {
                *error_message = "Unable to create CTP MD flow directory: " + config_.account.md_flow_dir.string();
            }
            return false;
        }

        {
            std::lock_guard lock(queue_mutex_);
            writer_stop_requested_ = false;
            pending_records_.clear();
        }

        writer_thread_ = std::thread(&ArbitrageBinaryMarketDataRecorder::writer_loop, this);
        const bool success = connect(error_message, config_.connect_timeout_ms);
        const auto startup_error = error_message == nullptr ? std::string {} : *error_message;
        if (!success) {
            if (config_.account.reconnect_enabled && looks_like_retryable_md_connect_error(startup_error)) {
                disconnect();
                return true;
            }
            stop();
        }
        return success || (config_.account.reconnect_enabled && looks_like_retryable_md_connect_error(startup_error));
    }

    void stop() {
        disconnect();

        {
            std::lock_guard lock(queue_mutex_);
            writer_stop_requested_ = true;
        }
        queue_cv_.notify_all();

        if (writer_thread_.joinable()) {
            writer_thread_.join();
        }
    }

    [[nodiscard]] bool ready() const {
        std::lock_guard lock(state_mutex_);
        return ready_;
    }

    [[nodiscard]] std::string fatal_error() const {
        std::lock_guard lock(state_mutex_);
        return fatal_error_;
    }

    [[nodiscard]] std::string last_error() const {
        std::lock_guard lock(state_mutex_);
        return last_error_;
    }

    bool ensure_ready(std::string* error_message) {
        if (ready()) {
            if (error_message != nullptr) {
                error_message->clear();
            }
            return true;
        }

        if (!config_.account.reconnect_enabled) {
            if (error_message != nullptr) {
                *error_message = "CTP MD reconnect is disabled.";
            }
            return false;
        }

        {
            std::lock_guard lock(state_mutex_);
            const auto now_ms = steady_now_millis();
            if (config_.account.reconnect_max_attempts > 0 && reconnect_attempts_ >= config_.account.reconnect_max_attempts) {
                if (error_message != nullptr) {
                    *error_message = "CTP MD reconnect attempt limit reached.";
                }
                return false;
            }
            if (last_connect_attempt_ms_ > 0
                && config_.account.reconnect_retry_interval_ms > 0
                && (now_ms - last_connect_attempt_ms_) < config_.account.reconnect_retry_interval_ms) {
                if (error_message != nullptr) {
                    *error_message = "CTP MD reconnect backoff is active.";
                }
                return false;
            }
            last_connect_attempt_ms_ = now_ms;
            ++reconnect_attempts_;
        }

        return connect(error_message, config_.connect_timeout_ms);
    }

    void print_summary(std::string_view label) const {
        std::size_t pending = 0;
        {
            std::lock_guard lock(queue_mutex_);
            pending = pending_records_.size();
        }

        const bool recorder_ready = ready();
        const auto warning = recorder_ready ? std::string {} : last_error();

        std::cout
            << "[arbitrage_recorder] " << label
            << " | ready=" << (recorder_ready ? "yes" : "no")
            << " | instruments=" << instruments_.size()
            << " | record_size=" << sizeof(CThostFtdcDepthMarketDataField)
            << " | received=" << received_ticks_.load()
            << " | written=" << written_ticks_.load()
            << " | dropped_invalid=" << dropped_invalid_ticks_.load()
            << " | max_queue_depth=" << max_queue_depth_.load()
            << " | pending=" << pending;
        if (!warning.empty()) {
            std::cout << " | warning=" << warning;
        }
        std::cout << "\n";
    }

private:
    struct DailyFile {
        std::string trading_day;
        std::filesystem::path path;
        std::ofstream stream;
    };

    bool connect(std::string* error_message, int timeout_ms) {
        disconnect();

        const auto flow_dir = config_.account.md_flow_dir.string();
        CThostFtdcMdApi* api = CThostFtdcMdApi::CreateFtdcMdApi(flow_dir.c_str(), false, false, config_.account.production_mode);
        if (api == nullptr) {
            if (error_message != nullptr) {
                *error_message = "CreateFtdcMdApi returned null.";
            }
            return false;
        }

        {
            std::lock_guard lock(state_mutex_);
            api_ = api;
            request_id_ = 0;
            front_connected_ = false;
            login_complete_ = false;
            ready_ = false;
            last_error_.clear();
            session_trading_day_.clear();
        }

        api_->RegisterSpi(this);
        std::vector<char> front(config_.account.md_front.begin(), config_.account.md_front.end());
        front.push_back('\0');
        api_->RegisterFront(front.data());
        api_->Init();

        if (!wait_until([this] { return front_connected_; }, timeout_ms)) {
            if (error_message != nullptr) {
                std::lock_guard lock(state_mutex_);
                *error_message = last_error_.empty() ? "Timed out waiting for CTP MD front connection." : last_error_;
            }
            return false;
        }

        CThostFtdcReqUserLoginField login_request {};
        copy_trunc(login_request.BrokerID, sizeof(login_request.BrokerID), config_.account.md_broker_id);
        copy_trunc(login_request.UserID, sizeof(login_request.UserID), config_.account.md_user_id);
        copy_trunc(login_request.Password, sizeof(login_request.Password), config_.account.md_password);
        copy_trunc(login_request.UserProductInfo, sizeof(login_request.UserProductInfo), config_.account.product_info);
        copy_trunc(login_request.InterfaceProductInfo, sizeof(login_request.InterfaceProductInfo), "iTraderArbMD");
        copy_trunc(login_request.ClientIPAddress, sizeof(login_request.ClientIPAddress), "127.0.0.1");
        api_->ReqUserLogin(&login_request, next_request_id());

        const bool login_success = wait_until([this] { return login_complete_; }, timeout_ms) && ready();
        if (!login_success) {
            if (error_message != nullptr) {
                std::lock_guard lock(state_mutex_);
                *error_message = last_error_.empty() ? "Timed out waiting for CTP MD login." : last_error_;
            }
            return false;
        }

        if (!subscribe_instruments(error_message)) {
            return false;
        }

        {
            std::lock_guard lock(state_mutex_);
            reconnect_attempts_ = 0;
            last_error_.clear();
        }
        if (error_message != nullptr) {
            error_message->clear();
        }
        return true;
    }

    void disconnect() {
        CThostFtdcMdApi* api = nullptr;
        {
            std::lock_guard lock(state_mutex_);
            api = api_;
            api_ = nullptr;
            ready_ = false;
            login_complete_ = false;
            front_connected_ = false;
        }

        if (api != nullptr) {
            api->RegisterSpi(nullptr);
            api->Release();
        }
        state_cv_.notify_all();
    }

    bool subscribe_instruments(std::string* error_message) {
        CThostFtdcMdApi* api = nullptr;
        {
            std::lock_guard lock(state_mutex_);
            api = api_;
            if (!ready_ || api == nullptr) {
                if (error_message != nullptr) {
                    *error_message = "CTP MD API is not ready for subscription.";
                }
                return false;
            }
        }

        std::vector<std::string> instrument_ids;
        instrument_ids.reserve(instruments_.size());
        for (const auto& instrument : instruments_) {
            if (!instrument.instrument_id.empty()
                && std::find(instrument_ids.begin(), instrument_ids.end(), instrument.instrument_id) == instrument_ids.end()) {
                instrument_ids.push_back(instrument.instrument_id);
            }
        }

        if (instrument_ids.empty()) {
            std::cout << "[arbitrage_recorder] no market-data subscription requested.\n";
            return true;
        }

        for (std::size_t offset = 0; offset < instrument_ids.size(); offset += static_cast<std::size_t>(config_.subscribe_batch_size)) {
            const auto end = std::min(instrument_ids.size(), offset + static_cast<std::size_t>(config_.subscribe_batch_size));
            std::vector<std::string> instrument_copies(instrument_ids.begin() + static_cast<std::ptrdiff_t>(offset),
                instrument_ids.begin() + static_cast<std::ptrdiff_t>(end));
            std::vector<char*> instrument_ptrs;
            instrument_ptrs.reserve(instrument_copies.size());
            for (auto& instrument : instrument_copies) {
                instrument_ptrs.push_back(instrument.data());
            }

            const int return_code = api->SubscribeMarketData(instrument_ptrs.data(), static_cast<int>(instrument_ptrs.size()));
            if (return_code != 0) {
                if (error_message != nullptr) {
                    *error_message = "SubscribeMarketData batch returned " + std::to_string(return_code)
                        + " at offset " + std::to_string(offset) + ".";
                }
                return false;
            }

            if (end < instrument_ids.size() && config_.subscribe_interval_ms > 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(config_.subscribe_interval_ms));
            }
        }

        std::cout << "[arbitrage_recorder] submitted subscription for "
                  << instrument_ids.size() << " arbitrage instrument(s) in "
                  << ((instrument_ids.size() + static_cast<std::size_t>(config_.subscribe_batch_size) - 1)
                      / static_cast<std::size_t>(config_.subscribe_batch_size))
                  << " batch(es).\n";
        return true;
    }

    bool wait_until(const std::function<bool()>& predicate, int timeout_ms) {
        std::unique_lock lock(state_mutex_);
        return state_cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms), predicate);
    }

    int next_request_id() {
        std::lock_guard lock(state_mutex_);
        return ++request_id_;
    }

    void set_error(std::string message) {
        {
            std::lock_guard lock(state_mutex_);
            last_error_ = std::move(message);
        }
        state_cv_.notify_all();
    }

    void set_fatal_error(std::string message) {
        {
            std::lock_guard lock(state_mutex_);
            fatal_error_ = std::move(message);
        }
        state_cv_.notify_all();
    }

    [[nodiscard]] std::string session_trading_day() const {
        std::lock_guard lock(state_mutex_);
        return session_trading_day_;
    }

    [[nodiscard]] std::string trading_day_for_record(const CThostFtdcDepthMarketDataField& depth_market_data) const {
        auto trading_day = canonical_trading_day(ctp_string(depth_market_data.TradingDay));
        if (!trading_day.empty()) {
            return trading_day;
        }

        trading_day = session_trading_day();
        if (!trading_day.empty()) {
            return trading_day;
        }

        trading_day = canonical_trading_day(ctp_string(depth_market_data.ActionDay));
        if (!trading_day.empty()) {
            return trading_day;
        }

        return local_calendar_day();
    }

    void observe_queue_depth(std::size_t depth) {
        auto current = max_queue_depth_.load();
        while (depth > current && !max_queue_depth_.compare_exchange_weak(current, depth)) {
        }
    }

    std::filesystem::path output_path_for_day(std::string_view trading_day) const {
        return config_.output_dir / (std::string(trading_day) + "_arbitrage.ctpmd.bin");
    }

    void open_daily_file(DailyFile& file, std::string trading_day) {
        file.trading_day = std::move(trading_day);
        file.path = output_path_for_day(file.trading_day);

        std::error_code error_code;
        std::filesystem::create_directories(file.path.parent_path(), error_code);
        if (error_code) {
            throw std::runtime_error("Unable to create arbitrage recorder directory: " + file.path.parent_path().string());
        }

        file.stream.open(file.path, std::ios::out | std::ios::binary | std::ios::app);
        if (!file.stream.is_open()) {
            throw std::runtime_error("Unable to open arbitrage recorder file: " + file.path.string());
        }
    }

    void write_record(DailyFile& file, const RawMarketDataRecord& record) {
        if (!file.stream.is_open() || file.trading_day != record.trading_day) {
            if (file.stream.is_open()) {
                file.stream.flush();
                file.stream.close();
            }
            open_daily_file(file, record.trading_day);
        }

        file.stream.write(
            reinterpret_cast<const char*>(&record.depth_market_data),
            static_cast<std::streamsize>(sizeof(record.depth_market_data)));
        if (!file.stream.good()) {
            throw std::runtime_error("Failed writing arbitrage recorder file: " + file.path.string());
        }
    }

    static void flush_file(DailyFile& file) {
        if (file.stream.is_open()) {
            file.stream.flush();
        }
    }

    static void close_file(DailyFile& file) {
        if (file.stream.is_open()) {
            file.stream.flush();
            file.stream.close();
        }
    }

    void writer_loop() {
        try {
            DailyFile file;
            const auto flush_interval = std::chrono::milliseconds(std::max(250, config_.flush_interval_ms));
            const auto status_interval = std::chrono::milliseconds(std::max(1000, config_.status_interval_ms));
            auto next_flush_at = Clock::now() + flush_interval;
            auto next_status_at = Clock::now() + status_interval;

            while (true) {
                std::deque<RawMarketDataRecord> batch;
                bool should_stop = false;
                {
                    std::unique_lock lock(queue_mutex_);
                    queue_cv_.wait_for(lock, std::chrono::milliseconds(config_.idle_sleep_ms), [this] {
                        return writer_stop_requested_ || !pending_records_.empty();
                    });

                    if (!pending_records_.empty()) {
                        batch.swap(pending_records_);
                    }
                    should_stop = writer_stop_requested_ && pending_records_.empty();
                }

                for (const auto& record : batch) {
                    write_record(file, record);
                    ++written_ticks_;
                }

                const auto now = Clock::now();
                if (config_.flush_interval_ms == 0 || now >= next_flush_at || should_stop) {
                    flush_file(file);
                    next_flush_at = now + flush_interval;
                }

                if (config_.status_interval_ms > 0 && now >= next_status_at) {
                    print_summary("heartbeat");
                    next_status_at = now + status_interval;
                }

                if (should_stop && batch.empty()) {
                    break;
                }
            }

            flush_file(file);
            close_file(file);
        } catch (const std::exception& ex) {
            set_fatal_error(std::string("Arbitrage recorder writer thread failed: ") + ex.what());
            g_stop_requested = true;
        }
    }

    void OnFrontConnected() override {
        {
            std::lock_guard lock(state_mutex_);
            front_connected_ = true;
        }
        state_cv_.notify_all();
        std::cout << "[arbitrage_recorder] CTP MD front connected.\n";
    }

    void OnFrontDisconnected(int reason) override {
        {
            std::lock_guard lock(state_mutex_);
            front_connected_ = false;
            login_complete_ = false;
            ready_ = false;
            last_error_ = "CTP MD front disconnected, reason=" + std::to_string(reason);
        }
        state_cv_.notify_all();
        std::cerr << "[arbitrage_recorder] disconnected from CTP MD front, reason=" << reason << "\n";
    }

    void OnRspUserLogin(CThostFtdcRspUserLoginField* login_response, CThostFtdcRspInfoField* rsp_info, int, bool is_last) override {
        if (!is_last) {
            return;
        }

        const auto error = rsp_error_message(rsp_info);
        {
            std::lock_guard lock(state_mutex_);
            if (!error.empty()) {
                last_error_ = error;
                ready_ = false;
            } else {
                ready_ = true;
                if (login_response != nullptr) {
                    session_trading_day_ = canonical_trading_day(ctp_string(login_response->TradingDay));
                }
                if (session_trading_day_.empty() && api_ != nullptr) {
                    session_trading_day_ = canonical_trading_day(api_->GetTradingDay());
                }
            }
            login_complete_ = true;
        }
        state_cv_.notify_all();

        if (!error.empty()) {
            std::cerr << "[arbitrage_recorder] CTP MD login failed: " << error << "\n";
            return;
        }

        std::cout << "[arbitrage_recorder] CTP MD login succeeded, trading_day="
                  << (session_trading_day().empty() ? std::string {"<unknown>"} : session_trading_day())
                  << "\n";
    }

    void OnRspSubMarketData(CThostFtdcSpecificInstrumentField* specific_instrument, CThostFtdcRspInfoField* rsp_info, int, bool is_last) override {
        const auto error = rsp_error_message(rsp_info);
        if (!error.empty()) {
            const auto instrument = specific_instrument == nullptr ? std::string {} : ctp_string(specific_instrument->InstrumentID);
            const auto message = instrument.empty()
                ? "SubscribeMarketData failed: " + error
                : "SubscribeMarketData failed for " + instrument + ": " + error;
            set_error(message);
            std::cerr << "[arbitrage_recorder] " << message << "\n";
        }

        if (is_last && error.empty()) {
            ++subscription_ack_batches_;
        }
    }

    void OnRspError(CThostFtdcRspInfoField* rsp_info, int, bool) override {
        const auto error = rsp_error_message(rsp_info);
        if (error.empty()) {
            return;
        }
        set_error(error);
        std::cerr << "[arbitrage_recorder] CTP MD response error: " << error << "\n";
    }

    void OnRtnDepthMarketData(CThostFtdcDepthMarketDataField* depth_market_data) override {
        if (depth_market_data == nullptr) {
            return;
        }

        ++received_ticks_;
        if (ctp_string(depth_market_data->InstrumentID).empty()) {
            ++dropped_invalid_ticks_;
            return;
        }

        RawMarketDataRecord record;
        record.trading_day = trading_day_for_record(*depth_market_data);
        record.depth_market_data = *depth_market_data;

        {
            std::lock_guard lock(queue_mutex_);
            pending_records_.push_back(record);
            observe_queue_depth(pending_records_.size());
        }
        queue_cv_.notify_one();
    }

    RecorderConfig config_;
    std::vector<ArbitrageInstrument> instruments_;
    CThostFtdcMdApi* api_ {nullptr};

    mutable std::mutex state_mutex_;
    std::condition_variable state_cv_;
    int request_id_ {0};
    bool front_connected_ {false};
    bool login_complete_ {false};
    bool ready_ {false};
    std::string last_error_;
    std::string fatal_error_;
    std::string session_trading_day_;
    int reconnect_attempts_ {0};
    long long last_connect_attempt_ms_ {0};

    mutable std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::deque<RawMarketDataRecord> pending_records_;
    bool writer_stop_requested_ {false};
    std::thread writer_thread_;

    std::atomic<std::size_t> received_ticks_ {0};
    std::atomic<std::size_t> written_ticks_ {0};
    std::atomic<std::size_t> dropped_invalid_ticks_ {0};
    std::atomic<std::size_t> max_queue_depth_ {0};
    std::atomic<std::size_t> subscription_ack_batches_ {0};
};

void print_usage() {
    std::cout << "Usage: itrader_arbitrage_recorder [--connect-test] --config <path-to-ini>\n"
              << "If no arguments are supplied, the recorder defaults to configs/arbitrage_recorder.ini\n";
}

int run_recorder(const std::filesystem::path& config_path, bool connect_test) {
    const auto ini = itrader::IniFile::parse(config_path);
    const auto config = read_recorder_config(config_path, ini, connect_test);

    std::cout << "[arbitrage_recorder] starting CTP arbitrage recorder\n"
              << "[arbitrage_recorder] account=" << config.account_id
              << " | discovery_mode=" << config.discovery_mode
              << " | trader_front=" << (config.account.front.empty() ? std::string {"<disabled>"} : config.account.front)
              << " | md_front=" << config.account.md_front
              << " | output_dir=" << config.output_dir.string()
              << " | record_size=" << sizeof(CThostFtdcDepthMarketDataField)
              << "\n";

    if (connect_test) {
        ArbitrageBinaryMarketDataRecorder recorder(config, {});
        std::string error_message;
        if (!recorder.start(&error_message)) {
            throw std::runtime_error("CTP MD connect test failed: " + error_message);
        }
        recorder.print_summary("connect-test");
        recorder.stop();
        std::cout << "[arbitrage_recorder] CTP MD connect test succeeded.\n";
        return 0;
    }

    std::vector<ArbitrageInstrument> instruments;
    std::string error_message;
    if (config.discovery_mode == "trader_query") {
        ArbitrageInstrumentDiscoverer discoverer(config);
        if (!discoverer.connect(&error_message, config.connect_timeout_ms)) {
            throw std::runtime_error("Failed to connect CTP trader front for arbitrage discovery: " + error_message);
        }

        instruments = discoverer.query_all(&error_message);
        discoverer.disconnect();
        if (!error_message.empty()) {
            throw std::runtime_error("Failed to discover arbitrage instruments: " + error_message);
        }
    } else {
        instruments = load_configured_instruments(config);
    }
    if (instruments.empty()) {
        throw std::runtime_error("No arbitrage instruments were configured or discovered.");
    }

    std::cout << "[arbitrage_recorder] loaded total " << instruments.size()
              << " arbitrage instrument(s).\n";

    ArbitrageBinaryMarketDataRecorder recorder(config, std::move(instruments));
    if (!recorder.start(&error_message)) {
        throw std::runtime_error("Failed to start arbitrage market-data recorder: " + error_message);
    }

    if (!error_message.empty()) {
        std::cerr << "[arbitrage_recorder] initial connect pending: " << error_message << "\n";
    }

    recorder.print_summary("started");

    std::string last_connect_error;
    auto last_connect_error_at = Clock::now() - std::chrono::seconds(30);

    while (!g_stop_requested.load()) {
        if (const auto fatal_error = recorder.fatal_error(); !fatal_error.empty()) {
            throw std::runtime_error(fatal_error);
        }

        if (!recorder.ready()) {
            error_message.clear();
            recorder.ensure_ready(&error_message);
            const auto now = Clock::now();
            if (!error_message.empty()
                && (error_message != last_connect_error
                    || (now - last_connect_error_at) >= std::chrono::seconds(10))) {
                std::cerr << "[arbitrage_recorder] reconnect status: " << error_message << "\n";
                last_connect_error = error_message;
                last_connect_error_at = now;
            }
        } else {
            last_connect_error.clear();
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(config.idle_sleep_ms));
    }

    recorder.stop();
    recorder.print_summary("stopped");
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    std::string config_path = "configs/arbitrage_recorder.ini";
    bool connect_test = false;

    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--help" || argument == "-h") {
            print_usage();
            return 0;
        }
        if (argument == "--connect-test") {
            connect_test = true;
            continue;
        }
        if (argument == "--config" && index + 1 < argc) {
            config_path = argv[++index];
            continue;
        }
    }

    try {
        return run_recorder(config_path, connect_test);
    } catch (const std::exception& ex) {
        std::cerr << "[arbitrage_recorder] fatal: " << ex.what() << "\n";
        return 1;
    }
}
