[CmdletBinding()]
param(
    [Parameter(Position = 0, Mandatory = $true)]
    [ValidateSet('ready', 'create', 'check', 'handoff', 'review')]
    [string] $Command,
    [string] $Id,
    [string] $Assignee,
    [string] $LanePath,
    [string] $Reviewer,
    [switch] $Json
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Stop-Workflow([string] $Message) {
    [Console]::Error.WriteLine("WORK_ITEM FAIL $Message")
    exit 1
}

function Resolve-LiteralDirectory([string] $Path, [string] $Label) {
    if ([string]::IsNullOrWhiteSpace($Path)) {
        Stop-Workflow "$Label is missing"
    }
    $resolved = [IO.Path]::GetFullPath($Path)
    $item = Get-Item -LiteralPath $resolved -Force -ErrorAction Stop
    if ($item -isnot [IO.DirectoryInfo] -or
        ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        Stop-Workflow "$Label must be one plain directory"
    }
    return $item.FullName
}

try {
    if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) {
        Stop-Workflow 'Windows is required'
    }
    $Command = $Command.ToLowerInvariant()
    $gitCommand = Get-Command git.exe -CommandType Application -ErrorAction Stop
    $callerRoot = (& $gitCommand.Source -C (Join-Path $PSScriptRoot '..') `
        rev-parse --show-toplevel).Trim()
    if ($LASTEXITCODE -ne 0) {
        Stop-Workflow 'launcher is not inside a Git worktree'
    }
    $callerRoot = Resolve-LiteralDirectory $callerRoot 'caller worktree'

    $mainRows = [Collections.Generic.List[string]]::new()
    $currentPath = $null
    foreach ($line in @(& $gitCommand.Source -C $callerRoot worktree list --porcelain)) {
        if ($line.StartsWith('worktree ', [StringComparison]::Ordinal)) {
            $currentPath = $line.Substring(9)
        }
        elseif ($line -ceq 'branch refs/heads/main' -and $null -ne $currentPath) {
            $mainRows.Add($currentPath)
        }
    }
    if ($LASTEXITCODE -ne 0 -or $mainRows.Count -ne 1) {
        Stop-Workflow 'repository must have exactly one main worktree'
    }
    $mainRoot = Resolve-LiteralDirectory $mainRows[0] 'main worktree'
    $mainTool = Join-Path $mainRoot 'tools\work-item.py'
    if (-not [IO.File]::Exists($mainTool)) {
        Stop-Workflow 'main work-item tool is missing'
    }
    $pythonCandidates = @(
        (Join-Path $mainRoot 'workspace\retail-work\tools\python-3.12-env\Scripts\python.exe'),
        (Join-Path $mainRoot 'workspace\retail-work\tools\cpython-3.12.13\python.exe')
    )
    $python = $pythonCandidates | Where-Object { [IO.File]::Exists($_) } |
        Select-Object -First 1
    if ([string]::IsNullOrWhiteSpace($python)) {
        Stop-Workflow 'pinned private Python runtime is missing'
    }

    $arguments = [Collections.Generic.List[string]]::new()
    foreach ($value in @('-I', '-S', '-B', $mainTool, '--main-root', $mainRoot)) {
        $arguments.Add($value)
    }

    if ($Command -in @('ready', 'create')) {
        if (-not [StringComparer]::OrdinalIgnoreCase.Equals($callerRoot, $mainRoot)) {
            Stop-Workflow "$Command must run from main"
        }
        if (-not [string]::IsNullOrWhiteSpace($LanePath) -or
            -not [string]::IsNullOrWhiteSpace($Reviewer) -or
            ($Command -ceq 'ready' -and
                (-not [string]::IsNullOrWhiteSpace($Id) -or
                 -not [string]::IsNullOrWhiteSpace($Assignee))) -or
            ($Command -ceq 'create' -and $Json)) {
            Stop-Workflow "$Command received an unsupported option"
        }
    }
    else {
        $targetLane = $LanePath
        if ([string]::IsNullOrWhiteSpace($targetLane)) {
            $targetLane = $callerRoot
        }
        $targetLane = Resolve-LiteralDirectory $targetLane 'target lane'
        if ([StringComparer]::OrdinalIgnoreCase.Equals($targetLane, $mainRoot)) {
            Stop-Workflow "$Command requires one sibling lane, not main"
        }
        $arguments.Add('--lane-root')
        $arguments.Add($targetLane)
    }

    $arguments.Add($Command)
    if ($Command -ceq 'ready') {
        if ($Json) {
            $arguments.Add('--json')
        }
    }
    elseif ($Command -ceq 'create') {
        if ([string]::IsNullOrWhiteSpace($Id) -or
            [string]::IsNullOrWhiteSpace($Assignee)) {
            Stop-Workflow 'create requires -Id and -Assignee'
        }
        $arguments.Add('--id')
        $arguments.Add($Id)
        $arguments.Add('--assignee')
        $arguments.Add($Assignee)
    }
    elseif ($Command -ceq 'review') {
        if ([string]::IsNullOrWhiteSpace($Reviewer)) {
            Stop-Workflow 'review requires -Reviewer'
        }
        if (-not [string]::IsNullOrWhiteSpace($Id) -or
            -not [string]::IsNullOrWhiteSpace($Assignee) -or $Json) {
            Stop-Workflow 'review received an unsupported option'
        }
        $arguments.Add('--reviewer')
        $arguments.Add($Reviewer)
    }
    elseif (-not [string]::IsNullOrWhiteSpace($Id) -or
        -not [string]::IsNullOrWhiteSpace($Assignee) -or
        -not [string]::IsNullOrWhiteSpace($Reviewer) -or $Json) {
        Stop-Workflow "$Command received an unsupported option"
    }

    $argumentArray = $arguments.ToArray()
    & $python @argumentArray
    exit $LASTEXITCODE
}
catch {
    Stop-Workflow $_.Exception.Message
}
