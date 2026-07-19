import { serve } from "bun"
import { extname, join, normalize } from "node:path"

const root = normalize(join(import.meta.dir, "..", "build-web"))
const port = Number(process.env.MOPPE_WEB_PORT ?? 8080)

const contentTypes: Record<string, string> = {
  ".css": "text/css",
  ".data": "application/octet-stream",
  ".html": "text/html; charset=utf-8",
  ".js": "text/javascript; charset=utf-8",
  ".json": "application/json",
  ".wasm": "application/wasm",
  ".worker.js": "text/javascript; charset=utf-8",
}

serve({
  port,
  async fetch(request) {
    const url = new URL(request.url)
    const requested = url.pathname === "/" ? "/moppe-web.html" : url.pathname
    const relative = normalize(requested).replace(/^[/\\]+/, "")
    const path = normalize(join(root, relative))
    if (!path.startsWith(root + "/")) {
      return new Response("Forbidden", { status: 403 })
    }

    const file = Bun.file(path)
    if (!(await file.exists())) {
      return new Response("Not found", { status: 404 })
    }
    return new Response(file, {
      headers: {
        "Cache-Control": "no-cache",
        "Content-Type": contentTypes[extname(path)] ?? "application/octet-stream",
        "Cross-Origin-Embedder-Policy": "require-corp",
        "Cross-Origin-Opener-Policy": "same-origin",
      },
    })
  },
})

console.log(`Moppe WebGPU: http://localhost:${port}`)
