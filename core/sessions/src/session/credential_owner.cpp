#include "credential_owner.h"

#include <secrets>

#include <array>
#include <cstdint>
#include <random>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace sessions::detail
{
    namespace
    {
        void secure_zero(void* memory, std::size_t bytes) noexcept
        {
            if (memory == nullptr || bytes == 0)
                return;

#if defined(_WIN32)
            SecureZeroMemory(memory, bytes);
#else
            volatile unsigned char* current =
                static_cast<volatile unsigned char*>(memory);
            while (bytes-- != 0)
                *current++ = 0;
#endif
        }

        void wipe_string(std::string& value) noexcept
        {
            if (!value.empty())
                secure_zero(value.data(), value.size());

            std::string empty;
            value.swap(empty);
        }

        std::string session_signature()
        {
            constexpr char hex[] = "0123456789abcdef";
            std::random_device random;

            std::array<std::uint32_t, 4> words{};
            for (std::uint32_t& word : words)
                word = random();

            std::string result = "HomegrowphHarness/Session/";
            result.reserve(result.size() + words.size() * 8);

            for (const std::uint32_t word : words)
            {
                for (int shift = 28; shift >= 0; shift -= 4)
                {
                    result.push_back(hex[(word >> shift) & 0x0Fu]);
                }
            }

            return result;
        }

        [[noreturn]] void throw_secret_error(
            const secrets::SecretOperationResult& result,
            const char* fallback_operation)
        {
            const std::string operation = result.error.operation.empty()
                ? fallback_operation
                : result.error.operation;

            if (result.error.code != 0)
            {
                throw std::system_error(
                    static_cast<int>(result.error.code),
                    std::system_category(),
                    operation);
            }

            throw std::runtime_error(operation + " failed");
        }
    }

    CredentialOwner::CredentialOwner(std::string signature) noexcept
        : signature_(std::move(signature))
    {
    }

    CredentialOwner::CredentialOwner(CredentialOwner&& other) noexcept
        : signature_(std::move(other.signature_)),
          closed_(std::exchange(other.closed_, true)),
          cleanup_attempted_(std::exchange(other.cleanup_attempted_, true)),
          secondary_cleanup_error_(std::move(other.secondary_cleanup_error_))
    {
    }

    CredentialOwner::~CredentialOwner() noexcept
    {
        if (closed_ || cleanup_attempted_ || signature_.empty())
            return;

        /*
         * Destruction is the exception-unwind fallback. Credential cleanup must
         * never replace a primary exception with a secondary cleanup failure.
         * Normal and known exception paths explicitly attempt cleanup before
         * destruction. This remains only a last-resort fallback.
         */
        try
        {
            (void)secrets::erase_session(signature_);
        }
        catch (...)
        {
        }
    }

    const std::string& CredentialOwner::signature() const noexcept
    {
        return signature_;
    }

    secrets::SecretOperationResult CredentialOwner::close()
    {
        if (closed_ || signature_.empty())
        {
            return {
                secrets::SecretStatus::success,
                {}};
        }

        cleanup_attempted_ = true;

        secrets::SecretOperationResult result =
            secrets::erase_session(signature_);

        if (
            result.status == secrets::SecretStatus::success ||
            result.status == secrets::SecretStatus::not_found)
        {
            closed_ = true;
        }

        return result;
    }

    void CredentialOwner::close_after_primary_error() noexcept
    {
        if (closed_ || cleanup_attempted_ || signature_.empty())
            return;

        cleanup_attempted_ = true;

        try
        {
            secrets::SecretOperationResult result =
                secrets::erase_session(signature_);

            if (
                result.status == secrets::SecretStatus::success ||
                result.status == secrets::SecretStatus::not_found)
            {
                closed_ = true;
                return;
            }

            secondary_cleanup_error_.emplace(
                CredentialSecondaryCleanupError{
                    std::move(result),
                    {}});
        }
        catch (...)
        {
            secondary_cleanup_error_.emplace(
                CredentialSecondaryCleanupError{
                    std::nullopt,
                    std::current_exception()});
        }
    }

    const CredentialSecondaryCleanupError*
    CredentialOwner::secondary_cleanup_error() const noexcept
    {
        return secondary_cleanup_error_.has_value()
            ? &*secondary_cleanup_error_
            : nullptr;
    }

    CredentialOwner persist_session_credential(std::string& raw_api_key)
    {
        if (raw_api_key.empty())
            throw std::invalid_argument("raw api key must not be empty");

        const std::string signature = session_signature();
        const secrets::SecretOperationResult stored =
            secrets::set_session(signature, raw_api_key);

        wipe_string(raw_api_key);

        if (stored.status != secrets::SecretStatus::success)
            throw_secret_error(stored, "store session credential");

        return CredentialOwner(signature);
    }
}
