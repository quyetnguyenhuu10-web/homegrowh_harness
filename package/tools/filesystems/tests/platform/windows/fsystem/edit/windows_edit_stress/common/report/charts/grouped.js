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

function groupedBarChart(records, configuration) {
    if (records.length === 0) {
        return chartShell(
            configuration.title,
            configuration.xLabel,
            configuration.yLabel,
            `<text class="empty-chart"
                x="514"
                y="160"
                text-anchor="middle">
                No data
             </text>`
        );
    }

    const rawXs = records.map((record) =>
        get(record, configuration.x, "unknown")
    );

    const numericX =
        rawXs.length > 0 &&
        rawXs.every(
            (value) =>
                value !== "" &&
                Number.isFinite(Number(value))
        );

    const xValues = numericX
        ? [
            ...new Set(
                rawXs.map((value) => Number(value))
            )
        ].sort((left, right) => left - right)
        : (
            Array.isArray(configuration.xOrder) &&
            configuration.xOrder.length > 0
                ? [...configuration.xOrder]
                : [...new Set(rawXs.map(String))]
        );

    const groups = [
        ...new Set(
            records.map((record) =>
                String(
                    get(
                        record,
                        configuration.group,
                        "unknown"
                    )
                )
            )
        )
    ].sort();

    const maximum = Math.max(
        1,
        ...records.map((record) =>
            number(record, configuration.y)
        )
    );

    const isBytes = String(configuration.x).includes("bytes");

    const formatXLabel = (value) =>
        numericX
            ? (
                isBytes
                    ? formatBytes(value)
                    : formatAxisNumber(value)
            )
            : String(value);

    const width =
        872 /
        Math.max(1, xValues.length);

    const gap = 8;

    const barWidth = Math.max(
        8,
        Math.min(
            48,
            (width * 0.64 - gap * (groups.length - 1)) /
                Math.max(1, groups.length)
        )
    );

    const clusterWidth =
        barWidth * groups.length +
        gap * (groups.length - 1);

    const seriesClasses = [
        "series-a",
        "series-b",
        "series-c",
        "series-d",
        "series-e",
    ];

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
            maximum * ratio;

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
                    formatAxisNumber(value)
                )}
            </text>
        `);
    }

    xValues.forEach((xValue, xIndex) => {
        const center =
            78 +
            xIndex * width +
            width / 2;

        const clusterStart =
            center -
            clusterWidth / 2;

        groups.forEach((group, groupIndex) => {
            const values = records.filter(
                (record) =>
                    (
                        numericX
                            ? number(record, configuration.x) === xValue
                            : String(
                                get(
                                    record,
                                    configuration.x,
                                    "unknown"
                                )
                            ) === String(xValue)
                    ) &&
                    String(
                        get(
                            record,
                            configuration.group,
                            "unknown"
                        )
                    ) === group
            );

            const value =
                values.reduce(
                    (total, record) =>
                        total +
                        number(record, configuration.y),
                    0
                ) /
                Math.max(1, values.length);

            const seriesClass =
                seriesClasses[
                    groupIndex %
                    seriesClasses.length
                ];

            const x =
                clusterStart +
                groupIndex * (barWidth + gap);

            const height =
                value /
                maximum *
                248;

            const y =
                282 -
                height;

            const noteField =
                configuration.barNote;

            const noteRaw =
                noteField && values.length > 0
                    ? Number(
                        get(
                            values[0],
                            noteField,
                            NaN
                        )
                    )
                    : NaN;

            const noteText =
                Number.isFinite(noteRaw)
                    ? formatBytes(noteRaw)
                    : "";

            const tooltip =
                noteText
                    ? `${formatXLabel(xValue)} · ${group}: ` +
                        `${formatNumber(value)} ` +
                        `(n=${values.length}; thay thế: ${noteText})`
                    : `${formatXLabel(xValue)} · ${group}: ` +
                        `${formatNumber(value)} ` +
                        `(n=${values.length})`;

            marks.push(`
                <rect
                    class="bar ${seriesClass}"
                    x="${x.toFixed(2)}"
                    y="${y.toFixed(2)}"
                    width="${barWidth.toFixed(2)}"
                    height="${Math.max(1, height).toFixed(2)}"
                    rx="4"
                    data-tooltip="${escapeHtml(tooltip)}"
                />

                <text
                    class="bar-value"
                    x="${(x + barWidth / 2).toFixed(2)}"
                    y="${Math.max(
                        20,
                        y - 7
                    ).toFixed(2)}"
                    text-anchor="middle"
                >
                    ${escapeHtml(
                        formatAxisNumber(value)
                    )}
                </text>
                ${noteText && height > 20 ? `
                <text
                    class="bar-bytes"
                    x="${(x + barWidth / 2).toFixed(2)}"
                    y="${(y + 15).toFixed(2)}"
                    text-anchor="middle"
                >
                    ${escapeHtml(noteText)}
                </text>
                ` : ""}
            `);
        });

        marks.push(`
            <text
                class="axis-number x-number"
                x="${center.toFixed(2)}"
                y="307"
                text-anchor="middle"
            >
                ${escapeHtml(formatXLabel(xValue))}
            </text>
        `);
    });

    const legend = groups.map(
        (group, index) => `
            <span class="legend-item">
                <i class="legend-dot ${
                    seriesClasses[
                        index %
                        seriesClasses.length
                    ]
                }"></i>
                ${escapeHtml(group)}
            </span>
        `
    ).join("");

    return chartShell(
        configuration.title,
        configuration.xLabel,
        configuration.yLabel,
        marks.join(""),
        legend,
        chartMeta(records, configuration)
    );
}

module.exports = {
    groupedBarChart
};
