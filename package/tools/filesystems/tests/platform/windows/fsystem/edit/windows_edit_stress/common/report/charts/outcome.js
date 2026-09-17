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

function outcomeChart(records) {
    const outcomes = [
        "pass",
        "fail",
        "inconclusive"
    ];

    const counts = outcomes.map(
        (outcome) =>
            records.filter(
                (record) =>
                    get(
                        record,
                        "result.outcome",
                        ""
                    ) === outcome
            ).length
    );

    const maximum = Math.max(
        1,
        ...counts
    );

    const width =
        872 /
        outcomes.length;

    const marks = [];

    /*
     * Y grid
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

    outcomes.forEach(
        (outcome, index) => {
            const count =
                counts[index];

            const center =
                78 +
                index * width +
                width / 2;

            const barWidth =
                Math.min(
                    150,
                    width * 0.55
                );

            const x =
                center -
                barWidth / 2;

            const height =
                count /
                maximum *
                248;

            const y =
                282 -
                height;

            const seriesClass =
                outcome === "pass"
                    ? "series-a"
                    : outcome === "fail"
                        ? "series-d"
                        : "series-c";

            marks.push(`
                <rect
                    class="bar ${seriesClass}"
                    x="${x.toFixed(2)}"
                    y="${y.toFixed(2)}"
                    width="${barWidth.toFixed(2)}"
                    height="${height.toFixed(2)}"
                    rx="7"
                    data-tooltip="${escapeHtml(
                        `${outcome}: ${count}`
                    )}"
                />

                <text
                    class="bar-value"
                    x="${center.toFixed(2)}"
                    y="${Math.max(
                        20,
                        y - 9
                    ).toFixed(2)}"
                    text-anchor="middle"
                >
                    ${count}
                </text>

                <text
                    class="axis-number x-number"
                    x="${center.toFixed(2)}"
                    y="307"
                    text-anchor="middle"
                >
                    ${escapeHtml(outcome)}
                </text>
            `);
        }
    );

    return chartShell(
        "Correctness outcomes",
        "outcome",
        "case count",
        marks.join(""),
        `
            <span class="legend-item">
                <i class="legend-dot series-a"></i>
                Pass
            </span>

            <span class="legend-item">
                <i class="legend-dot series-d"></i>
                Fail
            </span>

            <span class="legend-item">
                <i class="legend-dot series-c"></i>
                Inconclusive
            </span>
        `,
        chartMeta(records)
    );
}

module.exports = {
    outcomeChart
};
