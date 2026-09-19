#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Interference Rejection Rate HTML Report Generator
- Reads results.jsonl (edit cases + per-type summaries)
- Generates result.html with interactive Chart.js, Dark/Light mode,
  draggable Split View and Hide/Show title (same interactions as bench_ram).
Run: python gen_html.py
"""

import json
import re
from datetime import datetime
from pathlib import Path

BASE = Path(__file__).resolve().parent
JSONL = BASE / "results.jsonl"
HTML_OUT = BASE / "result.html"

TYPE_ORDER = ["lock", "modify", "delete", "rename"]


def _parse_line_lenient(line: str):
    """Strict json.loads first; regex fallback for unescaped Windows paths."""
    try:
        return json.loads(line)
    except Exception:
        pass

    if '"type":"summary"' not in line and '"type": "summary"' not in line:
        if '"type":"edit"' not in line and '"type": "edit"' not in line:
            return None

    def text(key):
        m = re.search(r'"' + key + r'"\s*:\s*"([^"]*)"', line)
        return m.group(1) if m else ""

    def num(key):
        m = re.search(r'"' + key + r'"\s*:\s*([0-9.\-]+)', line)
        return float(m.group(1)) if m else 0.0

    def integer(key):
        m = re.search(r'"' + key + r'"\s*:\s*(\d+)', line)
        return int(m.group(1)) if m else 0

    def boolean(key):
        m = re.search(r'"' + key + r'"\s*:\s*(true|false)', line)
        return (m.group(1) == "true") if m else False

    if '"summary"' in line:
        return {
            "type": "summary",
            "interference": text("interference"),
            "scheduled": integer("scheduled"),
            "applied": integer("applied"),
            "filtered": integer("filtered"),
            "rejected": integer("rejected"),
            "accepted": integer("accepted"),
            "rejection_rate_percent": num("rejection_rate_percent"),
        }

    return {
        "type": "edit",
        "interference": text("interference"),
        "point": text("point"),
        "edit": integer("edit"),
        "path": "",
        "note": text("note"),
        "error": integer("error"),
        "replace_attempted": boolean("replace_attempted"),
        "interference_applied": boolean("interference_applied"),
        "reject_edit": boolean("reject_edit"),
        "time_seconds": num("time_seconds"),
    }


def load_results():
    cases = []
    summaries = {}
    with open(JSONL, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            obj = _parse_line_lenient(line)
            if not obj:
                continue
            if obj.get("type") == "edit":
                cases.append(obj)
            elif obj.get("type") == "summary":
                summaries[obj.get("interference", "")] = obj
    ordered = [t for t in TYPE_ORDER if t in summaries]
    ordered += [t for t in summaries if t not in ordered]
    return cases, [summaries[t] for t in ordered]


def main():
    if not JSONL.exists():
        raise FileNotFoundError(f"Not found: {JSONL}")

    cases, summaries = load_results()
    if not summaries:
        raise ValueError("results.jsonl contains no type=summary rows.")

    total_cases = len(cases)
    total_rejected = sum(1 for c in cases if c.get("reject_edit"))
    total_accepted = total_cases - total_rejected
    rates = [float(s.get("rejection_rate_percent", 0.0)) for s in summaries]
    avg_rate = sum(rates) / len(rates) if rates else 0.0
    times = [float(c.get("time_seconds", 0.0)) for c in cases]
    avg_time = sum(times) / len(times) if times else 0.0
    max_time = max(times) if times else 0.0
    files_per_type = (
        max(int(s.get("scheduled", 0)) for s in summaries) if summaries else 0
    )
    y_max = max([int(s.get("scheduled", 0)) for s in summaries] + [1])

    chart_payload = [
        {
            "type": s.get("interference", ""),
            "scheduled": int(s.get("scheduled", 0)),
            "applied": int(s.get("applied", 0)),
            "filtered": int(s.get("filtered", 0)),
            "rejected": int(s.get("rejected", 0)),
            "accepted": int(s.get("accepted", 0)),
            "rate": round(float(s.get("rejection_rate_percent", 0.0)), 2),
        }
        for s in summaries
    ]

    summary_rows = "\n".join(
        f'<tr><td><strong>{d["type"].upper()}</strong></td>'
        f'<td>{d["scheduled"]}</td><td>{d["applied"]}</td>'
        f'<td>{d["filtered"]}</td><td>{d["accepted"]}</td>'
        f'<td>{d["rejected"]}</td>'
        f'<td><span class="badge badge-danger">{d["rate"]:.2f}%</span></td></tr>'
        for d in chart_payload
    )

    detail_rows = "\n".join(
        f'<tr><td>#{c.get("edit", 0)}</td>'
        f'<td>{c.get("interference", "")}</td>'
        f'<td>{c.get("point", "")}</td>'
        f'<td>{c.get("note", "")}</td>'
        f'<td>{int(c.get("error", 0))}</td>'
        f'<td>{"yes" if c.get("replace_attempted") else "no"}</td>'
        f'<td>{"yes" if c.get("reject_edit") else "no"}</td>'
        f'<td>{float(c.get("time_seconds", 0.0)):.4f} s</td></tr>'
        for c in sorted(
            cases, key=lambda r: (r.get("interference", ""), r.get("edit", 0))
        )
    )

    now_str = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

    html_content = f"""<!DOCTYPE html>
