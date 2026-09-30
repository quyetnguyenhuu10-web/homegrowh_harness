#include "error/error.h"
#include "platform/platform.h"
#include "process/process.h"
#include "runtime/response.h"
#include "runtime/runtime.h"

#include <cerrno>
#include <iostream>
#include <string>

#if defined(_WIN32)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

using nlohmann::json;
using namespace tool_runtime;
using namespace tool_runtime::detail;

#define CHECK(expression) do { if (!(expression)) { std::cerr << __func__ << ":" << __LINE__ << ": " << #expression << '\n'; return false; } } while (false)

namespace
{
    const json original_error = {
        {"source", "provider"}, {"operation", "request"}, {"type", "http_error"},
        {"message", "Rate limited"},
        {"data", json::array({{{"status_code", 429}, {"body", {{"retry", 10}}}}, nullptr, true, 17, "payload", json::array({1, "two"})})},
        {"causes", json::array({{
            {"source", "transport"}, {"operation", "send"}, {"type", "system_error"},
            {"message", "Connection reset"}, {"data", json::array({{{"code", 104}, {"api", "recv"}, {"path", "socket"}}})},
            {"causes", json::array()}
        }})}
    };

    bool has_payload(const Error& error, std::string_view key, const json& value)
    {
        for (const auto& item : error.data)
        {
            if (item.is_object() && item.contains(key) && item.at(key) == value)
                return true;
        }
        for (const auto& cause : error.causes)
        {
            if (has_payload(cause, key, value))
                return true;
        }
        return false;
    }

    json call(std::string_view name)
    {
        return {{"id", "test_call"}, {"type", "function"},
            {"function", {{"name", name}, {"arguments", "{}"}}}};
    }

#if defined(_WIN32)
    Result<DWORD> handle_count()
    {
        DWORD count = 0;
        if (!GetProcessHandleCount(GetCurrentProcess(), &count))
        {
            const DWORD code = GetLastError();
            Error error = make_system_error("test_handle_count", "GetProcessHandleCount",
                {static_cast<int>(code), std::system_category()});
            error.data[0]["code"] = code;
            return Result<DWORD>::failure(std::move(error));
        }
        return Result<DWORD>::success(std::move(count));
    }
#endif

    bool schema_round_trip()
    {
        auto decoded = deserialize_error(original_error);
        CHECK(decoded.value && !decoded.error);
        CHECK(json(*decoded.value) == original_error);
        CHECK(json(*decoded.value).size() == 6);
        auto malformed = original_error;
        malformed["causes"][0]["data"] = json::object();
        auto rejected = deserialize_error(malformed);
        CHECK(!rejected.value && rejected.error);
        CHECK(rejected.error->type == "protocol_error");
        CHECK(has_payload(*rejected.error, "path", "error.causes[0]"));
        malformed = original_error;
        malformed["code"] = 5;
        CHECK(deserialize_error(malformed).error.has_value());
        auto success = Result<int>::success(3);
        auto failure = Result<int>::failure(make_error("test", "validation_error", "Test failure"));
        CHECK(success.value == 3 && !success.error);
        CHECK(!failure.value && failure.error);
        return true;
    }

    bool legacy_adapter_preserves_payload()
    {
        json legacy = {{"code", "native_failure"}, {"message", "Cannot read"}, {"path", "file.txt"},
            {"stack", "original stack"}, {"body", {{"detail", 7}}}, {"cause", original_error}};
        Error adapted = adapt_error(legacy, "worker", "invoke");
        CHECK(adapted.source == "worker" && adapted.operation == "invoke");
        CHECK(adapted.message == "Cannot read");
        CHECK(has_payload(adapted, "code", "native_failure"));
        CHECK(has_payload(adapted, "path", "file.txt"));
        CHECK(has_payload(adapted, "stack", "original stack"));
        CHECK(has_payload(adapted, "body", legacy["body"]));
        CHECK(adapted.causes.size() == 1 && json(adapted.causes[0]) == original_error);
        CHECK(json(adapted).size() == 6);
        CHECK(json(adapted).at("data").is_array());
        CHECK(!json(adapted).contains("cause"));
        CHECK(json(adapt_error(original_error, "ignored", "ignored")) == original_error);
        CHECK(adapt_error("literal error", "worker", "invoke").data[0] == "literal error");
        CHECK(adapt_error(19, "worker", "invoke").data[0] == 19);
        return true;
    }

