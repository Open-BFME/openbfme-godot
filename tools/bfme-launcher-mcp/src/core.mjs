import { execFile, spawn } from "node:child_process";
import { createHash } from "node:crypto";
import { constants as fsConstants, statSync } from "node:fs";
import { access, lstat, open, readFile, rename, rm } from "node:fs/promises";
import os from "node:os";
import path from "node:path";
import { promisify } from "node:util";

const execFileAsync = promisify(execFile);

export const APPROVED_LAUNCHER_SHA256 = "D3C69A9846F085EDC847D88D30F470B6A1A568035C19FA309E7E66ED9748511D";

export const PROCESS_NAMES = new Set([
  "allinonelauncher.exe",
  "lotrbfme2.exe",
  "game.dat",
]);

// Locations a stock BFME2 install is normally found at, relative to a probe
// root. Kept in step with importer/openbfme_importer/paths.py.
const RETAIL_RELATIVE_DIRS = [
  "Electronic Arts\\The Battle for Middle-earth II",
  "EA Games\\The Battle for Middle-earth II",
  "Steam\\steamapps\\common\\The Battle for Middle-earth II",
  "GOG Galaxy\\Games\\The Battle for Middle-earth II",
];

/**
 * Candidate retail install directories for this machine, most likely first.
 * Every entry is derived from the environment — never from a developer's disk.
 */
export function retailInstallCandidates(environment = process.env) {
  const roots = [];
  for (const name of ["ProgramFiles(x86)", "ProgramFiles", "ProgramW6432"]) {
    if (environment[name]) {
      roots.push(environment[name]);
    }
  }
  for (const letter of ["C", "D", "E", "F", "G"]) {
    roots.push(`${letter}:\\`, `${letter}:\\Games`);
  }
  const candidates = [];
  for (const root of roots) {
    for (const relative of RETAIL_RELATIVE_DIRS) {
      candidates.push(path.win32.join(root, relative));
    }
  }
  return candidates;
}

/**
 * Resolve the retail game root.
 *
 * BFME2_INSTALL always wins so a player can point this anywhere. Otherwise the
 * first candidate directory that actually contains game.dat is used. If none
 * matches we fall back to the first candidate so callers still get a concrete
 * path to report in their "not installed" diagnostics.
 */
export function resolveGameRoot(environment = process.env, probe = existsSyncSafe) {
  const configured = (environment.BFME2_INSTALL ?? "").trim();
  if (configured) {
    return path.win32.normalize(configured);
  }
  const candidates = retailInstallCandidates(environment);
  for (const candidate of candidates) {
    if (probe(path.win32.join(candidate, "game.dat"))) {
      return candidate;
    }
  }
  return candidates[0];
}

function existsSyncSafe(file) {
  try {
    return statSync(file).isFile();
  } catch {
    return false;
  }
}

export function fixedPaths(environment = process.env) {
  if (!environment.APPDATA) {
    throw new Error("APPDATA is unavailable; the BFME launcher location cannot be resolved.");
  }
  const launcherRoot = path.join(environment.APPDATA, "BFME All In One Launcher");
  const gameRoot = resolveGameRoot(environment);
  const systemRoot = environment.SystemRoot || environment.windir || "C:\\Windows";
  return Object.freeze({
    launcherRoot,
    launcherExe: path.join(launcherRoot, "AllInOneLauncher.exe"),
    settingsFile: path.join(launcherRoot, "settings.json"),
    gameRoot,
    wrapperExe: path.join(gameRoot, "lotrbfme2.exe"),
    gameExe: path.join(gameRoot, "game.dat"),
    powershellExe: path.join(systemRoot, "System32", "WindowsPowerShell", "v1.0", "powershell.exe"),
  });
}

function canonical(value) {
  return path.win32.normalize(value).toLowerCase();
}

async function exists(file) {
  try {
    await access(file, fsConstants.F_OK);
    return true;
  } catch {
    return false;
  }
}

