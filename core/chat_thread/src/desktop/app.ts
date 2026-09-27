import { app, BrowserWindow } from "electron";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

import { installChatThreadDesktop } from "./main";

let mainWindow: BrowserWindow | null = null;
let disposeDesktop: (() => void) | null = null;
let preloadPath: string | null = null;

function rendererIndexPath(): string {
  return join(
    dirname(fileURLToPath(import.meta.url)),
    "..",
    "index.html",
  );
}

async function createWindow(): Promise<void> {
  if (!preloadPath) {
    throw new Error("chat_thread desktop chưa được install.");
  }

  const window = new BrowserWindow({
    width: 1360,
    height: 900,
    minWidth: 900,
    minHeight: 620,
    backgroundColor: "#ffffff",
    webPreferences: {
      preload: preloadPath,
      contextIsolation: true,
      nodeIntegration: false,
      sandbox: false,
    },
  });

  mainWindow = window;
  window.on("closed", () => {
    if (mainWindow === window) mainWindow = null;
  });

  const rendererUrl = process.env.CHAT_THREAD_RENDERER_URL?.trim();
  if (rendererUrl) {
    await window.loadURL(rendererUrl);
  } else {
    await window.loadFile(rendererIndexPath());
  }
}

void app.whenReady().then(async () => {
  const installation = installChatThreadDesktop();
  preloadPath = installation.preloadPath;
  disposeDesktop = installation.dispose;

  await createWindow();

  app.on("activate", () => {
    if (BrowserWindow.getAllWindows().length === 0) {
      void createWindow();
    }
  });
});

app.on("window-all-closed", () => {
  if (process.platform !== "darwin") app.quit();
});

app.on("before-quit", () => {
  disposeDesktop?.();
  disposeDesktop = null;
  preloadPath = null;
});
