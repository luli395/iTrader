#pragma once

#include "itrader/domain.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace itrader {

class IniFile {
public:
    using Section = std::map<std::string, std::string>;

    static IniFile parse(const std::filesystem::path& file_path) {
        std::ifstream input(file_path);
        if (!input.is_open()) {
            throw std::runtime_error("Unable to open config file: " + file_path.string());
        }

        IniFile ini;
        std::string current_section;
        std::string line;

        while (std::getline(input, line)) {
            const auto trimmed = trim_copy(line);
            if (trimmed.empty() || trimmed.starts_with('#') || trimmed.starts_with(';')) {
                continue;
            }

            if (trimmed.front() == '[' && trimmed.back() == ']') {
                current_section = trim_copy(trimmed.substr(1, trimmed.size() - 2));
                ini.sections_[current_section];
                continue;
            }

            const auto delimiter = trimmed.find('=');
            if (delimiter == std::string::npos) {
                continue;
            }

            const auto key = trim_copy(trimmed.substr(0, delimiter));
            const auto value = trim_copy(trimmed.substr(delimiter + 1));
            ini.sections_[current_section][key] = value;
        }

        ini.env_overrides_ = load_env_file(file_path.parent_path().parent_path() / ".env");

        return ini;
    }

    [[nodiscard]] bool has_section(const std::string& name) const {
        return sections_.contains(name);
    }

    [[nodiscard]] std::string get(const std::string& section, const std::string& key, const std::string& fallback = {}) const {
        const auto section_it = sections_.find(section);
        if (section_it == sections_.end()) {
            return resolve_env_placeholders(fallback);
        }

        const auto value_it = section_it->second.find(key);
        if (value_it == section_it->second.end()) {
            return resolve_env_placeholders(fallback);
        }

        return resolve_env_placeholders(value_it->second);
    }

    [[nodiscard]] int get_int(const std::string& section, const std::string& key, int fallback = 0) const {
        const auto value = get(section, key);
        return value.empty() ? fallback : std::stoi(value);
    }

    [[nodiscard]] double get_double(const std::string& section, const std::string& key, double fallback = 0.0) const {
        const auto value = get(section, key);
        return value.empty() ? fallback : std::stod(value);
    }

    [[nodiscard]] bool get_bool(const std::string& section, const std::string& key, bool fallback = false) const {
        auto value = get(section, key);
        if (value.empty()) {
            return fallback;
        }

        for (char& ch : value) {
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        }

        return value == "1" || value == "true" || value == "yes" || value == "on";
    }

    [[nodiscard]] std::vector<std::string> get_list(const std::string& section, const std::string& key) const {
        return split_csv(get(section, key));
    }

    [[nodiscard]] std::vector<std::string> sections_with_prefix(const std::string& prefix) const {
        std::vector<std::string> matches;
        for (const auto& [name, _] : sections_) {
            if (name.rfind(prefix, 0) == 0) {
                matches.push_back(name);
            }
        }
        return matches;
    }

    [[nodiscard]] Section section(const std::string& name) const {
        const auto it = sections_.find(name);
        if (it == sections_.end()) {
            return {};
        }
        return it->second;
    }

private:
    [[nodiscard]] static std::string lookup_process_env_value(const std::string& key) {
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

    static std::map<std::string, std::string> load_env_file(const std::filesystem::path& file_path) {
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

    [[nodiscard]] std::string lookup_env_value(const std::string& key) const {
        if (const auto process_value = lookup_process_env_value(key); !process_value.empty()) {
            return process_value;
        }

        const auto env_it = env_overrides_.find(key);
        return env_it == env_overrides_.end() ? std::string {} : env_it->second;
    }

    [[nodiscard]] std::string resolve_env_placeholders(std::string_view raw) const {
        std::string resolved;
        resolved.reserve(raw.size());

        std::size_t cursor = 0;
        while (cursor < raw.size()) {
            const auto marker = raw.find("${", cursor);
            if (marker == std::string_view::npos) {
                resolved.append(raw.substr(cursor));
                break;
            }

            resolved.append(raw.substr(cursor, marker - cursor));
            const auto terminator = raw.find('}', marker + 2);
            if (terminator == std::string_view::npos) {
                resolved.append(raw.substr(marker));
                break;
            }

            const auto expression = raw.substr(marker + 2, terminator - (marker + 2));
            const auto fallback_delimiter = expression.find(":-");
            const auto key = trim_copy(expression.substr(0, fallback_delimiter));
            const auto fallback = fallback_delimiter == std::string_view::npos
                ? std::string {}
                : std::string(expression.substr(fallback_delimiter + 2));

            std::string value;
            if (!key.empty()) {
                value = lookup_env_value(key);
            }
            if (value.empty()) {
                value = fallback;
            }

            resolved += value;
            cursor = terminator + 1;
        }

        return resolved;
    }

    std::map<std::string, Section> sections_;
    std::map<std::string, std::string> env_overrides_;
};

} // namespace itrader
