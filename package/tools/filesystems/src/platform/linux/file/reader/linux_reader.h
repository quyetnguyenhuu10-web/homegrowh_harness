#pragma once

#include "file/reader.h"

namespace fsystem::linux
{
    ReadResult read_file(std::filesystem::path path);
}
