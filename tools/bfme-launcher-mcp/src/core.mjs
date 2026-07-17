import { execFile, spawn } from "node:child_process";
import { createHash } from "node:crypto";
import { constants as fsConstants } from "node:fs";
import { access, lstat, open, readFile, rename, rm } from "node:fs/promises";
import path from "node:path";
import { promisify } from "node:util";

const execFileAsync = promisify(execFile);

export const APPROVED_LAUNCHER_SHA256 = "D3C69A9846F085EDC847D88D30F470B6A1A568035C19FA309E7E66ED9748511D";

export const PROCESS_NAMES = new Set([
  "allinonelauncher.exe",
  "lotrbfme2.exe",
  "game.dat",
]);

export function fixedPaths(environment = process.env) {
  if (!environment.APPDATA) {
    throw new Error("APPDATA is unavailable; the BFME launcher location cannot be resolved.");
  }
  const launcherRoot = path.join(environment.APPDATA, "BFME All In One Launcher");
  const gameRoot = "F:\\BFME2";
  return Object.freeze({
    launcherRoot,
    launcherExe: path.join(launcherRoot, "AllInOneLauncher.exe"),
    settingsFile: path.join(launcherRoot, "settings.json"),
    gameRoot,
    wrapperExe: path.join(gameRoot, "lotrbfme2.exe"),
    gameExe: path.join(gameRoot, "game.dat"),
    powershellExe: "C:\\WINDOWS\\System32\\WindowsPowerShell\\v1.0\\powershell.exe",
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

export async function queryProcesses(paths = fixedPaths(), runner = execFileAsync) {
  const script = [
    "$ErrorActionPreference = 'Stop'",
    "$names = @('AllInOneLauncher.exe','lotrbfme2.exe','game.dat')",
    "$rows = @(Get-CimInstance Win32_Process | Where-Object { $_.Name -in $names } | Select-Object ProcessId,ParentProcessId,Name,ExecutablePath)",
    "if ($rows.Count -gt 0) { $rows | ConvertTo-Json -Compress }",
  ].join("; ");
  const { stdout } = await runner(paths.powershellExe, ["-NoProfile", "-NonInteractive", "-Command", script], {
    windowsHide: true,
    timeout: 5000,
    maxBuffer: 1024 * 1024,
  });
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

export async function getLauncherStatus(paths = fixedPaths(), processRunner = execFileAsync) {
  const [launcherExists, wrapperExists, gameExists, settings, processes] = await Promise.all([
    exists(paths.launcherExe),
    exists(paths.wrapperExe),
    exists(paths.gameExe),
    readSettings(paths),
    queryProcesses(paths, processRunner),
  ]);
  const classified = classifyProcesses(processes, paths);
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
    state: classified.some((item) => item.name.toLowerCase() === "game.dat" && item.verified)
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

export function spawnDetachedPowerShell(script, paths = fixedPaths()) {
  const child = spawn(paths.powershellExe, ["-NoProfile", "-NonInteractive", "-Command", script], {
    detached: true,
    stdio: "ignore",
    windowsHide: true,
  });
  child.unref();
  return child.pid;
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

export async function launchBfme2(paths = fixedPaths(), detachedRunner = spawnDetachedPowerShell) {
  if (!(await exists(paths.launcherExe))) {
    throw new Error(`The fixed launcher executable is missing: ${paths.launcherExe}`);
  }
  const identity = await verifyLauncherIdentity(paths);
  const launcher = psLiteral(paths.launcherExe);
  const approvedHash = psLiteral(APPROVED_LAUNCHER_SHA256);
  const script = [
    `$item = Get-Item -LiteralPath ${launcher} -Force`,
    "if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw 'Launcher path is a reparse point.' }",
    `$hash = (Get-FileHash -Algorithm SHA256 -LiteralPath ${launcher}).Hash`,
    `if ($hash -ne ${approvedHash}) { throw 'Launcher identity changed before execution.' }`,
    `Start-Process -FilePath ${launcher}`,
  ].join("; ");
  const helperPid = detachedRunner(script, paths);
  return {
    state: "launch_requested",
    helper_pid: helperPid ?? null,
    target: paths.launcherExe,
    launcher_sha256: identity,
    approval_required: true,
    message: "Windows may show a UAC prompt. Approve it on the desktop, then poll get_launcher_status.",
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