    bool diagnostics_preserve_original_failure()
    {
        auto parsed = parse_json("{bad json", "parse_input");
        CHECK(!parsed.value && parsed.error);
        CHECK(parsed.error->type == "protocol_error");
        CHECK(has_payload(*parsed.error, "id", 101));
        CHECK(has_payload(*parsed.error, "input", "{bad json"));
        CHECK(parsed.error->data[0].contains("byte"));
        const std::filesystem::filesystem_error exception("filesystem operation", "first", "second",
            std::error_code(EACCES, std::generic_category()));
        Error error = exception_error("inspect_file", exception);
        CHECK(error.message == exception.what());
        CHECK(has_payload(error, "code", EACCES));
        CHECK(has_payload(error, "category", "generic"));
        CHECK(has_payload(error, "path", "first"));
        CHECK(has_payload(error, "path2", "second"));
        std::optional<Error> independent;
        append_error(independent, make_error("first", "io_error", "First"));
        append_error(independent, make_error("second", "io_error", "Second"));
        append_error(independent, make_error("third", "io_error", "Third"));
        CHECK(independent->causes.size() == 3);
        CHECK(independent->causes[0].operation == "first");
        const std::string invalid(1, static_cast<char>(0xff));
        auto bytes = parse_json(invalid, "parse_input");
        CHECK(bytes.error && has_payload(*bytes.error, "input", {{"encoding", "bytes"}, {"bytes", json::array({255})}}));
        CHECK(!json(*bytes.error).dump().empty());
        return true;
    }

    bool worker_and_native_forward_errors()
    {
        process_result_view view;
        view.started = true;
        view.exit_code = 0;
        view.stdout_text = json{{"ok", false}, {"error", original_error}}.dump();
        auto native = normalize_tool(call("edit_file"), view);
        CHECK(native.error && json(*native.error) == original_error);
        auto worker = normalize_tool(call("read"), view);
        CHECK(worker.error && json(*worker.error) == original_error);
        view.stdout_text = json{{"ok", false}, {"error", {{"message", "Failure"}, {"code", 5}, {"api", "worker_api"}}}}.dump();
        worker = normalize_tool(call("read"), view);
        CHECK(worker.error && has_payload(*worker.error, "code", 5));
        CHECK(has_payload(*worker.error, "api", "worker_api"));
        view.stdout_text = "not json";
        view.stderr_text = "original stderr";
        worker = normalize_tool(call("read"), view);
        CHECK(worker.error && has_payload(*worker.error, "stderr", "original stderr"));
        CHECK(has_payload(*worker.error, "id", 101));
        view.stdout_text = json{{"ok", true}, {"result", {{"content", "{}"}}}, {"readFiles", json::array({3})}}.dump();
        worker = normalize_tool(call("read"), view);
        CHECK(worker.error && worker.error->operation == "merge_read_files");
        CHECK(has_payload(*worker.error, "path", "readFiles[0]"));
        view.stdout_text = json{{"ok", true}, {"result", {{"content", json{{"error", original_error}}.dump()}}},
            {"readFiles", json::array({3})}}.dump();
        worker = normalize_tool(call("read"), view);
        CHECK(worker.error && worker.error->causes.size() == 2);
        CHECK(json(worker.error->causes[0]) == original_error);
        CHECK(worker.error->causes[1].operation == "merge_read_files");
        view.stdout_text = json{{"ok", false}, {"error", original_error}}.dump();
        view.exit_code = 9;
        native = normalize_tool(call("edit_file"), view);
        CHECK(native.error && native.error->type == "dependency_error");
        CHECK(native.error->data[0]["process"]["exit_code"] == 9);
        CHECK(native.error->causes.size() == 1 && json(native.error->causes[0]) == original_error);
        view.exit_code = 0;
        view.error = make_system_error("read_process_output", "ReadFile", {5, std::system_category()});
        native = normalize_tool(call("edit_file"), view);
        CHECK(native.error && native.error->causes.size() == 2);
        CHECK(json(native.error->causes[1]) == original_error);
        view.error.reset();
        view.stdout_text = "{}";
        view.exit_code = 23;
        native = normalize_tool(call("edit_file"), view);
        CHECK(native.error && native.error->type == "process_error");
        CHECK(native.error->data[0]["process"]["exit_code"] == 23);
        return true;
    }

    bool independent_errors_survive_results()
    {
        json other = original_error;
        other["source"] = "plugin_loader";
        other["operation"] = "invoke";
        auto payload = normalize_result_payload(json{
            {"ok", false}, {"results", json::array({
                {{"ok", false}, {"error", original_error}}, {{"ok", false}, {"error", other}}
            })}}, "native", "invoke");
        CHECK(payload.error && payload.error->causes.size() == 2);
        CHECK(json(payload.error->causes[0]) == original_error);
        CHECK(json(payload.error->causes[1]) == other);
        payload = normalize_result_payload(json{
            {"ok", false}, {"error", original_error},
            {"results", json::array({{{"ok", false}, {"error", other}}})}
        }, "native", "invoke");
        CHECK(payload.error && payload.error->causes.size() == 2);
        payload = normalize_result_payload(json{
            {"results", json::array({{{"error", original_error}}, {{"error", original_error}}})}
        }, "native", "invoke");
        CHECK(payload.error && payload.error->causes.size() == 2);
        CHECK(json(payload.error->causes[0]) == original_error);
        CHECK(json(payload.error->causes[1]) == original_error);
        const json completed = {{"ok", true}, {"value", {{"updated", 1}}}};
        payload = normalize_result_payload(json{
            {"ok", false}, {"diagnostic", {{"remaining", 1}}},
            {"results", json::array({completed, {{"error", original_error}}})}
        }, "native", "invoke");
        CHECK(payload.error && payload.error->causes.size() == 1);
        CHECK(json(payload.error->causes[0]) == original_error);
        CHECK(has_payload(*payload.error, "diagnostic", {{"remaining", 1}}));
        CHECK(has_payload(*payload.error, "successful_results", json::array({{{"index", 0}, {"result", completed}}})));
        CHECK(has_payload(*payload.error, "failed_result_indexes", json::array({1})));
        return true;
    }

