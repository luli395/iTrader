#include "itrader/domain.hpp"
#include "itrader/ctp_md_api_compat.hpp"
#include "itrader/ini.hpp"
#include "itrader/runtime_paths.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cctype>
#include <csignal>
#include <cmath>
#include <cstddef>
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
    std::string product_info {"iTrader"};
    std::filesystem::path flow_dir;
    bool production_mode {true};
    bool reconnect_enabled {true};
    int reconnect_retry_interval_ms {3000};
    int reconnect_max_attempts {0};
};

struct RecorderConfig {
    std::string account_id;
    RecorderAccountConfig account;
    std::filesystem::path output_dir;
    std::vector<std::string> instruments;
    int flush_interval_ms {1000};
    int status_interval_ms {30000};
    int idle_sleep_ms {250};
    int connect_timeout_ms {15000};
    bool deduplicate_exact_ticks {true};
};

struct RecordedTick {
    std::string trading_day;
    std::string instrument;
    std::string exchange;
    std::string timestamp;
    double last {0.0};
    double high {0.0};
    double low {0.0};
    int volume {0};
    double turnover {0.0};
    double open_interest {0.0};
    int ask_size {0};
    double ask {0.0};
    int bid_size {0};
    double bid {0.0};
    double upper_limit_price {0.0};
    double lower_limit_price {0.0};

    [[nodiscard]] std::string symbol_key() const {
        std::string key = instrument;
        for (char& ch : key) {
            ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        }
        key.push_back('.');
        for (const char ch : exchange) {
            key.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
        }
        return key;
    }

    [[nodiscard]] bool same_payload(const RecordedTick& other) const {
        return trading_day == other.trading_day
            && instrument == other.instrument
            && exchange == other.exchange
            && timestamp == other.timestamp
            && last == other.last
            && high == other.high
            && low == other.low
            && volume == other.volume
            && turnover == other.turnover
            && open_interest == other.open_interest
            && ask_size == other.ask_size
            && ask == other.ask
            && bid_size == other.bid_size
            && bid == other.bid
            && upper_limit_price == other.upper_limit_price
            && lower_limit_price == other.lower_limit_price;
    }
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

bool is_valid_market_value(double value) {
    return std::isfinite(value) && std::abs(value) < 1.0e20;
}

double sanitize_market_value(double value) {
    return is_valid_market_value(value) ? value : 0.0;
}

std::string format_ctp_timestamp(std::string_view action_day, std::string_view update_time, int update_millisec) {
    const auto day = canonical_trading_day(action_day);
    const auto time = itrader::trim_copy(update_time);
    if (day.empty() || time.empty()) {
        return {};
    }

    std::ostringstream output;
    output << day << ' ' << time << '.' << std::setw(3) << std::setfill('0')
           << std::clamp(update_millisec, 0, 999);
    return output.str();
}

std::string local_date_label() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t raw_time = std::chrono::system_clock::to_time_t(now);
    std::tm local_time {};
    localtime_s(&local_time, &raw_time);

    std::ostringstream output;
    output << std::put_time(&local_time, "%Y%m%d");
    return output.str();
}

std::optional<std::tm> parse_date_label(std::string_view raw_day) {
    const auto day = canonical_trading_day(raw_day);
    if (day.size() != 8) {
        return std::nullopt;
    }

    std::tm parsed {};
    std::istringstream input {day};
    input >> std::get_time(&parsed, "%Y%m%d");
    if (input.fail()) {
        return std::nullopt;
    }
    parsed.tm_hour = 0;
    parsed.tm_min = 0;
    parsed.tm_sec = 0;
    parsed.tm_isdst = -1;
    if (std::mktime(&parsed) == static_cast<std::time_t>(-1)) {
        return std::nullopt;
    }
    return parsed;
}

std::string format_date_label(const std::tm& parsed) {
    std::ostringstream output;
    output << std::put_time(&parsed, "%Y%m%d");
    return output.str();
}

std::string shift_date_label(std::string_view raw_day, int days) {
    auto parsed = parse_date_label(raw_day);
    if (!parsed.has_value()) {
        return canonical_trading_day(raw_day);
    }

    parsed->tm_mday += days;
    parsed->tm_isdst = -1;
    if (std::mktime(&*parsed) == static_cast<std::time_t>(-1)) {
        return canonical_trading_day(raw_day);
    }
    return format_date_label(*parsed);
}

std::string next_weekday_label(std::string_view raw_day) {
    auto day = shift_date_label(raw_day, 1);
    for (int guard = 0; guard < 7; ++guard) {
        auto parsed = parse_date_label(day);
        if (!parsed.has_value()) {
            return day;
        }
        if (parsed->tm_wday >= 1 && parsed->tm_wday <= 5) {
            return day;
        }
        day = shift_date_label(day, 1);
    }
    return day;
}

