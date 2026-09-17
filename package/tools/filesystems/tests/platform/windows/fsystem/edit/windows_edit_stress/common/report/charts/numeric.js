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

function numericChart(records, configuration) {
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

    const points = records.map((record) => ({
        record,

        x: number(
            record,
            configuration.x
        ),

        y: number(
            record,
            configuration.y
        ),

        group: configuration.group
            ? String(
                get(
                    record,
                    configuration.group,
                    "unknown"
                )
            )
            : "series",
    }));

    const maxX = Math.max(
        1,
        ...points.map(
            (point) => point.x
        )
    );

    const maxY = Math.max(
        1,
        ...points.map(
            (point) => point.y
        )
    );

    const groups = new Map();

    for (const point of points) {
        if (!groups.has(point.group)) {
            groups.set(
                point.group,
                []
            );
        }

        groups.get(point.group).push(point);
    }

    const seriesClasses = [
        "series-a",
        "series-b",
        "series-c",
        "series-d",
        "series-e",
    ];

    const marks = [];

    const formatYTick = (tick) =>
        String(configuration.y).includes("bytes")
            ? formatBytes(tick)
            : formatAxisNumber(tick);

    const formatXTick = (tick) =>
        String(configuration.x).includes("bytes")
            ? formatBytes(tick)
            : formatAxisNumber(tick);

    /*
     * Grid + Y labels
     */
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
                    formatYTick(value)
                )}
            </text>
        `);
    }

    /*
     * X labels
     */
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
                    formatXTick(value)
                )}
            </text>
        `);
    }

    /*
     * Series
     */
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

            groupPoints.sort(
                (left, right) =>
                    left.x - right.x
            );

            const screenPoints = groupPoints.map((point) => {
                const x =
                    78 +
                    point.x /
                        maxX *
                        872;

                const y =
                    282 -
                    point.y /
                        maxY *
                        248;

                return { point, x, y };
            });

            if (configuration.guides !== false) {
                for (const { x, y } of screenPoints) {
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

            if (
                screenPoints.length > 1 &&
                configuration.line !== false
            ) {
                const polyline =
                    screenPoints.map(({ x, y }) =>
                        `${x.toFixed(2)},${y.toFixed(2)}`
                    ).join(" ");

                lineMarks.push(`
                    <polyline
                        class="series-line ${seriesClass}"
                        points="${polyline}"
                    />
                `);
            }

            for (const { point, x, y } of screenPoints) {
                const id = get(
                    point.record,
                    "case.id",
                    ""
                );

                const tooltip =
                    `${id} · ` +
                    `${formatByteField(configuration.x, point.x)} · ` +
                    `${formatByteField(configuration.y, point.y)}`;

                pointMarks.push(`
                    <circle
                        class="data-point ${seriesClass}"
                        cx="${x.toFixed(2)}"
                        cy="${y.toFixed(2)}"
                        r="3.2"
                        data-tooltip="${escapeHtml(
                            tooltip
                        )}"
                    />
                `);
            }
        }
    );

    marks.push(...guideMarks, ...lineMarks, ...pointMarks);

    /*
     * Legend
     */
    const legend = [
        ...groups.keys()
    ].map(
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
    numericChart
};
