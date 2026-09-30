#include "validate.h"
#include "error.h"

#include <utility>
#include <vector>

namespace event_port::detail
{
    namespace
    {
        Result<void> validation_result(
            const char* operation,
            std::vector<Error>&& errors)
        {
            if (errors.empty())
                return Result<void>::success();
            if (errors.size() == 1)
                return Result<void>::failure(std::move(errors.front()));
            return Result<void>::failure(Error{
                "event_port", operation, "validation_error", "Multiple fields are invalid",
                {}, std::move(errors)});
        }

        Result<void> validate_references(const References& references)
        {
            return guard<void>("validate_references", [&]() -> Result<void>
            {
                std::vector<Error> errors;
                for (std::size_t index = 0; index < references.size(); ++index)
                {
                    if (references[index].type.empty())
                    {
                        errors.emplace_back(Error{
                            "event_port", "validate_references", "validation_error",
                            "Reference type must not be empty",
                            {{{"field", "references.type"}, {"index", index},
                              {"value", references[index].value}}}, {}});
                    }
                }
                return validation_result("validate_references", std::move(errors));
            });
        }
    }

    Result<void> validate_event(
        const std::string& package,
        const std::string& type,
        const References& references)
    {
        return guard<void>("validate_event", [&]() -> Result<void>
        {
            std::vector<Error> errors;
            if (package.empty())
            {
                errors.emplace_back(Error{
                    "event_port", "validate_event", "validation_error",
                    "Package must not be empty", {{{"field", "package"}, {"value", package}}}, {}});
            }
            if (type.empty())
            {
                errors.emplace_back(Error{
                    "event_port", "validate_event", "validation_error",
                    "Event type must not be empty", {{{"field", "type"}, {"value", type}}}, {}});
            }
            auto reference_result = validate_references(references);
            if (reference_result.error)
                errors.emplace_back(std::move(*reference_result.error));
            return validation_result("validate_event", std::move(errors));
        });
    }

    Result<void> validate_registration(const References& references)
    {
        return validate_references(references);
    }
}
