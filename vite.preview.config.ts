import { fileURLToPath, URL } from "node:url";
import react from "@vitejs/plugin-react";
import { defineConfig } from "vite";

// Standalone preview of the redesigned UI (mock data, no backend/auth).
export default defineConfig({
  plugins: [
    react(),
    {
      name: "preview-root-rewrite",
      configureServer(server) {
        server.middlewares.use((req, _res, next) => {
          // `/` opens the native stream overlay preview (the thing most people
          // come here to look at); `/ui` keeps the library/UI preview.
          if (req.url === "/" || req.url === "/index.html") {
            req.url = "/native-overlay-preview.html";
          } else if (req.url === "/ui") {
            req.url = "/preview.html";
          }
          next();
        });
      },
    },
  ],
  resolve: {
    alias: {
      "@shared": fileURLToPath(new URL("./src/shared", import.meta.url)),
      "@client": fileURLToPath(new URL("./src/client", import.meta.url)),
    },
  },
  server: {
    host: true,
    allowedHosts: true,
  },
});