std::string current_or_next_weekday_label(std::string_view raw_day) {
    auto day = canonical_trading_day(raw_day);
    for (int guard = 0; guard < 7; ++guard) {
        auto parsed = parse_date_label(day);
        if (!parsed.has_value()) {
            return day;
        }
        if (parsed->tm_wday >= 1 && parsed->tm_wday <= 5) {
            return day;
        }
        day = shift_date_label(day, 1);
    }
    return day;
}

std::string timestamp_date_label(std::string_view timestamp) {
    const auto trimmed = itrader::trim_copy(timestamp);
    if (trimmed.size() >= 8
        && std::all_of(trimmed.begin(), trimmed.begin() + 8, [](unsigned char ch) { return std::isdigit(ch) != 0; })) {
        return std::string(trimmed.substr(0, 8));
    }
    if (trimmed.size() >= 10
        && std::isdigit(static_cast<unsigned char>(trimmed[0])) != 0
        && std::isdigit(static_cast<unsigned char>(trimmed[1])) != 0
        && std::isdigit(static_cast<unsigned char>(trimmed[2])) != 0
        && std::isdigit(static_cast<unsigned char>(trimmed[3])) != 0
        && trimmed[4] == '-'
        && std::isdigit(static_cast<unsigned char>(trimmed[5])) != 0
        && std::isdigit(static_cast<unsigned char>(trimmed[6])) != 0
        && trimmed[7] == '-'
        && std::isdigit(static_cast<unsigned char>(trimmed[8])) != 0
        && std::isdigit(static_cast<unsigned char>(trimmed[9])) != 0) {
        std::string day;
        day.reserve(8);
        day.append(trimmed.substr(0, 4));
        day.append(trimmed.substr(5, 2));
        day.append(trimmed.substr(8, 2));
        return day;
    }
    return {};
}

std::optional<std::time_t> parse_recorder_timestamp_to_local_time(std::string_view timestamp) {
    const auto trimmed = itrader::trim_copy(timestamp);
    if (trimmed.size() < 17) {
        return std::nullopt;
    }

    std::tm parsed {};
    parsed.tm_isdst = -1;
    std::istringstream input {std::string(trimmed.substr(0, 17))};
    input >> std::get_time(&parsed, "%Y%m%d %H:%M:%S");
    if (input.fail()) {
        return std::nullopt;
    }

    const auto value = std::mktime(&parsed);
    if (value == static_cast<std::time_t>(-1)) {
        return std::nullopt;
    }
    return value;
}

bool timestamp_is_future(std::string_view timestamp, int tolerance_seconds = 120) {
    const auto parsed = parse_recorder_timestamp_to_local_time(timestamp);
    if (!parsed.has_value()) {
        return false;
    }

    const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    return *parsed > now + tolerance_seconds;
}

std::string instrument_alpha_prefix(std::string_view instrument) {
    std::string prefix;
    for (const unsigned char ch : instrument) {
        if (std::isalpha(ch) == 0) {
            break;
        }
        prefix.push_back(static_cast<char>(std::toupper(ch)));
    }
    return prefix;
}

int hhmmss_from_timestamp_label(std::string_view timestamp) {
    if (timestamp.size() < 17) {
        return -1;
    }

    const auto digit_at = [timestamp](std::size_t index) -> int {
        if (index >= timestamp.size()) {
            return -1;
        }
        const unsigned char ch = static_cast<unsigned char>(timestamp[index]);
        return std::isdigit(ch) == 0 ? -1 : (ch - '0');
    };

    const int hour_tens = digit_at(9);
    const int hour_ones = digit_at(10);
    const int minute_tens = digit_at(12);
    const int minute_ones = digit_at(13);
    const int second_tens = digit_at(15);
    const int second_ones = digit_at(16);
    if (hour_tens < 0 || hour_ones < 0 || minute_tens < 0 || minute_ones < 0 || second_tens < 0 || second_ones < 0) {
        return -1;
    }

    return (hour_tens * 10 + hour_ones) * 10000
        + (minute_tens * 10 + minute_ones) * 100
        + (second_tens * 10 + second_ones);
}

bool is_stock_index_future(std::string_view instrument, std::string_view exchange) {
    if (upper_copy(exchange) != "CFFEX") {
        return false;
    }

    const auto prefix = instrument_alpha_prefix(instrument);
    return prefix == "IF" || prefix == "IH" || prefix == "IC" || prefix == "IM";
}

bool is_treasury_future(std::string_view instrument, std::string_view exchange) {
    if (upper_copy(exchange) != "CFFEX") {
        return false;
    }

    const auto prefix = instrument_alpha_prefix(instrument);
    return prefix == "TS" || prefix == "TF" || prefix == "T" || prefix == "TL";
}

