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

function scatterChart(records, configuration) {
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

    const indexed = records.map((record, position) => ({
        record,
        order: position + 1,
    }));

    const netOf = (entry) =>
        Math.max(
            0,
            number(entry.record, "metrics.ram_edit_bytes") -
                number(entry.record, "metrics.ram_harness_bytes")
        );

    const axisValue = (field, entry) => {
        if (field === "$index") {
            return entry.order;
        }

        if (field === "$net") {
            return netOf(entry);
        }

        return number(entry.record, field);
    };

    const formatTick = (field, value) => {
        if (field === "$index") {
            return formatAxisNumber(value);
        }

        if (field === "$net") {
            return formatBytes(value);
        }

        return String(field).includes("bytes")
            ? formatBytes(value)
            : formatAxisNumber(value);
    };

    const formatPoint = (field, value) => {
        if (field === "$index") {
            return String(value);
        }

        if (field === "$net") {
            return formatBytes(value);
        }

        return String(field).includes("bytes")
            ? formatBytes(value)
            : formatNumber(value);
    };

    const xs = indexed.map((entry) => axisValue(configuration.x, entry));
    const ys = indexed.map((entry) => axisValue(configuration.y, entry));

    const maxX = Math.max(1, ...xs);
    const maxY = Math.max(1, ...ys);

    const tickValues = (field, max) => {
        if (field !== "$index") {
            const ticks = [];

            for (let index = 0; index <= 5; index += 1) {
                ticks.push(max * index / 5);
            }

            return ticks;
        }

        const step = Math.max(1, Math.ceil(max / 6));
        const ticks = [];

        for (let value = 0; value < max; value += step) {
            ticks.push(value);
        }

        if (ticks[ticks.length - 1] !== max) {
            ticks.push(max);
        }

        return ticks;
    };

    const nets = indexed.map(netOf);

    const maxNet = Math.max(0, ...nets);

    const groups = new Map();

    for (const entry of indexed) {
        const group = configuration.group
            ? String(
                get(
                    entry.record,
                    configuration.group,
                    "unknown"
                )
            )
            : "series";

        if (!groups.has(group)) {
            groups.set(group, []);
        }

        groups.get(group).push(entry);
    }

    const seriesClasses = [
        "series-a",
        "series-b",
        "series-c",
        "series-d",
        "series-e",
    ];

    const marks = [];
    const gridCount = 5;

    for (const value of tickValues(configuration.y, maxY)) {
        const ratio = maxY > 0 ? value / maxY : 0;
        const y = 282 - ratio * 248;

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
                    formatTick(configuration.y, value)
                )}
            </text>
        `);
    }

    for (const value of tickValues(configuration.x, maxX)) {
        const ratio = maxX > 0 ? value / maxX : 0;
        const x = 78 + ratio * 872;

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
                    formatTick(configuration.x, value)
                )}
            </text>
        `);
    }

    const guideMarks = [];
    const lineMarks = [];
    const pointMarks = [];

    [...groups.entries()].forEach(
        ([group, groupPoints], groupIndex) => {
            const seriesClass =
                seriesClasses[
                    groupIndex %
                    seriesClasses.length
                ];

            const screen = groupPoints.map((entry) => {
                const rawX = axisValue(configuration.x, entry);
                const rawY = axisValue(configuration.y, entry);

                return {
                    entry,
                    rawX,
                    rawY,
                    x: 78 + rawX / maxX * 872,
                    y: 282 - rawY / maxY * 248,
                    net: netOf(entry),
                };
            }).sort((left, right) => left.rawX - right.rawX);

            if (configuration.guides) {
                for (const { x, y } of screen) {
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

            if (screen.length > 1 && configuration.line) {
                lineMarks.push(`
                    <polyline
                        class="series-line ${seriesClass}"
                        points="${screen.map(({ x, y }) =>
                            `${x.toFixed(2)},${y.toFixed(2)}`
                        ).join(" ")}"
                    />
                `);
            }

            for (const { entry, rawX, rawY, x, y, net } of screen) {
                const radius = configuration.bubble
                    ? 2.5 + 6.5 * (maxNet > 0 ? net / maxNet : 0)
                    : 3.2;

                const id = get(
                    entry.record,
                    "case.id",
                    ""
                );

                const tooltip =
                    `${id} · ` +
                    `${formatPoint(configuration.x, rawX)} · ` +
                    `${formatPoint(configuration.y, rawY)}` +
                    (
                        configuration.bubble
                            ? ` · edit thuần ≈ ${formatBytes(net)}`
                            : ""
                    );

                pointMarks.push(`
                    <circle
                        class="data-point ${seriesClass}"
                        cx="${x.toFixed(2)}"
                        cy="${y.toFixed(2)}"
                        r="${radius.toFixed(2)}"
                        data-tooltip="${escapeHtml(tooltip)}"
                    />
                `);
            }
        }
    );

    marks.push(...guideMarks, ...lineMarks, ...pointMarks);

    const legend = [...groups.keys()].map(
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
    ).join("") + (
        configuration.bubble
            ? `
                <span class="legend-item">
                    bubble = edit thuần
                </span>
            `
            : ""
    );

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
    scatterChart,
};
