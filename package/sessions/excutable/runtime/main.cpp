#include "command.h"
#include "event_forwarder.h"
#include "runtime.h"

#include <ipc>

#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

namespace
{
    constexpr int usage_exit_code = 2;
    constexpr int runtime_exit_code = 1;

    void print_usage(const char* executable)
    {
        std::cerr
            << "usage:\n"
            << "  " << executable << " <ipc-name>\n";
    }

    void print_failure(std::exception_ptr error) noexcept
    {
        if (error == nullptr)
            return;

        try
        {
            std::rethrow_exception(error);
        }
        catch (const std::exception& exception)
        {
            std::cerr
                << "session runtime error: "
                << exception.what()
                << '\n';
        }
        catch (...)
        {
            std::cerr << "session runtime error: unknown exception\n";
        }
    }
}

int main(int argc, char** argv)
{
    sessions_runtime::Runtime runtime;

    if (argc != 2)
    {
        const std::exception_ptr error = std::make_exception_ptr(
            std::invalid_argument(
                "usage: session_runtime <ipc-name>"));
        runtime.emit_runtime_failure(
            "arguments",
            error,
            usage_exit_code);
        print_usage(argv[0]);
        return usage_exit_code;
    }

    try
    {
        ipc::connection_result connected = ipc::connect(argv[1]);
        if (connected.error)
        {
            const std::exception_ptr error = std::make_exception_ptr(
                std::system_error(
                    connected.error,
                    "connect session runtime IPC"));
            runtime.emit_runtime_failure(
                "ipc_connect",
                error,
                runtime_exit_code);
            print_failure(error);
            return runtime_exit_code;
        }

        ipc::connection connection = std::move(connected.value);
        sessions_runtime::EventForwarder forwarder(connection);

        forwarder.start();
        runtime.emit_ready();

        for (;;)
        {
            try
            {
                forwarder.rethrow_if_failed();
            }
            catch (...)
            {
                const std::exception_ptr error = std::current_exception();
                runtime.emit_runtime_failure(
                    "event_forwarder",
                    error,
                    runtime_exit_code);
                forwarder.stop();
                print_failure(error);
                return runtime_exit_code;
            }

            ipc::read_result read = ipc::read(connection);
            if (read.error)
            {
                const std::exception_ptr error = std::make_exception_ptr(
                    std::system_error(
                        read.error,
                        "read session runtime command"));
                runtime.emit_runtime_failure(
                    "ipc_read",
                    error,
                    runtime_exit_code);
                forwarder.stop();
                print_failure(error);
                return runtime_exit_code;
            }
            if (read.closed)
                break;

            try
            {
                sessions_runtime::Command command =
                    sessions_runtime::decode_command(read.data);
                if (runtime.execute(std::move(command)))
                    break;
            }
            catch (...)
            {
                runtime.emit_protocol_error(
                    std::current_exception());
            }
        }

        forwarder.stop();
        try
        {
            forwarder.rethrow_if_failed();
        }
        catch (...)
        {
            const std::exception_ptr error = std::current_exception();
            runtime.emit_runtime_failure(
                "event_forwarder",
                error,
                runtime_exit_code);
            print_failure(error);
            return runtime_exit_code;
        }
        return 0;
    }
    catch (...)
    {
        const std::exception_ptr error = std::current_exception();
        runtime.emit_runtime_failure(
            "startup",
            error,
            runtime_exit_code);
        print_failure(error);
        return runtime_exit_code;
    }
}
