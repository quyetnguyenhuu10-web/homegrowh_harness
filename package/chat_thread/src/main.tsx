import { StrictMode } from "react";
import { createRoot } from "react-dom/client";
import "./index.css";
import App from "./App";

// Entry cho `npm run dev` / build app độc lập.
const el = document.getElementById("root");
if (!el) throw new Error("Không thấy #root trong index.html.");

createRoot(el).render(
  <StrictMode>
    <App />
  </StrictMode>,
);
