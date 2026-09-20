import { builtinModules } from "node:module";
import { resolve } from "node:path";
import { defineConfig } from "vite";

const nodeExternals = [
  "electron",
  ...builtinModules,
  ...builtinModules.map((name) => `node:${name}`),
];

/**
 * Electron-side bundle của chat_thread plugin.
 * - main.cjs: đăng ký IPC + folder picker + history_conversation.addRepository
 * - preload.cjs: expose bridge tối thiểu sang renderer
 */
export default defineConfig({
  build: {
    ssr: true,
    target: "node22",
    outDir: "dist/desktop",
    emptyOutDir: true,
    rollupOptions: {
      input: {
        app: resolve(__dirname, "src/desktop/app.ts"),
        main: resolve(__dirname, "src/desktop/main.ts"),
        preload: resolve(__dirname, "src/desktop/preload.ts"),
      },
      external: nodeExternals,
      output: {
        format: "cjs",
        entryFileNames: "[name].cjs",
        chunkFileNames: "chunks/[name]-[hash].cjs",
      },
    },
  },
});
