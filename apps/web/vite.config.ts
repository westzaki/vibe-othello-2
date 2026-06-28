import { fileURLToPath, URL } from "node:url";

import react from "@vitejs/plugin-react";
import { defineConfig } from "vite";

const repoRoot = fileURLToPath(new URL("../..", import.meta.url));
const wasmCorePath = fileURLToPath(new URL("../../wasm/js/wasmCore.mjs", import.meta.url));

export default defineConfig({
  base: "/vibe-othello-2/",
  plugins: [react()],
  resolve: {
    alias: {
      "@vibe-othello/wasm-core": wasmCorePath,
    },
  },
  server: {
    fs: {
      allow: [repoRoot],
    },
  },
});
