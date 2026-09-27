#include "execute.h"

#include "../runtime/runtime.h"

#include <nlohmann/json.hpp>

namespace tool_runtime
{
    nlohmann::json execute(
        const nlohmann::json& tool_call,
        std::vector<std::string>& read_files,
        std::uint32_t timeout_ms)
    {
        return detail::execute_tool(
            tool_call,
            read_files,
            timeout_ms);
    }
}
