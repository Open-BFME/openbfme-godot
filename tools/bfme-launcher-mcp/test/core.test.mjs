import assert from "node:assert/strict";
import { mkdtemp, mkdir, readFile, writeFile } from "node:fs/promises";
import os from "node:os";
import path from "node:path";
import test from "node:test";
import {
  APPROVED_LAUNCHER_SHA256,
  buildElevatedDiscoveryStopScript,
  buildHelperReceiptScript,
  buildVerifiedLaunchScript,
  buildVerifiedStopScript,
  classifyProcesses,
  configureLauncher,
  fixedPaths,
  helperOutcomeHistory,
  lastHelperOutcome,
  launchBfme2,
  refreshHelperOutcomes,
  resetHelperOutcomes,
  spawnDetachedPowerShell,
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
  const paths = fixedPaths({
    APPDATA: "C:\\Users\\Example\\AppData\\Roaming",
    BFME2_INSTALL: "D:\\Games\\BFME2",
  });
  assert.equal(paths.launcherExe, "C:\\Users\\Example\\AppData\\Roaming\\BFME All In One Launcher\\AllInOneLauncher.exe");
  assert.equal(paths.gameExe, "D:\\Games\\BFME2\\game.dat");
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

  // A missing launcher must be reported plainly rather than launching anything.
  const absent = { ...paths, launcherExe: path.join(paths.launcherRoot, "does-not-exist.exe") };
  await assert.rejects(
    () => launchBfme2(absent, () => { throw new Error("must not launch"); }),
    /launcher executable is missing/,
  );
});

// Asserted against the pure builder: reproducing the approved hash would
// require shipping the proprietary launcher binary, so the end-to-end path
// cannot be exercised off the maintainer's machine.
test("generated launch script re-verifies identity and targets only the launcher", () => {
  const script = buildVerifiedLaunchScript(
    "C:\\Users\\Example\\AppData\\Roaming\\BFME All In One Launcher\\AllInOneLauncher.exe",
  );
  assert.match(script, /AllInOneLauncher\.exe/);
  assert.match(script, /Get-FileHash/);
  assert.match(script, /ReparsePoint/);
  assert.match(script, new RegExp(APPROVED_LAUNCHER_SHA256));
  assert.doesNotMatch(script, /lotrbfme2|game\.dat/i);
});

test("stop script revalidates exact path after elevation", () => {
  const script = buildVerifiedStopScript([{ pid: 99, name: "game.dat", verified: true, expected_path: "d:\\games\\bfme2\\game.dat" }]);
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

// The detached helper used to run with stdio "ignore", so a helper that never started
// -- or started and failed -- produced a cheerful "launch_requested" and no trace at all.
test("a detached helper that cannot start is reported, not silently discarded", async (t) => {
  resetHelperOutcomes();
  t.after(resetHelperOutcomes);
  const paths = await fixture();
  const missing = { ...paths, powershellExe: path.join(paths.launcherRoot, "no-such-shell.exe") };

  assert.throws(
    () => spawnDetachedPowerShell("Write-Output 1", missing),
    /helper process could not be started/,
  );

  const outcome = lastHelperOutcome();
  assert.equal(outcome.state, "spawn_failed");
  assert.equal(outcome.pid, null);
  assert.match(outcome.error, /did not launch/);
  assert.equal(helperOutcomeHistory().length, 1);
});

// The receipt wrapper exists because a Windows `detached: true` child's exit code and
// stderr are NOT observable from Node — such a child reports exit 0 with no output even
// when it failed. The helper must therefore record its own result.
test("the helper receipt wrapper captures success and failure inside the script", () => {
  const script = buildHelperReceiptScript("Start-Process -FilePath 'x'", "C:\\temp\\r.json");
  assert.match(script, /Start-Process -FilePath 'x'/);
  assert.match(script, /catch \{/);
  assert.match(script, /ConvertTo-Json/);
  assert.match(script, /C:\\temp\\r\.json/);
});

// The wrapper is real PowerShell and must actually parse and run. Exercised
// non-detached so its behaviour is observable: `try { } ; catch { }` is a parse error,
// and a wrapper that fails to parse silently writes no receipt at all.
test("the receipt wrapper really runs and records both outcomes", async (t) => {
  if (process.platform !== "win32") return;
  const { execFile } = await import("node:child_process");
  const { promisify } = await import("node:util");
  const run = promisify(execFile);
  const root = await mkdtemp(path.join(os.tmpdir(), "openbfme-receipt-"));

  for (const [body, expected] of [["$null = 1 + 1", "completed"], ["throw 'boom'", "failed"]]) {
    const receipt = path.join(root, `${expected}.json`);
    const encoded = Buffer.from(
      buildHelperReceiptScript(body, receipt), "utf16le").toString("base64");
    await run("powershell.exe", ["-NoProfile", "-NonInteractive", "-EncodedCommand", encoded]);
    const written = JSON.parse(await readFile(receipt, "utf8"));
    assert.equal(written.state, expected);
    if (expected === "failed") assert.match(written.error, /boom/);
  }
});

// Detached helpers are spawned so they can outlive a UAC prompt, which also makes their
// exit code and stderr unreadable from Node on Windows. The receipt is therefore the
// only trustworthy signal, so refresh is asserted directly against one.
test("a helper receipt is read back into the reported outcome", async (t) => {
  if (process.platform !== "win32") return;
  resetHelperOutcomes();
  t.after(resetHelperOutcomes);

  spawnDetachedPowerShell("$null = 1 + 1", fixedPaths(process.env));
  const outcome = lastHelperOutcome();
  assert.equal(outcome.state, "running");

  await mkdir(path.dirname(outcome.receipt), { recursive: true });
  await writeFile(outcome.receipt, JSON.stringify({ state: "failed", error: "UAC denied" }));
  await refreshHelperOutcomes();

  assert.equal(lastHelperOutcome().state, "failed");
  assert.equal(lastHelperOutcome().error, "UAC denied");
});

// The failure mode that matters most: the helper is gone and left no receipt. That is
// what a dismissed UAC prompt looks like, and it must never be read as success.
test("a helper that ends without a receipt is reported, never assumed successful", async (t) => {
  if (process.platform !== "win32") return;
  resetHelperOutcomes();
  t.after(resetHelperOutcomes);

  spawnDetachedPowerShell("$null = 1 + 1", fixedPaths(process.env));
  const outcome = lastHelperOutcome();

  const deadline = Date.now() + 30_000;
  while (lastHelperOutcome().state === "running" && Date.now() < deadline) {
    await new Promise((resolve) => setTimeout(resolve, 100));
    await refreshHelperOutcomes();
  }
  assert.notEqual(lastHelperOutcome().state, "completed");
  assert.notEqual(lastHelperOutcome().state, "running");
  assert.match(lastHelperOutcome().error, /without recording a result/);
  assert.equal(lastHelperOutcome().pid, outcome.pid);
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
