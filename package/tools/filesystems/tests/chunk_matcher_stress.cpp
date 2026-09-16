#include <fsystem/edit/edit_detail.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <random>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    constexpr std::uint64_t random_seed = 0x4D41544348455231ULL;
    constexpr std::size_t random_case_count = 8000;
    constexpr std::size_t maximum_random_file_size = 16 * 1024;
    constexpr std::size_t maximum_random_pattern_size = 512;

    struct expected_match
    {
        std::uint64_t first_occurrence =
            fsystem::detail::chunk_matcher::no_occurrence;
        fsystem::EditNote note = fsystem::EditNote::none;
    };

    expected_match oracle(
        std::string_view file,
        std::string_view pattern
    )
    {
        expected_match result;

        if (pattern.empty())
        {
            result.first_occurrence = 0;

            if (!file.empty())
            {
                result.note =
                    fsystem::EditNote::old_data_appears_more_than_once;
            }

            return result;
        }

        const std::size_t first = file.find(pattern);

        if (first == std::string_view::npos)
        {
            result.note = fsystem::EditNote::old_data_not_found;
            return result;
        }

        result.first_occurrence = first;

        const std::size_t second = file.find(pattern, first + 1);

        if (second != std::string_view::npos)
        {
            result.note =
                fsystem::EditNote::old_data_appears_more_than_once;
        }

        return result;
    }

    std::vector<std::size_t> fixed_chunks(
        std::size_t file_size,
        std::size_t chunk_size
    )
    {
        std::vector<std::size_t> chunks;

        while (file_size != 0)
        {
            const std::size_t current =
                std::min(file_size, chunk_size);
            chunks.push_back(current);
            file_size -= current;
        }

        return chunks;
    }

    std::vector<std::size_t> random_chunks(
        std::size_t file_size,
        std::mt19937_64& generator
    )
    {
        std::vector<std::size_t> chunks;
        std::size_t remaining = file_size;

        while (remaining != 0)
        {
            const std::uint64_t mode = generator() % 8;
            std::size_t requested = 1;

            switch (mode)
            {
            case 0:
                requested = 1;
                break;
            case 1:
                requested = 2;
                break;
            case 2:
                requested = 63;
                break;
            case 3:
                requested = 64 * 1024 - 1;
                break;
            case 4:
                requested = 64 * 1024;
                break;
            case 5:
                requested = 64 * 1024 + 1;
                break;
            default:
                requested = 1 + static_cast<std::size_t>(
                    generator() % std::min<std::size_t>(
                        remaining,
                        4096
                    )
                );
                break;
            }

            requested = std::min(requested, remaining);
            chunks.push_back(requested);
            remaining -= requested;
        }

        return chunks;
    }

    std::string random_file(
        std::size_t size,
        std::mt19937_64& generator
    )
    {
        static constexpr std::array<char, 8> alphabet{
            'a',
            'b',
            'c',
            '\0',
            '1',
            '\n',
            'x',
            'y'
        };

        std::string result(size, '\0');

        for (char& value : result)
        {
            value = alphabet[
                static_cast<std::size_t>(
                    generator() % alphabet.size()
                )
            ];
        }

        return result;
    }

    std::string random_pattern(
        const std::string& file,
        std::mt19937_64& generator
    )
    {
        const std::uint64_t mode = generator() % 10;

        if (mode == 0)
            return {};

        if (!file.empty() && mode < 7)
        {
            const std::size_t start = static_cast<std::size_t>(
                generator() % file.size()
            );
            const std::size_t maximum = std::min(
                maximum_random_pattern_size,
                file.size() - start
            );
            const std::size_t size = 1 + static_cast<std::size_t>(
                generator() % maximum
            );

            return file.substr(start, size);
        }

        return random_file(
            static_cast<std::size_t>(
                generator() % (maximum_random_pattern_size + 1)
            ),
            generator
        );
    }

    bool check_case(
        std::string_view file,
        std::string_view pattern,
        const std::vector<std::size_t>& chunks,
        std::size_t case_number,
        std::string_view case_name
    )
    {
        const expected_match expected = oracle(file, pattern);
        fsystem::detail::chunk_matcher matcher{
            std::string(pattern)
        };

        std::size_t offset = 0;
        bool stopped_on_duplicate = false;

        for (const std::size_t chunk_size : chunks)
        {
            if (
                chunk_size == 0 ||
                chunk_size > file.size() - offset
            )
            {
                std::cerr
                    << "Invalid chunk partition in case "
                    << case_number << " (" << case_name << ")\n";
                return false;
            }

            const bool consumed = matcher.consume(
                offset,
                file.substr(offset, chunk_size)
            );
            offset += chunk_size;

            if (!consumed)
            {
                if (!matcher.duplicate())
                {
                    std::cerr
                        << "Matcher stopped without duplicate in case "
                        << case_number << " (" << case_name << ")\n";
                    return false;
                }

                stopped_on_duplicate = true;
                break;
            }
        }

        if (!stopped_on_duplicate && offset != file.size())
        {
            std::cerr
                << "Chunk partition did not cover file in case "
                << case_number << " (" << case_name << ")\n";
            return false;
        }

        matcher.finish(file.size());

        if (
            matcher.note() != expected.note ||
            matcher.first_occurrence() != expected.first_occurrence
        )
        {
            std::cerr
                << "Matcher mismatch in case " << case_number
                << " (" << case_name << ")"
                << " file_size=" << file.size()
                << " pattern_size=" << pattern.size()
                << " expected_occurrence=" << expected.first_occurrence
                << " actual_occurrence=" << matcher.first_occurrence()
                << " expected_note="
                << static_cast<int>(expected.note)
                << " actual_note="
                << static_cast<int>(matcher.note())
                << '\n';
            return false;
        }

        return true;
    }

    bool check_targeted_cases(std::size_t& case_number)
    {
        const auto run = [&](
            std::string_view file,
            std::string_view pattern,
            std::vector<std::size_t> chunks,
            std::string_view name
        )
        {
            return check_case(
                file,
                pattern,
                chunks,
                case_number++,
                name
            );
        };

        if (!run("", "", {}, "empty file and empty pattern"))
            return false;

        if (!run("", "a", {}, "empty file and non-empty pattern"))
            return false;

        if (!run(
                "aaaaaaaaaaaa",
                "aaa",
                fixed_chunks(12, 1),
                "overlap with one-byte chunks"
            ))
        {
            return false;
        }

        const std::string boundary_file =
            std::string(65534, 'x') +
            "needle" +
            std::string(65536, 'y');

        if (!run(
                boundary_file,
                "needle",
                fixed_chunks(boundary_file.size(), 64 * 1024),
                "match across the 64 KiB boundary"
            ))
        {
            return false;
        }

        const std::string duplicate_boundary_file =
            std::string(65534, 'x') +
            "needle" +
            std::string(123, 'y') +
            "needle";

        if (!run(
                duplicate_boundary_file,
                "needle",
                fixed_chunks(duplicate_boundary_file.size(), 64 * 1024),
                "duplicate across the 64 KiB boundary"
            ))
        {
            return false;
        }

        const std::string long_pattern_file = "ABCDEFGH";

        if (!run(
                long_pattern_file,
                long_pattern_file,
                fixed_chunks(long_pattern_file.size(), 1),
                "pattern longer than every chunk"
            ))
        {
            return false;
        }

        const std::string binary_file{
            'a', '\0', 'b', 'a', '\0', 'b'
        };

        if (!run(
                binary_file,
                std::string{'a', '\0', 'b'},
                {2, 1, 3},
                "binary data and duplicate overlap"
            ))
        {
            return false;
        }

        return run(
            "abcXdef",
            "abcdef",
            {3, 4},
            "checkpoint discarded after mismatch"
        );
    }
}

int main()
{
    std::mt19937_64 generator(random_seed);
    std::size_t case_number = 0;

    if (!check_targeted_cases(case_number))
        return 1;

    for (std::size_t iteration = 0;
         iteration < random_case_count;
         ++iteration)
    {
        const std::size_t file_size = static_cast<std::size_t>(
            generator() % (maximum_random_file_size + 1)
        );
        const std::string file = random_file(file_size, generator);
        const std::string pattern = random_pattern(file, generator);
        const std::vector<std::size_t> chunks = random_chunks(
            file.size(),
            generator
        );

        if (!check_case(
                file,
                pattern,
                chunks,
                case_number++,
                "deterministic random partition"
            ))
        {
            std::cerr
                << "seed=" << random_seed
                << " iteration=" << iteration << '\n';
            return 1;
        }
    }

    std::cout
        << "Chunk matcher stress passed. cases=" << case_number
        << " seed=" << random_seed << '\n';
    return 0;
}
