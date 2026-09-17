#include "replace_transaction.h"

#include "edit_context.h"

#include <atomic>

namespace fsystem::windows::detail
{
    bool commit_replace(
        const std::filesystem::path& path,
        source_file& source,
        temporary_file& temp,
        fsystem::WatcherState& watcher_state,
        bool& replace_attempted,
        std::uint32_t& error
    )
    {
        if (
            watcher_state.file_changed.load(
                std::memory_order_acquire
            )
        )
        {
            temp.discard();
            return false;
        }

        if (cancellation_requested(watcher_state))
        {
            temp.discard();
            return false;
        }

        /* Both handles must be closed before the irreversible replacement. */
        temp.close();
        source.reset();

        replace_attempted = true;

        if (!ReplaceFileW(
                path.c_str(),
                temp.file_path().c_str(),
                nullptr,
                REPLACEFILE_WRITE_THROUGH,
                nullptr,
                nullptr
            ))
        {
            error = GetLastError();
            temp.discard();
            return false;
        }

        temp.commit_success();
        return true;
    }
}