    bool process_failures_are_structured(const std::filesystem::path& fixture)
    {
        const auto directory = fixture.parent_path();
        auto missing = process::run(directory / "missing-tool-runtime-fixture", {}, directory, "");
        CHECK(!missing.started && missing.error);
        CHECK(missing.error->type == "system_error");
#if defined(_WIN32)
        CHECK(has_payload(*missing.error, "api", "CreateProcessW"));
        CHECK(has_payload(*missing.error, "code", ERROR_FILE_NOT_FOUND));
#else
        CHECK(has_payload(*missing.error, "api", "execv"));
        CHECK(has_payload(*missing.error, "code", ENOENT));
#endif
        CHECK(has_payload(*missing.error, "path", path_text(directory / "missing-tool-runtime-fixture")));
        process_result_view view;
        view.error = std::move(missing.error);
        auto normalized = normalize_tool(call("edit_file"), view);
        CHECK(normalized.error && normalized.error->type == "dependency_error");
        CHECK(normalized.error->causes.size() == 1);
        CHECK(normalized.error->causes[0].operation == "start_process");
        auto invalid_directory = process::run(fixture, {"echo"}, directory / "missing-directory", "");
        CHECK(!invalid_directory.started && invalid_directory.error);
        CHECK(has_payload(*invalid_directory.error, "working_directory", path_text(directory / "missing-directory")));
#if defined(_WIN32)
        auto invalid_utf8 = process::run(fixture, {std::string(1, static_cast<char>(0xff))}, directory, "");
        CHECK(invalid_utf8.error && has_payload(*invalid_utf8.error, "api", "MultiByteToWideChar"));
        CHECK(has_payload(*invalid_utf8.error, "code", ERROR_NO_UNICODE_TRANSLATION));
        CHECK(!json(*invalid_utf8.error).dump().empty());
#endif
        auto null_argument = process::run(fixture, {std::string("a\0b", 3)}, directory, "");
        CHECK(null_argument.error && null_argument.error->type == "validation_error");
        return true;
    }

    bool process_io_and_cleanup(const std::filesystem::path& fixture)
    {
        const auto directory = fixture.parent_path();
        const std::string input(256 * 1024, 'x');
        auto output = process::run(fixture, {"streams"}, directory, input);
        CHECK(output.started && !output.error && output.exit_code == 0);
        CHECK(output.stdout_text == input && output.stderr_text == input);
        auto closed = process::run(fixture, {"no-input"}, directory, input);
        CHECK(closed.started && closed.error);
        CHECK(closed.error->operation == "write_process_input");
        const std::vector<std::string> arguments{"arguments", "", "contains spaces", "quote\"inside", "trailing\\", "slash\\\"quote"};
        output = process::run(fixture, arguments, directory, "");
        CHECK(output.started && !output.error);
        CHECK(json::parse(output.stdout_text) == json(std::vector<std::string>(arguments.begin() + 1, arguments.end())));
#if defined(_WIN32)
        auto before = handle_count();
        CHECK(before.value && !before.error);
#else
        const auto before = std::distance(std::filesystem::directory_iterator("/proc/self/fd"),
            std::filesystem::directory_iterator());
#endif
        for (int index = 0; index < 25; ++index)
        {
            auto failed = process::run(directory / "missing-tool-runtime-fixture", {}, directory, "");
            CHECK(failed.error && !failed.started);
        }
#if defined(_WIN32)
        auto after = handle_count();
        CHECK(after.value && !after.error);
        CHECK(after.value == before.value);
#else
        const auto after = std::distance(std::filesystem::directory_iterator("/proc/self/fd"),
            std::filesystem::directory_iterator());
        CHECK(after == before);
#endif
        return true;
    }
}

int main(int argc, char** argv)
{
    if (argc != 2)
        return 1;
    try
    {
        int failed = 0;
        for (auto test : {schema_round_trip, legacy_adapter_preserves_payload, diagnostics_preserve_original_failure,
                worker_and_native_forward_errors, independent_errors_survive_results})
        {
            if (!test())
                ++failed;
        }
        const auto fixture = std::filesystem::u8path(argv[1]);
        if (!process_failures_are_structured(fixture)) ++failed;
        if (!process_io_and_cleanup(fixture)) ++failed;
        if (failed == 0)
            std::cout << "7 error, adapter, and process regression groups passed\n";
        return failed == 0 ? 0 : 1;
    }
    catch (...)
    {
        std::cerr << json(current_exception_error("test")).dump() << '\n';
        return 1;
    }
}
