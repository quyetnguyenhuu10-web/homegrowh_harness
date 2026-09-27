#include <secrets>

#include <chrono>
#include <stdexcept>
#include <string>
#include <system_error>
#include <type_traits>
#include <utility>

namespace
{
    void require(bool condition, const char* message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    class secret_cleanup final
    {
    public:
        explicit secret_cleanup(std::string signature)
            : signature_(std::move(signature))
        {
        }

        secret_cleanup(const secret_cleanup&) = delete;
        secret_cleanup& operator=(const secret_cleanup&) = delete;

        ~secret_cleanup()
        {
            secrets::erase(signature_);
        }

    private:
        std::string signature_;
    };
}

int main()
{
    const auto unique = std::chrono::steady_clock::now()
        .time_since_epoch()
        .count();
    const std::string signature =
        "HomegrowphHarness/Test/Secrets/" + std::to_string(unique);
    secret_cleanup cleanup(signature);

    const secrets::SecretOperationResult first_set =
        secrets::set(signature, "first-secret");
    require(
        first_set.status == secrets::SecretStatus::success,
        "secrets::set failed");

    require(
        secrets::resolve(signature) == "first-secret",
        "secrets::resolve returned unexpected value");

    const secrets::SecretOperationResult second_set =
        secrets::set(signature, "second-secret");
    require(
        second_set.status == secrets::SecretStatus::success,
        "secrets::set update failed");

    require(
        secrets::resolve(signature) == "second-secret",
        "secrets::resolve did not return updated value");

    static_assert(!std::is_copy_constructible_v<secrets::SecureString>);
    static_assert(!std::is_copy_assignable_v<secrets::SecureString>);
    static_assert(std::is_move_constructible_v<secrets::SecureString>);

    secrets::SecureString secure = secrets::resolve_secure(signature);
    require(
        secure.view() == "second-secret",
        "secrets::resolve_secure returned unexpected value");

    secrets::SecureString moved = std::move(secure);
    require(secure.empty(), "moved-from SecureString must be empty");
    require(
        moved.view() == "second-secret",
        "moved SecureString returned unexpected value");

    const secrets::SecretOperationResult erased = secrets::erase(signature);
    require(
        erased.status == secrets::SecretStatus::success,
        "secrets::erase failed");

    const secrets::SecretResult missing = secrets::get(signature);
    require(
        missing.status == secrets::SecretStatus::not_found,
        "erased secret is still present");

    bool resolve_failed = false;
    try
    {
        (void)secrets::resolve(signature);
    }
    catch (const std::system_error&)
    {
        resolve_failed = true;
    }

    require(resolve_failed, "resolve of missing secret did not preserve OS error");

    const std::string session_signature =
        "HomegrowphHarness/Test/Secrets/Session/" + std::to_string(unique);

    const secrets::SecretOperationResult session_set =
        secrets::set_session(session_signature, "session-secret");
    require(
        session_set.status == secrets::SecretStatus::success,
        "secrets::set_session failed");

    secrets::SecureString session_value =
        secrets::resolve_secure_session(session_signature);
    require(
        session_value.view() == "session-secret",
        "secrets::resolve_secure_session returned unexpected value");

    const secrets::SecretOperationResult session_erased =
        secrets::erase_session(session_signature);
    require(
        session_erased.status == secrets::SecretStatus::success,
        "secrets::erase_session failed");

    bool session_resolve_failed = false;
    try
    {
        (void)secrets::resolve_secure_session(session_signature);
    }
    catch (const std::system_error&)
    {
        session_resolve_failed = true;
    }
    require(
        session_resolve_failed,
        "erased session secret is still resolvable");

    return 0;
}
