#pragma once

#include <filesystem>
#include <string>

namespace itrader {

int run_application(
    const std::string& mode_text,
    const std::filesystem::path& config_path,
    const std::filesystem::path& backtest_output_dir = {});

} // namespace itrader
