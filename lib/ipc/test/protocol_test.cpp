#include "test_support.h"

#include <iostream>
#include <thread>

#ifdef _WIN32
#include "windows/io.h"
#include "windows/resource.h"
#else
#include "linux/resource.h"
#include "error.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <cstring>
#endif

namespace
{
    using namespace ipc_test_support;

    class native_failure final : public std::exception
    {
    public:
        explicit native_failure(ipc::Error&& error) : error_(std::move(error)) {}
        const char* what() const noexcept override { return error_.message.c_str(); }
        const ipc::Error& error() const noexcept { return error_; }
    private:
        ipc::Error error_;
    };

#ifdef _WIN32
    using raw_peer = ipc::detail::unique_handle;

    raw_peer connect_raw(const std::string& name)
    {
        const std::wstring path = L"\\\\.\\pipe\\" + std::wstring(name.begin(), name.end());
        const HANDLE created = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
            0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (created == INVALID_HANDLE_VALUE)
        {
            const DWORD code = GetLastError();
            throw native_failure(ipc::detail::win32_error("connect", code, "CreateFileW", {{"name", name}}));
        }
        return raw_peer(created);
    }

    void write_raw(raw_peer& peer, std::span<const std::uint8_t> bytes)
    {
        DWORD written = 0;
        if (!WriteFile(peer.get(), bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr))
        {
            const DWORD code = GetLastError();
            throw native_failure(ipc::detail::win32_error("write", code, "WriteFile", nullptr));
        }
        require(written == bytes.size(), "Native test peer wrote an incomplete frame");
    }

    void close_raw(raw_peer& peer)
    {
        const DWORD code = peer.close();
        if (code != ERROR_SUCCESS)
            throw native_failure(ipc::detail::win32_error("close", code, "CloseHandle", nullptr));
    }
#else
    using raw_peer = ipc::detail::unique_fd;

