#include "itrader/ini.hpp"
#include "itrader/runtime_snapshot.hpp"
#include "itrader/runtime_paths.hpp"
#include "itrader/strategy_api.hpp"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <tlhelp32.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <wbemidl.h>

#if defined(_MSC_VER)
#pragma comment(lib, "wbemuuid.lib")
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <ctime>
#include <cwctype>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <map>
#include <mutex>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

struct ServerOptions {
    std::string host {"127.0.0.1"};
    unsigned short port {8080};
    std::optional<std::filesystem::path> root_override;
};

struct HttpResponse {
    std::string status {"200 OK"};
    std::string content_type {"text/plain; charset=utf-8"};
    std::string body;
    std::vector<std::pair<std::string, std::string>> headers;
};

struct HttpRequest {
    std::string method;
    std::string target;
    std::string version;
    std::string body;
    std::map<std::string, std::string> headers;
};

struct BacktestReplayJob {
    std::string id;
    std::string requested_config;
    std::string detail_level {"full"};
    std::atomic_bool cancel_requested {false};
    std::size_t processed_files {0};
    std::size_t total_files {0};
    std::size_t processed_ticks {0};
    long long started_at_ms {0};
    long long finished_at_ms {0};
    std::string status {"queued"};
    std::string phase {"queued"};
    std::string state_json;
    std::string error_message;
    mutable std::mutex mutex;
};

struct LiveRuntimeProcess {
    HANDLE process_handle {nullptr};
    DWORD process_id {0};
    std::filesystem::path executable_path;
    std::filesystem::path config_path;
    std::filesystem::path requested_config_path;
    std::filesystem::path log_path;
    std::wstring command_line;
    std::vector<std::string> strategy_ids;
    std::string status {"stopped"};
    std::string managed_by {"dashboard"};
    std::string controller_name;
    std::string message;
    long long started_at_ms {0};
    long long finished_at_ms {0};
    DWORD exit_code {0};
    bool stop_requested {false};
    bool auto_restart_enabled {false};
    int auto_restart_count {0};
    long long last_auto_restart_at_ms {0};
};

using LiveRuntimeProcessMap = std::map<std::string, LiveRuntimeProcess>;

std::mutex g_backtest_job_mutex;
std::shared_ptr<BacktestReplayJob> g_backtest_job;
std::atomic_ullong g_backtest_job_counter {0};
std::mutex g_live_runtime_mutex;
LiveRuntimeProcessMap g_live_runtime_processes;
std::mutex g_recorder_runtime_mutex;
LiveRuntimeProcess g_recorder_runtime_process;

std::string make_live_runtime_json(
    const std::filesystem::path& requested_config_path,
    const std::optional<bool>& ok = std::nullopt,
    std::string_view message_override = {},
    const std::filesystem::path* workspace_root = nullptr);

void refresh_live_runtime_processes_locked(const std::filesystem::path* workspace_root = nullptr);

std::filesystem::path live_runtime_snapshot_config_path_for_request_locked(
    const std::filesystem::path& requested_config_path);

std::string make_recorder_runtime_json(
    const std::filesystem::path& workspace_root,
    const std::optional<bool>& ok = std::nullopt,
    std::string_view message_override = {});

bool looks_like_placeholder_config_value(std::string_view raw);

std::wstring quote_windows_command_argument(std::wstring_view value);

std::string current_timestamp();

std::string trim_copy(std::string_view value) {
    std::size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
        ++begin;
    }

    std::size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }

    return std::string(value.substr(begin, end - begin));
}

std::wstring trim_copy(std::wstring_view value) {
    std::size_t begin = 0;
    while (begin < value.size() && std::iswspace(value[begin]) != 0) {
        ++begin;
    }

    std::size_t end = value.size();
    while (end > begin && std::iswspace(value[end - 1]) != 0) {
        --end;
    }

    return std::wstring(value.substr(begin, end - begin));
}

std::wstring lower_copy(std::wstring_view value) {
    std::wstring lowered(value);
    for (auto& ch : lowered) {
        ch = static_cast<wchar_t>(std::towlower(ch));
    }
    return lowered;
}

std::filesystem::path recorder_runtime_log_path(const std::filesystem::path& workspace_root) {
    return workspace_root / "runtime" / "ctp_md_recorder" / "dashboard_last_run.log";
}

std::filesystem::path recorder_watchdog_log_path(const std::filesystem::path& workspace_root) {
    return workspace_root / "runtime" / "ctp_md_recorder" / "watchdog.log";
}

std::filesystem::path live_runtime_log_path(const std::filesystem::path& config_path) {
    return itrader::runtime_namespace_directory(config_path) / "dashboard_last_run.log";
}

std::filesystem::path recorder_managed_task_name_path(const std::filesystem::path& workspace_root) {
    return workspace_root / "runtime" / "ctp_md_recorder" / "managed_task_name.txt";
}

std::string read_process_log_tail(const std::filesystem::path& log_path, std::size_t max_lines = 12) {
    if (log_path.empty()) {
        return {};
    }

    std::ifstream input(log_path);
    if (!input.is_open()) {
        return {};
    }

    std::deque<std::string> recent_lines;
    std::string line;
    while (std::getline(input, line)) {
        auto trimmed = trim_copy(line);
        if (trimmed.empty()) {
            continue;
        }
        recent_lines.push_back(std::move(trimmed));
        if (recent_lines.size() > max_lines) {
            recent_lines.pop_front();
        }
    }

    if (recent_lines.empty()) {
        return {};
    }

    const auto looks_like_reason = [](std::string_view text) {
        return text.find("fatal") != std::string_view::npos
            || text.find("failed") != std::string_view::npos
            || text.find("error") != std::string_view::npos
            || text.find("timed out") != std::string_view::npos
            || text.find("disconnected") != std::string_view::npos
            || text.find("Unable") != std::string_view::npos;
    };

    for (auto it = recent_lines.rbegin(); it != recent_lines.rend(); ++it) {
        if (looks_like_reason(*it)) {
            return *it;
        }
    }

    return recent_lines.back();
}

std::string format_process_exit_message(std::string_view process_name, DWORD exit_code, const std::filesystem::path& log_path) {
    std::string message = std::string(process_name) + " exited with code " + std::to_string(exit_code) + '.';
    const auto detail = trim_copy(read_process_log_tail(log_path));
    if (!detail.empty()) {
        message += " Reason: " + detail;
    }
    return message;
}

std::string format_recorder_exit_message(DWORD exit_code, const std::filesystem::path& log_path) {
    return format_process_exit_message("Recorder", exit_code, log_path);
}

void append_dashboard_watchdog_log(const std::filesystem::path& log_path, std::string_view message) {
    if (log_path.empty()) {
        return;
    }

    std::error_code error_code;
    std::filesystem::create_directories(log_path.parent_path(), error_code);
    std::ofstream output(log_path, std::ios::out | std::ios::app);
    if (!output.is_open()) {
        return;
    }

    output << '[' << current_timestamp() << "] " << message << '\n';
}

std::string format_live_runtime_exit_message(DWORD exit_code, const std::filesystem::path& log_path) {
    return format_process_exit_message("Live runtime", exit_code, log_path);
}

std::string lower_copy(std::string_view raw) {
    std::string value(raw);
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string url_decode(std::string_view raw) {
    std::string decoded;
    decoded.reserve(raw.size());
    for (std::size_t index = 0; index < raw.size(); ++index) {
        const char ch = raw[index];
        if (ch == '%' && index + 2 < raw.size()) {
            const auto hex_to_int = [](char value) -> int {
                if (value >= '0' && value <= '9') {
                    return value - '0';
                }
                if (value >= 'a' && value <= 'f') {
                    return value - 'a' + 10;
                }
                if (value >= 'A' && value <= 'F') {
                    return value - 'A' + 10;
                }
                return -1;
            };
            const int high = hex_to_int(raw[index + 1]);
            const int low = hex_to_int(raw[index + 2]);
            if (high >= 0 && low >= 0) {
                decoded.push_back(static_cast<char>((high << 4) | low));
                index += 2;
                continue;
            }
        }
        decoded.push_back(ch == '+' ? ' ' : ch);
    }
    return decoded;
}

std::string lookup_process_env_value(const std::string& key) {
#if defined(_MSC_VER)
    char* buffer = nullptr;
    std::size_t buffer_size = 0;
    const auto result = _dupenv_s(&buffer, &buffer_size, key.c_str());
    if (result != 0 || buffer == nullptr) {
        return {};
    }

    std::string value(buffer, buffer_size > 0 ? buffer_size - 1 : 0);
    std::free(buffer);
    return value;
#else
    if (const char* process_value = std::getenv(key.c_str()); process_value != nullptr) {
        return process_value;
    }
    return {};
#endif
}

std::map<std::string, std::string> load_env_file(const std::filesystem::path& file_path) {
    std::ifstream input(file_path);
    if (!input.is_open()) {
        return {};
    }

    std::map<std::string, std::string> variables;
    std::string line;
    while (std::getline(input, line)) {
        const auto trimmed = trim_copy(line);
        if (trimmed.empty() || trimmed.starts_with('#') || trimmed.starts_with(';')) {
            continue;
        }

        auto candidate = trimmed;
        if (candidate.rfind("export ", 0) == 0) {
            candidate = trim_copy(candidate.substr(7));
        }

        const auto delimiter = candidate.find('=');
        if (delimiter == std::string::npos) {
            continue;
        }

        const auto key = trim_copy(candidate.substr(0, delimiter));
        auto value = trim_copy(candidate.substr(delimiter + 1));
        if (value.size() >= 2
            && ((value.front() == '"' && value.back() == '"')
                || (value.front() == '\'' && value.back() == '\''))) {
            value = value.substr(1, value.size() - 2);
        }
        if (!key.empty()) {
            variables[key] = value;
        }
    }

    return variables;
}

std::string lookup_env_value(const std::filesystem::path& workspace_root, const std::string& key) {
    if (const auto process_value = lookup_process_env_value(key); !process_value.empty()) {
        return process_value;
    }

    const auto env_overrides = load_env_file(workspace_root / ".env");
    const auto env_it = env_overrides.find(key);
    return env_it == env_overrides.end() ? std::string {} : env_it->second;
}

std::string ui_access_password(const std::filesystem::path& workspace_root) {
    return trim_copy(lookup_env_value(workspace_root, "ITRADER_UI_PASSWORD"));
}

std::string base64_decode(std::string_view encoded) {
    static const std::string alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::array<int, 256> reverse_table {};
    reverse_table.fill(-1);
    for (std::size_t index = 0; index < alphabet.size(); ++index) {
        reverse_table[static_cast<unsigned char>(alphabet[index])] = static_cast<int>(index);
    }

    std::string decoded;
    int value = 0;
    int bits = -8;
    for (const unsigned char ch : encoded) {
        if (std::isspace(ch) != 0) {
            continue;
        }
        if (ch == '=') {
            break;
        }
        const int decoded_value = reverse_table[ch];
        if (decoded_value < 0) {
            return {};
        }
        value = (value << 6) + decoded_value;
        bits += 6;
        if (bits >= 0) {
            decoded.push_back(static_cast<char>((value >> bits) & 0xFF));
            bits -= 8;
        }
    }
    return decoded;
}

bool authorization_matches_password(std::string_view authorization_header, std::string_view expected_password) {
    const auto trimmed = trim_copy(authorization_header);
    if (trimmed.size() < 6 || lower_copy(trimmed.substr(0, 6)) != "basic ") {
        return false;
    }

    const auto decoded = base64_decode(trim_copy(trimmed.substr(6)));
    const auto separator = decoded.find(':');
    if (separator == std::string::npos) {
        return false;
    }

    return decoded.substr(separator + 1) == expected_password;
}

bool request_is_authorized(const std::filesystem::path& workspace_root, const HttpRequest& request) {
    const auto password = ui_access_password(workspace_root);
    if (password.empty() || looks_like_placeholder_config_value(password)) {
        return true;
    }

    const auto authorization_it = request.headers.find("authorization");
    if (authorization_it == request.headers.end()) {
        return false;
    }

    return authorization_matches_password(authorization_it->second, password);
}

HttpResponse make_unauthorized_response() {
    return HttpResponse {
        "401 Unauthorized",
        "text/plain; charset=utf-8",
        "Authentication required.",
        {{"WWW-Authenticate", "Basic realm=\"iTrader Control Center\", charset=\"UTF-8\""}}
    };
}

std::string normalize_backtest_detail_level(std::string_view raw) {
    const auto normalized = lower_copy(trim_copy(raw));
    return normalized == "summary" ? "summary" : "full";
}

std::string upper_copy(std::string_view raw) {
    std::string value(raw);
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });
    return value;
}

bool looks_like_placeholder_config_value(std::string_view raw) {
    const auto normalized = upper_copy(trim_copy(raw));
    return normalized.rfind("PLEASE_SET_", 0) == 0
        || normalized.rfind("CHANGEME", 0) == 0
        || normalized.rfind("TODO", 0) == 0;
}

std::string normalize_exchange_code(std::string_view raw) {
    const auto value = upper_copy(trim_copy(raw));
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

std::pair<std::string, std::string> split_symbol_and_exchange(std::string_view raw_symbol) {
    const auto trimmed = trim_copy(raw_symbol);
    const auto delimiter = trimmed.find('.');
    if (delimiter == std::string::npos) {
        return {upper_copy(trimmed), {}};
    }

    return {
        upper_copy(trimmed.substr(0, delimiter)),
        normalize_exchange_code(trimmed.substr(delimiter + 1))
    };
}

std::string canonical_instrument_for_filter(std::string_view raw_instrument, const std::set<std::string>* instrument_filter) {
    const auto normalized = upper_copy(trim_copy(raw_instrument));
    if (instrument_filter == nullptr || instrument_filter->empty()) {
        return normalized;
    }

    for (const auto& candidate : *instrument_filter) {
        if (upper_copy(candidate) == normalized) {
            return candidate;
        }
    }

    return {};
}

std::string json_escape(std::string_view raw) {
    std::string escaped;
    escaped.reserve(raw.size() + 8);
    for (const char ch : raw) {
        switch (ch) {
        case '\\':
            escaped += "\\\\";
            break;
        case '"':
            escaped += "\\\"";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            escaped.push_back(ch);
            break;
        }
    }
    return escaped;
}

std::string quoted(std::string_view raw) {
    return '"' + json_escape(raw) + '"';
}

std::string format_decimal(double value) {
    std::ostringstream output;
    output << std::fixed << std::setprecision(2) << value;
    return output.str();
}

long long current_time_millis() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string current_timestamp() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm local_time {};
    localtime_s(&local_time, &time);
    std::ostringstream output;
    output << std::put_time(&local_time, "%F %T");
    return output.str();
}

long long filetime_to_unix_millis(const FILETIME& file_time) {
    ULARGE_INTEGER value {};
    value.LowPart = file_time.dwLowDateTime;
    value.HighPart = file_time.dwHighDateTime;
    if (value.QuadPart == 0) {
        return 0;
    }

    constexpr unsigned long long kWindowsToUnixEpochOffset100ns = 116444736000000000ULL;
    if (value.QuadPart < kWindowsToUnixEpochOffset100ns) {
        return 0;
    }

    return static_cast<long long>((value.QuadPart - kWindowsToUnixEpochOffset100ns) / 10000ULL);
}

long long process_started_at_millis(HANDLE process_handle) {
    if (process_handle == nullptr) {
        return 0;
    }

    FILETIME creation_time {};
    FILETIME exit_time {};
    FILETIME kernel_time {};
    FILETIME user_time {};
    if (!GetProcessTimes(process_handle, &creation_time, &exit_time, &kernel_time, &user_time)) {
        return 0;
    }

    return filetime_to_unix_millis(creation_time);
}

bool run_hidden_process_command(std::wstring command_line, DWORD timeout_ms, DWORD* exit_code = nullptr) {
    STARTUPINFOW startup_info {};
    startup_info.cb = sizeof(startup_info);
    PROCESS_INFORMATION process_info {};

    std::vector<wchar_t> mutable_command_line(command_line.begin(), command_line.end());
    mutable_command_line.push_back(L'\0');

    if (!CreateProcessW(
            nullptr,
            mutable_command_line.data(),
            nullptr,
            nullptr,
            FALSE,
            CREATE_NO_WINDOW,
            nullptr,
            nullptr,
            &startup_info,
            &process_info)) {
        return false;
    }

    CloseHandle(process_info.hThread);
    const auto wait_result = WaitForSingleObject(process_info.hProcess, timeout_ms);
    if (wait_result == WAIT_TIMEOUT) {
        TerminateProcess(process_info.hProcess, 1);
        CloseHandle(process_info.hProcess);
        return false;
    }
    if (wait_result == WAIT_FAILED) {
        CloseHandle(process_info.hProcess);
        return false;
    }

    DWORD local_exit_code = 0;
    if (!GetExitCodeProcess(process_info.hProcess, &local_exit_code)) {
        CloseHandle(process_info.hProcess);
        return false;
    }

    CloseHandle(process_info.hProcess);
    if (exit_code != nullptr) {
        *exit_code = local_exit_code;
    }
    return local_exit_code == 0;
}

bool run_schtasks_action(const std::string& task_name, std::wstring_view action, DWORD timeout_ms = 5000) {
    if (trim_copy(task_name).empty()) {
        return false;
    }

    wchar_t system_directory[MAX_PATH] = {};
    const auto length = GetSystemDirectoryW(system_directory, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return false;
    }

    const auto schtasks_path = std::filesystem::path(system_directory) / "schtasks.exe";
    std::wstring command_line = quote_windows_command_argument(schtasks_path.wstring());
    command_line += L" /";
    command_line += std::wstring(action);
    command_line += L" /TN ";
    command_line += quote_windows_command_argument(std::wstring(task_name.begin(), task_name.end()));
    return run_hidden_process_command(std::move(command_line), timeout_ms);
}

std::string normalize_path_for_compare(const std::filesystem::path& path) {
    if (path.empty()) {
        return {};
    }

    std::error_code error_code;
    const auto normalized = std::filesystem::weakly_canonical(path, error_code);
    return lower_copy((error_code ? path.lexically_normal() : normalized).generic_string());
}

std::wstring quote_windows_command_argument(std::wstring_view value) {
    if (value.find_first_of(L" \t\"") == std::wstring_view::npos) {
        return std::wstring(value);
    }

    std::wstring quoted_value;
    quoted_value.reserve(value.size() + 2);
    quoted_value.push_back(L'"');
    for (const auto ch : value) {
        if (ch == L'"') {
            quoted_value += L"\\\"";
            continue;
        }
        quoted_value.push_back(ch);
    }
    quoted_value.push_back(L'"');
    return quoted_value;
}

std::string make_backtest_job_json(const std::shared_ptr<BacktestReplayJob>& job) {
    if (job == nullptr) {
        return "{\"ok\":false,\"message\":\"No backtest replay job exists.\"}";
    }

    std::scoped_lock guard(job->mutex);
    std::ostringstream json;
    json << '{'
         << "\"ok\":true,"
         << "\"id\":" << quoted(job->id) << ','
         << "\"requested_config\":" << quoted(job->requested_config) << ','
            << "\"detail_level\":" << quoted(job->detail_level) << ','
         << "\"status\":" << quoted(job->status) << ','
         << "\"phase\":" << quoted(job->phase) << ','
         << "\"processed_files\":" << job->processed_files << ','
         << "\"total_files\":" << job->total_files << ','
         << "\"processed_ticks\":" << job->processed_ticks << ','
         << "\"started_at_ms\":" << job->started_at_ms << ','
         << "\"finished_at_ms\":" << job->finished_at_ms << ','
         << "\"cancel_requested\":" << (job->cancel_requested.load() ? "true" : "false") << ','
         << "\"error_message\":" << quoted(job->error_message);
    if (!job->state_json.empty()) {
        json << ",\"state\":" << job->state_json;
    }
    json << '}';
    return json.str();
}

std::wstring executable_path() {
    std::array<wchar_t, 4096> buffer {};
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length == buffer.size()) {
        throw std::runtime_error("Unable to determine executable path");
    }
    return std::wstring(buffer.data(), length);
}

bool looks_like_workspace_root(const std::filesystem::path& candidate) {
    return std::filesystem::exists(candidate / "configs" / "backtest.ini")
        && std::filesystem::exists(candidate / "configs" / "live.ini")
        && std::filesystem::exists(candidate / "ui" / "index.html");
}

std::filesystem::path discover_workspace_root(const ServerOptions& options) {
    if (options.root_override.has_value()) {
        return std::filesystem::weakly_canonical(*options.root_override);
    }

    std::vector<std::filesystem::path> seeds {
        std::filesystem::current_path(),
        std::filesystem::path(executable_path()).parent_path()
    };

    for (const auto& seed : seeds) {
        auto current = std::filesystem::weakly_canonical(seed);
        while (!current.empty()) {
            if (looks_like_workspace_root(current)) {
                return current;
            }
            if (current == current.root_path()) {
                break;
            }
            current = current.parent_path();
        }
    }

    throw std::runtime_error("Unable to discover workspace root. Pass --root <path-to-workspace>.");
}

std::filesystem::path discover_live_runtime_binary_path(const std::filesystem::path& workspace_root) {
    const auto current_executable_directory = std::filesystem::path(executable_path()).parent_path();
    std::vector<std::filesystem::path> candidates {
        current_executable_directory / "itrader.exe"
    };

    if (!current_executable_directory.filename().empty()) {
        candidates.push_back(workspace_root / "build" / current_executable_directory.filename() / "itrader.exe");
    }
    candidates.push_back(workspace_root / "build" / "Release" / "itrader.exe");
    candidates.push_back(workspace_root / "build" / "Debug" / "itrader.exe");

    for (const auto& candidate : candidates) {
        std::error_code error_code;
        if (std::filesystem::exists(candidate, error_code) && std::filesystem::is_regular_file(candidate, error_code)) {
            return std::filesystem::weakly_canonical(candidate);
        }
    }

    throw std::runtime_error("Unable to locate itrader.exe next to itrader_ui_api.exe or under build/<Config>.");
}

std::filesystem::path discover_recorder_runtime_binary_path(const std::filesystem::path& workspace_root) {
    const auto current_executable_directory = std::filesystem::path(executable_path()).parent_path();
    std::vector<std::filesystem::path> candidates {
        current_executable_directory / "itrader_ctp_md_recorder.exe"
    };

    if (!current_executable_directory.filename().empty()) {
        candidates.push_back(workspace_root / "build" / current_executable_directory.filename() / "itrader_ctp_md_recorder.exe");
    }
    candidates.push_back(workspace_root / "build" / "Release" / "itrader_ctp_md_recorder.exe");
    candidates.push_back(workspace_root / "build" / "Debug" / "itrader_ctp_md_recorder.exe");

    for (const auto& candidate : candidates) {
        std::error_code error_code;
        if (std::filesystem::exists(candidate, error_code) && std::filesystem::is_regular_file(candidate, error_code)) {
            return std::filesystem::weakly_canonical(candidate);
        }
    }

    throw std::runtime_error("Unable to locate itrader_ctp_md_recorder.exe next to itrader_ui_api.exe or under build/<Config>.");
}

std::string read_text_file(const std::filesystem::path& file_path) {
    std::ifstream input(file_path, std::ios::binary);
    if (!input.is_open()) {
        throw std::runtime_error("Unable to open file: " + file_path.string());
    }

    std::ostringstream output;
    output << input.rdbuf();
    return output.str();
}

void write_text_file(const std::filesystem::path& file_path, const std::string& contents) {
    std::ofstream output(file_path, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
        throw std::runtime_error("Unable to write file: " + file_path.string());
    }
    output << contents;
}

void write_binary_file(const std::filesystem::path& file_path, const std::string& contents) {
    std::ofstream output(file_path, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
        throw std::runtime_error("Unable to write file: " + file_path.string());
    }
    output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
}

std::string get_query_value(std::string_view query, std::string_view key) {
    std::size_t start = 0;
    while (start < query.size()) {
        const auto end = query.find('&', start);
        const auto token = query.substr(start, end == std::string_view::npos ? query.size() - start : end - start);
        const auto equals = token.find('=');
        if (equals != std::string_view::npos) {
            const auto current_key = token.substr(0, equals);
            if (current_key == key) {
                return trim_copy(token.substr(equals + 1));
            }
        }
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1;
    }
    return {};
}

std::string get_header_value(const HttpRequest& request, std::string_view key) {
    const auto it = request.headers.find(lower_copy(key));
    return it == request.headers.end() ? std::string {} : it->second;
}

std::pair<std::string, std::string> split_target_path_and_query(std::string_view target) {
    const auto query_offset = target.find('?');
    if (query_offset == std::string_view::npos) {
        return {std::string(target), {}};
    }

    return {
        std::string(target.substr(0, query_offset)),
        std::string(target.substr(query_offset + 1))
    };
}

std::size_t parse_content_length(std::string_view headers) {
    std::istringstream input {std::string(headers)};
    std::string line;
    while (std::getline(input, line)) {
        const auto colon = line.find(':');
        if (colon == std::string::npos) {
            continue;
        }
        const auto header_name = lower_copy(trim_copy(line.substr(0, colon)));
        if (header_name != "content-length") {
            continue;
        }
        return static_cast<std::size_t>(std::stoul(trim_copy(line.substr(colon + 1))));
    }
    return 0;
}

std::vector<std::string> read_strategy_accounts(const itrader::IniFile& ini, const std::string& section_name) {
    std::vector<std::string> accounts = ini.get_list(section_name, "accounts");
    if (accounts.empty()) {
        accounts = ini.get_list(section_name, "account");
    }

    std::vector<std::string> normalized;
    for (const auto& account : accounts) {
        const auto trimmed = trim_copy(account);
        if (trimmed.empty()) {
            continue;
        }
        if (std::find(normalized.begin(), normalized.end(), trimmed) == normalized.end()) {
            normalized.push_back(trimmed);
        }
    }
    return normalized;
}

std::vector<std::string> read_strategy_instruments(const itrader::IniFile& ini, const std::string& section_name) {
    std::vector<std::string> instruments;
    for (const auto& instrument : ini.get_list(section_name, "instruments")) {
        const auto trimmed = trim_copy(instrument);
        if (trimmed.empty()) {
            continue;
        }
        if (std::find(instruments.begin(), instruments.end(), trimmed) == instruments.end()) {
            instruments.push_back(trimmed);
        }
    }
    return instruments;
}

std::filesystem::path resolve_path(const std::filesystem::path& base_dir, const std::string& value) {
    std::filesystem::path candidate(value);
    if (candidate.is_relative()) {
        candidate = base_dir / candidate;
    }
    return candidate.lexically_normal();
}

std::string default_config_filename(std::string_view mode) {
    return mode == "live" ? "live.example.ini" : "backtest.ini";
}

std::filesystem::path default_config_path_for_read(const std::filesystem::path& workspace_root, std::string_view mode) {
    const auto preferred = workspace_root / "configs" / default_config_filename(mode);
    if (std::filesystem::exists(preferred)) {
        return preferred;
    }

    if (mode == "live") {
        return workspace_root / "configs" / "live.example.ini";
    }
    return workspace_root / "configs" / "backtest.ini";
}

std::filesystem::path default_config_path_for_write(const std::filesystem::path& workspace_root, std::string_view mode) {
    return workspace_root / "configs" / default_config_filename(mode);
}

std::filesystem::path resolve_config_path(const std::filesystem::path& workspace_root, std::string_view mode, std::string_view requested_config) {
    const std::string normalized_mode = mode == "live" ? "live" : "backtest";
    const std::string trimmed_request = trim_copy(requested_config);
    if (trimmed_request.empty()) {
        return default_config_path_for_read(workspace_root, normalized_mode);
    }

    std::filesystem::path requested_path(trimmed_request);
    if (requested_path.is_absolute() || trimmed_request.find("..") != std::string::npos) {
        throw std::runtime_error("Config query parameter must stay within the configs directory.");
    }

    const auto candidate = (workspace_root / "configs" / requested_path).lexically_normal();
    const auto configs_root = (workspace_root / "configs").lexically_normal();
    const auto candidate_string = candidate.generic_string();
    const auto configs_string = configs_root.generic_string();
    if (candidate_string.rfind(configs_string, 0) != 0) {
        throw std::runtime_error("Config query parameter resolved outside the configs directory.");
    }
    if (!std::filesystem::exists(candidate)) {
        throw std::runtime_error("Requested config file does not exist: " + candidate.filename().generic_string());
    }
    return candidate;
}

std::filesystem::path resolve_config_write_path(const std::filesystem::path& workspace_root, std::string_view mode, std::string_view requested_config) {
    const std::string normalized_mode = mode == "live" ? "live" : "backtest";
    const std::string trimmed_request = trim_copy(requested_config);
    if (trimmed_request.empty()) {
        return default_config_path_for_write(workspace_root, normalized_mode);
    }

    std::filesystem::path requested_path(trimmed_request);
    if (requested_path.is_absolute() || trimmed_request.find("..") != std::string::npos) {
        throw std::runtime_error("Config query parameter must stay within the configs directory.");
    }

    const auto candidate = (workspace_root / "configs" / requested_path).lexically_normal();
    const auto configs_root = (workspace_root / "configs").lexically_normal();
    const auto candidate_string = candidate.generic_string();
    const auto configs_string = configs_root.generic_string();
    if (candidate_string.rfind(configs_string, 0) != 0) {
        throw std::runtime_error("Config query parameter resolved outside the configs directory.");
    }
    return candidate;
}

std::filesystem::path recorder_config_path(const std::filesystem::path& workspace_root) {
    return (workspace_root / "configs" / "ctp_md_recorder.ini").lexically_normal();
}

bool recorder_auto_restart_enabled(const std::filesystem::path& workspace_root) {
    const auto config_path = recorder_config_path(workspace_root);
    if (!std::filesystem::exists(config_path)) {
        return false;
    }

    try {
        const auto ini = itrader::IniFile::parse(config_path);
        return ini.get_bool("recorder", "auto_restart_enabled", true);
    } catch (const std::exception&) {
        return false;
    }
}

std::string managed_recorder_task_name(const std::filesystem::path& workspace_root) {
    const auto marker_path = recorder_managed_task_name_path(workspace_root);
    std::ifstream input(marker_path);
    if (!input.is_open()) {
        return {};
    }

    std::string line;
    std::getline(input, line);
    return trim_copy(line);
}

std::optional<LiveRuntimeProcess> detect_external_recorder_runtime(const std::filesystem::path& workspace_root) {
    std::filesystem::path expected_binary_path;
    try {
        expected_binary_path = discover_recorder_runtime_binary_path(workspace_root);
    } catch (const std::exception&) {
        return std::nullopt;
    }

    const auto expected_binary_text = normalize_path_for_compare(expected_binary_path);
    const auto config_path = recorder_config_path(workspace_root);
    const auto log_path = recorder_runtime_log_path(workspace_root);
    const auto task_name = managed_recorder_task_name(workspace_root);

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return std::nullopt;
    }

    PROCESSENTRY32W entry {};
    entry.dwSize = sizeof(entry);
    if (!Process32FirstW(snapshot, &entry)) {
        CloseHandle(snapshot);
        return std::nullopt;
    }

    do {
        if (_wcsicmp(entry.szExeFile, L"itrader_ctp_md_recorder.exe") != 0) {
            continue;
        }

        HANDLE process_handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, entry.th32ProcessID);
        if (process_handle == nullptr) {
            continue;
        }

        std::wstring image_path(32768, L'\0');
        DWORD image_path_size = static_cast<DWORD>(image_path.size());
        if (!QueryFullProcessImageNameW(process_handle, 0, image_path.data(), &image_path_size)) {
            CloseHandle(process_handle);
            continue;
        }

        image_path.resize(image_path_size);
        std::error_code error_code;
        const auto process_binary_path = std::filesystem::weakly_canonical(std::filesystem::path(image_path), error_code);
        const auto process_binary_text = normalize_path_for_compare(error_code ? std::filesystem::path(image_path) : process_binary_path);
        if (process_binary_text != expected_binary_text) {
            CloseHandle(process_handle);
            continue;
        }

        LiveRuntimeProcess runtime;
        runtime.process_id = entry.th32ProcessID;
        runtime.executable_path = error_code ? std::filesystem::path(image_path) : process_binary_path;
        runtime.config_path = config_path;
        runtime.log_path = log_path;
        runtime.status = "running";
        runtime.started_at_ms = process_started_at_millis(process_handle);
        runtime.finished_at_ms = 0;
        runtime.exit_code = STILL_ACTIVE;
        runtime.stop_requested = false;
        runtime.managed_by = task_name.empty() ? "external" : "scheduled_task";
        runtime.controller_name = task_name;
        runtime.message = task_name.empty()
            ? "Recorder is currently running outside dashboard tracking."
            : ("Recorder is currently running under scheduled task \"" + task_name + "\".");
        CloseHandle(process_handle);
        CloseHandle(snapshot);
        return runtime;
    } while (Process32NextW(snapshot, &entry));

    CloseHandle(snapshot);
    return std::nullopt;
}

