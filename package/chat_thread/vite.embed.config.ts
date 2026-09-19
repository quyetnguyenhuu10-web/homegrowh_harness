import { resolve } from "node:path";
import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";

// Build bundle NHÚNG 1 file duy nhất (React đóng gói sẵn bên trong):
// `npm run build:embed` -> dist/embed/chat-thread.js (ESM) + chat-thread.umd.js
// App khác chỉ cần 1 thẻ <script> rồi gọi window.ChatThread.mountChatThread(...)
// hoặc thả thẻ <chat-thread></chat-thread>.
export default defineConfig({
  plugins: [react()],
  build: {
    outDir: "dist/embed",
    emptyOutDir: true,
    lib: {
      entry: resolve(__dirname, "src/embed.tsx"),
      name: "ChatThread",
      formats: ["es", "umd"],
      fileName: (format) =>
        format === "es" ? "chat-thread.js" : "chat-thread.umd.js",
    },
  },
});
