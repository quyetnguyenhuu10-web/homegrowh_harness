#include "linux_writer.h"

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <fcntl.h>
#include <memory>
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

        WriteResult write_one(
            const std::filesystem::path& path,
            const std::string& new_content
        )
        {
            WriteResult result{};
            result.path = path;

            unique_fd handle{fd_handle{
                ::open(
                    path.c_str(),
                    O_WRONLY | O_CREAT | O_TRUNC,
                    0666
                )
            }};

            if (!handle)
            {
                result.error = static_cast<std::uint32_t>(errno);
                return result;
            }

            std::size_t offset = 0;

            while (offset < new_content.size())
            {
                const ssize_t bytes_written = ::write(
                    handle.get().get(),
                    new_content.data() + offset,
                    new_content.size() - offset
                );

                if (bytes_written < 0)
                {
                    if (errno == EINTR)
                        continue;

                    result.error = static_cast<std::uint32_t>(errno);
                    return result;
                }

                if (bytes_written == 0)
                {
                    result.error = EIO;
                    return result;
                }

                offset += static_cast<std::size_t>(bytes_written);
            }

            if (::fsync(handle.get().get()) < 0)
                result.error = static_cast<std::uint32_t>(errno);

            return result;
        }
    }

    WriteResults write_file(const WriteRequests& requests)
    {
        WriteResults results;
        results.reserve(requests.size());

        for (const WriteRequest& request : requests)
            results.push_back(write_one(request.path, request.new_content));

        return results;
    }

    WriteResult write_file(
        const std::filesystem::path& path,
        const std::string& new_content
    )
    {
        return write_one(path, new_content);
    }
}
