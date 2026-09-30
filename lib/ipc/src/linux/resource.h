#pragma once

#include <unistd.h>

#include <cerrno>
#include <string>
#include <utility>

namespace ipc::detail
{
    class unique_fd final
    {
    public:
        unique_fd() noexcept = default;
        explicit unique_fd(int value) noexcept : value_(value) {}
        unique_fd(const unique_fd&) = delete;
        unique_fd& operator=(const unique_fd&) = delete;
        unique_fd(unique_fd&& other) noexcept
            : value_(std::exchange(other.value_, -1)) {}
        unique_fd& operator=(unique_fd&& other) noexcept
        {
            if (this != &other)
            {
                close();
                value_ = std::exchange(other.value_, -1);
            }
            return *this;
        }
        ~unique_fd() { close(); }

        int get() const noexcept { return value_; }
        int close() noexcept
        {
            if (value_ >= 0)
            {
                const int descriptor = std::exchange(value_, -1);
                if (::close(descriptor) != 0)
                    close_error_ = errno;
                else
                    close_error_ = 0;
            }
            return close_error_;
        }

    private:
        int value_ = -1;
        int close_error_ = 0;
    };

    class owned_socket_path final
    {
    public:
        owned_socket_path() noexcept = default;
        explicit owned_socket_path(std::string&& path) noexcept : path_(std::move(path)) {}
        owned_socket_path(const owned_socket_path&) = delete;
        owned_socket_path& operator=(const owned_socket_path&) = delete;
        owned_socket_path(owned_socket_path&& other) noexcept
            : path_(std::move(other.path_)), owned_(std::exchange(other.owned_, false)) {}
        owned_socket_path& operator=(owned_socket_path&& other) noexcept
        {
            if (this != &other)
            {
                remove();
                path_ = std::move(other.path_);
                owned_ = std::exchange(other.owned_, false);
            }
            return *this;
        }
        ~owned_socket_path() { remove(); }

        const std::string& get() const noexcept { return path_; }
        void claim() noexcept { owned_ = true; }
        int remove() noexcept
        {
            if (std::exchange(owned_, false))
            {
                if (::unlink(path_.c_str()) != 0)
                    remove_error_ = errno;
                else
                    remove_error_ = 0;
            }
            return remove_error_;
        }

    private:
        std::string path_;
        bool owned_ = false;
        int remove_error_ = 0;
    };
}
