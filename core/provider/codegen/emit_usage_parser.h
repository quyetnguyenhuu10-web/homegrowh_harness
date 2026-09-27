#pragma once

#include <nlohmann/json.hpp>

#include <iosfwd>

void emit_usage_parser(
    std::ostream& output,
    const nlohmann::ordered_json& schema);
