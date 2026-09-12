#include "file/edit.h"

#if defined(_WIN32)

#include "../platform/windows/file/edit/window_edit.h"

namespace file
{
    EditResult edit(
        std::filesystem::path path,
        std::string old_data,
        std::string new_data
    )
    {
        return windows::edit_file(
            path,
            old_data,
            new_data
        );
    }
}

#elif defined(__linux__)

#include "../platform/linux/file/edit/linux_edit.h"

namespace file
{
    EditResult edit(
        std::filesystem::path path,
        std::string old_data,
        std::string new_data
    )
    {
        return linux::edit_file(
            path,
            old_data,
            new_data
        );
    }
}

#else

#error "Unsupported operating system"

#endif
