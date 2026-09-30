#include "stream.h"
#include "error/capture.h"

#include <ostream>

namespace provider
{
    namespace
    {
        Result<void> write_content(std::ostream& output, std::string_view event)
        {
            try
            {
                if (event == "[DONE]")
                {
                    return Result<void>::success();
                }
                const nlohmann::json payload = nlohmann::json::parse(event);
                const auto choices = payload.find("choices");
                if (choices == payload.end() || !choices->is_array())
                {
                    return Result<void>::success();
                }
                bool wrote = false;
                for (const nlohmann::json& choice : *choices)
                {
                    const auto delta = choice.find("delta");
                    if (delta == choice.end() || !delta->is_object())
                    {
                        continue;
                    }
                    const auto content = delta->find("content");
                    if (content != delta->end() && content->is_string())
                    {
                        output << content->get_ref<const std::string&>();
                        wrote = true;
                    }
                }
                if (wrote)
                {
                    output.flush();
                }
                if (!output)
                {
                    return Result<void>::failure(error_detail::make_error(
                        "write_stream", "system_error", "Output stream reports a failure",
                        {{{"api", "std::ostream"}, {"event", event},
                          {"stream_state", static_cast<int>(output.rdstate())}}}));
                }
                return Result<void>::success();
            }
            catch (...)
            {
                return Result<void>::failure(error_detail::capture_exception(
                    std::current_exception(), "write_stream",
                    {{{"api", "std::ostream"}, {"event", event},
                      {"stream_state", static_cast<int>(output.rdstate())}}}));
            }
        }
    }

    struct Stream::Impl
    {
        Impl(
            Provider selected, const std::string& endpoint,
            const std::string& key, const nlohmann::json& request_body)
            : provider(selected), url(endpoint), api_key(key), body(request_body)
        {
            body["stream"] = true;
        }

        Provider provider;
        std::string url;
        std::string api_key;
        nlohmann::json body;
        std::optional<RequestUsage> usage;
        bool consumed = false;
    };

    Stream::Stream(
        Provider selected, const std::string& url,
        const std::string& api_key, const nlohmann::json& body)
    {
        try
        {
            impl_ = std::make_unique<Impl>(selected, url, api_key, body);
        }
        catch (...)
        {
            error_ = error_detail::capture_exception(
                std::current_exception(), "create_stream",
                {{{"url", url}, {"body", body}}});
        }
    }

    Stream::~Stream() = default;

    Stream::Stream(Stream&& other) noexcept
        : impl_(std::move(other.impl_)), error_(std::move(other.error_)),
          moved_(other.moved_)
    {
        other.moved_ = true;
        other.error_.reset();
    }

    Stream& Stream::operator=(Stream&& other) noexcept
    {
        if (this != &other)
        {
            impl_ = std::move(other.impl_);
            error_ = std::move(other.error_);
            moved_ = other.moved_;
            other.moved_ = true;
            other.error_.reset();
        }
        return *this;
    }

    const std::optional<Error>& Stream::error() const noexcept
    {
        return error_;
    }

    Result<std::reference_wrapper<const RequestUsage>> Stream::usage() const
    {
        using UsageResult = Result<std::reference_wrapper<const RequestUsage>>;
        if (moved_)
        {
            return UsageResult::failure(error_detail::make_error(
                "stream_usage", "invalid_state", "Provider stream has been moved"));
        }
        if (error_.has_value())
        {
            return UsageResult::failure(Error{*error_});
        }
        if (impl_ == nullptr || !impl_->usage.has_value())
        {
            return UsageResult::failure(error_detail::make_error(
                "stream_usage", "invalid_state", "Provider stream usage is not available"));
        }
        return UsageResult::success(std::cref(*impl_->usage));
    }

    Result<void> Stream::write(std::ostream& output)
    {
        const auto fail = [&](Error&& error)
        {
            error_ = std::move(error);
            // The caller and the stream each retain an inspectable error.
            return Result<void>::failure(Error{*error_});
        };
        try
        {
            if (moved_)
            {
                return fail(error_detail::make_error(
                    "write_stream", "invalid_state", "Provider stream has been moved"));
            }
            if (error_.has_value())
            {
                return Result<void>::failure(Error{*error_});
            }
            if (impl_->consumed)
            {
                return fail(error_detail::make_error(
                    "write_stream", "invalid_state",
                    "Provider stream has already been consumed"));
            }
            impl_->consumed = true;

            auto requested = request(
                impl_->provider, impl_->url, impl_->api_key, impl_->body,
                EventSink{
                    &output,
                    [](void* context, std::string&& event)
                    {
                        return write_content(*static_cast<std::ostream*>(context), event);
                    },
                    nullptr});
            if (!requested)
            {
                return fail(std::move(*requested.error));
            }
            impl_->usage = std::move(*requested.value);
            return Result<void>::success();
        }
        catch (...)
        {
            return fail(error_detail::capture_exception(
                std::current_exception(), "write_stream",
                {{{"stream_state", static_cast<int>(output.rdstate())}}}));
        }
    }

    std::ostream& operator<<(std::ostream& output, Stream& stream)
    {
        auto written = stream.write(output);
        if (!written)
        {
            try
            {
                output.setstate(std::ios::failbit);
            }
            catch (...)
            {
                Error state_error = error_detail::capture_exception(
                    std::current_exception(), "set_stream_state",
                    {{{"api", "std::ostream::setstate"},
                      {"stream_state", static_cast<int>(output.rdstate())}}});
                Error combined = error_detail::dependency_error(
                    "write_stream", "Stream failed and output rejected its failure state",
                    std::move(*stream.error_));
                combined.causes.push_back(std::move(state_error));
                stream.error_ = std::move(combined);
            }
        }
        return output;
    }
}