<html lang="vi" data-theme="dark">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Báo cáo Tỷ lệ Từ chối Can thiệp (Interference Rejection Rate)</title>
<script src="https://cdnjs.cloudflare.com/ajax/libs/Chart.js/4.4.1/chart.umd.js"></script>
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
<link href="https://fonts.googleapis.com/css2?family=Inter:wght@300;400;500;600;700&family=JetBrains+Mono:wght@400;500;600&display=swap" rel="stylesheet">
<style>
  :root {{
    --font-sans: 'Inter', -apple-system, BlinkMacSystemFont, sans-serif;
    --font-mono: 'JetBrains Mono', monospace;

    /* Dark Theme (Default) */
    --bg-base: #090d16;
    --bg-surface: #111827;
    --bg-card: #1f2937;
    --bg-card-hover: #374151;
    --border-color: rgba(255, 255, 255, 0.08);
    --border-accent: rgba(59, 130, 246, 0.3);

    --text-main: #f9fafb;
    --text-muted: #9ca3af;
    --text-subtle: #6b7280;

    --accent-blue: #3b82f6;
    --accent-blue-glow: rgba(59, 130, 246, 0.15);
    --accent-pink: #ec4899;
    --accent-pink-glow: rgba(236, 72, 153, 0.15);
    --accent-green: #10b981;
    --accent-amber: #f59e0b;
    --accent-red: #ef4444;

    --grid-line: rgba(255, 255, 255, 0.05);
    --shadow-card: 0 4px 20px -2px rgba(0, 0, 0, 0.5);
  }}

  [data-theme="light"] {{
    --bg-base: #f8fafc;
    --bg-surface: #ffffff;
    --bg-card: #f1f5f9;
    --bg-card-hover: #e2e8f0;
    --border-color: rgba(0, 0, 0, 0.08);
    --border-accent: rgba(37, 99, 235, 0.3);

    --text-main: #0f172a;
    --text-muted: #475569;
    --text-subtle: #94a3b8;

    --accent-blue: #2563eb;
    --accent-blue-glow: rgba(37, 99, 235, 0.1);
    --accent-pink: #db2777;
    --accent-pink-glow: rgba(219, 39, 119, 0.1);
    --accent-green: #059669;
    --accent-amber: #d97706;
    --accent-red: #dc2626;

    --grid-line: rgba(0, 0, 0, 0.05);
    --shadow-card: 0 4px 12px -2px rgba(0, 0, 0, 0.05);
  }}

  * {{ box-sizing: border-box; margin: 0; padding: 0; transition: background-color 0.25s, border-color 0.25s; }}

  body {{
    font-family: var(--font-sans);
    background-color: var(--bg-base);
    color: var(--text-main);
    min-height: 100vh;
    padding: 24px;
    line-height: 1.5;
  }}

  .container {{
    max-width: 1600px;
    margin: 0 auto;
    display: flex;
    flex-direction: column;
    gap: 20px;
  }}

  /* Header Section */
  .header {{
    display: flex;
    justify-content: space-between;
    align-items: center;
    background: var(--bg-surface);
    padding: 20px 24px;
    border-radius: 16px;
    border: 1px solid var(--border-color);
    box-shadow: var(--shadow-card);
  }}

  .header-titles h1 {{
    font-size: 16px;
    font-weight: 600;
    letter-spacing: -0.01em;
    display: flex;
    align-items: center;
    gap: 8px;
  }}

  .header-sub {{
    font-size: 13px;
    color: var(--text-muted);
    margin-top: 4px;
    font-family: var(--font-mono);
  }}

  .controls {{
    display: flex;
    align-items: center;
    flex-wrap: wrap;
    gap: 8px;
  }}

  #panel-chart .panel-header {{
    flex-wrap: wrap;
    gap: 10px;
  }}

  .btn {{
    background: var(--bg-card);
    color: var(--text-main);
    border: 1px solid var(--border-color);
    padding: 8px 14px;
    border-radius: 8px;
    font-size: 12px;
    font-weight: 600;
    cursor: pointer;
    display: inline-flex;
    align-items: center;
    gap: 6px;
  }}

  .btn:hover {{
    background: var(--bg-card-hover);
    border-color: var(--accent-blue);
  }}

  .btn.active {{
    background: var(--accent-blue);
    color: #ffffff;
    border-color: var(--accent-blue);
  }}

  /* KPI Cards Grid */
  .kpi-grid {{
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(220px, 1fr));
    gap: 16px;
  }}

  .kpi-card {{
    background: var(--bg-surface);
    border: 1px solid var(--border-color);
    border-radius: 12px;
    padding: 12px;
    display: flex;
    flex-direction: column;
    justify-content: space-between;
    box-shadow: var(--shadow-card);
    position: relative;
    overflow: hidden;
  }}

  .kpi-card::before {{
    content: '';
    position: absolute;
    top: 0; left: 0; right: 0; height: 3px;
    background: transparent;
  }}

  .kpi-card.red-card::before {{ background: var(--accent-red); }}
  .kpi-card.green-card::before {{ background: var(--accent-green); }}
  .kpi-card.blue-card::before {{ background: var(--accent-blue); }}
  .kpi-card.amber-card::before {{ background: var(--accent-amber); }}

  .kpi-label {{
    font-size: 10px;
    font-weight: 500;
    color: var(--text-muted);
    text-transform: uppercase;
    letter-spacing: 0.04em;
  }}

  .kpi-value {{
    font-family: var(--font-mono);
    font-size: 18px;
    font-weight: 700;
    margin: 6px 0 2px;
    color: var(--text-main);
  }}

  .kpi-subtext {{
    font-size: 10px;
    color: var(--text-subtle);
    font-family: var(--font-mono);
  }}

  /* Compact KPI strip inside the chart panel */
  .kpi-inside-chart {{
    grid-template-columns: repeat(4, minmax(0, 1fr));
    gap: 10px;
    margin-bottom: 14px;
  }}

  .kpi-inside-chart .kpi-card {{
    background: var(--bg-card);
    border-radius: 10px;
    padding: 10px 12px;
    box-shadow: none;
  }}

  .kpi-inside-chart .kpi-value {{
    font-size: 15px;
    margin: 4px 0 2px;
  }}

  @media (max-width: 900px) {{
    .kpi-inside-chart {{
      grid-template-columns: repeat(2, minmax(0, 1fr));
    }}
  }}

  /* Layout Main Section */
  .main-content {{
    display: flex;
    flex-direction: column;
    gap: 20px;
    --split-pct: 60%;
  }}

  body.split-layout .main-content {{
    display: grid;
    grid-template-columns: var(--split-pct) 12px minmax(0, 1fr);
    gap: 0;
    align-items: stretch;
  }}

  body.split-layout .panel-chart {{
    border-top-right-radius: 0;
    border-bottom-right-radius: 0;
    min-width: 0;
  }}

  body.split-layout .panel-table {{
    border-top-left-radius: 0;
    border-bottom-left-radius: 0;
    border-left: none;
    min-width: 0;
    display: flex;
    flex-direction: column;
  }}

  /* Draggable vertical splitter (only visible in split mode) */
  .splitter {{
    display: none;
    cursor: col-resize;
    position: relative;
    background: transparent;
    touch-action: none;
    user-select: none;
    z-index: 5;
  }}

  body.split-layout .splitter {{
    display: block;
  }}

  .splitter::after {{
    content: '';
    position: absolute;
    top: 0; bottom: 0; left: 4px; right: 4px;
    border-radius: 4px;
    background: var(--border-color);
    transition: background 0.15s;
  }}

  .splitter:hover::after,
  .splitter.dragging::after {{
    background: var(--accent-blue);
  }}

  .splitter::before {{
    content: '⋮';
    position: absolute;
    top: 50%;
    left: 50%;
    transform: translate(-50%, -50%);
    color: var(--text-subtle);
    font-size: 14px;
    letter-spacing: -2px;
    z-index: 1;
  }}

  .splitter.dragging::before {{
    color: #fff;
  }}

  body.split-resizing {{
    cursor: col-resize !important;
  }}

  body.split-resizing * {{
    user-select: none !important;
    pointer-events: none !important;
  }}

  body.split-resizing .splitter {{
    pointer-events: auto !important;
  }}

  .panel {{
    background: var(--bg-surface);
    border: 1px solid var(--border-color);
    border-radius: 16px;
    padding: 20px;
    box-shadow: var(--shadow-card);
  }}

  .panel-header {{
    display: flex;
    justify-content: space-between;
    align-items: center;
    margin-bottom: 16px;
  }}

  .panel-title {{
    font-size: 12px;
    font-weight: 600;
    text-transform: uppercase;
    letter-spacing: 0.05em;
    color: var(--text-muted);
    display: flex;
    align-items: center;
    gap: 8px;
  }}

  /* Hide-title mode: collapse page header + chart panel title for max chart space */
  body.hide-titles .header {{
    display: none;
  }}

  body.hide-titles #panel-chart .panel-title {{
    display: none;
  }}

  body.hide-titles #panel-chart .panel-header {{
    margin-bottom: 8px;
    justify-content: flex-end;
  }}

  .chart-wrapper {{
    position: relative;
    height: 420px;
    width: 100%;
    min-width: 0;
  }}

  body.split-layout .chart-wrapper {{
    height: 480px;
  }}

  /* Data Table */
  .table-container {{
    max-height: 500px;
    overflow-y: auto;
    border-radius: 8px;
    border: 1px solid var(--border-color);
    margin-bottom: 16px;
  }}

  .table-container:last-child {{
    margin-bottom: 0;
  }}

  body.split-layout .table-container {{
    max-height: 560px;
    flex: 1 1 auto;
  }}

  table.data-table {{
    width: 100%;
    border-collapse: collapse;
    font-family: var(--font-mono);
    font-size: 12px;
    text-align: right;
  }}

  table.data-table th, table.data-table td {{
    padding: 10px 14px;
    border-bottom: 1px solid var(--border-color);
  }}

  table.data-table th {{
    background: var(--bg-card);
    color: var(--text-muted);
    position: sticky;
    top: 0;
    font-weight: 600;
    z-index: 2;
  }}

  table.data-table th:first-child, table.data-table td:first-child {{
    text-align: center;
  }}

  table.data-table td:nth-child(2), table.data-table th:nth-child(2) {{
    text-align: left;
  }}

  table.data-table tbody tr:hover {{
    background: var(--bg-card-hover);
  }}

  .badge {{
    display: inline-block;
    padding: 2px 8px;
    border-radius: 999px;
    font-size: 11px;
    font-weight: 600;
  }}

  .badge-danger {{
    background: var(--accent-pink-glow);
    color: var(--accent-pink);
  }}

  .badge-ok {{
    background: rgba(16, 185, 129, 0.12);
    color: var(--accent-green);
  }}

  .sub-title {{
    font-size: 12px;
    font-weight: 600;
    text-transform: uppercase;
    letter-spacing: 0.05em;
    color: var(--text-muted);
    margin: 4px 0 8px;
  }}

  @media (max-width: 1024px) {{
    body.split-layout .main-content {{
      grid-template-columns: 1fr;
    }}
    body.split-layout .splitter {{
      display: none;
    }}
    body.split-layout .panel-chart {{
      border-radius: 16px;
    }}
    body.split-layout .panel-table {{
      border-radius: 16px;
      border-left: 1px solid var(--border-color);
    }}
  }}
