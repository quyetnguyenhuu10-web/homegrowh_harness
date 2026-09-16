#pragma once

#include <filesystem>
#include <string>
#include <cstdint>

namespace fsystem
{
    enum class EditNote
    {
        none,
        old_data_not_found,
        old_data_appears_more_than_once,
        file_changed,
        /* Multiple old-data occurrences share bytes. */
        old_data_occurrences_overlap,
    };

    struct EditResult
    {
        std::string new_content;
        std::string old_content;
        std::uint32_t error;
        EditNote note{EditNote::none};

        /* True when the platform edit reached its final replace operation. */
        bool replace_attempted = false;
    };

    EditResult edit(std::filesystem::path path, std::string old_data, std::string new_data);
}
