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

function categoricalChart(
    records,
    configuration
) {
    const labels = [
        ...new Set(
            records.map((record) =>
                String(
                    get(
                        record,
                        configuration.x,
                        "unknown"
                    )
                )
            )
        )
    ];

    const maximum = Math.max(
        1,
        ...records.map((record) =>
            number(
                record,
                configuration.y
            )
        )
    );

    const width =
        872 /
        Math.max(1, labels.length);

    const marks = [];

    /*
     * Y grid + labels
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

    labels.forEach(
        (label, index) => {
            const values =
                records.filter(
                    (record) =>
                        String(
                            get(
                                record,
                                configuration.x,
                                "unknown"
                            )
                        ) === label
                );

            const value =
                values.reduce(
                    (
                        total,
                        record
                    ) =>
                        total +
                        number(
                            record,
                            configuration.y
                        ),
                    0
                ) /
                Math.max(
                    1,
                    values.length
                );

            const barWidth =
                Math.min(
                    120,
                    width * 0.58
                );

            const center =
                78 +
                index * width +
                width / 2;

            const x =
                center -
                barWidth / 2;

            const height =
                value /
                maximum *
                248;

            const y =
                282 -
                height;

            marks.push(`
                <rect
                    class="bar"
                    x="${x.toFixed(2)}"
                    y="${y.toFixed(2)}"
                    width="${barWidth.toFixed(2)}"
                    height="${height.toFixed(2)}"
                    rx="7"
                    data-tooltip="${escapeHtml(
                        `${label}: ${formatNumber(value)}`
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
                    ${escapeHtml(
                        formatAxisNumber(value)
                    )}
                </text>

                <text
                    class="axis-number x-number"
                    x="${center.toFixed(2)}"
                    y="307"
                    text-anchor="middle"
                >
                    ${escapeHtml(label)}
                </text>
            `);
        }
    );

    return chartShell(
        configuration.title,
        configuration.xLabel,
        configuration.yLabel,
        marks.join(""),
        `
            <span class="legend-item">
                <i class="legend-dot series-b"></i>
                Mean
            </span>
        `,
        chartMeta(records, configuration)
    );
}

module.exports = {
    categoricalChart
};
