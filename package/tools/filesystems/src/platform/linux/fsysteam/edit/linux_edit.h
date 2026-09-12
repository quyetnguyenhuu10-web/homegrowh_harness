#pragma once

#include "fsysteam/edit/edit.h"

namespace fsystem::linux
{
    EditResult edit_file(
        std::filesystem::path path,
        std::string old_data,
        std::string new_data
    );
}
