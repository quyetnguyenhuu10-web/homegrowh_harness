#include "stream.h"

#include <optional>
#include <ostream>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace provider
{
    namespace
    {
        void write_content(
            std::ostream& output,
            const std::string_view event)
        {
            const nlohmann::json payload = nlohmann::json::parse(
                event,
                nullptr,
                false);

            if (payload.is_discarded())
            {
                return;
            }

            const auto choices = payload.find("choices");
            if (choices == payload.end() || !choices->is_array())
            {
                return;
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
                if (content == delta->end() || !content->is_string())
                {
                    continue;
                }

                output << content->get_ref<const std::string&>();
                wrote = true;
            }

            if (wrote)
            {
                output.flush();
            }
        }
    }

    struct Stream::Impl
    {
        Impl(
            Provider provider_value,
            const std::string& url_value,
            const std::string& api_key_value,
            const nlohmann::json& body_value)
            : provider(provider_value),
              url(url_value),
              api_key(api_key_value),
              body(body_value)
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
        Provider provider,
        const std::string& url,
        const std::string& api_key,
        const nlohmann::json& body)
        : impl_(std::make_unique<Impl>(provider, url, api_key, body))
    {
    }

    Stream::~Stream() = default;

    Stream::Stream(Stream&&) noexcept = default;

    Stream& Stream::operator=(Stream&&) noexcept = default;

    const RequestUsage& Stream::usage() const
    {
        if (impl_ == nullptr || !impl_->usage.has_value())
        {
            throw std::logic_error("provider stream usage is not available");
        }

        return *impl_->usage;
    }

    std::ostream& operator<<(std::ostream& output, Stream& stream)
    {
        if (stream.impl_ == nullptr)
        {
            throw std::logic_error("provider stream has been moved");
        }

        Stream::Impl& impl = *stream.impl_;

        if (impl.consumed)
        {
            throw std::logic_error("provider stream has already been consumed");
        }

        impl.consumed = true;

        struct OutputContext
        {
            std::ostream* output;
        } context{&output};

        impl.usage = request(
            impl.provider,
            impl.url,
            impl.api_key,
            impl.body,
            EventSink{
                &context,
                [](void* raw_context, std::string&& event)
                {
                    auto* output_context =
                        static_cast<OutputContext*>(raw_context);
                    write_content(*output_context->output, event);
                },
                nullptr
            });

        if (!impl.usage.has_value())
        {
            throw std::runtime_error(
                "provider stream completed without usage");
        }

        return output;
    }
}
