import assert from "node:assert/strict";
import path from "node:path";
import test from "node:test";
import { fileURLToPath } from "node:url";
import { Client } from "@modelcontextprotocol/sdk/client/index.js";
import { StdioClientTransport } from "@modelcontextprotocol/sdk/client/stdio.js";

test("stdio MCP initializes and exposes only the four bounded tools", async () => {
  const here = path.dirname(fileURLToPath(import.meta.url));
  const server = path.resolve(here, "../src/server.mjs");
  const client = new Client({ name: "openbfme-launcher-test", version: "1.0.0" });
  const transport = new StdioClientTransport({ command: process.execPath, args: [server] });
  try {
    await client.connect(transport);
    const listed = await client.listTools();
    assert.deepEqual(listed.tools.map((tool) => tool.name).sort(), [
      "configure_launcher",
      "get_launcher_status",
      "launch_bfme2",
      "terminate_bfme_tree",
    ]);
    const status = await client.callTool({ name: "get_launcher_status", arguments: {} });
    assert.notEqual(status.isError, true);
    const payload = JSON.parse(status.content[0].text);
    assert.equal(payload.installed.launcher, true);
    assert.equal(payload.settings.launch_with_affinity_1, true);
    const invalid = await client.callTool({ name: "get_launcher_status", arguments: { executable: "C:\\evil.exe" } });
    assert.equal(invalid.isError, true);
  } finally {
    await client.close();
  }
});
