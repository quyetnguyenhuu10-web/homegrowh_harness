#include "context_usage.h"

#include <context_usage.generated.h>

namespace provider
{
    std::uint64_t context_usage(const RequestUsage& usage)
    {
        return context_usage_generated(usage);
    }
}
