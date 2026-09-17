"use strict";

const fs = require("node:fs");
const path = require("node:path");
const {
    escapeHtml,
    get,
    number,
    formatBytes,
    formatByteField,
    formatNumber,
    formatAxisNumber,
} = require("./utils");
const { chartShell, chartMeta } = require("./charts/shell");
const { numericChart } = require("./charts/numeric");
const { ramtimeChart } = require("./charts/ramtime");
const { barsChart } = require("./charts/bars");
const { scatterChart } = require("./charts/scatter");
const { pieChart } = require("./charts/pie");
const { groupedBarChart } = require("./charts/grouped");
const { categoricalChart } = require("./charts/categorical");
const { memoryChart } = require("./charts/memory");
const { outcomeChart } = require("./charts/outcome");

const REPORT_DIR = __dirname;

const assetCache = new Map();

function readAsset(name) {
    if (assetCache.has(name)) {
        return assetCache.get(name);
    }

    const text = fs.readFileSync(
        path.join(REPORT_DIR, name),
        "utf8"
    );

    assetCache.set(name, text);

    return text;
}

function recordsTable(records) {
    if (records.length === 0) {
        return `
            <div class="empty-state">
                <div class="empty-title">No result records</div>
                <div class="empty-text">
                    The benchmark did not produce any result records.
                </div>
            </div>
        `;
    }

    const rows = records.map((record) => {
        const result = get(
            record,
            "result",
            {}
        );

        const dimensions = get(
            record,
            "dimensions",
            {}
        );

        const metrics = get(
            record,
            "metrics",
            {}
        );

        const interference = get(
            record,
            "interference",
            {}
        );

        const outcome =
            result.outcome ?? "";

        const outcomeClass =
            outcome === "pass"
                ? "status-pass"
                : outcome === "fail"
                    ? "status-fail"
                    : "status-inconclusive";

        return `
            <tr data-outcome="${escapeHtml(outcome)}">
                <td class="mono">
                    ${escapeHtml(
                        get(record, "case.id", "")
                    )}
                </td>

                <td>
                    ${escapeHtml(
                        get(record, "case.scenario", "")
                    )}
                </td>

                <td>
                    ${escapeHtml(
                        get(record, "case.expected", "")
                    )}
                </td>

                <td>
                    <span class="status ${outcomeClass}">
                        ${escapeHtml(outcome)}
                    </span>
                </td>

                <td>
                    ${escapeHtml(
                        result.actual_note ?? ""
                    )}
                </td>

                <td>
                    ${escapeHtml(
                        dimensions.pattern_position ?? ""
                    )}
                </td>

                <td class="numeric-cell">
                    ${formatNumber(
                        metrics.duration_ms ?? 0
                    )}
                </td>

                <td>
                    ${escapeHtml(
                        interference.action ?? "none"
                    )}
                </td>

                <td class="detail-cell">
                    ${escapeHtml(
                        record.detail ?? ""
                    )}
                </td>
            </tr>
        `;
    }).join("");

    const chips = [
        ["all", "All", records.length],
        ["pass", "Pass", records.filter(
            (record) =>
                get(record, "result.outcome", "") === "pass"
        ).length],
        ["fail", "Fail", records.filter(
            (record) =>
                get(record, "result.outcome", "") === "fail"
        ).length],
        ["inconclusive", "Inconclusive", records.filter(
            (record) =>
                get(record, "result.outcome", "") === "inconclusive"
        ).length],
    ].map(([value, label, count], index) => `
        <button
            type="button"
            class="chip${index === 0 ? " is-active" : ""}"
            data-outcome-filter="${value}"
            aria-pressed="${index === 0 ? "true" : "false"}"
        >
            ${label}
            <b>${count}</b>
        </button>
    `).join("");

    return `
        <div class="table-block" data-table-block>
        <div class="table-toolbar">
            <input
                class="table-search"
                type="search"
                placeholder="Search records…"
                aria-label="Search records"
                data-table-search
            />
            <div class="chip-row">
                ${chips}
            </div>
        </div>

        <div class="table-shell">
            <div class="table-scroll">
                <table data-records-table>
                    <thead>
                        <tr>
                            <th data-sort="text">Case</th>
                            <th data-sort="text">Scenario</th>
                            <th data-sort="text">Expected</th>
                            <th data-sort="text">Outcome</th>
                            <th data-sort="text">Note</th>
                            <th data-sort="text">Position</th>
                            <th data-sort="number">Time</th>
                            <th data-sort="text">Interference</th>
                            <th data-sort="text">Detail</th>
                        </tr>
                    </thead>

                    <tbody>
                        ${rows}
                    </tbody>
                </table>
            </div>
        </div>
        </div>
    `;
}

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

