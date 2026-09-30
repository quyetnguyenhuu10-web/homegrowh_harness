#include <provider>
#include "../src/error/capture.h"

#include <iostream>
#include <memory>
#include <sstream>

namespace
{
    using Json = nlohmann::json;
    using provider::Error;
    using provider::Result;
    using provider::Provider;

    int failures = 0;

    void check(bool condition, const char* message)
    {
        if (!condition)
        {
            ++failures;
            std::cerr << message << '\n';
        }
    }

    Json encode(const Error& error)
    {
        auto encoded = provider::serialize_error(error);
        check(static_cast<bool>(encoded), "Error serialization failed");
        return encoded ? std::move(*encoded.value) : Json{};
    }

    template <typename T>
    const Error* failure(const Result<T>& result, const char* message)
    {
        check(!result && !result.value && result.error.has_value(), message);
        if (!result.error)
        {
            return nullptr;
        }
        const Json envelope = encode(*result.error);
        check(envelope.is_object() && envelope.size() == 6,
            "Error envelope must contain six fields");
        check(envelope.at("data").is_array() && envelope.at("causes").is_array(),
            "Error data and causes must be arrays");
        return &*result.error;
    }

    bool has_data(const Error& error, const char* key, const Json& value)
    {
        for (const Json& item : error.data)
        {
            if (item.is_object() && item.contains(key) && item.at(key) == value)
            {
                return true;
            }
        }
        return false;
    }

    Error original()
    {
        return Error{"plugin_loader", "invoke", "system_error", "native failure",
            {Json{{"code", 5}, {"api", "native_api"}, {"path", "D:\\input"},
                  {"stack", Json::array({"frame"})}},
             nullptr, true, 42, "detail", Json::array({1, 2})},
            {Error{"sandbox", "grant", "system_error", "denied",
                {Json{{"code", 13}, {"category", "errno"}}}, {}}}};
    }

    void schema_tests()
    {
        const Error error = original();
        const Json encoded = encode(error);
        auto decoded = provider::deserialize_error(encoded);
        check(static_cast<bool>(decoded), "Valid error must deserialize");
        if (decoded)
        {
            check(encode(*decoded.value) == encoded, "Nested error payload was lost");
        }
        for (const char* field : {"source", "operation", "type", "message", "data", "causes"})
        {
            Json missing = encoded;
            missing.erase(field);
            auto result = provider::deserialize_error(missing);
            failure(result, "Missing error field must be rejected");
        }
        for (const char* field : {"data", "causes"})
        {
            Json invalid = encoded;
            invalid[field] = Json::object();
            auto result = provider::deserialize_error(invalid);
            failure(result, "Error arrays must reject objects");
        }
        Json extra = encoded;
        extra["code"] = 5;
        auto rejected = provider::deserialize_error(extra);
        failure(rejected, "Top-level code must be rejected");

        Json invalid_children = encoded;
        invalid_children["causes"] = Json::array({nullptr, false});
        auto children = provider::deserialize_error(invalid_children);
        if (const auto* invalid = failure(children, "Invalid child errors must be rejected"))
        {
            check(invalid->causes.size() == 2, "Independent schema errors were merged");
        }
        auto successful = Result<std::unique_ptr<int>>::success(std::make_unique<int>(7));
        check(successful && !successful.error && **successful.value == 7,
            "Result success must retain move-only values");
        auto failed = Result<int>::failure(original());
        failure(failed, "Result failure must have only its error branch");
        auto done = Result<void>::success();
        check(done && done.value && !done.error, "Void result has invalid branches");

        auto unsupported = provider::provider_from_name("unknown_provider");
        if (const auto* invalid = failure(unsupported, "Unknown provider must return Error"))
        {
            check(has_data(*invalid, "provider", "unknown_provider"), "Provider id was lost");
        }
        auto invalid_usage = provider::parse_usage(Provider::openai, Json{{"total_tokens", "bad"}});
        if (const auto* invalid = failure(invalid_usage, "Malformed usage must return Error"))
        {
            check(invalid->type == "protocol_error", "Usage error has wrong semantic type");
            check(has_data(*invalid, "code", 403), "JSON exception id was lost");
            check(has_data(*invalid, "usage", Json{{"total_tokens", "bad"}}), "Usage payload was lost");
        }
        auto invalid_provider = provider::parse_usage(static_cast<Provider>(999), Json::object());
        failure(invalid_provider, "Invalid provider enum must return Error");
        auto absent = provider::usage_from_event(Provider::openai, Json::object());
        check(absent && !absent.value->has_value(), "Absent usage is a successful absence");
        auto invalid_event = provider::usage_from_event(Provider::openai, Json::array());
        failure(invalid_event, "Non-object event must return Error");
    }

