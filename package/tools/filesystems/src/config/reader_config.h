#pragma once

#include <cstdint>

#ifndef FILESYSTEMS_READER_BLOCK_SIZE
#define FILESYSTEMS_READER_BLOCK_SIZE 4096
#endif

namespace fsystem::config
{
    inline constexpr std::uint32_t kReadBlockSize =
        static_cast<std::uint32_t>(FILESYSTEMS_READER_BLOCK_SIZE);
}
