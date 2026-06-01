#!/usr/bin/env python3
"""Generate a new iTrader C++ strategy template.

Examples:
    python scripts/generate_strategy_template.py --name my_mean_reversion
    python scripts/generate_strategy_template.py --name trend_alpha --param quantity:int=1 --param threshold:double=1.5
    python scripts/generate_strategy_template.py --name breakout_v2 --add-cmake
"""

from __future__ import annotations

import argparse
import re
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_OUTPUT_DIR = REPO_ROOT / "strategies"
DEFAULT_CMAKE_PATH = REPO_ROOT / "CMakeLists.txt"


TYPE_INFO = {
    "int": {
        "cpp_type": "int",
        "storage": "int",
        "default": "0",
        "include": "",
        "read_expr": "std::stoi(value)",
        "normalize": "",
    },
    "double": {
        "cpp_type": "double",
        "storage": "double",
        "default": "0.0",
        "include": "",
        "read_expr": "std::stod(value)",
        "normalize": "",
    },
    "bool": {
        "cpp_type": "bool",
        "storage": "bool",
        "default": "false",
        "include": "",
        "read_expr": "",
        "normalize": "const auto normalized = itrader::trim_copy(value); target = normalized == \"1\" || normalized == \"true\" || normalized == \"yes\" || normalized == \"on\";",
    },
    "string": {
        "cpp_type": "std::string",
        "storage": "std::string",
        "default": '""',
        "include": "",
        "read_expr": "value",
        "normalize": "",
    },
}


@dataclass(frozen=True)
class ParamSpec:
    name: str
    kind: str
    default_cpp: str

    @property
    def member_name(self) -> str:
        return f"{self.name}_"

    @property
    def cpp_type(self) -> str:
        return TYPE_INFO[self.kind]["cpp_type"]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Generate an iTrader C++ strategy template.")
    parser.add_argument("--name", required=True, help="Strategy target/output name, usually snake_case.")
    parser.add_argument("--class-name", help="C++ class name. Defaults to PascalCase(name) + Strategy.")
    parser.add_argument("--output", help="Output cpp path. Defaults to strategies/<name>.cpp")
    parser.add_argument(
        "--param",
        action="append",
        default=[],
        help="Parameter spec in the form name:type=default. Supported types: int,double,bool,string. Can be repeated.",
    )
    parser.add_argument("--add-cmake", action="store_true", help="Append a DLL target block to CMakeLists.txt if missing.")
    parser.add_argument("--force", action="store_true", help="Overwrite an existing output file.")
    return parser.parse_args()


def to_pascal_case(raw: str) -> str:
    parts = re.split(r"[^A-Za-z0-9]+", raw)
    return "".join(part[:1].upper() + part[1:] for part in parts if part)


def sanitize_identifier(name: str) -> str:
    sanitized = re.sub(r"[^A-Za-z0-9_]", "_", name)
    if not sanitized:
        raise ValueError("Identifier became empty after sanitization.")
    if sanitized[0].isdigit():
        sanitized = f"p_{sanitized}"
    return sanitized


def parse_param_spec(raw: str) -> ParamSpec:
    match = re.fullmatch(r"([A-Za-z_][A-Za-z0-9_]*)\s*:\s*(int|double|bool|string)\s*=\s*(.+)", raw.strip())
    if match is None:
        raise ValueError(f"Invalid --param value: {raw!r}")

    name = sanitize_identifier(match.group(1))
    kind = match.group(2)
    default_text = match.group(3).strip()

    if kind == "int":
        int(default_text)
        default_cpp = default_text
    elif kind == "double":
        float(default_text)
        default_cpp = default_text if any(ch in default_text for ch in ".eE") else f"{default_text}.0"
    elif kind == "bool":
        normalized = default_text.lower()
        if normalized not in {"true", "false", "1", "0", "yes", "no", "on", "off"}:
            raise ValueError(f"Unsupported bool default: {default_text!r}")
        default_cpp = "true" if normalized in {"true", "1", "yes", "on"} else "false"
    else:
        escaped = default_text.replace("\\", "\\\\").replace('"', '\\"')
        default_cpp = f'"{escaped}"'

    return ParamSpec(name=name, kind=kind, default_cpp=default_cpp)