export async function readSettings(paths = fixedPaths()) {
  const raw = await readFile(paths.settingsFile, "utf8");
  const parsed = JSON.parse(raw);
  if (!parsed || Array.isArray(parsed) || typeof parsed !== "object") {
    throw new Error("Launcher settings must be a JSON object.");
  }
  return parsed;
}

export async function configureLauncher(input, paths = fixedPaths()) {
  const allowed = new Set(["launch_with_affinity_1", "windowed_bfme2"]);
  const keys = Object.keys(input ?? {});
  if (keys.length === 0) {
    throw new Error("At least one launcher setting must be supplied.");
  }
  const unknown = keys.filter((key) => !allowed.has(key));
  if (unknown.length > 0) {
    throw new Error(`Unsupported launcher setting: ${unknown.join(", ")}`);
  }
  for (const key of keys) {
    if (typeof input[key] !== "boolean") {
      throw new Error(`${key} must be a boolean.`);
    }
  }

  const settings = await readSettings(paths);
  if ("launch_with_affinity_1" in input) {
    settings.LaunchWithAffinity1 = input.launch_with_affinity_1;
  }
  if ("windowed_bfme2" in input) {
    settings.WindowedModeBfme2 = input.windowed_bfme2;
  }

  const temporary = `${paths.settingsFile}.openbfme-${process.pid}-${Date.now()}.tmp`;
  const payload = `${JSON.stringify(settings, null, 2)}\n`;
  let handle;
  try {
    handle = await open(temporary, "wx");
    await handle.writeFile(payload, "utf8");
    await handle.sync();
    await handle.close();
    handle = undefined;
    await rename(temporary, paths.settingsFile);
  } finally {
    if (handle) await handle.close().catch(() => {});
    await rm(temporary, { force: true }).catch(() => {});
  }

  return {
    launch_with_affinity_1: Boolean(settings.LaunchWithAffinity1),
    windowed_bfme2: Boolean(settings.WindowedModeBfme2),
  };
}

export function parseProcessJson(stdout) {
  const trimmed = stdout.trim();
  if (!trimmed) return [];
  const value = JSON.parse(trimmed);
  return (Array.isArray(value) ? value : [value]).map((item) => ({
    pid: Number(item.ProcessId),
    parent_pid: Number(item.ParentProcessId),
    name: String(item.Name ?? ""),
    executable_path: item.ExecutablePath ? String(item.ExecutablePath) : null,
  }));
}

/** How long the non-elevated Win32_Process query is allowed to take. */
export const PROCESS_QUERY_TIMEOUT_MS = 5000;

/**
 * The BFME names as a WQL WHERE clause, built from the one list that defines them.
 *
 * The filter is pushed into the query instead of being applied afterwards with
 * `Where-Object`. Piping every process through PowerShell makes the CIM provider
 * materialise and marshal all of them first — including `ExecutablePath`, which it has to
 * open each process to read — only for the pipeline to discard all but three. On a host
 * with slow WMI that work alone can outlast the timeout above. WQL compares strings
 * case-insensitively, so this matches exactly what the pipeline filter matched.
 *
 * Wrapped in a PowerShell single-quoted literal so the command line carries no double
 * quotes: what survives Windows command-line quoting is the recurring hazard in this file.
 */
const PROCESS_QUERY_SCRIPT = [
  "$ErrorActionPreference = 'Stop'",
  `$rows = @(Get-CimInstance Win32_Process -Filter ${psLiteral(
    [...PROCESS_NAMES].map((name) => `Name='${name}'`).join(" OR "),
  )} | Select-Object ProcessId,ParentProcessId,Name,ExecutablePath)`,
  "if ($rows.Count -gt 0) { $rows | ConvertTo-Json -Compress }",
].join("; ");

