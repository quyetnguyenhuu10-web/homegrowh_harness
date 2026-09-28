#pragma once

#include <cstddef>
#include <map>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

namespace sessions::detail
{
    class ToolStreamParser final
    {
    public:
        void append(std::string_view event) noexcept;
        nlohmann::json finish();

    private:
        void append_impl(std::string_view event) noexcept;

        std::map<std::size_t, nlohmann::json> tool_calls_;
        std::vector<nlohmann::json> unindexed_calls_;
    };
}