std::string make_recorder_config_json(const std::filesystem::path& workspace_root) {
    const auto config_path = recorder_config_path(workspace_root);
    const auto launch_script_path = (workspace_root / "scripts" / "start_recorder_release.ps1").lexically_normal();
    const bool exists = std::filesystem::exists(config_path);

    std::string account_section_name {"account.recorder"};
    std::string account_id {"recorder"};
    std::string output_dir {"../runtime/ctp_md_recorder/agtick"};
    std::string instruments {"ag2606"};
    std::string flush_interval_ms {"1000"};
    std::string status_interval_ms {"30000"};
    std::string idle_sleep_ms {"250"};
    std::string connect_timeout_ms {"15000"};
    std::string deduplicate_exact_ticks {"true"};
    std::string auto_restart_enabled {"true"};
    std::string front;
    std::string broker_id;
    std::string user_id;
    std::string password;
    std::string product_info {"iTrader"};
    std::string flow_dir;
    std::string production_mode {"true"};
    std::string reconnect_enabled {"true"};
    std::string reconnect_retry_interval_ms {"3000"};
    std::string reconnect_max_attempts {"0"};

    if (exists) {
        const auto ini = itrader::IniFile::parse(config_path);
        const auto account_sections = ini.sections_with_prefix("account.");
        if (!account_sections.empty()) {
            account_section_name = account_sections.front();
            const auto separator = account_section_name.find('.');
            if (separator != std::string::npos && separator + 1 < account_section_name.size()) {
                account_id = account_section_name.substr(separator + 1);
            }
        }

        output_dir = ini.get("recorder", "output_dir", output_dir);
        instruments = ini.get("recorder", "instruments", instruments);
        flush_interval_ms = ini.get("recorder", "flush_interval_ms", flush_interval_ms);
        status_interval_ms = ini.get("recorder", "status_interval_ms", status_interval_ms);
        idle_sleep_ms = ini.get("recorder", "idle_sleep_ms", idle_sleep_ms);
        connect_timeout_ms = ini.get("recorder", "connect_timeout_ms", connect_timeout_ms);
        deduplicate_exact_ticks = ini.get("recorder", "deduplicate_exact_ticks", deduplicate_exact_ticks);
        auto_restart_enabled = ini.get("recorder", "auto_restart_enabled", auto_restart_enabled);

        front = ini.get(account_section_name, "md_front", ini.get(account_section_name, "front"));
        broker_id = ini.get(account_section_name, "md_broker_id", ini.get(account_section_name, "broker_id"));
        user_id = ini.get(account_section_name, "md_user_id", ini.get(account_section_name, "user_id"));
        password = ini.get(account_section_name, "md_password", ini.get(account_section_name, "password"));
        product_info = ini.get(account_section_name, "product_info", product_info);
        flow_dir = ini.get(account_section_name, "md_flow_dir", flow_dir);
        production_mode = ini.get(account_section_name, "production_mode", production_mode);
        reconnect_enabled = ini.get(account_section_name, "reconnect_enabled", reconnect_enabled);
        reconnect_retry_interval_ms = ini.get(account_section_name, "reconnect_retry_interval_ms", reconnect_retry_interval_ms);
        reconnect_max_attempts = ini.get(account_section_name, "reconnect_max_attempts", reconnect_max_attempts);
    }

    std::ostringstream json;
    json << '{'
         << "\"config_path\":" << quoted(config_path.generic_string()) << ','
         << "\"launch_script\":" << quoted(launch_script_path.generic_string()) << ','
         << "\"exists\":" << (exists ? "true" : "false") << ','
         << "\"account_section\":" << quoted(account_section_name) << ','
         << "\"account_id\":" << quoted(account_id) << ','
         << "\"output_dir\":" << quoted(output_dir) << ','
         << "\"instruments\":" << quoted(instruments) << ','
         << "\"flush_interval_ms\":" << quoted(flush_interval_ms) << ','
         << "\"status_interval_ms\":" << quoted(status_interval_ms) << ','
         << "\"idle_sleep_ms\":" << quoted(idle_sleep_ms) << ','
         << "\"connect_timeout_ms\":" << quoted(connect_timeout_ms) << ','
         << "\"deduplicate_exact_ticks\":" << quoted(deduplicate_exact_ticks) << ','
         << "\"auto_restart_enabled\":" << quoted(auto_restart_enabled) << ','
         << "\"front\":" << quoted(front) << ','
         << "\"md_front\":" << quoted(front) << ','
         << "\"broker_id\":" << quoted(broker_id) << ','
         << "\"user_id\":" << quoted(user_id) << ','
         << "\"password\":" << quoted(password) << ','
         << "\"product_info\":" << quoted(product_info) << ','
         << "\"flow_dir\":" << quoted(flow_dir) << ','
         << "\"production_mode\":" << quoted(production_mode) << ','
         << "\"reconnect_enabled\":" << quoted(reconnect_enabled) << ','
         << "\"reconnect_retry_interval_ms\":" << quoted(reconnect_retry_interval_ms) << ','
         << "\"reconnect_max_attempts\":" << quoted(reconnect_max_attempts)
         << '}';
    return json.str();
}

bool looks_like_strategy_dll(const std::filesystem::path& file_path) {
    const auto extension = lower_copy(file_path.extension().string());
    const auto filename = lower_copy(file_path.filename().string());
    return extension == ".dll"
        && filename.find("strategy") != std::string::npos
        && filename.rfind("thost", 0) != 0;
}

std::filesystem::path discover_strategy_catalog_root(const std::filesystem::path& workspace_root) {
    return workspace_root / "strategies" / "bin";
}

std::vector<std::filesystem::path> discover_strategy_dlls(const std::filesystem::path& workspace_root) {
    const auto catalog_root = discover_strategy_catalog_root(workspace_root);
    if (!std::filesystem::exists(catalog_root) || !std::filesystem::is_directory(catalog_root)) {
        return {};
    }

    std::vector<std::filesystem::path> results;
    std::set<std::string> seen_paths;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(catalog_root, std::filesystem::directory_options::skip_permission_denied)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        if (!looks_like_strategy_dll(entry.path())) {
            continue;
        }

        const auto normalized = std::filesystem::weakly_canonical(entry.path()).generic_string();
        if (seen_paths.insert(normalized).second) {
            results.push_back(std::filesystem::path(normalized));
        }
    }

    std::sort(results.begin(), results.end(), [](const std::filesystem::path& left, const std::filesystem::path& right) {
        const auto left_name = left.filename().generic_string();
        const auto right_name = right.filename().generic_string();
        if (left_name != right_name) {
            return left_name < right_name;
        }
        return left.generic_string() < right.generic_string();
    });

    return results;
}

struct StrategyParameterSchemaEntry {
    std::string key;
    std::string type;
    std::string default_value;
};

std::vector<StrategyParameterSchemaEntry> infer_strategy_parameter_schema_from_source(
    const std::filesystem::path& workspace_root,
    const std::filesystem::path& strategy_path) {

    const auto source_path = workspace_root / "strategies" / (strategy_path.stem().generic_string() + ".cpp");
    std::error_code error_code;
    if (!std::filesystem::exists(source_path, error_code) || !std::filesystem::is_regular_file(source_path, error_code)) {
        return {};
    }

    const auto source_text = read_text_file(source_path);
    std::vector<StrategyParameterSchemaEntry> schema;
    std::set<std::string> seen_keys;
    const auto append_schema_entry = [&schema, &seen_keys](std::string key, std::string type, std::string default_value) {
        key = trim_copy(key);
        if (key.empty() || !seen_keys.insert(key).second) {
            return;
        }

        schema.push_back(StrategyParameterSchemaEntry {
            .key = std::move(key),
            .type = std::move(type),
            .default_value = trim_copy(default_value),
        });
    };

    const std::regex read_int_pattern(R"__REGEX__(read_int\s*\(\s*parameters\s*,\s*"([^"]+)"\s*,\s*[^,]+,\s*([^\)]+?)\s*\)\s*;)__REGEX__");
    for (std::sregex_iterator it(source_text.begin(), source_text.end(), read_int_pattern), end; it != end; ++it) {
        append_schema_entry((*it)[1].str(), "int", (*it)[2].str());
    }

    const std::regex read_double_pattern(R"__REGEX__(read_double\s*\(\s*parameters\s*,\s*"([^"]+)"\s*,\s*[^,]+,\s*([^\)]+?)\s*\)\s*;)__REGEX__");
    for (std::sregex_iterator it(source_text.begin(), source_text.end(), read_double_pattern), end; it != end; ++it) {
        append_schema_entry((*it)[1].str(), "double", (*it)[2].str());
    }

    const std::regex find_pattern(R"__REGEX__(parameters\.find\s*\(\s*"([^"]+)"\s*\))__REGEX__");
    for (std::sregex_iterator it(source_text.begin(), source_text.end(), find_pattern), end; it != end; ++it) {
        append_schema_entry((*it)[1].str(), "string", "");
    }

    return schema;
}

std::vector<StrategyParameterSchemaEntry> load_strategy_parameter_schema_from_dll(const std::filesystem::path& strategy_path) {
    std::vector<StrategyParameterSchemaEntry> schema;

    const HMODULE module = LoadLibraryExW(strategy_path.wstring().c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (module == nullptr) {
        return schema;
    }

    const auto unload_module = [&module]() {
        FreeLibrary(module);
    };

    const auto get_schema = reinterpret_cast<itrader::GetStrategySchemaFn>(GetProcAddress(module, itrader::kGetStrategySchemaSymbol));
    if (get_schema == nullptr) {
        unload_module();
        return schema;
    }

    const auto* exported_schema = get_schema();
    if (exported_schema == nullptr || exported_schema->parameters == nullptr || exported_schema->parameter_count == 0) {
        unload_module();
        return schema;
    }

    schema.reserve(exported_schema->parameter_count);
    for (std::size_t index = 0; index < exported_schema->parameter_count; ++index) {
        const auto& parameter = exported_schema->parameters[index];
        const std::string key = parameter.key == nullptr ? std::string {} : trim_copy(parameter.key);
        if (key.empty()) {
            continue;
        }

        schema.push_back(StrategyParameterSchemaEntry {
            .key = key,
            .type = parameter.type == nullptr ? std::string {} : trim_copy(parameter.type),
            .default_value = parameter.default_value == nullptr ? std::string {} : trim_copy(parameter.default_value),
        });
    }

    unload_module();
    return schema;
}

std::vector<StrategyParameterSchemaEntry> collect_strategy_parameter_schema(
    const std::filesystem::path& workspace_root,
    const std::filesystem::path& strategy_path) {

    auto schema = load_strategy_parameter_schema_from_dll(strategy_path);
    if (!schema.empty()) {
        return schema;
    }

    return infer_strategy_parameter_schema_from_source(workspace_root, strategy_path);
}

void append_strategy_parameter_schema_json(std::ostringstream& json, const std::vector<StrategyParameterSchemaEntry>& schema) {
    json << '[';
    for (std::size_t index = 0; index < schema.size(); ++index) {
        if (index > 0) {
            json << ',';
        }

        const auto& entry = schema[index];
        json << '{'
             << "\"key\":" << quoted(entry.key) << ','
             << "\"type\":" << quoted(entry.type) << ','
             << "\"default_value\":" << quoted(entry.default_value)
             << '}';
    }
    json << ']';
}

std::string make_config_relative_path(const std::filesystem::path& config_path, const std::filesystem::path& target_path) {
    std::error_code error_code;
    const auto relative = std::filesystem::relative(target_path, config_path.parent_path(), error_code);
    if (!error_code && !relative.empty()) {
        return relative.generic_string();
    }
    return target_path.generic_string();
}

std::string make_strategy_file_catalog_json(const std::filesystem::path& workspace_root, std::string_view mode, std::string_view requested_config) {
    const auto config_path = resolve_config_path(workspace_root, mode, requested_config);
    const auto catalog_root = discover_strategy_catalog_root(workspace_root);
    const auto strategy_files = discover_strategy_dlls(workspace_root);

    std::ostringstream json;
    json << "{";
    json << "\"config_path\":" << quoted(config_path.generic_string()) << ',';
    json << "\"catalog_root\":" << quoted(make_config_relative_path(config_path, catalog_root)) << ',';
    json << "\"strategy_files\":[";
    for (std::size_t index = 0; index < strategy_files.size(); ++index) {
        if (index > 0) {
            json << ',';
        }

        const auto& strategy_path = strategy_files[index];
        const auto parameter_schema = collect_strategy_parameter_schema(workspace_root, strategy_path);
        json << '{'
             << "\"id\":" << quoted(strategy_path.stem().generic_string()) << ','
             << "\"filename\":" << quoted(strategy_path.filename().generic_string()) << ','
             << "\"dll\":" << quoted(make_config_relative_path(config_path, strategy_path)) << ','
               << "\"absolute_path\":" << quoted(strategy_path.generic_string()) << ','
               << "\"parameter_schema\":";
           append_strategy_parameter_schema_json(json, parameter_schema);
           json << '}';
    }
    json << "]";
    json << "}";
    return json.str();
}

std::string sanitize_strategy_upload_filename(std::string raw_filename) {
    raw_filename = trim_copy(url_decode(raw_filename));
    if (raw_filename.empty()) {
        throw std::runtime_error("Missing strategy DLL filename.");
    }

    const auto filename = std::filesystem::path(raw_filename).filename().generic_string();
    if (filename.empty() || filename == "." || filename == ".." || filename != raw_filename) {
        throw std::runtime_error("Strategy DLL filename must not include a directory.");
    }
    if (filename.find('/') != std::string::npos || filename.find('\\') != std::string::npos) {
        throw std::runtime_error("Strategy DLL filename must not include path separators.");
    }

    const auto extension = lower_copy(std::filesystem::path(filename).extension().generic_string());
    if (extension != ".dll") {
        throw std::runtime_error("Only .dll strategy files can be uploaded.");
    }
    if (!looks_like_strategy_dll(filename)) {
        throw std::runtime_error("Uploaded DLL filename must look like a strategy DLL.");
    }

    return filename;
}

bool running_live_runtime_uses_strategy_filename(
    const LiveRuntimeProcess& runtime,
    const std::string& target_filename,
    std::string& detail) {

    if (runtime.status != "running" || runtime.config_path.empty()) {
        return false;
    }

    std::error_code error_code;
    if (!std::filesystem::exists(runtime.config_path, error_code)) {
        return false;
    }

    const auto target_filename_lower = lower_copy(target_filename);
    try {
        const auto ini = itrader::IniFile::parse(runtime.config_path);
        for (const auto& section_name : ini.sections_with_prefix("strategy.")) {
            const auto dll_value = trim_copy(ini.get(section_name, "dll"));
            if (dll_value.empty()) {
                continue;
            }

            const auto resolved_dll = resolve_path(runtime.config_path.parent_path(), dll_value);
            if (lower_copy(resolved_dll.filename().generic_string()) != target_filename_lower) {
                continue;
            }

            detail = "Strategy " + section_name.substr(std::string("strategy.").size())
                + " is running from " + runtime.config_path.generic_string()
                + " and uses " + resolved_dll.generic_string() + ".";
            return true;
        }
    } catch (const std::exception&) {
        return false;
    }

    return false;
}

std::optional<std::string> running_live_runtime_upload_block_reason(
    const std::filesystem::path& target_path) {

    std::scoped_lock guard(g_live_runtime_mutex);
    refresh_live_runtime_processes_locked();
    const auto target_filename = target_path.filename().generic_string();
    std::set<DWORD> tracked_running_process_ids;
    for (const auto& [runtime_key, runtime] : g_live_runtime_processes) {
        (void)runtime_key;
        if (runtime.status == "running" && runtime.process_id != 0) {
            tracked_running_process_ids.insert(runtime.process_id);
        }
        std::string detail;
        if (running_live_runtime_uses_strategy_filename(runtime, target_filename, detail)) {
            return detail;
        }
    }

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W entry {};
        entry.dwSize = sizeof(entry);
        if (Process32FirstW(snapshot, &entry)) {
            do {
                if (_wcsicmp(entry.szExeFile, L"itrader.exe") != 0) {
                    continue;
                }
                if (tracked_running_process_ids.contains(entry.th32ProcessID)) {
                    continue;
                }
                CloseHandle(snapshot);
                return "An itrader.exe live runtime is running outside current dashboard tracking (pid "
                    + std::to_string(entry.th32ProcessID)
                    + "). Stop it before overwriting existing strategy DLLs.";
            } while (Process32NextW(snapshot, &entry));
        }
        CloseHandle(snapshot);
    }

    return std::nullopt;
}

std::string make_strategy_file_upload_json(
    const std::filesystem::path& workspace_root,
    std::string_view mode,
    std::string_view requested_config,
    const HttpRequest& request,
    bool overwrite) {

    const auto config_path = resolve_config_path(workspace_root, mode, requested_config);
    const auto catalog_root = discover_strategy_catalog_root(workspace_root);
    const auto raw_filename = get_header_value(request, "x-strategy-filename");
    const auto filename = sanitize_strategy_upload_filename(raw_filename);
    const auto target_path = (catalog_root / filename).lexically_normal();
    const auto normalized_catalog_root = normalize_path_for_compare(catalog_root);
    const auto normalized_target = normalize_path_for_compare(target_path.parent_path());
    if (normalized_target != normalized_catalog_root) {
        throw std::runtime_error("Upload target escaped the strategy catalog root.");
    }
    if (request.body.empty()) {
        throw std::runtime_error("Uploaded DLL body is empty.");
    }

    std::error_code error_code;
    const bool exists = std::filesystem::exists(target_path, error_code);
    if (exists && !overwrite) {
        std::ostringstream json;
        json << '{'
             << "\"ok\":false,"
             << "\"conflict\":true,"
             << "\"filename\":" << quoted(filename) << ','
             << "\"target_path\":" << quoted(target_path.generic_string()) << ','
             << "\"message\":" << quoted("Strategy DLL " + filename + " already exists. Confirm overwrite before uploading.")
             << '}';
        return json.str();
    }

    if (exists) {
        if (const auto block_reason = running_live_runtime_upload_block_reason(target_path); block_reason.has_value()) {
            std::ostringstream json;
            json << '{'
                 << "\"ok\":false,"
                 << "\"blocked\":true,"
                 << "\"running\":true,"
                 << "\"filename\":" << quoted(filename) << ','
                 << "\"target_path\":" << quoted(target_path.generic_string()) << ','
                 << "\"message\":" << quoted("Stop the running strategy before overwriting " + filename + ". " + *block_reason)
                 << '}';
            return json.str();
        }
    }

    std::filesystem::create_directories(catalog_root);
    const auto temp_path = target_path.parent_path()
        / (target_path.filename().generic_string() + ".upload." + std::to_string(current_time_millis()) + ".tmp");
    write_binary_file(temp_path, request.body);

    try {
        std::filesystem::copy_file(temp_path, target_path, std::filesystem::copy_options::overwrite_existing);
        std::filesystem::remove(temp_path, error_code);
    } catch (const std::exception& ex) {
        std::filesystem::remove(temp_path, error_code);
        throw std::runtime_error("Unable to write uploaded strategy DLL. Stop any process using the DLL and retry. Detail: " + std::string(ex.what()));
    }

    std::ostringstream json;
    json << '{'
         << "\"ok\":true,"
         << "\"overwritten\":" << (exists ? "true" : "false") << ','
         << "\"filename\":" << quoted(filename) << ','
         << "\"dll\":" << quoted(make_config_relative_path(config_path, target_path)) << ','
         << "\"absolute_path\":" << quoted(target_path.generic_string()) << ','
         << "\"message\":" << quoted(std::string(exists ? "Uploaded and overwrote " : "Uploaded ") + filename + " to " + catalog_root.generic_string() + ".")
         << '}';
    return json.str();
}

std::optional<std::filesystem::path> pick_strategy_dll(const std::filesystem::path& workspace_root, const std::filesystem::path& config_path) {
    const auto catalog_root = discover_strategy_catalog_root(workspace_root);
    const auto initial_directory = std::filesystem::exists(catalog_root)
        ? std::filesystem::weakly_canonical(catalog_root)
        : std::filesystem::weakly_canonical(config_path.parent_path());

    std::array<wchar_t, 4096> selected_file {};
    const wchar_t filter[] = L"Strategy DLL (*.dll)\0*.dll\0All files (*.*)\0*.*\0";
    const auto initial_directory_text = initial_directory.wstring();

    OPENFILENAMEW dialog {};
    dialog.lStructSize = sizeof(dialog);
    dialog.lpstrFilter = filter;
    dialog.lpstrFile = selected_file.data();
    dialog.nMaxFile = static_cast<DWORD>(selected_file.size());
    dialog.lpstrInitialDir = initial_directory_text.c_str();
    dialog.lpstrTitle = L"Select strategy DLL";
    dialog.Flags = OFN_DONTADDTORECENT | OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST;
    dialog.lpstrDefExt = L"dll";

    if (GetOpenFileNameW(&dialog) == TRUE) {
        return std::filesystem::path(selected_file.data()).lexically_normal();
    }

    const DWORD error = CommDlgExtendedError();
    if (error == 0) {
        return std::nullopt;
    }

    throw std::runtime_error("GetOpenFileNameW failed with error " + std::to_string(error));
}

std::string make_strategy_file_pick_json(const std::filesystem::path& workspace_root, std::string_view mode, std::string_view requested_config) {
    const auto config_path = resolve_config_path(workspace_root, mode, requested_config);
    const auto selected_path = pick_strategy_dll(workspace_root, config_path);

    std::ostringstream json;
    json << '{';
    json << "\"config_path\":" << quoted(config_path.generic_string()) << ',';
    if (!selected_path.has_value()) {
        json << "\"cancelled\":true";
    } else {
        json << "\"cancelled\":false,";
        json << "\"filename\":" << quoted(selected_path->filename().generic_string()) << ',';
        json << "\"dll\":" << quoted(make_config_relative_path(config_path, *selected_path)) << ',';
        json << "\"absolute_path\":" << quoted(selected_path->generic_string());
    }
    json << '}';
    return json.str();
}

std::filesystem::path normalize_directory_picker_path(const std::filesystem::path& candidate) {
    std::error_code error_code;
    const auto normalized = std::filesystem::weakly_canonical(candidate, error_code);
    if (!error_code && !normalized.empty()) {
        return normalized;
    }
    return candidate.lexically_normal();
}

std::filesystem::path resolve_backtest_directory_start_path(
    const std::filesystem::path& workspace_root,
    const std::filesystem::path& config_path,
    std::string_view current_directory) {

    const auto trimmed_current = trim_copy(current_directory);
    if (!trimmed_current.empty()) {
        std::filesystem::path candidate(trimmed_current);
        if (candidate.is_relative()) {
            candidate = (config_path.parent_path() / candidate).lexically_normal();
        }

        std::error_code error_code;
        if (std::filesystem::exists(candidate, error_code) && std::filesystem::is_directory(candidate, error_code)) {
            return normalize_directory_picker_path(candidate);
        }
    }

    const auto data_root = workspace_root / "data";
    std::error_code error_code;
    if (std::filesystem::exists(data_root, error_code) && std::filesystem::is_directory(data_root, error_code)) {
        return normalize_directory_picker_path(data_root);
    }

    return normalize_directory_picker_path(config_path.parent_path());
}

std::optional<std::filesystem::path> pick_directory(const std::filesystem::path& initial_directory, const wchar_t* title) {
    const HRESULT initialize_result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    const bool should_uninitialize = initialize_result == S_OK || initialize_result == S_FALSE;
    if (FAILED(initialize_result) && initialize_result != RPC_E_CHANGED_MODE) {
        throw std::runtime_error("CoInitializeEx failed with HRESULT " + std::to_string(static_cast<unsigned long>(initialize_result)));
    }

    IFileOpenDialog* dialog = nullptr;
    HRESULT result = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
    if (FAILED(result) || dialog == nullptr) {
        if (should_uninitialize) {
            CoUninitialize();
        }
        throw std::runtime_error("CoCreateInstance(FileOpenDialog) failed with HRESULT " + std::to_string(static_cast<unsigned long>(result)));
    }

    DWORD options = 0;
    result = dialog->GetOptions(&options);
    if (FAILED(result)) {
        dialog->Release();
        if (should_uninitialize) {
            CoUninitialize();
        }
        throw std::runtime_error("IFileOpenDialog::GetOptions failed with HRESULT " + std::to_string(static_cast<unsigned long>(result)));
    }

    result = dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR);
    if (FAILED(result)) {
        dialog->Release();
        if (should_uninitialize) {
            CoUninitialize();
        }
        throw std::runtime_error("IFileOpenDialog::SetOptions failed with HRESULT " + std::to_string(static_cast<unsigned long>(result)));
    }

    if (title != nullptr) {
        dialog->SetTitle(title);
    }

    if (!initial_directory.empty()) {
        const auto initial_directory_text = normalize_directory_picker_path(initial_directory).wstring();
        IShellItem* folder = nullptr;
        result = SHCreateItemFromParsingName(initial_directory_text.c_str(), nullptr, IID_PPV_ARGS(&folder));
        if (SUCCEEDED(result) && folder != nullptr) {
            dialog->SetFolder(folder);
            folder->Release();
        }
    }

    result = dialog->Show(nullptr);
    if (result == HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
        dialog->Release();
        if (should_uninitialize) {
            CoUninitialize();
        }
        return std::nullopt;
    }

    if (FAILED(result)) {
        dialog->Release();
        if (should_uninitialize) {
            CoUninitialize();
        }
        throw std::runtime_error("IFileOpenDialog::Show failed with HRESULT " + std::to_string(static_cast<unsigned long>(result)));
    }

    IShellItem* item = nullptr;
    result = dialog->GetResult(&item);
    if (FAILED(result) || item == nullptr) {
        dialog->Release();
        if (should_uninitialize) {
            CoUninitialize();
        }
        throw std::runtime_error("IFileOpenDialog::GetResult failed with HRESULT " + std::to_string(static_cast<unsigned long>(result)));
    }

    PWSTR selected_path = nullptr;
    result = item->GetDisplayName(SIGDN_FILESYSPATH, &selected_path);
    if (FAILED(result) || selected_path == nullptr) {
        item->Release();
        dialog->Release();
        if (should_uninitialize) {
            CoUninitialize();
        }
        throw std::runtime_error("IShellItem::GetDisplayName failed with HRESULT " + std::to_string(static_cast<unsigned long>(result)));
    }

    const auto picked_path = normalize_directory_picker_path(std::filesystem::path(selected_path));
    CoTaskMemFree(selected_path);
    item->Release();
    dialog->Release();
    if (should_uninitialize) {
        CoUninitialize();
    }

    return picked_path;
}

std::string make_backtest_directory_pick_json(
    const std::filesystem::path& workspace_root,
    std::string_view mode,
    std::string_view requested_config,
    std::string_view current_directory) {

    const auto config_path = resolve_config_path(workspace_root, mode, requested_config);
    const auto initial_directory = resolve_backtest_directory_start_path(workspace_root, config_path, current_directory);
    const auto selected_path = pick_directory(initial_directory, L"Select backtest data directory");

    std::ostringstream json;
    json << '{';
    json << "\"config_path\":" << quoted(config_path.generic_string()) << ',';
    if (!selected_path.has_value()) {
        json << "\"cancelled\":true";
    } else {
        json << "\"cancelled\":false,";
        json << "\"directory\":" << quoted(make_config_relative_path(config_path, *selected_path)) << ',';
        json << "\"absolute_path\":" << quoted(selected_path->generic_string());
    }
    json << '}';
    return json.str();
}

