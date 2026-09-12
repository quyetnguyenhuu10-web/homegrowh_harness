#include "linux_reader.h"

#include "../../../../config/reader_config.h"

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <fcntl.h>
#include <memory>
#include <span>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace fsystem::linux
{
    namespace
    {
        class fd_handle
        {
        public:
            constexpr fd_handle() noexcept = default;
            constexpr fd_handle(std::nullptr_t) noexcept {}
            explicit constexpr fd_handle(int value) noexcept : value_(value) {}

            constexpr int get() const noexcept
            {
                return value_;
            }

            constexpr explicit operator bool() const noexcept
            {
                return value_ >= 0;
            }

            friend constexpr bool operator==(fd_handle lhs, fd_handle rhs) noexcept
            {
                return lhs.value_ == rhs.value_;
            }

            friend constexpr bool operator!=(fd_handle lhs, fd_handle rhs) noexcept
            {
                return !(lhs == rhs);
            }

            friend constexpr bool operator==(fd_handle lhs, std::nullptr_t) noexcept
            {
                return lhs.value_ < 0;
            }

            friend constexpr bool operator==(std::nullptr_t, fd_handle rhs) noexcept
            {
                return rhs == nullptr;
            }

            friend constexpr bool operator!=(fd_handle lhs, std::nullptr_t) noexcept
            {
                return !(lhs == nullptr);
            }

            friend constexpr bool operator!=(std::nullptr_t, fd_handle rhs) noexcept
            {
                return !(rhs == nullptr);
            }

        private:
            int value_{-1};
        };

        struct fd_deleter
        {
            using pointer = fd_handle;

            void operator()(pointer handle) const noexcept
            {
                if (handle != nullptr)
                    (void)::close(handle.get());
            }
        };

        using unique_fd = std::unique_ptr<int, fd_deleter>;
    }

    ReadResult read_file(std::filesystem::path path)
    {
        ReadResult result{};

        constexpr std::uint32_t block_size =
            fsystem::config::kReadBlockSize;

        unique_fd handle{fd_handle{::open(path.c_str(), O_RDONLY)}};

        if (!handle)
        {
            result.error = static_cast<std::uint32_t>(errno);
            return result;
        }

        struct stat file_status{};

        if (::fstat(handle.get().get(), &file_status) < 0)
        {
            result.error = static_cast<std::uint32_t>(errno);
            return result;
        }

        if (file_status.st_size < 0)
        {
            result.error = EOVERFLOW;
            return result;
        }

        const std::uintmax_t file_size_for_allocation =
            static_cast<std::uintmax_t>(file_status.st_size);

        if (file_size_for_allocation >
            static_cast<std::uintmax_t>(result.content.max_size()))
        {
            result.error = EFBIG;
            return result;
        }

        result.content.resize(
            static_cast<std::size_t>(file_size_for_allocation)
        );

        const std::size_t block_size_bytes = block_size;
        const off_t file_size = file_status.st_size;
        off_t position = 0;

        if (file_size == 0)
            return result;

        while (position < file_size)
        {
            const off_t remaining = file_size - position;

            const std::size_t chunk_size =
                remaining < static_cast<off_t>(block_size_bytes)
                    ? static_cast<std::size_t>(remaining)
                    : block_size_bytes;

            std::span<char> destination(
                result.content.data() +
                    static_cast<std::size_t>(position),
                chunk_size
            );

            const ssize_t bytes_read = ::pread(
                handle.get().get(),
                destination.data(),
                destination.size(),
                position
            );

            if (bytes_read < 0)
            {
                if (errno == EINTR)
                    continue;

                result.error = static_cast<std::uint32_t>(errno);
                result.content.clear();
                return result;
            }

            if (bytes_read == 0 ||
                static_cast<std::size_t>(bytes_read) < chunk_size)
            {
                result.error = EIO;
                result.content.clear();
                return result;
            }

            position += static_cast<off_t>(bytes_read);
        }

        return result;
    }
}