bool is_commodity_futures_exchange(std::string_view exchange) {
    const auto normalized_exchange = upper_copy(exchange);
    return normalized_exchange == "SHFE"
        || normalized_exchange == "DCE"
        || normalized_exchange == "CZCE"
        || normalized_exchange == "INE"
        || normalized_exchange == "GFEX";
}

bool prefix_matches_any(std::string_view prefix, std::initializer_list<std::string_view> candidates) {
    return std::find(candidates.begin(), candidates.end(), prefix) != candidates.end();
}

bool is_time_in_inclusive_window(int hhmmss, int start_hhmmss, int end_hhmmss) {
    return hhmmss >= start_hhmmss && hhmmss <= end_hhmmss;
}

bool is_time_in_trading_window(int hhmmss, int start_hhmmss, int end_hhmmss) {
    if (start_hhmmss <= end_hhmmss) {
        return is_time_in_inclusive_window(hhmmss, start_hhmmss, end_hhmmss);
    }
    return hhmmss >= start_hhmmss || hhmmss <= end_hhmmss;
}

enum class CommodityNightSession {
    None,
    EndsAt2300,
    EndsAt0100,
    EndsAt0230,
};

CommodityNightSession commodity_night_session(std::string_view instrument, std::string_view exchange) {
    const auto normalized_exchange = upper_copy(exchange);
    const auto prefix = instrument_alpha_prefix(instrument);

    if (normalized_exchange == "SHFE") {
        if (prefix_matches_any(prefix, {"AU", "AG"})) {
            return CommodityNightSession::EndsAt0230;
        }
        if (prefix_matches_any(prefix, {"CU", "AL", "ZN", "PB", "NI", "SN", "SS"})) {
            return CommodityNightSession::EndsAt0100;
        }
        if (prefix_matches_any(prefix, {"RB", "HC", "FU", "BU", "RU", "BR", "SP"})) {
            return CommodityNightSession::EndsAt2300;
        }
        return CommodityNightSession::None;
    }

    if (normalized_exchange == "INE") {
        if (prefix_matches_any(prefix, {"SC"})) {
            return CommodityNightSession::EndsAt0230;
        }
        if (prefix_matches_any(prefix, {"BC"})) {
            return CommodityNightSession::EndsAt0100;
        }
        if (prefix_matches_any(prefix, {"NR", "LU"})) {
            return CommodityNightSession::EndsAt2300;
        }
        return CommodityNightSession::None;
    }

    if (normalized_exchange == "DCE") {
        if (prefix_matches_any(prefix, {"A", "B", "M", "Y", "P", "C", "CS", "JD", "L", "V", "PP", "J", "JM", "I", "EG", "EB", "PG", "RR", "LH"})) {
            return CommodityNightSession::EndsAt2300;
        }
        return CommodityNightSession::None;
    }

    if (normalized_exchange == "CZCE") {
        if (prefix_matches_any(prefix, {"CF", "CY", "SR", "TA", "MA", "FG", "RM", "ZC", "OI", "SA", "PF", "PX", "SH", "SM", "SF"})) {
            return CommodityNightSession::EndsAt2300;
        }
        return CommodityNightSession::None;
    }

    if (normalized_exchange == "GFEX") {
        return CommodityNightSession::None;
    }

    return CommodityNightSession::None;
}

bool is_commodity_day_session_time(int hhmmss) {
    return is_time_in_inclusive_window(hhmmss, 85500, 101500)
        || is_time_in_inclusive_window(hhmmss, 103000, 113000)
        || is_time_in_inclusive_window(hhmmss, 133000, 150000);
}

bool is_commodity_night_session_time(int hhmmss, CommodityNightSession session) {
    switch (session) {
    case CommodityNightSession::EndsAt2300:
        return is_time_in_inclusive_window(hhmmss, 205500, 230000);
    case CommodityNightSession::EndsAt0100:
        return is_time_in_trading_window(hhmmss, 205500, 10000);
    case CommodityNightSession::EndsAt0230:
        return is_time_in_trading_window(hhmmss, 205500, 23000);
    case CommodityNightSession::None:
    default:
        return false;
    }
}

bool is_stock_index_session_time(int hhmmss) {
    return is_time_in_inclusive_window(hhmmss, 92500, 113000)
        || is_time_in_inclusive_window(hhmmss, 130000, 150000);
}

bool is_treasury_session_time(int hhmmss) {
    return is_time_in_inclusive_window(hhmmss, 92500, 113000)
        || is_time_in_inclusive_window(hhmmss, 130000, 151500);
}

