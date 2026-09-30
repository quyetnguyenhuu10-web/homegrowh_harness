#include "credential_owner.h"
#include "error/error.h"

#include <array>
#include <cstdint>
#include <random>
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
        void wipe_string(std::string& value) noexcept
        {
            if (!value.empty())
            {
#if defined(_WIN32)
                SecureZeroMemory(value.data(), value.size());
#else
                volatile unsigned char* current =
                    reinterpret_cast<volatile unsigned char*>(value.data());
                std::size_t bytes = value.size();
                while (bytes-- != 0)
                    *current++ = 0;
#endif
            }
            std::string empty;
            value.swap(empty);
        }

        struct KeyWiper final
        {
            std::string& value;
            ~KeyWiper() noexcept { wipe_string(value); }
        };

        secrets::Result<std::string> session_signature()
        {
            try
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
                        result.push_back(hex[(word >> shift) & 0x0Fu]);
                }
                return secrets::Result<std::string>::success(std::move(result));
            }
            catch (const std::exception& exception)
            {
                return secrets::Result<std::string>::failure(
                    exception_error("create_credential_signature", exception));
            }
            catch (...)
            {
                return secrets::Result<std::string>::failure(
                    Error{"sessions", "create_credential_signature", "unknown_exception", "", {}, {}});
            }
        }
    }

    CredentialOwner::CredentialOwner(std::string&& signature) noexcept
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
        close_after_primary_error();
    }

    const std::string& CredentialOwner::signature() const noexcept
    {
        return signature_;
    }

    secrets::SecretOperationResult CredentialOwner::close()
    {
        if (closed_ || signature_.empty())
            return secrets::SecretOperationResult::success(std::monostate{});
        cleanup_attempted_ = true;
        auto result = secrets::erase_session(signature_);
        if (!result.error || result.error->type == "not_found")
        {
            closed_ = true;
            return secrets::SecretOperationResult::success(std::monostate{});
        }
        return secrets::SecretOperationResult::failure(dependency_error(
            "close_session_credential", "Session credential could not be erased",
            std::move(*result.error)));
    }

    void CredentialOwner::close_after_primary_error() noexcept
    {
        if (closed_ || cleanup_attempted_ || signature_.empty())
            return;
        try
        {
            auto result = close();
            if (result.error)
                secondary_cleanup_error_.emplace(std::move(*result.error));
        }
        catch (const std::exception& exception)
        {
            secondary_cleanup_error_.emplace(
                exception_error("close_session_credential", exception));
        }
        catch (...)
        {
            secondary_cleanup_error_.emplace(
                Error{"sessions", "close_session_credential", "unknown_exception", "", {}, {}});
        }
    }

    const Error* CredentialOwner::secondary_cleanup_error() const noexcept
    {
        return secondary_cleanup_error_ ? &*secondary_cleanup_error_ : nullptr;
    }

    secrets::Result<CredentialOwner> persist_session_credential(std::string& raw_api_key)
    {
        KeyWiper wipe{raw_api_key};
        try
        {
            if (raw_api_key.empty())
            {
                return secrets::Result<CredentialOwner>::failure(Error{
                    "sessions", "persist_session_credential", "validation_error",
                    "Raw API key must not be empty", {{{"field", "raw_api_key"}}}, {}});
            }
            auto signature = session_signature();
            if (signature.error)
            {
                return secrets::Result<CredentialOwner>::failure(dependency_error(
                    "persist_session_credential", "Session credential signature could not be created",
                    std::move(*signature.error)));
            }
            auto stored = secrets::set_session(*signature.value, raw_api_key);
            if (stored.error)
            {
                return secrets::Result<CredentialOwner>::failure(dependency_error(
                    "persist_session_credential", "Session credential could not be stored",
                    std::move(*stored.error)));
            }
            return secrets::Result<CredentialOwner>::success(
                CredentialOwner(std::move(*signature.value)));
        }
        catch (const std::exception& exception)
        {
            return secrets::Result<CredentialOwner>::failure(
                exception_error("persist_session_credential", exception));
        }
        catch (...)
        {
            return secrets::Result<CredentialOwner>::failure(
                Error{"sessions", "persist_session_credential", "unknown_exception", "", {}, {}});
        }
    }
}
