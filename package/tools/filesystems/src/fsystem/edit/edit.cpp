#include "fsystem/edit/edit.h"

#if defined(_WIN32)

#include "../../platform/windows/fsystem/edit/window_edit.h"

namespace fsystem
{
    EditResults edit(const EditRequests& requests)
    {
        return windows::edit_file(requests);
    }

    EditResult edit(
        const std::filesystem::path& path,
        const std::string& old_data,
        const std::string& new_data
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

#include "../../platform/linux/fsystem/edit/linux_edit.h"

namespace fsystem
{
    EditResults edit(const EditRequests& requests)
    {
        return linux::edit_file(requests);
    }

    EditResult edit(
        const std::filesystem::path& path,
        const std::string& old_data,
        const std::string& new_data
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
