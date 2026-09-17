"use strict";

const fs = require("node:fs");
const path = require("node:path");
const { spawnSync } = require("node:child_process");

const categories = [
    ["bench", "bench.js"],
    ["size_analysis", "size_analysis.js"],
];

function parseArgs(argv) {
    const result = { profile: "smoke" };

    for (let index = 0; index < argv.length; index += 1) {
        const argument = argv[index];
        if (argument === "--profile" || argument === "--build-dir" || argument === "--repeat") {
            if (index + 1 >= argv.length) {
                throw new Error(`${argument} requires a value`);
            }
            const key = argument === "--profile"
                ? "profile"
                : argument === "--build-dir"
                    ? "buildDir"
                    : "repeat";
            result[key] = argv[++index];
        } else if (argument === "--keep-files") {
            result.keepFiles = true;
        } else if (argument === "--help" || argument === "-h") {
            console.log(
                "options: --profile smoke|standard|extreme " +
                "[--build-dir <directory>] [--keep-files] [--repeat <count>]"
            );
            process.exit(0);
        } else {
            throw new Error(`unknown option: ${argument}`);
        }
    }

    return result;
}

function writeIndex(profile, statuses) {
    const links = categories.map(([directory]) => {
        const status = statuses.get(directory) ?? "not run";
        return `<tr><td>${directory}</td><td>${status}</td>` +
            `<td><a href="${directory}/report.html">report</a></td></tr>`;
    }).join("");
    const html = `<!doctype html><html lang="en"><head><meta charset="utf-8">` +
        `<title>Windows edit stress reports</title><style>` +
        `body{font:14px system-ui,sans-serif;margin:24px}table{border-collapse:collapse}` +
        `th,td{border-bottom:1px solid #ccc;padding:8px;text-align:left}` +
        `</style></head><body><h1>Windows edit stress reports</h1>` +
        `<p>Profile: ${profile}</p><table><thead><tr><th>section</th>` +
        `<th>process</th><th>report</th></tr></thead><tbody>${links}` +
        `</tbody></table></body></html>`;
    fs.writeFileSync(path.join(__dirname, "index.html"), html);
}

try {
    const args = parseArgs(process.argv.slice(2));
    const statuses = new Map();
    let failed = false;

    for (const [directory, script] of categories) {
        const scriptPath = path.join(__dirname, directory, script);
        const childArgs = [scriptPath, "--profile", args.profile];
        if (args.buildDir) {
            childArgs.push("--build-dir", args.buildDir);
        }
        if (args.keepFiles) {
            childArgs.push("--keep-files");
        }
        if (args.repeat) {
            childArgs.push("--repeat", args.repeat);
        }

        const result = spawnSync(process.execPath, childArgs, {
            cwd: __dirname,
            stdio: "inherit",
        });
        const status = result.status ?? 1;
        statuses.set(directory, status === 0 ? "pass" : `failed (${status})`);
        if (status !== 0) {
            failed = true;
        }
    }

    writeIndex(args.profile, statuses);
    process.exitCode = failed ? 1 : 0;
} catch (error) {
    console.error(error instanceof Error ? error.message : error);
    process.exitCode = 1;
}
