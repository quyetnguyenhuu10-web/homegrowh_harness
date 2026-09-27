#include <event_port>

#include "validate.h"

#include <stdexcept>

namespace event_port::detail
{
    namespace
    {
        void validate_references(const References& references)
        {
            for (const Reference& reference : references)
            {
                if (reference.type.empty())
                {
                    throw std::invalid_argument(
                        "event_port: reference type must not be empty");
                }
            }
        }
    }

    void validate_event(
        const std::string& package,
        const std::string& type,
        const References& references)
    {
        if (package.empty())
        {
            throw std::invalid_argument(
                "event_port: package must not be empty");
        }

        if (type.empty())
        {
            throw std::invalid_argument(
                "event_port: type must not be empty");
        }

        validate_references(references);
    }

    void validate_registration(const References& references)
    {
        validate_references(references);
    }
}
