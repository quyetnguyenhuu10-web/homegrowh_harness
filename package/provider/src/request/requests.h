#pragma once

#include "bonsai.h"
#include "deepseek.h"
#include "openai.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>
#include <nlohmann/json.hpp>

namespace provider
{
    enum class UsageState
    {
        unavailable
    };

    using RequestUsage = std::variant<
        OpenAIUsage,
        DeepSeekUsage,
        BonsaiUsage,
        UsageState>;

    struct CompactionResult;

    enum class CompletionType
    {
        data,
        failed,
        finished
    };

    struct RawResponse
    {
        std::string buffer;
        std::atomic_bool readable{false};

    private:
        std::uintptr_t completion_port = 0;
        std::atomic_bool in_flight{false};

        friend class CompletionPort;
        friend RequestUsage request(
            const std::string&,
            const std::string&,
            const nlohmann::json&,
            RawResponse*);
        friend CompactionResult compaction(
            const std::string&,
            const nlohmann::json&,
            RawResponse*,
            bool,
            const nlohmann::json&,
            const nlohmann::json&);

        std::vector<std::size_t> event_sizes;
    };

    struct Completion
    {
        CompletionType type = CompletionType::data;
        RawResponse* response = nullptr;
        std::size_t offset = 0;
        std::size_t bytes = 0;
        /* Original OS/transport/HTTP error code. Zero outside failed. */
        std::uint32_t error = 0;
    };

    class CompletionPort
    {
    public:
        CompletionPort();
        ~CompletionPort();

        CompletionPort(const CompletionPort&) = delete;
        CompletionPort& operator=(const CompletionPort&) = delete;
        CompletionPort(CompletionPort&&) noexcept;
        CompletionPort& operator=(CompletionPort&&) noexcept;

        void register_response(RawResponse* response);
        bool wait(
            Completion* completion,
            std::uint32_t timeout_ms = 0xFFFFFFFFu);

    private:
        std::uintptr_t handles_[2]{};
    };

    RequestUsage request(
        const std::string& url,
        const std::string& api_key,
        const nlohmann::json& body,
        RawResponse* raw_response = nullptr);

    template <typename Reader>
    bool read(RawResponse* raw_response, Reader&& reader)
    {
        if (
            raw_response == nullptr ||
            !raw_response->readable.load(std::memory_order_acquire))
        {
            return false;
        }

        try
        {
            std::forward<Reader>(reader)(std::string_view(raw_response->buffer));
        }
        catch (...)
        {
            raw_response->readable.store(false, std::memory_order_release);
            raw_response->readable.notify_one();
            throw;
        }

        raw_response->readable.store(false, std::memory_order_release);
        raw_response->readable.notify_one();
        return true;
    }
}
