#pragma once

#include "error.h"
#include <event_port>

#include <type_traits>
#include <utility>

namespace sessions::detail
{
    template <typename Operation>
    auto checked_port(Operation&& operation)
    {
        auto result = event_port::port(std::forward<Operation>(operation));
        if (result.error)
            throw ErrorException(convert_error<Error>(std::move(*result.error)));

        if constexpr (!std::is_same_v<std::remove_cvref_t<Operation>, event_port::Close>)
            return std::move(*result.value);
    }
}
