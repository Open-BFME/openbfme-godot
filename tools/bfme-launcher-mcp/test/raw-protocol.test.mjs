import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import path from "node:path";
import test from "node:test";
import { fileURLToPath } from "node:url";

test("initialize returns the implemented protocol version instead of echoing an unsupported offer", async () => {
  const here = path.dirname(fileURLToPath(import.meta.url));
  const server = path.resolve(here, "../src/server.mjs");
  const child = spawn(process.execPath, [server], { stdio: ["pipe", "pipe", "pipe"] });
  let stdout = "";
  let stderr = "";
  child.stdout.setEncoding("utf8").on("data", (chunk) => { stdout += chunk; });
  child.stderr.setEncoding("utf8").on("data", (chunk) => { stderr += chunk; });
  child.stdin.end(`${JSON.stringify({
    jsonrpc: "2.0",
    id: 1,
    method: "initialize",
    params: { protocolVersion: "bogus", capabilities: {}, clientInfo: { name: "raw-test", version: "1" } },
  })}\n`);
  const exitCode = await new Promise((resolve, reject) => {
    child.once("error", reject);
    child.once("close", resolve);
  });
  assert.equal(exitCode, 0, stderr);
  const response = JSON.parse(stdout.trim());
  assert.equal(response.result.protocolVersion, "2025-06-18");
  assert.notEqual(response.result.protocolVersion, "bogus");
});