/**
 * Explain an execFile rejection that was actually our own timeout firing.
 *
 * A timeout kill arrives as `Command failed: <the entire command line>` with an empty
 * stderr and no mention of a timeout anywhere — the least informative error in this
 * module. Only that case is rewritten; every other rejection is rethrown untouched, so the
 * elevation detection in `terminateBfmeTree` still sees the original message and stderr.
 */
function describeProcessQueryFailure(error, paths) {
  if (error?.killed !== true) return error;
  const described = new Error(
    `The BFME process query timed out after ${PROCESS_QUERY_TIMEOUT_MS} ms and was killed. ` +
    `${paths.powershellExe} started, but its Get-CimInstance Win32_Process query did not ` +
    "answer in time; that is a property of this host's WMI service, not of the launcher.",
  );
  described.code = "ETIMEDOUT";
  described.cause = error;
  described.stderr = error.stderr ?? "";
  return described;
}

export async function queryProcesses(paths = fixedPaths(), runner = execFileAsync) {
  let stdout;
  try {
    ({ stdout } = await runner(paths.powershellExe, ["-NoProfile", "-NonInteractive", "-Command", PROCESS_QUERY_SCRIPT], {
      windowsHide: true,
      timeout: PROCESS_QUERY_TIMEOUT_MS,
      maxBuffer: 1024 * 1024,
    }));
  } catch (error) {
    throw describeProcessQueryFailure(error, paths);
  }
  return parseProcessJson(stdout);
}

export function classifyProcesses(processes, paths = fixedPaths()) {
  const expected = new Map([
    ["allinonelauncher.exe", canonical(paths.launcherExe)],
    ["lotrbfme2.exe", canonical(paths.wrapperExe)],
    ["game.dat", canonical(paths.gameExe)],
  ]);
  return processes.map((process) => {
    const name = process.name.toLowerCase();
    const expectedPath = expected.get(name) ?? null;
    const verified = Boolean(
      PROCESS_NAMES.has(name) &&
      process.executable_path &&
      expectedPath === canonical(process.executable_path),
    );
    return { ...process, expected_path: expectedPath, verified };
  });
}

/**
 * Run the process query, reporting a failure as data rather than throwing.
 *
 * Whether WMI answered is a separate question from whether the launcher is installed and
 * how it is configured. Letting a slow or blocked Win32_Process query fail the entire tool
 * threw away the two facts a caller can always be told, and made `get_launcher_status`
 * unusable on any host whose WMI is slow — GitHub's Windows runners among them, where the
 * query exceeds its budget on every run.
 *
 * Degrading is not the same as pretending. An empty `processes` list here means "could not
 * look", and the caller is told so twice: `process_query.ok` is false with the reason, and
 * `state` becomes "process_state_unknown" so nothing can read it as "nothing is running".
 */
async function attemptProcessQuery(paths, runner) {
  try {
    return { ok: true, error: null, processes: await queryProcesses(paths, runner) };
  } catch (error) {
    const message = error instanceof Error ? error.message : String(error);
    console.error(`[bfme-launcher] process query failed: ${message}`);
    return { ok: false, error: message, processes: [] };
  }
}

