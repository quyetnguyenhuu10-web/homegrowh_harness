#pragma once

#include <ipc>

#include <exception>
#include <utility>

namespace sessions_runtime
{
    // Preserve the IPC error across the runtime's exception channel.
    class IpcFailure final : public std::exception
    {
    public:
        explicit IpcFailure(ipc::Error&& error) noexcept : error_(std::move(error)) {}
        const char* what() const noexcept override { return error_.message.c_str(); }
        const ipc::Error& error() const noexcept { return error_; }

    private:
        ipc::Error error_;
    };
}
