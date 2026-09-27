#pragma once

#include "session.h"

#include <exception>

namespace sessions::detail
{
    struct SessionFailure final
    {
        std::exception_ptr exception;
        SessionState state = SessionState::closed;
    };
}
