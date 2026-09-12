#pragma once

#include "file/edit.h"

namespace file::linux
{
    EditResult edit_file(
        std::filesystem::path path,
        std::string old_data,
        std::string new_data
    );
}
