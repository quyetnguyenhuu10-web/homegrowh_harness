#pragma once

#include <request/requests.h>

#include <iosfwd>
#include <memory>
#include <string>

#include <nlohmann/json.hpp>

namespace provider
{
    class Stream
    {
    public:
        Stream(
            Provider provider,
            const std::string& url,
            const std::string& api_key,
            const nlohmann::json& body);

        ~Stream();

        Stream(const Stream&) = delete;
        Stream& operator=(const Stream&) = delete;

        Stream(Stream&&) noexcept;
        Stream& operator=(Stream&&) noexcept;

        const RequestUsage& usage() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;

        friend std::ostream& operator<<(std::ostream& output, Stream& stream);
    };

    std::ostream& operator<<(std::ostream& output, Stream& stream);
}
