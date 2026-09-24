#pragma once

#include <cstddef>
#include <cstdint>

namespace provider
{
    struct RawResponse;

    namespace detail
    {
        enum class NativeCompletionType
        {
            data,
            failed,
            finished
        };

        struct NativeCompletion
        {
            NativeCompletionType type = NativeCompletionType::data;
            RawResponse* response = nullptr;
            std::size_t bytes = 0;
            std::uint32_t error = 0;
        };
    }
}