</style>
</head>
<body>

<div class="container">
  <!-- Header -->
  <header class="header">
    <div class="header-titles">
      <h1>
        Báo cáo Tỷ lệ Từ chối Can thiệp (Interference Rejection)
      </h1>
      <div class="header-sub">
        Generated: {now_str} &bull; Source: {JSONL.name} &bull; {total_cases} cases / {len(summaries)} loại nhiễu
      </div>
    </div>
  </header>

  <!-- Main Content Panel -->
  <main class="main-content" id="main-content">
    <!-- Chart Panel -->
    <div class="panel panel-chart" id="panel-chart">
      <div class="panel-header">
        <span class="panel-title">📊 Chấp nhận vs Từ chối theo Dạng Nhiễu</span>
        <div class="controls">
          <button class="btn" id="title-toggle" onclick="toggleChartTitle()">👁 Ẩn tiêu đề</button>
          <button class="btn" id="theme-toggle" onclick="toggleTheme()">☀️ Chế độ Sáng</button>
          <button class="btn" id="split-toggle" onclick="toggleSplitView()">📑 Chia đôi</button>
          <button class="btn" onclick="toggleDataset(0)">Từ chối</button>
          <button class="btn" onclick="toggleDataset(1)">Chấp nhận</button>
        </div>
      </div>
      <!-- KPI cards moved inside the chart panel -->
      <section class="kpi-grid kpi-inside-chart">
        <div class="kpi-card blue-card">
          <div class="kpi-label">Tổng số case</div>
          <div class="kpi-value">{total_cases} / {len(summaries)} loại</div>
          <div class="kpi-subtext">{files_per_type} file &times; {len(summaries)} kiểu nhiễu</div>
        </div>
        <div class="kpi-card red-card">
          <div class="kpi-label">Tỷ lệ từ chối trung bình</div>
          <div class="kpi-value">{avg_rate:.2f}%</div>
          <div class="kpi-subtext">min {min(rates):.2f}% | max {max(rates):.2f}%</div>
        </div>
        <div class="kpi-card green-card">
          <div class="kpi-label">Từ chối / Chấp nhận</div>
          <div class="kpi-value">{total_rejected} / {total_accepted}</div>
          <div class="kpi-subtext">applied == scheduled toàn bộ</div>
        </div>
        <div class="kpi-card amber-card">
          <div class="kpi-label">Thời gian TB / case</div>
          <div class="kpi-value">{avg_time:.4f} s</div>
          <div class="kpi-subtext">max spike {max_time:.4f} s</div>
        </div>
      </section>
      <div class="chart-wrapper" id="chart-wrapper">
        <canvas id="rejectionChart"></canvas>
      </div>
    </div>

    <!-- Draggable vertical splitter between chart and table -->
    <div class="splitter" id="splitter" title="Kéo để đổi cỡ biểu đồ / bảng"></div>

    <!-- Data Table Panel -->
    <div class="panel panel-table" id="panel-table">
      <div class="panel-header">
        <span class="panel-title">📋 Chi tiết ({total_cases} cases)</span>
      </div>
      <div class="sub-title">Tổng hợp theo loại nhiễu</div>
      <div class="table-container" style="flex: 0 1 auto;">
        <table class="data-table">
          <thead>
            <tr>
              <th>Nhiễu</th>
              <th>Lên lịch</th>
              <th>Áp dụng</th>
              <th>Lọc bỏ</th>
              <th>Chấp nhận</th>
              <th>Từ chối</th>
              <th>Tỷ lệ từ chối</th>
            </tr>
          </thead>
          <tbody>
            {summary_rows}
          </tbody>
        </table>
      </div>
      <div class="sub-title">Nhật ký từng case</div>
      <div class="table-container">
        <table class="data-table">
          <thead>
            <tr>
              <th>#</th>
              <th>Nhiễu</th>
              <th>Điểm can thiệp</th>
              <th>Note</th>
              <th>Error</th>
              <th>Replace?</th>
              <th>Reject?</th>
              <th>Time</th>
            </tr>
          </thead>
          <tbody>
            {detail_rows}
          </tbody>
        </table>
      </div>
    </div>
  </main>
