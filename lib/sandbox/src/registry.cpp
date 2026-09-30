#include <registry.h>

#include "error_schema.h"
#include "filesystem_access.h"

namespace sandbox
{
    registry_result registry(
        const std::vector<registry_request>& requests,
        bool refresh)
    {
        try
        {
            if (refresh)
                return detail::filesystem::refresh_permissions(requests);
            return detail::filesystem::reuse_permissions(requests);
        }
        catch (...)
        {
            registry_result result;
            result.final_error = detail::capture_exception(
                "registry", std::current_exception());
            return result;
        }
    }

    release_result release(const std::filesystem::path& path)
    {
        try
        {
            return detail::filesystem::release_permissions(path);
        }
        catch (...)
        {
            release_result result;
            result.final_error = detail::capture_exception(
                "release", std::current_exception(),
                {{"path", detail::error_path_text(path)}});
            return result;
        }
    }

    release_result release_all()
    {
        try
        {
            return detail::filesystem::release_all_permissions();
        }
        catch (...)
        {
            release_result result;
            result.final_error = detail::capture_exception(
                "release_all", std::current_exception());
            return result;
        }
    }
}