export async function getLauncherStatus(paths = fixedPaths(), processRunner = execFileAsync) {
  const [launcherExists, wrapperExists, gameExists, settings, query] = await Promise.all([
    exists(paths.launcherExe),
    exists(paths.wrapperExe),
    exists(paths.gameExe),
    readSettings(paths),
    attemptProcessQuery(paths, processRunner),
    // launch_bfme2 and the elevated stop both return before their helper finishes, so
    // this poll is where their real outcome becomes visible.
    refreshHelperOutcomes(),
  ]);
  const classified = classifyProcesses(query.processes, paths);
  return {
    paths: {
      launcher: paths.launcherExe,
      settings: paths.settingsFile,
      game_root: paths.gameRoot,
    },
    installed: { launcher: launcherExists, wrapper: wrapperExists, game: gameExists },
    settings: {
      launch_with_affinity_1: Boolean(settings.LaunchWithAffinity1),
      windowed_bfme2: Boolean(settings.WindowedModeBfme2),
    },
    processes: classified,
    // Whether the machine could be asked at all. When `ok` is false the list above is
    // empty because the query failed, not because nothing is running — see the note on
    // attemptProcessQuery. Only a query that succeeded may report "stopped".
    process_query: { ok: query.ok, error: query.error },
    // The outcome of the last detached helper. `launch_bfme2` and the elevated stop
    // path both return immediately by design, so this is where their real result
    // becomes visible: a helper that threw reports state "failed" with its reason, and
    // one that ended without a receipt (a dismissed UAC prompt) reports "vanished".
    last_helper: lastHelperOutcome(),
    state: !query.ok
      ? "process_state_unknown"
      : classified.some((item) => item.name.toLowerCase() === "game.dat" && item.verified)
        ? "game_running"
        : classified.some((item) => item.name.toLowerCase() === "lotrbfme2.exe" && item.verified)
          ? "wrapper_running"
          : classified.some((item) => item.name.toLowerCase() === "allinonelauncher.exe" && item.verified)
            ? "launcher_running"
            : classified.length > 0
              ? "unverified_matching_process"
              : "stopped",
  };
}

/**
 * Diagnostics for the most recent detached helper, newest last.
 *
 * A detached helper is the only way to survive the UAC prompt, but it used to be
 * spawned with `stdio: "ignore"`, so a helper that failed to start, threw, or was
 * denied elevation left no trace anywhere: the MCP call returned a cheerful
 * "launch_requested" and nothing ever happened. Silent degradation is the worst
 * outcome this project can ship, so every helper's outcome is now recorded and
 * surfaced through `get_launcher_status` and the launch/stop results.
 *
 * States: "running", "completed", "failed" (the helper ran and reported why it
 * failed), "spawn_failed" (nothing started), "vanished" (the process ended without
 * recording anything — what a dismissed UAC prompt looks like).
 */
const helperOutcomes = [];
const HELPER_HISTORY_LIMIT = 8;

/** The most recent detached-helper outcome, or null if none has run. */
export function lastHelperOutcome() {
  return helperOutcomes.length === 0 ? null : helperOutcomes[helperOutcomes.length - 1];
}

/** Every recorded detached-helper outcome, oldest first. */
export function helperOutcomeHistory() {
  return helperOutcomes.slice();
}

/** The recorded outcome for a specific helper pid, or null when it was not ours. */
export function helperOutcomeFor(pid) {
  if (pid === undefined || pid === null) return null;
  for (let index = helperOutcomes.length - 1; index >= 0; index -= 1) {
    if (helperOutcomes[index].pid === pid) return helperOutcomes[index];
  }
  return null;
}

/** Test seam: forget recorded helper outcomes. */
export function resetHelperOutcomes() {
  helperOutcomes.length = 0;
}

function recordHelperOutcome(outcome) {
  helperOutcomes.push(outcome);
  if (helperOutcomes.length > HELPER_HISTORY_LIMIT) helperOutcomes.shift();
  return outcome;
}

/** Where detached helpers drop their outcome receipts. */
export function helperReceiptRoot() {
  return path.join(os.tmpdir(), "openbfme-launcher-helpers");
}

/**
 * Wrap a helper script so it records its own outcome on disk.
 *
 * This indirection is not decoration. On Windows a `detached: true` child's exit code
 * and piped stderr are NOT observable from Node: a helper that exits 3 after writing to
 * stderr is reported to the parent as exit 0 with no output. Believing that would be
 * worse than the original bug, because it would actively assert success. The helper
 * therefore writes its own result, and the parent reads the receipt.
 */
