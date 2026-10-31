import { fileURLToPath, URL } from 'node:url'

import { defineConfig } from 'vite'
import vue from '@vitejs/plugin-vue'
import vueDevTools from 'vite-plugin-vue-devtools'

// https://vite.dev/config/
export default defineConfig({
  plugins: [vue(), vueDevTools()],
  resolve: {
    alias: {
      '@': fileURLToPath(new URL('./src', import.meta.url)),
    },
  },
  server: {
    // `pnpm dev` serves the pages and forwards the API, WebSockets included, to the bot that
    // `just run` starts on the default listener of config.json.
    proxy: {
      '/api': { target: 'http://127.0.0.1:8080', ws: true },
    },
  },
})
