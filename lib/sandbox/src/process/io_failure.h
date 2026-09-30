#pragma once

#include "../error_schema.h"

#include <atomic>
#include <cstddef>
#include <type_traits>

namespace sandbox::detail
{
    inline bool same_error(const Error& left, const Error& right)
    {
        if (left.source != right.source || left.operation != right.operation
            || left.type != right.type || left.message != right.message
            || left.data != right.data || left.causes.size() != right.causes.size())
            return false;
        for (std::size_t index = 0; index < left.causes.size(); ++index)
        {
            if (!same_error(left.causes[index], right.causes[index]))
                return false;
        }
        return true;
    }

    inline bool io_already_observed(
        const Error& error, const std::optional<Error>& observed)
    {
        if (!observed)
            return false;
        if (same_error(error, *observed))
            return true;
        if (observed->source == "sandbox" && observed->operation == "process_io")
        {
            for (const auto& cause : observed->causes)
            {
                if (same_error(error, cause))
                    return true;
            }
        }
        return false;
    }

    // Workers retain native facts without allocating while handling an exception.
    template<class Code>
    class io_failure final
    {
    public:
        explicit io_failure(Code code = 0) noexcept : code_(code)
        {
        }

        void store(Code code, std::memory_order order = std::memory_order_relaxed) noexcept
        {
            code_.store(code, order);
        }

        void record_no_progress(std::size_t offset, std::size_t requested) noexcept
        {
            offset_.store(offset, std::memory_order_relaxed);
            requested_.store(requested, std::memory_order_relaxed);
            no_progress_.store(true, std::memory_order_release);
        }

        void record_exception() noexcept
        {
            const auto exception = std::current_exception();
            lock_exception();
            exception_ = exception;
            exception_lock_.clear(std::memory_order_release);
        }

        [[nodiscard]] std::optional<Error> observe(
            const char* operation,
            const std::error_category& category) const
        {
            std::vector<Error> errors;
            const Code code = code_.load(std::memory_order_relaxed);
            if (code != 0)
            {
                if constexpr (std::is_unsigned_v<Code>)
                    errors.push_back(make_native_error(operation, code));
                else
                    errors.push_back(make_system_error(operation, std::error_code(code, category)));
            }
            if (no_progress_.load(std::memory_order_acquire))
            {
                errors.push_back(make_error(
                    operation, "io_error", "Write completed without making progress",
                    {{"api", error_api_name(operation)},
                     {"offset", offset_.load(std::memory_order_relaxed)},
                     {"requested", requested_.load(std::memory_order_relaxed)},
                     {"written", 0}}));
            }
            lock_exception();
            const auto exception = exception_;
            exception_lock_.clear(std::memory_order_release);
            if (exception)
                errors.push_back(capture_exception(operation, exception));

            if (errors.empty())
                return std::nullopt;
            if (errors.size() == 1)
                return std::move(errors.front());
            return make_error(
                operation, "dependency_error", "Multiple I/O failures were observed",
                nullptr, std::move(errors));
        }

    private:
        void lock_exception() const noexcept
        {
            while (exception_lock_.test_and_set(std::memory_order_acquire))
            {
            }
        }

        std::atomic<Code> code_{0};
        std::atomic<bool> no_progress_{false};
        std::atomic<std::size_t> offset_{0};
        std::atomic<std::size_t> requested_{0};
        mutable std::atomic_flag exception_lock_ = ATOMIC_FLAG_INIT;
        std::exception_ptr exception_;
    };
}
