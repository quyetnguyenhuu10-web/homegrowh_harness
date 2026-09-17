"use strict";

const fs = require("node:fs");
const path = require("node:path");
const { spawnSync } = require("node:child_process");
const { reportBody } = require("./report");

function parseArgs(argv) {
    const result = {
        profile: "smoke",
        buildDir: null,
        keepFiles: false,
        repeat: null,
    };

    for (let index = 0; index < argv.length; index += 1) {
        const argument = argv[index];

        if (argument === "--help" || argument === "-h") {
            result.help = true;
            continue;
        }

        if (
            argument === "--profile" ||
            argument === "--build-dir" ||
            argument === "--repeat"
        ) {
            if (index + 1 >= argv.length) {
                throw new Error(`${argument} requires a value`);
            }

            const key = argument === "--profile"
                ? "profile"
                : argument === "--build-dir"
                    ? "buildDir"
                    : "repeat";

            result[key] = argv[++index];
            continue;
        }

        if (argument === "--keep-files") {
            result.keepFiles = true;
            continue;
        }

        throw new Error(`unknown option: ${argument}`);
    }

    if (result.help) {
        console.log(
            "options: --profile smoke|standard|extreme " +
            "[--build-dir <directory>] [--keep-files] [--repeat <count>]"
        );
        process.exit(0);
    }

    return result;
}

function findFilesystemsRoot(start) {
    let current = path.resolve(start);

    while (true) {
        if (
            fs.existsSync(path.join(current, "CMakeLists.txt")) &&
            fs.existsSync(path.join(current, "src"))
        ) {
            return current;
        }

        const parent = path.dirname(current);
        if (parent === current) {
            throw new Error("unable to locate filesystems root");
        }

        current = parent;
    }
}

function executableCandidates(root, buildDir, executable) {
    const directories = [];

    if (buildDir) {
        directories.push(
            path.isAbsolute(buildDir)
                ? buildDir
                : path.resolve(root, buildDir)
        );
    }

    if (process.env.FILESYSTEMS_BUILD_DIR) {
        const configuredBuildDir = process.env.FILESYSTEMS_BUILD_DIR;
        directories.push(
            path.isAbsolute(configuredBuildDir)
                ? configuredBuildDir
                : path.resolve(root, configuredBuildDir)
        );
    }

    directories.push(
        path.join(root, "build-ci"),
        path.join(root, "build"),
        path.join(root, "build-windows")
    );

    const candidates = [];

    for (const directory of directories) {
        for (const configuration of [
            "Debug",
            "Release",
            "RelWithDebInfo",
            ""
        ]) {
            const location = configuration
                ? path.join(
                    directory,
                    configuration,
                    `${executable}.exe`
                )
                : path.join(
                    directory,
                    `${executable}.exe`
                );

            candidates.push(location);
        }
    }

    return candidates;
}

function findExecutable(root, buildDir, executable) {
    const location = executableCandidates(
        root,
        buildDir,
        executable
    ).find((candidate) => fs.existsSync(candidate));

    if (!location) {
        throw new Error(
            `unable to find ${executable}.exe under a build directory`
        );
    }

    return location;
}

function runExecutable(executable, args, cwd) {
    const processResult = spawnSync(
        executable,
        args,
        {
            cwd,
            encoding: "utf8",
            stdio: "inherit",
        }
    );

    if (processResult.error) {
        throw processResult.error;
    }

    return processResult.status ?? 1;
}

function readRecords(resultsDirectory) {
    const input = path.join(
        resultsDirectory,
        "results.jsonl"
    );

    if (!fs.existsSync(input)) {
        throw new Error(`result file not found: ${input}`);
    }

    return fs.readFileSync(input, "utf8")
        .split(/\r?\n/)
        .filter((line) => line.trim().length !== 0)
        .map((line) => JSON.parse(line));
}

function runCategory(configuration) {
    try {
        const args = parseArgs(
            process.argv.slice(2)
        );

        const categoryDirectory =
            configuration.categoryDirectory;

        const root =
            findFilesystemsRoot(
                categoryDirectory
            );

        const resultsDirectory =
            path.join(
                categoryDirectory,
                "results"
            );

        fs.mkdirSync(
            resultsDirectory,
            {
                recursive: true
            }
        );

        const executable =
            findExecutable(
                root,
                args.buildDir,
                configuration.executable
            );

        const executableArgs = [
            "--profile",
            args.profile,
            "--output",
            resultsDirectory,
        ];

        if (args.keepFiles) {
            executableArgs.push(
                "--keep-files"
            );
        }

        if (args.repeat) {
            executableArgs.push(
                "--repeat",
                args.repeat
            );
        }

        const status =
            runExecutable(
                executable,
                executableArgs,
                root
            );

        const records =
            readRecords(
                resultsDirectory
            );

        const reportPath =
            path.join(
                categoryDirectory,
                "report.html"
            );

        fs.writeFileSync(
            reportPath,
            reportBody(
                configuration,
                records,
                args,
                resultsDirectory
            )
        );

        console.log(
            `HTML report: ${reportPath}`
        );

        if (status !== 0) {
            process.exitCode = status;
        }

    } catch (error) {
        console.error(
            error instanceof Error
                ? error.message
                : error
        );

        process.exitCode = 1;
    }
}

function runAggregate(configuration) {
    try {
        const args = parseArgs(
            process.argv.slice(2)
        );

        const categoryDirectory =
            configuration.categoryDirectory;

        const inputDirectories = (
            configuration.inputDirs ?? []
        ).map((input) =>
            path.isAbsolute(input)
                ? input
                : path.resolve(
                    categoryDirectory,
                    input
                )
        );

        if (inputDirectories.length === 0) {
            throw new Error(
                "aggregate category requires inputDirs"
            );
        }

        fs.mkdirSync(
            categoryDirectory,
            {
                recursive: true
            }
        );

        const records = inputDirectories.flatMap(
            (directory) => readRecords(directory)
        );

        const reportPath =
            path.join(
                categoryDirectory,
                "report.html"
            );

        fs.writeFileSync(
            reportPath,
            reportBody(
                configuration,
                records,
                args,
                inputDirectories.join(", ")
            )
        );

        console.log(
            `HTML report: ${reportPath} ` +
            `(${records.length} records)`
        );

    } catch (error) {
        console.error(
            error instanceof Error
                ? error.message
                : error
        );

        process.exitCode = 1;
    }
}

module.exports = {
    runCategory,
    runAggregate
};