std::optional<long long> parse_timestamp_to_epoch(std::string_view raw_timestamp) {
    const std::string trimmed = trim_copy(raw_timestamp);
    if (trimmed.empty()) {
        return std::nullopt;
    }

    auto try_parse = [](std::string_view candidate, const char* format) -> std::optional<long long> {
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
        return static_cast<long long>(local_time);
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

bool is_leap_year_for_chart_timestamp(int year) {
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

int days_in_month_for_chart_timestamp(int year, int month) {
    static constexpr std::array<int, 12> kDaysByMonth {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 2 && is_leap_year_for_chart_timestamp(year)) {
        return 29;
    }
    return kDaysByMonth[static_cast<std::size_t>(month - 1)];
}

int parse_fixed_width_chart_int(std::string_view value, std::size_t offset, std::size_t width) {
    if (offset + width > value.size()) {
        return -1;
    }

    int parsed = 0;
    for (std::size_t index = offset; index < offset + width; ++index) {
        const unsigned char ch = static_cast<unsigned char>(value[index]);
        if (std::isdigit(ch) == 0) {
            return -1;
        }
        parsed = parsed * 10 + static_cast<int>(ch - '0');
    }
    return parsed;
}

std::optional<long long> parse_wall_clock_as_chart_epoch(std::string_view raw_timestamp) {
    const std::string trimmed = trim_copy(raw_timestamp);
    int year = -1;
    int month = -1;
    int day = -1;
    int hour = -1;
    int minute = -1;
    int second = -1;

    if (trimmed.size() >= 19 && trimmed[4] == '-' && trimmed[7] == '-' && trimmed[10] == ' ') {
        year = parse_fixed_width_chart_int(trimmed, 0, 4);
        month = parse_fixed_width_chart_int(trimmed, 5, 2);
        day = parse_fixed_width_chart_int(trimmed, 8, 2);
        hour = parse_fixed_width_chart_int(trimmed, 11, 2);
        minute = parse_fixed_width_chart_int(trimmed, 14, 2);
        second = parse_fixed_width_chart_int(trimmed, 17, 2);
    } else if (trimmed.size() >= 14
        && std::all_of(trimmed.begin(), trimmed.begin() + 14, [](unsigned char ch) { return std::isdigit(ch) != 0; })) {
        year = parse_fixed_width_chart_int(trimmed, 0, 4);
        month = parse_fixed_width_chart_int(trimmed, 4, 2);
        day = parse_fixed_width_chart_int(trimmed, 6, 2);
        hour = parse_fixed_width_chart_int(trimmed, 8, 2);
        minute = parse_fixed_width_chart_int(trimmed, 10, 2);
        second = parse_fixed_width_chart_int(trimmed, 12, 2);
    } else if (trimmed.size() >= 17 && trimmed[8] == ' ') {
        year = parse_fixed_width_chart_int(trimmed, 0, 4);
        month = parse_fixed_width_chart_int(trimmed, 4, 2);
        day = parse_fixed_width_chart_int(trimmed, 6, 2);
        hour = parse_fixed_width_chart_int(trimmed, 9, 2);
        minute = parse_fixed_width_chart_int(trimmed, 12, 2);
        second = parse_fixed_width_chart_int(trimmed, 15, 2);
    }

    if (year < 1970 || month < 1 || month > 12 || day < 1 || day > days_in_month_for_chart_timestamp(year, month)
        || hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 59) {
        return std::nullopt;
    }

    long long days = 0;
    for (int current_year = 1970; current_year < year; ++current_year) {
        days += is_leap_year_for_chart_timestamp(current_year) ? 366 : 365;
    }
    for (int current_month = 1; current_month < month; ++current_month) {
        days += days_in_month_for_chart_timestamp(year, current_month);
    }
    days += day - 1;

    return ((days * 24LL + hour) * 60LL + minute) * 60LL + second;
}

std::vector<itrader::MarketTick> load_ticks_from_csv_filtered(const std::filesystem::path& file_path, const std::set<std::string>* instrument_filter) {
    std::ifstream input(file_path);
    if (!input.is_open()) {
        throw std::runtime_error("Unable to open chart CSV: " + file_path.string());
    }

    std::vector<itrader::MarketTick> ticks;
    std::string line;
    while (std::getline(input, line)) {
        const auto trimmed = trim_copy(line);
        if (trimmed.empty() || trimmed.starts_with('#')) {
            continue;
        }

        const auto fields = itrader::split_csv(trimmed);
        if (fields.empty()) {
            continue;
        }

        const auto first_column = lower_copy(fields[0]);
        if (first_column == "timestamp" || first_column == "time") {
            continue;
        }

        itrader::MarketTick tick;
        if (fields.size() < 12 || fields[1].find('.') == std::string::npos) {
            throw std::runtime_error(
                "Backtest chart CSV must use AGTICK format "
                "(time,symbol,current,high,low,volume,money,position,a1_v,a1_p,b1_v,b1_p[,upper_limit_price,lower_limit_price]): "
                + file_path.string());
        }

        tick.timestamp = fields[0];
        tick.timestamp_ms = parse_timestamp_to_epoch(tick.timestamp).value_or(0) * 1000LL;
        const auto [parsed_instrument, parsed_exchange] = split_symbol_and_exchange(fields[1]);
        const auto canonical_instrument = canonical_instrument_for_filter(parsed_instrument, instrument_filter);
        if (instrument_filter != nullptr && !instrument_filter->empty() && canonical_instrument.empty()) {
            continue;
        }

        tick.instrument = canonical_instrument.empty() ? parsed_instrument : canonical_instrument;
        tick.exchange = parsed_exchange;
        tick.last = std::stod(fields[2]);
        tick.volume = static_cast<int>(std::llround(std::stod(fields[5])));
        tick.turnover = std::stod(fields[6]);
        tick.ask_size = static_cast<int>(std::llround(std::stod(fields[8])));
        tick.ask = std::stod(fields[9]);
        tick.bid_size = static_cast<int>(std::llround(std::stod(fields[10])));
        tick.bid = std::stod(fields[11]);
        if (fields.size() > 12 && !fields[12].empty()) {
            tick.upper_limit_price = std::stod(fields[12]);
        }
        if (fields.size() > 13 && !fields[13].empty()) {
            tick.lower_limit_price = std::stod(fields[13]);
        }
        if (tick.ask <= 0.0 || tick.bid <= 0.0 || tick.ask_size <= 0 || tick.bid_size <= 0) {
            continue;
        }
        ticks.push_back(std::move(tick));
    }

    return ticks;
}

std::vector<itrader::MarketTick> load_ticks_from_csv(const std::filesystem::path& file_path) {
    return load_ticks_from_csv_filtered(file_path, nullptr);
}

std::set<std::string> collect_backtest_instrument_filter(const itrader::IniFile& ini, const std::vector<std::string>& strategy_sections) {
    std::set<std::string> instruments;
    for (const auto& section_name : strategy_sections) {
        for (const auto& instrument : read_strategy_instruments(ini, section_name)) {
            instruments.insert(instrument);
        }
    }
    return instruments;
}

void sort_ticks_chronologically(std::vector<itrader::MarketTick>& ticks) {
    std::stable_sort(ticks.begin(), ticks.end(), [](const itrader::MarketTick& left, const itrader::MarketTick& right) {
        const auto left_epoch = parse_timestamp_to_epoch(left.timestamp);
        const auto right_epoch = parse_timestamp_to_epoch(right.timestamp);
        if (left_epoch.has_value() && right_epoch.has_value() && left_epoch != right_epoch) {
            return *left_epoch < *right_epoch;
        }
        if (left.timestamp != right.timestamp) {
            return left.timestamp < right.timestamp;
        }
        if (left.instrument != right.instrument) {
            return left.instrument < right.instrument;
        }
        return left.exchange < right.exchange;
    });
}

std::vector<itrader::MarketTick> load_ticks_from_directory(const std::filesystem::path& directory_path, const std::set<std::string>& instrument_filter) {
    if (!std::filesystem::exists(directory_path)) {
        throw std::runtime_error("Backtest data directory does not exist: " + directory_path.string());
    }
    if (!std::filesystem::is_directory(directory_path)) {
        throw std::runtime_error("Backtest data path is not a directory: " + directory_path.string());
    }

    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(directory_path, std::filesystem::directory_options::skip_permission_denied)) {
        if (entry.is_regular_file() && lower_copy(entry.path().extension().string()) == ".csv") {
            files.push_back(entry.path());
        }
    }

    std::sort(files.begin(), files.end());

    std::vector<itrader::MarketTick> ticks;
    for (const auto& file_path : files) {
        auto file_ticks = load_ticks_from_csv_filtered(file_path, &instrument_filter);
        ticks.insert(ticks.end(), std::make_move_iterator(file_ticks.begin()), std::make_move_iterator(file_ticks.end()));
    }

    sort_ticks_chronologically(ticks);
    return ticks;
}

std::set<std::string> collect_strategy_instruments(const itrader::IniFile& ini, const std::string& section_name) {
    std::set<std::string> instruments;
    for (const auto& instrument : read_strategy_instruments(ini, section_name)) {
        instruments.insert(instrument);
    }
    return instruments;
}

void dedupe_ticks(std::vector<itrader::MarketTick>& ticks) {
    ticks.erase(std::unique(ticks.begin(), ticks.end(), [](const itrader::MarketTick& left, const itrader::MarketTick& right) {
        return left.timestamp == right.timestamp
            && left.instrument == right.instrument
            && left.exchange == right.exchange
            && left.last == right.last
            && left.bid == right.bid
            && left.ask == right.ask
            && left.volume == right.volume
            && left.turnover == right.turnover
            && left.bid_size == right.bid_size
            && left.ask_size == right.ask_size;
    }), ticks.end());
}

std::vector<itrader::MarketTick> load_backtest_ticks(
    const std::filesystem::path& config_path,
    const itrader::IniFile& ini,
    const std::vector<std::string>& strategy_sections) {

    std::vector<itrader::MarketTick> ticks;
    std::set<std::string> default_instruments;
    const auto global_data_dir = trim_copy(ini.get("backtest", "data_dir"));
    const auto csv_value = trim_copy(ini.get("backtest", "csv"));

    bool loaded_strategy_specific_source = false;
    for (const auto& section_name : strategy_sections) {
        const auto instruments = collect_strategy_instruments(ini, section_name);
        if (instruments.empty()) {
            continue;
        }

        const auto strategy_data_dir = trim_copy(ini.get(section_name, "backtest_data_dir"));
        if (strategy_data_dir.empty()) {
            default_instruments.insert(instruments.begin(), instruments.end());
            continue;
        }

        auto strategy_ticks = load_ticks_from_directory(resolve_path(config_path.parent_path(), strategy_data_dir), instruments);
        loaded_strategy_specific_source = true;
        ticks.insert(ticks.end(), std::make_move_iterator(strategy_ticks.begin()), std::make_move_iterator(strategy_ticks.end()));
    }

    if (!default_instruments.empty() || !loaded_strategy_specific_source) {
        if (!global_data_dir.empty()) {
            auto shared_ticks = load_ticks_from_directory(resolve_path(config_path.parent_path(), global_data_dir), default_instruments);
            ticks.insert(ticks.end(), std::make_move_iterator(shared_ticks.begin()), std::make_move_iterator(shared_ticks.end()));
        } else {
            if (csv_value.empty()) {
                throw std::runtime_error("[backtest] must set either data_dir=<AGTICK folder> or csv=<AGTICK file>, or each strategy must set backtest_data_dir=<AGTICK folder>");
            }
            auto shared_ticks = load_ticks_from_csv_filtered(resolve_path(config_path.parent_path(), csv_value), default_instruments.empty() ? nullptr : &default_instruments);
            ticks.insert(ticks.end(), std::make_move_iterator(shared_ticks.begin()), std::make_move_iterator(shared_ticks.end()));
        }
    }

    sort_ticks_chronologically(ticks);
    dedupe_ticks(ticks);
    return ticks;
}

std::filesystem::path live_telemetry_path(const std::filesystem::path& config_path) {
    return itrader::live_telemetry_path(config_path);
}

std::filesystem::path strategy_inventory_store_path(const std::filesystem::path& config_path) {
    return itrader::strategy_inventory_store_path(config_path);
}

std::filesystem::path strategy_state_store_path(const std::filesystem::path& config_path) {
    return itrader::strategy_state_store_path(config_path);
}

std::filesystem::path strategy_inventory_adjustments_path(const std::filesystem::path& config_path) {
    return itrader::strategy_inventory_adjustments_path(config_path);
}

std::string telemetry_attachment_key(std::string_view strategy_id, std::string_view account_id) {
    return std::string(strategy_id) + "::" + std::string(account_id);
}

std::string join_strings(const std::vector<std::string>& values, std::string_view delimiter) {
    std::ostringstream output;
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index > 0) {
            output << delimiter;
        }
        output << values[index];
    }
    return output.str();
}

std::vector<std::string> normalize_strategy_id_filter(const std::vector<std::string>& strategy_ids) {
    std::vector<std::string> normalized;
    normalized.reserve(strategy_ids.size());

    for (const auto& strategy_id : strategy_ids) {
        const auto trimmed = trim_copy(strategy_id);
        if (trimmed.empty()) {
            continue;
        }
        if (std::find(normalized.begin(), normalized.end(), trimmed) == normalized.end()) {
            normalized.push_back(trimmed);
        }
    }

    std::sort(normalized.begin(), normalized.end());
    return normalized;
}

std::vector<std::string> parse_strategy_id_filter(std::string_view raw) {
    return normalize_strategy_id_filter(itrader::split_csv(trim_copy(raw)));
}

std::string describe_strategy_scope(const std::vector<std::string>& strategy_ids) {
    if (strategy_ids.empty()) {
        return "all configured strategies";
    }

    if (strategy_ids.size() == 1) {
        return "strategy " + strategy_ids.front();
    }

    return "strategies " + join_strings(strategy_ids, ", ");
}

std::filesystem::path make_strategy_scoped_live_config_path(
    const std::filesystem::path& workspace_root,
    const std::filesystem::path& requested_config_path,
    const std::vector<std::string>& strategy_ids) {

    auto strategy_scope = itrader::sanitize_runtime_namespace(join_strings(strategy_ids, "_"));
    if (strategy_scope.empty()) {
        strategy_scope = "strategy";
    }
    if (strategy_scope.size() > 96) {
        strategy_scope.resize(96);
    }

    const auto extension = requested_config_path.extension().empty()
        ? std::string {".ini"}
        : requested_config_path.extension().generic_string();
    const auto filename = requested_config_path.stem().generic_string()
        + "__dashboard__"
        + strategy_scope
        + extension;
    return (workspace_root / "configs" / filename).lexically_normal();
}

std::string render_strategy_scoped_live_config(
    const std::filesystem::path& requested_config_path,
    const std::vector<std::string>& strategy_ids) {

    const std::set<std::string> selected_sections = [&strategy_ids]() {
        std::set<std::string> sections;
        for (const auto& strategy_id : strategy_ids) {
            sections.insert("strategy." + strategy_id);
        }
        return sections;
    }();

    std::set<std::string> retained_sections;
    std::istringstream input {read_text_file(requested_config_path)};
    std::ostringstream output;
    output << "; Auto-generated by itrader_ui_api for a strategy-scoped live launch.\n";
    output << "; Source config: " << requested_config_path.generic_string() << "\n";
    output << "; Selected strategies: " << join_strings(strategy_ids, ", ") << "\n\n";

    std::string line;
    bool include_current_section = true;
    while (std::getline(input, line)) {
        std::string_view line_view {line};
        if (!line_view.empty() && line_view.back() == '\r') {
            line_view.remove_suffix(1);
        }

        const auto trimmed = trim_copy(line_view);
        if (!trimmed.empty() && trimmed.front() == '[' && trimmed.back() == ']') {
            const auto section_name = trim_copy(trimmed.substr(1, trimmed.size() - 2));
            if (section_name.rfind("strategy.", 0) == 0) {
                include_current_section = selected_sections.contains(section_name);
                if (include_current_section) {
                    retained_sections.insert(section_name);
                }
            } else {
                include_current_section = true;
            }
        }

        if (include_current_section) {
            output << line << '\n';
        }
    }

    std::vector<std::string> missing_strategy_ids;
    for (const auto& strategy_id : strategy_ids) {
        if (!retained_sections.contains("strategy." + strategy_id)) {
            missing_strategy_ids.push_back(strategy_id);
        }
    }
    if (!missing_strategy_ids.empty()) {
        throw std::runtime_error(
            "Requested live strategy is not defined in "
            + requested_config_path.filename().generic_string()
            + ": "
            + join_strings(missing_strategy_ids, ", "));
    }

    return output.str();
}

void copy_file_if_present(const std::filesystem::path& source_path, const std::filesystem::path& target_path) {
    std::error_code error_code;
    if (!std::filesystem::exists(source_path, error_code) || !std::filesystem::is_regular_file(source_path, error_code)) {
        return;
    }
    if (std::filesystem::exists(target_path, error_code) && std::filesystem::is_regular_file(target_path, error_code)) {
        return;
    }

    std::filesystem::create_directories(target_path.parent_path(), error_code);
    if (error_code) {
        throw std::runtime_error("Unable to create runtime directory: " + target_path.parent_path().generic_string());
    }

    std::filesystem::copy_file(source_path, target_path, std::filesystem::copy_options::none, error_code);
    if (error_code) {
        throw std::runtime_error(
            "Unable to seed derived live runtime state from "
            + source_path.generic_string()
            + " to "
            + target_path.generic_string());
    }
}

struct ScopedStrategyAdjustmentFilter {
    std::string strategy_id;
    std::set<std::string> account_ids;
    std::set<std::string> instruments;
};

std::vector<ScopedStrategyAdjustmentFilter> collect_scoped_strategy_adjustment_filters(const itrader::IniFile& scoped_ini) {
    std::vector<ScopedStrategyAdjustmentFilter> filters;
    for (const auto& section_name : scoped_ini.sections_with_prefix("strategy.")) {
        auto strategy_id = trim_copy(section_name.substr(std::string("strategy.").size()));
        if (strategy_id.empty()) {
            continue;
        }

        ScopedStrategyAdjustmentFilter filter;
        filter.strategy_id = std::move(strategy_id);
        for (const auto& account_id : read_strategy_accounts(scoped_ini, section_name)) {
            filter.account_ids.insert(trim_copy(account_id));
        }
        for (const auto& instrument : read_strategy_instruments(scoped_ini, section_name)) {
            filter.instruments.insert(upper_copy(trim_copy(instrument)));
        }
        filters.push_back(std::move(filter));
    }
    return filters;
}

bool inventory_adjustment_matches_scoped_strategy(
    const itrader::IniFile& adjustments_ini,
    const std::string& section,
    const std::vector<ScopedStrategyAdjustmentFilter>& filters) {

    const auto strategy_id = trim_copy(adjustments_ini.get(section, "strategy_id"));
    const auto account_id = trim_copy(adjustments_ini.get(section, "account_id"));
    const auto instrument = upper_copy(trim_copy(adjustments_ini.get(section, "instrument")));
    if (strategy_id.empty() || account_id.empty() || instrument.empty()) {
        return false;
    }

    for (const auto& filter : filters) {
        if (filter.strategy_id != strategy_id) {
            continue;
        }
        if (!filter.account_ids.empty() && !filter.account_ids.contains(account_id)) {
            continue;
        }
        if (!filter.instruments.empty() && !filter.instruments.contains(instrument)) {
            continue;
        }
        return true;
    }
    return false;
}

void copy_scoped_inventory_adjustments_if_present(
    const std::filesystem::path& source_path,
    const std::filesystem::path& target_path,
    const std::filesystem::path& scoped_config_path) {

    std::error_code error_code;
    if (!std::filesystem::exists(source_path, error_code) || !std::filesystem::is_regular_file(source_path, error_code)) {
        return;
    }
    if (std::filesystem::exists(target_path, error_code) && std::filesystem::is_regular_file(target_path, error_code)) {
        return;
    }

    const auto scoped_ini = itrader::IniFile::parse(scoped_config_path);
    const auto filters = collect_scoped_strategy_adjustment_filters(scoped_ini);
    const auto adjustments_ini = itrader::IniFile::parse(source_path);

    std::ostringstream output;
    output << "; Generated by iTrader dashboard\n"
        << "; Each inventory_adjustment.<id> is applied once by the live runtime.\n"
        << "; To apply a new change after an adjustment has already been used, clone it with a new id.\n\n";

    for (const auto& section : adjustments_ini.sections_with_prefix("inventory_adjustment.")) {
        if (!inventory_adjustment_matches_scoped_strategy(adjustments_ini, section, filters)) {
            continue;
        }

        output << '[' << section << "]\n";
        for (const auto& [key, value] : adjustments_ini.section(section)) {
            output << key << '=' << value << "\n";
        }
        output << "\n";
    }

    std::filesystem::create_directories(target_path.parent_path(), error_code);
    if (error_code) {
        throw std::runtime_error("Unable to create runtime directory: " + target_path.parent_path().generic_string());
    }
    write_text_file(target_path, output.str());
}

void seed_strategy_scoped_live_runtime_state(
    const std::filesystem::path& requested_config_path,
    const std::filesystem::path& scoped_config_path) {

    if (normalize_path_for_compare(requested_config_path) == normalize_path_for_compare(scoped_config_path)) {
        return;
    }

    copy_file_if_present(live_telemetry_path(requested_config_path), live_telemetry_path(scoped_config_path));
    copy_file_if_present(strategy_inventory_store_path(requested_config_path), strategy_inventory_store_path(scoped_config_path));
    copy_file_if_present(strategy_state_store_path(requested_config_path), strategy_state_store_path(scoped_config_path));
    copy_scoped_inventory_adjustments_if_present(
        strategy_inventory_adjustments_path(requested_config_path),
        strategy_inventory_adjustments_path(scoped_config_path),
        scoped_config_path);
}

std::vector<std::string> collect_expected_live_attachment_keys(const itrader::IniFile& ini, const std::vector<std::string>& strategy_sections) {
    std::vector<std::string> keys;
    for (const auto& section_name : strategy_sections) {
        const auto strategy_id = section_name.substr(section_name.find('.') + 1);
        for (const auto& account_id : read_strategy_accounts(ini, section_name)) {
            const auto key = telemetry_attachment_key(strategy_id, account_id);
            if (std::find(keys.begin(), keys.end(), key) == keys.end()) {
                keys.push_back(key);
            }
        }
    }
    std::sort(keys.begin(), keys.end());
    return keys;
}

std::vector<std::string> collect_actual_live_attachment_keys(const itrader::IniFile& telemetry) {
    std::vector<std::string> keys;
    for (const auto& section_name : telemetry.sections_with_prefix("telemetry_attachment.")) {
        const auto tail = section_name.substr(std::string("telemetry_attachment.").size());
        const auto delimiter = tail.find('.');
        if (delimiter == std::string::npos) {
            continue;
        }
        const auto strategy_id = trim_copy(tail.substr(0, delimiter));
        const auto account_id = trim_copy(tail.substr(delimiter + 1));
        if (strategy_id.empty() || account_id.empty()) {
            continue;
        }
        const auto key = telemetry_attachment_key(strategy_id, account_id);
        if (std::find(keys.begin(), keys.end(), key) == keys.end()) {
            keys.push_back(key);
        }
    }
    std::sort(keys.begin(), keys.end());
    return keys;
}

struct LiveTelemetryCompatibility {
    bool usable {true};
    std::vector<std::string> warnings;
};

struct LiveAccountConnectivityStatus {
    bool known {false};
    bool trader_connected {false};
    bool market_data_connected {false};
};

bool parse_ini_bool(std::string_view raw, bool default_value = false) {
    const auto normalized = lower_copy(trim_copy(raw));
    if (normalized == "1" || normalized == "true" || normalized == "yes" || normalized == "on") {
        return true;
    }
    if (normalized == "0" || normalized == "false" || normalized == "no" || normalized == "off") {
        return false;
    }
    return default_value;
}

LiveTelemetryCompatibility assess_live_telemetry_compatibility(
    const std::filesystem::path& config_path,
    const itrader::IniFile& ini,
    const std::vector<std::string>& strategy_sections) {

    LiveTelemetryCompatibility compatibility;
    const auto telemetry_path = live_telemetry_path(config_path);
    if (!std::filesystem::exists(telemetry_path)) {
        return compatibility;
    }

    const auto expected_keys = collect_expected_live_attachment_keys(ini, strategy_sections);
    std::vector<std::string> actual_keys;
    std::string updated_at;

    constexpr int max_attempts = 4;
    constexpr auto retry_delay = std::chrono::milliseconds(40);
    for (int attempt = 0; attempt < max_attempts; ++attempt) {
        try {
            const auto telemetry = itrader::IniFile::parse(telemetry_path);
            actual_keys = collect_actual_live_attachment_keys(telemetry);
            updated_at = trim_copy(telemetry.get("telemetry", "updated_at"));
            if (expected_keys == actual_keys) {
                return compatibility;
            }
        } catch (const std::exception&) {
            actual_keys.clear();
            updated_at.clear();
        }

        if (attempt + 1 < max_attempts) {
            std::this_thread::sleep_for(retry_delay);
        }
    }

    compatibility.usable = false;
    compatibility.warnings.push_back(
        "Live telemetry file appears stale for the requested config and was ignored. Expected attachments ["
        + join_strings(expected_keys, ", ") + "] but found ["
        + join_strings(actual_keys, ", ") + "]"
        + (updated_at.empty() ? std::string {} : " (telemetry updated_at=" + updated_at + ")")
        + '.');
    return compatibility;
}

std::map<std::string, LiveAccountConnectivityStatus> load_live_account_connectivity(const std::filesystem::path& config_path) {
    std::map<std::string, LiveAccountConnectivityStatus> connectivity_by_account;
    const auto telemetry_path = live_telemetry_path(config_path);
    if (!std::filesystem::exists(telemetry_path)) {
        return connectivity_by_account;
    }

    constexpr int max_attempts = 4;
    constexpr auto retry_delay = std::chrono::milliseconds(40);
    for (int attempt = 0; attempt < max_attempts; ++attempt) {
        try {
            const auto telemetry = itrader::IniFile::parse(telemetry_path);
            for (const auto& section : telemetry.sections_with_prefix("telemetry_account.")) {
                const auto account_id = trim_copy(section.substr(std::string("telemetry_account.").size()));
                if (account_id.empty()) {
                    continue;
                }

                const auto trader_raw = trim_copy(telemetry.get(section, "trader_connected"));
                const auto market_data_raw = trim_copy(telemetry.get(section, "market_data_connected"));
                auto& status = connectivity_by_account[account_id];
                status.known = !trader_raw.empty() || !market_data_raw.empty();
                status.trader_connected = parse_ini_bool(trader_raw, false);
                status.market_data_connected = parse_ini_bool(market_data_raw, false);
            }
            return connectivity_by_account;
        } catch (const std::exception&) {
            connectivity_by_account.clear();
        }

        if (attempt + 1 < max_attempts) {
            std::this_thread::sleep_for(retry_delay);
        }
    }

    return connectivity_by_account;
}

struct ChartBarPoint {
    long long time {0};
    double open {0.0};
    double high {0.0};
    double low {0.0};
    double close {0.0};
};

struct ChartIndicatorPoint {
    long long time {0};
    double value {0.0};
};

struct ChartIndicatorSeriesPayload {
    std::string indicator_id;
    std::string label;
    std::string color;
    std::string strategy_id;
    std::string account_id;
    std::vector<ChartIndicatorPoint> points;
};

struct ChartSignalPoint {
    long long time {0};
    double price {0.0};
    std::string position;
    std::string color;
    std::string shape;
    std::string text;
    std::string strategy_id;
    std::string account_id;
};

struct ChartInstrumentPayload {
    std::string instrument;
    std::vector<ChartBarPoint> bars;
    std::vector<ChartIndicatorSeriesPayload> indicator_series;
    std::vector<ChartSignalPoint> signals;
    std::vector<std::string> warnings;
};

struct ChartPayload {
    std::string instrument;
    std::string default_instrument;
    std::string source {"runtime-snapshot"};
    std::vector<ChartBarPoint> bars;
    std::vector<ChartIndicatorSeriesPayload> indicator_series;
    std::vector<ChartSignalPoint> signals;
    std::vector<std::string> warnings;
    std::vector<std::string> account_ids;
    std::vector<ChartInstrumentPayload> instruments;
};

std::mutex g_live_chart_bars_cache_mutex;
std::map<std::string, std::map<std::string, std::vector<ChartBarPoint>>> g_live_chart_bars_cache;

std::vector<std::string> collect_chart_instruments(const itrader::IniFile& ini, const std::vector<std::string>& strategy_sections) {
    std::vector<std::string> instruments;
    for (const auto& section_name : strategy_sections) {
        for (const auto& instrument : read_strategy_instruments(ini, section_name)) {
            if (std::find(instruments.begin(), instruments.end(), instrument) == instruments.end()) {
                instruments.push_back(instrument);
            }
        }
    }

    return instruments;
}

std::vector<std::string> collect_chart_account_ids(const std::vector<std::string>& account_sections) {
    std::vector<std::string> account_ids;
    account_ids.reserve(account_sections.size());
    for (const auto& section_name : account_sections) {
        const auto separator = section_name.find('.');
        account_ids.push_back(separator == std::string::npos ? section_name : section_name.substr(separator + 1));
    }
    return account_ids;
}

void populate_backtest_chart_bars(ChartInstrumentPayload& chart_instrument, const std::vector<itrader::MarketTick>& ticks) {
    double previous_close = 0.0;
    bool has_previous_close = false;
    for (const auto& tick : ticks) {
        if (tick.instrument != chart_instrument.instrument) {
            continue;
        }

        const auto epoch = parse_timestamp_to_epoch(tick.timestamp);
        if (!epoch.has_value()) {
            continue;
        }

        if (!chart_instrument.bars.empty() && chart_instrument.bars.back().time == *epoch) {
            auto& existing = chart_instrument.bars.back();
            existing.high = std::max({existing.high, tick.last, tick.ask > 0.0 ? tick.ask : tick.last});
            existing.low = std::min({existing.low, tick.last, tick.bid > 0.0 ? tick.bid : tick.last});
            existing.close = tick.last;
            previous_close = existing.close;
            has_previous_close = true;
            continue;
        }

        ChartBarPoint bar;
        bar.time = *epoch;
        bar.open = has_previous_close ? previous_close : tick.last;
        bar.high = std::max({bar.open, tick.last, tick.ask > 0.0 ? tick.ask : tick.last});
        bar.low = std::min({bar.open, tick.last, tick.bid > 0.0 ? tick.bid : tick.last});
        bar.close = tick.last;
        chart_instrument.bars.push_back(bar);

        previous_close = bar.close;
        has_previous_close = true;
    }
}

bool is_dry_run_blocked_order(const itrader::RuntimeOrderSnapshot& order) {
    return order.status == itrader::OrderStatus::Rejected
        && order.filled_volume == 0
        && order.message.find("Dry run blocked live order") != std::string::npos
        && order.limit_price > 0.0;
}

bool should_emit_chart_signal(const itrader::RuntimeOrderSnapshot& order) {
    if ((order.status == itrader::OrderStatus::Filled || order.status == itrader::OrderStatus::PartiallyFilled)
        && order.filled_volume > 0
        && (order.filled_price > 0.0 || order.limit_price > 0.0)) {
        return true;
    }

    return is_dry_run_blocked_order(order);
}

double chart_signal_price(const itrader::RuntimeOrderSnapshot& order) {
    return order.filled_price > 0.0 ? order.filled_price : order.limit_price;
}

std::optional<long long> parse_order_signal_epoch(
    const itrader::RuntimeSnapshot& runtime_snapshot,
    const itrader::RuntimeOrderSnapshot& order) {
    static constexpr long long kMinimumPlausibleSignalTimeMs = 946'684'800'000LL;
    if (order.signal_time_ms >= kMinimumPlausibleSignalTimeMs) {
        return order.signal_time_ms / 1000LL;
    }

    if (runtime_snapshot.mode == itrader::Mode::Live) {
        const auto chart_epoch = parse_wall_clock_as_chart_epoch(order.timestamp);
        if (chart_epoch.has_value()) {
            return chart_epoch;
        }
    }

    return parse_timestamp_to_epoch(order.timestamp);
}

void populate_chart_signals(ChartInstrumentPayload& chart_instrument, const itrader::RuntimeSnapshot& runtime_snapshot) {
    const auto signal_bucket_seconds = std::max(1, runtime_snapshot.chart_bar_seconds);
    std::vector<std::string> seen_keys;
    for (const auto& attachment : runtime_snapshot.strategy_attachments) {
        for (const auto& order : attachment.closed_orders) {
            if (order.instrument != chart_instrument.instrument || !should_emit_chart_signal(order)) {
                continue;
            }

            const auto epoch = parse_order_signal_epoch(runtime_snapshot, order);
            if (!epoch.has_value()) {
                continue;
            }

            const std::string dedupe_key = order.order_id + '|' + order.source_order_id + '|' + order.timestamp + '|'
                + order.strategy_id + '|' + order.account_id + '|' + itrader::to_string(order.side) + '|' + itrader::to_string(order.offset);
            if (std::find(seen_keys.begin(), seen_keys.end(), dedupe_key) != seen_keys.end()) {
                continue;
            }
            seen_keys.push_back(dedupe_key);

            ChartSignalPoint signal;
            signal.time = *epoch - (*epoch % signal_bucket_seconds);
            signal.price = chart_signal_price(order);
            signal.position = order.side == itrader::Side::Sell ? "aboveBar" : "belowBar";
            signal.color = order.side == itrader::Side::Sell ? "#fb7185" : "#34d399";
            signal.shape = order.side == itrader::Side::Sell ? "arrowDown" : "arrowUp";
            signal.strategy_id = order.strategy_id;
            signal.account_id = order.account_id;
            signal.text = order.strategy_id + " " + itrader::to_string(order.side) + " " + itrader::to_string(order.offset) + " @ " + format_decimal(signal.price);
            chart_instrument.signals.push_back(std::move(signal));
        }
    }

    std::sort(chart_instrument.signals.begin(), chart_instrument.signals.end(), [](const ChartSignalPoint& left, const ChartSignalPoint& right) {
        return left.time < right.time;
    });
}

void append_chart_warning(std::vector<std::string>& warnings, const std::string& warning) {
    if (std::find(warnings.begin(), warnings.end(), warning) == warnings.end()) {
        warnings.push_back(warning);
    }
}