def render_param_reader(param: ParamSpec) -> str:
    info = TYPE_INFO[param.kind]
    if param.kind == "bool":
        body = info["normalize"]
    else:
        body = f"target = {info['read_expr']};".replace("value", "it->second")

    return f"""    static void read_{param.kind}(const std::unordered_map<std::string, std::string>& parameters,
                            const std::string& key,
                            {param.cpp_type}& target) {{
        const auto it = parameters.find(key);
        if (it == parameters.end()) {{
            return;
        }}
        {body}
    }}"""


def render_on_init_reads(params: Iterable[ParamSpec]) -> str:
    lines = []
    for param in params:
        lines.append(f'        read_{param.kind}(parameters, "{param.name}", {param.member_name});')
    return "\n".join(lines)


def render_init_log_parts(params: Iterable[ParamSpec]) -> str:
    lines = []
    for param in params:
        lines.append(f'            ", {param.name}=" + {render_to_string(param)}')
    return "\n".join(lines)


def render_to_string(param: ParamSpec) -> str:
    if param.kind == "string":
        return param.member_name
    if param.kind == "bool":
        return f"std::string({param.member_name} ? \"true\" : \"false\")"
    return f"std::to_string({param.member_name})"


def render_members(params: Iterable[ParamSpec]) -> str:
    return "\n".join(
        f"    {param.cpp_type} {param.member_name} {{{param.default_cpp}}};" for param in params
    )


def render_template(strategy_name: str, class_name: str, params: list[ParamSpec]) -> str:
    seen_kinds: list[str] = []
    for param in params:
        if param.kind not in seen_kinds:
            seen_kinds.append(param.kind)
    read_helpers = "\n\n".join(render_param_reader(ParamSpec(name=f"_{kind}", kind=kind, default_cpp="")) for kind in seen_kinds)
    init_reads = render_on_init_reads(params)
    init_log_parts = render_init_log_parts(params)
    members = render_members(params)

    if not read_helpers:
        read_helpers = "    // Add parameter readers here when you extend the template."
    if not init_reads:
        init_reads = "        (void)parameters;"
    if not init_log_parts:
        init_log_parts = ""
    if not members:
        members = "    // Add strategy parameters and local state here."

    return f"""#include "itrader/strategy_api.hpp"

#include <cstdlib>
#include <optional>
#include <string>
#include <unordered_map>

namespace {{

struct RuntimePositionView {{
    bool is_open {{false}};
    int direction {{0}};
    int quantity {{0}};
    double average_price {{0.0}};
}};

class {class_name} final : public itrader::IStrategy {{
public:
    [[nodiscard]] const char* name() const override {{
        return "{strategy_name}";
    }}

    void on_init(const std::unordered_map<std::string, std::string>& parameters,
                 itrader::IStrategyContext& context) override {{
{init_reads}

        context.log("Initialized {strategy_name}"
{init_log_parts});
    }}

    void on_start(itrader::IStrategyContext& context) override {{
        context.log("{strategy_name} started for account " + context.account_id());
    }}

    void on_tick(const itrader::MarketTick& tick, itrader::IStrategyContext& context) override {{
        auto& state = instrument_states_[tick.instrument];
        state.last_tick = tick;

        // Put bar aggregation, indicators, and signal generation here.
        // Recommended style:
        // 1. read runtime_position from on_position_update()
        // 2. read entry_order / exit_order slots from on_order_update()
        // 3. generate OrderIntent only when you truly want to act
        (void)context;
    }}

    void on_order_update(const itrader::RuntimeOrderSnapshot& order,
                         itrader::IStrategyContext& context) override {{
        auto state_it = instrument_states_.find(order.instrument);
        if (state_it == instrument_states_.end()) {{
            return;
        }}

        auto& state = state_it->second;
        if (itrader::matches_order_update(state.entry_order, order)) {{
            const int fill_delta = itrader::filled_volume_delta(state.entry_order, order);
            if (fill_delta > 0) {{
                context.log("entry order fill delta=" + std::to_string(fill_delta) + " for " + order.instrument);
            }}
            itrader::record_order_update(state.entry_order, order);
            if (is_terminal_status(order.status)) {{
                itrader::clear_order_slot(state.entry_order);
            }}
        }}

        if (itrader::matches_order_update(state.exit_order, order)) {{
            const int fill_delta = itrader::filled_volume_delta(state.exit_order, order);
            if (fill_delta > 0) {{
                context.log("exit order fill delta=" + std::to_string(fill_delta) + " for " + order.instrument);
            }}
            itrader::record_order_update(state.exit_order, order);
            if (is_terminal_status(order.status)) {{
                itrader::clear_order_slot(state.exit_order);
            }}
        }}
    }}

    void on_position_update(const itrader::RuntimePositionSnapshot& position,
                            itrader::IStrategyContext& context) override {{
        auto& state = instrument_states_[position.instrument];
        state.runtime_position = make_runtime_position_view(position);
        (void)context;
    }}

    void on_order_event(const itrader::OrderEvent& event,
                        itrader::IStrategyContext& context) override {{
        if (event.status == itrader::OrderStatus::Rejected) {{
            context.log("Order rejected for " + event.instrument + ": " + event.message);
        }}
    }}

    void on_stop(itrader::IStrategyContext& context) override {{
        context.log("{strategy_name} stopped for account " + context.account_id());
    }}

private:
    struct InstrumentState {{
        RuntimePositionView runtime_position;
        itrader::OrderSlotState entry_order;
        itrader::OrderSlotState exit_order;
        std::optional<itrader::MarketTick> last_tick;
    }};

{read_helpers}

    static bool is_terminal_status(itrader::OrderStatus status) {{
        return status == itrader::OrderStatus::Filled
            || status == itrader::OrderStatus::Cancelled
            || status == itrader::OrderStatus::Rejected;
    }}

    static RuntimePositionView make_runtime_position_view(const itrader::RuntimePositionSnapshot& position) {{
        RuntimePositionView view;
        view.quantity = std::abs(position.net);
        view.is_open = view.quantity > 0;
        view.direction = position.net > 0 ? 1 : (position.net < 0 ? -1 : 0);
        view.average_price = position.average_price;
        return view;
    }}

{members}
    std::unordered_map<std::string, InstrumentState> instrument_states_;
}};

}} // namespace

extern "C" ITRADER_STRATEGY_EXPORT itrader::IStrategy* CreateStrategy() {{
    return new {class_name}();
}}

extern "C" ITRADER_STRATEGY_EXPORT void DestroyStrategy(itrader::IStrategy* strategy) {{
    delete strategy;
}}
"""


