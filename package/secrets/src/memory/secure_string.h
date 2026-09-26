#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>

namespace secrets
{
    class SecureString final
    {
    public:
        SecureString() noexcept = default;

        SecureString(const SecureString&) = delete;
        SecureString& operator=(const SecureString&) = delete;

        SecureString(SecureString&& other) noexcept;
        SecureString& operator=(SecureString&& other) noexcept;

        [[nodiscard]] bool empty() const noexcept;
        [[nodiscard]] std::size_t size() const noexcept;
        [[nodiscard]] std::string_view view() const noexcept;

    private:
        struct SecureDeleter
        {
            std::size_t bytes = 0;

            void operator()(char* value) const noexcept;
        };

        using Buffer = std::unique_ptr<char[], SecureDeleter>;

        SecureString(Buffer buffer, std::size_t size) noexcept;

        Buffer buffer_{nullptr, SecureDeleter{}};
        std::size_t size_ = 0;

        friend SecureString secure_string_from(std::string& value);
    };

    SecureString secure_string_from(std::string& value);
}
