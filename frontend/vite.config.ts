import { readFile, writeFile } from 'node:fs/promises'
import { resolve } from 'node:path'
import react from '@vitejs/plugin-react'
import tailwindcss from '@tailwindcss/vite'
import { defineConfig, type Plugin } from 'vite'

// macOS serves the frontend from app:// via WKURLSchemeHandler, which gives
// the page a real origin: normal code-splitting and lazy imports work there.
// Other platforms still load from file:// where WebKit blocks ES module
// chunks on a null origin, so INLINE_BUNDLE=1 folds everything into a single
// self-contained index.html instead.
const outDir = '../resources/frontend'
const inlineBundleEnabled = process.env.INLINE_BUNDLE === '1'

function inlineBundle(): Plugin {
  return {
    name: 'inline-bundle',
    apply: 'build',
    enforce: 'post',
    async writeBundle(_options, bundle) {
      const htmlEntry = Object.entries(bundle).find(
        ([name, output]) => name.endsWith('.html') && output.type === 'asset',
      )
      if (!htmlEntry) {
        return
      }
      const htmlPath = resolve(__dirname, outDir, htmlEntry[0])
      let html = await readFile(htmlPath, 'utf-8')

      const entryChunk = Object.entries(bundle).find(
        ([, output]) => output.type === 'chunk' && (output as { isEntry?: boolean }).isEntry,
      )?.[0]
      if (entryChunk) {
        const scriptTag = new RegExp(
          `<script type="module"[^>]*src="\\./${entryChunk}"[^>]*></script>`,
        )
        const code = await readFile(resolve(__dirname, outDir, entryChunk), 'utf-8')
        const escaped = code.replace(/<\/script>/g, '<\\/script>')
        html = html.replace(scriptTag, () => `<script type="module">\n${escaped}\n</script>`)
      }

      const cssLinks = [
        ...html.matchAll(/<link rel="stylesheet"[^>]*href="\.\/(assets\/[^"]+\.css)"[^>]*>/g),
      ]
      for (const match of cssLinks) {
        const css = await readFile(resolve(__dirname, outDir, match[1]), 'utf-8')
        html = html.replace(match[0], () => `<style>\n${css}\n</style>`)
      }

      html = html.replace(/\scrossorigin(="[^"]*")?/g, '')
      await writeFile(htmlPath, html)
    },
  }
}

export default defineConfig({
  base: './',
  plugins: [react(), tailwindcss(), ...(inlineBundleEnabled ? [inlineBundle()] : [])],
  build: {
    outDir,
    emptyOutDir: true,
    modulePreload: { polyfill: false },
    ...(inlineBundleEnabled
      ? {
          cssCodeSplit: false,
          assetsInlineLimit: 100_000_000,
          chunkSizeWarningLimit: 100_000_000,
          rollupOptions: { output: { inlineDynamicImports: true } },
        }
      : {}),
  },
})
