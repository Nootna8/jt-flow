import { defineConfig } from 'vite'
import vue from '@vitejs/plugin-vue'
import { crx } from '@crxjs/vite-plugin'
import manifest from './manifest.json' assert { type: 'json' } // Node >=17
import nodePolyfills from 'vite-plugin-node-stdlib-browser'

// https://vitejs.dev/config/
export default defineConfig({
  plugins: [
    nodePolyfills(),
    vue(),
    crx({ manifest })
  ],
  server: {
    port: 5173,
    strictPort: true,
    hmr: {
      port: 5173,
    },
  },
  define: {
    global: 'globalThis',
    // 'process.env.NODE_ENV': JSON.stringify('development'),
  },
  build: {
    rollupOptions: {
      input: {
        'src/ph_hook.js': 'src/ph_hook.js',
      },
    },
  }
})
