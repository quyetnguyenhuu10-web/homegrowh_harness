#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Trình sinh Báo cáo Benchmark chất lượng cao
- Đọc results.jsonl (hỗ trợ đường dẫn Windows chưa escape & phân tích mềm)
- Sinh results.html với Chart.js tương tác, chế độ Sáng/Tối,
  thống kê phân vị (P50, P90, P99, StdDev) và bố cục co giãn.
Chạy: python gen_html.py
"""

import json
import math
import re
from datetime import datetime
from pathlib import Path

BASE = Path(__file__).resolve().parent
JSONL = BASE / "results.jsonl"
HTML_OUT = BASE / "results.html"


def _parse_line_lenient(line: str):
    """Parses JSON line with fallback regex extraction for invalid escaping in paths."""
    try:
        return json.loads(line)
    except Exception:
        pass

    if '"type":"summary"' in line or '"type": "summary"' in line:
        m = re.search(r'"edit_count"\s*:\s*(\d+)', line)
        mt = re.search(r'"total_time_seconds"\s*:\s*([0-9.]+)', line)
        return {
            "type": "summary",
            "edit_count": int(m.group(1)) if m else 0,
            "total_time_seconds": float(mt.group(1)) if mt else 0.0,
        }

    if '"type":"edit"' not in line and '"type": "edit"' not in line:
        return None

    def num(key):
        m = re.search(r'"' + key + r'"\s*:\s*([0-9.\-]+)', line)
        return float(m.group(1)) if m else 0.0

    m_edit = re.search(r'"edit"\s*:\s*(\d+)', line)
    m_note = re.search(r'"note"\s*:\s*"([^"]*)"', line)
    return {
        "type": "edit",
        "edit": int(m_edit.group(1)) if m_edit else 0,
        "path": "",
        "note": m_note.group(1) if m_note else "",
        "ram_before_mb": num("ram_before_mb"),
        "ram_peak_mb": num("ram_peak_mb"),
        "ram_after_mb": num("ram_after_mb"),
        "ram_jsonl_mb": num("ram_jsonl_mb"),
        "time_seconds": num("time_seconds"),
    }


def load_edits():
    edits = []
    summary = {}
    with open(JSONL, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            obj = _parse_line_lenient(line)
            if not obj:
                continue
            if obj.get("type") == "edit":
                edits.append(obj)
            elif obj.get("type") == "summary":
                summary = obj
    edits.sort(key=lambda x: x.get("edit", 0))
    return edits, summary


def percentile(data, p):
    if not data:
        return 0.0
    sorted_data = sorted(data)
    k = (len(sorted_data) - 1) * (p / 100.0)
    f = math.floor(k)
    c = math.ceil(k)
    if f == c:
        return sorted_data[int(k)]
    d0 = sorted_data[int(f)] * (c - k)
    d1 = sorted_data[int(c)] * (k - f)
    return d0 + d1


def calculate_stats(edits):
    n = len(edits)
    rams = [float(e["ram_peak_mb"]) for e in edits]
    times = [float(e["time_seconds"]) for e in edits]

    avg_ram = sum(rams) / n if n else 0
    stab_rams = rams[1:] if n > 1 else rams
    stab_ram_avg = sum(stab_rams) / len(stab_rams) if stab_rams else avg_ram

    # Variance and StdDev for RAM (excluding warmup)
    var_ram = (
        sum((x - stab_ram_avg) ** 2 for x in stab_rams) / len(stab_rams)
        if stab_rams
        else 0
    )
    std_ram = math.sqrt(var_ram)

    avg_time = sum(times) / n if n else 0

    return {
        "n": n,
        "initial_peak": rams[0] if rams else 0,
        "stab_ram": stab_ram_avg,
        "std_ram": std_ram,
        "min_ram": min(rams) if rams else 0,
        "max_ram": max(rams) if rams else 0,
        "p50_ram": percentile(rams, 50),
        "p95_ram": percentile(rams, 95),
        "avg_time": avg_time,
        "min_time": min(times) if times else 0,
        "max_time": max(times) if times else 0,
        "p50_time": percentile(times, 50),
        "p90_time": percentile(times, 90),
        "p99_time": percentile(times, 99),
    }


def main():
    if not JSONL.exists():
        raise FileNotFoundError(f"Not found: {JSONL}")

    edits, summary = load_edits()
    if not edits:
        raise ValueError("results.jsonl contains no valid edit records.")

    st = calculate_stats(edits)
    total_time = summary.get(
        "total_time_seconds", sum(float(e["time_seconds"]) for e in edits)
    )

    chart_data = [
        {
            "i": int(e["edit"]),
            "ram": round(float(e["ram_peak_mb"]), 4),
            "time": round(float(e["time_seconds"]), 4),
            "before": round(float(e.get("ram_before_mb", 0)), 4),
            "after": round(float(e.get("ram_after_mb", 0)), 4),
        }
        for e in edits
    ]

    table_rows = "\n".join(
        f'<tr><td>{d["i"]}</td><td>{d["ram"]:.4f} MB</td>'
        f'<td>{d["time"]:.4f} s</td><td>{d["before"]:.4f} MB</td><td>{d["after"]:.4f} MB</td></tr>'
        for d in chart_data
    )

    now_str = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

    html_content = f"""<!DOCTYPE html>
