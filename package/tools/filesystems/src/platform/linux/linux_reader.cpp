#include "linux_reader.h"

#include <cerrno>
#include <cstdint>
#include <fcntl.h>
#include <limits>
#include <sys/types.h>
#include <unistd.h>

namespace file::linux
{
    namespace
    {
        struct file_descriptor
        {
            int value{-1};

            ~file_descriptor()
            {
                if (value >= 0)
                    ::close(value);
            }

            file_descriptor(const file_descriptor&) = delete;
            file_descriptor& operator=(const file_descriptor&) = delete;

            int get() const
            {
                return value;
            }
        };
    }

    ReadResult read_file(
        std::filesystem::path path,
        std::uint32_t MAX_BYTES_READ
    )
    {
        ReadResult result{};

        file_descriptor handle{::open(path.c_str(), O_RDONLY)};

        if (handle.get() < 0)
        {
            result.error = static_cast<std::uint32_t>(errno);
            return result;
        }

        const std::size_t block_size = MAX_BYTES_READ;

        if (block_size > static_cast<std::size_t>(std::numeric_limits<ssize_t>::max()))
        {
            result.error = EOVERFLOW;
            return result;
        }

        std::string buffer(block_size, '\0');
        std::uint64_t block_index = 0;

        while (true)
        {
            if (MAX_BYTES_READ != 0 &&
                block_index >
                    static_cast<std::uint64_t>(std::numeric_limits<off_t>::max()) /
                        MAX_BYTES_READ)
            {
                result.error = EOVERFLOW;
                result.content.clear();
                return result;
            }

            const std::uint64_t position =
                static_cast<std::uint64_t>(MAX_BYTES_READ) * block_index;

            const ssize_t bytes_read = ::pread(
                handle.get(),
                buffer.data(),
                block_size,
                static_cast<off_t>(position)
            );

            if (bytes_read < 0)
            {
                if (errno == EINTR)
                    continue;

                result.error = static_cast<std::uint32_t>(errno);
                result.content.clear();
                return result;
            }

            if (bytes_read == 0)
                break;

            result.content.append(
                buffer.data(),
                static_cast<std::size_t>(bytes_read)
            );

            if (static_cast<std::size_t>(bytes_read) < block_size)
                break;

            ++block_index;
        }

        return result;
    }
}
