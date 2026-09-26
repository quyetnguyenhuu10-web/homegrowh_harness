#include "loop.h"

#include <session/request.h>
#include <session/response.h>
#include <session/session.h>
#include <session/tool.h>

#include <stdexcept>
#include <utility>

namespace sessions
{
    LoopResult loop(
        SessionConfig&& config)
    {
        Session session = register_session(std::move(config));

        while (!session.finished())
        {
            switch (session.state())
            {
                case SessionState::request:
                {
                    RequestStage stage = declare_request(session);
                    run_request(session, std::move(stage));
                    break;
                }

                case SessionState::response:
                {
                    ResponseStage stage = declare_response(session);
                    run_response(session, std::move(stage));
                    break;
                }

                case SessionState::tool:
                {
                    ToolStage stage = declare_tool(session);
                    run_tool(session, std::move(stage));
                    break;
                }

                case SessionState::finished:
                    break;

                case SessionState::closed:
                    throw std::logic_error("sessions loop closed before completion");
            }
        }

        SessionResult result = close_session(std::move(session));
        return LoopResult{
            std::move(result.history),
            std::move(result.usage)
        };
    }
}
