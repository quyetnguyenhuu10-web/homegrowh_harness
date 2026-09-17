"use strict";

const {
    escapeHtml,
    get,
    number,
} = require("../utils");

const { chartShell, chartHeader, chartMeta } = require("./shell");

const NICE_STEPS_MB = [
    0.1, 0.2, 0.25, 0.5, 1, 2, 2.5, 5,
    10, 20, 25, 50, 100, 200, 500,
];

function niceTicksMB(maxMB) {
    const raw = Math.max(maxMB, 0.001) / 5;
    const step = NICE_STEPS_MB.find((candidate) => candidate >= raw)
        ?? Math.ceil(raw);
    const top = Math.ceil(maxMB / step - 1e-9) * step || step;
    const ticks = [];

    for (let value = 0; value < top; value += step) {
        ticks.push(Math.round(value * 100) / 100);
    }

    ticks.push(Math.round(top * 100) / 100);

    return { step, top, ticks };
}

function formatTickMB(value) {
    return String(Math.round(value * 10) / 10);
}

function ramtimeChart(records, configuration) {
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

    const totals = records.map((record) =>
        number(record, "metrics.ram_harness_bytes") +
        number(record, "metrics.ram_edit_bytes")
    );

    const maxTotal = Math.max(1, ...totals);
    const { top, ticks } = niceTicksMB(maxTotal / 1048576);

    const yMarks = ticks.map((tick) => {
        const y = 282 - tick / top * 248;

        return `
            <text
                class="axis-number y-number"
                x="68"
                y="${(y + 5).toFixed(2)}"
                text-anchor="end"
            >
                ${escapeHtml(formatTickMB(tick))}
            </text>
        `;
    }).join("");

    const payload = {
        topMB: top,
        ticksMB: ticks,
        xLabel: configuration.xLabel ?? "lần edit",
        cases: records.map((record) => ({
            id: String(get(record, "case.id", "")),
            refused: get(
                record,
                "result.replace_attempted",
                true
            ) === false,
            total:
                number(record, "metrics.ram_harness_bytes") +
                number(record, "metrics.ram_edit_bytes"),
            ms: number(record, "metrics.duration_ms"),
        })),
    };

    const subtitle =
        `${escapeHtml(configuration.xLabel ?? "")}` +
        `<span>×</span>` +
        `${escapeHtml(configuration.yLabel ?? "")}`;

    return `
        <section class="chart-card" data-chart-card>

            ${chartHeader({
                title: configuration.title,
                subtitle,
                meta: chartMeta(records, configuration),
                legend: `
                    <span class="legend-item">
                        <i class="legend-dot series-b"></i>
                        Edit thành công (không bị từ chối)
                    </span>

                    <span class="legend-item">
                        <i class="legend-dot series-d"></i>
                        Edit bị từ chối (có can thiệp trước hoặc trong edit)
                    </span>

                    <span class="legend-item">
                        số trong cột = thời gian (giây)
                    </span>
                `,
                controls: `
                    <div class="zoom-controls" data-zoom>
                        <button
                            type="button"
                            data-zoom-out
                            aria-label="Zoom out"
                        >−</button>
                        <span
                            class="zoom-label"
                            data-zoom-label
                        >100%</span>
                        <button
                            type="button"
                            data-zoom-in
                            aria-label="Zoom in"
                        >+</button>
                        <button
                            type="button"
                            data-zoom-reset
                        >Reset</button>
                    </div>
                `,
            })}

            <div class="chart-wrap" data-collapsible>
                <div class="ramtime-body" data-ramtime>
                    <svg
                        class="ramtime-yaxis"
                        viewBox="0 0 78 320"
                        style="width:78px;height:320px"
                        aria-hidden="true"
                    >
                        ${yMarks}

                        <line
                            class="axis"
                            x1="78"
                            y1="34"
                            x2="78"
                            y2="282"
                        />

                        <text
                            class="axis-title"
                            x="14"
                            y="158"
                            text-anchor="middle"
                            transform="rotate(-90 14 158)"
                        >
                            ${escapeHtml(configuration.yLabel ?? "")}
                        </text>
                    </svg>

                    <div
                        class="ramtime-scroll"
                        data-ramtime-scroll
                    >
                        <svg
                            class="ramtime-plot"
                            data-ramtime-plot
                            viewBox="0 0 300 320"
                            style="width:300px;height:320px"
                        ></svg>
                    </div>

                    <script
                        type="application/json"
                        data-ramtime-data
                    >${JSON.stringify(payload).replace(/</g, "\\u003c")}</script>
                </div>
            </div>
        </section>
    `;
}

module.exports = {
    ramtimeChart,
};
