#pragma once

#include <filesystem>
#include <string>
#include <cstdint>

namespace file
{
    enum class EditNote
    {
        none,
        old_data_not_found,
        old_data_appears_more_than_once,
    };

    struct EditResult
    {
        std::string new_content;
        std::string old_content;
        std::uint32_t error;
        EditNote note{EditNote::none};
    };

    EditResult edit(std::filesystem::path path, std::string old_data, std::string new_data);
}
