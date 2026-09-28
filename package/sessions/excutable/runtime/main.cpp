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
    void print_usage(const char* executable)
    {
        std::cerr
            << "usage:\n"
            << "  " << executable << " <ipc-name>\n";
    }
}

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        print_usage(argv[0]);
        return 2;
    }

    try
    {
        ipc::connection_result connected = ipc::connect(argv[1]);
        if (connected.error)
        {
            throw std::system_error(
                connected.error,
                "connect session runtime IPC");
        }

        ipc::connection connection = std::move(connected.value);
        sessions_runtime::EventForwarder forwarder(connection);
        sessions_runtime::Runtime runtime;

        forwarder.start();
        runtime.emit_ready();

        for (;;)
        {
            forwarder.rethrow_if_failed();

            ipc::read_result read = ipc::read(connection);
            if (read.error)
            {
                throw std::system_error(
                    read.error,
                    "read session runtime command");
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
        forwarder.rethrow_if_failed();
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr
            << "session runtime error: "
            << error.what()
            << '\n';
        return 1;
    }
}
