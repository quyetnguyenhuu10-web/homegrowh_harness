#pragma once

#include "file/reader.h"

namespace file::linux
{
    ReadResult read_file(std::filesystem::path path);
}