std::vector<ChartBarPoint> merge_chart_bar_points(
    const std::vector<ChartBarPoint>& cached_bars,
    const std::vector<ChartBarPoint>& current_bars,
    std::size_t max_bars) {

    std::map<long long, ChartBarPoint> bars_by_time;
    for (const auto& bar : cached_bars) {
        if (bar.time > 0) {
            bars_by_time[bar.time] = bar;
        }
    }
    for (const auto& bar : current_bars) {
        if (bar.time <= 0) {
            continue;
        }
        auto it = bars_by_time.find(bar.time);
        if (it == bars_by_time.end()) {
            bars_by_time.emplace(bar.time, bar);
            continue;
        }

        auto& merged = it->second;
        if (merged.open <= 0.0) {
            merged.open = bar.open;
        }
        merged.high = std::max(merged.high, bar.high);
        merged.low = merged.low > 0.0 ? std::min(merged.low, bar.low) : bar.low;
        merged.close = bar.close;
    }

    std::vector<ChartBarPoint> merged_bars;
    merged_bars.reserve(bars_by_time.size());
    for (const auto& [_, bar] : bars_by_time) {
        merged_bars.push_back(bar);
    }

    if (max_bars > 0 && merged_bars.size() > max_bars) {
        merged_bars.erase(
            merged_bars.begin(),
            merged_bars.begin() + static_cast<std::ptrdiff_t>(merged_bars.size() - max_bars));
    }

    return merged_bars;
}

std::map<std::string, std::vector<ChartBarPoint>> load_live_chart_bars(const std::filesystem::path& config_path) {
    const auto telemetry_path = live_telemetry_path(config_path);
    if (!std::filesystem::exists(telemetry_path)) {
        throw std::runtime_error("Live telemetry file not found at " + telemetry_path.string());
    }

    const auto telemetry = itrader::IniFile::parse(telemetry_path);
    std::map<std::string, std::vector<ChartBarPoint>> bars_by_instrument;
    for (const auto& section : telemetry.sections_with_prefix("telemetry_chart_bar.")) {
        const auto instrument = trim_copy(telemetry.get(section, "instrument"));
        if (instrument.empty()) {
            continue;
        }

        const auto epoch = parse_timestamp_to_epoch(telemetry.get(section, "timestamp"));
        if (!epoch.has_value()) {
            continue;
        }

        ChartBarPoint bar;
        bar.time = *epoch;
        bar.open = telemetry.get_double(section, "open", 0.0);
        bar.high = telemetry.get_double(section, "high", bar.open);
        bar.low = telemetry.get_double(section, "low", bar.open);
        bar.close = telemetry.get_double(section, "close", bar.open);
        bars_by_instrument[instrument].push_back(bar);
    }

    for (auto& [_, bars] : bars_by_instrument) {
        bars = merge_chart_bar_points({}, bars, 0);
    }

    {
        const auto cache_key = config_path.generic_string();
        std::scoped_lock guard(g_live_chart_bars_cache_mutex);
        auto& cached_by_instrument = g_live_chart_bars_cache[cache_key];

        for (auto& [instrument, bars] : bars_by_instrument) {
            auto cached_it = cached_by_instrument.find(instrument);
            if (cached_it == cached_by_instrument.end() || cached_it->second.empty()) {
                cached_by_instrument[instrument] = bars;
                continue;
            }

            const auto& cached_bars = cached_it->second;
            const auto severe_shrink_threshold = std::max<std::size_t>(2, cached_bars.size() * 35 / 100);
            const bool severe_shrink = cached_bars.size() >= 3 && bars.size() < severe_shrink_threshold;
            const auto latest_current_time = bars.empty() ? 0 : bars.back().time;
            const auto latest_cached_time = cached_bars.empty() ? 0 : cached_bars.back().time;
            const bool looks_like_partial_snapshot = severe_shrink && latest_current_time <= latest_cached_time;
            const bool looks_like_live_restart_snapshot = severe_shrink && latest_current_time > latest_cached_time;

            if (looks_like_partial_snapshot) {
                bars = cached_bars;
            } else if (looks_like_live_restart_snapshot) {
                bars = merge_chart_bar_points(cached_bars, bars, std::max(cached_bars.size(), bars.size()));
                cached_it->second = bars;
            } else {
                cached_it->second = bars;
            }
        }

        if (bars_by_instrument.empty() && !cached_by_instrument.empty()) {
            bars_by_instrument = cached_by_instrument;
        } else {
            for (const auto& [instrument, cached_bars] : cached_by_instrument) {
                if (!bars_by_instrument.contains(instrument)) {
                    bars_by_instrument[instrument] = cached_bars;
                }
            }
        }
    }

    return bars_by_instrument;
}

std::map<std::string, std::vector<ChartIndicatorSeriesPayload>> load_live_chart_indicator_series(const std::filesystem::path& config_path) {
    const auto telemetry_path = live_telemetry_path(config_path);
    if (!std::filesystem::exists(telemetry_path)) {
        throw std::runtime_error("Live telemetry file not found at " + telemetry_path.string());
    }

    const auto telemetry = itrader::IniFile::parse(telemetry_path);
    std::map<std::string, ChartIndicatorSeriesPayload> series_by_key;
    for (const auto& section : telemetry.sections_with_prefix("telemetry_chart_indicator.")) {
        const auto instrument = trim_copy(telemetry.get(section, "instrument"));
        const auto indicator_id = trim_copy(telemetry.get(section, "indicator_id"));
        if (instrument.empty() || indicator_id.empty()) {
            continue;
        }

        const auto epoch = parse_timestamp_to_epoch(telemetry.get(section, "timestamp"));
        if (!epoch.has_value()) {
            continue;
        }

        const auto strategy_id = trim_copy(telemetry.get(section, "strategy_id"));
        const auto account_id = trim_copy(telemetry.get(section, "account_id"));
        const auto series_key = instrument + '|' + strategy_id + '|' + account_id + '|' + indicator_id;
        auto& payload = series_by_key[series_key];
        payload.indicator_id = indicator_id;
        payload.label = trim_copy(telemetry.get(section, "label", indicator_id));
        payload.color = trim_copy(telemetry.get(section, "color"));
        payload.strategy_id = strategy_id;
        payload.account_id = account_id;
        payload.points.push_back(ChartIndicatorPoint {
            .time = *epoch,
            .value = telemetry.get_double(section, "value", 0.0),
        });
    }

    std::map<std::string, std::vector<ChartIndicatorSeriesPayload>> series_by_instrument;
    for (auto& [key, payload] : series_by_key) {
        std::map<long long, ChartIndicatorPoint> points_by_time;
        for (const auto& point : payload.points) {
            if (point.time > 0) {
                points_by_time[point.time] = point;
            }
        }
        payload.points.clear();
        payload.points.reserve(points_by_time.size());
        for (const auto& [_, point] : points_by_time) {
            payload.points.push_back(point);
        }
        if (payload.points.empty()) {
            continue;
        }
        const auto delimiter = key.find('|');
        if (delimiter == std::string::npos) {
            continue;
        }
        series_by_instrument[key.substr(0, delimiter)].push_back(std::move(payload));
    }

    for (auto& [_, series] : series_by_instrument) {
        std::sort(series.begin(), series.end(), [](const ChartIndicatorSeriesPayload& left, const ChartIndicatorSeriesPayload& right) {
            if (left.strategy_id != right.strategy_id) {
                return left.strategy_id < right.strategy_id;
            }
            if (left.account_id != right.account_id) {
                return left.account_id < right.account_id;
            }
            return left.indicator_id < right.indicator_id;
        });
    }

    return series_by_instrument;
}

ChartPayload build_chart_payload(
    const std::filesystem::path& config_path,
    const itrader::IniFile& ini,
    std::string_view normalized_mode,
    bool enable_backtest_replay,
    bool include_backtest_chart,
    bool allow_live_telemetry,
    const std::optional<itrader::RuntimeSnapshot>& runtime_snapshot,
    const std::vector<std::string>& runtime_warnings,
    const std::vector<std::string>& strategy_sections,
    const std::vector<std::string>& account_sections) {

    ChartPayload chart;
    chart.source = normalized_mode == "live"
        ? "live-telemetry"
        : (enable_backtest_replay
            ? (include_backtest_chart ? "backtest-replay" : "backtest-summary")
            : "backtest-preview");
    chart.warnings = runtime_warnings;
    chart.account_ids = collect_chart_account_ids(account_sections);

    const auto instruments = collect_chart_instruments(ini, strategy_sections);
    if (instruments.empty()) {
        append_chart_warning(chart.warnings, "No instrument list is configured yet, so the chart has no market series to display.");
        return chart;
    }

    std::vector<itrader::MarketTick> ticks;
    std::map<std::string, std::vector<ChartBarPoint>> live_bars_by_instrument;
    std::map<std::string, std::vector<ChartIndicatorSeriesPayload>> live_indicator_series_by_instrument;
    const bool use_runtime_snapshot_chart = normalized_mode == "backtest"
        && runtime_snapshot.has_value()
        && !runtime_snapshot->chart_instruments.empty();
    if (normalized_mode == "backtest") {
        if (!enable_backtest_replay) {
            append_chart_warning(chart.warnings, "Backtest chart replay is skipped during dashboard hydration to keep the UI responsive. Click Run on an attached strategy to compute bars and signal markers for the current config.");
        } else if (!include_backtest_chart) {
            append_chart_warning(chart.warnings, "Summary replay intentionally skipped chart reconstruction to keep the UI responsive. Click Run on an attached strategy to compute replay bars and signal markers for the current config.");
        } else if (!use_runtime_snapshot_chart) {
            try {
                ticks = load_backtest_ticks(config_path, ini, strategy_sections);
            } catch (const std::exception& ex) {
                append_chart_warning(chart.warnings, std::string("Chart replay data is unavailable from the configured backtest data source: ") + ex.what());
            }
        }
    } else {
        if (!allow_live_telemetry) {
            append_chart_warning(chart.warnings, "Live chart telemetry was ignored because the shared telemetry file does not match the requested config.");
        } else {
            try {
                live_bars_by_instrument = load_live_chart_bars(config_path);
                live_indicator_series_by_instrument = load_live_chart_indicator_series(config_path);
            } catch (const std::exception& ex) {
                append_chart_warning(chart.warnings, std::string("Live chart telemetry is unavailable: ") + ex.what());
            }
        }
    }

    for (const auto& instrument : instruments) {
        ChartInstrumentPayload chart_instrument;
        chart_instrument.instrument = instrument;

        if (normalized_mode == "backtest") {
            if (enable_backtest_replay && use_runtime_snapshot_chart) {
                const auto runtime_chart_it = std::find_if(
                    runtime_snapshot->chart_instruments.begin(),
                    runtime_snapshot->chart_instruments.end(),
                    [&instrument](const itrader::RuntimeChartInstrumentSnapshot& entry) {
                        return entry.instrument == instrument;
                    });
                if (runtime_chart_it != runtime_snapshot->chart_instruments.end()) {
                    for (const auto& runtime_bar : runtime_chart_it->bars) {
                        chart_instrument.bars.push_back(ChartBarPoint {
                            .time = runtime_bar.time,
                            .open = runtime_bar.open,
                            .high = runtime_bar.high,
                            .low = runtime_bar.low,
                            .close = runtime_bar.close,
                        });
                    }
                    for (const auto& runtime_series : runtime_chart_it->indicator_series) {
                        ChartIndicatorSeriesPayload series;
                        series.indicator_id = runtime_series.indicator_id;
                        series.label = runtime_series.label.empty() ? runtime_series.indicator_id : runtime_series.label;
                        series.color = runtime_series.color;
                        series.strategy_id = runtime_series.strategy_id;
                        series.account_id = runtime_series.account_id;
                        for (const auto& point : runtime_series.points) {
                            series.points.push_back(ChartIndicatorPoint {.time = point.time, .value = point.value});
                        }
                        chart_instrument.indicator_series.push_back(std::move(series));
                    }
                }
                if (chart_instrument.bars.empty()) {
                    chart_instrument.warnings.push_back("The replay did not produce chart bars for instrument " + chart_instrument.instrument + ".");
                }
            } else if (enable_backtest_replay && include_backtest_chart) {
                populate_backtest_chart_bars(chart_instrument, ticks);
                if (chart_instrument.bars.empty()) {
                    chart_instrument.warnings.push_back("The configured AGTICK backtest file did not produce any chart bars for instrument " + chart_instrument.instrument + ".");
                }
            } else {
                chart_instrument.warnings.push_back(enable_backtest_replay
                    ? "Summary replay is using a lightweight synthetic chart preview. Click Run to compute replay bars and signal markers."
                    : "Dashboard preview is using a lightweight synthetic chart until you explicitly run the backtest.");
            }
        } else {
            const auto live_bars_it = live_bars_by_instrument.find(chart_instrument.instrument);
            if (live_bars_it != live_bars_by_instrument.end()) {
                chart_instrument.bars = live_bars_it->second;
            }
            const auto live_indicator_it = live_indicator_series_by_instrument.find(chart_instrument.instrument);
            if (live_indicator_it != live_indicator_series_by_instrument.end()) {
                chart_instrument.indicator_series = live_indicator_it->second;
            }
            if (chart_instrument.bars.empty()) {
                chart_instrument.warnings.push_back("Live chart telemetry does not yet contain candles for instrument " + chart_instrument.instrument + ".");
            }
        }

        if (runtime_snapshot.has_value()) {
            populate_chart_signals(chart_instrument, *runtime_snapshot);
        }

        chart.instruments.push_back(chart_instrument);
    }

    if (!chart.instruments.empty()) {
        chart.default_instrument = chart.instruments.front().instrument;
        chart.instrument = chart.default_instrument;
        chart.bars = chart.instruments.front().bars;
        chart.indicator_series = chart.instruments.front().indicator_series;
        chart.signals = chart.instruments.front().signals;
        if (chart.warnings.empty()) {
            chart.warnings = chart.instruments.front().warnings;
        }
    }

    return chart;
}

void append_string_array(std::ostringstream& json, const std::vector<std::string>& values) {
    json << '[';
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index > 0) {
            json << ',';
        }
        json << quoted(values[index]);
    }
    json << ']';
}

std::string make_live_inventory_json_from_config_path(const std::filesystem::path& config_path);

itrader::IniFile parse_ini_body(const std::filesystem::path& workspace_root, std::string_view body, std::string_view name_prefix) {
    const auto runtime_root = workspace_root / "runtime";
    std::filesystem::create_directories(runtime_root);
    const auto temp_path = runtime_root / (std::string(name_prefix) + "_" + std::to_string(current_time_millis()) + ".ini");
    write_text_file(temp_path, std::string(body));
    try {
        auto ini = itrader::IniFile::parse(temp_path);
        std::error_code remove_error;
        std::filesystem::remove(temp_path, remove_error);
        return ini;
    } catch (...) {
        std::error_code remove_error;
        std::filesystem::remove(temp_path, remove_error);
        throw;
    }
}

bool path_is_under(const std::filesystem::path& child, const std::filesystem::path& parent) {
    const auto child_text = lower_copy(child.lexically_normal().generic_string());
    auto parent_text = lower_copy(parent.lexically_normal().generic_string());
    if (!parent_text.empty() && parent_text.back() != '/') {
        parent_text.push_back('/');
    }
    return child_text.rfind(parent_text, 0) == 0;
}

std::filesystem::path resolve_editable_inventory_store_path(
    const std::filesystem::path& workspace_root,
    const std::string& raw_path) {

    const auto trimmed = trim_copy(raw_path);
    if (trimmed.empty()) {
        throw std::runtime_error("Persisted inventory edit is missing store_path.");
    }

    std::filesystem::path candidate = std::filesystem::path(trimmed);
    if (candidate.is_relative()) {
        candidate = workspace_root / candidate;
    }

    std::error_code canonical_error;
    const auto canonical_candidate = std::filesystem::weakly_canonical(candidate, canonical_error);
    if (canonical_error) {
        throw std::runtime_error("Unable to resolve persisted inventory store path: " + candidate.generic_string());
    }

    if (lower_copy(canonical_candidate.filename().generic_string()) != "strategy_inventory_store.ini") {
        throw std::runtime_error("Persisted inventory store edits can only target strategy_inventory_store.ini files.");
    }

    const auto runtime_root = std::filesystem::weakly_canonical(workspace_root / "runtime", canonical_error);
    if (canonical_error || !path_is_under(canonical_candidate, runtime_root)) {
        throw std::runtime_error("Persisted inventory store path is outside the workspace runtime directory.");
    }

    if (!std::filesystem::exists(canonical_candidate)) {
        throw std::runtime_error("Persisted inventory store does not exist: " + canonical_candidate.generic_string());
    }

    return canonical_candidate;
}

std::string section_name_from_line(std::string_view raw_line) {
    const auto trimmed = trim_copy(raw_line);
    if (trimmed.size() >= 2 && trimmed.front() == '[' && trimmed.back() == ']') {
        return trim_copy(trimmed.substr(1, trimmed.size() - 2));
    }
    return {};
}

std::optional<std::string> key_name_from_line(std::string_view raw_line) {
    const auto trimmed = trim_copy(raw_line);
    if (trimmed.empty() || trimmed.front() == '#' || trimmed.front() == ';') {
        return std::nullopt;
    }
    const auto delimiter = trimmed.find('=');
    if (delimiter == std::string_view::npos) {
        return std::nullopt;
    }
    return trim_copy(trimmed.substr(0, delimiter));
}

void write_ini_section_values(
    const std::filesystem::path& file_path,
    const std::string& section_name,
    const std::map<std::string, std::string>& values) {

    std::vector<std::string> lines;
    if (std::filesystem::exists(file_path)) {
        std::ifstream input(file_path);
        if (!input.is_open()) {
            throw std::runtime_error("Unable to open inventory store for editing: " + file_path.generic_string());
        }
        std::string line;
        while (std::getline(input, line)) {
            lines.push_back(line);
        }
    }

    std::ostringstream output;
    std::string current_section;
    bool in_target = false;
    bool found_section = false;
    std::set<std::string> written_keys;

    const auto append_missing_values = [&]() {
        for (const auto& [key, value] : values) {
            if (!written_keys.contains(key)) {
                output << key << '=' << value << '\n';
                written_keys.insert(key);
            }
        }
    };

    for (const auto& line : lines) {
        const auto parsed_section = section_name_from_line(line);
        if (!parsed_section.empty()) {
            if (in_target) {
                append_missing_values();
            }
            current_section = parsed_section;
            in_target = current_section == section_name;
            if (in_target) {
                found_section = true;
                written_keys.clear();
            }
            output << line << '\n';
            continue;
        }

        if (in_target) {
            const auto key = key_name_from_line(line);
            if (key.has_value() && values.contains(*key)) {
                output << *key << '=' << values.at(*key) << '\n';
                written_keys.insert(*key);
                continue;
            }
        }

        output << line << '\n';
    }

    if (in_target) {
        append_missing_values();
    }

    if (!found_section) {
        if (!lines.empty()) {
            output << '\n';
        }
        output << '[' << section_name << "]\n";
        written_keys.clear();
        append_missing_values();
    }

    write_text_file(file_path, output.str());
}

std::string resolve_inventory_state_section_name(
    const std::filesystem::path& store_path,
    const std::string& strategy_id,
    const std::string& account_id,
    const std::string& instrument) {

    const std::string prefix = "strategy_inventory_state.";
    const auto requested_section = prefix + strategy_id + '.' + account_id + '.' + instrument;
    if (!std::filesystem::exists(store_path)) {
        return requested_section;
    }

    const auto requested_instrument = lower_copy(instrument);
    const auto store_ini = itrader::IniFile::parse(store_path);
    for (const auto& section : store_ini.sections_with_prefix(prefix)) {
        const auto tail = section.substr(prefix.size());
        const auto first_delimiter = tail.find('.');
        const auto second_delimiter = first_delimiter == std::string::npos ? std::string::npos : tail.find('.', first_delimiter + 1);
        if (first_delimiter == std::string::npos || second_delimiter == std::string::npos) {
            continue;
        }

        const auto section_strategy_id = trim_copy(std::string_view {tail}.substr(0, first_delimiter));
        const auto section_account_id = trim_copy(std::string_view {tail}.substr(first_delimiter + 1, second_delimiter - first_delimiter - 1));
        const auto section_instrument = trim_copy(std::string_view {tail}.substr(second_delimiter + 1));
        if (section_strategy_id == strategy_id
            && section_account_id == account_id
            && lower_copy(section_instrument) == requested_instrument) {
            return section;
        }
    }

    return requested_section;
}

std::string save_live_inventory_store_edits_json(
    const std::filesystem::path& workspace_root,
    const std::filesystem::path& config_path,
    std::string_view body) {

    const auto edits_ini = parse_ini_body(workspace_root, body, "live_inventory_store_edits");
    const auto sections = edits_ini.sections_with_prefix("persisted_inventory_position.");
    std::set<std::filesystem::path> touched_stores;

    for (const auto& section : sections) {
        const auto store_path = resolve_editable_inventory_store_path(workspace_root, edits_ini.get(section, "store_path"));
        const auto strategy_id = trim_copy(edits_ini.get(section, "strategy_id"));
        const auto account_id = trim_copy(edits_ini.get(section, "account_id"));
        const auto instrument = trim_copy(edits_ini.get(section, "instrument"));
        if (strategy_id.empty() || account_id.empty() || instrument.empty()) {
            throw std::runtime_error("Persisted inventory edit section [" + section + "] is missing strategy_id, account_id, or instrument.");
        }

        const auto integer_value = [&](const std::string& key) {
            const int value = edits_ini.get_int(section, key, 0);
            if (value < 0) {
                throw std::runtime_error("Persisted inventory edit [" + section + "] has a negative value for " + key + ".");
            }
            return std::to_string(value);
        };
        const auto price_value = [&](const std::string& key) {
            const auto raw = trim_copy(edits_ini.get(section, key, "0"));
            if (raw.empty()) {
                return std::string {"0.00"};
            }
            return format_decimal(std::stod(raw));
        };

        const auto inventory_section = resolve_inventory_state_section_name(store_path, strategy_id, account_id, instrument);
        write_ini_section_values(
            store_path,
            inventory_section,
            {
                {"long_today_quantity", integer_value("long_today_quantity")},
                {"long_today_average_price", price_value("long_average_price")},
                {"long_yesterday_quantity", integer_value("long_yesterday_quantity")},
                {"long_yesterday_average_price", price_value("long_average_price")},
                {"short_today_quantity", integer_value("short_today_quantity")},
                {"short_today_average_price", price_value("short_average_price")},
                {"short_yesterday_quantity", integer_value("short_yesterday_quantity")},
                {"short_yesterday_average_price", price_value("short_average_price")}
            });
        touched_stores.insert(store_path);
    }

    for (const auto& store_path : touched_stores) {
        write_ini_section_values(
            store_path,
            "strategy_inventory_store",
            {{"updated_at", current_timestamp()}});
    }

    return make_live_inventory_json_from_config_path(config_path);
}

std::string make_live_inventory_json_from_config_path(const std::filesystem::path& config_path) {
    const auto adjustments_path = strategy_inventory_adjustments_path(config_path);
    const auto store_path = strategy_inventory_store_path(config_path);

    std::optional<itrader::IniFile> adjustments_ini;
    if (std::filesystem::exists(adjustments_path)) {
        adjustments_ini = itrader::IniFile::parse(adjustments_path);
    }

    std::optional<itrader::IniFile> store_ini;
    if (std::filesystem::exists(store_path)) {
        store_ini = itrader::IniFile::parse(store_path);
    }

    std::vector<std::string> warnings;
    if (!std::filesystem::exists(adjustments_path)) {
        warnings.push_back("Manual inventory adjustment file does not exist yet. Saving from the dashboard will create it.");
    }
    if (!std::filesystem::exists(store_path)) {
        warnings.push_back("Strategy inventory store does not exist yet. Start the live runtime once to populate reconciliation snapshots.");
    }

    std::ostringstream json;
    json << '{';
    json << "\"config_path\":" << quoted(config_path.generic_string()) << ',';
    json << "\"adjustments_path\":" << quoted(adjustments_path.generic_string()) << ',';
    json << "\"store_path\":" << quoted(store_path.generic_string()) << ',';
    json << "\"adjustments_exists\":" << (std::filesystem::exists(adjustments_path) ? "true" : "false") << ',';
    json << "\"store_exists\":" << (std::filesystem::exists(store_path) ? "true" : "false") << ',';
    json << "\"store_updated_at\":" << quoted(store_ini.has_value() ? store_ini->get("strategy_inventory_store", "updated_at") : std::string {}) << ',';

    json << "\"persisted_positions\":[";
    bool first_position = true;
    const auto append_persisted_positions = [&](const std::filesystem::path& source_store_path, const itrader::IniFile& source_store_ini) {
        const auto sections = source_store_ini.sections_with_prefix("strategy_inventory_state.");
        for (const auto& section : sections) {
            const auto tail = section.substr(std::string("strategy_inventory_state.").size());
            const auto first_delimiter = tail.find('.');
            const auto second_delimiter = first_delimiter == std::string::npos ? std::string::npos : tail.find('.', first_delimiter + 1);
            if (first_delimiter == std::string::npos || second_delimiter == std::string::npos) {
                continue;
            }
            const auto strategy_id = trim_copy(std::string_view {tail}.substr(0, first_delimiter));
            const auto account_id = trim_copy(std::string_view {tail}.substr(first_delimiter + 1, second_delimiter - first_delimiter - 1));
            const auto instrument = trim_copy(std::string_view {tail}.substr(second_delimiter + 1));
            if (strategy_id.empty() || account_id.empty() || instrument.empty()) {
                continue;
            }

            const int long_today_quantity = source_store_ini.get_int(section, "long_today_quantity", 0);
            const int long_yesterday_quantity = source_store_ini.get_int(section, "long_yesterday_quantity", 0);
            const int short_today_quantity = source_store_ini.get_int(section, "short_today_quantity", 0);
            const int short_yesterday_quantity = source_store_ini.get_int(section, "short_yesterday_quantity", 0);
            const int long_quantity = long_today_quantity + long_yesterday_quantity;
            const int short_quantity = short_today_quantity + short_yesterday_quantity;

            const double long_today_average_price = source_store_ini.get_double(section, "long_today_average_price", 0.0);
            const double long_yesterday_average_price = source_store_ini.get_double(section, "long_yesterday_average_price", 0.0);
            const double short_today_average_price = source_store_ini.get_double(section, "short_today_average_price", 0.0);
            const double short_yesterday_average_price = source_store_ini.get_double(section, "short_yesterday_average_price", 0.0);
            const double long_average_price = long_quantity == 0
                ? 0.0
                : ((long_today_average_price * long_today_quantity) + (long_yesterday_average_price * long_yesterday_quantity)) / long_quantity;
            const double short_average_price = short_quantity == 0
                ? 0.0
                : ((short_today_average_price * short_today_quantity) + (short_yesterday_average_price * short_yesterday_quantity)) / short_quantity;

            if (!first_position) {
                json << ',';
            }
            first_position = false;
            json << '{'
                 << "\"store_path\":" << quoted(source_store_path.generic_string()) << ','
                 << "\"store_namespace\":" << quoted(source_store_path.parent_path().filename().generic_string()) << ','
                 << "\"strategy_id\":" << quoted(strategy_id) << ','
                 << "\"account_id\":" << quoted(account_id) << ','
                 << "\"instrument\":" << quoted(instrument) << ','
                 << "\"long_today_quantity\":" << long_today_quantity << ','
                 << "\"long_yesterday_quantity\":" << long_yesterday_quantity << ','
                 << "\"long_quantity\":" << long_quantity << ','
                 << "\"long_average_price\":" << quoted(format_decimal(long_average_price)) << ','
                 << "\"short_today_quantity\":" << short_today_quantity << ','
                 << "\"short_yesterday_quantity\":" << short_yesterday_quantity << ','
                 << "\"short_quantity\":" << short_quantity << ','
                 << "\"short_average_price\":" << quoted(format_decimal(short_average_price)) << ','
                 << "\"net\":" << (long_quantity - short_quantity)
                 << '}';
        }
    };

    if (store_ini.has_value()) {
        append_persisted_positions(store_path, *store_ini);
    }
    const auto runtime_root = store_path.parent_path().parent_path();
    const auto scoped_prefix = itrader::config_runtime_namespace(config_path) + "_dashboard_";
    if (std::filesystem::exists(runtime_root)) {
        for (const auto& entry : std::filesystem::directory_iterator(runtime_root)) {
            if (!entry.is_directory()) {
                continue;
            }
            const auto namespace_name = entry.path().filename().generic_string();
            if (namespace_name.rfind(scoped_prefix, 0) != 0) {
                continue;
            }
            const auto scoped_store_path = entry.path() / "strategy_inventory_store.ini";
            std::error_code equivalent_error;
            const bool same_as_base_store = std::filesystem::exists(store_path)
                && std::filesystem::equivalent(scoped_store_path, store_path, equivalent_error)
                && !equivalent_error;
            if (!std::filesystem::exists(scoped_store_path) || same_as_base_store) {
                continue;
            }
            append_persisted_positions(scoped_store_path, itrader::IniFile::parse(scoped_store_path));
        }
    }
    json << "],";

    json << "\"adjustments\":[";
    if (adjustments_ini.has_value()) {
        const auto sections = adjustments_ini->sections_with_prefix("inventory_adjustment.");
        for (std::size_t index = 0; index < sections.size(); ++index) {
            if (index > 0) {
                json << ',';
            }
            const auto& section = sections[index];
            const auto adjustment_id = trim_copy(section.substr(std::string("inventory_adjustment.").size()));
            const auto applied_section = "inventory_adjustments_applied." + adjustment_id;
            const bool applied = store_ini.has_value() && store_ini->has_section(applied_section);
            json << '{'
                 << "\"id\":" << quoted(adjustment_id) << ','
                 << "\"enabled\":" << (adjustments_ini->get_bool(section, "enabled", true) ? "true" : "false") << ','
                 << "\"account_id\":" << quoted(adjustments_ini->get(section, "account_id")) << ','
                 << "\"strategy_id\":" << quoted(adjustments_ini->get(section, "strategy_id")) << ','
                 << "\"instrument\":" << quoted(adjustments_ini->get(section, "instrument")) << ','
                 << "\"exchange\":" << quoted(adjustments_ini->get(section, "exchange")) << ','
                 << "\"operator_id\":" << quoted(adjustments_ini->get(section, "operator_id")) << ','
                 << "\"reason_code\":" << quoted(adjustments_ini->get(section, "reason_code")) << ','
                 << "\"reason_text\":" << quoted(adjustments_ini->get(section, "reason_text")) << ','
                 << "\"long_today_delta\":" << quoted(adjustments_ini->get(section, "long_today_delta", "0")) << ','
                 << "\"long_today_average_price\":" << quoted(adjustments_ini->get(section, "long_today_average_price")) << ','
                 << "\"long_yesterday_delta\":" << quoted(adjustments_ini->get(section, "long_yesterday_delta", "0")) << ','
                 << "\"long_yesterday_average_price\":" << quoted(adjustments_ini->get(section, "long_yesterday_average_price")) << ','
                 << "\"short_today_delta\":" << quoted(adjustments_ini->get(section, "short_today_delta", "0")) << ','
                 << "\"short_today_average_price\":" << quoted(adjustments_ini->get(section, "short_today_average_price")) << ','
                 << "\"short_yesterday_delta\":" << quoted(adjustments_ini->get(section, "short_yesterday_delta", "0")) << ','
                 << "\"short_yesterday_average_price\":" << quoted(adjustments_ini->get(section, "short_yesterday_average_price")) << ','
                 << "\"applied\":" << (applied ? "true" : "false") << ','
                 << "\"applied_at\":" << quoted(applied && store_ini.has_value() ? store_ini->get(applied_section, "applied_at") : std::string {})
                 << '}';
        }
    }
    json << "],";

    json << "\"reconciliations\":[";
    if (store_ini.has_value()) {
        const auto sections = store_ini->sections_with_prefix("reconciliation_runs.");
        for (std::size_t index = 0; index < sections.size(); ++index) {
            if (index > 0) {
                json << ',';
            }
            const auto& section = sections[index];
            const auto account_id = trim_copy(store_ini->get(section, "account_id", section.substr(std::string("reconciliation_runs.").size())));
            json << '{'
                 << "\"account_id\":" << quoted(account_id) << ','
                 << "\"aggregate_match\":" << (store_ini->get_bool(section, "aggregate_match", false) ? "true" : "false") << ','
                 << "\"applied_adjustment_count\":" << quoted(store_ini->get(section, "applied_adjustment_count", "0")) << ','
                 << "\"broker_snapshot_timestamp\":" << quoted(store_ini->get(section, "broker_snapshot_timestamp")) << ','
                 << "\"mismatch_summary\":" << quoted(store_ini->get(section, "mismatch_summary")) << ','
                 << "\"manual_adjustments_path\":" << quoted(store_ini->get(section, "manual_adjustments_path", adjustments_path.generic_string()))
                 << '}';
        }
    }
    json << "],";

    json << "\"warnings\":";
    append_string_array(json, warnings);
    json << '}';
    return json.str();
}