function applyFilter(records, filter) {
    if (!filter) {
        return records;
    }

    const filters = Array.isArray(filter)
        ? filter
        : [filter];

    return records.filter((record) =>
        filters.every((entry) => {
            const actual = String(
                get(record, entry.field, "")
            );

            if ("notEquals" in entry) {
                return actual !== String(entry.notEquals);
            }

            return actual === String(entry.equals);
        })
    );
}

function resolveCharts(configuration) {
    if (Array.isArray(configuration.charts)) {
        return configuration.charts;
    }

    if (typeof configuration.chart === "string") {
        return [{
            kind: configuration.chart,
            title: configuration.title,
            x: configuration.x,
            y: configuration.y,
            group: configuration.group,
            xLabel: configuration.xLabel,
            yLabel: configuration.yLabel,
            barNote: configuration.barNote,
        }];
    }

    if (
        configuration.chart &&
        typeof configuration.chart === "object"
    ) {
        return [configuration.chart];
    }

    return [];
}

function renderChart(records, configuration, chart) {
    const scoped = applyFilter(
        records,
        chart.filter ?? configuration.filter
    );

    const kind = chart.kind ?? configuration.chart;

    if (kind === "numeric") {
        return numericChart(scoped, chart);
    }

    if (kind === "ramtime") {
        return ramtimeChart(scoped, chart);
    }

    if (kind === "bars") {
        return barsChart(scoped, chart);
    }

    if (kind === "scatter") {
        return scatterChart(scoped, chart);
    }

    if (kind === "pie") {
        return pieChart(scoped, chart);
    }

    if (kind === "grouped") {
        return groupedBarChart(scoped, chart);
    }

    if (kind === "categorical") {
        return categoricalChart(scoped, chart);
    }

    if (kind === "memory") {
        return memoryChart(scoped);
    }

    if (kind === "outcome") {
        return outcomeChart(scoped);
    }

    return "";
}

function countOutcomes(records) {
    return {
        total: records.length,

        pass: records.filter(
            (record) =>
                get(
                    record,
                    "result.outcome",
                    ""
                ) === "pass"
        ).length,

        fail: records.filter(
            (record) =>
                get(
                    record,
                    "result.outcome",
                    ""
                ) === "fail"
        ).length,

        inconclusive: records.filter(
            (record) =>
                get(
                    record,
                    "result.outcome",
                    ""
                ) === "inconclusive"
        ).length,
    };
}

function statsHtml(counts) {
    return `
        <div class="stat total">
            <div class="stat-label">
                Total
            </div>
            <div class="stat-value">
                ${counts.total}
            </div>
        </div>

        <div class="stat pass">
            <div class="stat-label">
                Pass
            </div>
            <div class="stat-value">
                ${counts.pass}
            </div>
        </div>

        <div class="stat fail">
            <div class="stat-label">
                Fail
            </div>
            <div class="stat-value">
                ${counts.fail}
            </div>
        </div>

        <div class="stat inconclusive">
            <div class="stat-label">
                Inconclusive
            </div>
            <div class="stat-value">
                ${counts.inconclusive}
            </div>
        </div>
    `;
}

function chartsHtml(records, configuration) {
    return resolveCharts(configuration).map((chart) =>
        renderChart(
            records,
            configuration,
            chart
        )
    ).join("");
}

function pageSummaryHtml(records) {
    const validInterference = records.filter(
        (record) =>
            String(
                get(record, "interference.action", "")
            ) !== "none" &&
            get(
                record,
                "interference.succeeded",
                false
            ) === true
    );

    const clean = records.filter(
        (record) =>
            String(
                get(record, "interference.action", "")
            ) === "none"
    );

    const refused = validInterference.filter(
        (record) =>
            get(
                record,
                "result.replace_attempted",
                true
            ) === false
    ).length;

    const cleanPass = clean.filter(
        (record) =>
            get(
                record,
                "result.outcome",
                ""
            ) === "pass"
    ).length;

    const percent = (part, whole) =>
        whole > 0
            ? `${(part / whole * 100).toFixed(1)}%`
            : "0.0%";

    return `
        <div class="summary-strip">
            <div class="summary-chip interf">
                <span class="summary-chip-title">
                    Interference
                </span>
                <span class="summary-chip-num">
                    ${refused} / ${validInterference.length} (${percent(refused, validInterference.length)})
                </span>
                <span class="summary-chip-sub">
                    Rejected (trước hoặc trong edit)
                </span>
            </div>

            <div class="summary-chip clean">
                <span class="summary-chip-title">
                    Clean
                </span>
                <span class="summary-chip-num">
                    ${cleanPass} / ${clean.length} (${percent(cleanPass, clean.length)})
                </span>
                <span class="summary-chip-sub">
                    Success (không có interference)
                </span>
            </div>

            <div class="summary-chip total">
                <span class="summary-chip-title">
                    Tổng số lần edit
                </span>
                <span class="summary-chip-num">
                    ${records.length}
                </span>
            </div>

            <details class="note-details">
                <summary>
                    Lưu ý
                </summary>
                <ul>
                    <li>
                        Chỉ tính interference xảy ra trước hoặc trong edit; muộn thì bị loại.
                    </li>
                    <li>
                        RAM mỗi cột = RAM harness resident + phần edit đẩy thêm.
                    </li>
                    <li>
                        Nhấn vào từng cột để xem chi tiết.
                    </li>
                </ul>
            </details>
        </div>
    `;
}

