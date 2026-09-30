#include <sandbox>

#include "error_schema.h"
#include "process/io_failure.h"

#if defined(_WIN32)
#include "window/state.h"
#elif defined(__linux__)
#include "linux/state.h"
#endif

#include <cerrno>
#include <iostream>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <thread>

namespace
{
    void require(bool condition, const char* message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    sandbox::Error sample_error()
    {
        return {
            "plugin_loader", "invoke", "dependency_error", "Plugin invocation failed",
            {nullptr, false, 12, "detail", nlohmann::json::array({1, 2}),
             nlohmann::json{{"plugin_id", "test"}, {"path", "sample-path"}}},
            {
                {"sandbox", "grant_filesystem", "system_error", "Access denied",
                 {nlohmann::json{{"code", 5}, {"api", "SetNamedSecurityInfoW"},
                                 {"path", "sample-path"}}}, {}},
                {"provider", "request", "http_error", "HTTP request failed",
                 {nlohmann::json{{"status_code", 429}, {"body", "response"}}}, {}},
            },
        };
    }

    void schema_round_trip()
    {
        const auto original = sample_error();
        const auto encoded = sandbox::serialize_error(original);
        require(encoded.value.has_value() && !encoded.error, "serialize Result branch");
        require(encoded.value->size() == 6, "error field count");
        require(encoded.value->at("data").is_array(), "data must be an array");
        require(encoded.value->at("causes").is_array(), "causes must be an array");
        const auto decoded = sandbox::deserialize_error(*encoded.value);
        require(decoded.value.has_value() && !decoded.error, "deserialize Result branch");
        require(nlohmann::json(*decoded.value) == *encoded.value, "round trip lost error identity");

        auto invalid = *encoded.value;
        invalid["causes"][1]["data"] = nlohmann::json::object();
        const auto rejected = sandbox::deserialize_error(invalid);
        require(!rejected.value && rejected.error.has_value(), "invalid data accepted");
        require(rejected.error->type == "protocol_error", "schema error type");
        require(rejected.error->data.at(0).at("path") == "$.causes[1].data", "schema error path");
        require(rejected.error->data.at(0).at("payload") == invalid, "malformed payload lost");

        invalid = *encoded.value;
        invalid["code"] = 5;
        require(sandbox::deserialize_error(invalid).error.has_value(), "extra field accepted");
        invalid = *encoded.value;
        invalid.erase("operation");
        require(sandbox::deserialize_error(invalid).error.has_value(), "missing field accepted");
        invalid = *encoded.value;
        invalid["message"] = 5;
        require(sandbox::deserialize_error(invalid).error.has_value(), "non-string message accepted");
        invalid = *encoded.value;
        invalid["causes"] = nullptr;
        require(sandbox::deserialize_error(invalid).error.has_value(), "null causes accepted");
        require(sandbox::deserialize_error(nullptr).error.has_value(), "non-object error accepted");
    }

    void native_exception_details()
    {
        const auto native = sandbox::detail::make_native_error("NativeApi", 0xffff0001u);
        require(native.data.at(0).at("code") == 0xffff0001u, "DWORD was narrowed");
        require(native.data.at(0).at("api") == "NativeApi", "native API was lost");

        const std::filesystem::filesystem_error filesystem(
            "rename failed", std::filesystem::path("old-path"),
            std::filesystem::path("new-path"), std::error_code(EACCES, std::generic_category()));
        const auto captured = sandbox::detail::make_exception_error("rename", filesystem);
        require(captured.message == filesystem.what(), "native exception message lost");
        require(captured.data.at(0).at("code") == EACCES, "native errno lost");
        require(captured.data.at(0).at("category") == "generic", "native category lost");
        require(captured.data.at(0).at("path1") == "old-path", "native first path lost");
        require(captured.data.at(0).at("path2") == "new-path", "native second path lost");

        try
        {
            const auto parsed = nlohmann::json::parse("{");
            static_cast<void>(parsed);
            require(false, "invalid JSON accepted");
        }
        catch (const nlohmann::json::parse_error& exception)
        {
            const auto error = sandbox::detail::capture_exception(
                "parse", std::current_exception(), {{"body", "{"}});
            require(error.type == "protocol_error", "JSON exception type");
            require(error.message == exception.what(), "JSON message lost");
            require(error.data.at(0).at("id") == exception.id, "JSON exception id lost");
            require(error.data.at(0).at("byte") == exception.byte, "JSON exception byte lost");
            require(error.data.at(1).at("body") == "{", "JSON input lost");
        }

        std::exception_ptr structured;
        try
        {
            sandbox::detail::throw_error(sample_error());
        }
        catch (...)
        {
            structured = std::current_exception();
        }
        const auto first = sandbox::detail::capture_exception("outer", structured);
        const auto second = sandbox::detail::capture_exception("outer", structured);
        require(nlohmann::json(first) == nlohmann::json(sample_error()), "structured error was wrapped");
        require(nlohmann::json(second) == nlohmann::json(first), "observing exception consumed it");
    }

    void result_and_config()
    {
        auto owned = sandbox::Result<std::unique_ptr<int>>::success(std::make_unique<int>(42));
        require(owned.value.has_value() && !owned.error && **owned.value == 42, "move-only Result");
        const auto failed = sandbox::Result<int>::failure(sample_error());
        require(!failed.value && failed.error.has_value(), "failure Result branch");
        const auto success = sandbox::Result<void>::success();
        require(success.value.has_value() && !success.error, "void Result branch");

        sandbox::config config;
        require(config.add(sandbox::read_only("sample-path")).value.has_value(), "valid config");
        const auto conflict = config.add(sandbox::read_write("sample-path"));
        require(conflict.error.has_value() && !conflict.value, "conflicting config accepted");
        require(config.error().has_value(), "config did not retain error");
        require(conflict.error->data.at(0).at("path") == "sample-path", "config path lost");

        sandbox::config invalid_network;
        const auto invalid = invalid_network.add(sandbox::network(
            static_cast<sandbox::network_access>(99)));
        require(invalid.error.has_value(), "invalid network policy accepted");
        require(invalid.error->data.at(0).at("requested_access") == 99, "network policy lost");

        sandbox::process_request request;
        request.config = sandbox::config{sandbox::read_only(std::filesystem::path{})};
        require(request.config.error().has_value(), "constructor error was not retained");
        const auto process = sandbox::process(request);
        require(process.error.has_value() && !process.value, "invalid config process accepted");
        require(!request.results.state.started, "invalid config started a process");
        require(nlohmann::json(*process.error) == nlohmann::json(*request.config.error()),
                "config error was not forwarded");
        require(request.results.state.config.final_error.has_value(), "lifecycle config error lost");
    }

    void io_failure_details()
    {
        sandbox::detail::io_failure<int> no_progress;
        no_progress.record_no_progress(8, 32);
        const auto stalled = no_progress.observe("write(stdin)", std::generic_category());
        require(stalled.has_value() && stalled->type == "io_error", "zero-byte write error type");
        require(stalled->data.at(0).at("written") == 0, "observed write count lost");
        require(stalled->data.at(0).at("offset") == 8, "write offset lost");
        require(!stalled->data.at(0).contains("code"), "zero-byte write fabricated an OS code");
        require(stalled->data.at(0).at("api") == "write", "write API lost");

        sandbox::detail::io_failure<int> allocation;
        std::thread worker([&]
        {
            try
            {
                // Simulate a native allocation failure in an output worker.
                throw std::bad_alloc();
            }
            catch (...)
            {
                allocation.record_exception();
            }
        });
        worker.join();
        const auto error = allocation.observe("read(stdout)", std::generic_category());
        require(error.has_value() && error->type == "resource_error", "worker exception lost");
        require(error->data.at(0).contains("exception_type"), "worker native type lost");

        allocation.store(EACCES);
        const auto multiple = allocation.observe("read(stdout)", std::generic_category());
        require(multiple.has_value() && multiple->causes.size() == 2, "independent I/O errors lost");
        require(multiple->causes.at(0).data.at(0).at("code") == EACCES, "I/O errno lost");
        require(multiple->causes.at(1).type == "resource_error", "I/O exception cause lost");
        require(sandbox::detail::io_already_observed(*stalled, stalled),
                "pre-termination error was duplicated");
        require(!sandbox::detail::io_already_observed(*error, stalled),
                "new I/O error was discarded");
    }

    void malformed_registry_payload()
    {
        const auto path = std::filesystem::temp_directory_path()
            / ("sandbox-error-schema-" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()) + ".state");
        struct fixture final
        {
            std::filesystem::path path;
            ~fixture()
            {
                std::error_code error;
                std::filesystem::remove(path, error);
                if (error)
                    std::cerr << error.message() << '\n';
            }
        } cleanup{path};

        const std::string body = std::string("invalid registry\n") + static_cast<char>(0xff);
        {
            std::ofstream file(path, std::ios::binary);
            require(file.is_open(), "registry payload fixture open");
            file.write(body.data(), static_cast<std::streamsize>(body.size()));
            file.close();
            require(!file.fail(), "registry payload fixture write");
        }
        try
        {
#if defined(_WIN32)
            const auto state = sandbox::detail::filesystem::windows::load_registry_state(path);
#elif defined(__linux__)
            const auto state = sandbox::detail::filesystem::linux::load_registry_state(path, false);
#endif
            static_cast<void>(state);
            require(false, "malformed registry accepted");
        }
        catch (const sandbox::detail::error_exception& exception)
        {
            const auto& error = exception.error();
            require(error.operation == "load_registry_state", "registry error identity");
            require(error.type == "invalid_state", "registry error type");
            require(error.data.at(0).at("body_bytes")
                == nlohmann::json(std::vector<unsigned char>(body.begin(), body.end())),
                "registry bytes lost");
            const auto wire = nlohmann::json(error).dump();
            const auto decoded = sandbox::deserialize_error(nlohmann::json::parse(wire));
            require(decoded.value.has_value(), "registry error wire round trip");
            require(nlohmann::json(*decoded.value) == nlohmann::json(error),
                    "registry wire changed payload");
        }
    }
}

int main()
{
    try
    {
        schema_round_trip();
        native_exception_details();
        result_and_config();
        io_failure_details();
        malformed_registry_payload();
        std::cout << "Sandbox error schema tests passed\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
