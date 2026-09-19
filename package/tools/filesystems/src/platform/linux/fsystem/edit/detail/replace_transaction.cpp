#include "replace_transaction.h"

#include "edit_context.h"

#include <atomic>
#include <cerrno>
#include <unistd.h>

namespace fsystem::linux::detail
{
    bool commit_replace(
        const std::filesystem::path& path,
        source_file& source,
        temporary_file& temp,
        watcher_state& watcher_state,
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

        /* Timeout and commit race through one atomic final gate. */
        if (!begin_commit(watcher_state))
        {
            temp.discard();
            return false;
        }

        /* Both handles are closed before the irreversible rename. */
        temp.close();
        source.reset();

        replace_attempted = true;

        if (::rename(temp.file_path().c_str(), path.c_str()) < 0)
        {
            error = static_cast<std::uint32_t>(errno);
            temp.discard();
            return false;
        }

        /* rename succeeded: this is the edit linearization point. */
        mark_committed(watcher_state);
        temp.commit_success();
        return true;
    }
}
