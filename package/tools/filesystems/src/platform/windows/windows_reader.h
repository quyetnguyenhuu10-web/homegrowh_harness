#pragma once

#include "file/reader.h"

namespace file::windows
{
    ReadResult read_file(std::filesystem::path path);
}
