#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace fsystem::detail
{
    class line_range_collector
    {
    public:
        line_range_collector(
            std::uint64_t start_line,
            std::uint64_t end_line,
            std::string& output
        ) noexcept
            : start_line_(start_line),
              end_line_(end_line),
              output_(output)
        {
        }

        bool consume(std::string_view chunk)
        {
            if (done_ || overflowed_)
                return false;

            std::size_t segment_start = 0;

            for (std::size_t index = 0; index < chunk.size(); ++index)
            {
                if (chunk[index] != '\n')
                    continue;

                if (current_line_ >= start_line_)
                {
                    if (!append(chunk.substr(
                            segment_start,
                            index + 1 - segment_start
                        )))
                    {
                        return false;
                    }
                }

                segment_start = index + 1;

                if (current_line_ >= end_line_)
                {
                    done_ = true;
                    return false;
                }

                ++current_line_;
            }

            if (
                segment_start < chunk.size() &&
                current_line_ >= start_line_
            )
            {
                if (!append(chunk.substr(segment_start)))
                    return false;
            }

            return true;
        }

        bool done() const noexcept
        {
            return done_;
        }

        bool overflowed() const noexcept
        {
            return overflowed_;
        }

    private:
        bool append(std::string_view data)
        {
            if (data.size() > output_.max_size() - output_.size())
            {
                overflowed_ = true;
                return false;
            }

            output_.append(data.data(), data.size());
            return true;
        }

        std::uint64_t start_line_ = 1;
        std::uint64_t end_line_ = 1;
        std::uint64_t current_line_ = 1;
        std::string& output_;
        bool done_ = false;
        bool overflowed_ = false;
    };
}
