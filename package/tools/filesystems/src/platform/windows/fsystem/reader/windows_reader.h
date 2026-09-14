#pragma once

#include "fsystem/read/reader.h"

namespace fsystem::windows
{
    ReadResult read_file(std::filesystem::path path);
}
