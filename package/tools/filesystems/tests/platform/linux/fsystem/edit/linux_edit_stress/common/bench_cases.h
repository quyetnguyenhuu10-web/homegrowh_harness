#pragma once

#include "stress_support.h"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace linux_edit_stress
{
    inline std::vector<std::uint64_t> bench_file_sizes(
        std::string_view profile
    )
    {
        if (profile == "standard")
        {
            return {
                1 * mebibyte,
                4 * mebibyte,
                16 * mebibyte,
                32 * mebibyte,
                64 * mebibyte,
                128 * mebibyte,
                256 * mebibyte,
                512 * mebibyte,
            };
        }

        if (profile == "extreme")
        {
            return {
                1 * gibibyte,
                2 * gibibyte,
                3 * gibibyte,
                4 * gibibyte,
            };
        }

        return {
            1 * mebibyte,
            16 * mebibyte,
        };
    }

    inline std::uint64_t bench_long_new_size(std::uint64_t file_size)
    {
        return file_size / 16;
    }

    inline std::size_t bench_old_extra(std::uint64_t file_size)
    {
        const std::uint64_t scaled = file_size / 16384;

        if (scaled < 16)
            return 16;

        if (scaled > 16384)
            return 16384;

        return static_cast<std::size_t>(scaled);
    }

    inline void run_bench_matrix(
        const options& value,
        result_writer& writer,
        std::string_view category_id,
        std::string_view category_title,
        const std::vector<std::uint64_t>& file_sizes
    )
    {
        constexpr std::uint64_t short_new_size = 32;

        constexpr interference_kind kinds[] = {
            interference_kind::modify,
            interference_kind::delete_file,
            interference_kind::rename_file,
            interference_kind::lock,
        };

        constexpr const char* kind_names[] = {
            "modify",
            "delete",
            "rename",
            "lock",
        };

        constexpr const char* positions[] = {
            "beginning",
            "middle",
            "end",
        };

        std::size_t index = 0;

        for (const std::uint64_t file_size : file_sizes)
        {
            const std::size_t old_extra = bench_old_extra(file_size);
            const std::uint64_t long_new_size =
                bench_long_new_size(file_size);

            for (int length = 0; length < 2; ++length)
            {
                const bool is_short = length == 0;
                const char* length_name = is_short ? "short" : "long";

                const std::string old_data = make_marker(
                    is_short ? "BENCH_SHORT" : "BENCH_LONG",
                    index,
                    old_extra
                );

                const std::string new_data = is_short
                    ? make_payload(
                        static_cast<std::size_t>(short_new_size),
                        value.seed + index
                    )
                    : make_payload(
                        static_cast<std::size_t>(long_new_size),
                        value.seed + index + 1000
                    );

                for (int position = 0; position < 3; ++position)
                {
                    const std::uint64_t marker_offset =
                        position == 0
                        ? 0
                        : position == 1
                            ? file_size / 2
                            : file_size - old_data.size();

                    const std::string position_name_value = position_name(
                        marker_offset,
                        file_size,
                        old_data.size()
                    );

                    const std::string size_suffix =
                        std::to_string(file_size);

                    for (std::uint64_t rep = 0; rep < value.repeat; ++rep)
                    {
                        const std::string rep_suffix = value.repeat > 1
                            ? "-r" + std::to_string(rep + 1)
                            : "";

                        edit_case_spec clean;
                        clean.id = "clean-" +
                            std::string(positions[position]) + "-" +
                            length_name + "-" + size_suffix + rep_suffix;
                        clean.scenario = length_name;
                        clean.expected = "success";
                        clean.path = case_path(
                            value,
                            category_id,
                            clean.id
                        );
                        clean.file_size = file_size;
                        clean.marker_offset = marker_offset;
                        clean.old_data = old_data;
                        clean.new_data = new_data;
                        clean.pattern_position = position_name_value;

                        writer.write(run_edit_case(
                            value,
                            category_id,
                            category_title,
                            clean
                        ));
                        ++index;

                        for (std::size_t kind = 0; kind < 4; ++kind)
                        {
                            interference_case_spec spec;
                            spec.edit.id =
                                std::string(kind_names[kind]) + "-" +
                                positions[position] + "-" +
                                length_name + "-" + size_suffix +
                                rep_suffix;
                            spec.edit.scenario = length_name;
                            spec.edit.expected = "interference";
                            spec.edit.path = case_path(
                                value,
                                category_id,
                                spec.edit.id
                            );
                            spec.edit.file_size = file_size;
                            spec.edit.marker_offset = marker_offset;
                            spec.edit.old_data = old_data;
                            spec.edit.new_data = new_data;
                            spec.edit.pattern_position = position_name_value;
                            spec.action = kinds[kind];
                            spec.interference_offset = marker_offset;
                            spec.lock_before_edit =
                                kinds[kind] == interference_kind::lock;

                            for (int attempt = 0;; ++attempt)
                            {
                                const result_record observed =
                                    run_interference_case(
                                        value,
                                        category_id,
                                        category_title,
                                        spec
                                    );
                                ++index;

                                const bool landed_after_edit =
                                    observed.interference_succeeded &&
                                    !observed.interference_in_window;

                                if (!landed_after_edit)
                                {
                                    writer.write(observed);
                                    break;
                                }

                                if (attempt + 1 >= 10)
                                {
                                    std::cout
                                        << observed.case_id
                                        << " dropped: interference landed "
                                           "after edit 10 times"
                                        << '\n';
                                    break;
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
