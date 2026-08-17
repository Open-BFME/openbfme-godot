#!/usr/bin/env python3
"""Fail if a Git-tracked path shares NTFS identity with disposable evidence."""

from __future__ import annotations

import argparse
import ctypes
from ctypes import wintypes
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
from typing import Iterable

EXIT_PASS = 0
EXIT_VIOLATION = 1
EXIT_ERROR = 2
FILE_ATTRIBUTE_REPARSE_POINT = 0x400
FILE_READ_ATTRIBUTES = 0x80
FILE_SHARE_ALL = 0x1 | 0x2 | 0x4
OPEN_EXISTING = 3
FILE_FLAG_BACKUP_SEMANTICS = 0x02000000
FILE_FLAG_OPEN_REPARSE_POINT = 0x00200000
INVALID_HANDLE_VALUE = ctypes.c_void_p(-1).value
DEFAULT_EVIDENCE_ROOT = "workspace/orchestration/wotr/disposable"


class GateError(RuntimeError):
    """An error that means the gate did not complete and must not pass."""


class BY_HANDLE_FILE_INFORMATION(ctypes.Structure):
    _fields_ = [
        ("dwFileAttributes", wintypes.DWORD),
        ("ftCreationTime", wintypes.FILETIME),
        ("ftLastAccessTime", wintypes.FILETIME),
        ("ftLastWriteTime", wintypes.FILETIME),
        ("dwVolumeSerialNumber", wintypes.DWORD),
        ("nFileSizeHigh", wintypes.DWORD),
        ("nFileSizeLow", wintypes.DWORD),
        ("nNumberOfLinks", wintypes.DWORD),
        ("nFileIndexHigh", wintypes.DWORD),
        ("nFileIndexLow", wintypes.DWORD),
    ]


def _windows_api():
    if os.name != "nt":
        raise GateError("Windows is required; NTFS identity cannot be authoritative here")
    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel32.CreateFileW.argtypes = [
        wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, wintypes.LPVOID,
        wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE,
    ]
    kernel32.CreateFileW.restype = wintypes.HANDLE
    kernel32.GetFileInformationByHandle.argtypes = [
        wintypes.HANDLE, ctypes.POINTER(BY_HANDLE_FILE_INFORMATION)
    ]
    kernel32.GetFileInformationByHandle.restype = wintypes.BOOL
    kernel32.CloseHandle.argtypes = [wintypes.HANDLE]
    kernel32.CloseHandle.restype = wintypes.BOOL
    kernel32.GetVolumePathNameW.argtypes = [
        wintypes.LPCWSTR, wintypes.LPWSTR, wintypes.DWORD
    ]
    kernel32.GetVolumePathNameW.restype = wintypes.BOOL
    kernel32.GetVolumeInformationW.argtypes = [
        wintypes.LPCWSTR, wintypes.LPWSTR, wintypes.DWORD,
        ctypes.POINTER(wintypes.DWORD), ctypes.POINTER(wintypes.DWORD),
        ctypes.POINTER(wintypes.DWORD), wintypes.LPWSTR, wintypes.DWORD,
    ]
    kernel32.GetVolumeInformationW.restype = wintypes.BOOL
    return kernel32


def _win_error(operation: str, path: Path) -> GateError:
    code = ctypes.get_last_error()
    return GateError(f"{operation} failed for {path}: {ctypes.WinError(code)}")


def _filesystem_type(kernel32, path: Path) -> str:
    volume = ctypes.create_unicode_buffer(32768)
    if not kernel32.GetVolumePathNameW(str(path), volume, len(volume)):
        raise _win_error("GetVolumePathNameW", path)
    fs_name = ctypes.create_unicode_buffer(256)
    if not kernel32.GetVolumeInformationW(
        volume.value, None, 0, None, None, None, fs_name, len(fs_name)
    ):
        raise _win_error("GetVolumeInformationW", path)
    return fs_name.value


