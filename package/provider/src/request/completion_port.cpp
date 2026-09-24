#include "requests.h"
#include "completion_detail.h"

#if defined(_WIN32)

#include "platform/windows/request/completion.h"

namespace provider
{
    namespace platform_router = windows;
}

#elif defined(__linux__)

#include "platform/linux/request/completion.h"

namespace provider
{
    namespace platform_router = linux;
}

#else

#error "Unsupported operating system"

#endif

#include <stdexcept>
#include <utility>

namespace provider
{
    CompletionPort::CompletionPort()
    {
        platform_router::completion_create(handles_);
    }

    CompletionPort::~CompletionPort()
    {
        platform_router::completion_destroy(handles_);
    }

    CompletionPort::CompletionPort(CompletionPort&& other) noexcept
    {
        handles_[0] = std::exchange(other.handles_[0], 0);
        handles_[1] = std::exchange(other.handles_[1], 0);
    }

    CompletionPort& CompletionPort::operator=(CompletionPort&& other) noexcept
    {
        if (this == &other)
        {
            return *this;
        }

        platform_router::completion_destroy(handles_);
        handles_[0] = std::exchange(other.handles_[0], 0);
        handles_[1] = std::exchange(other.handles_[1], 0);
        return *this;
    }

    void CompletionPort::register_response(RawResponse* response)
    {
        if (response == nullptr)
        {
            throw std::invalid_argument("response is null");
        }

        if (response->in_flight.load(std::memory_order_acquire))
        {
            throw std::logic_error(
                "cannot register RawResponse while request is in flight");
        }

        response->completion_port =
            platform_router::completion_producer_handle(handles_);
    }

    bool CompletionPort::wait(
        Completion* completion,
        std::uint32_t timeout_ms)
    {
        if (completion == nullptr)
        {
            throw std::invalid_argument("completion is null");
        }

        detail::NativeCompletion native_completion;

        if (!platform_router::completion_wait(
                handles_,
                &native_completion,
                timeout_ms))
        {
            return false;
        }

        if (
            native_completion.type == detail::NativeCompletionType::failed &&
            native_completion.response == nullptr)
        {
            completion->type = CompletionType::failed;
            completion->response = nullptr;
            completion->offset = 0;
            completion->bytes = 0;
            completion->error = native_completion.error;
            return true;
        }

        RawResponse* response = native_completion.response;

        if (response == nullptr)
        {
            throw std::runtime_error("invalid provider completion");
        }

        completion->response = response;
        completion->error = native_completion.error;
        completion->type = native_completion.type ==
                detail::NativeCompletionType::finished
            ? CompletionType::finished
            : native_completion.type == detail::NativeCompletionType::failed
                ? CompletionType::failed
                : CompletionType::data;

        if (completion->type != CompletionType::data)
        {
            completion->offset = response->buffer.size();
            completion->bytes = 0;
            return true;
        }

        if (native_completion.bytes > response->buffer.size())
        {
            throw std::runtime_error("invalid provider completion");
        }

        completion->offset =
            response->buffer.size() - native_completion.bytes;
        completion->bytes = native_completion.bytes;
        return true;
    }
}
