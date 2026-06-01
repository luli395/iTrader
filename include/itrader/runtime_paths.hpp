#pragma once

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <string_view>

namespace itrader {

inline std::string sanitize_runtime_namespace(std::string_view raw) {
    std::string value;
    value.reserve(raw.size());
    bool previous_was_separator = false;
    for (const char ch : raw) {
        if (std::isalnum(static_cast<unsigned char>(ch)) != 0) {
            value.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
            previous_was_separator = false;
            continue;
        }

        if (!previous_was_separator) {
            value.push_back('_');
            previous_was_separator = true;
        }
    }

    while (!value.empty() && value.front() == '_') {
        value.erase(value.begin());
    }
    while (!value.empty() && value.back() == '_') {
        value.pop_back();
    }

    return value.empty() ? std::string {"live"} : value;
}

inline std::string config_runtime_namespace(const std::filesystem::path& config_path) {
    const auto stem = config_path.stem().generic_string();
    return sanitize_runtime_namespace(stem.empty() ? std::string_view {"live"} : std::string_view {stem});
}

inline std::filesystem::path runtime_namespace_directory(const std::filesystem::path& config_path) {
    return config_path.parent_path().parent_path() / "runtime" / config_runtime_namespace(config_path);
}

inline std::filesystem::path live_telemetry_path(const std::filesystem::path& config_path) {
    return runtime_namespace_directory(config_path) / "live_telemetry.ini";
}

inline std::filesystem::path strategy_inventory_store_path(const std::filesystem::path& config_path) {
    return runtime_namespace_directory(config_path) / "strategy_inventory_store.ini";
}

inline std::filesystem::path strategy_state_store_path(const std::filesystem::path& config_path) {
    return runtime_namespace_directory(config_path) / "strategy_state_store.ini";
}

inline std::filesystem::path strategy_inventory_adjustments_path(const std::filesystem::path& config_path) {
    return runtime_namespace_directory(config_path) / "strategy_inventory_adjustments.ini";
}

inline std::filesystem::path default_ctp_flow_dir(const std::filesystem::path& config_path, std::string_view account_id) {
    return std::filesystem::path("..")
        / "runtime"
        / config_runtime_namespace(config_path)
        / ("ctp_flow_" + sanitize_runtime_namespace(account_id));
}

inline std::filesystem::path default_ctp_md_flow_dir(const std::filesystem::path& config_path, std::string_view account_id) {
    return std::filesystem::path("..")
        / "runtime"
        / config_runtime_namespace(config_path)
        / ("ctp_md_flow_" + sanitize_runtime_namespace(account_id));
}

} // namespace itrader
