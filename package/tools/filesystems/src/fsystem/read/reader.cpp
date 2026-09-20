#include "fsystem/read/reader.h"

#if defined(_WIN32)

#include "../../platform/windows/fsystem/reader/windows_reader.h"

namespace fsystem
{
    ReadResults read(const ReadRequests& requests)
    {
        return windows::read_file(requests);
    }

    ReadResult read(
        const std::filesystem::path& path,
        std::uint64_t start_line,
        std::uint64_t end_line
    )
    {
        return windows::read_file(path, start_line, end_line);
    }

    ReadResult read(const std::filesystem::path& path)
    {
        return windows::read_file(path);
    }
}

#elif defined(__linux__)

#include "../../platform/linux/fsystem/reader/linux_reader.h"

namespace fsystem
{
    ReadResults read(const ReadRequests& requests)
    {
        return linux::read_file(requests);
    }

    ReadResult read(
        const std::filesystem::path& path,
        std::uint64_t start_line,
        std::uint64_t end_line
    )
    {
        return linux::read_file(path, start_line, end_line);
    }

    ReadResult read(const std::filesystem::path& path)
    {
        return linux::read_file(path);
    }
}

#else

#error "Unsupported operating system"

#endif
