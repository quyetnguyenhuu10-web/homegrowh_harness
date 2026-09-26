#include "execute.h"

#include "../runtime/runtime.h"

#include <nlohmann/json.hpp>

namespace tool_runtime
{
    nlohmann::json execute(
        const nlohmann::json& tool_call,
        const std::filesystem::path& workspace_path,
        bool refresh,
        std::uint32_t timeout_ms)
    {
        return detail::execute_tool(
            tool_call,
            workspace_path,
            refresh,
            timeout_ms);
    }
}
