#pragma once

#include <registry.h>

#include <cstddef>
#include <cstdint>
#include <system_error>

namespace sandbox::detail::process::linux
{
    enum class landlock_error_stage : std::uint32_t
    {
        none = 0,
        unsupported,
        query_abi,
        create_ruleset,
        inspect_path,
        open_path,
        add_rule,
        set_no_new_privileges,
        restrict_self,
        close_path,
        close_ruleset,
    };

    struct landlock_error
    {
        int code = 0;
        landlock_error_stage stage = landlock_error_stage::none;
        std::size_t permission_index = static_cast<std::size_t>(-1);
        int cleanup_code = 0;
        landlock_error_stage cleanup_stage = landlock_error_stage::none;
        int native_result = 0;

        explicit operator bool() const noexcept
        {
            return code != 0 || stage != landlock_error_stage::none;
        }
    };

    landlock_error apply_landlock(const registry_result& registry) noexcept;
}