void append_runtime_positions(std::ostringstream& json, const std::vector<itrader::RuntimePositionSnapshot>& positions) {
    json << '[';
    for (std::size_t index = 0; index < positions.size(); ++index) {
        if (index > 0) {
            json << ',';
        }
        const auto& position = positions[index];
        json << '{'
             << "\"instrument\":" << quoted(position.instrument) << ','
             << "\"account_id\":" << quoted(position.account_id) << ','
             << "\"strategy_id\":" << quoted(position.strategy_id) << ','
                             << "\"long_today_quantity\":" << position.long_today_quantity << ','
                             << "\"long_yesterday_quantity\":" << position.long_yesterday_quantity << ','
               << "\"long_quantity\":" << position.long_quantity << ','
               << "\"long_average_price\":" << quoted(format_decimal(position.long_average_price)) << ','
                             << "\"short_today_quantity\":" << position.short_today_quantity << ','
                             << "\"short_yesterday_quantity\":" << position.short_yesterday_quantity << ','
               << "\"short_quantity\":" << position.short_quantity << ','
               << "\"short_average_price\":" << quoted(format_decimal(position.short_average_price)) << ','
             << "\"net\":" << position.net << ','
             << "\"average_price\":" << quoted(format_decimal(position.average_price))
             << '}';
    }
    json << ']';
}

void append_runtime_orders(std::ostringstream& json, const std::vector<itrader::RuntimeOrderSnapshot>& orders) {
    json << '[';
    for (std::size_t index = 0; index < orders.size(); ++index) {
        if (index > 0) {
            json << ',';
        }
        const auto& order = orders[index];
        json << '{'
             << "\"order_id\":" << quoted(order.order_id) << ','
             << "\"source_order_id\":" << quoted(order.source_order_id) << ','
             << "\"account_id\":" << quoted(order.account_id) << ','
             << "\"strategy_id\":" << quoted(order.strategy_id) << ','
             << "\"instrument\":" << quoted(order.instrument) << ','
               << "\"exchange\":" << quoted(order.exchange) << ','
             << "\"side\":" << quoted(itrader::to_string(order.side)) << ','
             << "\"offset\":" << quoted(itrader::to_string(order.offset)) << ','
             << "\"requested_volume\":" << order.requested_volume << ','
             << "\"filled_volume\":" << order.filled_volume << ','
             << "\"limit_price\":" << quoted(format_decimal(order.limit_price)) << ','
             << "\"filled_price\":" << quoted(format_decimal(order.filled_price)) << ','
             << "\"signal_time_ms\":" << order.signal_time_ms << ','
             << "\"status\":" << quoted(itrader::to_string(order.status)) << ','
             << "\"message\":" << quoted(order.message) << ','
             << "\"timestamp\":" << quoted(order.timestamp)
             << '}';
    }
    json << ']';
}

void append_runtime_detail(
    std::ostringstream& json,
    const itrader::StrategyAttachmentSnapshot* attachment,
    const LiveAccountConnectivityStatus* connectivity,
    const std::vector<std::string>& fallback_warnings,
    std::string_view detail_level) {

    const auto opened_order_count = attachment != nullptr ? attachment->opened_order_count : std::size_t {0};
    const auto closed_order_count = attachment != nullptr ? attachment->closed_order_count : std::size_t {0};
    const auto filled_trade_count = attachment != nullptr ? attachment->filled_trade_count : std::size_t {0};

    json << '{';
    json << "\"detail_level\":" << quoted(detail_level) << ',';
    json << "\"opened_order_count\":" << opened_order_count << ',';
    json << "\"closed_order_count\":" << closed_order_count << ',';
    json << "\"filled_trade_count\":" << filled_trade_count << ',';
    json << "\"connection_status_known\":" << (connectivity != nullptr && connectivity->known ? "true" : "false") << ',';
    json << "\"trader_connected\":" << (connectivity != nullptr && connectivity->known && connectivity->trader_connected ? "true" : "false") << ',';
    json << "\"market_data_connected\":" << (connectivity != nullptr && connectivity->known && connectivity->market_data_connected ? "true" : "false") << ',';
    if (attachment != nullptr) {
        json << "\"positions\":";
        append_runtime_positions(json, attachment->positions);
        json << ",\"opened_orders\":";
        append_runtime_orders(json, attachment->opened_orders);
        json << ",\"closed_orders\":";
        append_runtime_orders(json, attachment->closed_orders);
        json << ",\"warnings\":";
        append_string_array(json, attachment->warnings.empty() ? fallback_warnings : attachment->warnings);
    } else {
        json << "\"positions\":[],\"opened_orders\":[],\"closed_orders\":[],\"warnings\":";
        append_string_array(json, fallback_warnings);
    }
    json << '}';
}

void append_chart_payload(std::ostringstream& json, const ChartPayload& chart) {
    const auto append_bars = [&json](const std::vector<ChartBarPoint>& bars) {
        json << '[';
        for (std::size_t index = 0; index < bars.size(); ++index) {
            if (index > 0) {
                json << ',';
            }
            const auto& bar = bars[index];
            json << '{'
                 << "\"time\":" << bar.time << ','
                 << "\"open\":" << format_decimal(bar.open) << ','
                 << "\"high\":" << format_decimal(bar.high) << ','
                 << "\"low\":" << format_decimal(bar.low) << ','
                 << "\"close\":" << format_decimal(bar.close)
                 << '}';
        }
        json << ']';
    };

    const auto append_indicator_series = [&json](const std::vector<ChartIndicatorSeriesPayload>& indicator_series) {
        json << '[';
        for (std::size_t index = 0; index < indicator_series.size(); ++index) {
            if (index > 0) {
                json << ',';
            }
            const auto& series = indicator_series[index];
            json << '{'
                 << "\"indicator_id\":" << quoted(series.indicator_id) << ','
                 << "\"label\":" << quoted(series.label.empty() ? series.indicator_id : series.label) << ','
                 << "\"color\":" << quoted(series.color) << ','
                 << "\"strategy_id\":" << quoted(series.strategy_id) << ','
                 << "\"account_id\":" << quoted(series.account_id) << ','
                 << "\"points\":[";
            for (std::size_t point_index = 0; point_index < series.points.size(); ++point_index) {
                if (point_index > 0) {
                    json << ',';
                }
                const auto& point = series.points[point_index];
                json << '{'
                     << "\"time\":" << point.time << ','
                     << "\"value\":" << format_decimal(point.value)
                     << '}';
            }
            json << "]}";
        }
        json << ']';
    };

    const auto append_signals = [&json](const std::vector<ChartSignalPoint>& signals) {
        json << '[';
        for (std::size_t index = 0; index < signals.size(); ++index) {
            if (index > 0) {
                json << ',';
            }
            const auto& signal = signals[index];
            json << '{'
                 << "\"time\":" << signal.time << ','
                 << "\"price\":" << format_decimal(signal.price) << ','
                 << "\"position\":" << quoted(signal.position) << ','
                 << "\"color\":" << quoted(signal.color) << ','
                 << "\"shape\":" << quoted(signal.shape) << ','
                 << "\"text\":" << quoted(signal.text) << ','
                 << "\"strategy_id\":" << quoted(signal.strategy_id) << ','
                 << "\"account_id\":" << quoted(signal.account_id)
                 << '}';
        }
        json << ']';
    };

    json << '{';
    json << "\"instrument\":" << quoted(chart.instrument) << ',';
    json << "\"default_instrument\":" << quoted(chart.default_instrument.empty() ? chart.instrument : chart.default_instrument) << ',';
    json << "\"source\":" << quoted(chart.source) << ',';
    json << "\"account_ids\":";
    append_string_array(json, chart.account_ids);
    json << ',';
    json << "\"bars\":";
    append_bars(chart.bars);
    json << ",\"indicator_series\":";
    append_indicator_series(chart.indicator_series);
    json << ",\"signals\":";
    append_signals(chart.signals);
    json << ",\"instruments\":[";
    for (std::size_t index = 0; index < chart.instruments.size(); ++index) {
        if (index > 0) {
            json << ',';
        }
        const auto& instrument = chart.instruments[index];
        json << '{'
             << "\"instrument\":" << quoted(instrument.instrument) << ','
             << "\"bars\":";
        append_bars(instrument.bars);
           json << ",\"indicator_series\":";
           append_indicator_series(instrument.indicator_series);
        json << ",\"signals\":";
        append_signals(instrument.signals);
        json << ",\"warnings\":";
        append_string_array(json, instrument.warnings);
        json << '}';
    }
    json << "],\"warnings\":";
    append_string_array(json, chart.warnings);
    json << '}';
}

const itrader::StrategyAttachmentSnapshot* find_runtime_attachment(
    const itrader::RuntimeSnapshot& snapshot,
    const std::string& strategy_id,
    const std::string& account_id) {

    for (const auto& attachment : snapshot.strategy_attachments) {
        if (attachment.strategy_id == strategy_id && attachment.account_id == account_id) {
            return &attachment;
        }
    }
    return nullptr;
}

std::string make_ui_state_json(
    const std::filesystem::path& workspace_root,
    std::string_view mode,
    std::string_view requested_config,
    bool enable_backtest_replay,
    const itrader::RuntimeSnapshotBuildOptions* snapshot_build_options = nullptr,
    std::string_view backtest_detail_level = "full") {

    const std::string normalized_mode = mode == "live" ? "live" : "backtest";
    const std::string normalized_backtest_detail_level = normalize_backtest_detail_level(backtest_detail_level);
    const auto config_path = resolve_config_path(workspace_root, normalized_mode, requested_config);
    const auto ini = itrader::IniFile::parse(config_path);
    const auto account_sections = ini.sections_with_prefix("account.");
    const auto strategy_sections = ini.sections_with_prefix("strategy.");
    const auto runtime_mode = normalized_mode == "live" ? itrader::Mode::Live : itrader::Mode::Backtest;
    auto runtime_state_config_path = config_path;
    std::optional<itrader::IniFile> runtime_state_ini_storage;
    std::vector<std::string> runtime_state_strategy_sections = strategy_sections;
    if (runtime_mode == itrader::Mode::Live) {
        {
            std::scoped_lock guard(g_live_runtime_mutex);
            refresh_live_runtime_processes_locked(&workspace_root);
            runtime_state_config_path = live_runtime_snapshot_config_path_for_request_locked(config_path);
        }

        if (normalize_path_for_compare(runtime_state_config_path) != normalize_path_for_compare(config_path)
            && std::filesystem::exists(runtime_state_config_path)) {
            runtime_state_ini_storage = itrader::IniFile::parse(runtime_state_config_path);
            runtime_state_strategy_sections = runtime_state_ini_storage->sections_with_prefix("strategy.");
        } else {
            runtime_state_config_path = config_path;
        }
    }
    const auto& runtime_state_ini = runtime_state_ini_storage.has_value() ? *runtime_state_ini_storage : ini;

    std::optional<itrader::RuntimeSnapshotBuildOptions> effective_snapshot_build_options_storage;
    const itrader::RuntimeSnapshotBuildOptions* effective_snapshot_build_options = snapshot_build_options;
    if (runtime_mode == itrader::Mode::Backtest && enable_backtest_replay && normalized_backtest_detail_level == "summary") {
        effective_snapshot_build_options_storage = snapshot_build_options == nullptr
            ? itrader::RuntimeSnapshotBuildOptions {}
            : *snapshot_build_options;
        effective_snapshot_build_options_storage->include_chart = false;
        effective_snapshot_build_options_storage->include_order_history = false;
        effective_snapshot_build_options = &*effective_snapshot_build_options_storage;
    }

    const bool include_backtest_chart = runtime_mode == itrader::Mode::Backtest
        && enable_backtest_replay
        && (effective_snapshot_build_options == nullptr || effective_snapshot_build_options->include_chart);

    std::optional<itrader::RuntimeSnapshot> runtime_snapshot;
    std::vector<std::string> runtime_warnings;
    bool allow_live_telemetry = true;
    if (runtime_mode == itrader::Mode::Live) {
        const auto compatibility = assess_live_telemetry_compatibility(
            runtime_state_config_path,
            runtime_state_ini,
            runtime_state_strategy_sections);
        allow_live_telemetry = compatibility.usable;
        runtime_warnings = compatibility.warnings;
    }
    const auto live_account_connectivity = runtime_mode == itrader::Mode::Live
        ? load_live_account_connectivity(runtime_state_config_path)
        : std::map<std::string, LiveAccountConnectivityStatus> {};

    const bool should_build_runtime_snapshot = runtime_mode == itrader::Mode::Live
        ? allow_live_telemetry
        : enable_backtest_replay;

    if (should_build_runtime_snapshot) {
        try {
            runtime_snapshot = itrader::build_runtime_snapshot(
                runtime_mode == itrader::Mode::Live ? runtime_state_config_path : config_path,
                runtime_mode == itrader::Mode::Live ? runtime_state_ini : ini,
                runtime_mode,
                effective_snapshot_build_options == nullptr ? itrader::RuntimeSnapshotBuildOptions {} : *effective_snapshot_build_options);
            runtime_warnings = runtime_snapshot->warnings;
        } catch (const std::exception& ex) {
            if (snapshot_build_options != nullptr && runtime_mode == itrader::Mode::Backtest && enable_backtest_replay) {
                throw;
            }
            runtime_warnings.push_back(std::string("Runtime detail snapshot unavailable: ") + ex.what());
        }
    } else if (runtime_mode == itrader::Mode::Backtest) {
        runtime_warnings.push_back("Backtest runtime replay is skipped during dashboard hydration to keep the UI responsive. Click Run on an attached strategy to compute positions, trades, and chart markers for the current config.");
    }

    std::map<std::string, itrader::AccountSnapshot> runtime_accounts_by_id;
    if (runtime_snapshot.has_value()) {
        for (const auto& account_snapshot : runtime_snapshot->accounts) {
            runtime_accounts_by_id[account_snapshot.account_id] = account_snapshot;
        }
    }

    std::ostringstream json;
    json << "{";
    json << "\"mode\":" << quoted(normalized_mode) << ",";
    json << "\"accounts\":[";
    for (std::size_t index = 0; index < account_sections.size(); ++index) {
        if (index > 0) {
            json << ',';
        }
        const auto& section_name = account_sections[index];
        const auto account_id = section_name.substr(section_name.find('.') + 1);
        json << '{';
        json << "\"id\":" << quoted(account_id);
        const auto section = ini.section(section_name);
        for (const auto& [key, value] : section) {
            (void)value;
            json << ',' << quoted(key) << ':' << quoted(ini.get(section_name, key));
        }

        const auto runtime_account_it = runtime_accounts_by_id.find(account_id);
        if (runtime_account_it != runtime_accounts_by_id.end()) {
            const auto& runtime_account = runtime_account_it->second;
            const double return_pct = runtime_account.initial_cash == 0.0
                ? 0.0
                : ((runtime_account.cash - runtime_account.initial_cash) / runtime_account.initial_cash) * 100.0;
            json << ",\"runtime_cash\":" << quoted(format_decimal(runtime_account.cash));
            json << ",\"runtime_realized_pnl\":" << quoted(format_decimal(runtime_account.realized_pnl));
            json << ",\"runtime_return_pct\":" << quoted(format_decimal(return_pct));
            json << ",\"runtime_open_position_count\":" << runtime_account.net_positions.size();
        }
        json << '}';
    }
    json << "],";

    json << "\"strategies\":[";
    for (std::size_t index = 0; index < strategy_sections.size(); ++index) {
        if (index > 0) {
            json << ',';
        }
        const auto& section_name = strategy_sections[index];
        const auto strategy_id = section_name.substr(section_name.find('.') + 1);
        const auto account_values = read_strategy_accounts(ini, section_name);
        json << '{';
        json << "\"id\":" << quoted(strategy_id);
        json << ",\"accounts\":[";
        for (std::size_t account_index = 0; account_index < account_values.size(); ++account_index) {
            if (account_index > 0) {
                json << ',';
            }
            json << quoted(account_values[account_index]);
        }
        json << "]";
        const auto section = ini.section(section_name);
        for (const auto& [key, value] : section) {
            if (key == "account" || key == "accounts") {
                continue;
            }
            (void)value;
            json << ',' << quoted(key) << ':' << quoted(ini.get(section_name, key));
        }
        json << ",\"runtime_details\":{";
        for (std::size_t account_index = 0; account_index < account_values.size(); ++account_index) {
            if (account_index > 0) {
                json << ',';
            }
            const auto& account_id = account_values[account_index];
            json << quoted(account_id) << ':';
            const auto* attachment = runtime_snapshot.has_value()
                ? find_runtime_attachment(*runtime_snapshot, strategy_id, account_id)
                : nullptr;
            const auto connectivity_it = live_account_connectivity.find(account_id);
            const auto* connectivity = connectivity_it == live_account_connectivity.end()
                ? nullptr
                : &connectivity_it->second;
            append_runtime_detail(
                json,
                attachment,
                connectivity,
                runtime_warnings,
                normalized_mode == "backtest" && enable_backtest_replay ? normalized_backtest_detail_level : "full");
        }
        json << '}';
        json << '}';
    }
    json << "],";

    const auto chart = build_chart_payload(
        runtime_mode == itrader::Mode::Live ? runtime_state_config_path : config_path,
        runtime_mode == itrader::Mode::Live ? runtime_state_ini : ini,
        normalized_mode,
        enable_backtest_replay,
        include_backtest_chart,
        allow_live_telemetry,
        runtime_snapshot,
        runtime_warnings,
        runtime_mode == itrader::Mode::Live ? runtime_state_strategy_sections : strategy_sections,
        account_sections);
    json << "\"chart\":";
    append_chart_payload(json, chart);
    json << ',';

    json << "\"backtest\":{";
    json << "\"detail_level\":" << quoted(enable_backtest_replay ? normalized_backtest_detail_level : "preview") << ',';
    json << "\"data_dir\":" << quoted(ini.get("backtest", "data_dir")) << ',';
    json << "\"csv\":" << quoted(ini.get("backtest", "csv"));
    json << "},";

    json << "\"live\":{";
    json << "\"environment\":" << quoted(ini.get("live", "environment", itrader::config_runtime_namespace(config_path))) << ',';
    json << "\"poll_interval_ms\":" << quoted(ini.get("live", "poll_interval_ms", "1000")) << ',';
    json << "\"iterations\":" << quoted(ini.get("live", "iterations", "0")) << ',';
    json << "\"dry_run\":" << quoted(ini.get("live", "dry_run", "false"));
    json << "},";

    json << "\"recorder\":";
    json << make_recorder_config_json(workspace_root);
    json << ',';

    json << "\"recorder_runtime\":";
    json << make_recorder_runtime_json(workspace_root, true);
    json << ',';

    json << "\"live_inventory\":";
    if (normalized_mode == "live") {
        json << make_live_inventory_json_from_config_path(runtime_state_config_path);
    } else {
        json << "{\"adjustments\":[],\"reconciliations\":[],\"warnings\":[]}";
    }
    json << ',';

    json << "\"live_runtime\":";
    if (normalized_mode == "live") {
        json << make_live_runtime_json(config_path, std::nullopt, {}, &workspace_root);
    } else {
        json << "{\"status\":\"stopped\",\"running\":false,\"stop_requested\":false,\"process_id\":0,\"exit_code\":0,\"executable_path\":\"\",\"active_config_path\":\"\",\"requested_config_path\":\"\",\"strategy_ids\":[],\"config_matches_request\":true,\"started_at_ms\":0,\"finished_at_ms\":0,\"message\":\"\"}";
    }
    json << ',';

    if (normalized_mode == "live") {
        json << "\"equity\":[0,8,12,6,19,24,21],";
    } else {
        json << "\"equity\":[1000000,1000030,999995,1000070,1000150,1000205],";
    }

    json << "\"activity\":[";
    json << "{\"time\":\"API\",\"text\":" << quoted("Loaded " + config_path.generic_string() + " from the local backend API.") << "},";
    const auto unassigned_count = std::count_if(strategy_sections.begin(), strategy_sections.end(), [&ini](const std::string& section_name) {
        return read_strategy_accounts(ini, section_name).empty();
    });
    json << "{\"time\":\"CFG\",\"text\":" << quoted("Detected " + std::to_string(account_sections.size()) + " account section(s), " + std::to_string(strategy_sections.size()) + " strategy section(s), and " + std::to_string(unassigned_count) + " unassigned strategy section(s).") << "},";
    if (!runtime_warnings.empty()) {
        json << "{\"time\":\"RUNTIME\",\"text\":" << quoted(runtime_warnings.front()) << "},";
    }
    json << "{\"time\":\"RUN\",\"text\":" << quoted("Use the generated INI or current config file to launch the C++ runtime.") << "}";
    json << "],";

    json << "\"api\":{";
    json << "\"connected\":true,";
    json << "\"source_config\":" << quoted(config_path.generic_string()) << ',';
    json << "\"base_url\":" << quoted("http://127.0.0.1:8080") << ',';
    json << "\"origin\":\"local-http\"";
    json << "}";
    json << '}';
    return json.str();
}

std::shared_ptr<BacktestReplayJob> find_backtest_job(std::string_view requested_id) {
    std::scoped_lock guard(g_backtest_job_mutex);
    if (g_backtest_job == nullptr) {
        return nullptr;
    }
    if (requested_id.empty() || g_backtest_job->id == requested_id) {
        return g_backtest_job;
    }
    return nullptr;
}

std::string start_backtest_replay_job_json(
    const std::filesystem::path& workspace_root,
    std::string_view requested_config,
    std::string_view requested_detail_level) {

    const auto normalized_detail_level = normalize_backtest_detail_level(requested_detail_level);
    {
        std::scoped_lock guard(g_backtest_job_mutex);
        if (g_backtest_job != nullptr) {
            const auto existing_job = g_backtest_job;
            bool return_existing_job = false;
            {
                std::scoped_lock job_guard(existing_job->mutex);
                return_existing_job = existing_job->status == "queued" || existing_job->status == "running";
            }
            if (return_existing_job) {
                return make_backtest_job_json(existing_job);
            }
        }

        auto job = std::make_shared<BacktestReplayJob>();
        job->id = "backtest-job-" + std::to_string(++g_backtest_job_counter);
        job->requested_config = trim_copy(requested_config);
        job->detail_level = normalized_detail_level;
        job->started_at_ms = current_time_millis();
        job->status = "queued";
        job->phase = "queued";
        g_backtest_job = job;

        const auto workspace_root_copy = workspace_root;
        std::thread([job, workspace_root_copy]() {
            try {
                {
                    std::scoped_lock job_guard(job->mutex);
                    job->status = "running";
                    job->phase = "preparing_backtest_replay";
                }

                itrader::RuntimeSnapshotBuildOptions snapshot_options;
                snapshot_options.cancel_requested = &job->cancel_requested;
                snapshot_options.include_chart = job->detail_level == "full";
                snapshot_options.include_order_history = job->detail_level == "full";
                snapshot_options.chart_bar_seconds = job->detail_level == "full" ? 60 : 1;
                snapshot_options.on_progress = [job](const itrader::RuntimeSnapshotProgress& progress) {
                    std::scoped_lock job_guard(job->mutex);
                    job->phase = progress.phase;
                    job->processed_files = progress.processed_files;
                    job->total_files = progress.total_files;
                    job->processed_ticks = progress.processed_ticks;
                    if (job->status != "cancelled") {
                        job->status = job->cancel_requested.load() ? "cancelling" : "running";
                    }
                };

                const auto state_json = make_ui_state_json(
                    workspace_root_copy,
                    "backtest",
                    job->requested_config,
                    true,
                    &snapshot_options,
                    job->detail_level);
                std::scoped_lock job_guard(job->mutex);
                if (job->cancel_requested.load()) {
                    job->status = "cancelled";
                    job->phase = "cancelled";
                    job->error_message = "Backtest replay cancelled by request.";
                } else {
                    job->state_json = state_json;
                    job->status = "completed";
                    job->phase = "completed";
                    job->error_message.clear();
                }
            } catch (const std::exception& ex) {
                std::scoped_lock job_guard(job->mutex);
                if (job->cancel_requested.load() || std::string(ex.what()).find("cancelled by request") != std::string::npos) {
                    job->status = "cancelled";
                    job->phase = "cancelled";
                    job->error_message = "Backtest replay cancelled by request.";
                } else {
                    job->status = "failed";
                    job->phase = "failed";
                    job->error_message = ex.what();
                }
            }
            job->finished_at_ms = current_time_millis();
        }).detach();

        return make_backtest_job_json(job);
    }
}

std::string cancel_backtest_replay_job_json(std::string_view requested_id) {
    const auto job = find_backtest_job(requested_id);
    if (job == nullptr) {
        return "{\"ok\":false,\"message\":\"Backtest replay job not found.\"}";
    }

    job->cancel_requested = true;
    {
        std::scoped_lock guard(job->mutex);
        if (job->status == "queued" || job->status == "running") {
            job->status = "cancelling";
            job->phase = "cancelling";
        }
    }
    return make_backtest_job_json(job);
}

void close_live_runtime_process_handle(LiveRuntimeProcess& runtime) {
    if (runtime.process_handle != nullptr) {
        CloseHandle(runtime.process_handle);
        runtime.process_handle = nullptr;
    }
}

std::string live_runtime_key(
    const std::filesystem::path& requested_config_path,
    const std::vector<std::string>& strategy_ids) {

    return normalize_path_for_compare(requested_config_path)
        + "|"
        + join_strings(normalize_strategy_id_filter(strategy_ids), "\x1f");
}

std::vector<std::wstring> split_windows_command_line(const std::wstring& command_line) {
    int argument_count = 0;
    LPWSTR* arguments = CommandLineToArgvW(command_line.c_str(), &argument_count);
    if (arguments == nullptr) {
        return {};
    }

    std::vector<std::wstring> result;
    result.reserve(static_cast<std::size_t>(argument_count));
    for (int index = 0; index < argument_count; ++index) {
        result.emplace_back(arguments[index]);
    }
    LocalFree(arguments);
    return result;
}

std::optional<std::filesystem::path> live_config_path_from_command_line(
    const std::filesystem::path& workspace_root,
    const std::wstring& command_line) {

    const auto arguments = split_windows_command_line(command_line);
    if (arguments.empty()) {
        return std::nullopt;
    }

    std::wstring mode;
    std::wstring config;
    for (std::size_t index = 1; index < arguments.size(); ++index) {
        const auto argument = trim_copy(arguments[index]);
        const auto lowered = lower_copy(argument);
        if (lowered == L"--mode" && index + 1 < arguments.size()) {
            mode = lower_copy(trim_copy(arguments[++index]));
            continue;
        }
        if (lowered.rfind(L"--mode=", 0) == 0) {
            mode = lower_copy(trim_copy(argument.substr(std::wstring_view {L"--mode="}.size())));
            continue;
        }
        if (lowered == L"--config" && index + 1 < arguments.size()) {
            config = trim_copy(arguments[++index]);
            continue;
        }
        if (lowered.rfind(L"--config=", 0) == 0) {
            config = trim_copy(argument.substr(std::wstring_view {L"--config="}.size()));
            continue;
        }
    }

    if (mode != L"live" || config.empty()) {
        return std::nullopt;
    }

    std::filesystem::path config_path(config);
    if (config_path.is_relative()) {
        config_path = workspace_root / config_path;
    }

    std::error_code error_code;
    const auto canonical = std::filesystem::weakly_canonical(config_path, error_code);
    config_path = error_code ? config_path.lexically_normal() : canonical;

    const auto configs_root = normalize_path_for_compare(workspace_root / "configs");
    const auto config_text = normalize_path_for_compare(config_path);
    if (config_text.rfind(configs_root, 0) != 0) {
        return std::nullopt;
    }
    return config_path;
}

std::optional<std::filesystem::path> source_config_path_from_scoped_live_config(const std::filesystem::path& config_path) {
    std::ifstream input(config_path);
    if (!input.is_open()) {
        return std::nullopt;
    }

    static constexpr std::string_view kPrefix = "; Source config:";
    std::string line;
    while (std::getline(input, line)) {
        const auto trimmed = trim_copy(line);
        if (trimmed.empty()) {
            continue;
        }
        if (trimmed.front() != ';') {
            break;
        }
        if (trimmed.rfind(kPrefix, 0) != 0) {
            continue;
        }

        auto source = trim_copy(trimmed.substr(kPrefix.size()));
        if (source.empty()) {
            return std::nullopt;
        }
        std::filesystem::path source_path(source);
        if (source_path.is_relative()) {
            source_path = config_path.parent_path() / source_path;
        }
        std::error_code error_code;
        const auto canonical = std::filesystem::weakly_canonical(source_path, error_code);
        return error_code ? source_path.lexically_normal() : canonical;
    }

    return std::nullopt;
}

bool is_dashboard_scoped_live_config_path(const std::filesystem::path& config_path) {
    return config_path.stem().generic_string().find("__dashboard__") != std::string::npos;
}

std::filesystem::path requested_config_path_for_live_process(
    const std::filesystem::path& workspace_root,
    const std::filesystem::path& active_config_path) {

    if (const auto source_config_path = source_config_path_from_scoped_live_config(active_config_path); source_config_path.has_value()) {
        return *source_config_path;
    }

    const auto stem = active_config_path.stem().generic_string();
    const auto marker = stem.find("__dashboard__");
    if (marker != std::string::npos) {
        const auto extension = active_config_path.extension().empty()
            ? std::string {".ini"}
            : active_config_path.extension().generic_string();
        auto base_config_path = workspace_root / "configs" / (stem.substr(0, marker) + extension);
        std::error_code error_code;
        const auto canonical = std::filesystem::weakly_canonical(base_config_path, error_code);
        return error_code ? base_config_path.lexically_normal() : canonical;
    }

    return active_config_path;
}

std::vector<std::string> strategy_ids_for_live_process_config(const std::filesystem::path& active_config_path) {
    if (!is_dashboard_scoped_live_config_path(active_config_path) || !std::filesystem::exists(active_config_path)) {
        return {};
    }

    try {
        const auto ini = itrader::IniFile::parse(active_config_path);
        std::vector<std::string> strategy_ids;
        for (const auto& section_name : ini.sections_with_prefix("strategy.")) {
            auto strategy_id = trim_copy(section_name.substr(std::string("strategy.").size()));
            if (!strategy_id.empty() && std::find(strategy_ids.begin(), strategy_ids.end(), strategy_id) == strategy_ids.end()) {
                strategy_ids.push_back(std::move(strategy_id));
            }
        }
        return normalize_strategy_id_filter(strategy_ids);
    } catch (const std::exception&) {
        return {};
    }
}

