#pragma once

#include "edit_context.h"
#include "windows_handle.h"

#include <config/edit_config.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace fsystem::windows::detail
{
    class source_file
    {
    public:
        bool open(
            const std::filesystem::path& path,
            std::uint32_t& error
        );

        void reset() noexcept;

        std::uint64_t size() const noexcept;

        template<typename callback_type>
        bool for_each_chunk(
            callback_type&& callback,
            watcher_state& watcher_state,
            std::uint32_t& error
        )
        {
            /* Cancellation can already have happened before the read. */
            if (cancellation_requested(watcher_state))
                return false;

            if (!rewind(error))
                return false;

            constexpr std::size_t stream_chunk_capacity =
                fsystem::config::edit_stream_chunk_capacity;

            std::string chunk(stream_chunk_capacity, '\0');
            std::uint64_t position = 0;

            while (position < size_)
            {
                /* Stop before starting another chunk after cancellation. */
                if (cancellation_requested(watcher_state))
                    return false;

                const std::uint64_t remaining = size_ - position;

                const std::size_t requested = static_cast<std::size_t>(
                    std::min(
                        remaining,
                        static_cast<std::uint64_t>(
                            stream_chunk_capacity
                        )
                    )
                );

                std::size_t filled = 0;

                if (!read_chunk(
                        chunk.data(),
                        requested,
                        filled,
                        watcher_state,
                        error
                    ))
                {
                    return false;
                }

                if (!callback(
                        position,
                        std::string_view(
                            chunk.data(),
                            filled
                        )
                    ))
                {
                    /* A false callback result means normal early stop. */
                    return true;
                }

                /* The watcher may cancel while the callback is running. */
                if (cancellation_requested(watcher_state))
                    return false;

                position += filled;
            }

            return true;
        }

    private:
        bool rewind(std::uint32_t& error);

        bool read_chunk(
            char* buffer,
            std::size_t requested,
            std::size_t& filled,
            watcher_state& watcher_state,
            std::uint32_t& error
        );

        unique_handle handle_{nullptr};
        std::uint64_t size_ = 0;
    };
}