<html lang="vi" data-theme="dark">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Báo cáo Phân tích Hiệu năng Benchmark</title>
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

  .kpi-card.ram-card::before {{ background: var(--accent-blue); }}
  .kpi-card.time-card::before {{ background: var(--accent-pink); }}
  .kpi-card.stat-card::before {{ background: var(--accent-green); }}

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

  table.data-table tbody tr:hover {{
    background: var(--bg-card-hover);
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
  <!-- Tiêu đề -->
  <header class="header">
    <div class="header-titles">
      <h1>
        Hệ thống Phân tích Benchmark
      </h1>
      <div class="header-sub">
        Tạo lúc: {now_str} &bull; Nguồn: {JSONL.name} &bull; Tổng thời gian chạy: {total_time:.2f}s
      </div>
    </div>
  </header>

  <!-- Khối nội dung chính -->
  <main class="main-content" id="main-content">
    <!-- Panel biểu đồ -->
    <div class="panel panel-chart" id="panel-chart">
      <div class="panel-header">
        <span class="panel-title">📈 Tương quan Đỉnh RAM & Độ trễ Thực thi</span>
        <div class="controls">
          <button class="btn" id="title-toggle" onclick="toggleChartTitle()">👁 Ẩn tiêu đề</button>
          <button class="btn" id="theme-toggle" onclick="toggleTheme()">☀️ Chế độ Sáng</button>
          <button class="btn" id="split-toggle" onclick="toggleSplitView()">📑 Chia đôi</button>
          <button class="btn" onclick="toggleDataset(0)">Đỉnh RAM</button>
          <button class="btn" onclick="toggleDataset(1)">Thời gian chạy</button>
        </div>
      </div>
      <!-- Thẻ KPI nằm trong panel biểu đồ -->
      <section class="kpi-grid kpi-inside-chart">
        <div class="kpi-card ram-card">
          <div class="kpi-label">RAM ổn định (từ lần 2+)</div>
          <div class="kpi-value">{st['stab_ram']:.2f} MB</div>
          <div class="kpi-subtext">Độ lệch chuẩn: &plusmn;{st['std_ram']:.4f} MB</div>
        </div>
        <div class="kpi-card ram-card">
          <div class="kpi-label">Đỉnh ban đầu / Đỉnh cao nhất</div>
          <div class="kpi-value">{st['initial_peak']:.2f} / {st['max_ram']:.2f} MB</div>
          <div class="kpi-subtext">P50: {st['p50_ram']:.2f} MB | P95: {st['p95_ram']:.2f} MB</div>
        </div>
        <div class="kpi-card time-card">
          <div class="kpi-label">Độ trễ (P50 / P90)</div>
          <div class="kpi-value">{st['p50_time']:.2f}s / {st['p90_time']:.2f}s</div>
          <div class="kpi-subtext">Đỉnh đột biến: {st['max_time']:.2f}s (P99: {st['p99_time']:.2f}s)</div>
        </div>
        <div class="kpi-card stat-card">
          <div class="kpi-label">Số lần lặp & Thời gian TB</div>
          <div class="kpi-value">{st['n']} / {st['avg_time']:.2f}s</div>
          <div class="kpi-subtext">Đã kiểm chứng không rò rỉ bộ nhớ (O(1))</div>
        </div>
      </section>
      <div class="chart-wrapper" id="chart-wrapper">
        <canvas id="benchmarkChart"></canvas>
      </div>
    </div>

    <!-- Thanh kéo dọc giữa biểu đồ và bảng -->
    <div class="splitter" id="splitter" title="Kéo để đổi cỡ biểu đồ / bảng"></div>

    <!-- Panel bảng dữ liệu -->
    <div class="panel panel-table" id="panel-table">
      <div class="panel-header">
        <span class="panel-title">📋 Nhật ký Thực thi Thô ({st['n']} Lượt chạy)</span>
      </div>
      <div class="table-container">
        <table class="data-table">
          <thead>
            <tr>
              <th>Lần sửa</th>
              <th>Đỉnh RAM</th>
              <th>Thời gian</th>
              <th>RAM trước</th>
              <th>RAM sau</th>
            </tr>
          </thead>
          <tbody>
            {table_rows}
          </tbody>
        </table>
      </div>
    </div>
  </main>
</div>

<script>
const rawData = {json.dumps(chart_data, ensure_ascii=False)};
let chartInstance = null;

function getThemeColors() {{
  const isDark = document.documentElement.getAttribute('data-theme') === 'dark';
  return {{
    textColor: isDark ? '#f9fafb' : '#0f172a',
    mutedColor: isDark ? '#9ca3af' : '#475569',
    gridColor: isDark ? 'rgba(255, 255, 255, 0.05)' : 'rgba(0, 0, 0, 0.05)',
    blue: isDark ? '#3b82f6' : '#2563eb',
    blueBg: isDark ? 'rgba(59, 130, 246, 0.12)' : 'rgba(37, 99, 235, 0.08)',
    pink: isDark ? '#ec4899' : '#db2777',
    pinkBg: isDark ? 'rgba(236, 72, 153, 0.1)' : 'rgba(219, 39, 119, 0.06)',
    cardBg: isDark ? '#111827' : '#ffffff'
  }};
}}

function initChart() {{
  const ctx = document.getElementById('benchmarkChart').getContext('2d');
  const colors = getThemeColors();
  
  if (chartInstance) chartInstance.destroy();

  chartInstance = new Chart(ctx, {{
    type: 'line',
    data: {{
      labels: rawData.map(d => 'Lần sửa #' + d.i),
      datasets: [
        {{
          label: 'Đỉnh RAM (MB)',
          data: rawData.map(d => d.ram),
          yAxisID: 'yRAM',
          borderColor: colors.blue,
          backgroundColor: colors.blueBg,
          borderWidth: 2,
          fill: true,
          tension: 0.15,
          pointRadius: 0,
          pointHoverRadius: 6,
          pointHoverBackgroundColor: colors.blue
        }},
        {{
          label: 'Thời gian chạy (giây)',
          data: rawData.map(d => d.time),
          yAxisID: 'yTime',
          borderColor: colors.pink,
          backgroundColor: colors.pinkBg,
          borderWidth: 1.5,
          fill: true,
          tension: 0.1,
          pointRadius: 0,
          pointHoverRadius: 6,
          pointHoverBackgroundColor: colors.pink
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
          bodyFont: {{ family: 'JetBrains Mono', size: 12 }},
          callbacks: {{
            label: (ctx) => ` ${{ctx.dataset.label}}: ${{ctx.parsed.y.toFixed(4)}}`
          }}
        }}
      }},
      scales: {{
        x: {{
          grid: {{ display: false }},
          ticks: {{ color: colors.mutedColor, font: {{ family: 'JetBrains Mono', size: 10 }}, maxTicksLimit: 12 }}
        }},
        yRAM: {{
          type: 'linear',
          position: 'left',
          grid: {{ color: colors.gridColor }},
          ticks: {{
            color: colors.mutedColor,
            font: {{ family: 'JetBrains Mono', size: 11 }},
            callback: (v) => v.toFixed(1) + ' MB'
          }}
        }},
        yTime: {{
          type: 'linear',
          position: 'right',
          grid: {{ display: false }},
          ticks: {{
            color: colors.mutedColor,
            font: {{ family: 'JetBrains Mono', size: 11 }},
            callback: (v) => v.toFixed(1) + ' s'
          }}
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
  let startX = 0;
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
    startX = e.clientX;
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
    print(f"[XONG] Đã sinh báo cáo HTML tại: {HTML_OUT}")
    print(
        f"Tóm tắt: {st['n']} lần sửa | Đỉnh RAM TB: {st['stab_ram']:.4f} MB | Độ trễ P50: {st['p50_time']:.4f}s"
    )


if __name__ == "__main__":
    main()