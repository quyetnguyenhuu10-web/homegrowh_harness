#include "../common/bench_cases.h"

int main(int argc, char** argv)
{
    return linux_edit_stress::run_category(
        argc,
        argv,
        "bench",
        "Benchmark edit",
        [](const linux_edit_stress::options& value,
           linux_edit_stress::result_writer& writer)
        {
            linux_edit_stress::run_bench_matrix(
                value,
                writer,
                "bench",
                "Benchmark edit",
                linux_edit_stress::bench_file_sizes(value.profile)
            );
        }
    );
}
