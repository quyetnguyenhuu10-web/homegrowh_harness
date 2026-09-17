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

/*
 * Compact chart system
 *
 * SVG coordinate system:
 *
 * left   = 78
 * right  = 950
 * top    = 34
 * bottom = 282
 *
 * Extra space is reserved for:
 * - large axis values
 * - grid
 * - axis titles
 * - legend
 */

function chartHeader(options) {
    const title = options.title;
    const subtitle = options.subtitle;
    const meta = options.meta ?? "";
    const legend = options.legend ?? "";
    const controls = options.controls ?? "";

    return `
            <div class="chart-header">
                <div>
                    <h2>${escapeHtml(title)}</h2>
                    <div class="chart-subtitle">
                        ${subtitle}
                    </div>
                    ${
                        meta
                            ? `<div class="chart-meta">
                                ${escapeHtml(meta)}
                               </div>`
                            : ""
                    }
                </div>

                ${
                    legend
                        ? `<div class="chart-legend">
                            ${legend}
                           </div>`
                        : ""
                }

                ${controls}

                <button
                    type="button"
                    class="collapse-btn"
                    data-collapse
                    aria-expanded="true"
                    aria-label="Collapse chart"
                >−</button>
            </div>`;
}

function chartShell(
    title,
    xLabel,
    yLabel,
    body,
    legend = "",
    meta = "",
    hideAxes = false
) {
    const subtitle =
        `${escapeHtml(xLabel)}` +
        `<span>×</span>` +
        `${escapeHtml(yLabel)}`;

    return `
        <section class="chart-card" data-chart-card>

            ${chartHeader({ title, subtitle, meta, legend })}

            <div class="chart-wrap" data-collapsible>
                <svg
                    class="chart"
                    viewBox="0 0 1000 350"
                    role="img"
                    aria-label="${escapeHtml(title)}"
                >
                    <title>
                        ${escapeHtml(title)}
                    </title>

                    ${
                        hideAxes
                            ? ""
                            : `<!-- horizontal axis -->
                    <line
                        class="axis"
                        x1="78"
                        y1="282"
                        x2="950"
                        y2="282"
                    />

                    <!-- vertical axis -->
                    <line
                        class="axis"
                        x1="78"
                        y1="34"
                        x2="78"
                        y2="282"
                    />`
                    }

                    ${body}

                    ${
                        hideAxes
                            ? ""
                            : `<!-- X axis title -->
                    <text
                        class="axis-title"
                        x="514"
                        y="340"
                        text-anchor="middle"
                    >
                        ${escapeHtml(xLabel)}
                    </text>

                    <!-- Y axis title -->
                    <text
                        class="axis-title"
                        x="18"
                        y="158"
                        text-anchor="middle"
                        transform="rotate(-90 18 158)"
                    >
                        ${escapeHtml(yLabel)}
                    </text>`
                    }
                </svg>
            </div>
        </section>
    `;
}

function chartMeta(records, configuration = {}) {
    if (records.length === 0) {
        return "";
    }

    let editPeak = 0;
    let harness = 0;

    for (const record of records) {
        const edit = number(
            record,
            "metrics.ram_edit_bytes"
        );

        const held = number(
            record,
            "metrics.ram_harness_bytes"
        );

        if (edit > editPeak) {
            editPeak = edit;
        }

        if (held > harness) {
            harness = held;
        }
    }

    const parts = [
        `RAM edit peak ${formatBytes(editPeak)}`,
        `harness ${formatBytes(harness)}`,
    ];

    if (configuration.refusals) {
        const refused = records.filter(
            (record) =>
                get(
                    record,
                    "result.replace_attempted",
                    true
                ) === false
        ).length;

        parts.push(
            `từ chối sửa: ${refused}/${records.length}`
        );
    }

    return parts.join(" · ");
}

module.exports = {
    chartShell,
    chartHeader,
    chartMeta
};
