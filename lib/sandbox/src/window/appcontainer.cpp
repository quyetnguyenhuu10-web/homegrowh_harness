#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "appcontainer.h"
#include "../error_schema.h"

#include <Sddl.h>
#include <UserEnv.h>

#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <system_error>

namespace sandbox::detail::process::windows
{
    namespace
    {
        constexpr wchar_t appcontainer_name[] =
            L"HomegrowphHarness.ToolSandbox";

        [[noreturn]] void throw_win32(const char* action, DWORD error)
        {
            sandbox::detail::throw_error(
                sandbox::detail::make_native_error(
                    action,
                    error));
        }

        [[noreturn]] void throw_hresult(const char* action, HRESULT result)
        {
            Error error = sandbox::detail::make_error(
                action,
                "system_error",
                "HRESULT failure",
                {{"code", static_cast<std::int64_t>(result)},
                 {"category", "hresult"},
                 {"api", action}});
            if (HRESULT_FACILITY(result) == FACILITY_WIN32)
            {
                const DWORD code = HRESULT_CODE(result);
                error.message = std::error_code(
                    static_cast<int>(code), std::system_category()).message();
                error.data.front()["win32_code"] = code;
            }
            sandbox::detail::throw_error(std::move(error));
        }

        unique_sid create_or_derive_appcontainer_sid()
        {
            PSID raw_sid = nullptr;
            HRESULT result = CreateAppContainerProfile(
                appcontainer_name,
                appcontainer_name,
                L"HomegrowphHarness tool sandbox",
                nullptr,
                0,
                &raw_sid);
            const char* api = "CreateAppContainerProfile";

            if (result == HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS))
            {
                api = "DeriveAppContainerSidFromAppContainerName";
                result = DeriveAppContainerSidFromAppContainerName(
                    appcontainer_name,
                    &raw_sid);
            }
            if (FAILED(result))
                throw_hresult(api, result);
            return unique_sid(raw_sid);
        }
    }

    appcontainer_security::appcontainer_security(
        const registry_result& registry,
        network_access network)
        : appcontainer_sid_(create_or_derive_appcontainer_sid())
    {
        capability_sid_storage_.reserve(registry.permissions.size());
        capabilities_.reserve(
            registry.permissions.size()
            + (network == network_access::internet_client ? 1u : 0u));

        for (const registered_permission& permission : registry.permissions)
        {
            PSID raw_sid = nullptr;
            if (!ConvertStringSidToSidW(permission.sid.c_str(), &raw_sid))
                throw_win32("ConvertStringSidToSidW", GetLastError());

            capability_sid_storage_.emplace_back(raw_sid);
            capabilities_.push_back({
                raw_sid,
                SE_GROUP_ENABLED,
            });
        }

        if (network == network_access::internet_client)
        {
            DWORD size = SECURITY_MAX_SID_SIZE;
            capability_buffer_storage_.emplace_back(size);
            auto& buffer = capability_buffer_storage_.back();
            PSID sid = buffer.data();
            if (!CreateWellKnownSid(
                    WinCapabilityInternetClientSid,
                    nullptr,
                    sid,
                    &size))
            {
                throw_win32("CreateWellKnownSid(internetClient)", GetLastError());
            }
            buffer.resize(size);
            capabilities_.push_back({sid, SE_GROUP_ENABLED});
        }

        security_.AppContainerSid = appcontainer_sid_.get();
        security_.Capabilities = capabilities_.empty()
            ? nullptr
            : capabilities_.data();
        security_.CapabilityCount = static_cast<DWORD>(capabilities_.size());
        security_.Reserved = 0;
    }

    void appcontainer_security::apply(
        LPPROC_THREAD_ATTRIBUTE_LIST attributes) const
    {
        if (!UpdateProcThreadAttribute(
                attributes,
                0,
                PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES,
                const_cast<SECURITY_CAPABILITIES*>(&security_),
                sizeof(security_),
                nullptr,
                nullptr))
        {
            throw_win32(
                "UpdateProcThreadAttribute(SECURITY_CAPABILITIES)",
                GetLastError());
        }
    }
}
