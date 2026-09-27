import { spawn } from "node:child_process";
import { watch } from "node:fs";
import { basename, extname, resolve } from "node:path";

const isWindows = process.platform === "win32";
const npmCommand = "npm";
const npxCommand = "npx";
const rendererUrl = "http://127.0.0.1:5173";
const rootDir = process.cwd();
const desktopDir = resolve(rootDir, "src/desktop");
const historyDir = resolve(rootDir, "src/component/history_conversation");
const providerDir = resolve(rootDir, "../provider");
const providerSrcDir = resolve(providerDir, "src");

const watchedHistoryFiles = new Set([
  "desktop_bridge.ts",
  "repository.ts",
  "scope.ts",
  "store.ts",
  "types.ts",
]);

function run(command, args, options = {}) {
  if (isWindows) {
    const commandLine = [command, ...args]
      .map((part) =>
        /[\s"&|<>^]/.test(part)
          ? '"' + part.replaceAll('"', '""') + '"'
          : part,
      )
      .join(" ");

    return spawn(
      process.env.ComSpec || "cmd.exe",
      ["/d", "/s", "/c", commandLine],
      {
        stdio: "inherit",
        ...options,
      },
    );
  }

  return spawn(command, args, {
    stdio: "inherit",
    ...options,
  });
}

function waitForExit(child) {
  return new Promise((resolve, reject) => {
    child.once("error", reject);
    child.once("exit", (code, signal) => {
      if (signal) {
        reject(new Error(String(child.spawnfile) + " stopped by " + signal));
        return;
      }

      resolve(code ?? 0);
    });
  });
}

async function waitForRenderer(timeoutMs = 30_000) {
  const deadline = Date.now() + timeoutMs;

  while (Date.now() < deadline) {
    try {
      const response = await fetch(rendererUrl);
      if (response.ok) return;
    } catch {
      // Vite chưa sẵn sàng.
    }

    await new Promise((resolve) => setTimeout(resolve, 100));
  }

  throw new Error("Vite renderer không sẵn sàng tại " + rendererUrl + ".");
}

function terminate(child) {
  if (!child || child.killed) return;

  if (isWindows && child.pid) {
    const killer = spawn(
      "taskkill",
      ["/pid", String(child.pid), "/t", "/f"],
      { stdio: "ignore" },
    );
    killer.unref();
    return;
  }

  child.kill("SIGTERM");
}

async function buildDesktop() {
  const desktopBuild = run(npmCommand, ["run", "build:desktop"]);
  const buildExitCode = await waitForExit(desktopBuild);
  if (buildExitCode !== 0) {
    throw new Error("Desktop build thất bại với exit code " + buildExitCode + ".");
  }
}

let vite = null;
let electron = null;
let shuttingDown = false;
let restartingElectron = false;
let rebuildRunning = false;
let rebuildQueued = false;
let rebuildTimer = null;
const watchers = [];

function startElectron() {
  const child = run(
    npxCommand,
    ["--no-install", "electron", "dist/desktop/app.cjs"],
    {
      env: {
        ...process.env,
        CHAT_THREAD_RENDERER_URL: rendererUrl,
      },
    },
  );

  electron = child;
  child.once("error", (error) => {
    if (shuttingDown || restartingElectron) return;
    console.error(error instanceof Error ? error.message : String(error));
    cleanup(1);
  });
  child.once("exit", (code, signal) => {
    if (electron === child) electron = null;
    if (shuttingDown || restartingElectron) return;

    if (signal) {
      console.error("Electron đã dừng bởi " + signal + ".");
      cleanup(1);
      return;
    }

    cleanup(code ?? 0);
  });
}

async function restartElectron() {
  const currentElectron = electron;
  if (!currentElectron) {
    startElectron();
    return;
  }

  restartingElectron = true;
  const electronExit = waitForExit(currentElectron);
  terminate(currentElectron);

  try {
    await electronExit;
  } catch {
    // taskkill/SIGTERM là chủ ý trong quá trình restart.
  } finally {
    restartingElectron = false;
  }

  if (!shuttingDown) startElectron();
}

async function rebuildDesktopAndRestart() {
  if (rebuildRunning || shuttingDown) {
    rebuildQueued = true;
    return;
  }

  rebuildRunning = true;
  try {
    do {
      rebuildQueued = false;
      console.log("[electron-dev] Desktop source changed; rebuilding...");

      try {
        await buildDesktop();
      } catch (error) {
        console.error(error instanceof Error ? error.message : String(error));
        continue;
      }

      if (!shuttingDown) await restartElectron();
    } while (rebuildQueued && !shuttingDown);
  } finally {
    rebuildRunning = false;
  }
}

function queueDesktopRebuild() {
  if (shuttingDown) return;
  if (rebuildTimer) clearTimeout(rebuildTimer);
  rebuildTimer = setTimeout(() => {
    rebuildTimer = null;
    void rebuildDesktopAndRestart();
  }, 180);
}

function watchDirectory(directory, shouldRebuild) {
  const watcher = watch(directory, { recursive: true }, (_eventType, filename) => {
    if (!filename) return;
    const normalized = String(filename).replaceAll("\\", "/");
    if (shouldRebuild(normalized)) queueDesktopRebuild();
  });
  watcher.on("error", (error) => {
    console.error(
      "[electron-dev] Watcher lỗi tại " + directory + ": " +
        (error instanceof Error ? error.message : String(error)),
    );
  });
  watchers.push(watcher);
}

function installDesktopWatchers() {
  watchDirectory(desktopDir, (filename) => {
    const extension = extname(filename);
    return extension === ".ts" || extension === ".tsx" || extension === ".json";
  });

  watchDirectory(historyDir, (filename) => {
    if (filename.includes("/")) return false;
    return watchedHistoryFiles.has(basename(filename));
  });

  watchDirectory(providerSrcDir, (filename) => {
    const extension = extname(filename);
    return extension === ".ts" || extension === ".json";
  });

  const providerConfigWatcher = watch(providerDir, (_eventType, filename) => {
    if (!filename) return;
    const name = basename(String(filename));
    if (name === "package.json" || name === "tsconfig.json") {
      queueDesktopRebuild();
    }
  });
  providerConfigWatcher.on("error", (error) => {
    console.error(
      "[electron-dev] Provider config watcher lỗi: " +
        (error instanceof Error ? error.message : String(error)),
    );
  });
  watchers.push(providerConfigWatcher);
}

function cleanup(exitCode = null) {
  if (shuttingDown) return;
  shuttingDown = true;

  if (rebuildTimer) {
    clearTimeout(rebuildTimer);
    rebuildTimer = null;
  }
  for (const watcher of watchers) watcher.close();
  watchers.length = 0;
  terminate(electron);
  terminate(vite);

  if (exitCode !== null) process.exitCode = exitCode;
}

process.once("SIGINT", () => cleanup());
process.once("SIGTERM", () => cleanup());
process.once("exit", () => cleanup());

try {
  await buildDesktop();

  vite = run(
    npmCommand,
    ["run", "renderer", "--", "--host", "127.0.0.1"],
  );

  const viteExit = waitForExit(vite).then((code) => {
    if (shuttingDown) return code;
    throw new Error("Vite renderer đã dừng với exit code " + code + ".");
  });

  await Promise.race([waitForRenderer(), viteExit]);
  installDesktopWatchers();
  startElectron();

  await viteExit;
} catch (error) {
  console.error(error instanceof Error ? error.message : String(error));
  cleanup(1);
}
