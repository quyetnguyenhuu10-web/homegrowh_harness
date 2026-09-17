#include "../common/bench_cases.h"

int main(int argc, char** argv)
{
    return windows_edit_stress::run_category(
        argc,
        argv,
        "bench",
        "Benchmark edit",
        [](const windows_edit_stress::options& value,
           windows_edit_stress::result_writer& writer)
        {
            windows_edit_stress::run_bench_matrix(
                value,
                writer,
                "bench",
                "Benchmark edit",
                windows_edit_stress::bench_file_sizes(value.profile)
            );
        }
    );
}
