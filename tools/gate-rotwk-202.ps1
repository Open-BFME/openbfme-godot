[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[A-Z0-9]+(?:-[A-Z0-9]+)+$')]
    [string] $WorkItemId,
    [switch] $SelfTest
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$targetBaseline = 'rotwk-202-v9.7.7-en'
$liveTimeoutMilliseconds = 600000
$pinnedGodot = [ordered]@{
    version = '4.7.stable.official.5b4e0cb0f'
    consoleName = 'Godot_v4.7-stable_win64_console.exe'
    consoleSha256 = 'd8055fb8c7e7f5010d7439ec69be051554055dae55a265f8647bd7301c34161c'
    engineName = 'Godot_v4.7-stable_win64.exe'
    engineSha256 = 'b2ca888d5115a6cedee564764a2ee494a625f2ec2edbabd010fe33c9a88a6bf8'
}
$ambientGodotVariables = @('OPENBFME_GODOT', 'GODOT_CONSOLE', 'GODOT_EXE', 'GODOT')
$controlPlanePaths = @(
    'orchestration/work-items.json', 'tools/work-item.py', 'tools/work-item.ps1',
    'tools/check-work-items.py'
)

Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading.Tasks;
using Microsoft.Win32.SafeHandles;

namespace OpenBFME {
    public sealed class ContainedResult {
        public int ExitCode;
        public string Stdout;
        public string Stderr;
        public bool TimedOut;
    }

    public static class JobRunner {
        const uint CREATE_SUSPENDED = 0x00000004;
        const uint CREATE_NO_WINDOW = 0x08000000;
        const uint STARTF_USESTDHANDLES = 0x00000100;
        const uint WAIT_OBJECT_0 = 0;
        const uint WAIT_TIMEOUT = 258;
        const uint HANDLE_FLAG_INHERIT = 1;
        const int JobObjectExtendedLimitInformation = 9;
        const uint JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE = 0x00002000;
        const int STD_INPUT_HANDLE = -10;

        [StructLayout(LayoutKind.Sequential)]
        struct SECURITY_ATTRIBUTES {
            public int nLength;
            public IntPtr lpSecurityDescriptor;
            [MarshalAs(UnmanagedType.Bool)] public bool bInheritHandle;
        }

        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
        struct STARTUPINFO {
            public int cb;
            public string lpReserved;
            public string lpDesktop;
            public string lpTitle;
            public uint dwX;
            public uint dwY;
            public uint dwXSize;
            public uint dwYSize;
            public uint dwXCountChars;
            public uint dwYCountChars;
            public uint dwFillAttribute;
            public uint dwFlags;
            public short wShowWindow;
            public short cbReserved2;
            public IntPtr lpReserved2;
            public IntPtr hStdInput;
            public IntPtr hStdOutput;
            public IntPtr hStdError;
        }

        [StructLayout(LayoutKind.Sequential)]
        struct PROCESS_INFORMATION {
            public IntPtr hProcess;
            public IntPtr hThread;
            public uint dwProcessId;
            public uint dwThreadId;
        }

        [StructLayout(LayoutKind.Sequential)]
        struct JOBOBJECT_BASIC_LIMIT_INFORMATION {
            public long PerProcessUserTimeLimit;
            public long PerJobUserTimeLimit;
            public uint LimitFlags;
            public UIntPtr MinimumWorkingSetSize;
            public UIntPtr MaximumWorkingSetSize;
            public uint ActiveProcessLimit;
            public UIntPtr Affinity;
            public uint PriorityClass;
            public uint SchedulingClass;
        }

        [StructLayout(LayoutKind.Sequential)]
        struct IO_COUNTERS {
            public ulong ReadOperationCount;
            public ulong WriteOperationCount;
            public ulong OtherOperationCount;
            public ulong ReadTransferCount;
            public ulong WriteTransferCount;
            public ulong OtherTransferCount;
        }

        [StructLayout(LayoutKind.Sequential)]
        struct JOBOBJECT_EXTENDED_LIMIT_INFORMATION {
            public JOBOBJECT_BASIC_LIMIT_INFORMATION BasicLimitInformation;
            public IO_COUNTERS IoInfo;
            public UIntPtr ProcessMemoryLimit;
            public UIntPtr JobMemoryLimit;
            public UIntPtr PeakProcessMemoryUsed;
            public UIntPtr PeakJobMemoryUsed;
        }

        [DllImport("kernel32.dll", SetLastError = true)]
        static extern bool CreatePipe(out IntPtr read, out IntPtr write, ref SECURITY_ATTRIBUTES attributes, uint size);
        [DllImport("kernel32.dll", SetLastError = true)]
        static extern bool SetHandleInformation(IntPtr handle, uint mask, uint flags);
        [DllImport("kernel32.dll")]
        static extern IntPtr GetStdHandle(int id);
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        static extern bool CreateProcessW(string application, StringBuilder commandLine, IntPtr processAttributes,
            IntPtr threadAttributes, bool inheritHandles, uint flags, IntPtr environment, string currentDirectory,
            ref STARTUPINFO startup, out PROCESS_INFORMATION process);
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        static extern IntPtr CreateJobObjectW(IntPtr attributes, string name);
        [DllImport("kernel32.dll", SetLastError = true)]
        static extern bool SetInformationJobObject(IntPtr job, int informationClass, IntPtr information, uint length);
        [DllImport("kernel32.dll", SetLastError = true)]
        static extern bool AssignProcessToJobObject(IntPtr job, IntPtr process);
        [DllImport("kernel32.dll", SetLastError = true)]
        static extern uint ResumeThread(IntPtr thread);
        [DllImport("kernel32.dll", SetLastError = true)]
        static extern uint WaitForSingleObject(IntPtr handle, uint milliseconds);
        [DllImport("kernel32.dll", SetLastError = true)]
        static extern bool TerminateJobObject(IntPtr job, uint exitCode);
        [DllImport("kernel32.dll", SetLastError = true)]
        static extern bool GetExitCodeProcess(IntPtr process, out uint exitCode);
        [DllImport("kernel32.dll", SetLastError = true)]
        static extern bool TerminateProcess(IntPtr process, uint exitCode);
        [DllImport("kernel32.dll", SetLastError = true)]
        static extern bool CloseHandle(IntPtr handle);

        static void Check(bool ok, string operation) {
            if (!ok) throw new Win32Exception(Marshal.GetLastWin32Error(), operation);
        }

        static string Quote(string value) {
            if (value.Length > 0 && value.IndexOfAny(new [] {' ', '\t', '"'}) < 0) return value;
            var result = new StringBuilder("\"");
            int slashes = 0;
            foreach (char ch in value) {
                if (ch == '\\') { slashes++; continue; }
                if (ch == '"') { result.Append('\\', slashes * 2 + 1); result.Append('"'); slashes = 0; continue; }
                result.Append('\\', slashes); slashes = 0; result.Append(ch);
            }
            result.Append('\\', slashes * 2); result.Append('"');
            return result.ToString();
        }

        static string CommandLine(string file, string[] arguments) {
            var parts = new List<string>();
            parts.Add(Quote(file));
            foreach (string argument in arguments) parts.Add(Quote(argument ?? ""));
            return String.Join(" ", parts.ToArray());
        }

        public static ContainedResult Run(string file, string[] arguments, string workingDirectory, int timeoutMilliseconds) {
            if (timeoutMilliseconds < 1) throw new ArgumentOutOfRangeException("timeoutMilliseconds");
            IntPtr outRead = IntPtr.Zero, outWrite = IntPtr.Zero, errRead = IntPtr.Zero, errWrite = IntPtr.Zero;
            IntPtr job = IntPtr.Zero;
            PROCESS_INFORMATION pi = new PROCESS_INFORMATION();
            FileStream outStream = null, errStream = null;
            bool assigned = false;
            bool completed = false;
            try {
                var sa = new SECURITY_ATTRIBUTES { nLength = Marshal.SizeOf(typeof(SECURITY_ATTRIBUTES)), bInheritHandle = true };
                Check(CreatePipe(out outRead, out outWrite, ref sa, 0), "CreatePipe(stdout)");
                Check(CreatePipe(out errRead, out errWrite, ref sa, 0), "CreatePipe(stderr)");
                Check(SetHandleInformation(outRead, HANDLE_FLAG_INHERIT, 0), "SetHandleInformation(stdout)");
                Check(SetHandleInformation(errRead, HANDLE_FLAG_INHERIT, 0), "SetHandleInformation(stderr)");

                job = CreateJobObjectW(IntPtr.Zero, null);
                if (job == IntPtr.Zero) throw new Win32Exception(Marshal.GetLastWin32Error(), "CreateJobObject");
                var limits = new JOBOBJECT_EXTENDED_LIMIT_INFORMATION();
                limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
                int limitSize = Marshal.SizeOf(typeof(JOBOBJECT_EXTENDED_LIMIT_INFORMATION));
                IntPtr limitPtr = Marshal.AllocHGlobal(limitSize);
                try {
                    Marshal.StructureToPtr(limits, limitPtr, false);
                    Check(SetInformationJobObject(job, JobObjectExtendedLimitInformation, limitPtr, (uint)limitSize),
                        "SetInformationJobObject");
                } finally { Marshal.FreeHGlobal(limitPtr); }

                var startup = new STARTUPINFO();
                startup.cb = Marshal.SizeOf(typeof(STARTUPINFO));
                startup.dwFlags = STARTF_USESTDHANDLES;
                startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
                startup.hStdOutput = outWrite;
                startup.hStdError = errWrite;
                var command = new StringBuilder(CommandLine(file, arguments));
                Check(CreateProcessW(file, command, IntPtr.Zero, IntPtr.Zero, true,
                    CREATE_SUSPENDED | CREATE_NO_WINDOW, IntPtr.Zero, workingDirectory, ref startup, out pi), "CreateProcess");
                CloseHandle(outWrite); outWrite = IntPtr.Zero;
                CloseHandle(errWrite); errWrite = IntPtr.Zero;
                Check(AssignProcessToJobObject(job, pi.hProcess), "AssignProcessToJobObject");
                assigned = true;

                outStream = new FileStream(new SafeFileHandle(outRead, true), FileAccess.Read, 4096, false);
                outRead = IntPtr.Zero;
                errStream = new FileStream(new SafeFileHandle(errRead, true), FileAccess.Read, 4096, false);
                errRead = IntPtr.Zero;
                var outReader = new StreamReader(outStream, new UTF8Encoding(false), true, 4096, false);
                var errReader = new StreamReader(errStream, new UTF8Encoding(false), true, 4096, false);
                Task<string> stdout = outReader.ReadToEndAsync();
                Task<string> stderr = errReader.ReadToEndAsync();
                if (ResumeThread(pi.hThread) == 0xffffffff) throw new Win32Exception(Marshal.GetLastWin32Error(), "ResumeThread");
                CloseHandle(pi.hThread); pi.hThread = IntPtr.Zero;

                uint wait = WaitForSingleObject(pi.hProcess, (uint)timeoutMilliseconds);
                bool timedOut = wait == WAIT_TIMEOUT;
                if (timedOut) {
                    Check(TerminateJobObject(job, 124), "TerminateJobObject");
                    WaitForSingleObject(pi.hProcess, 5000);
                } else if (wait != WAIT_OBJECT_0) {
                    throw new Win32Exception(Marshal.GetLastWin32Error(), "WaitForSingleObject");
                }
                uint exitCode;
                Check(GetExitCodeProcess(pi.hProcess, out exitCode), "GetExitCodeProcess");
                completed = true;

                // Closing a kill-on-close job terminates descendants that outlived the direct child.
                CloseHandle(job); job = IntPtr.Zero;
                if (!Task.WaitAll(new Task[] { stdout, stderr }, 5000))
                    throw new TimeoutException("redirected stream drain timed out");
                return new ContainedResult {
                    ExitCode = timedOut ? 124 : unchecked((int)exitCode),
                    Stdout = stdout.Result,
                    Stderr = stderr.Result,
                    TimedOut = timedOut
                };
            } finally {
                if (!completed && pi.hProcess != IntPtr.Zero) {
                    if (assigned && job != IntPtr.Zero) TerminateJobObject(job, 125);
                    else TerminateProcess(pi.hProcess, 125);
                }
                if (job != IntPtr.Zero) CloseHandle(job);
                if (pi.hThread != IntPtr.Zero) CloseHandle(pi.hThread);
                if (pi.hProcess != IntPtr.Zero) CloseHandle(pi.hProcess);
                if (outWrite != IntPtr.Zero) CloseHandle(outWrite);
                if (errWrite != IntPtr.Zero) CloseHandle(errWrite);
                if (outRead != IntPtr.Zero) CloseHandle(outRead);
                if (errRead != IntPtr.Zero) CloseHandle(errRead);
                if (outStream != null) outStream.Dispose();
                if (errStream != null) errStream.Dispose();
            }
        }
    }
}
'@

function Stop-Gate([string] $Reason) {
    [Console]::Error.WriteLine("OPENBFME_WORK_ITEM FAIL id=$WorkItemId reason=$Reason")
    exit 1
}

function Invoke-Contained(
    [string] $FilePath,
    [string[]] $Arguments,
    [int] $TimeoutMilliseconds,
    [string] $Role,
    [string] $WorkingDirectory = $repoRoot
) {
    if (-not [IO.File]::Exists($FilePath)) { throw "child-executable-missing:$Role" }
    $result = [OpenBFME.JobRunner]::Run(
        [IO.Path]::GetFullPath($FilePath), $Arguments, $WorkingDirectory, $TimeoutMilliseconds
    )
    if ($result.TimedOut) { throw "child-timeout:$Role" }
    return $result
}

function Get-Sha256([string] $Path) {
    if (-not [IO.File]::Exists($Path)) { throw "identity-file-missing:$Path" }
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Get-BytesSha256([byte[]] $Bytes) {
    $hasher = [Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($hasher.ComputeHash($Bytes))).Replace('-', '').ToLowerInvariant() }
    finally { $hasher.Dispose() }
}

function Get-GitBlobSha1([byte[]] $Bytes) {
    $header = [Text.Encoding]::ASCII.GetBytes("blob $($Bytes.Length)" + [char]0)
    $hasher = [Security.Cryptography.SHA1]::Create()
    try {
        [void]$hasher.TransformBlock($header, 0, $header.Length, $null, 0)
        [void]$hasher.TransformFinalBlock($Bytes, 0, $Bytes.Length)
        return ([BitConverter]::ToString($hasher.Hash)).Replace('-', '').ToLowerInvariant()
    }
    finally { $hasher.Dispose() }
}

function Read-Json([string] $Path) {
    try { return Get-Content -LiteralPath $Path -Raw -Encoding UTF8 | ConvertFrom-Json }
    catch { throw "invalid-json:$Path" }
}

function Write-Utf8([string] $Path, [string] $Text) {
    [IO.File]::WriteAllText($Path, $Text, [Text.UTF8Encoding]::new($false))
}

function Assert-PlainChain([string] $Path) {
    $resolved = [IO.Path]::GetFullPath($Path)
    $cursor = [IO.Path]::GetPathRoot($resolved)
    $components = [Collections.Generic.List[string]]::new()
    $components.Add($cursor)
    foreach ($part in @($resolved.Substring($cursor.Length) -split '\\' | Where-Object { $_ -ne '' })) {
        $cursor = Join-Path $cursor $part
        $components.Add($cursor)
    }
    foreach ($component in $components) {
        if (-not (Test-Path -LiteralPath $component)) { throw 'contained-path-missing' }
        $item = Get-Item -LiteralPath $component -Force
        if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw 'reparse-point-refused' }
    }
    return $resolved
}

