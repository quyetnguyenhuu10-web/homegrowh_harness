#pragma once

#include "fsysteam/read/reader.h"

namespace fsystem::linux
{
    ReadResult read_file(std::filesystem::path path);
}
