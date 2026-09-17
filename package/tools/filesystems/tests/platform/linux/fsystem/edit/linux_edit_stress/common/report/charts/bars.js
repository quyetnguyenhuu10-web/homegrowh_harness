"use strict";

const {
    escapeHtml,
    get,
    number,
    formatBytes,
    formatNumber,
    formatAxisNumber,
} = require("../utils");

const { chartShell, chartMeta } = require("./shell");

function barsChart(records, configuration) {
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

    const isBytes = String(configuration.value).includes("bytes");

    const formatValue = (value) =>
        isBytes
            ? formatBytes(value)
            : formatNumber(value);

    const formatTick = (value) =>
        isBytes
            ? formatBytes(value)
            : formatAxisNumber(value);

    const maximum = Math.max(
        1,
        ...records.map((record) =>
            number(record, configuration.value)
        )
    );

    const seriesClass = configuration.series ?? "series-b";

    const width = 872 / records.length;

    const barWidth = Math.max(
        4,
        Math.min(44, width * 0.6)
    );

    const labelEvery = records.length > 15
        ? Math.ceil(records.length / 12)
        : 1;

    const marks = [];
    const gridCount = 5;

    for (
        let index = 0;
        index <= gridCount;
        index += 1
    ) {
        const ratio = index / gridCount;
        const y = 282 - ratio * 248;
        const value = maximum * ratio;

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
                ${escapeHtml(formatTick(value))}
            </text>
        `);
    }

    records.forEach((record, position) => {
        const value = number(record, configuration.value);
        const center = 78 + position * width + width / 2;
        const x = center - barWidth / 2;
        const height = value / maximum * 248;
        const y = 282 - height;

        const id = get(record, "case.id", "");

        marks.push(`
            <rect
                class="bar ${seriesClass}"
                x="${x.toFixed(2)}"
                y="${y.toFixed(2)}"
                width="${barWidth.toFixed(2)}"
                height="${Math.max(1, height).toFixed(2)}"
                rx="4"
                data-tooltip="${escapeHtml(
                    `Edit ${position + 1} · ${id} · ${formatValue(value)}`
                )}"
            />
        `);

        if (records.length <= 15) {
            marks.push(`
                <text
                    class="bar-value"
                    x="${center.toFixed(2)}"
                    y="${Math.max(20, y - 7).toFixed(2)}"
                    text-anchor="middle"
                >
                    ${escapeHtml(formatValue(value))}
                </text>
            `);
        }

        if (position % labelEvery === 0) {
            marks.push(`
                <text
                    class="axis-number x-number"
                    x="${center.toFixed(2)}"
                    y="307"
                    text-anchor="middle"
                >
                    ${escapeHtml(`Edit ${position + 1}`)}
                </text>
            `);
        }
    });

    return chartShell(
        configuration.title,
        configuration.xLabel,
        configuration.yLabel,
        marks.join(""),
        `
            <span class="legend-item">
                <i class="legend-dot ${seriesClass}"></i>
                ${escapeHtml(configuration.yLabel ?? "value")}
            </span>
        `,
        chartMeta(records, configuration)
    );
}

module.exports = {
    barsChart,
};
