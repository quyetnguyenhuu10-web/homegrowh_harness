#include "identity.h"
#include "../error_schema.h"

#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>

namespace sandbox::detail::filesystem::linux
{
    namespace
    {
        std::uint64_t fnv1a_64(std::string_view input)
        {
            std::uint64_t value = 14695981039346656037ull;
            for (const unsigned char byte : input)
            {
                value ^= byte;
                value *= 1099511628211ull;
            }
            return value;
        }

        std::wstring hex64(std::uint64_t value)
        {
            std::wostringstream stream;
            stream << std::uppercase
                   << std::hex
                   << std::setw(16)
                   << std::setfill(L'0')
                   << value;
            return stream.str();
        }
    }

    std::filesystem::path canonical_existing_path(
        const std::filesystem::path& input)
    {
        if (input.empty())
        {
            sandbox::detail::throw_error(
                sandbox::detail::make_error(
                    "canonical_existing_path",
                    "invalid_argument",
                    "sandbox registry path must not be empty"));
        }

        std::error_code error;
        auto canonical = std::filesystem::canonical(input, error);
        if (error)
        {
            sandbox::detail::throw_error(
                sandbox::detail::make_system_error(
                    "std::filesystem::canonical",
                    error,
                    input));
        }
        return canonical.lexically_normal();
    }

    std::string_view permission_name(permission value)
    {
        switch (value)
        {
        case permission::read_only:
            return "read_only";
        case permission::read_write:
            return "read_write";
        }
        sandbox::detail::throw_error(
            sandbox::detail::make_error(
                "permission_name",
                "invalid_argument",
                "unknown sandbox permission"));
    }

    std::wstring policy_identity(
        const std::filesystem::path& canonical_path,
        permission access)
    {
        std::string material(capability_signature);
        material.push_back('\n');
        material += canonical_path.native();
        material.push_back('\n');
        material += access == permission::read_write
            ? "read_modify"
            : permission_name(access);

        /*
         * This identity is only a stable registry key. The security boundary
         * is Landlock enforcement at process launch, not this hash value.
         */
        return L"HomegrowphHarnessFilesystem"
            + hex64(fnv1a_64(material));
    }
}
