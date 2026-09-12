#pragma once

#include "file/reader.h"

namespace fsystem::windows
{
    ReadResult read_file(std::filesystem::path path);
}
