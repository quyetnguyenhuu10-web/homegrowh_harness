#include <sandbox/registry.h>

#include "../filesystem/filesystem.h"

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
        catch (const std::system_error& exception)
        {
            registry_result result;
            result.final_error = exception.code();
            return result;
        }
    }
}
