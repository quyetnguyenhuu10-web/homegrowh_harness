#include <event_port>

#include "detail/registration_state.h"

#include <utility>

namespace event_port
{
    Registration::Registration(
        std::shared_ptr<detail::RegistrationState>&& state) noexcept
        : state_(std::move(state))
    {
    }

    Registration::~Registration()
    {
        release();
    }

    Registration::Registration(Registration&& other) noexcept
        : state_(std::move(other.state_)),
          cleanup_error_(std::move(other.cleanup_error_))
    {
        other.cleanup_error_.reset();
    }

    Registration& Registration::operator=(Registration&& other) noexcept
    {
        if (this == &other)
        {
            return *this;
        }

        release();
        state_ = std::move(other.state_);
        if (other.cleanup_error_)
            record_cleanup_error(std::move(*other.cleanup_error_));
        other.cleanup_error_.reset();
        return *this;
    }

    const Error* Registration::cleanup_error() const noexcept
    {
        return cleanup_error_ ? &*cleanup_error_ : nullptr;
    }

    void Registration::release() noexcept
    {
        if (state_ == nullptr)
            return;

        auto result = detail::close_registration_state(*state_, true);
        if (result.error)
            record_cleanup_error(std::move(*result.error));
        state_.reset();
    }

    void Registration::record_cleanup_error(Error&& error) noexcept
    {
        if (!cleanup_error_)
        {
            cleanup_error_.emplace(std::move(error));
            return;
        }

        std::vector<Error> causes;
        causes.emplace_back(std::move(*cleanup_error_));
        causes.emplace_back(std::move(error));
        cleanup_error_.emplace(Error{
            "event_port", "release_registration", "cleanup_error",
            "Multiple registration cleanup operations failed", {}, std::move(causes)});
    }
}