function Assert-ContainedPath([string] $TrustedRoot, [string] $Path) {
    $root = [IO.Path]::GetFullPath($TrustedRoot).TrimEnd('\')
    $resolved = [IO.Path]::GetFullPath($Path)
    if ($resolved -cne $root -and -not $resolved.StartsWith($root + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw 'path-escapes-trusted-root'
    }
    return Assert-PlainChain $resolved
}

function Resolve-SafeRelativeFile([string] $Root, [string] $Relative) {
    $portable = $Relative.Replace('\', '/')
    if ([string]::IsNullOrWhiteSpace($portable) -or $portable.StartsWith('/') -or
        $portable.StartsWith('~') -or $portable -match ':' -or
        @($portable.Split('/') | Where-Object { $_ -in @('', '.', '..') }).Count -ne 0) {
        throw 'unsafe-relative-path'
    }
    return Assert-ContainedPath $Root (Join-Path $Root ($portable.Replace('/', '\')))
}

function Resolve-GitExecutable {
    $command = @(Get-Command git.exe -CommandType Application -ErrorAction Stop)[0]
    return [IO.Path]::GetFullPath($command.Source)
}

function Invoke-Git([string] $Root, [string[]] $Arguments, [switch] $AllowFailure) {
    $result = Invoke-Contained (Resolve-GitExecutable) (@('-C', $Root) + $Arguments) 60000 'git'
    if (-not $AllowFailure -and $result.ExitCode -ne 0) { throw "git-failed:$($Arguments[0])" }
    return $result
}

function Resolve-CanonicalMain([string] $PorcelainText) {
    $mains = [Collections.Generic.List[string]]::new()
    $current = $null
    foreach ($line in $PorcelainText -split "`n") {
        $line = $line.TrimEnd("`r")
        if ($line.StartsWith('worktree ', [StringComparison]::Ordinal)) { $current = $line.Substring(9) }
        elseif ($line -ceq 'branch refs/heads/main' -and $null -ne $current) { $mains.Add($current) }
    }
    if ($mains.Count -ne 1) { throw 'canonical-main-not-unique' }
    return [IO.Path]::GetFullPath($mains[0].Replace('/', '\'))
}

function Get-CanonicalMain([string] $Root) {
    $porcelain = Invoke-Git $Root @('worktree', 'list', '--porcelain')
    return Assert-PlainChain (Resolve-CanonicalMain $porcelain.Stdout)
}

function Get-PinnedPython([string] $MainRoot) {
    return Assert-ContainedPath $MainRoot (Join-Path $MainRoot 'workspace\retail-work\tools\python-3.12-env\Scripts\python.exe')
}

function Get-CanonicalDigests([string] $Python, [string] $LedgerPath, [string] $Id) {
    $code = @'
import hashlib,json,pathlib,sys
document=json.loads(pathlib.Path(sys.argv[1]).read_text(encoding="utf-8"))
rows=[row for row in document["workItems"] if row.get("id")==sys.argv[2]]
if len(rows)!=1: raise SystemExit("row-cardinality")
digest=lambda value: hashlib.sha256((json.dumps(value,sort_keys=True,separators=(",",":"),ensure_ascii=True)+"\n").encode("utf-8")).hexdigest()
print(digest(rows[0]),digest(rows[0]["verificationCommands"]))
'@
    $result = Invoke-Contained $Python @('-I', '-S', '-B', '-c', $code, $LedgerPath, $Id) 30000 'assignment-digest'
    if ($result.ExitCode -ne 0 -or -not [string]::IsNullOrWhiteSpace($result.Stderr)) {
        throw 'assignment-digest-computation-failed'
    }
    $match = [regex]::Match($result.Stdout, '^([0-9a-f]{64}) ([0-9a-f]{64})\s*$')
    if (-not $match.Success) { throw 'assignment-digest-output-invalid' }
    return [pscustomobject]@{ row = $match.Groups[1].Value; commands = $match.Groups[2].Value }
}

function Find-WorkItem([object] $Ledger, [string] $Id) {
    $rows = @($Ledger.workItems | Where-Object { [string]$_.id -ceq $Id })
    if ($rows.Count -eq 0) { throw 'unknown-work-item' }
    if ($rows.Count -ne 1) { throw 'duplicate-work-item' }
    return $rows[0]
}

function Assert-WorkItemAdmission([object] $Row) {
    if ([string]$Row.status -ceq 'blocked') { throw 'blocked-work-item' }
    $ownedProperty = $Row.ownership.PSObject.Properties['ownedPaths']
    $owned = @($Row.ownership.ownedPaths)
    if ([string]$Row.ownership.state -cne 'assigned' -or
        [string]::IsNullOrWhiteSpace([string]$Row.ownership.assignee) -or
        $null -eq $ownedProperty -or $null -eq $Row.ownership.ownedPaths -or
        $owned.Count -eq 0 -or @($owned | Where-Object {
            $_ -isnot [string] -or [string]::IsNullOrWhiteSpace([string]$_)
        }).Count -ne 0) { throw 'unassigned-work-item' }
    if ([string]$Row.status -cne 'in-progress') { throw "work-item-status-not-runnable:$($Row.status)" }
    if ([string]$Row.allocationClass -cne 'worker-lane') { throw "work-item-class-not-runnable:$($Row.allocationClass)" }
}

function Read-LaneAssignment([string] $LaneRoot) {
    $candidates = @(Get-ChildItem -LiteralPath (Join-Path $LaneRoot 'workspace\logs') -Directory -ErrorAction SilentlyContinue |
        ForEach-Object { Join-Path $_.FullName 'assignment.json' } | Where-Object { [IO.File]::Exists($_) })
    if ($candidates.Count -ne 1) { throw 'assignment-not-unique' }
    $assignment = Read-Json $candidates[0]
    if ([string]$assignment.schema -cne 'openbfme.work-item-assignment' -or
        [int]$assignment.schemaVersion -ne 1) { throw 'assignment-schema-invalid' }
    return [pscustomobject]@{ document = $assignment; bytes = [IO.File]::ReadAllBytes($candidates[0]) }
}

function Assert-AssignmentBinding([string] $LaneRoot, [string] $Id, [string] $Python) {
    $mainRoot = Get-CanonicalMain $LaneRoot
    $lane = Read-LaneAssignment $LaneRoot
    $assignment = $lane.document
    if ([string]$assignment.itemId -cne $Id) { throw 'assignment-item-mismatch' }
    if ([IO.Path]::GetFullPath([string]$assignment.lanePath) -cne [IO.Path]::GetFullPath($LaneRoot)) { throw 'assignment-lane-mismatch' }
    if ([IO.Path]::GetFullPath([string]$assignment.mainPath) -cne $mainRoot) { throw 'assignment-main-not-canonical' }
    $ownerCopy = Join-Path $mainRoot "workspace\logs\$Id\assignment.json"
    if (-not [IO.File]::Exists($ownerCopy) -or
        (Get-BytesSha256 ([IO.File]::ReadAllBytes($ownerCopy))) -cne (Get-BytesSha256 $lane.bytes)) {
        throw 'assignment-owner-copy-mismatch'
    }
    $branch = 'work/' + $Id.ToLowerInvariant()
    if ([string]$assignment.branch -cne $branch -or
        (Invoke-Git $LaneRoot @('branch', '--show-current')).Stdout.Trim() -cne $branch) { throw 'assignment-branch-mismatch' }
    $base = [string]$assignment.assignmentCommit
    if ($base -cnotmatch '^[0-9a-f]{40}$') { throw 'assignment-commit-invalid' }
    $subject = (Invoke-Git $LaneRoot @('log', '-1', '--format=%s', $base)).Stdout.Trim()
    if ($subject -cne "orchestration: assign $Id to $($assignment.assignee)") { throw 'assignment-commit-subject-mismatch' }
    if ((Invoke-Git $LaneRoot @('merge-base', '--is-ancestor', $base, 'HEAD') -AllowFailure).ExitCode -ne 0) {
        throw 'assignment-commit-not-ancestor'
    }
    $count = [int](Invoke-Git $LaneRoot @('rev-list', '--count', "$base..HEAD")).Stdout.Trim()
    $merges = (Invoke-Git $LaneRoot @('rev-list', '--merges', "$base..HEAD")).Stdout.Trim()
    if ($count -gt 1 -or -not [string]::IsNullOrWhiteSpace($merges)) { throw 'assignment-commit-topology-invalid' }
    foreach ($protected in $controlPlanePaths) {
        $dirty = (Invoke-Git $mainRoot @('status', '--porcelain=v1', '--untracked-files=all', '--', $protected)).Stdout
        if (-not [string]::IsNullOrWhiteSpace($dirty)) { throw 'main-control-plane-dirty' }
    }
    if ((Invoke-Git $mainRoot @('merge-base', '--is-ancestor', $base, 'HEAD') -AllowFailure).ExitCode -ne 0) {
        throw 'assignment-commit-not-on-main'
    }
    $scratch = Join-Path $LaneRoot "workspace\logs\$Id\assignment-ledger.json"
    Write-Utf8 $scratch (Invoke-Git $LaneRoot @('show', "${base}:orchestration/work-items.json")).Stdout
    $assigned = Get-CanonicalDigests $Python $scratch $Id
    if ([string]$assigned.row -cne [string]$assignment.workItemSha256) { throw 'assignment-row-digest-mismatch' }
    if ([string]$assigned.commands -cne [string]$assignment.commandsSha256) { throw 'assignment-command-digest-mismatch' }
    $ledgerPath = Join-Path $mainRoot 'orchestration\work-items.json'
    $ledger = Read-Json $ledgerPath
    $row = Find-WorkItem $ledger $Id
    Assert-WorkItemAdmission $row
    $current = Get-CanonicalDigests $Python $ledgerPath $Id
    if ([string]$current.row -cne [string]$assignment.workItemSha256) { throw 'assignment-revoked' }
    if ([string]$row.ownership.assignee -cne [string]$assignment.assignee) { throw 'assignment-assignee-mismatch' }
    if (($row.ownership.ownedPaths | ConvertTo-Json -Compress) -cne
        ($assignment.ownedPaths | ConvertTo-Json -Compress)) { throw 'assignment-owned-paths-mismatch' }
    return [pscustomobject]@{
        assignment = $assignment
        mainRoot = $mainRoot
        laneRoot = [IO.Path]::GetFullPath($LaneRoot)
        python = $Python
        ledger = $ledger
        ledgerPath = $ledgerPath
        assignmentLedgerPath = $scratch
        row = $row
    }
}

function Clear-AmbientGodot {
    foreach ($name in $ambientGodotVariables) { [Environment]::SetEnvironmentVariable($name, $null) }
}

function Resolve-PinnedGodot([string] $ToolRoot) {
    Clear-AmbientGodot
    $console = Assert-ContainedPath $ToolRoot (Join-Path $ToolRoot $pinnedGodot.consoleName)
    $engine = Assert-ContainedPath $ToolRoot (Join-Path $ToolRoot $pinnedGodot.engineName)
    $consoleSha256 = Get-Sha256 $console
    $engineSha256 = Get-Sha256 $engine
    if ($consoleSha256 -cne $pinnedGodot.consoleSha256 -or $engineSha256 -cne $pinnedGodot.engineSha256) {
        throw 'godot-identity-mismatch'
    }
    $version = Invoke-Contained $console @('--version') 60000 'godot-version'
    if ($version.ExitCode -ne 0 -or $version.Stdout.Trim() -cne $pinnedGodot.version) { throw 'godot-version-mismatch' }
    return [ordered]@{ console = $console; engine = $engine; consoleSha256 = $consoleSha256; engineSha256 = $engineSha256; version = $pinnedGodot.version }
}

function Get-RecipeTreeSha256([object] $Recipe, [string] $ProducerRoot) {
    if ($null -eq $Recipe -or $Recipe -isnot [pscustomobject]) { throw 'recipe-not-object' }
    if ([string]$Recipe.tree_sha256 -cnotmatch '^[0-9a-f]{64}$') { throw 'recipe-tree-invalid' }
    $commit = [string]$Recipe.git_commit
    if ($commit -cnotmatch '^[0-9a-f]{40}$' -or
        $Recipe.git_worktree_clean -isnot [bool] -or -not [bool]$Recipe.git_worktree_clean) {
        throw 'recipe-source-identity-invalid'
    }
    if ([string]$Recipe.provenance_source -notin @('git-exact-root', 'release-identity')) {
        throw 'recipe-provenance-source-invalid'
    }
    $kind = Invoke-Git $ProducerRoot @('cat-file', '-t', $commit) -AllowFailure
    if ($kind.ExitCode -ne 0 -or $kind.Stdout.Trim() -cne 'commit') { throw 'recipe-commit-unknown' }
    $committed = @{}
    foreach ($entry in (Invoke-Git $ProducerRoot @('ls-tree', '-r', '-z', $commit)).Stdout.Split([char]0)) {
        $match = [regex]::Match($entry, '^[0-7]+ blob ([0-9a-f]{40})\t(.+)$')
        if ($match.Success) { $committed[$match.Groups[2].Value] = $match.Groups[1].Value }
    }
    $files = @($Recipe.files)
    if ($files.Count -eq 0) { throw 'recipe-inventory-empty' }
    $seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $builder = [Text.StringBuilder]::new()
    $previous = $null
    foreach ($file in $files) {
        $relative = [string]$file.path
        if ($null -ne $previous -and [StringComparer]::Ordinal.Compare($previous, $relative) -ge 0) {
            throw 'recipe-inventory-order-invalid'
        }
        $previous = $relative
        if (-not $seen.Add($relative)) { throw 'recipe-inventory-duplicate' }
        if ($file.size -isnot [int] -and $file.size -isnot [long]) { throw 'recipe-file-size-invalid' }
        if ([long]$file.size -lt 0 -or [string]$file.sha256 -cnotmatch '^[0-9a-f]{64}$') {
            throw 'recipe-file-identity-invalid'
        }
        $actual = Resolve-SafeRelativeFile $ProducerRoot $relative
        $bytes = [IO.File]::ReadAllBytes($actual)
        if ($bytes.Length -ne [long]$file.size) { throw 'recipe-source-size-mismatch' }
        if ((Get-BytesSha256 $bytes) -cne [string]$file.sha256) { throw 'recipe-source-sha256-mismatch' }
        if (-not $committed.ContainsKey($relative) -or $committed[$relative] -cne (Get-GitBlobSha1 $bytes)) {
            throw 'recipe-file-not-at-declared-revision'
        }
        [void]$builder.Append($relative).Append([char]0).Append([string][long]$file.size).Append([char]0).Append([string]$file.sha256).Append("`n")
    }
    $requirements = @($Recipe.requirements_files)
    if ($requirements.Count -eq 0) { throw 'recipe-requirements-empty' }
    foreach ($required in $requirements) {
        if (-not $seen.Contains([string]$required)) { throw 'recipe-requirement-not-in-inventory' }
    }
    $actualTree = Get-BytesSha256 ([Text.Encoding]::UTF8.GetBytes($builder.ToString()))
    if ($actualTree -cne [string]$Recipe.tree_sha256) { throw 'recipe-tree-mismatch' }
    return $actualTree
}

function Assert-PackProvenance([string] $PackRoot, [string] $ProducerRoot, [string] $CatalogSha256) {
    $packPath = Assert-ContainedPath $PackRoot (Join-Path $PackRoot 'pack.json')
    $manifestPath = Assert-ContainedPath $PackRoot (Join-Path $PackRoot 'provenance\manifest.json')
    $pack = Read-Json $packPath
    $manifest = Read-Json $manifestPath
    $baselineProperty = $pack.PSObject.Properties['sourceBaselineId']
    $catalogProperty = $pack.PSObject.Properties['sourceCatalogIdentitySha256']
    $recipeProperty = $pack.PSObject.Properties['sourceRecipeSha256']
    $manifestRecipeProperty = $manifest.PSObject.Properties['importer_recipe']
    if ($null -eq $baselineProperty -or [string]$baselineProperty.Value -cne $targetBaseline) {
        throw 'selection-baseline-mismatch'
    }
    if ($null -eq $catalogProperty -or [string]$catalogProperty.Value -cne $CatalogSha256) {
        throw 'selection-catalog-mismatch'
    }
    if ($null -eq $recipeProperty -or [string]$recipeProperty.Value -cnotmatch '^[0-9a-f]{64}$') {
        throw 'selection-recipe-marker-invalid'
    }
    if ($null -eq $manifestRecipeProperty) { throw 'selection-recipe-missing' }
    $tree = Get-RecipeTreeSha256 $manifestRecipeProperty.Value $ProducerRoot
    if ([string]$recipeProperty.Value -cne $tree) { throw 'selection-recipe-content-mismatch' }
    return [ordered]@{
        packSha256 = Get-Sha256 $packPath
        provenanceSha256 = Get-Sha256 $manifestPath
        recipeTreeSha256 = $tree
    }
}

function Get-SelectionEntries([object] $Selection) {
    if ([string]$Selection.schema -cne 'openbfme.pack-selection' -or
        $null -eq $Selection.PSObject.Properties['schemaVersion'] -or
        $Selection.schemaVersion -isnot [int] -or [int]$Selection.schemaVersion -ne 0) {
        throw 'selection-schema-invalid'
    }
    $entries = @([string]$Selection.activePack) + @($Selection.supplementalPacks | ForEach-Object { [string]$_ })
    $seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($entry in $entries) {
        if ($entry -cnotmatch '^([a-z0-9]+(?:-[a-z0-9]+)*)/([0-9a-f]{64})$') { throw 'selection-entry-invalid' }
        if (-not $seen.Add($entry)) { throw 'selection-entry-duplicate' }
    }
    return $entries
}

function Invoke-SelectionAdmission(
    [string] $SelectionRoot,
    [string] $ProducerRoot,
    [string] $CatalogSha256,
    [scriptblock] $PackChecker
) {
    # Every component from the drive root through selection.json is verified
    # reparse-free before one selection byte is read, and every pack component
    # before the checker child starts.
    $selectionPath = Assert-ContainedPath $SelectionRoot (Join-Path $SelectionRoot 'selection.json')
    $bytes = [IO.File]::ReadAllBytes($selectionPath)
    try { $selection = [Text.UTF8Encoding]::new($false, $true).GetString($bytes) | ConvertFrom-Json }
    catch { throw 'selection-document-invalid' }
    $entries = @(Get-SelectionEntries $selection)
    $packs = [ordered]@{}
    foreach ($entry in $entries) {
        $packRoot = Assert-ContainedPath $SelectionRoot (Join-Path $SelectionRoot ($entry.Replace('/', '\')))
        $packs[$entry] = Assert-PackProvenance $packRoot $ProducerRoot $CatalogSha256
    }
    & $PackChecker $SelectionRoot $entries.Count
    return [ordered]@{
        selectionRoot = [IO.Path]::GetFullPath($SelectionRoot)
        selectionSha256 = Get-BytesSha256 $bytes
        entries = $entries
        packs = $packs
    }
}

function Assert-NoForbiddenDiagnostics([string] $Stream, [string] $Text, [string[]] $Forbidden) {
    foreach ($diagnostic in $Forbidden) {
        if ($Text.IndexOf($diagnostic, [StringComparison]::OrdinalIgnoreCase) -ge 0) {
            throw "forbidden-$Stream-diagnostic:$diagnostic"
        }
    }
}

function Assert-RuntimeResult([int] $ExitCode, [string] $Stdout, [string] $Stderr, [string[]] $Forbidden) {
    if ($ExitCode -ne 0) { throw "runtime-exit-code:$ExitCode" }
    $combined = $Stdout + "`n" + $Stderr
    if ($combined -match '(?im)\bSKIP(?:PED)?\b') { throw 'runtime-skip' }
    if ($combined -match '(?im)^(?:RETAIL_SLICE(?:_ACCEPTANCE)?|OPENBFME_WORK_ITEM)\s+(?:FAIL|SKIP)\b') {
        throw 'contradictory-terminal-marker'
    }
    Assert-NoForbiddenDiagnostics 'stdout' $Stdout $Forbidden
    Assert-NoForbiddenDiagnostics 'stderr' $Stderr $Forbidden
    $results = [regex]::Matches($Stdout, '(?m)^RETAIL_SLICE_RESULT passed=([0-9]+) failed=([0-9]+)\s*$')
    if ($results.Count -ne 1) { throw 'runtime-result-marker-count' }
    if ([int]$results[0].Groups[1].Value -le 0) { throw 'runtime-result-empty' }
    if ([int]$results[0].Groups[2].Value -ne 0) { throw 'runtime-result-failed-nonzero' }
    $acceptance = [regex]::Matches(
        $Stdout, '(?m)^RETAIL_SLICE_ACCEPTANCE PASS min_passed=([0-9]+) pinned_known_failures=([0-9]+)\s*$'
    )
    if ($acceptance.Count -ne 1) { throw 'runtime-acceptance-marker-count' }
    if ([int]$acceptance[0].Groups[2].Value -ne 0) { throw 'runtime-known-failures-nonzero' }
    if ([int]$results[0].Groups[1].Value -lt [int]$acceptance[0].Groups[1].Value) {
        throw 'runtime-result-below-ratchet'
    }
}

function Get-ExecutedIdentity([object] $Binding, [object] $Godot, [object] $Selection) {
    $files = [ordered]@{
        gate = Get-Sha256 (Join-Path $Binding.laneRoot 'tools\gate-rotwk-202.ps1')
        gateTests = Get-Sha256 (Join-Path $Binding.laneRoot 'importer\tests\test_retail_gate_script.py')
        runner = Get-Sha256 (Join-Path $Binding.laneRoot 'game\tests\retail_slice_runner.gd')
        packChecker = Get-Sha256 (Join-Path $Binding.laneRoot 'tools\check_pack_addresses.py')
        retailPython = Get-Sha256 $Binding.python
        powershell = Get-Sha256 (Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe')
        git = Get-Sha256 (Resolve-GitExecutable)
        godotConsole = $Godot.consoleSha256
        godotEngine = $Godot.engineSha256
    }
    $dirty = (Invoke-Git $Binding.laneRoot @('status', '--porcelain=v1', '--untracked-files=all')).Stdout
    return [ordered]@{
        head = (Invoke-Git $Binding.laneRoot @('rev-parse', 'HEAD')).Stdout.Trim()
        branch = (Invoke-Git $Binding.laneRoot @('branch', '--show-current')).Stdout.Trim()
        assignmentCommit = [string]$Binding.assignment.assignmentCommit
        ledgerSha256 = Get-Sha256 $Binding.ledgerPath
        assignmentLedgerSha256 = Get-Sha256 $Binding.assignmentLedgerPath
        baselineSha256 = Get-Sha256 (Join-Path $Binding.mainRoot 'contracts\rotwk-202-v9.7.7-baseline.json')
        dirtyStateSha256 = Get-BytesSha256 ([Text.Encoding]::UTF8.GetBytes($dirty))
        selectionSha256 = $Selection.selectionSha256
        packs = $Selection.packs
        files = $files
    }
}

function Assert-IdentityFixed([object] $Before, [object] $After) {
    if (($Before | ConvertTo-Json -Compress -Depth 8) -cne ($After | ConvertTo-Json -Compress -Depth 8)) {
        throw 'executed-identity-drift'
    }
}

function Assert-ThrowsExact([scriptblock] $Action, [string] $Expected) {
    try { & $Action }
    catch {
        if ($_.Exception.Message -ceq $Expected) { return }
        throw "self-test-wrong-diagnostic:${Expected}:$($_.Exception.Message)"
    }
    throw "self-test-did-not-reject:$Expected"
}

function Remove-Tree([string] $Root) {
    # Junctions are unlinked, never descended, so fixture cleanup cannot reach a target.
    $resolved = [IO.Path]::GetFullPath($Root)
    if (-not [IO.Directory]::Exists($resolved)) { return }
    foreach ($directory in [IO.Directory]::GetDirectories($resolved)) {
        $item = Get-Item -LiteralPath $directory -Force
        if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { [IO.Directory]::Delete($directory) }
        else { Remove-Tree $directory }
    }
    foreach ($file in [IO.Directory]::GetFiles($resolved)) {
        [IO.File]::SetAttributes($file, [IO.FileAttributes]::Normal)
        [IO.File]::Delete($file)
    }
    [IO.Directory]::Delete($resolved)
}

function New-SyntheticRepository([string] $Root, [string] $Subject, [hashtable] $Files) {
    [IO.Directory]::CreateDirectory($Root) | Out-Null
    Invoke-Git $Root @('init', '-q', '-b', 'main') | Out-Null
    Invoke-Git $Root @('config', 'user.email', 'self-test@openbfme.invalid') | Out-Null
    Invoke-Git $Root @('config', 'user.name', 'self-test') | Out-Null
    Invoke-Git $Root @('config', 'core.hooksPath', 'NUL') | Out-Null
    Write-Utf8 (Join-Path $Root '.gitignore') "workspace/`n"
    foreach ($relative in $Files.Keys) {
        $path = Join-Path $Root ($relative.Replace('/', '\'))
        [IO.Directory]::CreateDirectory((Split-Path -Parent $path)) | Out-Null
        Write-Utf8 $path $Files[$relative]
    }
    Invoke-Git $Root @('add', '-A') | Out-Null
    Invoke-Git $Root @('commit', '-q', '-m', $Subject) | Out-Null
    return (Invoke-Git $Root @('rev-parse', 'HEAD')).Stdout.Trim()
}

function New-SyntheticPack([string] $PackRoot, [string] $ProducerRoot, [string] $Commit, [string] $Catalog) {
    $sourceFile = Join-Path $ProducerRoot 'importer\fixture.py'
    $bytes = [IO.File]::ReadAllBytes($sourceFile)
    $sha = Get-BytesSha256 $bytes
    $tree = Get-BytesSha256 ([Text.Encoding]::UTF8.GetBytes("importer/fixture.py$([char]0)$($bytes.Length)$([char]0)$sha`n"))
    $recipe = [ordered]@{
        tree_sha256 = $tree
        files = @([ordered]@{ path = 'importer/fixture.py'; size = [long]$bytes.Length; sha256 = $sha })
        git_commit = $Commit
        git_worktree_clean = $true
        provenance_source = 'git-exact-root'
        requirements_files = @('importer/fixture.py')
    }
    [IO.Directory]::CreateDirectory((Join-Path $PackRoot 'provenance')) | Out-Null
    $pack = [ordered]@{ sourceBaselineId = $targetBaseline; sourceCatalogIdentitySha256 = $Catalog; sourceRecipeSha256 = $tree }
    Write-Utf8 (Join-Path $PackRoot 'pack.json') (($pack | ConvertTo-Json -Depth 8) + "`n")
    Write-Utf8 (Join-Path $PackRoot 'provenance\manifest.json') (([ordered]@{ importer_recipe = $recipe } | ConvertTo-Json -Depth 8) + "`n")
    return $recipe
}

function Write-SyntheticRecipe([string] $PackRoot, [object] $Recipe) {
    Write-Utf8 (Join-Path $PackRoot 'provenance\manifest.json') (([ordered]@{ importer_recipe = $Recipe } | ConvertTo-Json -Depth 8) + "`n")
}

function New-Junction([string] $Link, [string] $Target) {
    $cmd = Join-Path $env:SystemRoot 'System32\cmd.exe'
    $result = Invoke-Contained $cmd @('/d', '/c', 'mklink', '/J', $Link, $Target) 10000 'junction-fixture'
    if ($result.ExitCode -ne 0) { throw 'self-test-junction-create-failed' }
}

function Invoke-SelfTest([string] $MainRoot, [string[]] $Forbidden) {
    $cases = [Collections.Generic.List[string]]::new()
    $passOut = "RETAIL_SLICE_RESULT passed=400 failed=0`n" +
        "RETAIL_SLICE_ACCEPTANCE PASS min_passed=374 pinned_known_failures=0`n"
    Assert-RuntimeResult 0 $passOut '' $Forbidden; $cases.Add('runtime-pass')
    Assert-ThrowsExact { Assert-RuntimeResult 0 ($passOut -replace 'failed=0', 'failed=2') '' $Forbidden } 'runtime-result-failed-nonzero'; $cases.Add('failed-nonzero')
    Assert-ThrowsExact { Assert-RuntimeResult 1 $passOut '' $Forbidden } 'runtime-exit-code:1'; $cases.Add('exit-code-nonzero')
    Assert-ThrowsExact { Assert-RuntimeResult 0 'RETAIL_SLICE_ACCEPTANCE PASS min_passed=1 pinned_known_failures=0' '' $Forbidden } 'runtime-result-marker-count'; $cases.Add('missing-result')
    Assert-ThrowsExact { Assert-RuntimeResult 0 ($passOut + $passOut) '' $Forbidden } 'runtime-result-marker-count'; $cases.Add('duplicate-result')
    Assert-ThrowsExact { Assert-RuntimeResult 0 ($passOut + 'RETAIL_SLICE_ACCEPTANCE FAIL min_passed=374 pinned_known_failures=0') '' $Forbidden } 'contradictory-terminal-marker'; $cases.Add('contradictory-marker')
    Assert-ThrowsExact { Assert-RuntimeResult 0 ($passOut + 'SKIP') '' $Forbidden } 'runtime-skip'; $cases.Add('skip')
    Assert-ThrowsExact { Assert-RuntimeResult 0 ($passOut + 'fallback') '' $Forbidden } 'forbidden-stdout-diagnostic:fallback'; $cases.Add('forbidden-stdout')
    Assert-ThrowsExact { Assert-RuntimeResult 0 $passOut 'missing resource' $Forbidden } 'forbidden-stderr-diagnostic:missing resource'; $cases.Add('forbidden-stderr')
    Assert-ThrowsExact { Assert-RuntimeResult 0 ($passOut -replace 'pinned_known_failures=0', 'pinned_known_failures=1') '' $Forbidden } 'runtime-known-failures-nonzero'; $cases.Add('known-failures-nonzero')

    $syntheticLedger = [pscustomobject]@{ workItems = @() }
    Assert-ThrowsExact { Find-WorkItem $syntheticLedger 'P0-SYNTHETIC-001' | Out-Null } 'unknown-work-item'; $cases.Add('unknown-synthetic-row')
    $blocked = [pscustomobject]@{ status = 'blocked'; allocationClass = 'worker-lane'; ownership = [pscustomobject]@{ state = 'assigned'; assignee = 'agent'; ownedPaths = @('x') } }
    Assert-ThrowsExact { Assert-WorkItemAdmission $blocked } 'blocked-work-item'; $cases.Add('blocked-synthetic-row')
    $unassigned = [pscustomobject]@{ status = 'pending'; allocationClass = 'worker-lane'; ownership = [pscustomobject]@{ state = 'unassigned'; assignee = $null; ownedPaths = @() } }
    Assert-ThrowsExact { Assert-WorkItemAdmission $unassigned } 'unassigned-work-item'; $cases.Add('unassigned-synthetic-row')
    $runnable = [pscustomobject]@{ id = 'P0-SYNTHETIC-001'; status = 'in-progress'; allocationClass = 'worker-lane'; ownership = [pscustomobject]@{ state = 'assigned'; assignee = 'agent'; ownedPaths = @('x') }; verificationCommands = @() }
    $unrelated = [pscustomobject]@{ id = 'P9-UNRELATED-001'; status = 'blocked'; allocationClass = 'worker-lane'; ownership = [pscustomobject]@{ state = 'unassigned'; assignee = $null; ownedPaths = @() }; verificationCommands = @() }
    $syntheticLedger.workItems = @($runnable, $unrelated)
    Assert-WorkItemAdmission (Find-WorkItem $syntheticLedger 'P0-SYNTHETIC-001')
    $unrelated.status = 'pending'
    Assert-WorkItemAdmission (Find-WorkItem $syntheticLedger 'P0-SYNTHETIC-001')
    $cases.Add('unrelated-synthetic-row-ignored')

    $oneMain = "worktree C:/repo`nHEAD 0000000000000000000000000000000000000000`nbranch refs/heads/main`n`nworktree C:/lane`nHEAD 0000000000000000000000000000000000000000`nbranch refs/heads/work/x`n"
    if ((Resolve-CanonicalMain $oneMain) -cne 'C:\repo') { throw 'self-test-canonical-main-parse' }
    Assert-ThrowsExact { Resolve-CanonicalMain ($oneMain -replace 'refs/heads/main', 'refs/heads/other') | Out-Null } 'canonical-main-not-unique'
    Assert-ThrowsExact { Resolve-CanonicalMain ($oneMain -replace 'refs/heads/work/x', 'refs/heads/main') | Out-Null } 'canonical-main-not-unique'
    $cases.Add('canonical-main-unique-parse')

    $selfRoot = Join-Path $repoRoot ("workspace\logs\$WorkItemId\self-test-" + [Guid]::NewGuid().ToString('N'))
    [IO.Directory]::CreateDirectory($selfRoot) | Out-Null
    try {
        $python = Get-PinnedPython $MainRoot
        $syntheticId = 'P0-SYNTHETIC-001'
        $ledgerText = ($syntheticLedger | ConvertTo-Json -Depth 8) + "`n"
        $mainFixture = Join-Path $selfRoot 'main'
        $commit = New-SyntheticRepository $mainFixture "orchestration: assign $syntheticId to agent" @{
            'orchestration/work-items.json' = $ledgerText
            'importer/fixture.py' = "print('fixture')`n"
        }
        $laneFixture = Join-Path $selfRoot 'lane'
        Invoke-Git $mainFixture @('worktree', 'add', '-q', '-b', "work/$($syntheticId.ToLowerInvariant())", $laneFixture, $commit) | Out-Null
        $digests = Get-CanonicalDigests $python (Join-Path $mainFixture 'orchestration\work-items.json') $syntheticId
        $decoy = Join-Path $selfRoot 'decoy'
        [IO.Directory]::CreateDirectory($decoy) | Out-Null
        $assignment = [ordered]@{
            schema = 'openbfme.work-item-assignment'; schemaVersion = 1; itemId = $syntheticId; assignee = 'agent'
            assignmentCommit = $commit; branch = "work/$($syntheticId.ToLowerInvariant())"
            mainPath = $mainFixture; lanePath = $laneFixture; ownedPaths = @('x')
            workItemSha256 = $digests.row; commandsSha256 = $digests.commands
        }
        $ownerCopy = Join-Path $mainFixture "workspace\logs\$syntheticId\assignment.json"
        $laneCopy = Join-Path $laneFixture "workspace\logs\$syntheticId\assignment.json"
        [IO.Directory]::CreateDirectory((Split-Path -Parent $ownerCopy)) | Out-Null
        [IO.Directory]::CreateDirectory((Split-Path -Parent $laneCopy)) | Out-Null
        $assignmentText = ($assignment | ConvertTo-Json -Depth 8) + "`n"
        Write-Utf8 $ownerCopy $assignmentText
        Write-Utf8 $laneCopy $assignmentText
        $binding = Assert-AssignmentBinding $laneFixture $syntheticId $python
        if ($binding.mainRoot -cne [IO.Path]::GetFullPath($mainFixture)) { throw 'self-test-binding-main' }
        $cases.Add('synthetic-lane-binding')
        $redirected = [ordered]@{} + $assignment
        $redirected.mainPath = $decoy
        $redirectedText = ($redirected | ConvertTo-Json -Depth 8) + "`n"
        Write-Utf8 $ownerCopy $redirectedText
        Write-Utf8 $laneCopy $redirectedText
        Assert-ThrowsExact { Assert-AssignmentBinding $laneFixture $syntheticId $python | Out-Null } 'assignment-main-not-canonical'
        $cases.Add('ignored-metadata-cannot-redirect-main')
        Write-Utf8 $ownerCopy $assignmentText
        Write-Utf8 $laneCopy ($assignmentText -replace '"assignee":\s*"agent"', '"assignee": "other"')
        Assert-ThrowsExact { Assert-AssignmentBinding $laneFixture $syntheticId $python | Out-Null } 'assignment-owner-copy-mismatch'
        $cases.Add('lane-assignment-must-match-owner-copy')
        Write-Utf8 $laneCopy $assignmentText
        $tamperedLedger = $ledgerText -replace '"assignee":\s*"agent"', '"assignee": "other"'
        Write-Utf8 (Join-Path $mainFixture 'orchestration\work-items.json') $tamperedLedger
        Assert-ThrowsExact { Assert-AssignmentBinding $laneFixture $syntheticId $python | Out-Null } 'main-control-plane-dirty'
        $cases.Add('dirty-main-ledger-refused')
        Write-Utf8 (Join-Path $mainFixture 'orchestration\work-items.json') $ledgerText

        $catalog = 'b' * 64
        $packRoot = Join-Path $selfRoot ('content-packs\synthetic-pack\' + ('c' * 64))
        $recipe = New-SyntheticPack $packRoot $mainFixture $commit $catalog
        Assert-PackProvenance $packRoot $mainFixture $catalog | Out-Null; $cases.Add('content-bound-recipe')
        $sourceFile = Join-Path $mainFixture 'importer\fixture.py'
        [IO.File]::AppendAllText($sourceFile, '# tamper')
        Assert-ThrowsExact { Assert-PackProvenance $packRoot $mainFixture $catalog | Out-Null } 'recipe-source-size-mismatch'; $cases.Add('recipe-source-tamper')
        $tamperedBytes = [IO.File]::ReadAllBytes($sourceFile)
        $rewritten = [ordered]@{} + $recipe
        $rewritten.files = @([ordered]@{ path = 'importer/fixture.py'; size = [long]$tamperedBytes.Length; sha256 = Get-BytesSha256 $tamperedBytes })
        $rewritten.tree_sha256 = Get-BytesSha256 ([Text.Encoding]::UTF8.GetBytes("importer/fixture.py$([char]0)$($tamperedBytes.Length)$([char]0)$($rewritten.files[0].sha256)`n"))
        Write-SyntheticRecipe $packRoot $rewritten
        Assert-ThrowsExact { Assert-PackProvenance $packRoot $mainFixture $catalog | Out-Null } 'recipe-file-not-at-declared-revision'; $cases.Add('recipe-not-at-declared-revision')
        Write-Utf8 $sourceFile "print('fixture')`n"
        $unknownCommit = [ordered]@{} + $recipe
        $unknownCommit.git_commit = 'a' * 40
        Write-SyntheticRecipe $packRoot $unknownCommit
        Assert-ThrowsExact { Assert-PackProvenance $packRoot $mainFixture $catalog | Out-Null } 'recipe-commit-unknown'; $cases.Add('recipe-commit-unknown')
        $dirtyRecipe = [ordered]@{} + $recipe
        $dirtyRecipe.git_worktree_clean = $false
        Write-SyntheticRecipe $packRoot $dirtyRecipe
        Assert-ThrowsExact { Assert-PackProvenance $packRoot $mainFixture $catalog | Out-Null } 'recipe-source-identity-invalid'; $cases.Add('recipe-dirty-producer-refused')
        Write-SyntheticRecipe $packRoot $recipe
        $packDocument = Join-Path $packRoot 'pack.json'
        $packText = [IO.File]::ReadAllText($packDocument)
        Write-Utf8 $packDocument ($packText -replace [regex]::Escape($recipe.tree_sha256), ('d' * 64))
        Assert-ThrowsExact { Assert-PackProvenance $packRoot $mainFixture $catalog | Out-Null } 'selection-recipe-content-mismatch'; $cases.Add('recipe-marker-only-refused')
        Write-Utf8 $packDocument $packText

        $selectionRoot = Join-Path $selfRoot 'content-packs'
        $selectionText = ([ordered]@{ schema = 'openbfme.pack-selection'; schemaVersion = 0; activePack = 'synthetic-pack/' + ('c' * 64); supplementalPacks = @() } | ConvertTo-Json -Depth 8) + "`n"
        Write-Utf8 (Join-Path $selectionRoot 'selection.json') $selectionText
        $checkerCalls = [Collections.Generic.List[string]]::new()
        $checker = { param($Root, $Count) $checkerCalls.Add("$Root|$Count") }.GetNewClosure()
        $admitted = Invoke-SelectionAdmission $selectionRoot $mainFixture $catalog $checker
        if ($checkerCalls.Count -ne 1 -or $checkerCalls[0] -cne "$selectionRoot|1" -or @($admitted.entries).Count -ne 1) { throw 'self-test-selection-admission' }
        $cases.Add('contained-selection-admitted-then-checked')
        $junctionRoot = Join-Path $selfRoot 'junction-packs'
        New-Junction $junctionRoot $selectionRoot
        Assert-ThrowsExact { Invoke-SelectionAdmission $junctionRoot $mainFixture $catalog $checker | Out-Null } 'reparse-point-refused'
        if ($checkerCalls.Count -ne 1) { throw 'self-test-checker-ran-after-refusal' }
        $cases.Add('junctioned-selection-refused-before-checker')
        Write-Utf8 $packDocument (([ordered]@{ id = 'legacy-pack' } | ConvertTo-Json) + "`n")
        Assert-ThrowsExact { Invoke-SelectionAdmission $selectionRoot $mainFixture $catalog $checker | Out-Null } 'selection-baseline-mismatch'
        if ($checkerCalls.Count -ne 1) { throw 'self-test-checker-ran-after-legacy' }
        $cases.Add('legacy-selection-rejected-before-runtime')

        $decoyGodot = Join-Path $selfRoot 'decoy-godot.py'
        Write-Utf8 $decoyGodot "print('4.7.stable.official.5b4e0cb0f')`n"
        foreach ($name in $ambientGodotVariables) { [Environment]::SetEnvironmentVariable($name, $decoyGodot) }
        $godot = Resolve-PinnedGodot (Join-Path $MainRoot '.tools\godot')
        if ($godot.consoleSha256 -cne $pinnedGodot.consoleSha256 -or
            -not [string]::IsNullOrEmpty([Environment]::GetEnvironmentVariable('OPENBFME_GODOT'))) {
            throw 'self-test-godot-binding'
        }
        $cases.Add('ambient-godot-ignored')
        $fakeToolRoot = Join-Path $selfRoot 'fake-godot'
        [IO.Directory]::CreateDirectory($fakeToolRoot) | Out-Null
        [IO.File]::Copy((Join-Path $env:SystemRoot 'System32\cmd.exe'), (Join-Path $fakeToolRoot $pinnedGodot.engineName))
        [IO.File]::Copy((Join-Path $env:SystemRoot 'System32\cmd.exe'), (Join-Path $fakeToolRoot $pinnedGodot.consoleName))
        Assert-ThrowsExact { Resolve-PinnedGodot $fakeToolRoot | Out-Null } 'godot-identity-mismatch'
        $cases.Add('substituted-godot-refused')

        $sentinel = Join-Path $selfRoot 'escaped.txt'
        $grandchild = Join-Path $selfRoot 'grandchild.py'
        $parent = Join-Path $selfRoot 'parent.py'
        $flood = Join-Path $selfRoot 'flood.py'
        Write-Utf8 $grandchild "import pathlib,time`ntime.sleep(2)`npathlib.Path(r'$sentinel').write_text('escaped')`ntime.sleep(20)`n"
        Write-Utf8 $parent "import subprocess,sys,time`nsubprocess.Popen([sys.executable,r'$grandchild'])`ntime.sleep(30)`n"
        Write-Utf8 $flood "import sys`nfor i in range(2000):`n print('o'*80)`n print('e'*80,file=sys.stderr)`n"
        $timed = [OpenBFME.JobRunner]::Run($python, @('-I', '-S', '-B', $parent), $repoRoot, 500)
        if (-not $timed.TimedOut -or $timed.ExitCode -ne 124) { throw 'self-test-timeout-not-observed' }
        Start-Sleep -Seconds 3
        if ([IO.File]::Exists($sentinel)) { throw 'self-test-grandchild-escaped-job' }
        $cases.Add('job-timeout-descendant-cleanup')
        $flooded = Invoke-Contained $python @('-I', '-S', '-B', $flood) 10000 'stream-flood'
        if ($flooded.ExitCode -ne 0 -or $flooded.Stdout.Length -lt 100000 -or $flooded.Stderr.Length -lt 100000) {
            throw 'self-test-concurrent-drain-failed'
        }
        $cases.Add('concurrent-stream-drain')
    }
    finally {
        Remove-Tree $selfRoot
    }

    $artifact = Join-Path $repoRoot "workspace\logs\$WorkItemId\runtime-gate-negative-tests.json"
    $document = [ordered]@{
        schema = 'openbfme.runtime-gate-negative-tests'
        schemaVersion = 3
        workItemId = $WorkItemId
        claimBoundary = [ordered]@{ level = 'L0'; source = 'NOT_REQUIRED'; load = 'NOT_REQUIRED'; legacyRejectionIsEvidence = $false }
        cases = @($cases)
        caseCount = $cases.Count
        pinnedGodot = $pinnedGodot
        executedFiles = [ordered]@{
            gate = Get-Sha256 (Join-Path $repoRoot 'tools\gate-rotwk-202.ps1')
            gateTests = Get-Sha256 (Join-Path $repoRoot 'importer\tests\test_retail_gate_script.py')
            retailPython = Get-Sha256 (Get-PinnedPython $MainRoot)
            powershell = Get-Sha256 (Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe')
            git = Get-Sha256 (Resolve-GitExecutable)
        }
        result = 'PASS'
    }
    Write-Utf8 $artifact (($document | ConvertTo-Json -Depth 8) + "`n")
    [Console]::Out.WriteLine("ROTWK_202_GATE_SELF_TEST PASS cases=$($cases.Count)")
}

function Invoke-LiveGate([string] $LaneRoot, [string] $Id) {
    $binding = Assert-AssignmentBinding $LaneRoot $Id (Get-PinnedPython (Get-CanonicalMain $repoRoot))
    $policy = $binding.ledger.verificationPolicies.'strict-default'
    if ($null -eq $policy -or [bool]$policy.skipIsSuccess) { throw 'strict-policy-invalid' }
    $forbidden = @($policy.forbiddenDiagnostics | ForEach-Object { [string]$_ })
    if ($forbidden.Count -eq 0) { throw 'strict-policy-has-no-forbidden-diagnostics' }
    $dirty = (Invoke-Git $binding.laneRoot @('status', '--porcelain=v1', '--untracked-files=all')).Stdout
    if (-not [string]::IsNullOrWhiteSpace($dirty)) { throw 'live-lane-not-clean' }
    $godot = Resolve-PinnedGodot (Join-Path $binding.mainRoot '.tools\godot')
    $baseline = Read-Json (Assert-ContainedPath $binding.mainRoot (Join-Path $binding.mainRoot 'contracts\rotwk-202-v9.7.7-baseline.json'))
    if ([string]$baseline.baselineId -cne $targetBaseline) { throw 'baseline-id-drift' }
    $checkerScript = Join-Path $binding.laneRoot 'tools\check_pack_addresses.py'
    $disabledDurable = Join-Path $binding.laneRoot "workspace\logs\$Id\disabled-durable-root"
    [IO.Directory]::CreateDirectory($disabledDurable) | Out-Null
    $checker = {
        param($Root, $Count)
        $address = Invoke-Contained $binding.python @('-I', '-S', '-B', $checkerScript, '--packs-root', $Root, '--durable-root', $disabledDurable) 300000 'pack-address' $binding.laneRoot
        if ($address.ExitCode -ne 0 -or $address.Stdout -cnotmatch "(?m)^PACK_ADDRESS_CHECK PASS packs=$Count roots=1\s*$") {
            throw 'pack-address-check-failed'
        }
        Assert-NoForbiddenDiagnostics 'pack-address-stdout' $address.Stdout $forbidden
        Assert-NoForbiddenDiagnostics 'pack-address-stderr' $address.Stderr $forbidden
    }.GetNewClosure()
    $selection = Invoke-SelectionAdmission (Join-Path $binding.mainRoot 'workspace\content-packs') $binding.mainRoot ([string]$baseline.authority.catalogSha256) $checker
    $before = Get-ExecutedIdentity $binding $godot $selection
    $env:OPENBFME_CONTENT = $selection.selectionRoot
    $env:OPENBFME_STRICT_PARITY_PROFILE = '1'
    $runtime = Invoke-Contained $godot.console @('--headless', '--path', (Join-Path $binding.laneRoot 'game'), '--script', 'res://tests/retail_slice_runner.gd') $liveTimeoutMilliseconds 'retail-runtime' $binding.laneRoot
    Assert-RuntimeResult $runtime.ExitCode $runtime.Stdout $runtime.Stderr $forbidden
    $selectionAfter = Invoke-SelectionAdmission $selection.selectionRoot $binding.mainRoot ([string]$baseline.authority.catalogSha256) $checker
    Assert-IdentityFixed $before (Get-ExecutedIdentity $binding $godot $selectionAfter)
    [Console]::Out.WriteLine("OPENBFME_WORK_ITEM PASS id=$Id level=L0")
}

try {
    if ($SelfTest) {
        $mainRoot = Get-CanonicalMain $repoRoot
        $policy = (Read-Json (Join-Path $mainRoot 'orchestration\work-items.json')).verificationPolicies.'strict-default'
        Invoke-SelfTest $mainRoot @($policy.forbiddenDiagnostics | ForEach-Object { [string]$_ })
        exit 0
    }
    Invoke-LiveGate $repoRoot $WorkItemId
    exit 0
}
catch { Stop-Gate $_.Exception.Message }
