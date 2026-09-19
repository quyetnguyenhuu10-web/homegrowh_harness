#pragma once

#include "fsystem/edit/edit.h"

namespace fsystem::linux
{
    EditResults edit_file(const EditRequests& requests);

    EditResult edit_file(
        const std::filesystem::path& path,
        const std::string& old_data,
        const std::string& new_data
    );
}