bool should_record_tick_in_trading_session(const RecordedTick& tick) {
    const int hhmmss = hhmmss_from_timestamp_label(tick.timestamp);
    if (hhmmss < 0) {
        return false;
    }

    if (is_treasury_future(tick.instrument, tick.exchange)) {
        return is_treasury_session_time(hhmmss);
    }

    if (is_stock_index_future(tick.instrument, tick.exchange)) {
        return is_stock_index_session_time(hhmmss);
    }

    if (is_commodity_futures_exchange(tick.exchange)) {
        return is_commodity_day_session_time(hhmmss)
            || is_commodity_night_session_time(hhmmss, commodity_night_session(tick.instrument, tick.exchange));
    }

    return true;
}

bool is_midnight_night_session_time(int hhmmss, CommodityNightSession session) {
    switch (session) {
    case CommodityNightSession::EndsAt0100:
        return hhmmss >= 0 && hhmmss <= 10000;
    case CommodityNightSession::EndsAt0230:
        return hhmmss >= 0 && hhmmss <= 23000;
    case CommodityNightSession::EndsAt2300:
    case CommodityNightSession::None:
    default:
        return false;
    }
}

std::string infer_recording_trading_day(
    std::string_view raw_trading_day,
    std::string_view instrument,
    std::string_view exchange,
    std::string_view timestamp) {

    const auto fallback_trading_day = canonical_trading_day(raw_trading_day);
    const auto prefer_later_trading_day = [&fallback_trading_day](const std::string& inferred) {
        if (fallback_trading_day.size() == 8 && (inferred.empty() || fallback_trading_day > inferred)) {
            return fallback_trading_day;
        }
        return inferred;
    };
    const auto action_day = timestamp_date_label(timestamp);
    const int hhmmss = hhmmss_from_timestamp_label(timestamp);
    if (action_day.empty() || hhmmss < 0) {
        return fallback_trading_day;
    }

    if (is_commodity_futures_exchange(exchange)) {
        const auto session = commodity_night_session(instrument, exchange);
        if (session != CommodityNightSession::None) {
            if (hhmmss >= 205500) {
                return prefer_later_trading_day(next_weekday_label(action_day));
            }
            if (is_midnight_night_session_time(hhmmss, session)) {
                return prefer_later_trading_day(current_or_next_weekday_label(action_day));
            }
        }
    }

    return fallback_trading_day.empty() ? action_day : fallback_trading_day;
}

bool looks_like_retryable_recorder_connect_error(std::string_view error_message) {
    const auto normalized = upper_copy(itrader::trim_copy(error_message));
    return normalized.find("TIMED OUT WAITING FOR CTP MD FRONT CONNECTION") != std::string::npos
        || normalized.find("TIMED OUT WAITING FOR CTP MD LOGIN") != std::string::npos
        || normalized.find("CTP MD FRONT DISCONNECTED") != std::string::npos;
}

void copy_trunc(char* destination, std::size_t destination_size, const std::string& value) {
    if (destination == nullptr || destination_size == 0) {
        return;
    }

    const auto copy_size = std::min(destination_size - 1, value.size());
    std::fill(destination, destination + destination_size, '\0');
    std::copy_n(value.begin(), static_cast<std::ptrdiff_t>(copy_size), destination);
}

std::vector<std::string> normalize_instruments(const std::vector<std::string>& raw_instruments) {
    std::vector<std::string> normalized;
    normalized.reserve(raw_instruments.size());
    for (const auto& raw_instrument : raw_instruments) {
        const auto instrument = itrader::trim_copy(raw_instrument);
        if (instrument.empty()) {
            continue;
        }
        if (std::find(normalized.begin(), normalized.end(), instrument) == normalized.end()) {
            normalized.push_back(instrument);
        }
    }
    return normalized;
}

RecorderAccountConfig read_account_config(const itrader::IniFile& ini, const std::string& section, const std::filesystem::path& config_path) {
    RecorderAccountConfig config;
    const auto account_id = section.substr(section.find('.') + 1);
    const auto base_dir = config_path.parent_path();

    const auto fallback_front = ini.get(section, "front");
    const auto fallback_broker_id = ini.get(section, "broker_id");
    const auto fallback_user_id = ini.get(section, "user_id");
    const auto fallback_password = ini.get(section, "password");

    config.front = normalize_front_address(ini.get(section, "md_front", fallback_front));
    config.broker_id = ini.get(section, "md_broker_id", fallback_broker_id);
    config.user_id = ini.get(section, "md_user_id", fallback_user_id);
    config.password = ini.get(section, "md_password", fallback_password);
    config.product_info = ini.get(section, "product_info", "iTrader");
    config.flow_dir = resolve_path(
        base_dir,
        ini.get(section, "md_flow_dir", itrader::default_ctp_md_flow_dir(config_path, account_id).generic_string()));
    config.production_mode = ini.get_bool(section, "production_mode", true);
    config.reconnect_enabled = ini.get_bool(section, "reconnect_enabled", true);
    config.reconnect_retry_interval_ms = std::max(0, ini.get_int(section, "reconnect_retry_interval_ms", 3000));
    config.reconnect_max_attempts = std::max(0, ini.get_int(section, "reconnect_max_attempts", 0));
    return config;
}

