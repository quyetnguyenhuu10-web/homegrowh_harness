#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#include <tool_runtime/tool_runtime.h>

namespace tool_runtime::detail
{
    std::string path_text(const std::filesystem::path& path);
    Result<std::filesystem::path::string_type> environment_text(std::string_view name);
    Result<std::filesystem::path> current_executable();
    Result<std::filesystem::path> current_workspace();
    Result<nlohmann::json> read_json_file(
        const std::filesystem::path& path,
        std::string_view operation);
}
