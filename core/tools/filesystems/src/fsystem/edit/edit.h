#pragma once

#include <filesystem>
#include <string>
#include <cstdint>
#include <vector>

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
        /* Edit timeout won before the final commit completed. */
        timeout,
    };

    struct EditRequest
    {
        std::filesystem::path path;
        std::string old_content;
        std::string new_content;
    };

    using EditRequests = std::vector<EditRequest>;

    struct EditResult
    {
        std::filesystem::path path;
        std::uint32_t error = 0;
        EditNote note{EditNote::none};

        /* True when the platform edit reached its final replace operation. */
        bool replace_attempted = false;
    };

    using EditResults = std::vector<EditResult>;

    EditResults edit(const EditRequests& requests);

    EditResult edit(
        const std::filesystem::path& path,
        const std::string& old_data,
        const std::string& new_data
    );
}
