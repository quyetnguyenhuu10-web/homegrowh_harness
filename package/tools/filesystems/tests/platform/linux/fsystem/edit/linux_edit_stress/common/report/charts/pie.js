"use strict";

const {
    escapeHtml,
    get,
} = require("../utils");

const { chartShell, chartMeta } = require("./shell");

function polarToCartesian(cx, cy, radius, degrees) {
    const radians = (degrees - 90) * Math.PI / 180;

    return [
        cx + radius * Math.cos(radians),
        cy + radius * Math.sin(radians),
    ];
}

function pieChart(records, configuration) {
    if (records.length === 0) {
        return chartShell(
            configuration.title,
            "",
            "",
            `<text class="empty-chart"
                x="514"
                y="160"
                text-anchor="middle">
                No data
             </text>`,
            "",
            "",
            true
        );
    }

    const total = records.length;

    const mode = configuration.pieBy ?? "refusal";

    let slices;

    if (mode === "outcome") {
        const passed = records.filter(
            (record) =>
                get(
                    record,
                    "result.outcome",
                    ""
                ) === "pass"
        ).length;

        slices = [
            {
                count: passed,
                seriesClass: "series-a",
                label: "Thành công",
            },
            {
                count: total - passed,
                seriesClass: "series-d",
                label: "Thất bại",
            },
        ];
    } else {
        const refused = records.filter(
            (record) =>
                get(
                    record,
                    "result.replace_attempted",
                    true
                ) === false
        ).length;

        slices = [
            {
                count: refused,
                seriesClass: "series-d",
                label: "Từ chối sửa",
            },
            {
                count: total - refused,
                seriesClass: "series-a",
                label: "Đã sửa",
            },
        ];
    }

    const cx = 514;
    const cy = 158;
    const radius = 110;
    const marks = [];
    let angle = 0;

    slices.forEach((slice) => {
        if (slice.count <= 0) {
            return;
        }

        const fraction = slice.count / total;
        const sweep = fraction * 360;
        const percent = (fraction * 100).toFixed(1);

        const tooltip =
            `${slice.label}: ${slice.count}/${total} (${percent}%)`;

        if (fraction >= 1) {
            marks.push(`
                <circle
                    class="pie-slice ${slice.seriesClass}"
                    cx="${cx}"
                    cy="${cy}"
                    r="${radius}"
                    data-tooltip="${escapeHtml(tooltip)}"
                />
            `);
            return;
        }

        const [x1, y1] = polarToCartesian(cx, cy, radius, angle);
        const [x2, y2] = polarToCartesian(cx, cy, radius, angle + sweep);
        const largeArc = sweep > 180 ? 1 : 0;

        marks.push(`
            <path
                class="pie-slice ${slice.seriesClass}"
                d="M ${cx} ${cy} L ${x1.toFixed(2)} ${y1.toFixed(2)}
                   A ${radius} ${radius} 0 ${largeArc} 1
                   ${x2.toFixed(2)} ${y2.toFixed(2)} Z"
                data-tooltip="${escapeHtml(tooltip)}"
            />
        `);

        if (fraction >= 0.08) {
            const [tx, ty] = polarToCartesian(
                cx,
                cy,
                radius * 0.62,
                angle + sweep / 2
            );

            marks.push(`
                <text
                    class="pie-label"
                    x="${tx.toFixed(2)}"
                    y="${(ty + 5).toFixed(2)}"
                    text-anchor="middle"
                >
                    ${escapeHtml(`${percent}%`)}
                </text>
            `);
        }

        angle += sweep;
    });

    const legend = slices.map(
        (slice) => {
            const percent = total > 0
                ? (slice.count / total * 100).toFixed(1)
                : "0.0";

            return `
                <span class="legend-item">
                    <i class="legend-dot ${slice.seriesClass}"></i>
                    ${escapeHtml(
                        `${slice.label}: ${slice.count} (${percent}%)`
                    )}
                </span>
            `;
        }
    ).join("");

    return chartShell(
        configuration.title,
        "",
        "",
        marks.join(""),
        legend,
        chartMeta(records, configuration),
        true
    );
}

module.exports = {
    pieChart,
};
