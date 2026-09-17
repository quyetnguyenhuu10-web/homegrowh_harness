"use strict";

const {
    escapeHtml,
    get,
    number,
    formatBytes,
    formatByteField,
    formatNumber,
    formatAxisNumber,
} = require("../utils");

const { chartShell, chartMeta } = require("./shell");

function memoryChart(records) {
    const points = records.map(
        (record) => ({
            record,

            x: number(
                record,
                "dimensions.file_size_bytes"
            ),

            working: number(
                record,
                "metrics.working_set_bytes"
            ),

            peak: number(
                record,
                "metrics.peak_working_set_bytes"
            ),
        })
    );

    const maxX = Math.max(
        1,
        ...points.map(
            (point) => point.x
        )
    );

    const maxY = Math.max(
        1,
        ...points.flatMap(
            (point) => [
                point.working,
                point.peak
            ]
        )
    );

    const marks = [];

    const gridCount = 5;

    for (
        let index = 0;
        index <= gridCount;
        index += 1
    ) {
        const ratio =
            index / gridCount;

        const y =
            282 -
            ratio * 248;

        const value =
            maxY * ratio;

        marks.push(`
            <line
                class="grid"
                x1="78"
                y1="${y.toFixed(2)}"
                x2="950"
                y2="${y.toFixed(2)}"
            />

            <text
                class="axis-number y-number"
                x="68"
                y="${(y + 5).toFixed(2)}"
                text-anchor="end"
            >
                ${escapeHtml(
                    formatBytes(value)
                )}
            </text>
        `);
    }

    for (
        let index = 0;
        index <= gridCount;
        index += 1
    ) {
        const ratio =
            index / gridCount;

        const x =
            78 +
            ratio * 872;

        const value =
            maxX * ratio;

        marks.push(`
            <line
                class="tick"
                x1="${x.toFixed(2)}"
                y1="282"
                x2="${x.toFixed(2)}"
                y2="288"
            />

            <text
                class="axis-number x-number"
                x="${x.toFixed(2)}"
                y="307"
                text-anchor="middle"
            >
                ${escapeHtml(
                    formatBytes(value)
                )}
            </text>
        `);
    }

    const sortedPoints = [...points].sort(
        (left, right) => left.x - right.x
    );

    const guideMarks = [];
    const pointMarks = [];

    for (const point of points) {
        const x =
            78 +
            point.x /
                maxX *
                872;

        const values = [
            point.working,
            point.peak,
        ];

        for (const value of values) {
            const y =
                282 -
                value /
                    maxY *
                    248;

            guideMarks.push(`
                <line
                    class="guide"
                    x1="${x.toFixed(2)}"
                    y1="${y.toFixed(2)}"
                    x2="${x.toFixed(2)}"
                    y2="282"
                />

                <line
                    class="guide"
                    x1="78"
                    y1="${y.toFixed(2)}"
                    x2="${x.toFixed(2)}"
                    y2="${y.toFixed(2)}"
                />
            `);
        }
    }

    marks.push(...guideMarks);

    if (sortedPoints.length > 1) {
        const workingPolyline = sortedPoints.map((point) => {
            const x =
                78 +
                point.x /
                    maxX *
                    872;

            const y =
                282 -
                point.working /
                    maxY *
                    248;

            return `${x.toFixed(2)},${y.toFixed(2)}`;
        }).join(" ");

        const peakPolyline = sortedPoints.map((point) => {
            const x =
                78 +
                point.x /
                    maxX *
                    872;

            const y =
                282 -
                point.peak /
                    maxY *
                    248;

            return `${x.toFixed(2)},${y.toFixed(2)}`;
        }).join(" ");

        marks.push(`
            <polyline
                class="series-line series-a"
                points="${workingPolyline}"
            />

            <polyline
                class="series-line series-c"
                points="${peakPolyline}"
            />
        `);
    }

    for (const point of points) {
        const x =
            78 +
            point.x /
                maxX *
                872;

        const values = [
            [
                point.working,
                "series-a",
                "working set"
            ],
            [
                point.peak,
                "series-c",
                "peak working set"
            ],
        ];

        for (
            const [
                value,
                seriesClass,
                label
            ]
            of values
        ) {
            const y =
                282 -
                value /
                    maxY *
                    248;

            pointMarks.push(`
                <circle
                    class="data-point ${seriesClass}"
                    cx="${x.toFixed(2)}"
                    cy="${y.toFixed(2)}"
                    r="3.2"
                    data-tooltip="${escapeHtml(
                        `${get(
                            point.record,
                            "case.id",
                            ""
                        )} · ${label} · ${formatBytes(value)}`
                    )}"
                />
            `);
        }
    }

    marks.push(...pointMarks);

    return chartShell(
        "Working Set / Peak Working Set",
        "file size",
        "RAM",
        marks.join(""),
        `
            <span class="legend-item">
                <i class="legend-dot series-a"></i>
                Working set
            </span>

            <span class="legend-item">
                <i class="legend-dot series-c"></i>
                Peak working set
            </span>
        `,
        chartMeta(records)
    );
}

module.exports = {
    memoryChart
};