function formatPageValue(field, value) {
    return String(field).includes("bytes")
        ? formatBytes(Number(value))
        : String(value);
}

function reportPages(configuration, records) {
    const pages = configuration.pages;

    if (!pages || !pages.field || records.length === 0) {
        return [{
            key: "all",
            label: "",
            records,
        }];
    }

    const raw = records.map((record) =>
        get(record, pages.field, "unknown")
    );

    const numeric =
        raw.length > 0 &&
        raw.every(
            (value) =>
                value !== "" &&
                Number.isFinite(Number(value))
        );

    const values = numeric
        ? [
            ...new Set(
                raw.map((value) => Number(value))
            )
        ].sort((left, right) => left - right)
        : [...new Set(raw.map(String))];

    return values.map((value) => ({
        key: String(value),

        label:
            `${pages.title ?? pages.field}: ` +
            `${formatPageValue(pages.field, value)}`,

        records: records.filter(
            (record) =>
                String(
                    get(record, pages.field, "unknown")
                ) === String(value)
        ),
    }));
}

function reportBody(
    configuration,
    records,
    args,
    resultsDirectory
) {
    const charts = resolveCharts(configuration);

    const pageList = reportPages(configuration, records);

    const pageNav = pageList.length > 1
        ? `<div class="tabbar" role="tablist" aria-label="Pages">${
            pageList.map((page, index) =>
                `<button
                    type="button"
                    class="tab-btn${index === 0 ? " is-active" : ""}"
                    data-tab="size-page-${index}"
                    role="tab"
                    aria-selected="${index === 0 ? "true" : "false"}"
                >${escapeHtml(page.label)}</button>`
            ).join("")
        }</div>`
        : "";

    const multiPage = pageList.length > 1;

    const bodySections = pageList.map((page, index) => `
        <section
            class="size-page"
            id="size-page-${index}"
            data-page
            role="tabpanel"
            ${multiPage && index > 0 ? "hidden" : ""}
        >
            ${
                configuration.pageSummary
                    ? pageSummaryHtml(page.records)
                    : `<div class="stats">
                        ${statsHtml(countOutcomes(page.records))}
                       </div>`
            }

            ${chartsHtml(page.records, configuration)}

            <section
                class="result-section"
                ${index === 0 ? `id="records"` : ""}
            >

                <div class="result-section-header">

                    <h2 class="result-section-title">
                        Result records
                    </h2>

                    <div
                        class="result-section-count"
                        data-visible-count
                    >
                        ${page.records.length} records
                    </div>

                </div>

                ${recordsTable(page.records)}

            </section>
        </section>
    `).join("");

    const reportType =
        charts.length > 0
            ? "chart + result table"
            : "report table only";

    return `
<!doctype html>

<html lang="en" data-theme="auto">

<head>

<meta charset="utf-8">

<meta
    name="viewport"
    content="width=device-width,initial-scale=1"
/>

<title>
    ${escapeHtml(configuration.title)}
</title>

<style>
${readAsset("styles.css")}</style>

</head>

<body>

<div class="page">

    <header class="header">

        <div class="header-title">

            <h1>
                ${escapeHtml(
                    configuration.title
                )}
            </h1>

            <div class="subtitle">
                Profile:
                <strong>
                    ${escapeHtml(args.profile)}
                </strong>

                · ${escapeHtml(reportType)}
            </div>

        </div>

        <nav class="header-links">

            <a
                class="header-link"
                href="#records"
            >
                Records
            </a>

            <a
                class="header-link"
                href="results/schema.json"
            >
                Schema
            </a>

            <a
                class="header-link"
                href="results/results.jsonl"
            >
                Raw JSONL
            </a>

            <button
                type="button"
                class="header-link theme-toggle"
                data-theme-toggle
                aria-label="Theme: Auto (click to change)"
            >
                <span aria-hidden="true">◐</span>
                <span data-theme-label>Auto</span>
            </button>

        </nav>

    </header>

    ${pageNav}

    ${bodySections}

    <footer class="footer">

        <span>
            Results directory:
            <code>
                ${escapeHtml(
                    resultsDirectory
                )}
            </code>
        </span>

        <span>
            Generated from result schema v1
        </span>

    </footer>

</div>

<div
    class="chart-tooltip"
    data-tooltip-box
    hidden
></div>

<button
    type="button"
    class="back-to-top"
    data-back-to-top
    aria-label="Back to top"
    hidden
>↑</button>

<script>
${readAsset("app.js")}</script>

</body>

</html>
`;
}

module.exports = {
    reportBody
};
