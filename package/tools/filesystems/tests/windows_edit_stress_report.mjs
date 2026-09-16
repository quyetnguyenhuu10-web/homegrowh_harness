import fs from "node:fs";
import path from "node:path";

function usage() {
    console.log(
        "node windows_edit_stress_report.mjs --input results.csv " +
        "--output report.html"
    );
}

function parseArgs(argv) {
    const result = {};

    for (let index = 0; index < argv.length; index += 1) {
        const argument = argv[index];

        if (argument === "--help" || argument === "-h") {
            usage();
            process.exit(0);
        }

        if (argument === "--input" || argument === "--output") {
            if (index + 1 >= argv.length) {
                throw new Error(`${argument} requires a value`);
            }

            result[argument.slice(2)] = argv[++index];
            continue;
        }

        throw new Error(`unknown option: ${argument}`);
    }

    if (!result.input) {
        throw new Error("--input is required");
    }

    if (!result.output) {
        result.output = path.join(
            path.dirname(result.input),
            "report.html"
        );
    }

    return result;
}

function parseCsvLine(line) {
    const values = [];
    let value = "";
    let quoted = false;

    for (let index = 0; index < line.length; index += 1) {
        const character = line[index];

        if (quoted) {
            if (character === '"' && line[index + 1] === '"') {
                value += '"';
                index += 1;
            } else if (character === '"') {
                quoted = false;
            } else {
                value += character;
            }
        } else if (character === '"') {
            quoted = true;
        } else if (character === ",") {
            values.push(value);
            value = "";
        } else {
            value += character;
        }
    }

    values.push(value);
    return values;
}

function readRows(inputPath) {
    const lines = fs.readFileSync(inputPath, "utf8")
        .split(/\r?\n/)
        .filter((line) => line.length !== 0);

    if (lines.length < 2) {
        return [];
    }

    const headers = parseCsvLine(lines[0]);

    return lines.slice(1).map((line) => {
        const values = parseCsvLine(line);
        return Object.fromEntries(
            headers.map((header, index) => [header, values[index] ?? ""])
        );
    });
}

function number(row, key) {
    return Number(row[key] ?? 0);
}

function escapeHtml(value) {
    return String(value)
        .replaceAll("&", "&amp;")
        .replaceAll("<", "&lt;")
        .replaceAll(">", "&gt;")
        .replaceAll('"', "&quot;")
        .replaceAll("'", "&#39;");
}

function percentile(values, ratio) {
    if (values.length === 0) {
        return 0;
    }

    const ordered = [...values].sort((left, right) => left - right);
    const position = (ordered.length - 1) * ratio;
    const lower = Math.floor(position);
    const upper = Math.ceil(position);

    if (lower === upper) {
        return ordered[lower];
    }

    return ordered[lower] +
        (ordered[upper] - ordered[lower]) * (position - lower);
}

function groupRows(rows) {
    const groups = new Map();

    for (const row of rows) {
        if (!groups.has(row.scenario)) {
            groups.set(row.scenario, []);
        }

        groups.get(row.scenario).push(row);
    }

    return [...groups.entries()];
}

function outcomeChart(groups) {
    const width = 940;
    const height = 340;
    const left = 170;
    const top = 28;
    const chartWidth = width - left - 24;
    const chartHeight = 250;
    const maximum = Math.max(
        1,
        ...groups.map(([, rows]) => rows.length)
    );
    const barWidth = chartWidth / Math.max(1, groups.length) * 0.58;
    const colors = {
        pass: "var(--pass)",
        fail: "var(--fail)",
        inconclusive: "var(--inconclusive)",
    };
    const outcomeOrder = ["pass", "fail", "inconclusive"];
    const marks = [];

    groups.forEach(([scenario, rows], index) => {
        const counts = Object.fromEntries(
            outcomeOrder.map((outcome) => [
                outcome,
                rows.filter((row) => row.outcome === outcome).length,
            ])
        );
        let offset = 0;
        const x = left + (index + 0.5) * chartWidth / groups.length -
            barWidth / 2;

        for (const outcome of outcomeOrder) {
            const heightValue = counts[outcome] / maximum * chartHeight;
            const y = top + chartHeight - offset - heightValue;

            if (heightValue > 0) {
                marks.push(
                    `<rect x="${x.toFixed(2)}" y="${y.toFixed(2)}" ` +
                    `width="${barWidth.toFixed(2)}" ` +
                    `height="${heightValue.toFixed(2)}" ` +
                    `fill="${colors[outcome]}" ` +
                    `data-tooltip="${escapeHtml(`${scenario}: ${outcome} ${counts[outcome]}`)}"></rect>`
                );
            }

            offset += heightValue;
        }

        marks.push(
            `<text x="${(x + barWidth / 2).toFixed(2)}" ` +
            `y="${top + chartHeight + 24}" text-anchor="middle">` +
            `${escapeHtml(scenario)}</text>`
        );
    });

    return `<svg class="chart" viewBox="0 0 ${width} ${height}" ` +
        `role="img" aria-label="Outcomes by scenario">` +
        `<title>Outcomes by scenario</title>` +
        `<line class="axis" x1="${left}" y1="${top + chartHeight}" ` +
        `x2="${width - 24}" y2="${top + chartHeight}"></line>` +
        `<line class="axis" x1="${left}" y1="${top}" ` +
        `x2="${left}" y2="${top + chartHeight}"></line>` +
        `<text class="axis-title" x="18" y="${top + chartHeight / 2}" ` +
        `transform="rotate(-90 18 ${top + chartHeight / 2})">cases</text>` +
        marks.join("") +
        `</svg>`;
}

