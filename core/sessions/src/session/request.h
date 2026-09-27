#pragma once

#include "session.h"

#include <cstdint>
#include <string>

#include <provider>

namespace sessions
{
    class RequestStage final
    {
    public:
        RequestStage(const RequestStage&) = delete;
        RequestStage& operator=(const RequestStage&) = delete;
        RequestStage(RequestStage&&) noexcept = default;
        RequestStage& operator=(RequestStage&&) noexcept = default;

        [[nodiscard]] bool compact() const noexcept;
        [[nodiscard]] std::uint64_t context_usage() const noexcept;
        [[nodiscard]] std::uint64_t context_limit() const noexcept;
        [[nodiscard]] const std::string& model() const noexcept;
        [[nodiscard]] provider::Provider selected_provider() const noexcept;

    private:
        RequestStage(
            std::uint64_t generation,
            std::uint64_t token,
            bool compact,
            std::uint64_t context_usage,
            std::uint64_t context_limit,
            std::string model,
            provider::Provider selected_provider) noexcept;

        std::uint64_t generation_ = 0;
        std::uint64_t token_ = 0;
        bool compact_ = false;
        std::uint64_t context_usage_ = 0;
        std::uint64_t context_limit_ = 0;
        std::string model_;
        provider::Provider selected_provider_ = provider::Provider::openai;

        friend RequestStage declare_request(Session& session);
        friend void run_request(Session& session, RequestStage&& stage);
    };

    RequestStage declare_request(Session& session);
    void run_request(Session& session, RequestStage&& stage);
}
