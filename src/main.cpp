#include "itrader/platform.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include <windows.h>
#include <shellapi.h>

namespace {

struct LauncherOptions {
    bool launch_ui {false};
    bool has_ui_specific_option {false};
    std::string mode;
    std::string config_path;
    std::filesystem::path backtest_output_dir;
    std::string host {"127.0.0.1"};
    unsigned short port {8080};
    std::optional<std::filesystem::path> root_override;
};

std::string normalize_mode_argument(std::string mode) {
    std::transform(
        mode.begin(),
        mode.end(),
        mode.begin(),
        [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
    return mode;
}

std::string default_config_path_for_mode(const std::string& mode) {
    return normalize_mode_argument(mode) == "live"
        ? "configs/live.ini"
        : "configs/backtest.ini";
}

std::wstring executable_path() {
    std::array<wchar_t, 4096> buffer {};
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length == buffer.size()) {
        throw std::runtime_error("Unable to determine executable path");
    }
    return std::wstring(buffer.data(), length);
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

std::wstring widen(std::string_view value) {
    return std::wstring(value.begin(), value.end());
}

bool looks_like_workspace_root(const std::filesystem::path& candidate) {
    return std::filesystem::exists(candidate / "configs" / "backtest.ini")
        && std::filesystem::exists(candidate / "configs" / "live.ini")
        && std::filesystem::exists(candidate / "ui" / "index.html");
}

std::filesystem::path discover_workspace_root(const LauncherOptions& options) {
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

std::filesystem::path discover_ui_api_binary_path(const std::filesystem::path& workspace_root) {
    const auto current_executable_directory = std::filesystem::path(executable_path()).parent_path();
    std::vector<std::filesystem::path> candidates {
        current_executable_directory / "itrader_ui_api.exe"
    };

    if (!current_executable_directory.filename().empty()) {
        candidates.push_back(workspace_root / "build" / current_executable_directory.filename() / "itrader_ui_api.exe");
    }
    candidates.push_back(workspace_root / "build" / "Release" / "itrader_ui_api.exe");
    candidates.push_back(workspace_root / "build" / "Debug" / "itrader_ui_api.exe");

    for (const auto& candidate : candidates) {
        std::error_code error_code;
        if (std::filesystem::exists(candidate, error_code) && std::filesystem::is_regular_file(candidate, error_code)) {
            return std::filesystem::weakly_canonical(candidate);
        }
    }

    throw std::runtime_error("Unable to locate itrader_ui_api.exe next to itrader.exe or under build/<Config>.");
}

LauncherOptions parse_arguments(int argc, char** argv) {
    LauncherOptions options;

    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--ui") {
            options.launch_ui = true;
            continue;
        }
        if (argument == "--mode" && index + 1 < argc) {
            options.mode = argv[++index];
            continue;
        }
        if (argument == "--config" && index + 1 < argc) {
            options.config_path = argv[++index];
            continue;
        }
        if ((argument == "--output-dir" || argument == "--backtest-output-dir") && index + 1 < argc) {
            options.backtest_output_dir = argv[++index];
            continue;
        }
        if (argument == "--host" && index + 1 < argc) {
            options.host = argv[++index];
            options.has_ui_specific_option = true;
            continue;
        }
        if (argument == "--port" && index + 1 < argc) {
            options.port = static_cast<unsigned short>(std::stoi(argv[++index]));
            options.has_ui_specific_option = true;
            continue;
        }
        if (argument == "--root" && index + 1 < argc) {
            options.root_override = argv[++index];
            options.has_ui_specific_option = true;
            continue;
        }
    }

    return options;
}

bool should_launch_control_center(const LauncherOptions& options, int argc) {
    return argc == 1
        || options.launch_ui
        || (options.mode.empty() && options.config_path.empty() && options.has_ui_specific_option);
}

int launch_control_center(const LauncherOptions& options) {
    const auto workspace_root = discover_workspace_root(options);
    const auto ui_binary_path = discover_ui_api_binary_path(workspace_root);
    const auto ui_binary_directory = ui_binary_path.parent_path();

    const std::wstring host = widen(options.host);
    const std::wstring port = std::to_wstring(options.port);
    const std::wstring url = L"http://" + host + L":" + port + L"/";
    const std::wstring command_line = quote_windows_command_argument(ui_binary_path.wstring())
        + L" --host " + quote_windows_command_argument(host)
        + L" --port " + quote_windows_command_argument(port)
        + L" --root " + quote_windows_command_argument(workspace_root.wstring());

    STARTUPINFOW startup_info {};
    startup_info.cb = sizeof(startup_info);
    PROCESS_INFORMATION process_info {};

    std::vector<wchar_t> mutable_command_line(command_line.begin(), command_line.end());
    mutable_command_line.push_back(L'\0');
    const auto working_directory = ui_binary_directory.wstring();

    if (!CreateProcessW(
            ui_binary_path.wstring().c_str(),
            mutable_command_line.data(),
            nullptr,
            nullptr,
            FALSE,
            DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP,
            nullptr,
            working_directory.c_str(),
            &startup_info,
            &process_info)) {
        throw std::runtime_error("Unable to launch itrader_ui_api.exe (Windows error " + std::to_string(GetLastError()) + ")");
    }

    CloseHandle(process_info.hThread);
    CloseHandle(process_info.hProcess);

    std::cout << "Launching iTrader Control Center via " << ui_binary_path.string() << "\n";
    std::cout << "Workspace root: " << workspace_root.string() << "\n";
    std::cout << "Control Center URL: " << options.host << ':' << options.port << "/\n";

    const auto open_result = reinterpret_cast<std::intptr_t>(ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
    if (open_result <= 32) {
        std::cout << "Browser auto-open failed; open the URL above manually.\n";
    }

    return 0;
}

void print_usage() {
    std::cerr << "Usage: itrader [--ui] [--host <host>] [--port <port>] [--root <workspace>]\n"
              << "   or: itrader [--mode <backtest|live>] [--config <path-to-ini>] [--output-dir <dir>]\n"
              << "Defaults: no arguments -> launch Control Center UI; --mode backtest without --config -> configs/backtest.ini; --mode live without --config -> configs/live.ini\n";
}

} // namespace

int main(int argc, char** argv) {
    try {
        const auto options = parse_arguments(argc, argv);

        if (should_launch_control_center(options, argc)) {
            return launch_control_center(options);
        }

        std::string config_path = options.config_path;
        if (config_path.empty()) {
            if (options.mode.empty()) {
                print_usage();
                return 1;
            }

            config_path = default_config_path_for_mode(options.mode);
            std::cout << "No --config supplied; defaulting to " << config_path
                      << " for " << normalize_mode_argument(options.mode) << " mode\n";
        }

        return itrader::run_application(options.mode, config_path, options.backtest_output_dir);
    } catch (const std::exception& ex) {
        std::cerr << "Fatal error: " << ex.what() << '\n';
        return 1;
    }
}
