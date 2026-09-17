#pragma once

#include <cstddef>

#ifndef FILESYSTEMS_EDIT_STREAM_CHUNK_SIZE
#define FILESYSTEMS_EDIT_STREAM_CHUNK_SIZE 65536
#endif

namespace fsystem::config
{
    inline constexpr std::size_t edit_stream_chunk_capacity =
        static_cast<std::size_t>(FILESYSTEMS_EDIT_STREAM_CHUNK_SIZE);
}
