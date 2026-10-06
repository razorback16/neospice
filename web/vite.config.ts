import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";
export default defineConfig({
  plugins: [react()],
  base: process.env.BASE_PATH || "/",
  server: { host: "127.0.0.1", port: 48173, strictPort: true },
  build: { target: "es2022" },
});
