"use strict";

require("../common/report_runtime").runCategory({
    categoryDirectory: __dirname,
    executable: "linux_edit_stress_bench",
    title: "Benchmark edit",
    pages: {
        field: "dimensions.file_size_bytes",
        title: "File size",
    },
    pageSummary: true,
    charts: [
        {
            kind: "ramtime",
            title: "RAM & Time",
            xLabel: "lần edit",
            yLabel: "RAM (MB)",
        },
    ],
});
