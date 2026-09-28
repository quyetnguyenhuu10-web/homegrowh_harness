#include <ipc>

#include <chrono>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    void require(bool condition, const char* message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    std::vector<std::uint8_t> bytes(std::string_view value)
    {
        return std::vector<std::uint8_t>(value.begin(), value.end());
    }
}

int main()
{
    const std::string name =
        "homegrowh_ipc_test_" +
        std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count());

    ipc::server_result listener_result = ipc::listen(name);
    require(!listener_result.error, "listen failed");
    ipc::server listener = std::move(listener_result.value);

    ipc::connection_result client_result = ipc::connect(name);
    require(!client_result.error, "connect failed");
    ipc::connection client = std::move(client_result.value);

    ipc::connection_result accepted_result = ipc::accept(listener);
    require(!accepted_result.error, "accept failed");
    ipc::connection server_connection = std::move(accepted_result.value);

    const std::vector<std::uint8_t> from_client = bytes("from-client");
    ipc::write_result client_write = ipc::write(client, from_client);
    require(!client_write.error, "client write failed");

    ipc::read_result server_read = ipc::read(server_connection);
    require(!server_read.error, "server read failed");
    require(!server_read.closed, "server unexpectedly observed close");
    require(server_read.data == from_client, "server payload mismatch");

    const std::vector<std::uint8_t> from_server = bytes("from-server");
    ipc::write_result server_write = ipc::write(server_connection, from_server);
    require(!server_write.error, "server write failed");

    ipc::read_result client_read = ipc::read(client);
    require(!client_read.error, "client read failed");
    require(!client_read.closed, "client unexpectedly observed close");
    require(client_read.data == from_server, "client payload mismatch");

    const std::vector<std::uint8_t> empty;
    ipc::write_result empty_write = ipc::write(client, empty);
    require(!empty_write.error, "empty write failed");

    ipc::read_result empty_read = ipc::read(server_connection);
    require(!empty_read.error, "empty read failed");
    require(!empty_read.closed, "empty message was mistaken for close");
    require(empty_read.data.empty(), "empty message payload mismatch");

    const std::vector<std::uint8_t> binary = {
        0x00, 0xff, 0x01, 0x00, 0x7f, 0x80
    };
    ipc::write_result binary_write = ipc::write(client, binary);
    require(!binary_write.error, "binary write failed");

    ipc::read_result binary_read = ipc::read(server_connection);
    require(!binary_read.error, "binary read failed");
    require(binary_read.data == binary, "binary payload mismatch");

    ipc::connection_result missing = ipc::connect(
        "homegrowh_ipc_missing_endpoint_for_error_test");
    require(static_cast<bool>(missing.error), "connect error was not preserved");

    std::cout << "ipc duplex tests passed\n";
    return 0;
}
