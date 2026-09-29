#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "identity.h"

#include "error.h"

#include <bcrypt.h>
#include <sddl.h>

#include <array>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace sandbox::detail::filesystem::windows
{
    namespace
    {
        struct local_free_deleter
        {
            void operator()(void* pointer) const noexcept
            {
                if (pointer != nullptr)
                    LocalFree(pointer);
            }
        };

        using local_memory = std::unique_ptr<void, local_free_deleter>;

        class sid_array
        {
        public:
            sid_array() = default;

            sid_array(PSID* values, DWORD count) noexcept
                : values_(values), count_(count)
            {
            }

            sid_array(const sid_array&) = delete;
            sid_array& operator=(const sid_array&) = delete;

            sid_array(sid_array&& other) noexcept
                : values_(std::exchange(other.values_, nullptr)),
                  count_(std::exchange(other.count_, 0))
            {
            }

            sid_array& operator=(sid_array&& other) noexcept
            {
                if (this == &other)
                    return *this;
                reset();
                values_ = std::exchange(other.values_, nullptr);
                count_ = std::exchange(other.count_, 0);
                return *this;
            }

            ~sid_array()
            {
                reset();
            }

            [[nodiscard]] PSID at(DWORD index) const
            {
                if (index >= count_ || values_ == nullptr)
                    throw std::out_of_range("capability SID index out of range");
                return values_[index];
            }

            [[nodiscard]] DWORD size() const noexcept
            {
                return count_;
            }

        private:
            void reset() noexcept
            {
                if (values_ == nullptr)
                    return;
                for (DWORD index = 0; index < count_; ++index)
                {
                    if (values_[index] != nullptr)
                        FreeSid(values_[index]);
                }
                LocalFree(values_);
                values_ = nullptr;
                count_ = 0;
            }

            PSID* values_ = nullptr;
            DWORD count_ = 0;
        };

        struct derived_sid
        {
            sid_array groups;
            sid_array capabilities;

            [[nodiscard]] PSID value() const
            {
                if (capabilities.size() != 1)
                    throw std::runtime_error(
                        "Windows did not derive exactly one capability SID");
                return capabilities.at(0);
            }
        };

        using derive_capability_sids_fn = BOOL(WINAPI*)(
            LPCWSTR,
            PSID**,
            DWORD*,
            PSID**,
            DWORD*);

        std::wstring uppercase_path(const std::filesystem::path& path)
        {
            std::wstring value = path.native();
            if (!value.empty())
                CharUpperBuffW(value.data(), static_cast<DWORD>(value.size()));
            return value;
        }

        std::string utf8(std::wstring_view input)
        {
            if (input.empty())
                return {};

            const int size = WideCharToMultiByte(
                CP_UTF8,
                WC_ERR_INVALID_CHARS,
                input.data(),
                static_cast<int>(input.size()),
                nullptr,
                0,
                nullptr,
                nullptr);
            if (size <= 0)
                throw_win32("WideCharToMultiByte(size)", GetLastError());

            std::string output(static_cast<std::size_t>(size), '\0');
            if (WideCharToMultiByte(
                    CP_UTF8,
                    WC_ERR_INVALID_CHARS,
                    input.data(),
                    static_cast<int>(input.size()),
                    output.data(),
                    size,
                    nullptr,
                    nullptr) <= 0)
            {
                throw_win32("WideCharToMultiByte", GetLastError());
            }
            return output;
        }

        std::array<std::uint8_t, 32> sha256(std::string_view input)
        {
            BCRYPT_ALG_HANDLE algorithm = nullptr;
            if (const NTSTATUS status = BCryptOpenAlgorithmProvider(
                    &algorithm,
                    BCRYPT_SHA256_ALGORITHM,
                    nullptr,
                    0);
                status < 0)
            {
                throw std::runtime_error("BCryptOpenAlgorithmProvider(SHA256) failed");
            }

            struct algorithm_guard
            {
                BCRYPT_ALG_HANDLE value;

                ~algorithm_guard()
                {
                    if (value != nullptr)
                        BCryptCloseAlgorithmProvider(value, 0);
                }
            } guard{algorithm};

            std::array<std::uint8_t, 32> digest{};
            if (const NTSTATUS status = BCryptHash(
                    algorithm,
                    nullptr,
                    0,
                    reinterpret_cast<PUCHAR>(const_cast<char*>(input.data())),
                    static_cast<ULONG>(input.size()),
                    digest.data(),
                    static_cast<ULONG>(digest.size()));
                status < 0)
            {
                throw std::runtime_error("BCryptHash(SHA256) failed");
            }
            return digest;
        }

        std::wstring hex(const std::array<std::uint8_t, 32>& digest)
        {
            constexpr wchar_t alphabet[] = L"0123456789ABCDEF";
            std::wstring output(digest.size() * 2, L'\0');
            for (std::size_t index = 0; index < digest.size(); ++index)
            {
                output[index * 2] = alphabet[digest[index] >> 4];
                output[index * 2 + 1] = alphabet[digest[index] & 0x0F];
            }
            return output;
        }

        std::wstring build_capability_name(
            const std::filesystem::path& canonical_path,
            permission access)
        {
            std::string material = utf8(capability_signature);
            material.push_back('\n');
            material += utf8(uppercase_path(canonical_path));
            material.push_back('\n');
            material += access == permission::read_write
                ? "read_modify"
                : permission_name(access);
            return std::wstring(capability_signature)
                + L"Filesystem"
                + hex(sha256(material));
        }

        derive_capability_sids_fn derive_capability_sids_api()
        {
            static const derive_capability_sids_fn function = [] {
                HMODULE module = GetModuleHandleW(L"kernelbase.dll");
                if (module != nullptr)
                {
                    if (auto* address = GetProcAddress(
                            module,
                            "DeriveCapabilitySidsFromName"))
                    {
                        return reinterpret_cast<derive_capability_sids_fn>(address);
                    }
                }

                module = LoadLibraryW(L"api-ms-win-security-base-l1-2-2.dll");
                if (module == nullptr)
                    throw_win32("LoadLibraryW(capability API set)", GetLastError());

                auto* address = GetProcAddress(
                    module,
                    "DeriveCapabilitySidsFromName");
                if (address == nullptr)
                    throw_win32(
                        "GetProcAddress(DeriveCapabilitySidsFromName)",
                        GetLastError());
                return reinterpret_cast<derive_capability_sids_fn>(address);
            }();
            return function;
        }

        derived_sid derive_capability_sid(const std::wstring& name)
        {
            PSID* group_values = nullptr;
            PSID* capability_values = nullptr;
            DWORD group_count = 0;
            DWORD capability_count = 0;
            if (!derive_capability_sids_api()(
                    name.c_str(),
                    &group_values,
                    &group_count,
                    &capability_values,
                    &capability_count))
            {
                throw_win32("DeriveCapabilitySidsFromName", GetLastError());
            }
            return {
                sid_array(group_values, group_count),
                sid_array(capability_values, capability_count),
            };
        }

        std::wstring sid_to_string(PSID sid)
        {
            LPWSTR raw = nullptr;
            if (!ConvertSidToStringSidW(sid, &raw))
                throw_win32("ConvertSidToStringSidW", GetLastError());
            local_memory memory(raw);
            return raw;
        }

        std::vector<unsigned char> copy_sid(PSID sid)
        {
            const DWORD size = GetLengthSid(sid);
            std::vector<unsigned char> bytes(size);
            if (!CopySid(size, bytes.data(), sid))
                throw_win32("CopySid", GetLastError());
            return bytes;
        }
    }

    std::filesystem::path canonical_existing_path(
        const std::filesystem::path& input)
    {
        if (input.empty())
            throw std::invalid_argument("sandbox registry path must not be empty");

        std::error_code error;
        auto canonical = std::filesystem::canonical(input, error);
        if (error)
        {
            throw std::system_error(
                error,
                "sandbox registry path must exist: " + input.string());
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
        throw std::invalid_argument("unknown sandbox permission");
    }

    capability_identity derive_capability_identity(
        const std::filesystem::path& canonical_path,
        permission access)
    {
        const std::wstring name = build_capability_name(canonical_path, access);
        derived_sid derived = derive_capability_sid(name);
        PSID sid = derived.value();
        return {
            name,
            sid_to_string(sid),
            copy_sid(sid),
        };
    }
}