export function buildHelperReceiptScript(script, receiptPath) {
  // Joined with newlines, not "; ": `try { } ; catch { }` is a PowerShell parse error,
  // and a wrapper that fails to parse writes no receipt at all.
  return [
    "$ErrorActionPreference = 'Stop'",
    `$openbfmeReceipt = ${psLiteral(receiptPath)}`,
    "$openbfmeResult = [ordered]@{ state = 'failed'; error = 'The helper did not run.' }",
    "try {",
    `  ${script}`,
    "  $openbfmeResult.state = 'completed'",
    "  $openbfmeResult.error = $null",
    "} catch {",
    "  $openbfmeResult.state = 'failed'",
    "  $openbfmeResult.error = $_.Exception.Message",
    "} finally {",
    "  New-Item -ItemType Directory -Force -Path (Split-Path $openbfmeReceipt -Parent) | Out-Null",
    "  [IO.File]::WriteAllText($openbfmeReceipt, ($openbfmeResult | ConvertTo-Json -Compress))",
    "}",
  ].join("\n");
}

export function spawnDetachedPowerShell(script, paths = fixedPaths()) {
  const receipt = path.join(
    helperReceiptRoot(),
    `helper-${process.pid}-${Date.now()}-${Math.random().toString(36).slice(2, 10)}.json`,
  );
  // -EncodedCommand (base64 UTF-16LE), not -Command: the wrapper is multi-line, and a
  // multi-line -Command string does not survive Windows command-line quoting intact —
  // PowerShell then exits before writing any receipt. Encoding sidesteps quoting
  // entirely, and is the same mechanism the elevated stop helper already uses.
  const wrapped = Buffer.from(
    buildHelperReceiptScript(script, receipt),
    "utf16le",
  ).toString("base64");

  const child = spawn(paths.powershellExe, ["-NoProfile", "-NonInteractive", "-EncodedCommand", wrapped], {
    detached: true,
    stdio: "ignore",
    windowsHide: true,
  });

  // spawn() reports ENOENT asynchronously, but an unspawnable child has no pid at all,
  // so this is a reliable synchronous signal that nothing was started.
  if (child.pid === undefined) {
    const message = `The helper process could not be started: ${paths.powershellExe} did not launch.`;
    const failure = recordHelperOutcome({
      pid: null,
      receipt,
      started_at: new Date().toISOString(),
      state: "spawn_failed",
      error: message,
    });
    // The 'error' event still arrives on a later tick. It must be consumed here, or
    // Node raises it as an uncaught exception and takes the MCP server down with it.
    child.on("error", (error) => {
      failure.error = `${message} (${error?.message ?? error})`;
    });
    throw new Error(message);
  }

  const outcome = recordHelperOutcome({
    pid: child.pid,
    receipt,
    started_at: new Date().toISOString(),
    state: "running",
    error: null,
  });

  child.on("error", (error) => {
    outcome.state = "spawn_failed";
    outcome.error = error?.message ?? String(error);
    console.error(`[bfme-launcher] helper failed to start: ${outcome.error}`);
  });

  child.unref();
  return child.pid;
}

/**
 * Bring recorded helper outcomes up to date from their receipts.
 *
 * A helper that is gone with no receipt is reported as `vanished` rather than assumed
 * to have succeeded — that state is what a dismissed UAC prompt looks like, and it is
 * precisely the outcome the old `stdio: "ignore"` code threw away.
 */
export async function refreshHelperOutcomes() {
  for (const outcome of helperOutcomes) {
    if (outcome.state !== "running") continue;
    let receipt;
    try {
      receipt = JSON.parse(await readFile(outcome.receipt, "utf8"));
    } catch {
      if (isProcessAlive(outcome.pid)) continue; // still working; nothing to report yet
      outcome.state = "vanished";
      outcome.error =
        "The helper process ended without recording a result. On Windows this normally " +
        "means the User Account Control prompt was dismissed or denied, so nothing ran.";
      console.error(`[bfme-launcher] ${outcome.error}`);
      continue;
    }
    outcome.state = receipt?.state === "completed" ? "completed" : "failed";
    outcome.error = receipt?.error ?? null;
    if (outcome.state === "failed") {
      console.error(`[bfme-launcher] helper failed: ${outcome.error ?? "no reason was recorded"}`);
    }
    await rm(outcome.receipt, { force: true }).catch(() => {});
  }
  return helperOutcomes.slice();
}

