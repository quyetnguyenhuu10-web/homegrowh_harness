#include "fsystem/write/writer.h"

#if defined(_WIN32)

#include "../../platform/windows/fsystem/write/windows_writer.h"

namespace fsystem
{
    WriteResults write(const WriteRequests& requests)
    {
        return windows::write_file(requests);
    }

    WriteResult write(
        const std::filesystem::path& path,
        const std::string& new_content
    )
    {
        return windows::write_file(path, new_content);
    }
}

#elif defined(__linux__)

#include "../../platform/linux/fsystem/write/linux_writer.h"

namespace fsystem
{
    WriteResults write(const WriteRequests& requests)
    {
        return linux::write_file(requests);
    }

    WriteResult write(
        const std::filesystem::path& path,
        const std::string& new_content
    )
    {
        return linux::write_file(path, new_content);
    }
}

#else

#error "Unsupported operating system"

#endif
