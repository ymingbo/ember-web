import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';

// https://vitejs.dev/config/
export default defineConfig({
  plugins: [react()],
  /* 相对 base：包解压到任意子目录（http://host/xxx/）都能跑。
     默认的 '/' 会让 index.html 去 http://host/assets/... 找脚本，
     放子目录时全部 404，页面只剩一个空 #root —— 就是「一片白板」。 */
  base: './',
  server: {
    port: 5173,
    headers: {
      'Cross-Origin-Opener-Policy': 'same-origin',
      'Cross-Origin-Embedder-Policy': 'require-corp',
    },
  },
  build: {
    outDir: 'dist',
    sourcemap: true,
    rollupOptions: {
      output: {
        manualChunks: {
          'ember-wasm': ['./src/ember/ember_wasm.js'],
        },
      },
    },
  },
  optimizeDeps: {
    exclude: ['ember-wasm'],
  },
  assetsInclude: ['**/*.wasm', '**/*.data'],
});
