"use strict";

function scatter(title, field, label) {
    return {
        kind: "numeric",
        title,
        x: field,
        y: "metrics.duration_ms",
        group: "case.scenario",
        xLabel: label,
        yLabel: "time (ms)",
        line: false,
        guides: false,
    };
}

require("../common/report_runtime").runAggregate({
    categoryDirectory: __dirname,
    title: "Phân tích kích thước",
    inputDirs: [
        "../bench/results",
    ],
    charts: [
        scatter(
            "old_data size → duration",
            "dimensions.old_size_bytes",
            "old_data size"
        ),
        scatter(
            "new_data size → duration",
            "dimensions.new_size_bytes",
            "new_data size"
        ),
    ],
});