function latencyChart(groups) {
    const width = 940;
    const height = 330;
    const left = 170;
    const top = 28;
    const chartWidth = width - left - 24;
    const chartHeight = 245;
    const summaries = groups.map(([scenario, rows]) => ({
        scenario,
        p50: percentile(rows.map((row) => number(row, "duration_ms")), 0.5),
        p95: percentile(rows.map((row) => number(row, "duration_ms")), 0.95),
    }));
    const maximum = Math.max(
        1,
        ...summaries.map((summary) => summary.p95)
    );
    const rowHeight = chartHeight / Math.max(1, summaries.length);
    const marks = [];

    summaries.forEach((summary, index) => {
        const y = top + index * rowHeight + rowHeight / 2;
        const p50 = summary.p50 / maximum * chartWidth;
        const p95 = summary.p95 / maximum * chartWidth;

        marks.push(
            `<text x="${left - 12}" y="${y + 4}" text-anchor="end">` +
            `${escapeHtml(summary.scenario)}</text>` +
            `<line class="p95" x1="${left}" y1="${y}" ` +
            `x2="${left + p95}" y2="${y}"></line>` +
            `<circle class="p50" cx="${left + p50}" cy="${y}" r="5" ` +
            `data-tooltip="${escapeHtml(`${summary.scenario}: p50 ${summary.p50.toFixed(1)} ms, p95 ${summary.p95.toFixed(1)} ms`)}"></circle>`
        );
    });

    return `<svg class="chart" viewBox="0 0 ${width} ${height}" ` +
        `role="img" aria-label="Latency percentiles by scenario">` +
        `<title>Latency percentiles by scenario</title>` +
        `<line class="axis" x1="${left}" y1="${top + chartHeight}" ` +
        `x2="${width - 24}" y2="${top + chartHeight}"></line>` +
        `<line class="axis" x1="${left}" y1="${top}" ` +
        `x2="${left}" y2="${top + chartHeight}"></line>` +
        `<text class="axis-title" x="${width / 2}" ` +
        `y="${height - 10}" text-anchor="middle">duration (ms)</text>` +
        `<text class="legend" x="${left}" y="${top - 10}">p95 line / p50 dot</text>` +
        marks.join("") +
        `</svg>`;
}

function memoryChart(rows) {
    const width = 940;
    const height = 330;
    const left = 90;
    const right = 28;
    const top = 28;
    const bottom = 52;
    const plotWidth = width - left - right;
    const plotHeight = height - top - bottom;
    const points = rows.map((row) => ({
        x: number(row, "file_size_bytes"),
        y: number(row, "peak_working_set_bytes"),
        outcome: row.outcome,
        id: row.id,
    }));
    const maxX = Math.max(1, ...points.map((point) => point.x));
    const maxY = Math.max(1, ...points.map((point) => point.y));
    const color = {
        pass: "var(--pass)",
        fail: "var(--fail)",
        inconclusive: "var(--inconclusive)",
    };
    const marks = points.map((point) => {
        const x = left + point.x / maxX * plotWidth;
        const y = top + plotHeight - point.y / maxY * plotHeight;
        const label = `${point.id}: ${point.x} bytes, ${point.y} bytes peak`;

        return `<circle cx="${x.toFixed(2)}" cy="${y.toFixed(2)}" r="4" ` +
            `fill="${color[point.outcome] ?? "var(--neutral)"}" ` +
            `data-tooltip="${escapeHtml(label)}"></circle>`;
    });

    return `<svg class="chart" viewBox="0 0 ${width} ${height}" ` +
        `role="img" aria-label="Peak working set by file size">` +
        `<title>Peak working set by file size</title>` +
        `<line class="axis" x1="${left}" y1="${top + plotHeight}" ` +
        `x2="${left + plotWidth}" y2="${top + plotHeight}"></line>` +
        `<line class="axis" x1="${left}" y1="${top}" ` +
        `x2="${left}" y2="${top + plotHeight}"></line>` +
        `<text class="axis-title" x="${left + plotWidth / 2}" ` +
        `y="${height - 10}" text-anchor="middle">input file size (bytes)</text>` +
        `<text class="axis-title" x="16" y="${top + plotHeight / 2}" ` +
        `transform="rotate(-90 16 ${top + plotHeight / 2})">peak working set (bytes)</text>` +
        marks.join("") +
        `</svg>`;
}

