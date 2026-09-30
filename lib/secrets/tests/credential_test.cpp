#include <secrets>

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <wincred.h>
#endif

namespace
{
    void require(bool condition, const char* message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    template <typename T>
    void require_success(const secrets::Result<T>& result)
    {
        if (result.error)
            throw std::runtime_error(nlohmann::json(*result.error).dump());
        require(result.value.has_value(), "Successful result has no value");
    }

    void require_schema(const nlohmann::json& error)
    {
        require(error.is_object() && error.size() == 6, "Error envelope must have six fields");
        for (const char* field : {"source", "operation", "type", "message"})
            require(error.at(field).is_string(), "Error identity field must be a string");
        require(error.at("data").is_array(), "Error data must be an array");
        require(error.at("causes").is_array(), "Error causes must be an array");
        for (const auto& cause : error.at("causes"))
            require_schema(cause);
        require(nlohmann::json::parse(error.dump()) == error, "Error JSON lost information");
    }

    class SecretCleanup final
    {
    public:
        explicit SecretCleanup(const std::string& signature, bool session = false)
            : signature_(signature), session_(session) {}
        ~SecretCleanup()
        {
            const auto cleanup = session_
                ? secrets::erase_session(signature_) : secrets::erase(signature_);
            (void)cleanup;
        }
    private:
        std::string signature_;
        bool session_;
    };

    void check_validation()
    {
        const auto missing_signature = secrets::get("");
        require(!missing_signature.value && missing_signature.error, "Invalid get must return error only");
        require_schema(*missing_signature.error);
        require(missing_signature.error->type == "validation_error", "Validation has wrong type");
        require(!missing_signature.error->data.front().contains("code"), "Validation invented an OS code");

        const auto resolved = secrets::resolve("");
        const auto secure = secrets::resolve_secure("");
        require(resolved.error && secure.error, "Resolvers must return structured errors");
        require(nlohmann::json(*resolved.error) == nlohmann::json(*missing_signature.error),
            "resolve changed the original error");
        require(nlohmann::json(*secure.error) == nlohmann::json(*missing_signature.error),
            "resolve_secure changed the original error");

        const auto invalid = secrets::set("", "");
        require(!invalid.value && invalid.error, "Invalid set must return error only");
        require_schema(*invalid.error);
        require(invalid.error->causes.size() == 2, "Independent validation errors were lost");
        require(invalid.error->causes[0].data.front().at("field") == "signature",
            "Signature validation cause was lost");
        require(invalid.error->causes[1].data.front().at("field") == "value",
            "Value validation cause was lost");

        const auto null_signature = secrets::get(std::string("prefix\0suffix", 13));
        require(null_signature.error && null_signature.error->type == "validation_error",
            "Embedded null must not silently select another credential");

        std::string empty;
        const auto empty_secure = secrets::secure_string_from(empty);
        require(empty_secure.error && !empty_secure.value, "Empty secure source must return error");
        require_schema(*empty_secure.error);

        std::string raw("secret\0bytes", 12);
        auto secured = secrets::secure_string_from(raw);
        require_success(secured);
        require(raw.empty(), "Secure string source was not wiped");
        require(secured.value->view() == std::string_view("secret\0bytes", 12),
            "Secure string lost binary bytes");

#if defined(_WIN32)
        const auto malformed = secrets::get(std::string(1, static_cast<char>(0xff)));
        require(malformed.error && !malformed.value, "Invalid UTF-8 must return structured error");
        require_schema(*malformed.error);
        const auto& data = malformed.error->data.front();
        require(data.at("code") == ERROR_NO_UNICODE_TRANSLATION, "UTF-8 conversion lost Win32 code");
        require(data.at("api") == "MultiByteToWideChar", "UTF-8 conversion lost API identity");
        require(data.at("signature_bytes").at(0) == 255, "Invalid signature bytes were lost");

        const auto oversized = secrets::set("test", std::string(CRED_MAX_CREDENTIAL_BLOB_SIZE + 1, 'x'));
        require(oversized.error && oversized.error->type == "validation_error",
            "Oversized credential must be a validation error");
        require_schema(*oversized.error);
#endif
    }

    void check_credentials()
    {
        const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
        const std::string signature = "HomegrowphHarness/Test/Secrets/" + std::to_string(unique);
        SecretCleanup cleanup(signature);
        require_success(secrets::set(signature, "first-secret"));
        auto first = secrets::resolve(signature);
        require_success(first);
        require(*first.value == "first-secret", "resolve returned an unexpected value");

        const std::string binary_value("second\0secret", 13);
        require_success(secrets::set(signature, binary_value));
        auto updated = secrets::get(signature);
        require_success(updated);
        require(*updated.value == binary_value, "Credential update lost binary bytes");

        auto secure = secrets::resolve_secure(signature);
        require_success(secure);
        secrets::SecureString moved = std::move(*secure.value);
        require(secure.value->empty(), "Moved-from SecureString must be empty");
        require(moved.view() == binary_value, "Moved SecureString lost credential bytes");

        require_success(secrets::erase(signature));
        const auto missing = secrets::get(signature);
        require(missing.error && !missing.value && missing.error->type == "not_found",
            "Missing credential must return not_found");
        require_schema(*missing.error);
        require(nlohmann::json(*secrets::resolve(signature).error) == nlohmann::json(*missing.error),
            "resolve flattened the OS error");
        require(nlohmann::json(*secrets::resolve_secure(signature).error) == nlohmann::json(*missing.error),
            "resolve_secure flattened the OS error");
#if defined(_WIN32)
        require(missing.error->data.front().at("code") == ERROR_NOT_FOUND, "Missing credential lost Win32 code");
        require(missing.error->data.front().at("api") == "CredReadW", "Missing credential lost API identity");
#endif

        const std::string session_signature = signature + "/Session";
        SecretCleanup session_cleanup(session_signature, true);
        require_success(secrets::set_session(session_signature, "session-secret"));
        auto session_value = secrets::resolve_secure_session(session_signature);
        require_success(session_value);
        require(session_value.value->view() == "session-secret", "Session credential has wrong value");
        require_success(secrets::erase_session(session_signature));
        const auto session_missing = secrets::resolve_secure_session(session_signature);
        require(session_missing.error && !session_missing.value &&
            session_missing.error->type == "not_found", "Deleted session credential remained resolvable");
        require_schema(*session_missing.error);
    }
}

int main()
{
    static_assert(!std::is_default_constructible_v<secrets::SecretResult>);
    static_assert(!std::is_copy_constructible_v<secrets::SecureString>);
    static_assert(!std::is_copy_constructible_v<secrets::SecureSecretResult>);
    static_assert(std::is_move_constructible_v<secrets::SecureSecretResult>);
    try
    {
        check_validation();
        check_credentials();
        std::cout << "secrets credential and error schema tests passed\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
