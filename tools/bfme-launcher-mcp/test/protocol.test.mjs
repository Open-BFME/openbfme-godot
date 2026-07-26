import assert from "node:assert/strict";
import { mkdtemp, mkdir, rm, writeFile } from "node:fs/promises";
import os from "node:os";
import path from "node:path";
import test from "node:test";
import { fileURLToPath } from "node:url";
import { Client } from "@modelcontextprotocol/sdk/client/index.js";
import { StdioClientTransport } from "@modelcontextprotocol/sdk/client/stdio.js";

/**
 * A synthetic install the server can be pointed at.
 *
 * This test used to read whatever was on the developer's disk: it asserted that
 * `installed.launcher` was true, which only held on a machine with the real BFME
 * All In One Launcher in %APPDATA%. It therefore passed for one person and
 * failed everywhere else — including the first CI run that ever executed it.
 *
 * The fix is a fixture, NOT a softened assertion: the server resolves its fixed
 * paths from APPDATA and BFME2_INSTALL (see fixedPaths/resolveGameRoot in
 * src/core.mjs), so handing it a temporary root makes "installed" a fact this
 * test establishes rather than a property of the host machine.
 */
async function makeFakeInstall() {
  const root = await mkdtemp(path.join(os.tmpdir(), "openbfme-mcp-protocol-"));
  const appData = path.join(root, "appdata");
  const launcherRoot = path.join(appData, "BFME All In One Launcher");
  const gameRoot = path.join(root, "game");
  await mkdir(launcherRoot, { recursive: true });
  await mkdir(gameRoot, { recursive: true });
  await writeFile(path.join(launcherRoot, "AllInOneLauncher.exe"), "not a real launcher");
  await writeFile(
    path.join(launcherRoot, "settings.json"),
    JSON.stringify({ LaunchWithAffinity1: true, WindowedModeBfme2: false }, null, 2),
  );
  await writeFile(path.join(gameRoot, "lotrbfme2.exe"), "not a real wrapper");
  await writeFile(path.join(gameRoot, "game.dat"), "not a real game");
  return { root, appData, launcherRoot, gameRoot };
}

test("stdio MCP initializes and exposes only the four bounded tools", async () => {
  const here = path.dirname(fileURLToPath(import.meta.url));
  const server = path.resolve(here, "../src/server.mjs");
  const install = await makeFakeInstall();
  const client = new Client({ name: "openbfme-launcher-test", version: "1.0.0" });
  // StdioClientTransport merges this over its default inherited environment, so
  // SYSTEMROOT and PATH (which the server needs to reach powershell.exe) survive.
  const transport = new StdioClientTransport({
    command: process.execPath,
    args: [server],
    env: { APPDATA: install.appData, BFME2_INSTALL: install.gameRoot },
  });
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
    // Prove the injected root is the one the server actually resolved. Without
    // this, a host that happens to have a real launcher installed would satisfy
    // the assertions below even if the environment never reached the child.
    assert.equal(payload.paths.launcher, path.join(install.launcherRoot, "AllInOneLauncher.exe"));
    assert.equal(payload.installed.launcher, true);
    assert.equal(payload.settings.launch_with_affinity_1, true);
    const invalid = await client.callTool({ name: "get_launcher_status", arguments: { executable: "C:\\evil.exe" } });
    assert.equal(invalid.isError, true);
  } finally {
    await client.close();
    await rm(install.root, { recursive: true, force: true });
  }
});