RecorderConfig read_recorder_config(const std::filesystem::path& config_path, const itrader::IniFile& ini) {
    RecorderConfig config;
    const auto account_sections = ini.sections_with_prefix("account.");
    if (account_sections.empty()) {
        throw std::runtime_error("Recorder config needs exactly one [account.*] section.");
    }
    if (account_sections.size() > 1) {
        throw std::runtime_error("Recorder currently supports exactly one [account.*] section.");
    }

    config.account_id = account_sections.front().substr(account_sections.front().find('.') + 1);
    config.account = read_account_config(ini, account_sections.front(), config_path);
    config.instruments = normalize_instruments(ini.get_list("recorder", "instruments"));
    config.output_dir = resolve_path(
        config_path.parent_path(),
        ini.get("recorder", "output_dir", (itrader::runtime_namespace_directory(config_path) / "agtick").generic_string()));
    config.flush_interval_ms = std::max(0, ini.get_int("recorder", "flush_interval_ms", 1000));
    config.status_interval_ms = std::max(0, ini.get_int("recorder", "status_interval_ms", 30000));
    config.idle_sleep_ms = std::max(50, ini.get_int("recorder", "idle_sleep_ms", 250));
    config.connect_timeout_ms = std::max(1000, ini.get_int("recorder", "connect_timeout_ms", 15000));
    config.deduplicate_exact_ticks = ini.get_bool("recorder", "deduplicate_exact_ticks", true);

    if (config.account.front.empty()) {
        throw std::runtime_error("Recorder account must set md_front (or front).");
    }
    if (config.account.broker_id.empty()) {
        throw std::runtime_error("Recorder account must set md_broker_id (or broker_id).");
    }
    if (config.account.user_id.empty()) {
        throw std::runtime_error("Recorder account must set md_user_id (or user_id).");
    }
    if (config.account.password.empty()) {
        throw std::runtime_error("Recorder account must set md_password (or password).");
    }
    if (config.instruments.empty()) {
        throw std::runtime_error("[recorder] must set instruments=instrument_a,instrument_b,...");
    }
    if (config.output_dir.empty()) {
        throw std::runtime_error("[recorder] output_dir resolved to an empty path.");
    }

    return config;
}

class CtpMdCsvRecorder final : private CThostFtdcMdSpi {
public:
    explicit CtpMdCsvRecorder(RecorderConfig config)
        : config_(std::move(config)) {}

    ~CtpMdCsvRecorder() {
        stop();
    }

    bool start(std::string* error_message) {
        std::error_code error_code;
        std::filesystem::create_directories(config_.output_dir, error_code);
        if (error_code) {
            if (error_message != nullptr) {
                *error_message = "Unable to create recorder output directory: " + config_.output_dir.string();
            }
            return false;
        }

        std::filesystem::create_directories(config_.account.flow_dir, error_code);
        if (error_code) {
            if (error_message != nullptr) {
                *error_message = "Unable to create CTP MD flow directory: " + config_.account.flow_dir.string();
            }
            return false;
        }

        {
            std::lock_guard lock(queue_mutex_);
            writer_stop_requested_ = false;
            pending_rows_.clear();
            last_enqueued_by_symbol_.clear();
        }

        writer_thread_ = std::thread(&CtpMdCsvRecorder::writer_loop, this);
        const bool success = connect(error_message, config_.connect_timeout_ms);
        const auto startup_error = error_message == nullptr ? std::string {} : *error_message;
        if (!success) {
            if (config_.account.reconnect_enabled && looks_like_retryable_recorder_connect_error(startup_error)) {
                disconnect();
                return true;
            }
            stop();
        }
        return success || (config_.account.reconnect_enabled && looks_like_retryable_recorder_connect_error(startup_error));
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
        std::size_t pending_rows = 0;
        {
            std::lock_guard lock(queue_mutex_);
            pending_rows = pending_rows_.size();
        }

        const bool recorder_ready = ready();
        const auto warning = recorder_ready ? std::string {} : last_error();

        std::cout
            << "[recorder] " << label
            << " | ready=" << (recorder_ready ? "yes" : "no")
            << " | received=" << received_ticks_.load()
            << " | written=" << written_ticks_.load()
            << " | dropped_invalid=" << dropped_invalid_ticks_.load()
            << " | dropped_out_of_session=" << dropped_out_of_session_ticks_.load()
            << " | deduplicated=" << deduplicated_ticks_.load()
            << " | max_queue_depth=" << max_queue_depth_.load()
            << " | pending=" << pending_rows;
        if (!warning.empty()) {
            std::cout << " | warning=" << warning;
        }
        std::cout << "\n";
    }

private:
    struct FileShard {
        std::string trading_day;
        std::filesystem::path path;
        std::ofstream stream;
    };

