#pragma once

#include "fsysteam/read/reader.h"

namespace fsystem::windows
{
    ReadResult read_file(std::filesystem::path path);
}