    Json request_body()
    {
        return Json{{"model", "fixture-model"}, {"stream", true},
            {"messages", Json::array()}};
    }

    void request_tests(const std::string& base)
    {
        const Json body = request_body();
        int events = 0;
        int finished = 0;
        struct Counters { int* events; int* finished; } counters{&events, &finished};
        const provider::EventSink sink{
            &counters,
            [](void* raw, std::string&&)
            {
                ++*static_cast<Counters*>(raw)->events;
                return Result<void>::success();
            },
            [](void* raw)
            {
                ++*static_cast<Counters*>(raw)->finished;
                return Result<void>::success();
            }};

        auto http = provider::request(Provider::openai, base + "/http", "", body, sink);
        if (const auto* error = failure(http, "HTTP 429 must return Error"))
        {
            check(error->source == "provider" && error->operation == "request"
                && error->type == "http_error", "HTTP error identity changed");
            check(has_data(*error, "status_code", 429), "HTTP status was lost");
            check(has_data(*error, "status_line", "HTTP/1.1 429 Too Many Requests"),
                "HTTP status line was lost");
            check(has_data(*error, "reason", "Too Many Requests"), "HTTP reason was lost");
            check(has_data(*error, "body", "{\"error\":{\"code\":\"rate_limit\",\"message\":\"slow down\"}}"),
                "HTTP body was lost");
            check(error->causes.empty(), "Forwarded HTTP error acquired redundant wrappers");
        }
        check(events == 0 && finished == 0, "HTTP failure must not dispatch successful callbacks");

        auto transport = provider::request(Provider::openai, "http://", "", body);
        if (const auto* error = failure(transport, "Transport failure must return Error"))
        {
            check(has_data(*error, "category", "libcurl") && has_data(*error, "code", 3),
                "Original CURLcode was lost");
            check(has_data(*error, "category", "cpr"), "CPR error payload was lost");
        }
        auto partial = provider::request(Provider::openai, base + "/partial", "", body);
        if (const auto* error = failure(partial, "Partial HTTP failure must return Error"))
        {
            check(error->type == "http_error" && !error->causes.empty(),
                "HTTP and transport errors must both survive");
            if (!error->causes.empty())
            {
                check(has_data(error->causes.front(), "code", 18),
                    "Partial response CURLcode was lost");
            }
        }
        auto malformed = provider::request(Provider::openai, base + "/invalid_json", "", body);
        if (const auto* error = failure(malformed, "Malformed SSE JSON must return Error"))
        {
            check(error->operation == "receive_event" && has_data(*error, "code", 101),
                "SSE parse error was replaced by callback cancellation");
            check(has_data(*error, "event", "{invalid"), "Original SSE event was lost");
        }
        auto malformed_usage = provider::request(Provider::openai, base + "/invalid_usage", "", body);
        if (const auto* error = failure(malformed_usage, "Malformed SSE usage must return Error"))
        {
            check(error->operation == "parse_usage" && has_data(*error, "code", 302),
                "Usage parser native error was lost");
        }
        auto native = provider::request(Provider::openai, base + "/error_event", "", body);
        if (const auto* error = failure(native, "Provider SSE error must return Error"))
        {
            check(error->type == "provider_error" && error->message == "quota exceeded",
                "Provider event identity was lost");
            check(error->data.front().at("event").at("error").at("code") == "quota",
                "Provider event code was lost");
        }
        auto unified = provider::request(Provider::openai, base + "/unified_error", "", body);
        if (const auto* error = failure(unified, "Unified SSE error must be forwarded"))
        {
            check(error->source == "remote" && error->operation == "invoke"
                && error->data.front().at("path") == "/native/path",
                "Unified event identity or path was replaced");
        }

        Error callback_error = original();
        const provider::EventSink failing_sink{
            &callback_error,
            [](void* raw, std::string&&)
            {
                return Result<void>::failure(Error{*static_cast<Error*>(raw)});
            }, nullptr};
        auto callback = provider::request(Provider::openai, base + "/ok", "", body, failing_sink);
        if (const auto* error = failure(callback, "Callback Error must propagate"))
        {
            check(encode(*error) == encode(callback_error), "Callback Error was flattened or wrapped");
        }
        auto native_callback = provider::request(Provider::openai, base + "/ok", "", body,
            provider::EventSink{nullptr,
                [](void*, std::string&&)
                {
                    (void)Json::parse("{invalid"); // Exercise a native JSON exception.
                    return Result<void>::success();
                }, nullptr});
        if (const auto* error = failure(native_callback, "Native callback exception must normalize"))
        {
            check(error->operation == "dispatch_event" && has_data(*error, "code", 101),
                "Native callback exception id was lost");
        }
        auto finish_error = provider::request(Provider::openai, base + "/ok", "", body,
            provider::EventSink{&callback_error, nullptr,
                [](void* raw)
                {
                    return Result<void>::failure(Error{*static_cast<Error*>(raw)});
                }});
        if (const auto* error = failure(finish_error, "Completion Error must propagate"))
        {
            check(encode(*error) == encode(callback_error), "Completion error identity changed");
        }
        auto valid = provider::request(Provider::openai, base + "/ok", "", body, sink);
        check(valid && finished == 1 && events == 3, "Successful request lifecycle changed");
        if (valid)
        {
            check(std::get<provider::OpenAIUsage>(*valid.value).total_tokens == 5,
                "Successful usage changed");
        }
        auto no_usage = provider::request(Provider::openai, base + "/without_usage", "", body);
        check(no_usage && std::holds_alternative<provider::UsageState>(*no_usage.value),
            "Unavailable usage must remain a successful request");
        auto bad_model = provider::request(Provider::openai, base + "/ok", "", Json{{"model", 42}});
        if (const auto* error = failure(bad_model, "Invalid model type must return Error"))
        {
            check(has_data(*error, "code", 303), "Request JSON exception id was lost");
            check(has_data(*error, "body", Json{{"model", 42}}), "Invalid request body was lost");
        }
    }