std::wstring wmi_variant_to_wstring(const VARIANT& value) {
    if (value.vt == VT_BSTR && value.bstrVal != nullptr) {
        return value.bstrVal;
    }
    return {};
}

DWORD wmi_variant_to_dword(const VARIANT& value) {
    if (value.vt == VT_I4 || value.vt == VT_UI4) {
        return static_cast<DWORD>(value.uintVal);
    }
    if (value.vt == VT_I2 || value.vt == VT_UI2) {
        return static_cast<DWORD>(value.uiVal);
    }
    return 0;
}

std::vector<LiveRuntimeProcess> discover_live_runtime_processes(const std::filesystem::path& workspace_root) {
    std::vector<LiveRuntimeProcess> runtimes;

    std::filesystem::path expected_binary_path;
    std::string expected_binary_text;
    try {
        expected_binary_path = discover_live_runtime_binary_path(workspace_root);
        expected_binary_text = normalize_path_for_compare(expected_binary_path);
    } catch (const std::exception&) {
        return runtimes;
    }

    const HRESULT initialize_result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool should_uninitialize = SUCCEEDED(initialize_result);
    if (FAILED(initialize_result) && initialize_result != RPC_E_CHANGED_MODE) {
        return runtimes;
    }

    const HRESULT security_result = CoInitializeSecurity(
        nullptr,
        -1,
        nullptr,
        nullptr,
        RPC_C_AUTHN_LEVEL_DEFAULT,
        RPC_C_IMP_LEVEL_IMPERSONATE,
        nullptr,
        EOAC_NONE,
        nullptr);
    (void)security_result;

    IWbemLocator* locator = nullptr;
    HRESULT result = CoCreateInstance(
        CLSID_WbemLocator,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_IWbemLocator,
        reinterpret_cast<LPVOID*>(&locator));
    if (FAILED(result) || locator == nullptr) {
        if (should_uninitialize) {
            CoUninitialize();
        }
        return runtimes;
    }

    IWbemServices* services = nullptr;
    BSTR namespace_name = SysAllocString(L"ROOT\\CIMV2");
    result = locator->ConnectServer(namespace_name, nullptr, nullptr, nullptr, 0, nullptr, nullptr, &services);
    SysFreeString(namespace_name);
    locator->Release();
    if (FAILED(result) || services == nullptr) {
        if (should_uninitialize) {
            CoUninitialize();
        }
        return runtimes;
    }

    CoSetProxyBlanket(
        services,
        RPC_C_AUTHN_WINNT,
        RPC_C_AUTHZ_NONE,
        nullptr,
        RPC_C_AUTHN_LEVEL_CALL,
        RPC_C_IMP_LEVEL_IMPERSONATE,
        nullptr,
        EOAC_NONE);

    IEnumWbemClassObject* enumerator = nullptr;
    BSTR query_language = SysAllocString(L"WQL");
    BSTR query = SysAllocString(L"SELECT ProcessId,CommandLine,ExecutablePath FROM Win32_Process WHERE Name='itrader.exe'");
    result = services->ExecQuery(
        query_language,
        query,
        WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
        nullptr,
        &enumerator);
    SysFreeString(query);
    SysFreeString(query_language);
    services->Release();
    if (FAILED(result) || enumerator == nullptr) {
        if (should_uninitialize) {
            CoUninitialize();
        }
        return runtimes;
    }

    while (true) {
        IWbemClassObject* process_object = nullptr;
        ULONG returned = 0;
        result = enumerator->Next(1000, 1, &process_object, &returned);
        if (FAILED(result) || returned == 0 || process_object == nullptr) {
            break;
        }

        VARIANT process_id_value {};
        VARIANT command_line_value {};
        VARIANT executable_path_value {};
        VariantInit(&process_id_value);
        VariantInit(&command_line_value);
        VariantInit(&executable_path_value);
        process_object->Get(L"ProcessId", 0, &process_id_value, nullptr, nullptr);
        process_object->Get(L"CommandLine", 0, &command_line_value, nullptr, nullptr);
        process_object->Get(L"ExecutablePath", 0, &executable_path_value, nullptr, nullptr);

        const DWORD process_id = wmi_variant_to_dword(process_id_value);
        const auto command_line = wmi_variant_to_wstring(command_line_value);
        auto executable_path_text = wmi_variant_to_wstring(executable_path_value);

        VariantClear(&process_id_value);
        VariantClear(&command_line_value);
        VariantClear(&executable_path_value);
        process_object->Release();

        if (process_id == 0 || command_line.empty()) {
            continue;
        }

        HANDLE process_handle = OpenProcess(
            PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE | SYNCHRONIZE,
            FALSE,
            process_id);
        if (process_handle == nullptr) {
            continue;
        }

        if (WaitForSingleObject(process_handle, 0) != WAIT_TIMEOUT) {
            CloseHandle(process_handle);
            continue;
        }

        if (executable_path_text.empty()) {
            std::wstring image_path(32768, L'\0');
            DWORD image_path_size = static_cast<DWORD>(image_path.size());
            if (QueryFullProcessImageNameW(process_handle, 0, image_path.data(), &image_path_size)) {
                image_path.resize(image_path_size);
                executable_path_text = std::move(image_path);
            }
        }

        std::filesystem::path executable_path(executable_path_text);
        std::error_code executable_error;
        const auto canonical_executable = std::filesystem::weakly_canonical(executable_path, executable_error);
        executable_path = executable_error ? executable_path.lexically_normal() : canonical_executable;
        if (normalize_path_for_compare(executable_path) != expected_binary_text) {
            CloseHandle(process_handle);
            continue;
        }

        const auto active_config_path = live_config_path_from_command_line(workspace_root, command_line);
        if (!active_config_path.has_value()) {
            CloseHandle(process_handle);
            continue;
        }

        LiveRuntimeProcess runtime;
        runtime.process_handle = process_handle;
        runtime.process_id = process_id;
        runtime.executable_path = executable_path;
        runtime.config_path = *active_config_path;
        runtime.requested_config_path = requested_config_path_for_live_process(workspace_root, *active_config_path);
        runtime.log_path = live_runtime_log_path(*active_config_path);
        runtime.command_line = command_line;
        runtime.strategy_ids = strategy_ids_for_live_process_config(*active_config_path);
        runtime.status = "running";
        runtime.managed_by = "external";
        runtime.started_at_ms = process_started_at_millis(process_handle);
        runtime.finished_at_ms = 0;
        runtime.exit_code = STILL_ACTIVE;
        runtime.stop_requested = false;
        runtime.message = "Recovered running live runtime from the Windows process list.";
        runtimes.push_back(std::move(runtime));
    }

    enumerator->Release();
    if (should_uninitialize) {
        CoUninitialize();
    }
    return runtimes;
}

void adopt_live_runtime_process_locked(LiveRuntimeProcess runtime) {
    if (runtime.process_id == 0 || runtime.process_handle == nullptr) {
        close_live_runtime_process_handle(runtime);
        return;
    }

    for (auto& [runtime_key, existing] : g_live_runtime_processes) {
        (void)runtime_key;
        if (existing.process_id != runtime.process_id) {
            continue;
        }

        if (existing.process_handle == nullptr) {
            existing.process_handle = runtime.process_handle;
            runtime.process_handle = nullptr;
        } else {
            close_live_runtime_process_handle(runtime);
        }
        existing.executable_path = runtime.executable_path;
        existing.config_path = runtime.config_path;
        existing.requested_config_path = runtime.requested_config_path;
        existing.log_path = runtime.log_path;
        existing.command_line = runtime.command_line;
        existing.strategy_ids = runtime.strategy_ids;
        existing.status = "running";
        existing.managed_by = existing.managed_by.empty() ? "external" : existing.managed_by;
        existing.started_at_ms = runtime.started_at_ms;
        existing.finished_at_ms = 0;
        existing.exit_code = STILL_ACTIVE;
        existing.message = "Recovered running live runtime from the Windows process list.";
        return;
    }

    auto runtime_key = live_runtime_key(runtime.requested_config_path, runtime.strategy_ids);
    auto existing_it = g_live_runtime_processes.find(runtime_key);
    if (existing_it != g_live_runtime_processes.end()
        && existing_it->second.status == "running"
        && existing_it->second.process_id != runtime.process_id) {
        runtime_key += "|pid=" + std::to_string(runtime.process_id);
    }

    auto& slot = g_live_runtime_processes[runtime_key];
    close_live_runtime_process_handle(slot);
    slot = runtime;
    runtime.process_handle = nullptr;
}

void adopt_live_runtime_processes_from_os_locked(const std::filesystem::path& workspace_root) {
    for (auto& runtime : discover_live_runtime_processes(workspace_root)) {
        adopt_live_runtime_process_locked(std::move(runtime));
    }
}

bool live_runtime_config_matches_requested(
    const LiveRuntimeProcess& runtime,
    const std::filesystem::path& requested_config_path) {

    const auto active_requested_config_path = runtime.requested_config_path.empty()
        ? runtime.config_path
        : runtime.requested_config_path;
    if (active_requested_config_path.empty() || requested_config_path.empty()) {
        return true;
    }

    return normalize_path_for_compare(active_requested_config_path)
        == normalize_path_for_compare(requested_config_path);
}

bool live_runtime_strategy_ids_match_requested(
    const LiveRuntimeProcess& runtime,
    const std::vector<std::string>& requested_strategy_ids) {

    const auto normalized_requested = normalize_strategy_id_filter(requested_strategy_ids);
    if (normalized_requested.empty()) {
        return true;
    }

    return normalized_requested == normalize_strategy_id_filter(runtime.strategy_ids);
}

bool live_runtime_strategy_ids_overlap(
    const std::vector<std::string>& left,
    const std::vector<std::string>& right) {

    const auto normalized_left = normalize_strategy_id_filter(left);
    const auto normalized_right = normalize_strategy_id_filter(right);
    if (normalized_left.empty() || normalized_right.empty()) {
        return true;
    }

    for (const auto& strategy_id : normalized_left) {
        if (std::find(normalized_right.begin(), normalized_right.end(), strategy_id) != normalized_right.end()) {
            return true;
        }
    }
    return false;
}

void refresh_live_runtime_process_locked(LiveRuntimeProcess& runtime) {
    if (runtime.process_handle == nullptr) {
        return;
    }

    const DWORD wait_result = WaitForSingleObject(runtime.process_handle, 0);
    if (wait_result == WAIT_TIMEOUT) {
        runtime.status = "running";
        if (runtime.stop_requested) {
            runtime.message = "Stop requested for the live runtime; waiting for itrader.exe to exit.";
        } else if (runtime.message.empty()) {
            runtime.message = "Live runtime is currently running.";
        }
        return;
    }

    if (runtime.finished_at_ms == 0) {
        runtime.finished_at_ms = current_time_millis();
    }

    if (wait_result == WAIT_FAILED) {
        const auto wait_error = GetLastError();
        runtime.exit_code = wait_error;
        runtime.status = runtime.stop_requested ? "stopped" : "failed";
        runtime.message = runtime.stop_requested
            ? "Live runtime stop was requested, but the final process status could not be queried."
            : ("Unable to query the live runtime status (WaitForSingleObject error " + std::to_string(wait_error) + ").");
        runtime.stop_requested = false;
        close_live_runtime_process_handle(runtime);
        return;
    }

    DWORD exit_code = 0;
    if (!GetExitCodeProcess(runtime.process_handle, &exit_code)) {
        exit_code = GetLastError();
        runtime.status = runtime.stop_requested ? "stopped" : "failed";
        runtime.message = runtime.stop_requested
            ? "Live runtime stop was requested, but GetExitCodeProcess could not confirm the final exit code."
            : ("Unable to query the live runtime exit code (GetExitCodeProcess error " + std::to_string(exit_code) + ").");
    } else if (runtime.stop_requested) {
        runtime.status = "stopped";
        runtime.message = "Live runtime stopped on request.";
    } else {
        runtime.status = exit_code == 0 ? "stopped" : "failed";
        runtime.message = exit_code == 0
            ? "Live runtime exited cleanly."
            : format_live_runtime_exit_message(exit_code, runtime.log_path);
    }

    runtime.exit_code = exit_code;
    runtime.stop_requested = false;
    close_live_runtime_process_handle(runtime);
}

void refresh_live_runtime_processes_locked(const std::filesystem::path* workspace_root) {
    if (workspace_root != nullptr) {
        adopt_live_runtime_processes_from_os_locked(*workspace_root);
    }
    for (auto& [runtime_key, runtime] : g_live_runtime_processes) {
        (void)runtime_key;
        refresh_live_runtime_process_locked(runtime);
    }
}

std::filesystem::path live_runtime_snapshot_config_path_for_request_locked(const std::filesystem::path& requested_config_path) {
    refresh_live_runtime_processes_locked();
    for (const auto& [runtime_key, runtime] : g_live_runtime_processes) {
        (void)runtime_key;
        if (runtime.status == "running" && live_runtime_config_matches_requested(runtime, requested_config_path)) {
            return runtime.config_path.empty() ? requested_config_path : runtime.config_path;
        }
    }
    return requested_config_path;
}

std::vector<std::string> collect_live_runtime_strategy_ids(const std::vector<const LiveRuntimeProcess*>& runtimes) {
    bool covers_all_strategies = false;
    std::vector<std::string> strategy_ids;
    for (const auto* runtime : runtimes) {
        if (runtime == nullptr) {
            continue;
        }
        const auto normalized = normalize_strategy_id_filter(runtime->strategy_ids);
        if (normalized.empty()) {
            covers_all_strategies = true;
            break;
        }
        for (const auto& strategy_id : normalized) {
            if (std::find(strategy_ids.begin(), strategy_ids.end(), strategy_id) == strategy_ids.end()) {
                strategy_ids.push_back(strategy_id);
            }
        }
    }

    if (covers_all_strategies) {
        return {};
    }

    std::sort(strategy_ids.begin(), strategy_ids.end());
    return strategy_ids;
}

const LiveRuntimeProcess* newest_live_runtime(const std::vector<const LiveRuntimeProcess*>& runtimes) {
    const LiveRuntimeProcess* newest = nullptr;
    for (const auto* runtime : runtimes) {
        if (runtime == nullptr) {
            continue;
        }
        if (newest == nullptr || runtime->started_at_ms >= newest->started_at_ms) {
            newest = runtime;
        }
    }
    return newest;
}

std::string default_live_runtime_message(
    const LiveRuntimeProcess& runtime,
    const std::filesystem::path& requested_config_path,
    bool config_matches_request) {

    if (runtime.status == "running") {
        if (runtime.stop_requested) {
            return "Stop requested for the live runtime; waiting for itrader.exe to exit.";
        }
        return config_matches_request
            ? ("Live runtime is currently running for " + describe_strategy_scope(runtime.strategy_ids) + '.')
            : ("Live runtime is currently running for " + runtime.config_path.generic_string() + '.');
    }
    if (runtime.status == "failed") {
        return trim_copy(runtime.message).empty()
            ? "Live runtime is not running because the last launch failed."
            : trim_copy(runtime.message);
    }

    (void)requested_config_path;
    return "No live runtime launched from the dashboard is currently running.";
}

void append_live_runtime_process_json(
    std::ostringstream& json,
    const LiveRuntimeProcess& runtime,
    const std::filesystem::path& requested_config_path,
    const std::optional<bool>& ok = std::nullopt,
    std::string_view message_override = {}) {

    const bool config_matches_request = live_runtime_config_matches_requested(runtime, requested_config_path);
    const auto requested_config_text = requested_config_path.empty()
        ? std::string {}
        : requested_config_path.generic_string();
    const auto active_config_text = runtime.config_path.empty()
        ? std::string {}
        : runtime.config_path.generic_string();
    const auto executable_path_text = runtime.executable_path.empty()
        ? std::string {}
        : runtime.executable_path.generic_string();
    const auto log_path_text = runtime.log_path.empty()
        ? std::string {}
        : runtime.log_path.generic_string();

    std::string message = trim_copy(message_override);
    if (message.empty()) {
        message = trim_copy(runtime.message);
    }
    if (message.empty()) {
        message = default_live_runtime_message(runtime, requested_config_path, config_matches_request);
    }

    if (ok.has_value()) {
        json << "\"ok\":" << (*ok ? "true" : "false") << ',';
    }
    json << "\"status\":" << quoted(runtime.status) << ','
         << "\"running\":" << (runtime.status == "running" ? "true" : "false") << ','
         << "\"stop_requested\":" << (runtime.stop_requested ? "true" : "false") << ','
         << "\"process_id\":" << runtime.process_id << ','
         << "\"exit_code\":" << runtime.exit_code << ','
         << "\"executable_path\":" << quoted(executable_path_text) << ','
         << "\"log_path\":" << quoted(log_path_text) << ','
         << "\"active_config_path\":" << quoted(active_config_text) << ','
         << "\"requested_config_path\":" << quoted(requested_config_text) << ','
         << "\"strategy_ids\":[";
    for (std::size_t index = 0; index < runtime.strategy_ids.size(); ++index) {
        if (index > 0) {
            json << ',';
        }
        json << quoted(runtime.strategy_ids[index]);
    }
    json << "],"
         << "\"config_matches_request\":" << (config_matches_request ? "true" : "false") << ','
         << "\"started_at_ms\":" << runtime.started_at_ms << ','
         << "\"finished_at_ms\":" << runtime.finished_at_ms << ','
         << "\"message\":" << quoted(message);
}

void append_live_runtime_json_locked(
    std::ostringstream& json,
    const std::filesystem::path& requested_config_path,
    const std::optional<bool>& ok = std::nullopt,
    std::string_view message_override = {}) {

    refresh_live_runtime_processes_locked();

    std::vector<const LiveRuntimeProcess*> matching_runtimes;
    std::vector<const LiveRuntimeProcess*> matching_running_runtimes;
    std::vector<const LiveRuntimeProcess*> matching_failed_runtimes;
    std::vector<const LiveRuntimeProcess*> running_runtimes;
    for (const auto& [runtime_key, runtime] : g_live_runtime_processes) {
        (void)runtime_key;
        if (live_runtime_config_matches_requested(runtime, requested_config_path)) {
            matching_runtimes.push_back(&runtime);
            if (runtime.status == "running") {
                matching_running_runtimes.push_back(&runtime);
            } else if (runtime.status == "failed") {
                matching_failed_runtimes.push_back(&runtime);
            }
        }
        if (runtime.status == "running") {
            running_runtimes.push_back(&runtime);
        }
    }

    LiveRuntimeProcess summary;
    summary.requested_config_path = requested_config_path;
    const bool has_matching_running = !matching_running_runtimes.empty();
    const bool has_matching_failed = !matching_failed_runtimes.empty();
    const bool has_any_running = !running_runtimes.empty();
    bool config_matches_request = true;

    if (has_matching_running) {
        const auto* representative = newest_live_runtime(matching_running_runtimes);
        summary = *representative;
        summary.strategy_ids = collect_live_runtime_strategy_ids(matching_running_runtimes);
        summary.status = "running";
        summary.stop_requested = std::any_of(
            matching_running_runtimes.begin(),
            matching_running_runtimes.end(),
            [](const LiveRuntimeProcess* runtime) {
                return runtime != nullptr && runtime->stop_requested;
            });
        if (matching_running_runtimes.size() > 1) {
            summary.message = std::to_string(matching_running_runtimes.size())
                + " live runtimes are running for the requested config.";
        }
    } else if (has_matching_failed) {
        summary = *newest_live_runtime(matching_failed_runtimes);
        summary.status = "failed";
    } else if (has_any_running) {
        const auto* representative = newest_live_runtime(running_runtimes);
        summary = *representative;
        config_matches_request = false;
        summary.message = running_runtimes.size() == 1
            ? ("A live runtime is running for " + representative->config_path.generic_string() + '.')
            : (std::to_string(running_runtimes.size()) + " live runtimes are running for other configs.");
    } else {
        const auto* representative = newest_live_runtime(matching_runtimes);
        if (representative != nullptr) {
            summary = *representative;
            summary.status = summary.status.empty() ? "stopped" : summary.status;
        } else {
            summary.status = "stopped";
            summary.message = "No live runtime launched from the dashboard is currently running.";
        }
    }

    const auto requested_config_text = requested_config_path.empty()
        ? std::string {}
        : requested_config_path.generic_string();
    const auto active_config_text = summary.config_path.empty()
        ? std::string {}
        : summary.config_path.generic_string();
    const auto executable_path_text = summary.executable_path.empty()
        ? std::string {}
        : summary.executable_path.generic_string();
    const auto log_path_text = summary.log_path.empty()
        ? std::string {}
        : summary.log_path.generic_string();

    std::string message = trim_copy(message_override);
    if (message.empty()) {
        message = trim_copy(summary.message);
    }
    if (message.empty()) {
        message = default_live_runtime_message(summary, requested_config_path, config_matches_request);
    }

    const auto summary_strategy_ids = normalize_strategy_id_filter(summary.strategy_ids);
    if (ok.has_value()) {
        json << "\"ok\":" << (*ok ? "true" : "false") << ',';
    }
    json << "\"status\":" << quoted(summary.status) << ','
         << "\"running\":" << (summary.status == "running" ? "true" : "false") << ','
         << "\"stop_requested\":" << (summary.stop_requested ? "true" : "false") << ','
         << "\"process_id\":" << summary.process_id << ','
         << "\"exit_code\":" << summary.exit_code << ','
         << "\"executable_path\":" << quoted(executable_path_text) << ','
         << "\"log_path\":" << quoted(log_path_text) << ','
         << "\"active_config_path\":" << quoted(active_config_text) << ','
         << "\"requested_config_path\":" << quoted(requested_config_text) << ','
         << "\"strategy_ids\":[";
    for (std::size_t index = 0; index < summary_strategy_ids.size(); ++index) {
        if (index > 0) {
            json << ',';
        }
        json << quoted(summary_strategy_ids[index]);
    }
    json << "],"
         << "\"config_matches_request\":" << (config_matches_request ? "true" : "false") << ','
         << "\"started_at_ms\":" << summary.started_at_ms << ','
         << "\"finished_at_ms\":" << summary.finished_at_ms << ','
         << "\"runtime_count\":" << g_live_runtime_processes.size() << ','
         << "\"running_count\":" << running_runtimes.size() << ','
         << "\"message\":" << quoted(message) << ','
         << "\"instances\":[";
    bool first_instance = true;
    for (const auto& [runtime_key, runtime] : g_live_runtime_processes) {
        (void)runtime_key;
        if (!first_instance) {
            json << ',';
        }
        first_instance = false;
        json << '{';
        append_live_runtime_process_json(json, runtime, requested_config_path);
        json << '}';
    }
    json << ']';
}

std::string make_live_runtime_json(
    const std::filesystem::path& requested_config_path,
    const std::optional<bool>& ok,
    std::string_view message_override,
    const std::filesystem::path* workspace_root) {

    std::scoped_lock guard(g_live_runtime_mutex);
    if (workspace_root != nullptr) {
        refresh_live_runtime_processes_locked(workspace_root);
    }

    std::ostringstream json;
    json << '{';
    append_live_runtime_json_locked(json, requested_config_path, ok, message_override);
    json << '}';
    return json.str();
}

bool stop_live_runtime_process_locked(LiveRuntimeProcess& runtime, std::string& stop_message) {
    runtime.stop_requested = true;
    runtime.message = "Stop requested for the live runtime; waiting for itrader.exe to exit.";

    if (!GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, runtime.process_id)) {
        const auto break_error = GetLastError();
        if (!TerminateProcess(runtime.process_handle, 1)) {
            const auto terminate_error = GetLastError();
            runtime.stop_requested = false;
            stop_message = "Unable to stop the live runtime (CTRL_BREAK error " + std::to_string(break_error)
                + ", TerminateProcess error " + std::to_string(terminate_error) + ").";
            return false;
        }

        stop_message = "The live runtime did not accept CTRL_BREAK, so the dashboard force-terminated itrader.exe.";
        WaitForSingleObject(runtime.process_handle, 1000);
    } else {
        const auto graceful_wait = WaitForSingleObject(runtime.process_handle, 3000);
        if (graceful_wait == WAIT_TIMEOUT) {
            if (!TerminateProcess(runtime.process_handle, 1)) {
                const auto terminate_error = GetLastError();
                runtime.stop_requested = false;
                stop_message = "Live runtime did not exit after CTRL_BREAK, and TerminateProcess failed with error "
                    + std::to_string(terminate_error) + '.';
                return false;
            }

            stop_message = "The dashboard requested a graceful stop, then force-terminated itrader.exe after it did not exit within 3 seconds.";
            WaitForSingleObject(runtime.process_handle, 1000);
        } else if (graceful_wait == WAIT_FAILED) {
            const auto wait_error = GetLastError();
            if (!TerminateProcess(runtime.process_handle, 1)) {
                const auto terminate_error = GetLastError();
                runtime.stop_requested = false;
                stop_message = "Unable to wait for the live runtime to stop (WaitForSingleObject error "
                    + std::to_string(wait_error)
                    + ", TerminateProcess error " + std::to_string(terminate_error) + ").";
                return false;
            }

            stop_message = "The dashboard could not confirm a graceful shutdown, so itrader.exe was force-terminated.";
            WaitForSingleObject(runtime.process_handle, 1000);
        } else {
            stop_message = "The dashboard requested a graceful live runtime stop.";
        }
    }

    refresh_live_runtime_process_locked(runtime);
    if (runtime.status == "running") {
        if (stop_message.empty()) {
            stop_message = "Stop requested for the live runtime; itrader.exe is still shutting down.";
        }
        return false;
    }

    runtime.status = "stopped";
    runtime.message = stop_message.empty()
        ? "Live runtime stopped on request."
        : stop_message;
    runtime.stop_requested = false;
    return true;
}

