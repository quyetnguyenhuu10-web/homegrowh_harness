#include "file/reader.h"

#if defined(_WIN32)

#include "../platform/windows/windows_reader.h"

namespace file
{
    ReadResult read(
        std::filesystem::path path,
        std::uint32_t MAX_BYTES_READ
    )
    {
        return windows::read_file(path, MAX_BYTES_READ);
    }
}

#elif defined(__linux__)

#include "../platform/linux/linux_reader.h"

namespace file
{
    ReadResult read(
        std::filesystem::path path,
        std::uint32_t MAX_BYTES_READ
    )
    {
        return linux::read_file(path, MAX_BYTES_READ);
    }
}

#else

#error "Unsupported operating system"

#endif