</div>

<script>
const rawData = {json.dumps(chart_payload, ensure_ascii=False)};
const yMax = {y_max};
let chartInstance = null;

function getThemeColors() {{
  const isDark = document.documentElement.getAttribute('data-theme') === 'dark';
  return {{
    textColor: isDark ? '#f9fafb' : '#0f172a',
    mutedColor: isDark ? '#9ca3af' : '#475569',
    gridColor: isDark ? 'rgba(255, 255, 255, 0.05)' : 'rgba(0, 0, 0, 0.05)',
    red: isDark ? '#ec4899' : '#dc2626',
    redBg: isDark ? 'rgba(236, 72, 153, 0.55)' : 'rgba(220, 38, 38, 0.75)',
    green: isDark ? '#10b981' : '#059669',
    greenBg: isDark ? 'rgba(16, 185, 129, 0.45)' : 'rgba(5, 150, 105, 0.7)',
    cardBg: isDark ? '#111827' : '#ffffff'
  }};
}}

function initChart() {{
  const ctx = document.getElementById('rejectionChart').getContext('2d');
  const colors = getThemeColors();

  if (chartInstance) chartInstance.destroy();

  chartInstance = new Chart(ctx, {{
    type: 'bar',
    data: {{
      labels: rawData.map(d => d.type.toUpperCase()),
      datasets: [
        {{
          label: 'Từ chối (Rejected)',
          data: rawData.map(d => d.rejected),
          backgroundColor: colors.redBg,
          borderColor: colors.red,
          borderWidth: 1,
          borderRadius: 4
        }},
        {{
          label: 'Chấp nhận (Accepted)',
          data: rawData.map(d => d.accepted),
          backgroundColor: colors.greenBg,
          borderColor: colors.green,
          borderWidth: 1,
          borderRadius: 4
        }}
      ]
    }},
    options: {{
      responsive: true,
      maintainAspectRatio: false,
      animation: {{ duration: 300 }},
      interaction: {{ mode: 'index', intersect: false }},
      plugins: {{
        legend: {{ display: false }},
        tooltip: {{
          backgroundColor: colors.cardBg,
          titleColor: colors.textColor,
          bodyColor: colors.mutedColor,
          borderColor: colors.gridColor,
          borderWidth: 1,
          padding: 12,
          titleFont: {{ family: 'JetBrains Mono', size: 13, weight: 'bold' }},
          bodyFont: {{ family: 'JetBrains Mono', size: 12 }}
        }}
      }},
      scales: {{
        x: {{
          stacked: true,
          grid: {{ display: false }},
          ticks: {{ color: colors.mutedColor, font: {{ family: 'JetBrains Mono', size: 11 }} }}
        }},
        y: {{
          stacked: true,
          beginAtZero: true,
          max: yMax,
          grid: {{ color: colors.gridColor }},
          ticks: {{ color: colors.mutedColor, font: {{ family: 'JetBrains Mono', size: 11 }}, stepSize: Math.max(1, Math.ceil(yMax / 8)) }}
        }}
      }}
    }}
  }});
}}

