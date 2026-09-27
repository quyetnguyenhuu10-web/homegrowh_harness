#include "process_protocol.h"
#include "process_path.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <istream>
#include <limits>
#include <ostream>
#include <string>
#include <string_view>

namespace sandbox::executable::process_protocol
{
    namespace
    {
        constexpr std::array<char, 8> request_magic{
            'H', 'H', 'S', 'B', 'X', '0', '0', '2',
        };
        constexpr std::array<char, 8> result_magic{
            'H', 'H', 'S', 'B', 'R', '0', '0', '1',
        };
        constexpr std::uint32_t max_field_bytes = 64u * 1024u * 1024u;
        constexpr std::uint32_t max_items = 4096u;

        bool read_exact(std::istream& input, void* data, std::size_t size)
        {
            input.read(static_cast<char*>(data), static_cast<std::streamsize>(size));
            return static_cast<std::size_t>(input.gcount()) == size;
        }

        bool read_u32(std::istream& input, std::uint32_t& value)
        {
            std::array<unsigned char, 4> bytes{};
            if (!read_exact(input, bytes.data(), bytes.size()))
                return false;
            value = static_cast<std::uint32_t>(bytes[0])
                | (static_cast<std::uint32_t>(bytes[1]) << 8)
                | (static_cast<std::uint32_t>(bytes[2]) << 16)
                | (static_cast<std::uint32_t>(bytes[3]) << 24);
            return true;
        }

        bool read_string(
            std::istream& input,
            std::string& value,
            std::string& error)
        {
            std::uint32_t size = 0;
            if (!read_u32(input, size))
            {
                error = "sandbox request ended while reading string length";
                return false;
            }
            if (size > max_field_bytes)
            {
                error = "sandbox request string exceeds protocol limit";
                return false;
            }
            value.resize(size);
            if (size != 0 && !read_exact(input, value.data(), size))
            {
                error = "sandbox request ended while reading string payload";
                return false;
            }
            return true;
        }

        void write_u32(std::ostream& output, std::uint32_t value)
        {
            const std::array<unsigned char, 4> bytes{
                static_cast<unsigned char>(value & 0xffu),
                static_cast<unsigned char>((value >> 8) & 0xffu),
                static_cast<unsigned char>((value >> 16) & 0xffu),
                static_cast<unsigned char>((value >> 24) & 0xffu),
            };
            output.write(
                reinterpret_cast<const char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
        }

        void write_i32(std::ostream& output, std::int32_t value)
        {
            write_u32(output, static_cast<std::uint32_t>(value));
        }

        void write_string(std::ostream& output, std::string_view value)
        {
            write_u32(output, static_cast<std::uint32_t>(value.size()));
            if (!value.empty())
            {
                output.write(
                    value.data(),
                    static_cast<std::streamsize>(value.size()));
            }
        }

        void write_error(
            std::ostream& output,
            const std::error_code& error,
            bool preserve_zero = false)
        {
            write_i32(output, static_cast<std::int32_t>(error.value()));
            write_string(
                output,
                (error || preserve_zero) ? error.message() : std::string{});
        }
    }

    bool read_request(
        std::istream& input,
        process_request& request,
        std::string& error)
    {
        std::array<char, request_magic.size()> magic{};
        if (!read_exact(input, magic.data(), magic.size()) || magic != request_magic)
        {
            error = "invalid sandbox process request magic";
            return false;
        }

        std::uint32_t timeout_ms = 0;
        std::uint32_t network = 0;
        std::uint32_t refresh = 0;
        if (!read_u32(input, timeout_ms)
            || !read_u32(input, network)
            || !read_u32(input, refresh))
        {
            error = "sandbox request ended before timeout/network/refresh fields";
            return false;
        }
        if (network > static_cast<std::uint32_t>(network_access::internet_client))
        {
            error = "sandbox request contains unknown network permission";
            return false;
        }
        if (refresh > 1)
        {
            error = "sandbox request contains invalid refresh flag";
            return false;
        }
        std::string executable;
        std::string working_directory;
        if (!read_string(input, executable, error)
            || !read_string(input, working_directory, error))
        {
            return false;
        }
        request.executable = detail::path_from_utf8(executable);
        request.working_directory = detail::path_from_utf8(working_directory);
        if (request.executable.empty())
        {
            error = "sandbox request executable is empty or invalid UTF-8";
            return false;
        }
        if (!working_directory.empty() && request.working_directory.empty())
        {
            error = "sandbox request working directory contains invalid UTF-8";
            return false;
        }

        std::uint32_t argument_count = 0;
        if (!read_u32(input, argument_count) || argument_count > max_items)
        {
            error = "sandbox request argument count is invalid";
            return false;
        }
        request.arguments.clear();
        request.arguments.reserve(argument_count);
        for (std::uint32_t index = 0; index < argument_count; ++index)
        {
            std::string argument;
            if (!read_string(input, argument, error))
                return false;
            request.arguments.push_back(std::move(argument));
        }

        std::uint32_t filesystem_count = 0;
        if (!read_u32(input, filesystem_count) || filesystem_count > max_items)
        {
            error = "sandbox request filesystem count is invalid";
            return false;
        }
        request.filesystem.clear();
        request.filesystem.reserve(filesystem_count);
        for (std::uint32_t index = 0; index < filesystem_count; ++index)
        {
            std::string path_text;
            std::uint32_t access = 0;
            if (!read_string(input, path_text, error) || !read_u32(input, access))
                return false;
            if (access > static_cast<std::uint32_t>(permission::read_write))
            {
                error = "sandbox request contains unknown filesystem permission";
                return false;
            }
            const std::filesystem::path path = detail::path_from_utf8(path_text);
            if (path.empty())
            {
                error = "sandbox request contains empty or invalid filesystem path";
                return false;
            }
            request.filesystem.push_back({
                path,
                static_cast<permission>(access),
            });
        }

        if (!read_string(input, request.stdin_data, error))
            return false;

        request.timeout = std::chrono::milliseconds(timeout_ms == 0 ? 1 : timeout_ms);
        request.network = static_cast<network_access>(network);
        request.refresh = refresh != 0;
        return true;
    }

    bool write_result(std::ostream& output, const process_result& result)
    {
        output.write(result_magic.data(), result_magic.size());
        write_u32(output, result.state.started ? 1u : 0u);
        write_u32(output, result.state.timed_out ? 1u : 0u);
        write_u32(output, result.state.terminated ? 1u : 0u);
        write_i32(output, static_cast<std::int32_t>(result.state.exit_code));
        write_error(
            output,
            result.state.os_error_before_termination,
            result.state.timed_out);
        write_error(output, result.state.final_error);
        write_error(output, result.state.registry.final_error);

        write_u32(
            output,
            static_cast<std::uint32_t>(result.state.registry.path_errors.size()));
        for (const registry_path_error& item : result.state.registry.path_errors)
        {
            write_string(output, detail::path_to_utf8(item.path));
            write_error(output, item.error);
        }

        write_string(output, result.stdout_text);
        write_string(output, result.stderr_text);
        output.flush();
        return static_cast<bool>(output);
    }
}