def render_cmake_block(target_name: str, source_path: Path) -> str:
    relative_source = source_path.relative_to(REPO_ROOT).as_posix()
    return f"""
add_library({target_name} SHARED {relative_source})
target_include_directories({target_name} PRIVATE "${{CMAKE_SOURCE_DIR}}/include")
target_compile_definitions({target_name} PRIVATE ITRADER_BUILD_STRATEGY NOMINMAX WIN32_LEAN_AND_MEAN)
set_target_properties({target_name} PROPERTIES OUTPUT_NAME "{target_name}")
""".lstrip("\n")


def maybe_append_cmake_target(cmake_path: Path, target_name: str, source_path: Path) -> bool:
    content = cmake_path.read_text(encoding="utf-8")
    if re.search(rf"add_library\(\s*{re.escape(target_name)}\s+SHARED\b", content):
        return False

    block = render_cmake_block(target_name, source_path)
    cmake_path.write_text(content.rstrip() + "\n\n" + block, encoding="utf-8")
    return True


def main() -> int:
    args = parse_args()

    strategy_name = sanitize_identifier(args.name)
    class_name = args.class_name or f"{to_pascal_case(strategy_name)}Strategy"
    class_name = sanitize_identifier(class_name)

    params = [parse_param_spec(raw) for raw in args.param]
    output_path = Path(args.output) if args.output else DEFAULT_OUTPUT_DIR / f"{strategy_name}.cpp"

    if output_path.exists() and not args.force:
        raise SystemExit(f"Refusing to overwrite existing file without --force: {output_path}")

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(render_template(strategy_name, class_name, params), encoding="utf-8", newline="\n")

    print(f"Generated strategy template: {output_path}")

    if args.add_cmake:
        updated = maybe_append_cmake_target(DEFAULT_CMAKE_PATH, strategy_name, output_path)
        if updated:
            print(f"Appended CMake target '{strategy_name}' to {DEFAULT_CMAKE_PATH}")
        else:
            print(f"CMake target '{strategy_name}' already exists in {DEFAULT_CMAKE_PATH}")

    if params:
        print("Template parameters:")
        for param in params:
            print(f"  - {param.name}: {param.kind} = {param.default_cpp}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
