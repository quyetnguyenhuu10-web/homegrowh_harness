import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";

// Build app độc lập: `npm run dev` / `npm run build` / `npm run preview`.
export default defineConfig({
  // Electron load bằng file:// nên production assets phải tương đối với index.html.
  base: "./",
  plugins: [react()],
  server: {
    port: 5173,
    strictPort: true,
  },
});