    raw_peer connect_raw(const std::string& name)
    {
        const std::string path = "/tmp/" + name + ".sock";
        const int created = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (created < 0)
        {
            const int code = errno;
            throw native_failure(ipc::detail::make_system_error("connect",
                std::error_code(code, std::generic_category()), "socket", {{"path", path}}));
        }
        raw_peer peer(created);
        sockaddr_un address{};
        address.sun_family = AF_UNIX;
        require(path.size() < sizeof(address.sun_path), "Test endpoint is too long");
        std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
        if (::connect(peer.get(), reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0)
        {
            const int code = errno;
            throw native_failure(ipc::detail::make_system_error("connect",
                std::error_code(code, std::generic_category()), "connect", {{"path", path}}));
        }
        return peer;
    }

    void write_raw(raw_peer& peer, std::span<const std::uint8_t> bytes)
    {
        std::size_t offset = 0;
        while (offset < bytes.size())
        {
            const ssize_t written = ::send(peer.get(), bytes.data() + offset, bytes.size() - offset, MSG_NOSIGNAL);
            if (written < 0)
            {
                const int code = errno;
                if (code == EINTR)
                    continue;
                throw native_failure(ipc::detail::make_system_error("write",
                    std::error_code(code, std::generic_category()), "send", nullptr));
            }
            require(written != 0, "Native test peer wrote zero bytes");
            offset += static_cast<std::size_t>(written);
        }
    }

    void close_raw(raw_peer& peer)
    {
        const int code = peer.close();
        if (code != 0)
            throw native_failure(ipc::detail::make_system_error("close",
                std::error_code(code, std::generic_category()), "close", nullptr));
    }
#endif

    ipc::read_result read_raw_frame(std::span<const std::uint8_t> frame)
    {
        const auto name = endpoint_name();
        auto listener = ipc::listen(name);
        require(!listener.error, "Protocol test listener failed");
        auto peer = connect_raw(name);
        auto accepted = ipc::accept(listener.value);
        require(!accepted.error, "Protocol test accept failed");
        if (!frame.empty())
            write_raw(peer, frame);
        close_raw(peer);
        return ipc::read(accepted.value);
    }

    void require_truncated(
        const ipc::read_result& result, std::string_view phase,
        std::size_t expected, std::size_t transferred)
    {
        const auto& error = require_error(result.error, "read", "protocol_error");
        require(!result.closed && result.data.empty(), "Truncated frame exposed a successful read");
        const auto& data = error.data.at(0);
        require(data.at("phase").get<std::string>() == phase, "Truncated frame phase was lost");
        require(data.at("expected_size") == expected, "Truncated frame expected size was lost");
        require(data.at("transferred") == transferred, "Truncated frame progress was lost");
        require(data.contains("api") && data.contains("path"), "Truncated frame lost API or path");
        require(!data.contains("code"), "Protocol error invented an OS code");
#ifdef _WIN32
        require(error.causes.size() == 1, "Protocol wrapper lost the native pipe error");
        const auto& native = error.causes.front();
        require(native.type == "system_error", "Native pipe error type was changed");
        const int code = native.data.at(0).at("code").get<int>();
        require(code == ERROR_BROKEN_PIPE || code == ERROR_NO_DATA || code == ERROR_PIPE_NOT_CONNECTED,
            "Native pipe error code was changed");
        require(native.message == std::error_code(code, std::system_category()).message(),
            "Native pipe error message was changed");
#else
        require(error.causes.empty(), "Socket EOF invented a native error");
#endif
    }

    void malformed_frames()
    {
        const auto empty = read_raw_frame({});
        require(!empty.error && empty.closed, "Clean EOF was mistaken for a truncated frame");
        require_truncated(read_raw_frame(std::vector<std::uint8_t>{1, 0}), "header", 4, 2);
        require_truncated(read_raw_frame(std::vector<std::uint8_t>{5, 0, 0, 0}), "payload", 5, 0);
        require_truncated(read_raw_frame(std::vector<std::uint8_t>{5, 0, 0, 0, 0x41, 0x42}), "payload", 5, 2);
        const auto oversized = read_raw_frame(std::vector<std::uint8_t>{1, 0, 0, 1});
        const auto& error = require_error(oversized.error, "read", "protocol_error");
        require(error.data.at(0).at("size") == 16 * 1024 * 1024 + 1, "Oversized frame length was changed");
        require(oversized.data.empty() && !oversized.closed, "Oversized frame exposed payload or EOF");
    }

#ifdef _WIN32
    void overlapped_truncated_header()
    {
        const auto name = endpoint_name();
        const std::wstring path = L"\\\\.\\pipe\\" + std::wstring(name.begin(), name.end());
        const HANDLE created = CreateNamedPipeW(path.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, 4096, 4096, 0, nullptr);
        if (created == INVALID_HANDLE_VALUE)
        {
            const DWORD code = GetLastError();
            throw native_failure(ipc::detail::win32_error("listen", code, "CreateNamedPipeW", {{"name", name}}));
        }
        raw_peer peer(created);
        auto client = ipc::connect(name);
        require(!client.error, "Overlapped protocol test connect failed");
        if (!ConnectNamedPipe(peer.get(), nullptr))
        {
            const DWORD code = GetLastError();
            if (code != ERROR_PIPE_CONNECTED)
                throw native_failure(ipc::detail::win32_error("accept", code, "ConnectNamedPipe", nullptr));
        }
        ipc::read_result result;
        std::thread reader([&] { result = ipc::read(client.value); });
        std::exception_ptr failure;
        try
        {
            write_raw(peer, std::vector<std::uint8_t>{1, 0});
            if (!FlushFileBuffers(peer.get()))
            {
                const DWORD code = GetLastError();
                throw native_failure(ipc::detail::win32_error("write", code, "FlushFileBuffers", nullptr));
            }
            close_raw(peer);
        }
        catch (...)
        {
            failure = std::current_exception();
            peer.close();
        }
        reader.join();
        if (failure != nullptr)
            std::rethrow_exception(failure);
        require_truncated(result, "header", 4, 2);
    }
#endif
}

int main()
{
    try
    {
        malformed_frames();
#ifdef _WIN32
        overlapped_truncated_header();
#endif
        std::cout << "ipc framing error tests passed\n";
        return 0;
    }
    catch (const native_failure& failure)
    {
        std::cerr << nlohmann::json(failure.error()).dump() << '\n';
        return 1;
    }
    catch (const std::exception& exception)
    {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