function isProcessAlive(pid) {
  if (typeof pid !== "number") return false;
  try {
    process.kill(pid, 0);
    return true;
  } catch (error) {
    return error?.code === "EPERM"; // alive, but owned by another user
  }
}

function psLiteral(value) {
  return `'${String(value).replaceAll("'", "''")}'`;
}

export async function verifyLauncherIdentity(paths = fixedPaths()) {
  const metadata = await lstat(paths.launcherExe);
  if (!metadata.isFile() || metadata.isSymbolicLink()) {
    throw new Error("The fixed launcher path is not a regular non-link file.");
  }
  const handle = await open(paths.launcherExe, "r");
  const hash = createHash("sha256");
  try {
    for await (const chunk of handle.createReadStream({ autoClose: false })) hash.update(chunk);
  } finally {
    await handle.close();
  }
  const observed = hash.digest("hex").toUpperCase();
  if (observed !== APPROVED_LAUNCHER_SHA256) {
    throw new Error(`Launcher identity mismatch: expected ${APPROVED_LAUNCHER_SHA256}, observed ${observed}.`);
  }
  return observed;
}

/**
 * Build the PowerShell that launches the approved launcher.
 *
 * Pure and exported so the generated script can be asserted on any machine,
 * without needing the proprietary launcher binary present on disk.
 *
 * The script re-checks the hash immediately before Start-Process: the
 * verification in launchBfme2 happens in this process, and the file could be
 * swapped between then and execution.
 */
export function buildVerifiedLaunchScript(launcherExe) {
  const launcher = psLiteral(launcherExe);
  const approvedHash = psLiteral(APPROVED_LAUNCHER_SHA256);
  return [
    `$item = Get-Item -LiteralPath ${launcher} -Force`,
    "if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw 'Launcher path is a reparse point.' }",
    `$hash = (Get-FileHash -Algorithm SHA256 -LiteralPath ${launcher}).Hash`,
    `if ($hash -ne ${approvedHash}) { throw 'Launcher identity changed before execution.' }`,
    `Start-Process -FilePath ${launcher}`,
  ].join("; ");
}

export async function launchBfme2(paths = fixedPaths(), detachedRunner = spawnDetachedPowerShell) {
  if (!(await exists(paths.launcherExe))) {
    throw new Error(`The fixed launcher executable is missing: ${paths.launcherExe}`);
  }
  const identity = await verifyLauncherIdentity(paths);
  const script = buildVerifiedLaunchScript(paths.launcherExe);
  const helperPid = detachedRunner(script, paths);
  return {
    state: "launch_requested",
    helper_pid: helperPid ?? null,
    helper: helperOutcomeFor(helperPid),
    target: paths.launcherExe,
    launcher_sha256: identity,
    approval_required: true,
    message:
      "Windows may show a UAC prompt. Approve it on the desktop, then poll get_launcher_status. " +
      "If the game does not appear, read last_helper in that status: a failed or dismissed " +
      "helper is reported there rather than being silently discarded.",
  };
}

export function buildVerifiedStopScript(processes) {
  if (processes.length === 0 || processes.some((item) => !item.verified)) {
    throw new Error("Only a non-empty set of path-verified BFME processes can be stopped.");
  }
  const checks = processes.map((item) => {
    const expected = psLiteral(item.expected_path);
    return [
      `$p = Get-CimInstance Win32_Process -Filter \"ProcessId = ${item.pid}\"`,
      `if ($null -ne $p -and $p.ExecutablePath.ToLowerInvariant() -eq ${expected}.ToLowerInvariant()) { Stop-Process -Id ${item.pid} -Force -ErrorAction Stop }`,
    ].join("; ");
  });
  return `$ErrorActionPreference = 'Stop'; ${checks.join("; ")}`;
}

