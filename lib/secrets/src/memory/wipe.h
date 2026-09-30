#pragma once

#include <cstddef>
#include <string>

namespace secrets::detail
{
    void secure_zero(void* memory, std::size_t bytes) noexcept;
    void wipe_string(std::string& value) noexcept;

    class StringWiper final
    {
    public:
        explicit StringWiper(std::string& value) noexcept : value_(value) {}
        StringWiper(const StringWiper&) = delete;
        StringWiper& operator=(const StringWiper&) = delete;
        ~StringWiper() noexcept { wipe_string(value_); }

    private:
        std::string& value_;
    };
}
