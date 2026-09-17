"use strict";

function escapeHtml(value) {
    return String(value)
        .replaceAll("&", "&amp;")
        .replaceAll("<", "&lt;")
        .replaceAll(">", "&gt;")
        .replaceAll('"', "&quot;")
        .replaceAll("'", "&#39;");
}

function get(record, location, fallback = 0) {
    const value = location.split(".").reduce(
        (current, key) =>
            current == null ? undefined : current[key],
        record
    );

    return value ?? fallback;
}

function number(record, location) {
    const value = Number(
        get(record, location, 0)
    );

    return Number.isFinite(value) ? value : 0;
}

function formatBytes(value) {
    const bytes = Number(value);

    if (!Number.isFinite(bytes)) {
        return "0 B";
    }

    const units = [
        "B",
        "KB",
        "MB",
        "GB",
        "TB"
    ];

    let scaled = Math.max(0, bytes);
    let unit = 0;

    while (
        scaled >= 1000 &&
        unit < units.length - 1
    ) {
        scaled /= 1000;
        unit += 1;
    }

    return `${scaled.toFixed(
        unit === 0 ? 0 : 2
    )} ${units[unit]}`;
}

function formatByteField(field, raw) {
    return String(field).includes("bytes")
        ? formatBytes(raw)
        : formatNumber(raw);
}

function formatNumber(value) {
    const numberValue = Number(value);

    if (!Number.isFinite(numberValue)) {
        return "0";
    }

    return new Intl.NumberFormat("en-US", {
        maximumFractionDigits: 2,
    }).format(numberValue);
}

function formatAxisNumber(value) {
    const numberValue = Number(value);

    if (!Number.isFinite(numberValue)) {
        return "0";
    }

    if (Math.abs(numberValue) >= 1000000) {
        return `${(numberValue / 1000000).toFixed(1)}M`;
    }

    if (Math.abs(numberValue) >= 1000) {
        return `${(numberValue / 1000).toFixed(1)}k`;
    }

    return formatNumber(numberValue);
}

module.exports = {
    escapeHtml,
    get,
    number,
    formatBytes,
    formatByteField,
    formatNumber,
    formatAxisNumber
};