export function buildElevatedDiscoveryStopScript(paths = fixedPaths()) {
  const expected = [
    ["allinonelauncher.exe", paths.launcherExe],
    ["lotrbfme2.exe", paths.wrapperExe],
    ["game.dat", paths.gameExe],
  ];
  const table = expected.map(([name, executable]) => `${psLiteral(name)} = ${psLiteral(canonical(executable))}`).join("; ");
  return [
    "$ErrorActionPreference = 'Stop'",
    `$expected = @{ ${table} }`,
    "$rows = @(Get-CimInstance Win32_Process | Where-Object { $expected.ContainsKey($_.Name.ToLowerInvariant()) })",
    "foreach ($p in $rows) { $wanted = $expected[$p.Name.ToLowerInvariant()]; if ($null -ne $p.ExecutablePath -and $p.ExecutablePath.ToLowerInvariant() -eq $wanted) { Stop-Process -Id $p.ProcessId -Force -ErrorAction Stop } }",
  ].join("; ");
}

function requestElevatedStop(paths, detachedRunner, reason) {
  const discovery = buildElevatedDiscoveryStopScript(paths);
  const elevated = `Start-Process -FilePath ${psLiteral(paths.powershellExe)} -Verb RunAs -ArgumentList @('-NoProfile','-NonInteractive','-EncodedCommand',[Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes(${psLiteral(discovery)}))) -Wait`;
  const helperPid = detachedRunner(elevated, paths);
  return {
    state: "elevation_requested",
    approval_required: true,
    helper_pid: helperPid ?? null,
    helper: helperOutcomeFor(helperPid),
    reason,
    message: "Approve the Windows UAC prompt. The elevated helper discovers only the fixed BFME names and revalidates every executable path before stopping it.",
  };
}

export async function terminateBfmeTree(
  paths = fixedPaths(),
  processRunner = execFileAsync,
  detachedRunner = spawnDetachedPowerShell,
) {
  let observed;
  try {
    observed = await queryProcesses(paths, processRunner);
  } catch (error) {
    const message = `${error?.message ?? ""} ${error?.stderr ?? ""}`;
    if (/access.+denied|requested operation requires elevation|privilege/i.test(message)) {
      return requestElevatedStop(paths, detachedRunner, "process_query_denied");
    }
    throw error;
  }
  const classified = classifyProcesses(observed, paths);
  const matching = classified.filter((item) => PROCESS_NAMES.has(item.name.toLowerCase()));
  const mismatched = matching.filter((item) => item.executable_path && !item.verified);
  if (mismatched.length > 0) {
    return {
      state: "refused_unverified_process",
      approval_required: false,
      processes: mismatched,
      message: "A matching process has an unexpected executable path; nothing was terminated.",
    };
  }
  const inaccessible = matching.filter((item) => !item.executable_path);
  if (inaccessible.length > 0) {
    return { ...requestElevatedStop(paths, detachedRunner, "process_path_inaccessible"), processes: inaccessible };
  }
  if (matching.length === 0) {
    return { state: "already_stopped", approval_required: false, processes: [] };
  }

  const script = buildVerifiedStopScript(matching);
  try {
    await processRunner(paths.powershellExe, ["-NoProfile", "-NonInteractive", "-Command", script], {
      windowsHide: true,
      timeout: 5000,
      maxBuffer: 1024 * 1024,
    });
    return { state: "stopped", approval_required: false, processes: matching };
  } catch (error) {
    const message = `${error?.message ?? ""} ${error?.stderr ?? ""}`;
    if (!/access.+denied|requested operation requires elevation|privilege/i.test(message)) {
      throw error;
    }
    return { ...requestElevatedStop(paths, detachedRunner, "process_stop_denied"), processes: matching };
  }
}
