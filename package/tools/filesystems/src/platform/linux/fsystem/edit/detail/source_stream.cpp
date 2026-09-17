#include "source_stream.h"

#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace fsystem::linux::detail
{
    bool source_file::open(
        const std::filesystem::path& path,
        std::uint32_t& error
    )
    {
        handle_.reset();

        const int raw_fd = ::open(
            path.c_str(),
            O_RDONLY | O_CLOEXEC
        );

        if (raw_fd < 0)
        {
            error = static_cast<std::uint32_t>(errno);
            return false;
        }

        handle_.reset(fd_handle{raw_fd});

        struct stat file_status{};

        if (::fstat(handle_.get().get(), &file_status) < 0)
        {
            error = static_cast<std::uint32_t>(errno);
            handle_.reset();
            return false;
        }

        if (file_status.st_size < 0)
        {
            error = EOVERFLOW;
            handle_.reset();
            return false;
        }

        size_ = static_cast<std::uint64_t>(file_status.st_size);
        return true;
    }

    void source_file::reset() noexcept
    {
        handle_.reset();
        size_ = 0;
    }

    std::uint64_t source_file::size() const noexcept
    {
        return size_;
    }

    bool source_file::rewind(std::uint32_t& error)
    {
        if (::lseek(handle_.get().get(), 0, SEEK_SET) < 0)
        {
            error = static_cast<std::uint32_t>(errno);
            return false;
        }

        return true;
    }

    bool source_file::read_chunk(
        char* buffer,
        std::size_t requested,
        std::size_t& filled,
        fsystem::WatcherState& watcher_state,
        std::uint32_t& error
    )
    {
        filled = 0;

        while (filled < requested)
        {
            if (cancellation_requested(watcher_state))
                return false;

            const ssize_t bytes_read = ::read(
                handle_.get().get(),
                buffer + filled,
                requested - filled
            );

            if (bytes_read < 0)
            {
                if (errno == EINTR)
                    continue;

                error = static_cast<std::uint32_t>(errno);
                return false;
            }

            if (bytes_read == 0)
            {
                error = EIO;
                return false;
            }

            filled += static_cast<std::size_t>(bytes_read);

            if (cancellation_requested(watcher_state))
                return false;
        }

        return true;
    }
}
