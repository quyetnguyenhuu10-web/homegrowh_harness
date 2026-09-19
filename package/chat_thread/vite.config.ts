import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";

// Build app độc lập: `npm run dev` / `npm run build` / `npm run preview`.
export default defineConfig({
  plugins: [react()],
  server: {
    port: 5173,
    strictPort: true,
  },
});
