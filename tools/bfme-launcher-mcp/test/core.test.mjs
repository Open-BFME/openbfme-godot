import assert from "node:assert/strict";
import { mkdtemp, mkdir, readFile, writeFile } from "node:fs/promises";
import os from "node:os";
import path from "node:path";
import test from "node:test";
import {
  APPROVED_LAUNCHER_SHA256,
  buildElevatedDiscoveryStopScript,
  buildVerifiedStopScript,
  classifyProcesses,
  configureLauncher,
  fixedPaths,
  launchBfme2,
  terminateBfmeTree,
} from "../src/core.mjs";

async function fixture() {
  const root = await mkdtemp(path.join(os.tmpdir(), "openbfme-launcher-mcp-"));
  const launcherRoot = path.join(root, "launcher");
  const gameRoot = path.join(root, "BFME2");
  await mkdir(launcherRoot);
  await mkdir(gameRoot);
  const paths = {
    launcherRoot,
    launcherExe: path.join(launcherRoot, "AllInOneLauncher.exe"),
    settingsFile: path.join(launcherRoot, "settings.json"),
    gameRoot,
    wrapperExe: path.join(gameRoot, "lotrbfme2.exe"),
    gameExe: path.join(gameRoot, "game.dat"),
    powershellExe: "powershell.exe",
  };
  await writeFile(paths.launcherExe, "fixture");
  await writeFile(paths.wrapperExe, "fixture");
  await writeFile(paths.gameExe, "fixture");
  await writeFile(paths.settingsFile, JSON.stringify({ KeepMe: 42, LaunchWithAffinity1: false, WindowedModeBfme2: false }));
  return paths;
}

test("fixed paths cannot be supplied by an MCP caller", () => {
  const paths = fixedPaths({ APPDATA: "C:\\Users\\Example\\AppData\\Roaming" });
  assert.equal(paths.launcherExe, "C:\\Users\\Example\\AppData\\Roaming\\BFME All In One Launcher\\AllInOneLauncher.exe");
  assert.equal(paths.gameExe, "F:\\BFME2\\game.dat");
  assert.ok(Object.isFrozen(paths));
});

test("configuration is atomic, typed, bounded, and preserves unknown launcher-owned keys", async () => {
  const paths = await fixture();
  const result = await configureLauncher({ launch_with_affinity_1: true }, paths);
  assert.deepEqual(result, { launch_with_affinity_1: true, windowed_bfme2: false });
  const stored = JSON.parse(await readFile(paths.settingsFile, "utf8"));
  assert.equal(stored.KeepMe, 42);
  assert.equal(stored.LaunchWithAffinity1, true);
  await assert.rejects(() => configureLauncher({ arbitrary: true }, paths), /Unsupported launcher setting/);
  await assert.rejects(() => configureLauncher({ windowed_bfme2: "yes" }, paths), /must be a boolean/);
  await assert.rejects(() => configureLauncher({}, paths), /At least one/);
});

test("process classification requires both a known name and exact executable path", async () => {
  const paths = await fixture();
  const classified = classifyProcesses([
    { pid: 1, parent_pid: 0, name: "game.dat", executable_path: paths.gameExe, command_line: null },
    { pid: 2, parent_pid: 0, name: "game.dat", executable_path: "C:\\Other\\game.dat", command_line: null },
    { pid: 3, parent_pid: 0, name: "game.dat", executable_path: null, command_line: null },
  ], paths);
  assert.deepEqual(classified.map((item) => item.verified), [true, false, false]);
});

test("launch invokes only a generated fixed-target script", async () => {
  const paths = await fixture();
  const crypto = await import("node:crypto");
  const approvedFixture = Buffer.from("fixture");
  assert.notEqual(crypto.createHash("sha256").update(approvedFixture).digest("hex").toUpperCase(), APPROVED_LAUNCHER_SHA256);
  await assert.rejects(() => launchBfme2(paths, () => { throw new Error("must not launch"); }), /identity mismatch/);

  paths.launcherExe = "C:\\Users\\Jonathan\\AppData\\Roaming\\BFME All In One Launcher\\AllInOneLauncher.exe";
  let script;
  const result = await launchBfme2(paths, (value) => { script = value; return 123; });
  assert.equal(result.target, paths.launcherExe);
  assert.equal(result.approval_required, true);
  assert.match(script, /AllInOneLauncher\.exe/);
  assert.match(script, /Get-FileHash/);
  assert.match(script, new RegExp(APPROVED_LAUNCHER_SHA256));
  assert.doesNotMatch(script, /lotrbfme2|game\.dat/i);
});

test("stop script revalidates exact path after elevation", () => {
  const script = buildVerifiedStopScript([{ pid: 99, name: "game.dat", verified: true, expected_path: "f:\\bfme2\\game.dat" }]);
  assert.match(script, /ProcessId = 99/);
  assert.match(script, /ExecutablePath\.ToLowerInvariant/);
  assert.match(script, /Stop-Process -Id 99/);
  assert.throws(() => buildVerifiedStopScript([{ pid: 99, verified: false }]), /path-verified/);
});

test("termination elevates when a matching process path is inaccessible", async () => {
  const paths = await fixture();
  const queryRunner = async (_exe, args) => {
    if (args.join(" ").includes("Get-CimInstance")) {
      return { stdout: JSON.stringify({ ProcessId: 55, ParentProcessId: 1, Name: "game.dat", ExecutablePath: null, CommandLine: null }), stderr: "" };
    }
    throw new Error("stop must not run");
  };
  let elevated;
  const result = await terminateBfmeTree(paths, queryRunner, (script) => { elevated = script; return 88; });
  assert.equal(result.state, "elevation_requested");
  assert.equal(result.reason, "process_path_inaccessible");
  assert.match(elevated, /Verb RunAs/);
});

test("elevated discovery contains only fixed names and exact expected paths", async () => {
  const paths = await fixture();
  const script = buildElevatedDiscoveryStopScript(paths);
  assert.match(script, /allinonelauncher\.exe/);
  assert.match(script, /lotrbfme2\.exe/);
  assert.match(script, /game\.dat/);
  assert.match(script, /ExecutablePath\.ToLowerInvariant/);
});

test("termination elevates when the non-elevated process query is denied", async () => {
  const paths = await fixture();
  let elevated;
  const result = await terminateBfmeTree(
    paths,
    async () => { const error = new Error("Access is denied"); error.stderr = "Access denied"; throw error; },
    (script) => { elevated = script; return 89; },
  );
  assert.equal(result.reason, "process_query_denied");
  assert.match(elevated, /Verb RunAs/);
});