std::string start_live_runtime_json(
    const std::filesystem::path& workspace_root,
    std::string_view requested_config,
    const std::vector<std::string>& requested_strategy_ids = {}) {

    std::filesystem::path requested_config_path;
    std::filesystem::path launch_config_path;
    std::filesystem::path binary_path;
    std::filesystem::path log_path;
    std::string runtime_key_text;
    const auto normalized_strategy_ids = normalize_strategy_id_filter(requested_strategy_ids);

    try {
        requested_config_path = resolve_config_path(workspace_root, "live", requested_config);
        runtime_key_text = live_runtime_key(requested_config_path, normalized_strategy_ids);
        launch_config_path = normalized_strategy_ids.empty()
            ? requested_config_path
            : make_strategy_scoped_live_config_path(workspace_root, requested_config_path, normalized_strategy_ids);

        if (!normalized_strategy_ids.empty()) {
            std::error_code error_code;
            std::filesystem::create_directories(launch_config_path.parent_path(), error_code);
            if (error_code) {
                throw std::runtime_error("Unable to create strategy-scoped config directory: " + launch_config_path.parent_path().generic_string());
            }

            write_text_file(
                launch_config_path,
                render_strategy_scoped_live_config(requested_config_path, normalized_strategy_ids));
            seed_strategy_scoped_live_runtime_state(requested_config_path, launch_config_path);
        }

        binary_path = discover_live_runtime_binary_path(workspace_root);
        log_path = live_runtime_log_path(launch_config_path);

        const auto binary_directory = binary_path.parent_path();
        const auto command_line = quote_windows_command_argument(binary_path.wstring())
            + L" --mode live --config "
            + quote_windows_command_argument(launch_config_path.wstring());

        std::error_code error_code;
        std::filesystem::create_directories(log_path.parent_path(), error_code);
        if (error_code) {
            throw std::runtime_error("Unable to create live runtime log directory: " + log_path.parent_path().generic_string());
        }

        std::scoped_lock guard(g_live_runtime_mutex);
        refresh_live_runtime_processes_locked(&workspace_root);
        const auto existing_runtime_it = g_live_runtime_processes.find(runtime_key_text);
        if (existing_runtime_it != g_live_runtime_processes.end()
            && existing_runtime_it->second.status == "running"
            && existing_runtime_it->second.process_handle != nullptr) {
            std::ostringstream json;
            json << '{';
            append_live_runtime_json_locked(
                json,
                requested_config_path,
                true,
                "Live runtime is already running for " + describe_strategy_scope(normalized_strategy_ids) + '.');
            json << '}';
            return json.str();
        }

        for (const auto& [active_runtime_key, active_runtime] : g_live_runtime_processes) {
            (void)active_runtime_key;
            if (active_runtime.status != "running" || active_runtime.process_handle == nullptr) {
                continue;
            }
            if (!live_runtime_config_matches_requested(active_runtime, requested_config_path)) {
                continue;
            }
            if (live_runtime_strategy_ids_overlap(active_runtime.strategy_ids, normalized_strategy_ids)) {
                std::ostringstream json;
                json << '{';
                append_live_runtime_json_locked(
                    json,
                    requested_config_path,
                    false,
                    "A live runtime is already running for overlapping " + describe_strategy_scope(active_runtime.strategy_ids)
                        + ". Stop that scope before launching " + describe_strategy_scope(normalized_strategy_ids) + '.');
                json << '}';
                return json.str();
            }
        }

        STARTUPINFOW startup_info {};
        startup_info.cb = sizeof(startup_info);
        startup_info.dwFlags |= STARTF_USESTDHANDLES;
        startup_info.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        PROCESS_INFORMATION process_info {};

        SECURITY_ATTRIBUTES security_attributes {};
        security_attributes.nLength = sizeof(security_attributes);
        security_attributes.bInheritHandle = TRUE;

        HANDLE live_log_handle = CreateFileW(
            log_path.wstring().c_str(),
            FILE_APPEND_DATA,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            &security_attributes,
            CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
        if (live_log_handle == INVALID_HANDLE_VALUE) {
            const auto log_error = GetLastError();
            throw std::runtime_error("Unable to open live runtime log file (CreateFile error " + std::to_string(log_error) + "): " + log_path.generic_string());
        }

        startup_info.hStdOutput = live_log_handle;
        startup_info.hStdError = live_log_handle;

        std::vector<wchar_t> mutable_command_line(command_line.begin(), command_line.end());
        mutable_command_line.push_back(L'\0');
        const auto working_directory = binary_directory.wstring();

        if (!CreateProcessW(
            binary_path.wstring().c_str(),
            mutable_command_line.data(),
            nullptr,
            nullptr,
            TRUE,
            CREATE_NEW_PROCESS_GROUP,
            nullptr,
            working_directory.c_str(),
            &startup_info,
            &process_info)) {
            const auto create_process_error = GetLastError();
            CloseHandle(live_log_handle);
            auto& runtime = g_live_runtime_processes[runtime_key_text];
            close_live_runtime_process_handle(runtime);
            runtime.process_id = 0;
            runtime.executable_path = binary_path;
            runtime.config_path = launch_config_path;
            runtime.requested_config_path = requested_config_path;
            runtime.log_path = log_path;
            runtime.command_line = command_line;
            runtime.strategy_ids = normalized_strategy_ids;
            runtime.status = "failed";
            runtime.managed_by = "dashboard";
            runtime.controller_name.clear();
            runtime.started_at_ms = current_time_millis();
            runtime.finished_at_ms = runtime.started_at_ms;
            runtime.exit_code = create_process_error;
            runtime.stop_requested = false;
            runtime.message = "Unable to launch itrader.exe (CreateProcess error " + std::to_string(create_process_error) + ").";

            std::ostringstream json;
            json << '{';
            append_live_runtime_json_locked(json, requested_config_path, false);
            json << '}';
            return json.str();
        }

        CloseHandle(live_log_handle);
        CloseHandle(process_info.hThread);
        auto& runtime = g_live_runtime_processes[runtime_key_text];
        close_live_runtime_process_handle(runtime);
        runtime.process_handle = process_info.hProcess;
        runtime.process_id = process_info.dwProcessId;
        runtime.executable_path = binary_path;
        runtime.config_path = launch_config_path;
        runtime.requested_config_path = requested_config_path;
        runtime.log_path = log_path;
        runtime.command_line = command_line;
        runtime.strategy_ids = normalized_strategy_ids;
        runtime.status = "running";
        runtime.managed_by = "dashboard";
        runtime.controller_name.clear();
        runtime.started_at_ms = current_time_millis();
        runtime.finished_at_ms = 0;
        runtime.exit_code = STILL_ACTIVE;
        runtime.stop_requested = false;
        runtime.message = normalized_strategy_ids.empty()
            ? ("Live runtime launched for " + requested_config_path.filename().generic_string()
                + " (PID " + std::to_string(process_info.dwProcessId) + ").")
            : ("Live runtime launched for " + describe_strategy_scope(normalized_strategy_ids)
                + " from " + requested_config_path.filename().generic_string()
                + " (PID " + std::to_string(process_info.dwProcessId) + ").");

        const auto immediate_wait = WaitForSingleObject(runtime.process_handle, 250);
        if (immediate_wait == WAIT_OBJECT_0 || immediate_wait == WAIT_FAILED) {
            refresh_live_runtime_process_locked(runtime);
        }

        std::ostringstream json;
        json << '{';
        append_live_runtime_json_locked(json, requested_config_path, runtime.status == "running");
        json << '}';
        return json.str();
    } catch (const std::exception& ex) {
        std::scoped_lock guard(g_live_runtime_mutex);
        if (runtime_key_text.empty()) {
            runtime_key_text = live_runtime_key(requested_config_path, normalized_strategy_ids);
        }
        auto& runtime = g_live_runtime_processes[runtime_key_text];
        close_live_runtime_process_handle(runtime);
        runtime.process_id = 0;
        runtime.executable_path = binary_path;
        runtime.config_path = launch_config_path;
        runtime.requested_config_path = requested_config_path;
        runtime.log_path = log_path;
        runtime.command_line.clear();
        runtime.strategy_ids = normalized_strategy_ids;
        runtime.status = "failed";
        runtime.managed_by = "dashboard";
        runtime.controller_name.clear();
        runtime.started_at_ms = current_time_millis();
        runtime.finished_at_ms = runtime.started_at_ms;
        runtime.exit_code = 0;
        runtime.stop_requested = false;
        runtime.message = ex.what();

        std::ostringstream json;
        json << '{';
        append_live_runtime_json_locked(json, requested_config_path, false);
        json << '}';
        return json.str();
    }
}

std::string stop_live_runtime_json(
    const std::filesystem::path& workspace_root,
    std::string_view requested_config,
    const std::vector<std::string>& requested_strategy_ids = {}) {

    std::filesystem::path config_path;
    const auto normalized_strategy_ids = normalize_strategy_id_filter(requested_strategy_ids);

    try {
        config_path = resolve_config_write_path(workspace_root, "live", requested_config);

        std::scoped_lock guard(g_live_runtime_mutex);
        refresh_live_runtime_processes_locked(&workspace_root);

        std::vector<std::string> target_runtime_keys;
        for (const auto& [runtime_key, runtime] : g_live_runtime_processes) {
            if (runtime.status != "running" || runtime.process_handle == nullptr) {
                continue;
            }
            if (!live_runtime_config_matches_requested(runtime, config_path)) {
                continue;
            }
            if (!live_runtime_strategy_ids_match_requested(runtime, normalized_strategy_ids)) {
                continue;
            }
            target_runtime_keys.push_back(runtime_key);
        }

        if (target_runtime_keys.empty()) {
            std::ostringstream json;
            json << '{';
            append_live_runtime_json_locked(
                json,
                config_path,
                true,
                normalized_strategy_ids.empty()
                    ? "No live runtime is currently running for the requested config."
                    : ("No live runtime is currently running for " + describe_strategy_scope(normalized_strategy_ids) + '.'));
            json << '}';
            return json.str();
        }

        bool all_stopped = true;
        std::vector<std::string> stop_messages;
        for (const auto& runtime_key : target_runtime_keys) {
            auto runtime_it = g_live_runtime_processes.find(runtime_key);
            if (runtime_it == g_live_runtime_processes.end()) {
                continue;
            }
            std::string stop_message;
            if (!stop_live_runtime_process_locked(runtime_it->second, stop_message)) {
                all_stopped = false;
            }
            if (!trim_copy(stop_message).empty()) {
                stop_messages.push_back(stop_message);
            }
        }

        const std::string message = stop_messages.empty()
            ? (target_runtime_keys.size() == 1
                ? "Live runtime stopped on request."
                : "Requested live runtimes stopped.")
            : (stop_messages.size() == 1
                ? stop_messages.front()
                : (std::to_string(stop_messages.size()) + " live runtime stop results: " + join_strings(stop_messages, " | ")));

        std::ostringstream json;
        json << '{';
        append_live_runtime_json_locked(json, config_path, all_stopped, message);
        json << '}';
        return json.str();
    } catch (const std::exception& ex) {
        return make_live_runtime_json(config_path, false, ex.what(), &workspace_root);
    }
}

void close_recorder_runtime_process_handle_locked() {
    if (g_recorder_runtime_process.process_handle != nullptr) {
        CloseHandle(g_recorder_runtime_process.process_handle);
        g_recorder_runtime_process.process_handle = nullptr;
    }
}

void refresh_recorder_runtime_process_locked() {
    if (g_recorder_runtime_process.process_handle == nullptr) {
        return;
    }

    const DWORD wait_result = WaitForSingleObject(g_recorder_runtime_process.process_handle, 0);
    if (wait_result == WAIT_TIMEOUT) {
        g_recorder_runtime_process.status = "running";
        if (g_recorder_runtime_process.stop_requested) {
            g_recorder_runtime_process.message = "Stop requested for the recorder; waiting for itrader_ctp_md_recorder.exe to exit.";
        } else if (g_recorder_runtime_process.message.empty()) {
            g_recorder_runtime_process.message = "Recorder is currently running.";
        }
        return;
    }

    if (g_recorder_runtime_process.finished_at_ms == 0) {
        g_recorder_runtime_process.finished_at_ms = current_time_millis();
    }

    if (wait_result == WAIT_FAILED) {
        const auto wait_error = GetLastError();
        g_recorder_runtime_process.exit_code = wait_error;
        g_recorder_runtime_process.status = g_recorder_runtime_process.stop_requested ? "stopped" : "failed";
        g_recorder_runtime_process.message = g_recorder_runtime_process.stop_requested
            ? "Recorder stop was requested, but the final process status could not be queried."
            : ("Unable to query the recorder status (WaitForSingleObject error " + std::to_string(wait_error) + ").");
        g_recorder_runtime_process.stop_requested = false;
        close_recorder_runtime_process_handle_locked();
        return;
    }

    DWORD exit_code = 0;
    if (!GetExitCodeProcess(g_recorder_runtime_process.process_handle, &exit_code)) {
        exit_code = GetLastError();
        g_recorder_runtime_process.status = g_recorder_runtime_process.stop_requested ? "stopped" : "failed";
        g_recorder_runtime_process.message = g_recorder_runtime_process.stop_requested
            ? "Recorder stop was requested, but GetExitCodeProcess could not confirm the final exit code."
            : ("Unable to query the recorder exit code (GetExitCodeProcess error " + std::to_string(exit_code) + ").");
    } else if (g_recorder_runtime_process.stop_requested) {
        g_recorder_runtime_process.status = "stopped";
        g_recorder_runtime_process.message = "Recorder stopped on request.";
    } else {
        g_recorder_runtime_process.status = exit_code == 0 ? "stopped" : "failed";
        g_recorder_runtime_process.message = exit_code == 0
            ? "Recorder exited cleanly."
            : format_recorder_exit_message(exit_code, g_recorder_runtime_process.log_path);
    }

    g_recorder_runtime_process.exit_code = exit_code;
    g_recorder_runtime_process.stop_requested = false;
    close_recorder_runtime_process_handle_locked();
}

bool terminate_process_by_id(DWORD process_id, DWORD timeout_ms) {
    if (process_id == 0) {
        return false;
    }

    HANDLE process_handle = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, process_id);
    if (process_handle == nullptr) {
        return false;
    }

    const bool terminate_requested = TerminateProcess(process_handle, 1) != FALSE;
    const auto wait_result = terminate_requested ? WaitForSingleObject(process_handle, timeout_ms) : WAIT_FAILED;
    CloseHandle(process_handle);
    return terminate_requested && wait_result == WAIT_OBJECT_0;
}

LiveRuntimeProcess current_recorder_runtime_locked(const std::filesystem::path& workspace_root) {
    refresh_recorder_runtime_process_locked();
    if (g_recorder_runtime_process.status == "running" && g_recorder_runtime_process.process_handle != nullptr) {
        return g_recorder_runtime_process;
    }

    if (const auto external_runtime = detect_external_recorder_runtime(workspace_root); external_runtime.has_value()) {
        return *external_runtime;
    }

    return g_recorder_runtime_process;
}

void append_recorder_runtime_json(
    std::ostringstream& json,
    const LiveRuntimeProcess& runtime,
    const std::optional<bool>& ok = std::nullopt,
    std::string_view message_override = {}) {

    const auto config_path_text = runtime.config_path.empty()
        ? std::string {}
        : runtime.config_path.generic_string();
    const auto executable_path_text = runtime.executable_path.empty()
        ? std::string {}
        : runtime.executable_path.generic_string();
    const auto log_path_text = runtime.log_path.empty()
        ? std::string {}
        : runtime.log_path.generic_string();

    const auto managed_by = trim_copy(runtime.managed_by).empty() ? std::string {"dashboard"} : trim_copy(runtime.managed_by);
    const auto controller_name = trim_copy(runtime.controller_name);

    std::string message = trim_copy(message_override);
    if (message.empty()) {
        message = trim_copy(runtime.message);
    }
    if (message.empty()) {
        if (runtime.status == "running") {
            if (runtime.stop_requested) {
                message = "Stop requested for the recorder; waiting for itrader_ctp_md_recorder.exe to exit.";
            } else if (managed_by == "scheduled_task" && !controller_name.empty()) {
                message = "Recorder is currently running under scheduled task \"" + controller_name + "\".";
            } else if (managed_by == "external") {
                message = "Recorder is currently running outside dashboard tracking.";
            } else {
                message = "Recorder is currently running.";
            }
        } else if (runtime.status == "failed") {
            message = "Recorder is not running because the last launch failed.";
        } else if (managed_by == "scheduled_task" && !controller_name.empty()) {
            message = "No task-managed recorder is currently running.";
        } else {
            message = "No recorder is currently running.";
        }
    }

    if (ok.has_value()) {
        json << "\"ok\":" << (*ok ? "true" : "false") << ',';
    }
    json << "\"status\":" << quoted(runtime.status) << ','
         << "\"running\":" << (runtime.status == "running" ? "true" : "false") << ','
         << "\"stop_requested\":" << (runtime.stop_requested ? "true" : "false") << ','
         << "\"process_id\":" << runtime.process_id << ','
         << "\"exit_code\":" << runtime.exit_code << ','
         << "\"auto_restart_enabled\":" << (runtime.auto_restart_enabled ? "true" : "false") << ','
         << "\"auto_restart_count\":" << runtime.auto_restart_count << ','
         << "\"last_auto_restart_at_ms\":" << runtime.last_auto_restart_at_ms << ','
         << "\"managed_by\":" << quoted(managed_by) << ','
         << "\"controller_name\":" << quoted(controller_name) << ','
         << "\"executable_path\":" << quoted(executable_path_text) << ','
         << "\"config_path\":" << quoted(config_path_text) << ','
         << "\"log_path\":" << quoted(log_path_text) << ','
         << "\"started_at_ms\":" << runtime.started_at_ms << ','
         << "\"finished_at_ms\":" << runtime.finished_at_ms << ','
         << "\"message\":" << quoted(message);
}

void append_recorder_runtime_json_locked(
    std::ostringstream& json,
    const std::optional<bool>& ok = std::nullopt,
    std::string_view message_override = {}) {

    append_recorder_runtime_json(json, g_recorder_runtime_process, ok, message_override);
}

bool launch_dashboard_recorder_process_locked(
    const std::filesystem::path& binary_path,
    const std::filesystem::path& config_path,
    const std::filesystem::path& log_path,
    bool truncate_log,
    bool auto_restart_enabled,
    int auto_restart_count,
    long long last_auto_restart_at_ms,
    std::string_view launch_note,
    std::string* error_message) {

    const auto command_line = quote_windows_command_argument(binary_path.wstring())
        + L" --config "
        + quote_windows_command_argument(config_path.wstring());

    std::error_code error_code;
    std::filesystem::create_directories(log_path.parent_path(), error_code);
    if (error_code) {
        if (error_message != nullptr) {
            *error_message = "Unable to create recorder runtime log directory: " + log_path.parent_path().generic_string();
        }
        return false;
    }

    STARTUPINFOW startup_info {};
    startup_info.cb = sizeof(startup_info);
    startup_info.dwFlags |= STARTF_USESTDHANDLES;
    startup_info.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION process_info {};

    SECURITY_ATTRIBUTES security_attributes {};
    security_attributes.nLength = sizeof(security_attributes);
    security_attributes.bInheritHandle = TRUE;

    HANDLE recorder_log_handle = CreateFileW(
        log_path.wstring().c_str(),
        FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        &security_attributes,
        truncate_log ? CREATE_ALWAYS : OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (recorder_log_handle == INVALID_HANDLE_VALUE) {
        const auto log_error = GetLastError();
        if (error_message != nullptr) {
            *error_message = "Unable to open recorder runtime log file (CreateFile error "
                + std::to_string(log_error) + "): " + log_path.generic_string();
        }
        return false;
    }

    if (!launch_note.empty()) {
        const auto line = "[" + current_timestamp() + "] " + std::string(launch_note) + "\r\n";
        DWORD bytes_written = 0;
        WriteFile(
            recorder_log_handle,
            line.data(),
            static_cast<DWORD>(line.size()),
            &bytes_written,
            nullptr);
    }

    startup_info.hStdOutput = recorder_log_handle;
    startup_info.hStdError = recorder_log_handle;

    std::vector<wchar_t> mutable_command_line(command_line.begin(), command_line.end());
    mutable_command_line.push_back(L'\0');
    const auto working_directory = binary_path.parent_path().wstring();

    if (!CreateProcessW(
        binary_path.wstring().c_str(),
        mutable_command_line.data(),
        nullptr,
        nullptr,
        TRUE,
        CREATE_NEW_PROCESS_GROUP,
        nullptr,
        working_directory.c_str(),
        &startup_info,
        &process_info)) {
        const auto create_process_error = GetLastError();
        CloseHandle(recorder_log_handle);
        close_recorder_runtime_process_handle_locked();
        g_recorder_runtime_process.process_id = 0;
        g_recorder_runtime_process.executable_path = binary_path;
        g_recorder_runtime_process.config_path = config_path;
        g_recorder_runtime_process.log_path = log_path;
        g_recorder_runtime_process.command_line = command_line;
        g_recorder_runtime_process.status = "failed";
        g_recorder_runtime_process.managed_by = "dashboard";
        g_recorder_runtime_process.controller_name.clear();
        g_recorder_runtime_process.started_at_ms = current_time_millis();
        g_recorder_runtime_process.finished_at_ms = g_recorder_runtime_process.started_at_ms;
        g_recorder_runtime_process.exit_code = create_process_error;
        g_recorder_runtime_process.stop_requested = false;
        g_recorder_runtime_process.auto_restart_enabled = auto_restart_enabled;
        g_recorder_runtime_process.auto_restart_count = auto_restart_count;
        g_recorder_runtime_process.last_auto_restart_at_ms = last_auto_restart_at_ms;
        g_recorder_runtime_process.message = "Unable to launch itrader_ctp_md_recorder.exe (CreateProcess error "
            + std::to_string(create_process_error) + ").";
        if (error_message != nullptr) {
            *error_message = g_recorder_runtime_process.message;
        }
        return false;
    }

    CloseHandle(recorder_log_handle);
    CloseHandle(process_info.hThread);
    close_recorder_runtime_process_handle_locked();
    g_recorder_runtime_process.process_handle = process_info.hProcess;
    g_recorder_runtime_process.process_id = process_info.dwProcessId;
    g_recorder_runtime_process.executable_path = binary_path;
    g_recorder_runtime_process.config_path = config_path;
    g_recorder_runtime_process.log_path = log_path;
    g_recorder_runtime_process.command_line = command_line;
    g_recorder_runtime_process.status = "running";
    g_recorder_runtime_process.managed_by = "dashboard";
    g_recorder_runtime_process.controller_name.clear();
    g_recorder_runtime_process.started_at_ms = current_time_millis();
    g_recorder_runtime_process.finished_at_ms = 0;
    g_recorder_runtime_process.exit_code = STILL_ACTIVE;
    g_recorder_runtime_process.stop_requested = false;
    g_recorder_runtime_process.auto_restart_enabled = auto_restart_enabled;
    g_recorder_runtime_process.auto_restart_count = auto_restart_count;
    g_recorder_runtime_process.last_auto_restart_at_ms = last_auto_restart_at_ms;
    g_recorder_runtime_process.message = launch_note.empty()
        ? ("Recorder launched for " + config_path.filename().generic_string()
            + " (PID " + std::to_string(process_info.dwProcessId) + ").")
        : (std::string(launch_note) + " PID " + std::to_string(process_info.dwProcessId) + '.');

    const auto immediate_wait = WaitForSingleObject(g_recorder_runtime_process.process_handle, 250);
    if (immediate_wait == WAIT_OBJECT_0 || immediate_wait == WAIT_FAILED) {
        refresh_recorder_runtime_process_locked();
    }

    if (error_message != nullptr) {
        error_message->clear();
    }
    return g_recorder_runtime_process.status == "running";
}

bool should_auto_restart_recorder_locked(
    const LiveRuntimeProcess& runtime,
    const std::filesystem::path& workspace_root,
    long long now_ms) {

    if (runtime.status == "running"
        || runtime.stop_requested
        || !runtime.auto_restart_enabled
        || runtime.managed_by != "dashboard"
        || runtime.started_at_ms <= 0
        || (runtime.process_id == 0 && runtime.auto_restart_count == 0)) {
        return false;
    }
    if (!recorder_auto_restart_enabled(workspace_root)) {
        return false;
    }
    constexpr long long min_restart_interval_ms = 5000;
    return runtime.last_auto_restart_at_ms <= 0
        || now_ms - runtime.last_auto_restart_at_ms >= min_restart_interval_ms;
}

bool auto_restart_recorder_locked(const std::filesystem::path& workspace_root) {
    const auto now_ms = current_time_millis();
    if (!should_auto_restart_recorder_locked(g_recorder_runtime_process, workspace_root, now_ms)) {
        return false;
    }

    const auto previous_message = trim_copy(g_recorder_runtime_process.message);
    const auto previous_exit_code = g_recorder_runtime_process.exit_code;
    const auto next_restart_count = g_recorder_runtime_process.auto_restart_count + 1;
    const auto watchdog_log = recorder_watchdog_log_path(workspace_root);
    append_dashboard_watchdog_log(
        watchdog_log,
        "Recorder watchdog detected an unexpected recorder stop"
        + (previous_message.empty() ? std::string {} : ": " + previous_message));

    std::filesystem::path config_path;
    std::filesystem::path binary_path;
    std::filesystem::path log_path;
    try {
        config_path = recorder_config_path(workspace_root);
        binary_path = discover_recorder_runtime_binary_path(workspace_root);
        log_path = recorder_runtime_log_path(workspace_root);
    } catch (const std::exception& ex) {
        g_recorder_runtime_process.status = "failed";
        g_recorder_runtime_process.message = std::string("Recorder watchdog could not prepare auto-restart: ") + ex.what();
        g_recorder_runtime_process.last_auto_restart_at_ms = now_ms;
        append_dashboard_watchdog_log(watchdog_log, g_recorder_runtime_process.message);
        return false;
    }

    std::string launch_error;
    const auto note = "Recorder auto-restarted by dashboard watchdog after unexpected stop"
        + (previous_exit_code == STILL_ACTIVE ? std::string {} : " (previous exit_code=" + std::to_string(previous_exit_code) + ")");
    const bool launched = launch_dashboard_recorder_process_locked(
        binary_path,
        config_path,
        log_path,
        false,
        true,
        next_restart_count,
        now_ms,
        note,
        &launch_error);

    append_dashboard_watchdog_log(
        watchdog_log,
        launched
            ? ("Recorder watchdog restart succeeded; pid=" + std::to_string(g_recorder_runtime_process.process_id))
            : ("Recorder watchdog restart did not produce a running process: "
                + (launch_error.empty() ? trim_copy(g_recorder_runtime_process.message) : launch_error)));
    return launched;
}

std::string make_recorder_runtime_json(
    const std::filesystem::path& workspace_root,
    const std::optional<bool>& ok,
    std::string_view message_override) {

    std::scoped_lock guard(g_recorder_runtime_mutex);
    auto runtime = current_recorder_runtime_locked(workspace_root);
    if (auto_restart_recorder_locked(workspace_root)) {
        runtime = g_recorder_runtime_process;
    } else {
        runtime = g_recorder_runtime_process.status == "running" && g_recorder_runtime_process.process_handle != nullptr
            ? g_recorder_runtime_process
            : runtime;
    }

    std::ostringstream json;
    json << '{';
    append_recorder_runtime_json(json, runtime, ok, message_override);
    json << '}';
    return json.str();
}

std::string start_recorder_runtime_json(const std::filesystem::path& workspace_root) {
    std::filesystem::path config_path;
    std::filesystem::path binary_path;
    std::filesystem::path log_path;

    try {
        config_path = recorder_config_path(workspace_root);
        binary_path = discover_recorder_runtime_binary_path(workspace_root);
        log_path = recorder_runtime_log_path(workspace_root);
        if (!std::filesystem::exists(config_path)) {
            throw std::runtime_error("Recorder config file does not exist: " + config_path.generic_string());
        }

        const auto ini = itrader::IniFile::parse(config_path);
        const auto account_sections = ini.sections_with_prefix("account.");
        if (account_sections.empty()) {
            throw std::runtime_error("Recorder config must define exactly one [account.*] section.");
        }

        const auto& account_section = account_sections.front();
        const auto broker_id = ini.get(account_section, "md_broker_id", ini.get(account_section, "broker_id"));
        const auto user_id = ini.get(account_section, "md_user_id", ini.get(account_section, "user_id"));
        const auto password = ini.get(account_section, "md_password", ini.get(account_section, "password"));
        std::vector<std::string> missing_keys;
        const auto collect_if_missing = [&missing_keys](std::string_view key, const std::string& value) {
            if (trim_copy(value).empty() || looks_like_placeholder_config_value(value)) {
                missing_keys.emplace_back(key);
            }
        };
        collect_if_missing("md_broker_id", broker_id);
        collect_if_missing("md_user_id", user_id);
        collect_if_missing("md_password", password);
        if (!missing_keys.empty()) {
            throw std::runtime_error("Recorder prerequisites missing or still placeholder: " + join_strings(missing_keys, ", "));
        }

        {
            std::scoped_lock guard(g_recorder_runtime_mutex);
            const auto current_runtime = current_recorder_runtime_locked(workspace_root);
            if (current_runtime.status == "running") {
                std::ostringstream json;
                json << '{';
                append_recorder_runtime_json(json, current_runtime, true, "Recorder is already running.");
                json << '}';
                return json.str();
            }
        }

        const bool auto_restart_enabled = ini.get_bool("recorder", "auto_restart_enabled", true);
        const auto managed_task_name = managed_recorder_task_name(workspace_root);
        if (!managed_task_name.empty() && run_schtasks_action(managed_task_name, L"Run")) {
            return make_recorder_runtime_json(
                workspace_root,
                true,
                "Requested start of the task-managed recorder.");
        }

        std::scoped_lock guard(g_recorder_runtime_mutex);
        const auto current_runtime = current_recorder_runtime_locked(workspace_root);
        if (current_runtime.status == "running") {
            std::ostringstream json;
            json << '{';
            append_recorder_runtime_json(json, current_runtime, true, "Recorder is already running.");
            json << '}';
            return json.str();
        }

        std::string launch_error;
        launch_dashboard_recorder_process_locked(
            binary_path,
            config_path,
            log_path,
            true,
            auto_restart_enabled,
            0,
            0,
            {},
            &launch_error);

        std::ostringstream json;
        json << '{';
        append_recorder_runtime_json_locked(json, g_recorder_runtime_process.status == "running");
        json << '}';
        return json.str();
    } catch (const std::exception& ex) {
        std::scoped_lock guard(g_recorder_runtime_mutex);
        close_recorder_runtime_process_handle_locked();
        g_recorder_runtime_process.process_id = 0;
        g_recorder_runtime_process.executable_path = binary_path;
        g_recorder_runtime_process.config_path = config_path;
        g_recorder_runtime_process.log_path = log_path;
        g_recorder_runtime_process.command_line.clear();
        g_recorder_runtime_process.status = "failed";
        g_recorder_runtime_process.managed_by = "dashboard";
        g_recorder_runtime_process.controller_name.clear();
        g_recorder_runtime_process.started_at_ms = current_time_millis();
        g_recorder_runtime_process.finished_at_ms = g_recorder_runtime_process.started_at_ms;
        g_recorder_runtime_process.exit_code = 0;
        g_recorder_runtime_process.stop_requested = false;
        g_recorder_runtime_process.auto_restart_enabled = false;
        g_recorder_runtime_process.auto_restart_count = 0;
        g_recorder_runtime_process.last_auto_restart_at_ms = 0;
        g_recorder_runtime_process.message = ex.what();

        std::ostringstream json;
        json << '{';
        append_recorder_runtime_json_locked(json, false);
        json << '}';
        return json.str();
    }
}

std::string stop_recorder_runtime_json(const std::filesystem::path& workspace_root) {
    try {
        LiveRuntimeProcess current_runtime;
        bool dashboard_managed = false;
        {
            std::scoped_lock guard(g_recorder_runtime_mutex);
            current_runtime = current_recorder_runtime_locked(workspace_root);
            dashboard_managed = g_recorder_runtime_process.status == "running"
                && g_recorder_runtime_process.process_handle != nullptr
                && g_recorder_runtime_process.process_id == current_runtime.process_id;

            if (current_runtime.status != "running") {
                g_recorder_runtime_process.status = "stopped";
                g_recorder_runtime_process.stop_requested = false;
                if (g_recorder_runtime_process.finished_at_ms == 0) {
                    g_recorder_runtime_process.finished_at_ms = current_time_millis();
                }
                g_recorder_runtime_process.exit_code = 0;
                g_recorder_runtime_process.auto_restart_enabled = false;
                g_recorder_runtime_process.message = "No recorder is currently running.";
                std::ostringstream json;
                json << '{';
                append_recorder_runtime_json(json, current_runtime, true, "No recorder is currently running.");
                json << '}';
                return json.str();
            }

            if (dashboard_managed) {
                g_recorder_runtime_process.stop_requested = true;
                g_recorder_runtime_process.auto_restart_enabled = false;
                g_recorder_runtime_process.message = "Stop requested for the recorder; waiting for itrader_ctp_md_recorder.exe to exit.";

                std::string stop_message;
                if (!GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, g_recorder_runtime_process.process_id)) {
                    const auto break_error = GetLastError();
                    if (!TerminateProcess(g_recorder_runtime_process.process_handle, 1)) {
                        const auto terminate_error = GetLastError();
                        g_recorder_runtime_process.stop_requested = false;
                        std::ostringstream json;
                        json << '{';
                        append_recorder_runtime_json_locked(
                            json,
                            false,
                            "Unable to stop the recorder (CTRL_BREAK error " + std::to_string(break_error)
                                + ", TerminateProcess error " + std::to_string(terminate_error) + ").");
                        json << '}';
                        return json.str();
                    }

                    stop_message = "Recorder did not accept CTRL_BREAK_EVENT, so TerminateProcess was used.";
                }

                const auto wait_result = WaitForSingleObject(g_recorder_runtime_process.process_handle, 3000);
                if (wait_result == WAIT_TIMEOUT) {
                    std::ostringstream json;
                    json << '{';
                    append_recorder_runtime_json_locked(
                        json,
                        true,
                        stop_message.empty()
                            ? "Stop requested for the recorder; itrader_ctp_md_recorder.exe is still shutting down."
                            : stop_message);
                    json << '}';
                    return json.str();
                }

                g_recorder_runtime_process.status = "stopped";
                g_recorder_runtime_process.message = stop_message.empty()
                    ? "Recorder stopped on request."
                    : stop_message;
                g_recorder_runtime_process.stop_requested = false;
                g_recorder_runtime_process.auto_restart_enabled = false;

                std::ostringstream json;
                json << '{';
                append_recorder_runtime_json_locked(json, true);
                json << '}';
                return json.str();
            }
        }

        std::string stop_message = current_runtime.managed_by == "scheduled_task" && !current_runtime.controller_name.empty()
            ? ("Requested stop of scheduled task \"" + current_runtime.controller_name + "\".")
            : "Recorder stopped on request.";

        if (current_runtime.managed_by == "scheduled_task" && !current_runtime.controller_name.empty()) {
            run_schtasks_action(current_runtime.controller_name, L"End");
        }

        auto remaining_runtime = detect_external_recorder_runtime(workspace_root);
        if (remaining_runtime.has_value() && remaining_runtime->process_id == current_runtime.process_id) {
            if (!terminate_process_by_id(current_runtime.process_id, 3000)) {
                std::ostringstream json;
                json << '{';
                append_recorder_runtime_json(
                    json,
                    *remaining_runtime,
                    false,
                    "Unable to stop the externally managed recorder process.");
                json << '}';
                return json.str();
            }
            remaining_runtime = detect_external_recorder_runtime(workspace_root);
        }

        {
            std::scoped_lock guard(g_recorder_runtime_mutex);
            close_recorder_runtime_process_handle_locked();
            g_recorder_runtime_process.process_id = 0;
            g_recorder_runtime_process.executable_path = current_runtime.executable_path;
            g_recorder_runtime_process.config_path = current_runtime.config_path;
            g_recorder_runtime_process.log_path = current_runtime.log_path;
            g_recorder_runtime_process.command_line.clear();
            g_recorder_runtime_process.status = "stopped";
            g_recorder_runtime_process.managed_by = current_runtime.managed_by;
            g_recorder_runtime_process.controller_name = current_runtime.controller_name;
            g_recorder_runtime_process.started_at_ms = current_runtime.started_at_ms;
            g_recorder_runtime_process.finished_at_ms = current_time_millis();
            g_recorder_runtime_process.exit_code = 0;
            g_recorder_runtime_process.stop_requested = false;
            g_recorder_runtime_process.auto_restart_enabled = false;
            g_recorder_runtime_process.message = stop_message;
        }

        LiveRuntimeProcess stopped_runtime = current_runtime;
        stopped_runtime.process_id = 0;
        stopped_runtime.status = "stopped";
        stopped_runtime.stop_requested = false;
        stopped_runtime.auto_restart_enabled = false;
        stopped_runtime.finished_at_ms = current_time_millis();
        stopped_runtime.exit_code = 0;
        stopped_runtime.message = stop_message;

        std::ostringstream json;
        json << '{';
        append_recorder_runtime_json(json, stopped_runtime, true);
        json << '}';
        return json.str();
    } catch (const std::exception& ex) {
        return make_recorder_runtime_json(workspace_root, false, ex.what());
    }
}

