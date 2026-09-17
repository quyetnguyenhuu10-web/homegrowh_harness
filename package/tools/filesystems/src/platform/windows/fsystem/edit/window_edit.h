#pragma once
#include "fsystem/edit/edit.h"

namespace fsystem::windows
{
    EditResult edit_file(
        const std::filesystem::path& path,
        const std::string& old_data,
        const std::string& new_data
    );
}
