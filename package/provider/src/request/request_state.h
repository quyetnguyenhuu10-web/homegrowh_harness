#pragma once

#include "requests.h"
#include "sse.h"

#include <deque>
#include <optional>
#include <system_error>
#include <vector>

namespace provider
{
    namespace
    {
        class RequestState
        {
        public:
            RequestUsage send(
                const std::string& url,
                const std::string& api_key,
                const nlohmann::json& body,
                RawResponse* response,
                std::uintptr_t completion_port,
                Provider provider)
            {
                response_ = response;
                completion_port_ = completion_port;
                provider_ = provider;
                deltas_.clear();
                event_sizes_.clear();
                usage_.reset();
                error_ = 0;

                if (response_ != nullptr)
                {
                    response_->buffer.clear();
                    response_->readable.store(false, std::memory_order_release);
                }

                sse_.post(
                    url,
                    api_key,
                    body,
                    this,
                    &RequestState::receive_event,
                    &error_);

                /*
                 * Only a successful transport/SSE run reaches this drain.
                 * Failure is not recovered here: the request fails as-is.
                 */
                while (!deltas_.empty())
                {
                    publish_front();

                    if (!deltas_.empty())
                    {
                        response_->readable.wait(
                            true,
                            std::memory_order_acquire);
                    }
                }

                if (!usage_.has_value())
                {
                    return UsageState::unavailable;
                }

                return parse_usage(provider_, *usage_);
            }

        private:
            static void receive_event(
                void* context,
                std::string&& event)
            {
                auto* self = static_cast<RequestState*>(context);

                const nlohmann::json payload = nlohmann::json::parse(
                    event,
                    nullptr,
                    false);

                if (!payload.is_discarded())
                {
                    if (auto usage = usage_from_event(
                            self->provider_, payload))
                    {
                        self->usage_ = std::move(*usage);
                    }
                }

                self->deltas_.emplace_back(std::move(event));
                self->publish_front();
            }

            void publish_front()
            {
                if (response_ == nullptr)
                {
                    deltas_.clear();
                    return;
                }

                if (
                    response_->readable.load(std::memory_order_acquire) ||
                    deltas_.empty())
                {
                    return;
                }

                const std::size_t bytes = deltas_.front().size();
                response_->buffer.append(deltas_.front());
                event_sizes_.push_back(bytes);

                /*
                 * The public buffer becomes the long-lived owner after
                 * publish. Destroy the consumed internal string immediately
                 * while keeping queue pop O(1) with deque::pop_front().
                 */
                deltas_.pop_front();

                response_->readable.store(true, std::memory_order_release);

                try
                {
                    platform_router::completion_post_data(
                        completion_port_,
                        response_,
                        bytes);
                }
                catch (const std::system_error& error)
                {
                    error_ = static_cast<std::uint32_t>(
                        error.code().value());

                    response_->readable.store(false, std::memory_order_release);
                    response_->readable.notify_one();
                    throw;
                }
                catch (...)
                {
                    response_->readable.store(false, std::memory_order_release);
                    response_->readable.notify_one();
                    throw;
                }
            }

            SSE sse_;
            RawResponse* response_ = nullptr;
            std::uintptr_t completion_port_ = 0;
            std::deque<std::string> deltas_;
            Provider provider_ = Provider::openai;
            std::vector<std::size_t> event_sizes_;
            std::optional<nlohmann::json> usage_;
            std::uint32_t error_ = 0;

        public:
            std::vector<std::size_t> take_event_sizes()
            {
                return std::move(event_sizes_);
            }

            std::uint32_t error() const noexcept
            {
                return error_;
            }
        };
    }
}