    void compaction_tests(const std::string& base)
    {
        const Json current = {{"messages", Json::array({{{"role", "user"}, {"content", "continue"}}})}};
        const Json history = Json::array({{{"role", "user"}, {"content", "history"}}});
        for (const bool compact : {false, true})
        {
            auto result = provider::compaction(Provider::openai, base + "/http",
                "fixture-model", "", "summarize", current, Json::array(), history, {}, compact);
            if (const auto* error = failure(result, "Compaction request failure must return Error"))
            {
                check(error->operation == "compaction" && error->type == "dependency_error"
                    && error->causes.size() == 1, "Compaction context has invalid causes");
                if (!error->causes.empty())
                {
                    check(error->causes.front().type == "http_error"
                        && has_data(error->causes.front(), "status_code", 429),
                        "Compaction lost original HTTP error");
                }
                check(has_data(*error, "phase", compact ? "summary_request" : "response_request"),
                    "Compaction failure phase was lost");
            }
        }
        for (const char* path : {"/without_usage", "/empty"})
        {
            auto invalid_summary = provider::compaction(Provider::openai, base + path,
                "fixture-model", "", "summarize", current, Json::array(), history, {}, true);
            failure(invalid_summary, "Incomplete summary must return Error");
        }
        auto valid = provider::compaction(Provider::openai, base + "/ok",
            "fixture-model", "", "summarize", current, Json::array(), history, {}, true);
        check(valid && valid.value->messages.size() == 2, "Compaction success changed");
        auto bad_history = provider::compaction(Provider::openai, base + "/ok",
            "fixture-model", "", "summarize", current, Json::array(),
            Json::array({Json::object()}), {}, true);
        if (const auto* error = failure(bad_history, "Transcript failure must return Error"))
        {
            check(error->causes.size() == 1
                && has_data(error->causes.front(), "code", 403),
                "Transcript parser exception was lost");
        }
    }

