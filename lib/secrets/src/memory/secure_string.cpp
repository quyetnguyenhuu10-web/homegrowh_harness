#include "secure_string.h"

#include <cstring>
#include <stdexcept>
#include <utility>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace secrets
{
    namespace
    {
        void secure_zero(void* memory, std::size_t bytes) noexcept
        {
            if (memory == nullptr || bytes == 0)
                return;

#if defined(_WIN32)
            SecureZeroMemory(memory, bytes);
#else
            volatile unsigned char* current =
                static_cast<volatile unsigned char*>(memory);
            while (bytes-- != 0)
                *current++ = 0;
#endif
        }

        void wipe_string(std::string& value) noexcept
        {
            if (!value.empty())
                secure_zero(value.data(), value.size());

            std::string empty;
            value.swap(empty);
        }
    }

    void SecureString::SecureDeleter::operator()(char* value) const noexcept
    {
        if (value == nullptr)
            return;

        secure_zero(value, bytes);
        delete[] value;
    }

    SecureString::SecureString(Buffer buffer, std::size_t size) noexcept
        : buffer_(std::move(buffer)),
          size_(size)
    {
    }

    SecureString::SecureString(SecureString&& other) noexcept
        : buffer_(std::move(other.buffer_)),
          size_(std::exchange(other.size_, 0))
    {
    }

    SecureString& SecureString::operator=(SecureString&& other) noexcept
    {
        if (this == &other)
            return *this;

        buffer_ = std::move(other.buffer_);
        size_ = std::exchange(other.size_, 0);
        return *this;
    }

    bool SecureString::empty() const noexcept
    {
        return size_ == 0;
    }

    std::size_t SecureString::size() const noexcept
    {
        return size_;
    }

    std::string_view SecureString::view() const noexcept
    {
        if (size_ == 0)
            return {};
        return {buffer_.get(), size_};
    }

    SecureString secure_string_from(std::string& value)
    {
        if (value.empty())
            throw std::invalid_argument("secure string source must not be empty");

        const std::size_t size = value.size();
        SecureString::Buffer buffer(
            new char[size + 1],
            SecureString::SecureDeleter{size + 1});

        std::memcpy(buffer.get(), value.data(), size);
        buffer[size] = '\0';

        wipe_string(value);
        return SecureString(std::move(buffer), size);
    }
}