function toggleTheme() {{
  const html = document.documentElement;
  const current = html.getAttribute('data-theme');
  const target = current === 'dark' ? 'light' : 'dark';
  html.setAttribute('data-theme', target);
  document.getElementById('theme-toggle').textContent = target === 'dark' ? '☀️ Chế độ Sáng' : '🌙 Chế độ Tối';
  initChart();
}}

function refreshChartSize() {{
  if (!chartInstance) return;
  // Chart.js caches canvas size: force a synchronous reflow then resize.
  // Multiple passes cover grid transition + font/layout settle.
  try {{
    void document.getElementById('chart-wrapper').offsetWidth;
    chartInstance.resize();
    chartInstance.update('none');
  }} catch (e) {{}}
  requestAnimationFrame(() => {{
    try {{ chartInstance.resize(); }} catch (e) {{}}
  }});
  setTimeout(() => {{ try {{ chartInstance.resize(); }} catch (e) {{}} }}, 50);
  setTimeout(() => {{ try {{ chartInstance.resize(); }} catch (e) {{}} }}, 250);
}}

function toggleSplitView() {{
  const btn = document.getElementById('split-toggle');
  const on = document.body.classList.toggle('split-layout');
  if (btn) {{
    btn.classList.toggle('active', on);
    btn.textContent = on ? '📑 Xếp chồng' : '📑 Chia đôi';
  }}
  // Force layout recalculation immediately so the split shows without refresh.
  void document.body.offsetWidth;
  refreshChartSize();
}}

