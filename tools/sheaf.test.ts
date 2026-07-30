import { afterEach, describe, expect, test } from "bun:test";

const cli = new URL("./sheaf", import.meta.url).pathname;
const servers: Bun.Server<unknown>[] = [];

afterEach(() => {
  for (const server of servers.splice(0)) server.stop(true);
});

describe("Moppe Sheaf helper", () => {
  test("defaults browser links to the hosted research library", async () => {
    const result = await runCli(["open", "#ABC123"]);

    expect(result.exitCode).toBe(0);
    expect(result.stdout).toBe("https://m.sheaf.less.rest/ABC123\n");
    expect(result.stderr).toBe("");
  });

  test("maps documents to the research-library tool", async () => {
    const requests: RequestRecord[] = [];
    const server = mockServer(requests, {
      content: [{ type: "text", text: "# Research library\n\nABC123 Example" }],
      isError: false,
    });
    const result = await runCli(["documents"], server);

    expect(result.exitCode).toBe(0);
    expect(result.stdout).toBe("# Research library\n\nABC123 Example\n");
    expect(result.stderr).toBe("");
    expect(requests).toHaveLength(1);
    expect(requests[0]?.authorization).toBe("Bearer test-token");
    expect(requests[0]?.body).toMatchObject({
      jsonrpc: "2.0",
      method: "tools/call",
      params: { name: "list_documents", arguments: {} },
    });
  });

  test("passes search scopes and a numeric limit", async () => {
    const requests: RequestRecord[] = [];
    const server = mockServer(requests, {
      content: [{ type: "text", text: "search result" }],
      isError: false,
    });
    const result = await runCli(
      [
        "search",
        "distributed",
        "cognition",
        "--document",
        "#ABC123",
        "--kind",
        "literature",
        "--limit",
        "7",
      ],
      server,
    );

    expect(result.exitCode).toBe(0);
    expect(requests[0]?.body.params).toEqual({
      name: "search_text",
      arguments: {
        query: "distributed cognition",
        document_id: "ABC123",
        document_kind: "literature",
        limit: 7,
      },
    });
  });

  test("writes a note from stdin with explicit related blocks", async () => {
    const requests: RequestRecord[] = [];
    const server = mockServer(requests, {
      content: [{ type: "text", text: "Saved research note #NOTE01" }],
      isError: false,
    });
    const result = await runCli(
      [
        "note",
        "--title",
        "Useful finding",
        "--block",
        "#ABC123",
        "--block",
        "DEF456",
      ],
      server,
      "A durable finding.\n",
    );

    expect(result.exitCode).toBe(0);
    expect(result.stdout).toBe("Saved research note #NOTE01\n");
    expect(requests[0]?.body.params).toEqual({
      name: "write_note",
      arguments: {
        text: "A durable finding.",
        title: "Useful finding",
        block_ids: ["ABC123", "DEF456"],
      },
    });
  });

  test("reports authentication failures without exposing the token", async () => {
    const server = Bun.serve({
      port: 0,
      fetch: () =>
        Response.json(
          { error: "invalid or missing bearer token" },
          { status: 401 },
        ),
    });
    servers.push(server);
    const result = await runCli(["notes"], server);

    expect(result.exitCode).toBe(1);
    expect(result.stderr).toContain("HTTP 401");
    expect(result.stderr).toContain("check SHEAF_TOKEN");
    expect(result.stderr).not.toContain("test-token");
  });
});

type RequestRecord = {
  authorization: string | null;
  body: Record<string, any>;
};

function mockServer(
  requests: RequestRecord[],
  result: Record<string, unknown>,
) {
  const server = Bun.serve({
    port: 0,
    async fetch(request) {
      requests.push({
        authorization: request.headers.get("authorization"),
        body: await request.json(),
      });
      return Response.json({ jsonrpc: "2.0", id: 1, result });
    },
  });
  servers.push(server);
  return server;
}

async function runCli(
  args: string[],
  server?: Bun.Server<unknown>,
  stdin?: string,
) {
  const process = Bun.spawn(["bun", cli, ...args], {
    env: {
      ...Bun.env,
      SHEAF_URL: server ? `http://127.0.0.1:${server.port}` : "",
      SHEAF_TOKEN: server ? "test-token" : "",
      SHEAF_MCP_TOKEN: "",
    },
    stdin: stdin === undefined ? "ignore" : "pipe",
    stdout: "pipe",
    stderr: "pipe",
  });

  if (stdin !== undefined && process.stdin) {
    process.stdin.write(stdin);
    process.stdin.end();
  }

  const [exitCode, stdout, stderr] = await Promise.all([
    process.exited,
    new Response(process.stdout).text(),
    new Response(process.stderr).text(),
  ]);
  return { exitCode, stdout, stderr };
}
