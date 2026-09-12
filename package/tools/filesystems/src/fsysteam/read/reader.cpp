#include "fsysteam/read/reader.h"

#if defined(_WIN32)

#include "../../platform/windows/fsysteam/reader/windows_reader.h"

namespace fsystem
{
    ReadResult read(std::filesystem::path path)
    {
        return windows::read_file(path);
    }
}

#elif defined(__linux__)

#include "../../platform/linux/fsysteam/reader/linux_reader.h"

namespace fsystem
{
    ReadResult read(std::filesystem::path path)
    {
        return linux::read_file(path);
    }
}

#else

#error "Unsupported operating system"

#endif
