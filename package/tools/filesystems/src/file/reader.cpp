#include "file/reader.h"

#if defined(_WIN32)

#include "../platform/windows/file/reader/windows_reader.h"

namespace file
{
    ReadResult read(std::filesystem::path path)
    {
        return windows::read_file(path);
    }
}

#elif defined(__linux__)

#include "../platform/linux/file/reader/linux_reader.h"

namespace file
{
    ReadResult read(std::filesystem::path path)
    {
        return linux::read_file(path);
    }
}

#else

#error "Unsupported operating system"

#endif