    class FailingBuffer final : public std::streambuf
    {
        std::streamsize xsputn(const char*, std::streamsize) override { return 0; }
        int_type overflow(int_type) override { return traits_type::eof(); }
    };

    void stream_tests(const std::string& base)
    {
        const Json body = request_body();
        std::ostringstream output;
        provider::Stream stream(Provider::openai, base + "/ok", "", body);
        failure(stream.usage(), "Stream usage before consumption must return Error");
        auto written = stream.write(output);
        check(written && output.str() == "summary text", "Stream output changed");
        auto usage = stream.usage();
        check(usage && std::get<provider::OpenAIUsage>(usage.value->get()).total_tokens == 5,
            "Stream usage changed");
        failure(stream.write(output), "Repeated stream consumption must return Error");
        provider::Stream source(Provider::openai, base + "/ok", "", body);
        provider::Stream moved(std::move(source));
        failure(source.write(output), "Moved stream must return Error");
        check(static_cast<bool>(moved.write(output)), "Moved-to stream must still work");

        provider::Stream failed(Provider::openai, base + "/http", "", body);
        auto failed_write = failed.write(output);
        if (const auto* error = failure(failed_write, "Stream HTTP failure must return Error"))
        {
            check(failed.error() && encode(*failed.error()) == encode(*error),
                "Stream lost inspectable error");
            check(error->type == "http_error", "Stream wrapped HTTP failure without context");
        }
        provider::Stream invalid(Provider::openai, base + "/ok", "", Json::array());
        failure(invalid.write(output), "Stream construction error must be normalized");

        FailingBuffer buffer;
        std::ostream bad_output(&buffer);
        bad_output.exceptions(std::ios::badbit);
        provider::Stream bad(Provider::openai, base + "/ok", "", body);
        auto io_error = bad.write(bad_output);
        if (const auto* error = failure(io_error, "Output exception must return Error"))
        {
            check(error->operation == "write_stream" && has_data(*error, "category", "iostream"),
                "Output error category was lost");
        }
        std::ostringstream insertion_output;
        provider::Stream insertion(Provider::openai, base + "/http", "", body);
        insertion_output << insertion;
        check(insertion_output.fail() && insertion.error()
            && insertion.error()->type == "http_error", "Stream insertion lost failure");

        std::ostringstream exception_output;
        exception_output.exceptions(std::ios::failbit);
        provider::Stream exception_stream(Provider::openai, base + "/http", "", body);
        exception_output << exception_stream;
        check(exception_stream.error() && exception_stream.error()->causes.size() == 2,
            "Request and output state errors must remain distinct causes");
        if (exception_stream.error() && exception_stream.error()->causes.size() == 2)
        {
            check(exception_stream.error()->causes[0].type == "http_error"
                && has_data(exception_stream.error()->causes[0], "status_code", 429),
                "Original request error was replaced by output state exception");
            check(has_data(exception_stream.error()->causes[1], "category", "iostream"),
                "Original output state exception was lost");
        }
    }
}

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::cerr << "Expected local fixture URL\n";
        return 2;
    }
    try
    {
        schema_tests();
        request_tests(argv[1]);
        compaction_tests(argv[1]);
        stream_tests(argv[1]);
    }
    catch (...)
    {
        std::cerr << encode(provider::error_detail::capture_exception(
            std::current_exception(), "test_provider_errors")).dump() << '\n';
        return 1;
    }
    if (failures == 0)
    {
        std::cout << "Provider error schema, request, compaction and stream tests passed\n";
    }
    return failures == 0 ? 0 : 1;
}
