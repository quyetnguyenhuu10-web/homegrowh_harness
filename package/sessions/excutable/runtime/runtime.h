#pragma once

#include "command.h"

#include <cstdint>
#include <exception>
#include <optional>
#include <string_view>

#include <sessions>

namespace sessions_runtime
{
    class Runtime final
    {
    public:
        Runtime() = default;

        Runtime(const Runtime&) = delete;
        Runtime& operator=(const Runtime&) = delete;

        void emit_ready() const;
        void emit_protocol_error(std::exception_ptr error) const noexcept;
        void emit_runtime_failure(
            std::string_view source,
            std::exception_ptr error,
            int exit_code) const noexcept;

        [[nodiscard]] bool execute(Command&& command);

    private:
        nlohmann::json register_session(nlohmann::json&& payload);
        nlohmann::json declare_request();
        nlohmann::json run_request();
        nlohmann::json declare_response();
        nlohmann::json run_response();
        nlohmann::json declare_tool();
        nlohmann::json run_tool();
        nlohmann::json close();

        void require_session() const;
        void require_no_pending_stage() const;
        std::string state_name() const;

        void emit_finished(
            const Command& command,
            nlohmann::json&& result,
            const char* state_override = nullptr) const;
        void emit_failed(
            const Command& command,
            std::exception_ptr error) const noexcept;

        std::optional<sessions::Session> session_;
        std::optional<sessions::RequestStage> request_stage_;
        std::optional<sessions::ResponseStage> response_stage_;
        std::optional<sessions::ToolStage> tool_stage_;
        std::uint64_t last_command_id_ = 0;
    };
}
