// Serves build-web/ for the browser game (docs/web.md), never cached:
// `bun run tools/serve-web.ts`, then http://localhost:8080.
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
}

serve({
  port,
  async fetch(request) {
    const url = new URL(request.url)
    const requested = url.pathname === "/" ? "/moppe.html" : url.pathname
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
      },
    })
  },
})

console.log(`Moppe in the browser: http://localhost:${port}`)
