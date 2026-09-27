#pragma once

#include <secrets>

#include <exception>
#include <optional>
#include <string>

namespace sessions::detail
{
    struct CredentialSecondaryCleanupError
    {
        std::optional<secrets::SecretOperationResult> result;
        std::exception_ptr exception;
    };

    class CredentialOwner final
    {
    public:
        explicit CredentialOwner(std::string signature) noexcept;

        CredentialOwner(const CredentialOwner&) = delete;
        CredentialOwner& operator=(const CredentialOwner&) = delete;

        CredentialOwner(CredentialOwner&& other) noexcept;
        CredentialOwner& operator=(CredentialOwner&&) = delete;

        ~CredentialOwner() noexcept;

        [[nodiscard]] const std::string& signature() const noexcept;

        [[nodiscard]] secrets::SecretOperationResult close();

        void close_after_primary_error() noexcept;

        [[nodiscard]] const CredentialSecondaryCleanupError*
            secondary_cleanup_error() const noexcept;

    private:
        std::string signature_;
        bool closed_ = false;
        bool cleanup_attempted_ = false;
        std::optional<CredentialSecondaryCleanupError>
            secondary_cleanup_error_;
    };

    CredentialOwner persist_session_credential(std::string& raw_api_key);
}