HttpResponse serve_static_file(const std::filesystem::path& workspace_root, const std::string& path) {
    std::filesystem::path target;
    std::string content_type = "text/plain; charset=utf-8";

    if (path == "/" || path == "/index.html") {
        target = workspace_root / "ui" / "index.html";
        content_type = "text/html; charset=utf-8";
    } else if (path == "/styles.css") {
        target = workspace_root / "ui" / "styles.css";
        content_type = "text/css; charset=utf-8";
    } else if (path == "/app.js") {
        target = workspace_root / "ui" / "app.js";
        content_type = "application/javascript; charset=utf-8";
    } else {
        return HttpResponse {"404 Not Found", "text/plain; charset=utf-8", "Not found"};
    }

    return HttpResponse {"200 OK", content_type, read_text_file(target)};
}

HttpResponse serve_deploy_file(const std::filesystem::path& workspace_root, const std::string& path) {
    static constexpr std::string_view kPrefix = "/deploy/";
    if (path.rfind(std::string(kPrefix), 0) != 0) {
        return HttpResponse {"404 Not Found", "text/plain; charset=utf-8", "Not found"};
    }

    const auto filename = path.substr(kPrefix.size());
    if (filename.empty() || filename.find('/') != std::string::npos || filename.find('\\') != std::string::npos) {
        return HttpResponse {"400 Bad Request", "text/plain; charset=utf-8", "Invalid deploy filename"};
    }

    const std::filesystem::path deploy_root = (workspace_root / "deploy_share").lexically_normal();
    const std::filesystem::path target = (deploy_root / std::filesystem::path(filename).filename()).lexically_normal();
    if (normalize_path_for_compare(target.parent_path()) != normalize_path_for_compare(deploy_root)) {
        return HttpResponse {"400 Bad Request", "text/plain; charset=utf-8", "Invalid deploy path"};
    }

    std::error_code error_code;
    if (!std::filesystem::exists(target, error_code) || !std::filesystem::is_regular_file(target, error_code)) {
        return HttpResponse {"404 Not Found", "text/plain; charset=utf-8", "Deploy file not found"};
    }

    std::string content_type = "application/octet-stream";
    if (lower_copy(target.extension().generic_string()) == ".zip") {
        content_type = "application/zip";
    } else if (lower_copy(target.extension().generic_string()) == ".txt") {
        content_type = "text/plain; charset=utf-8";
    }

    return HttpResponse {
        "200 OK",
        content_type,
        read_text_file(target),
        {{"Content-Disposition", "attachment; filename=\"" + target.filename().generic_string() + "\""}}
    };
}

HttpResponse route_request(const std::filesystem::path& workspace_root, const HttpRequest& request) {
    if (!request_is_authorized(workspace_root, request)) {
        return make_unauthorized_response();
    }

    const auto query_offset = request.target.find('?');
    const auto path = query_offset == std::string::npos ? request.target : request.target.substr(0, query_offset);
    const auto query = query_offset == std::string::npos ? std::string {} : request.target.substr(query_offset + 1);

    if (path.rfind("/deploy/", 0) == 0) {
        if (request.method != "GET") {
            return HttpResponse {"405 Method Not Allowed", "text/plain; charset=utf-8", "Only GET is supported for /deploy"};
        }
        return serve_deploy_file(workspace_root, path);
    }

    if (path == "/api/config") {
        if (request.method != "POST") {
            return HttpResponse {"405 Method Not Allowed", "text/plain; charset=utf-8", "Only POST is supported for /api/config"};
        }

        const std::string normalized_mode = get_query_value(query, "mode") == "live" ? "live" : "backtest";
        const auto requested_config = get_query_value(query, "config");
        const auto config_path = resolve_config_write_path(workspace_root, normalized_mode, requested_config);
        std::filesystem::create_directories(config_path.parent_path());
        write_text_file(config_path, request.body);

        std::ostringstream json;
        json << '{'
             << "\"ok\":true,"
             << "\"mode\":" << quoted(normalized_mode) << ','
             << "\"config_path\":" << quoted(config_path.generic_string())
             << '}';
        return HttpResponse {"200 OK", "application/json; charset=utf-8", json.str()};
    }

    if (path == "/api/recorder-config") {
        const auto config_path = recorder_config_path(workspace_root);

        if (request.method == "POST") {
            std::filesystem::create_directories(config_path.parent_path());
            write_text_file(config_path, request.body);
            return HttpResponse {"200 OK", "application/json; charset=utf-8", make_recorder_config_json(workspace_root)};
        }

        if (request.method == "GET") {
            return HttpResponse {"200 OK", "application/json; charset=utf-8", make_recorder_config_json(workspace_root)};
        }

        return HttpResponse {"405 Method Not Allowed", "text/plain; charset=utf-8", "Only GET and POST are supported for /api/recorder-config"};
    }

    if (path == "/api/recorder-run") {
        if (request.method == "POST") {
            return HttpResponse {"200 OK", "application/json; charset=utf-8", start_recorder_runtime_json(workspace_root)};
        }
        if (request.method == "GET") {
            return HttpResponse {"200 OK", "application/json; charset=utf-8", make_recorder_runtime_json(workspace_root, true)};
        }
        return HttpResponse {"405 Method Not Allowed", "text/plain; charset=utf-8", "Only GET and POST are supported for /api/recorder-run"};
    }

    if (path == "/api/recorder-run-stop") {
        if (request.method != "POST") {
            return HttpResponse {"405 Method Not Allowed", "text/plain; charset=utf-8", "Only POST is supported for /api/recorder-run-stop"};
        }
        return HttpResponse {"200 OK", "application/json; charset=utf-8", stop_recorder_runtime_json(workspace_root)};
    }

    if (path == "/api/live-inventory-adjustments") {
        const auto requested_config = get_query_value(query, "config");
        const auto config_path = resolve_config_write_path(workspace_root, "live", requested_config);
        const auto adjustments_path = strategy_inventory_adjustments_path(config_path);

        if (request.method == "POST") {
            std::filesystem::create_directories(adjustments_path.parent_path());
            write_text_file(adjustments_path, request.body);
            return HttpResponse {"200 OK", "application/json; charset=utf-8", make_live_inventory_json_from_config_path(config_path)};
        }

        if (request.method == "GET") {
            return HttpResponse {"200 OK", "application/json; charset=utf-8", make_live_inventory_json_from_config_path(config_path)};
        }

        return HttpResponse {"405 Method Not Allowed", "text/plain; charset=utf-8", "Only GET and POST are supported for /api/live-inventory-adjustments"};
    }

    if (path == "/api/live-inventory-store") {
        const auto requested_config = get_query_value(query, "config");
        const auto config_path = resolve_config_write_path(workspace_root, "live", requested_config);

        if (request.method == "POST") {
            return HttpResponse {"200 OK", "application/json; charset=utf-8", save_live_inventory_store_edits_json(workspace_root, config_path, request.body)};
        }

        if (request.method == "GET") {
            return HttpResponse {"200 OK", "application/json; charset=utf-8", make_live_inventory_json_from_config_path(config_path)};
        }

        return HttpResponse {"405 Method Not Allowed", "text/plain; charset=utf-8", "Only GET and POST are supported for /api/live-inventory-store"};
    }

    if (path == "/api/backtest-run") {
        if (request.method == "POST") {
            const auto config_name = get_query_value(query, "config");
            const auto detail_level = get_query_value(query, "detail");
            return HttpResponse {"200 OK", "application/json; charset=utf-8", start_backtest_replay_job_json(workspace_root, config_name, detail_level)};
        }
        if (request.method == "GET") {
            const auto job_id = get_query_value(query, "id");
            return HttpResponse {"200 OK", "application/json; charset=utf-8", make_backtest_job_json(find_backtest_job(job_id))};
        }
        return HttpResponse {"405 Method Not Allowed", "text/plain; charset=utf-8", "Only GET and POST are supported for /api/backtest-run"};
    }

    if (path == "/api/backtest-run-cancel") {
        if (request.method != "POST") {
            return HttpResponse {"405 Method Not Allowed", "text/plain; charset=utf-8", "Only POST is supported for /api/backtest-run-cancel"};
        }
        const auto job_id = get_query_value(query, "id");
        return HttpResponse {"200 OK", "application/json; charset=utf-8", cancel_backtest_replay_job_json(job_id)};
    }

    if (path == "/api/live-run") {
        const auto requested_config = get_query_value(query, "config");
        const auto requested_strategy_ids = parse_strategy_id_filter(get_query_value(query, "strategy_ids"));
        if (request.method == "POST") {
            return HttpResponse {
                "200 OK",
                "application/json; charset=utf-8",
                start_live_runtime_json(workspace_root, requested_config, requested_strategy_ids)
            };
        }
        if (request.method == "GET") {
            const auto config_path = resolve_config_write_path(workspace_root, "live", requested_config);
            return HttpResponse {"200 OK", "application/json; charset=utf-8", make_live_runtime_json(config_path, true, {}, &workspace_root)};
        }
        return HttpResponse {"405 Method Not Allowed", "text/plain; charset=utf-8", "Only GET and POST are supported for /api/live-run"};
    }

    if (path == "/api/live-run-stop") {
        if (request.method != "POST") {
            return HttpResponse {"405 Method Not Allowed", "text/plain; charset=utf-8", "Only POST is supported for /api/live-run-stop"};
        }
        const auto requested_config = get_query_value(query, "config");
        const auto requested_strategy_ids = parse_strategy_id_filter(get_query_value(query, "strategy_ids"));
        return HttpResponse {
            "200 OK",
            "application/json; charset=utf-8",
            stop_live_runtime_json(workspace_root, requested_config, requested_strategy_ids)
        };
    }

    if (path == "/api/strategy-files/upload") {
        if (request.method != "POST") {
            return HttpResponse {"405 Method Not Allowed", "text/plain; charset=utf-8", "Only POST is supported for /api/strategy-files/upload"};
        }

        const auto mode = get_query_value(query, "mode");
        const auto config_name = get_query_value(query, "config");
        const auto overwrite = lower_copy(get_query_value(query, "overwrite"));
        const bool allow_overwrite = overwrite == "1" || overwrite == "true" || overwrite == "yes";
        try {
            return HttpResponse {
                "200 OK",
                "application/json; charset=utf-8",
                make_strategy_file_upload_json(workspace_root, mode, config_name, request, allow_overwrite)
            };
        } catch (const std::exception& ex) {
            std::ostringstream json;
            json << '{'
                 << "\"ok\":false,"
                 << "\"message\":" << quoted(ex.what())
                 << '}';
            return HttpResponse {"400 Bad Request", "application/json; charset=utf-8", json.str()};
        }
    }

    if (request.method != "GET") {
        return HttpResponse {"405 Method Not Allowed", "text/plain; charset=utf-8", "Only GET is supported"};
    }

    if (path == "/api/health") {
        return HttpResponse {"200 OK", "application/json; charset=utf-8", "{\"ok\":true,\"service\":\"itrader_ui_api\"}"};
    }

    if (path == "/api/strategy-files") {
        const auto mode = get_query_value(query, "mode");
        const auto config_name = get_query_value(query, "config");
        return HttpResponse {"200 OK", "application/json; charset=utf-8", make_strategy_file_catalog_json(workspace_root, mode, config_name)};
    }

    if (path == "/api/pick-strategy-file") {
        const auto mode = get_query_value(query, "mode");
        const auto config_name = get_query_value(query, "config");
        return HttpResponse {"200 OK", "application/json; charset=utf-8", make_strategy_file_pick_json(workspace_root, mode, config_name)};
    }

    if (path == "/api/pick-backtest-directory") {
        const auto mode = get_query_value(query, "mode");
        const auto config_name = get_query_value(query, "config");
        const auto current_directory = get_query_value(query, "current");
        return HttpResponse {"200 OK", "application/json; charset=utf-8", make_backtest_directory_pick_json(workspace_root, mode, config_name, current_directory)};
    }

    if (path == "/api/state") {
        const auto mode = get_query_value(query, "mode");
        const auto config_name = get_query_value(query, "config");
        const auto replay = lower_copy(get_query_value(query, "replay"));
        const bool enable_backtest_replay = replay == "1" || replay == "true" || replay == "yes" || replay == "full" || replay == "summary";
        const auto detail_level = replay == "summary"
            ? std::string {"summary"}
            : get_query_value(query, "detail");
        return HttpResponse {
            "200 OK",
            "application/json; charset=utf-8",
            make_ui_state_json(workspace_root, mode, config_name, enable_backtest_replay, nullptr, detail_level)
        };
    }

    return serve_static_file(workspace_root, path);
}

std::string make_http_message(const HttpResponse& response) {
    std::ostringstream stream;
    stream << "HTTP/1.1 " << response.status << "\r\n";
    stream << "Content-Type: " << response.content_type << "\r\n";
    stream << "Content-Length: " << response.body.size() << "\r\n";
    for (const auto& [header_name, header_value] : response.headers) {
        stream << header_name << ": " << header_value << "\r\n";
    }
    stream << "Access-Control-Allow-Origin: *\r\n";
    stream << "Cache-Control: no-store\r\n";
    stream << "Connection: close\r\n\r\n";
    stream << response.body;
    return stream.str();
}

std::string make_sse_event_message(std::string_view event_name, std::string_view payload) {
    std::ostringstream output;
    if (!event_name.empty()) {
        output << "event: " << event_name << "\n";
    }

    std::istringstream input {std::string(payload)};
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        output << "data: " << line << "\n";
    }
    output << '\n';
    return output.str();
}

std::string file_change_marker(const std::filesystem::path& file_path) {
    std::error_code error_code;
    const bool exists = std::filesystem::exists(file_path, error_code);
    if (error_code || !exists) {
        return file_path.generic_string() + "|missing";
    }

    const auto last_write = std::filesystem::last_write_time(file_path, error_code);
    if (error_code) {
        return file_path.generic_string() + "|exists|unknown";
    }

    const auto raw_count = last_write.time_since_epoch().count();
    return file_path.generic_string() + "|exists|" + std::to_string(raw_count);
}

std::string live_runtime_change_marker(const std::filesystem::path& requested_config_path) {
    std::scoped_lock guard(g_live_runtime_mutex);
    refresh_live_runtime_processes_locked();
    std::ostringstream marker;
    marker << "live|count=" << g_live_runtime_processes.size()
           << "|req=" << (requested_config_path.empty() ? std::string {} : requested_config_path.generic_string());
    std::size_t index = 0;
    for (const auto& [runtime_key, runtime] : g_live_runtime_processes) {
        (void)runtime_key;
        marker << "|rt" << index
               << "=" << runtime.status
               << ",pid=" << runtime.process_id
               << ",stop=" << (runtime.stop_requested ? "1" : "0")
               << ",exit=" << runtime.exit_code
               << ",match=" << (live_runtime_config_matches_requested(runtime, requested_config_path) ? "1" : "0")
               << ",cfg=" << (runtime.config_path.empty() ? std::string {} : runtime.config_path.generic_string());
        ++index;
    }
    return marker.str();
}

std::vector<std::filesystem::path> live_runtime_config_paths_for_request(const std::filesystem::path& requested_config_path) {
    std::vector<std::filesystem::path> config_paths;
    std::scoped_lock guard(g_live_runtime_mutex);
    refresh_live_runtime_processes_locked();
    for (const auto& [runtime_key, runtime] : g_live_runtime_processes) {
        (void)runtime_key;
        if (runtime.config_path.empty() || !live_runtime_config_matches_requested(runtime, requested_config_path)) {
            continue;
        }
        if (std::find(config_paths.begin(), config_paths.end(), runtime.config_path) == config_paths.end()) {
            config_paths.push_back(runtime.config_path);
        }
    }
    return config_paths;
}

std::string recorder_runtime_change_marker(const std::filesystem::path& workspace_root) {
    std::scoped_lock guard(g_recorder_runtime_mutex);
    const auto runtime = current_recorder_runtime_locked(workspace_root);
    std::ostringstream marker;
    marker << "recorder|status=" << runtime.status
           << "|pid=" << runtime.process_id
           << "|stop=" << (runtime.stop_requested ? "1" : "0")
           << "|exit=" << runtime.exit_code
           << "|autorestart=" << (runtime.auto_restart_enabled ? "1" : "0")
           << "|restart_count=" << runtime.auto_restart_count
           << "|managed=" << runtime.managed_by
           << "|controller=" << runtime.controller_name;
    return marker.str();
}

std::string compute_state_stream_change_marker(
    const std::filesystem::path& workspace_root,
    std::string_view mode,
    std::string_view requested_config) {

    std::ostringstream marker;
    marker << "mode=" << mode << ';';
    try {
        const auto config_path = resolve_config_write_path(workspace_root, mode, requested_config);
        marker << file_change_marker(config_path) << ';';

        if (mode == "live") {
            marker << file_change_marker(live_telemetry_path(config_path)) << ';';
            marker << file_change_marker(strategy_inventory_store_path(config_path)) << ';';
            marker << file_change_marker(strategy_inventory_adjustments_path(config_path)) << ';';
            for (const auto& runtime_config_path : live_runtime_config_paths_for_request(config_path)) {
                if (normalize_path_for_compare(runtime_config_path) == normalize_path_for_compare(config_path)) {
                    continue;
                }
                marker << file_change_marker(live_telemetry_path(runtime_config_path)) << ';';
                marker << file_change_marker(strategy_inventory_store_path(runtime_config_path)) << ';';
                marker << file_change_marker(strategy_inventory_adjustments_path(runtime_config_path)) << ';';
            }
            marker << file_change_marker(recorder_config_path(workspace_root)) << ';';
            marker << live_runtime_change_marker(config_path) << ';';
            marker << recorder_runtime_change_marker(workspace_root) << ';';
        }
    } catch (const std::exception& ex) {
        marker << "resolve_error=" << ex.what() << ';';
    }

    return marker.str();
}

std::optional<std::filesystem::path> existing_watch_directory(std::filesystem::path candidate) {
    if (candidate.empty()) {
        return std::nullopt;
    }

    std::error_code error;
    if (std::filesystem::exists(candidate, error) && !std::filesystem::is_directory(candidate, error)) {
        candidate = candidate.parent_path();
    }

    while (!candidate.empty() && !std::filesystem::exists(candidate, error)) {
        candidate = candidate.parent_path();
    }

    if (candidate.empty() || !std::filesystem::is_directory(candidate, error)) {
        return std::nullopt;
    }

    return std::filesystem::absolute(candidate, error).lexically_normal();
}

void append_unique_watch_directory(std::vector<std::filesystem::path>& directories, const std::filesystem::path& candidate) {
    const auto directory = existing_watch_directory(candidate);
    if (!directory.has_value()) {
        return;
    }

    if (std::find(directories.begin(), directories.end(), *directory) == directories.end()) {
        directories.push_back(*directory);
    }
}

std::vector<std::filesystem::path> collect_state_stream_watch_directories(
    const std::filesystem::path& workspace_root,
    std::string_view mode,
    std::string_view requested_config) {

    std::vector<std::filesystem::path> directories;
    append_unique_watch_directory(directories, workspace_root);

    try {
        const auto config_path = resolve_config_write_path(workspace_root, mode, requested_config);
        append_unique_watch_directory(directories, config_path.parent_path());

        if (mode == "live") {
            append_unique_watch_directory(directories, live_telemetry_path(config_path).parent_path());
            append_unique_watch_directory(directories, strategy_inventory_store_path(config_path).parent_path());
            append_unique_watch_directory(directories, strategy_inventory_adjustments_path(config_path).parent_path());
            for (const auto& runtime_config_path : live_runtime_config_paths_for_request(config_path)) {
                append_unique_watch_directory(directories, live_telemetry_path(runtime_config_path).parent_path());
                append_unique_watch_directory(directories, strategy_inventory_store_path(runtime_config_path).parent_path());
                append_unique_watch_directory(directories, strategy_inventory_adjustments_path(runtime_config_path).parent_path());
            }
            append_unique_watch_directory(directories, recorder_config_path(workspace_root).parent_path());
        }
    } catch (const std::exception&) {
        // The state endpoint will surface the real error; the stream still watches the workspace for recovery.
    }

    return directories;
}

class DirectoryChangeWaiter {
public:
    explicit DirectoryChangeWaiter(const std::vector<std::filesystem::path>& directories) {
        static constexpr DWORD kChangeFlags = FILE_NOTIFY_CHANGE_FILE_NAME
            | FILE_NOTIFY_CHANGE_DIR_NAME
            | FILE_NOTIFY_CHANGE_ATTRIBUTES
            | FILE_NOTIFY_CHANGE_SIZE
            | FILE_NOTIFY_CHANGE_LAST_WRITE
            | FILE_NOTIFY_CHANGE_CREATION;

        handles_.reserve(std::min<std::size_t>(directories.size(), MAXIMUM_WAIT_OBJECTS));
        for (const auto& directory : directories) {
            if (handles_.size() >= MAXIMUM_WAIT_OBJECTS) {
                break;
            }

            const auto wide_path = directory.wstring();
            HANDLE handle = FindFirstChangeNotificationW(wide_path.c_str(), FALSE, kChangeFlags);
            if (handle != INVALID_HANDLE_VALUE && handle != nullptr) {
                handles_.push_back(handle);
            }
        }
    }

    DirectoryChangeWaiter(const DirectoryChangeWaiter&) = delete;
    DirectoryChangeWaiter& operator=(const DirectoryChangeWaiter&) = delete;

    ~DirectoryChangeWaiter() {
        for (HANDLE handle : handles_) {
            FindCloseChangeNotification(handle);
        }
    }

    bool wait_for_change(std::chrono::milliseconds timeout) {
        if (timeout.count() <= 0) {
            return false;
        }

        if (handles_.empty()) {
            std::this_thread::sleep_for(timeout);
            return false;
        }

        const DWORD timeout_ms = timeout.count() > static_cast<long long>(INFINITE - 1)
            ? INFINITE - 1
            : static_cast<DWORD>(timeout.count());
        const DWORD result = WaitForMultipleObjects(
            static_cast<DWORD>(handles_.size()),
            handles_.data(),
            FALSE,
            timeout_ms);

        if (result >= WAIT_OBJECT_0 && result < WAIT_OBJECT_0 + handles_.size()) {
            const std::size_t index = static_cast<std::size_t>(result - WAIT_OBJECT_0);
            FindNextChangeNotification(handles_[index]);
            return true;
        }

        if (result == WAIT_FAILED) {
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
            return true;
        }

        return false;
    }

private:
    std::vector<HANDLE> handles_;
};

void send_all(SOCKET socket_handle, const std::string& data) {
    std::size_t sent = 0;
    while (sent < data.size()) {
        const int count = send(socket_handle, data.data() + sent, static_cast<int>(data.size() - sent), 0);
        if (count == SOCKET_ERROR) {
            throw std::runtime_error("send failed with error " + std::to_string(WSAGetLastError()));
        }
        sent += static_cast<std::size_t>(count);
    }
}

bool stream_state_sse(const std::filesystem::path& workspace_root, const HttpRequest& request, SOCKET socket_handle) {
    const auto [path, query] = split_target_path_and_query(request.target);
    if (path != "/api/state/stream") {
        return false;
    }

    if (!request_is_authorized(workspace_root, request)) {
        send_all(socket_handle, make_http_message(make_unauthorized_response()));
        return true;
    }

    if (request.method != "GET") {
        send_all(socket_handle, make_http_message(HttpResponse {
            "405 Method Not Allowed",
            "text/plain; charset=utf-8",
            "Only GET is supported for /api/state/stream"
        }));
        return true;
    }

    const std::string mode = get_query_value(query, "mode") == "live" ? "live" : "backtest";
    const auto config_name = get_query_value(query, "config");
    const auto replay = lower_copy(get_query_value(query, "replay"));
    const bool enable_backtest_replay = replay == "1" || replay == "true" || replay == "yes" || replay == "full" || replay == "summary";
    const auto detail_level = replay == "summary" ? std::string {"summary"} : get_query_value(query, "detail");

    std::ostringstream headers;
    headers << "HTTP/1.1 200 OK\r\n"
            << "Content-Type: text/event-stream; charset=utf-8\r\n"
            << "Cache-Control: no-store\r\n"
            << "Connection: keep-alive\r\n"
            << "Access-Control-Allow-Origin: *\r\n"
            << "X-Accel-Buffering: no\r\n\r\n";
    send_all(socket_handle, headers.str());
    send_all(socket_handle, ": itrader-ui-api state stream\n\n");

    DirectoryChangeWaiter change_waiter(collect_state_stream_watch_directories(workspace_root, mode, config_name));
    std::string previous_marker;
    auto last_heartbeat_at = std::chrono::steady_clock::now();

    while (true) {
        const auto current_marker = compute_state_stream_change_marker(workspace_root, mode, config_name);
        if (current_marker != previous_marker) {
            std::string state_payload;
            try {
                state_payload = make_ui_state_json(workspace_root, mode, config_name, enable_backtest_replay, nullptr, detail_level);
            } catch (const std::exception& ex) {
                std::ostringstream error_json;
                error_json << '{'
                           << "\"ok\":false,"
                           << "\"message\":" << quoted(ex.what())
                           << '}';
                state_payload = error_json.str();
            }

            send_all(socket_handle, make_sse_event_message("state", state_payload));
            previous_marker = current_marker;
            last_heartbeat_at = std::chrono::steady_clock::now();
        } else {
            const auto now = std::chrono::steady_clock::now();
            if (now - last_heartbeat_at >= std::chrono::seconds(15)) {
                send_all(socket_handle, ": keep-alive\n\n");
                last_heartbeat_at = now;
            }
        }

        const auto now = std::chrono::steady_clock::now();
        const auto heartbeat_due_in = std::chrono::seconds(15) - (now - last_heartbeat_at);
        if (heartbeat_due_in > std::chrono::steady_clock::duration::zero()) {
            change_waiter.wait_for_change(std::chrono::duration_cast<std::chrono::milliseconds>(heartbeat_due_in));
        }
    }
}

std::string receive_request(SOCKET socket_handle) {
    std::string request;
    std::array<char, 4096> buffer {};
    std::optional<std::size_t> header_end;
    std::size_t content_length = 0;

    while (true) {
        const int count = recv(socket_handle, buffer.data(), static_cast<int>(buffer.size()), 0);
        if (count == SOCKET_ERROR) {
            throw std::runtime_error("recv failed with error " + std::to_string(WSAGetLastError()));
        }
        if (count <= 0) {
            break;
        }

        request.append(buffer.data(), static_cast<std::size_t>(count));

        if (!header_end.has_value()) {
            const auto marker = request.find("\r\n\r\n");
            if (marker != std::string::npos) {
                header_end = marker + 4;
                content_length = parse_content_length(std::string_view(request.data(), marker));
            }
        }

        if (header_end.has_value() && request.size() >= *header_end + content_length) {
            break;
        }
    }

    return request;
}

HttpRequest parse_http_request(const std::string& raw_request) {
    const auto marker = raw_request.find("\r\n\r\n");
    const auto header_text = marker == std::string::npos ? raw_request : raw_request.substr(0, marker);
    const auto body_offset = marker == std::string::npos ? raw_request.size() : marker + 4;
    const auto content_length = parse_content_length(header_text);

    std::istringstream input(header_text);
    HttpRequest request;

    std::string request_line;
    if (std::getline(input, request_line)) {
        if (!request_line.empty() && request_line.back() == '\r') {
            request_line.pop_back();
        }

        std::istringstream request_line_stream(request_line);
        request_line_stream >> request.method >> request.target >> request.version;
    }

    std::string header_line;
    while (std::getline(input, header_line)) {
        if (!header_line.empty() && header_line.back() == '\r') {
            header_line.pop_back();
        }
        if (header_line.empty()) {
            continue;
        }

        const auto colon = header_line.find(':');
        if (colon == std::string::npos) {
            continue;
        }

        request.headers[lower_copy(trim_copy(header_line.substr(0, colon)))] = trim_copy(header_line.substr(colon + 1));
    }

    if (body_offset < raw_request.size()) {
        request.body = raw_request.substr(body_offset, std::min(content_length, raw_request.size() - body_offset));
    }

    return request;
}

ServerOptions parse_arguments(int argc, char** argv) {
    ServerOptions options;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--host" && index + 1 < argc) {
            options.host = argv[++index];
            continue;
        }
        if (argument == "--port" && index + 1 < argc) {
            options.port = static_cast<unsigned short>(std::stoi(argv[++index]));
            continue;
        }
        if (argument == "--root" && index + 1 < argc) {
            options.root_override = argv[++index];
            continue;
        }
    }
    return options;
}

} // namespace

int main(int argc, char** argv) {
    try {
        const auto options = parse_arguments(argc, argv);
        const auto workspace_root = discover_workspace_root(options);

        WSADATA wsa_data {};
        if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
            throw std::runtime_error("WSAStartup failed");
        }

        const auto cleanup_wsa = [] { WSACleanup(); };

        addrinfo hints {};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;
        hints.ai_flags = AI_PASSIVE;

        addrinfo* address_info = nullptr;
        const auto port_text = std::to_string(options.port);
        if (getaddrinfo(options.host.c_str(), port_text.c_str(), &hints, &address_info) != 0) {
            cleanup_wsa();
            throw std::runtime_error("getaddrinfo failed");
        }

        SOCKET listen_socket = socket(address_info->ai_family, address_info->ai_socktype, address_info->ai_protocol);
        if (listen_socket == INVALID_SOCKET) {
            freeaddrinfo(address_info);
            cleanup_wsa();
            throw std::runtime_error("socket creation failed");
        }

        const BOOL exclusive_address = TRUE;
        if (setsockopt(
                listen_socket,
                SOL_SOCKET,
                SO_EXCLUSIVEADDRUSE,
                reinterpret_cast<const char*>(&exclusive_address),
                sizeof(exclusive_address)) == SOCKET_ERROR) {
            const auto error = WSAGetLastError();
            closesocket(listen_socket);
            freeaddrinfo(address_info);
            cleanup_wsa();
            throw std::runtime_error("setsockopt(SO_EXCLUSIVEADDRUSE) failed with error " + std::to_string(error));
        }

        if (bind(listen_socket, address_info->ai_addr, static_cast<int>(address_info->ai_addrlen)) == SOCKET_ERROR) {
            const auto error = WSAGetLastError();
            closesocket(listen_socket);
            freeaddrinfo(address_info);
            cleanup_wsa();
            throw std::runtime_error("bind failed with error " + std::to_string(error));
        }

        freeaddrinfo(address_info);

        if (listen(listen_socket, SOMAXCONN) == SOCKET_ERROR) {
            const auto error = WSAGetLastError();
            closesocket(listen_socket);
            cleanup_wsa();
            throw std::runtime_error("listen failed with error " + std::to_string(error));
        }

        std::cout << "iTrader UI API listening on http://" << options.host << ':' << options.port << '\n';
        std::cout << "Workspace root: " << workspace_root.string() << '\n';
        std::cout << "Open http://" << options.host << ':' << options.port << "/ in a browser.\n";

        while (true) {
            SOCKET client_socket = accept(listen_socket, nullptr, nullptr);
            if (client_socket == INVALID_SOCKET) {
                std::cerr << "accept failed: " << WSAGetLastError() << '\n';
                continue;
            }

            std::thread([workspace_root, client_socket]() {
                try {
                    const auto request = receive_request(client_socket);
                    if (!request.empty()) {
                        const auto parsed_request = parse_http_request(request);
                        if (!stream_state_sse(workspace_root, parsed_request, client_socket)) {
                            const auto response = route_request(workspace_root, parsed_request);
                            send_all(client_socket, make_http_message(response));
                        }
                    }
                } catch (const std::exception& ex) {
                    const auto response = make_http_message(HttpResponse {"500 Internal Server Error", "text/plain; charset=utf-8", ex.what()});
                    try {
                        send_all(client_socket, response);
                    } catch (...) {
                    }
                    std::cerr << "request handling error: " << ex.what() << '\n';
                }

                closesocket(client_socket);
            }).detach();
        }
    } catch (const std::exception& ex) {
        std::cerr << "Fatal UI API server error: " << ex.what() << '\n';
        return 1;
    }
}