function buildReport(rows, inputPath) {
    const groups = groupRows(rows);
    const counts = {
        total: rows.length,
        pass: rows.filter((row) => row.outcome === "pass").length,
        fail: rows.filter((row) => row.outcome === "fail").length,
        inconclusive: rows.filter((row) => row.outcome === "inconclusive").length,
    };
    const failures = rows.filter((row) => row.outcome !== "pass");
    const profile = rows[0]?.profile ?? "from CSV";
    const cards = Object.entries(counts).map(([key, value]) =>
        `<div class="stat"><span>${escapeHtml(key)}</span>` +
        `<strong>${value}</strong></div>`
    ).join("");
    const failureRows = failures.length === 0
        ? `<tr><td colspan="6">No failures or inconclusive cases.</td></tr>`
        : failures.slice(0, 40).map((row) =>
            `<tr><td>${escapeHtml(row.id)}</td>` +
            `<td>${escapeHtml(row.scenario)}</td>` +
            `<td>${escapeHtml(row.action)}</td>` +
            `<td>${escapeHtml(row.outcome)}</td>` +
            `<td>${escapeHtml(row.actual_note)}</td>` +
            `<td>${escapeHtml(row.detail)}</td></tr>`
        ).join("");

    return `<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Windows edit stress report</title>
<style>
:root { color-scheme: light dark; --bg: light-dark(#f7f8fa,#121417); --fg: light-dark(#20242a,#e8eaed); --muted: light-dark(#5d6570,#aab2bd); --line: light-dark(#d4d9df,#39414d); --pass: #35a56a; --fail: #d65c60; --inconclusive: #d49a3a; --neutral: #75808c; }
body { margin: 24px; background: var(--bg); color: var(--fg); font: 14px/1.45 system-ui, sans-serif; }
h1 { font-size: 22px; font-weight: 500; margin: 0 0 4px; }
p { color: var(--muted); margin: 0 0 18px; }
.stats { display: flex; flex-wrap: wrap; gap: 18px; margin: 18px 0; }
.stat { min-width: 100px; border-bottom: 1px solid var(--line); padding: 8px 0; }
.stat span { display: block; color: var(--muted); text-transform: uppercase; font-size: 11px; letter-spacing: .06em; }
.stat strong { display: block; font-size: 24px; font-weight: 500; }
section { margin: 28px 0; }
h2 { font-size: 16px; font-weight: 500; margin: 0 0 8px; }
.chart { width: 100%; max-width: 940px; height: auto; overflow: visible; }
.axis { stroke: var(--line); stroke-width: 1; }
.axis-title, .legend, text { fill: var(--fg); font-size: 12px; }
.p95 { stroke: var(--inconclusive); stroke-width: 6; stroke-linecap: round; }
.p50 { fill: var(--pass); }
rect, circle { cursor: help; }
table { border-collapse: collapse; width: 100%; max-width: 1100px; }
th, td { border-bottom: 1px solid var(--line); padding: 7px 8px; text-align: left; vertical-align: top; }
th { color: var(--muted); font-weight: 500; }
code { color: var(--muted); }
</style>
</head>
<body>
<h1>Windows edit stress report</h1>
<p>Input: <code>${escapeHtml(inputPath)}</code></p>
<div class="stats">${cards}</div>
<section><h2>Outcomes by scenario</h2>${outcomeChart(groups)}</section>
<section><h2>Latency</h2>${latencyChart(groups)}</section>
<section><h2>Memory versus input size</h2>${memoryChart(rows)}</section>
<section><h2>Failures and inconclusive cases</h2>
<table><thead><tr><th>id</th><th>scenario</th><th>action</th><th>outcome</th><th>note</th><th>detail</th></tr></thead>
<tbody>${failureRows}</tbody></table></section>
</body>
</html>
`;
}

try {
    const args = parseArgs(process.argv.slice(2));
    const rows = readRows(args.input);
    fs.mkdirSync(path.dirname(args.output), { recursive: true });
    fs.writeFileSync(args.output, buildReport(rows, args.input));

    const summary = {
        input: path.resolve(args.input),
        output: path.resolve(args.output),
        total: rows.length,
        pass: rows.filter((row) => row.outcome === "pass").length,
        fail: rows.filter((row) => row.outcome === "fail").length,
        inconclusive: rows.filter((row) => row.outcome === "inconclusive").length,
    };
    fs.writeFileSync(
        path.join(path.dirname(args.output), "summary.json"),
        `${JSON.stringify(summary, null, 2)}\n`
    );
} catch (error) {
    console.error(error instanceof Error ? error.message : error);
    process.exit(1);
}
