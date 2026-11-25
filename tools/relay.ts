// Tiny WebSocket room relay for Moppe multiplayer (native + browser):
// `bun run tools/relay.ts`, then join `ws://localhost:27960/room/<name>`.
// Every binary frame sent by one player is relayed verbatim to every other
// player connected to the same URL path.
import { serve, type ServerWebSocket } from "bun"

const port = Number(process.env.MOPPE_RELAY_PORT ?? process.env.PORT ?? 27960)
const maxRoomPlayers = 16
const maxMessageBytes = 4096

type PeerData = { room: string; id: number }

const rooms = new Map<string, Set<ServerWebSocket<PeerData>>>()
let nextId = 1

function normalizeRoom(pathname: string): string {
  const cleaned = pathname.replace(/\/+$/, "") || "/default"
  return cleaned.slice(0, 128)
}

const server = serve<PeerData>({
  port,
  idleTimeout: 120,
  fetch(request, server) {
    const url = new URL(request.url)
    if (url.pathname === "/health") {
      let players = 0
      for (const members of rooms.values()) players += members.size
      return Response.json({ ok: true, rooms: rooms.size, players })
    }
    const room = normalizeRoom(url.pathname)
    const members = rooms.get(room)
    if (members && members.size >= maxRoomPlayers) {
      return new Response("Room full", { status: 429 })
    }
    if (server.upgrade(request, { data: { room, id: nextId++ } })) {
      return undefined
    }
    return new Response(
      `Moppe multiplayer relay.\nConnect via ws://${url.host}/room/<name>\n`,
      { headers: { "Content-Type": "text/plain; charset=utf-8" } },
    )
  },
  websocket: {
    maxPayloadLength: maxMessageBytes,
    idleTimeout: 120,
    open(ws) {
      let members = rooms.get(ws.data.room)
      if (!members) {
        members = new Set()
        rooms.set(ws.data.room, members)
      }
      members.add(ws)
      console.log(
        `relay: #${ws.data.id} joined ${ws.data.room} (${members.size} present)`,
      )
    },
    message(ws, message) {
      if (typeof message === "string") return
      const members = rooms.get(ws.data.room)
      if (!members) return
      for (const peer of members) {
        if (peer !== ws && peer.readyState === 1) {
          peer.sendBinary(message)
        }
      }
    },
    close(ws) {
      const members = rooms.get(ws.data.room)
      if (!members) return
      members.delete(ws)
      if (members.size === 0) rooms.delete(ws.data.room)
      console.log(
        `relay: #${ws.data.id} left ${ws.data.room} (${members.size} present)`,
      )
    },
  },
})

console.log(`Moppe relay: ws://localhost:${server.port}/room/<name>`)