function initSplitter() {{
  const splitter = document.getElementById('splitter');
  const main = document.getElementById('main-content');
  if (!splitter || !main) return;
  let dragging = false;
  let startPct = 60;

  const pctFromEvent = (clientX) => {{
    const rect = main.getBoundingClientRect();
    if (rect.width <= 0) return startPct;
    const relX = clientX - rect.left;
    let pct = (relX / rect.width) * 100;
    // Keep both panes usable.
    pct = Math.min(80, Math.max(20, pct));
    return pct;
  }};

  const onMove = (clientX) => {{
    const pct = pctFromEvent(clientX);
    main.style.setProperty('--split-pct', pct + '%');
    refreshChartSize();
  }};

  splitter.addEventListener('mousedown', (e) => {{
    if (!document.body.classList.contains('split-layout')) return;
    dragging = true;
    const computed = getComputedStyle(main).getPropertyValue('--split-pct').trim();
    startPct = parseFloat(computed) || 60;
    splitter.classList.add('dragging');
    document.body.classList.add('split-resizing');
    e.preventDefault();
  }});

  window.addEventListener('mousemove', (e) => {{
    if (!dragging) return;
    onMove(e.clientX);
  }});

  window.addEventListener('mouseup', () => {{
    if (!dragging) return;
    dragging = false;
    splitter.classList.remove('dragging');
    document.body.classList.remove('split-resizing');
    refreshChartSize();
  }});

  // Touch support
  splitter.addEventListener('touchstart', (e) => {{
    if (!document.body.classList.contains('split-layout')) return;
    dragging = true;
    splitter.classList.add('dragging');
    document.body.classList.add('split-resizing');
    e.preventDefault();
  }}, {{ passive: false }});

  window.addEventListener('touchmove', (e) => {{
    if (!dragging || !e.touches.length) return;
    onMove(e.touches[0].clientX);
  }}, {{ passive: true }});

  window.addEventListener('touchend', () => {{
    if (!dragging) return;
    dragging = false;
    splitter.classList.remove('dragging');
    document.body.classList.remove('split-resizing');
    refreshChartSize();
  }});

  // Double-click splitter to reset to 60/40
  splitter.addEventListener('dblclick', () => {{
    main.style.setProperty('--split-pct', '60%');
    refreshChartSize();
  }});
}}

function toggleChartTitle() {{
  const btn = document.getElementById('title-toggle');
  const hidden = document.body.classList.toggle('hide-titles');
  if (btn) {{
    btn.classList.toggle('active', hidden);
    btn.textContent = hidden ? '👁 Hiện tiêu đề' : '👁 Ẩn tiêu đề';
  }}
  void document.body.offsetWidth;
  refreshChartSize();
}}

function toggleDataset(index) {{
  if (!chartInstance) return;
  const meta = chartInstance.getDatasetMeta(index);
  meta.hidden = meta.hidden === null ? !chartInstance.data.datasets[index].hidden : null;
  chartInstance.update();
}}

document.addEventListener('DOMContentLoaded', () => {{
  initChart();
  initSplitter();
  window.addEventListener('resize', () => {{ if (chartInstance) chartInstance.resize(); }});
}});
</script>
</body>
</html>
"""

    HTML_OUT.write_text(html_content, encoding="utf-8")
    print(f"[SUCCESS] Interference rejection HTML report generated at: {HTML_OUT}")
    print(
        f"Stats: {total_cases} cases | {len(summaries)} types | "
        f"avg rejection {avg_rate:.2f}% | rejected {total_rejected}/{total_cases}"
    )


if __name__ == "__main__":
    main()