    bool connect(std::string* error_message, int timeout_ms) {
        disconnect();

        const auto flow_dir = config_.account.flow_dir.string();
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
        std::vector<char> front(config_.account.front.begin(), config_.account.front.end());
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
        copy_trunc(login_request.BrokerID, sizeof(login_request.BrokerID), config_.account.broker_id);
        copy_trunc(login_request.UserID, sizeof(login_request.UserID), config_.account.user_id);
        copy_trunc(login_request.Password, sizeof(login_request.Password), config_.account.password);
        copy_trunc(login_request.UserProductInfo, sizeof(login_request.UserProductInfo), config_.account.product_info);
        copy_trunc(login_request.InterfaceProductInfo, sizeof(login_request.InterfaceProductInfo), "iTraderMDRecorder");
        api_->ReqUserLogin(&login_request, next_request_id());

        const bool login_success = wait_until([this] { return login_complete_; }, timeout_ms) && ready();
        if (!login_success) {
            if (error_message != nullptr) {
                std::lock_guard lock(state_mutex_);
                *error_message = last_error_.empty() ? "Timed out waiting for CTP MD login." : last_error_;
            }
            return false;
        }

        if (!subscribe_configured_instruments(error_message)) {
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

    bool subscribe_configured_instruments(std::string* error_message) {
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

        auto instrument_copies = config_.instruments;
        std::vector<char*> instrument_ptrs;
        instrument_ptrs.reserve(instrument_copies.size());
        for (auto& instrument : instrument_copies) {
            instrument_ptrs.push_back(instrument.data());
        }

        const int return_code = api->SubscribeMarketData(instrument_ptrs.data(), static_cast<int>(instrument_ptrs.size()));
        if (return_code != 0) {
            if (error_message != nullptr) {
                *error_message = "SubscribeMarketData returned " + std::to_string(return_code) + ".";
            }
            return false;
        }
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

    [[nodiscard]] std::optional<RecordedTick> make_recorded_tick(const CThostFtdcDepthMarketDataField& depth_market_data) const {
        RecordedTick tick;
        tick.instrument = itrader::trim_copy(depth_market_data.InstrumentID);
        tick.exchange = normalize_exchange_code(depth_market_data.ExchangeID);
        auto ctp_trading_day = canonical_trading_day(depth_market_data.TradingDay);
        if (ctp_trading_day.empty()) {
            ctp_trading_day = session_trading_day();
        }

        auto action_day = canonical_trading_day(depth_market_data.ActionDay);
        if (action_day.empty()) {
            action_day = local_date_label();
        }
        tick.timestamp = format_ctp_timestamp(action_day, depth_market_data.UpdateTime, depth_market_data.UpdateMillisec);
        if (timestamp_is_future(tick.timestamp)) {
            return std::nullopt;
        }
        tick.trading_day = infer_recording_trading_day(ctp_trading_day, tick.instrument, tick.exchange, tick.timestamp);
        tick.last = sanitize_market_value(depth_market_data.LastPrice);
        tick.high = sanitize_market_value(depth_market_data.HighestPrice);
        tick.low = sanitize_market_value(depth_market_data.LowestPrice);
        if (tick.high <= 0.0) {
            tick.high = tick.last;
        }
        if (tick.low <= 0.0) {
            tick.low = tick.last;
        }
        tick.volume = std::max(0, static_cast<int>(depth_market_data.Volume));
        tick.turnover = sanitize_market_value(depth_market_data.Turnover);
        tick.open_interest = sanitize_market_value(depth_market_data.OpenInterest);
        tick.ask_size = std::max(0, static_cast<int>(depth_market_data.AskVolume1));
        tick.ask = sanitize_market_value(depth_market_data.AskPrice1);
        tick.bid_size = std::max(0, static_cast<int>(depth_market_data.BidVolume1));
        tick.bid = sanitize_market_value(depth_market_data.BidPrice1);
        tick.upper_limit_price = sanitize_market_value(depth_market_data.UpperLimitPrice);
        tick.lower_limit_price = sanitize_market_value(depth_market_data.LowerLimitPrice);

        if (tick.instrument.empty()
            || tick.exchange.empty()
            || tick.trading_day.empty()
            || tick.timestamp.empty()
            || tick.last <= 0.0
            || tick.ask <= 0.0
            || tick.bid <= 0.0
            || tick.ask_size <= 0
            || tick.bid_size <= 0) {
            return std::nullopt;
        }

        return tick;
    }

    void observe_queue_depth(std::size_t depth) {
        auto current = max_queue_depth_.load();
        while (depth > current && !max_queue_depth_.compare_exchange_weak(current, depth)) {
        }
    }

    std::filesystem::path output_path_for_tick(const RecordedTick& tick) const {
        return config_.output_dir / (tick.trading_day + "_" + tick.instrument + "." + tick.exchange + ".csv");
    }

    void open_shard(FileShard& shard, const RecordedTick& tick) {
        shard.trading_day = tick.trading_day;
        shard.path = output_path_for_tick(tick);

        std::error_code error_code;
        std::filesystem::create_directories(shard.path.parent_path(), error_code);
        if (error_code) {
            throw std::runtime_error("Unable to create recorder shard directory: " + shard.path.parent_path().string());
        }

        error_code.clear();
        const bool write_header = !std::filesystem::exists(shard.path)
            || std::filesystem::file_size(shard.path, error_code) == 0;
        if (error_code) {
            throw std::runtime_error("Unable to stat recorder shard file: " + shard.path.string());
        }

        shard.stream.open(shard.path, std::ios::out | std::ios::app);
        if (!shard.stream.is_open()) {
            throw std::runtime_error("Unable to open recorder shard file: " + shard.path.string());
        }

        shard.stream << std::setprecision(12);
        if (write_header) {
            shard.stream << "time,symbol,current,high,low,volume,money,position,a1_v,a1_p,b1_v,b1_p,upper_limit_price,lower_limit_price\n";
        }
    }

    void write_tick(std::map<std::string, FileShard>& shards, const RecordedTick& tick) {
        auto shard_it = shards.find(tick.symbol_key());
        if (shard_it == shards.end()) {
            FileShard shard;
            open_shard(shard, tick);
            shard_it = shards.emplace(tick.symbol_key(), std::move(shard)).first;
        } else if (shard_it->second.trading_day != tick.trading_day) {
            shard_it->second.stream.flush();
            shard_it->second.stream.close();
            open_shard(shard_it->second, tick);
        }

        auto& stream = shard_it->second.stream;
        stream
            << tick.timestamp << ','
            << tick.instrument << '.' << tick.exchange << ','
            << tick.last << ','
            << tick.high << ','
            << tick.low << ','
            << tick.volume << ','
            << tick.turnover << ','
            << tick.open_interest << ','
            << tick.ask_size << ','
            << tick.ask << ','
            << tick.bid_size << ','
                << tick.bid << ','
                << tick.upper_limit_price << ','
                << tick.lower_limit_price << '\n';
    }

    static void flush_all(std::map<std::string, FileShard>& shards) {
        for (auto& [_, shard] : shards) {
            if (shard.stream.is_open()) {
                shard.stream.flush();
            }
        }
    }

    static void close_all(std::map<std::string, FileShard>& shards) {
        for (auto& [_, shard] : shards) {
            if (shard.stream.is_open()) {
                shard.stream.flush();
                shard.stream.close();
            }
        }
    }

    void writer_loop() {
        try {
            std::map<std::string, FileShard> shards;
            const auto flush_interval = std::chrono::milliseconds(std::max(250, config_.flush_interval_ms));
            const auto status_interval = std::chrono::milliseconds(std::max(1000, config_.status_interval_ms));
            auto next_flush_at = Clock::now() + flush_interval;
            auto next_status_at = Clock::now() + status_interval;

            while (true) {
                std::deque<RecordedTick> batch;
                bool should_stop = false;
                {
                    std::unique_lock lock(queue_mutex_);
                    queue_cv_.wait_for(lock, std::chrono::milliseconds(config_.idle_sleep_ms), [this] {
                        return writer_stop_requested_ || !pending_rows_.empty();
                    });

                    if (!pending_rows_.empty()) {
                        batch.swap(pending_rows_);
                    }
                    should_stop = writer_stop_requested_ && pending_rows_.empty();
                }

                for (const auto& tick : batch) {
                    write_tick(shards, tick);
                    ++written_ticks_;
                }

                const auto now = Clock::now();
                if (config_.flush_interval_ms == 0 || now >= next_flush_at || should_stop) {
                    flush_all(shards);
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

            flush_all(shards);
            close_all(shards);
        } catch (const std::exception& ex) {
            set_fatal_error(std::string("Recorder writer thread failed: ") + ex.what());
            g_stop_requested = true;
        }
    }

    void OnFrontConnected() override {
        {
            std::lock_guard lock(state_mutex_);
            front_connected_ = true;
        }
        state_cv_.notify_all();
        std::cout << "[recorder] CTP MD front connected.\n";
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
        std::cerr << "[recorder] disconnected from CTP MD front, reason=" << reason << "\n";
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
                    session_trading_day_ = canonical_trading_day(login_response->TradingDay);
                }
                if (session_trading_day_.empty() && api_ != nullptr) {
                    session_trading_day_ = canonical_trading_day(api_->GetTradingDay());
                }
            }
            login_complete_ = true;
        }
        state_cv_.notify_all();

        if (!error.empty()) {
            std::cerr << "[recorder] CTP MD login failed: " << error << "\n";
            return;
        }

        std::cout << "[recorder] CTP MD login succeeded, trading_day="
                  << (session_trading_day().empty() ? std::string {"<unknown>"} : session_trading_day())
                  << "\n";
    }

    void OnRspSubMarketData(CThostFtdcSpecificInstrumentField* specific_instrument, CThostFtdcRspInfoField* rsp_info, int, bool is_last) override {
        const auto error = rsp_error_message(rsp_info);
        if (!error.empty()) {
            const auto instrument = specific_instrument == nullptr ? std::string {} : itrader::trim_copy(specific_instrument->InstrumentID);
            const auto message = instrument.empty()
                ? "SubscribeMarketData failed: " + error
                : "SubscribeMarketData failed for " + instrument + ": " + error;
            set_error(message);
            std::cerr << "[recorder] " << message << "\n";
        }

        if (is_last && error.empty()) {
            std::cout << "[recorder] market-data subscription request accepted for "
                      << config_.instruments.size() << " instrument(s).\n";
        }
    }

    void OnRspError(CThostFtdcRspInfoField* rsp_info, int, bool) override {
        const auto error = rsp_error_message(rsp_info);
        if (error.empty()) {
            return;
        }
        set_error(error);
        std::cerr << "[recorder] CTP MD response error: " << error << "\n";
    }

    void OnRtnDepthMarketData(CThostFtdcDepthMarketDataField* depth_market_data) override {
        if (depth_market_data == nullptr) {
            return;
        }

        ++received_ticks_;
        const auto tick = make_recorded_tick(*depth_market_data);
        if (!tick.has_value()) {
            ++dropped_invalid_ticks_;
            return;
        }

        if (!should_record_tick_in_trading_session(*tick)) {
            ++dropped_out_of_session_ticks_;
            return;
        }

        {
            std::lock_guard lock(queue_mutex_);
            if (config_.deduplicate_exact_ticks) {
                const auto dedupe_key = tick->symbol_key();
                const auto last_it = last_enqueued_by_symbol_.find(dedupe_key);
                if (last_it != last_enqueued_by_symbol_.end() && last_it->second.same_payload(*tick)) {
                    ++deduplicated_ticks_;
                    return;
                }
                last_enqueued_by_symbol_[dedupe_key] = *tick;
            }

            pending_rows_.push_back(*tick);
            observe_queue_depth(pending_rows_.size());
        }
        queue_cv_.notify_one();
    }

    RecorderConfig config_;
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
    std::deque<RecordedTick> pending_rows_;
    std::unordered_map<std::string, RecordedTick> last_enqueued_by_symbol_;
    bool writer_stop_requested_ {false};
    std::thread writer_thread_;

    std::atomic<std::size_t> received_ticks_ {0};
    std::atomic<std::size_t> written_ticks_ {0};
    std::atomic<std::size_t> dropped_invalid_ticks_ {0};
    std::atomic<std::size_t> dropped_out_of_session_ticks_ {0};
    std::atomic<std::size_t> deduplicated_ticks_ {0};
    std::atomic<std::size_t> max_queue_depth_ {0};
};

void print_usage() {
    std::cout << "Usage: itrader_ctp_md_recorder --config <path-to-ini>\n"
              << "If no arguments are supplied, the recorder defaults to configs/ctp_md_recorder.ini\n";
}

int run_recorder(const std::filesystem::path& config_path) {
    const auto ini = itrader::IniFile::parse(config_path);
    const auto config = read_recorder_config(config_path, ini);

    std::cout << "[recorder] starting CTP MD recorder\n"
              << "[recorder] account=" << config.account_id
              << " | front=" << config.account.front
              << " | output_dir=" << config.output_dir.string()
              << " | instruments=" << config.instruments.size()
              << "\n";

    CtpMdCsvRecorder recorder(config);
    std::string error_message;
    if (!recorder.start(&error_message)) {
        throw std::runtime_error("Failed to start CTP MD recorder: " + error_message);
    }

    if (!error_message.empty()) {
        std::cerr << "[recorder] initial connect pending: " << error_message << "\n";
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
                std::cerr << "[recorder] reconnect status: " << error_message << "\n";
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
    std::string config_path = "configs/ctp_md_recorder.ini";

    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--help" || argument == "-h") {
            print_usage();
            return 0;
        }
        if (argument == "--config" && index + 1 < argc) {
            config_path = argv[++index];
            continue;
        }
    }

    try {
        return run_recorder(config_path);
    } catch (const std::exception& ex) {
        std::cerr << "[recorder] fatal: " << ex.what() << "\n";
        return 1;
    }
}