def _file_identity(kernel32, path: Path) -> tuple[tuple[int, int], bool]:
    handle = kernel32.CreateFileW(
        str(path), FILE_READ_ATTRIBUTES, FILE_SHARE_ALL, None, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, None,
    )
    if handle == INVALID_HANDLE_VALUE:
        raise _win_error("CreateFileW", path)
    try:
        info = BY_HANDLE_FILE_INFORMATION()
        if not kernel32.GetFileInformationByHandle(handle, ctypes.byref(info)):
            raise _win_error("GetFileInformationByHandle", path)
    finally:
        kernel32.CloseHandle(handle)
    file_index = (info.nFileIndexHigh << 32) | info.nFileIndexLow
    identity = (info.dwVolumeSerialNumber, file_index)
    is_reparse = bool(info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
    return identity, is_reparse


def _identity_text(identity: tuple[int, int]) -> str:
    return f"{identity[0]:08x}:{identity[1]:016x}"


def _display(path: Path, repo: Path) -> str:
    try:
        return path.relative_to(repo).as_posix()
    except ValueError:
        return str(path)


def _git_paths(repo: Path, git: str) -> list[Path]:
    try:
        result = subprocess.run(
            [git, "-C", str(repo), "ls-files", "-z"],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False,
        )
    except OSError as exc:
        raise GateError(f"could not execute git: {exc}") from exc
    if result.returncode != 0:
        detail = result.stderr.decode("utf-8", "replace").strip()
        raise GateError(f"git ls-files failed ({result.returncode}): {detail}")
    try:
        names = result.stdout.decode("utf-8").split("\0")
    except UnicodeDecodeError as exc:
        raise GateError(f"git ls-files produced invalid UTF-8: {exc}") from exc
    if names and names[-1] == "":
        names.pop()
    return [repo / Path(name) for name in names]


def _evidence_paths(root: Path) -> Iterable[Path]:
    pending = [root]
    while pending:
        directory = pending.pop()
        try:
            with os.scandir(directory) as entries:
                batch = list(entries)
        except OSError as exc:
            raise GateError(f"could not enumerate evidence directory {directory}: {exc}") from exc
        batch.sort(key=lambda entry: entry.name)
        for entry in batch:
            path = Path(entry.path)
            yield path
            try:
                if entry.is_dir(follow_symlinks=False):
                    pending.append(path)
            except OSError as exc:
                raise GateError(f"could not inspect evidence path {path}: {exc}") from exc


def _validate_fsutil(repo: Path) -> str:
    fsutil = shutil.which("fsutil.exe") or shutil.which("fsutil")
    if not fsutil:
        raise GateError("fsutil is unavailable on PATH")
    try:
        probe = subprocess.run(
            [fsutil, "file", "queryfileid", str(repo)],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False,
        )
    except OSError as exc:
        raise GateError(f"fsutil could not be executed: {exc}") from exc
    if probe.returncode != 0:
        detail = (probe.stderr or probe.stdout).decode("utf-8", "replace").strip()
        raise GateError(f"fsutil file queryfileid failed ({probe.returncode}): {detail}")
    return fsutil


def run_gate(repo: Path, evidence_arguments: list[str], as_json: bool) -> int:
    started = time.perf_counter()
    receipt: dict[str, object] = {
        "verdict": "ERROR",
        "exit_code": EXIT_ERROR,
        "repository": str(repo),
        "evidence_root": evidence_arguments[0] if len(evidence_arguments) == 1 else None,
        "evidence_roots": evidence_arguments,
        "counts": {"tracked_files_scanned": 0, "evidence_paths_scanned": 0,
                   "intersections_found": 0, "tracked_reparse_points": 0},
        "timings_seconds": {},
    }
    try:
        repo = repo.resolve(strict=True)
        receipt["repository"] = str(repo)
        kernel32 = _windows_api()
        _validate_fsutil(repo)
        if _filesystem_type(kernel32, repo).upper() != "NTFS":
            raise GateError(f"repository filesystem is not NTFS: {_filesystem_type(kernel32, repo)}")
        git = shutil.which("git.exe") or shutil.which("git")
        if not git:
            raise GateError("git is unavailable on PATH")

        roots: list[Path] = []
        for argument in evidence_arguments:
            candidate = Path(argument)
            root = candidate if candidate.is_absolute() else repo / candidate
            try:
                root = root.resolve(strict=True)
            except OSError as exc:
                raise GateError(f"evidence directory is absent or inaccessible: {root}: {exc}") from exc
            if not root.is_dir():
                raise GateError(f"evidence root is not a directory: {root}")
            if _filesystem_type(kernel32, root).upper() != "NTFS":
                raise GateError(f"evidence filesystem is not NTFS: {root}")
            roots.append(root)
        receipt["evidence_roots"] = [str(path) for path in roots]
        receipt["evidence_root"] = str(roots[0]) if len(roots) == 1 else None

        phase = time.perf_counter()
        tracked_paths = _git_paths(repo, git)
        receipt["timings_seconds"]["git_enumeration"] = time.perf_counter() - phase

        phase = time.perf_counter()
        tracked: list[tuple[Path, tuple[int, int]]] = []
        reparse_paths: list[Path] = []
        for path in tracked_paths:
            identity, is_reparse = _file_identity(kernel32, path)
            tracked.append((path, identity))
            if is_reparse:
                reparse_paths.append(path)
        receipt["counts"]["tracked_files_scanned"] = len(tracked)
        receipt["counts"]["tracked_reparse_points"] = len(reparse_paths)
        receipt["timings_seconds"]["tracked_scan"] = time.perf_counter() - phase

        phase = time.perf_counter()
        evidence_by_id: dict[tuple[int, int], list[Path]] = {}
        evidence_count = 0
        for evidence_root in roots:
            for path in _evidence_paths(evidence_root):
                identity, is_reparse = _file_identity(kernel32, path)
                if is_reparse:
                    raise GateError(
                        f"evidence reparse point prevents complete enumeration: {path}"
                    )
                evidence_by_id.setdefault(identity, []).append(path)
                evidence_count += 1
        receipt["counts"]["evidence_paths_scanned"] = evidence_count
        receipt["timings_seconds"]["evidence_scan"] = time.perf_counter() - phase

        offenses: list[dict[str, str]] = []
        for tracked_path, identity in tracked:
            for evidence_path in evidence_by_id.get(identity, []):
                offenses.append({
                    "tracked": _display(tracked_path, repo),
                    "evidence": _display(evidence_path, repo),
                    "identity": _identity_text(identity),
                })
        receipt["counts"]["intersections_found"] = len(offenses)
        receipt["offending_pairs"] = offenses
        receipt["tracked_reparse_paths"] = [_display(path, repo) for path in reparse_paths]
        failed = bool(offenses or reparse_paths)
        receipt["verdict"] = "FAIL" if failed else "PASS"
        receipt["exit_code"] = EXIT_VIOLATION if failed else EXIT_PASS
        code = EXIT_VIOLATION if failed else EXIT_PASS
    except (GateError, OSError) as exc:
        receipt["error"] = str(exc)
        code = EXIT_ERROR

    receipt["timings_seconds"]["total"] = time.perf_counter() - started
    # Stable precision makes receipts easier to compare while retaining useful timing detail.
    receipt["timings_seconds"] = {
        key: round(value, 6) for key, value in receipt["timings_seconds"].items()
    }
    if as_json:
        print(json.dumps(receipt, ensure_ascii=False, sort_keys=True))
    else:
        if code == EXIT_VIOLATION:
            for offense in receipt.get("offending_pairs", []):
                print("OFFENDING PAIR: "
                      f"tracked={offense['tracked']} evidence={offense['evidence']} "
                      f"file_id={offense['identity']}")
            for path in receipt.get("tracked_reparse_paths", []):
                print(f"TRACKED REPARSE POINT: {path}")
        elif code == EXIT_ERROR:
            print(f"ERROR: {receipt.get('error', 'unknown enumeration error')}")
        counts = receipt["counts"]
        note = " (0 evidence paths; empty evidence directory is legal)" if (
            code != EXIT_ERROR and counts["evidence_paths_scanned"] == 0
        ) else ""
        print(
            f"HARDLINK ISOLATION {receipt['verdict']}: "
            f"tracked={counts['tracked_files_scanned']} "
            f"evidence={counts['evidence_paths_scanned']} "
            f"intersections={counts['intersections_found']} "
            f"tracked_reparse={counts['tracked_reparse_points']}{note}"
        )
    return code


def _run_command(command: list[str], cwd: Path, env=None) -> subprocess.CompletedProcess[str]:
    return subprocess.run(command, cwd=cwd, env=env, text=True,
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=False)


def _make_fixture(base: Path, name: str, mode: str) -> tuple[Path, Path]:
    repo = base / name
    evidence = repo / DEFAULT_EVIDENCE_ROOT
    evidence.mkdir(parents=True)
    init = _run_command([shutil.which("git.exe") or shutil.which("git") or "git",
                         "init", "--quiet"], repo)
    if init.returncode:
        raise GateError(f"self-test git init failed: {init.stdout.strip()}")
    tracked = repo / "tracked.txt"
    tracked.write_bytes(b"hardlink-isolation-self-test\n")
    add = _run_command([shutil.which("git.exe") or shutil.which("git") or "git",
                        "add", "--", "tracked.txt"], repo)
    if add.returncode:
        raise GateError(f"self-test git add failed: {add.stdout.strip()}")
    if mode == "hardlink":
        os.link(tracked, evidence / "capture.txt")
    elif mode == "clean":
        shutil.copyfile(tracked, evidence / "capture.txt")
    elif mode == "junction":
        tracked.unlink()
        target = repo / "junction-target"
        target.mkdir()
        cmd = Path(os.environ.get("SystemRoot", r"C:\Windows")) / "System32" / "cmd.exe"
        made = _run_command([str(cmd), "/d", "/c", "mklink", "/J",
                             str(tracked), str(target)], repo)
        if made.returncode:
            raise GateError(f"self-test junction creation failed: {made.stdout.strip()}")
    elif mode in ("empty", "absent"):
        if mode == "absent":
            shutil.rmtree(evidence)
    else:
        raise AssertionError(mode)
    return repo, evidence


def run_self_test(script: Path) -> int:
    if os.name != "nt":
        print("SELF-TEST ERROR: Windows is required")
        return EXIT_ERROR
    git = shutil.which("git.exe") or shutil.which("git")
    fsutil = shutil.which("fsutil.exe") or shutil.which("fsutil")
    if not git or not fsutil:
        print("SELF-TEST ERROR: git and fsutil must be available")
        return EXIT_ERROR
    cases = [
        ("planted-hardlink", "hardlink", EXIT_VIOLATION, False),
        ("tracked-junction", "junction", EXIT_VIOLATION, False),
        ("clean-copy", "clean", EXIT_PASS, False),
        ("missing-fsutil", "clean", EXIT_ERROR, True),
        ("absent-evidence", "absent", EXIT_ERROR, False),
        ("empty-evidence", "empty", EXIT_PASS, False),
    ]
    all_ok = True
    try:
        with tempfile.TemporaryDirectory(prefix="hardlink-isolation-self-test-") as temporary:
            base = Path(temporary)
            for label, mode, expected, strip_path in cases:
                repo, evidence = _make_fixture(base, label, mode)
                command = [sys.executable, str(script), "--repo", str(repo),
                           "--evidence-root", str(evidence)]
                env = os.environ.copy()
                if strip_path:
                    env["PATH"] = ""
                result = _run_command(command, repo, env)
                ok = result.returncode == expected
                all_ok &= ok
                print(f"SELF-TEST case={label} expected={expected} exit={result.returncode} "
                      f"result={'OK' if ok else 'WRONG'}")
                if result.stdout:
                    for line in result.stdout.rstrip().splitlines():
                        print(f"  {line}")
    except (GateError, OSError) as exc:
        print(f"SELF-TEST ERROR: {exc}")
        return EXIT_ERROR
    print(f"SELF-TEST {'PASS' if all_ok else 'FAIL'}: {len(cases)} cases")
    return EXIT_PASS if all_ok else EXIT_VIOLATION


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", default=".", help="repository root (default: current directory)")
    parser.add_argument(
        "--evidence-root", action="append", dest="evidence_roots",
        help=("evidence directory, absolute or relative to the repository; repeat for "
              f"additional roots (default: {DEFAULT_EVIDENCE_ROOT})"),
    )
    parser.add_argument("--json", action="store_true", help="emit one JSON receipt")
    parser.add_argument("--self-test", action="store_true", help="run isolated fixtures under TEMP")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(sys.argv[1:] if argv is None else argv)
    if args.self_test:
        if args.json or args.evidence_roots or args.repo != ".":
            print("ERROR: --self-test cannot be combined with --json, --repo, or --evidence-root")
            return EXIT_ERROR
        return run_self_test(Path(__file__).resolve())
    roots = args.evidence_roots or [DEFAULT_EVIDENCE_ROOT]
    return run_gate(Path(args.repo), roots, args.json)


if __name__ == "__main__":
    raise SystemExit(main())
