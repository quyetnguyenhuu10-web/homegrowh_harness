#include "linux_reader.h"

#include "../../../../config/reader_config.h"
#include "fsystem/read/read_detail.h"

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <fcntl.h>
#include <limits>
#include <memory>
#include <string_view>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <utility>
#include <vector>

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

        ReadResult read_one(const ReadRequest& request)
        {
            ReadResult result{};
            result.path = request.path;
            result.start_line = request.start_line;
            result.end_line = request.end_line;

            if (
                request.start_line == 0 ||
                request.end_line < request.start_line
            )
            {
                result.error = EINVAL;
                return result;
            }

            constexpr std::uint32_t block_size =
                fsystem::config::kReadBlockSize;

            unique_fd handle{fd_handle{::open(request.path.c_str(), O_RDONLY)}};

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

            const off_t file_size = file_status.st_size;

            if (file_size == 0)
                return result;

            std::vector<char> buffer(block_size);
            detail::line_range_collector collector(
                request.start_line,
                request.end_line,
                result.content
            );

            off_t position = 0;

            while (position < file_size)
            {
                const off_t remaining = file_size - position;
                const std::size_t chunk_size =
                    remaining < static_cast<off_t>(block_size)
                        ? static_cast<std::size_t>(remaining)
                        : static_cast<std::size_t>(block_size);

                const ssize_t bytes_read = ::pread(
                    handle.get().get(),
                    buffer.data(),
                    chunk_size,
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

                if (
                    bytes_read == 0 ||
                    static_cast<std::size_t>(bytes_read) < chunk_size
                )
                {
                    result.error = EIO;
                    result.content.clear();
                    return result;
                }

                const bool continue_reading = collector.consume(
                    std::string_view(
                        buffer.data(),
                        static_cast<std::size_t>(bytes_read)
                    )
                );

                if (!continue_reading)
                {
                    if (collector.overflowed())
                    {
                        result.error = EFBIG;
                        result.content.clear();
                    }

                    return result;
                }

                position += static_cast<off_t>(bytes_read);
            }

            return result;
        }
    }

    ReadResults read_file(const ReadRequests& requests)
    {
        ReadResults results;
        results.reserve(requests.size());

        for (const ReadRequest& request : requests)
            results.push_back(read_one(request));

        return results;
    }

    ReadResult read_file(
        const std::filesystem::path& path,
        std::uint64_t start_line,
        std::uint64_t end_line
    )
    {
        const ReadRequests requests{
            ReadRequest{path, start_line, end_line}
        };

        ReadResults results = read_file(requests);

        return results.empty()
            ? ReadResult{path, start_line, end_line}
            : std::move(results.front());
    }

    ReadResult read_file(const std::filesystem::path& path)
    {
        return read_file(
            path,
            1,
            std::numeric_limits<std::uint64_t>::max()
        );
    }
}
