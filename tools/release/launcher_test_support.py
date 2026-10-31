"""Portable fixtures shared by the launcher release tests."""
from __future__ import annotations

import contextlib
import ctypes
import os
import shutil
import subprocess
import sys
import tempfile
from ctypes import wintypes
from pathlib import Path

WINDOWS = os.name == "nt"
PLATFORM = "windows-x64" if WINDOWS else "linux-x64"
GAME_EXE = "OpenBFME.exe" if WINDOWS else "OpenBFME.x86_64"
LAUNCHER_EXE = "OpenBFMELauncher.exe" if WINDOWS else "OpenBFMELauncher.x86_64"
ARCHIVE_SUFFIX = ".zip" if WINDOWS else ".tar.gz"
# lane MACGATE-1: the launcher tests serve and run linux-x64 / windows-x64 packages; a launcher on macOS takes none of them (it does no
# automated updates by design: updater.macos_updates_unsupported(), game_start.gd), so on macOS they are skipped by name; the Mac launcher
# has its own tests (test_macos_update.py, test_macplay.py)
MACOS = sys.platform == "darwin"
MACOS_SKIP = ("SKIP on macOS: these tests drive the Linux / Windows launcher's update path; the Mac launcher does no automated updates "
              "(test_macos_update.py / test_macplay.py cover it)")


def godot_engine(executable: str) -> str:
    executable = executable.strip()
    if WINDOWS and executable.lower().endswith("_console.exe"):
        engine = Path(executable[:-len("_console.exe")] + ".exe")
        if not engine.is_file():
            raise RuntimeError(f"NATIVE TEST PREREQUISITE: Godot engine beside its console wrapper is missing: {engine}")
        return str(engine)
    return executable


def dismiss_missing_pack_alert(pid: int) -> bool:
    """A broken Windows Godot export displays a modal error before it can exit."""
    if not WINDOWS:
        return False
    user32 = ctypes.WinDLL("user32", use_last_error=True)
    callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    user32.EnumWindows.argtypes = (callback_type, wintypes.LPARAM)
    user32.EnumChildWindows.argtypes = (wintypes.HWND, callback_type, wintypes.LPARAM)
    user32.GetWindowThreadProcessId.argtypes = (wintypes.HWND, ctypes.POINTER(wintypes.DWORD))
    user32.GetWindowTextW.argtypes = (wintypes.HWND, wintypes.LPWSTR, ctypes.c_int)
    user32.PostMessageW.argtypes = (wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM)
    dismissed = False

    def window(hwnd, _):
        nonlocal dismissed
        owner = wintypes.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        if owner.value != pid:
            return True
        title = ctypes.create_unicode_buffer(1024)
        user32.GetWindowTextW(hwnd, title, len(title))
        if title.value != "ALERT!":
            return True
        labels = []
        ok_buttons = []

        def child(control, _):
            label = ctypes.create_unicode_buffer(4096)
            user32.GetWindowTextW(control, label, len(label))
            labels.append(label.value)
            if label.value == "OK":
                ok_buttons.append(control)
            return True

        user32.EnumChildWindows(hwnd, callback_type(child), 0)
        if any("Couldn't load project data" in label for label in labels):
            if len(ok_buttons) != 1:
                return True
            if not user32.PostMessageW(ok_buttons[0], 0x00F5, 0, 0):
                raise ctypes.WinError(ctypes.get_last_error())
            dismissed = True
        return True

    user32.EnumWindows(callback_type(window), 0)
    return dismissed


def asset_name(kind: str, version: str) -> str:
    return f"openbfme{'-launcher' if kind == 'launcher' else ''}-{version}-{PLATFORM}{ARCHIVE_SUFFIX}"


def archive_name(version: str) -> str:
    return f"openbfme-{version}-{PLATFORM}{ARCHIVE_SUFFIX}"


def isolated_environment(tmp: Path, **extra: str) -> dict[str, str]:
    env = dict(os.environ)
    for name in ("BFME1_INSTALL", "ROTWK_INSTALL", "BFME2_INSTALL", "OPENBFME_GAME_USER_DIR"):
        env.pop(name, None)
    env.update(
        HOME=str(tmp / "home"),
        XDG_DATA_HOME=str(tmp / "data"),
        XDG_CONFIG_HOME=str(tmp / "config"),
        XDG_CACHE_HOME=str(tmp / "cache"),
        APPDATA=str(tmp / "appdata"),
        LOCALAPPDATA=str(tmp / "localappdata"),
    )
    if WINDOWS:
        env.update(
            USERPROFILE=str(tmp / "userprofile"),
            HOME=str(tmp / "userprofile"),
        )
    env.update(extra)
    return env


def _cl() -> str:
    compiler = shutil.which("cl.exe" if WINDOWS else "cl")
    if compiler:
        return compiler
    raise RuntimeError(
        "NATIVE TEST PREREQUISITE: cl.exe was not found; run from the VS 2022 x64 Native Tools "
        "Developer Command Prompt or PowerShell"
    )


_GAME_CACHE: dict[str, Path] = {}
_GAME_TEMP: tempfile.TemporaryDirectory | None = None


def _game_root() -> Path:
    global _GAME_TEMP
    if _GAME_TEMP is None:
        _GAME_TEMP = tempfile.TemporaryDirectory(prefix="openbfme-launcher-test-games-")
    return Path(_GAME_TEMP.name)


def native_game(mode: str) -> Path:
    """Build and return one explicit-mode Windows game fixture."""
    if not WINDOWS:
        raise RuntimeError("native_game is only available on Windows")
    if mode not in {"played", "version", "long", "args", "marker"}:
        raise ValueError(f"unknown native game mode: {mode}")
    if mode in _GAME_CACHE and _GAME_CACHE[mode].is_file():
        return _GAME_CACHE[mode]
    root = _game_root()
    root.mkdir(parents=True, exist_ok=True)
    output = root / f"OpenBFME-test-{mode}.exe"
    obj = root / f"OpenBFME-test-{mode}.obj"
    source = Path(__file__).with_name("launcher_test_game.c")
    mode_number = {"played": 0, "version": 1, "long": 2, "args": 3, "marker": 4}[mode]
    try:
        result = subprocess.run(
            [_cl(), "/nologo", "/O2", "/TC", f"/DOPENBFME_TEST_MODE={mode_number}", str(source), f"/Fo{obj}", f"/Fe{output}"],
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
        )
    finally:
        obj.unlink(missing_ok=True)
    if result.returncode:
        raise RuntimeError(f"NATIVE TEST GAME BUILD FAILED ({mode}):\n{result.stdout}{result.stderr}")
    _GAME_CACHE[mode] = output
    return output


def native_launcher_fake_game() -> Path:
    """Build the existing ONEZIP harness stand-in without changing its output contract."""
    if not WINDOWS:
        raise RuntimeError("native_launcher_fake_game is only available on Windows")
    root = _game_root()
    root.mkdir(parents=True, exist_ok=True)
    output = root / "OpenBFME-launcher-fake-game.exe"
    obj = root / "OpenBFME-launcher-fake-game.obj"
    source = Path(__file__).with_name("launcher_fake_game.c")
    try:
        result = subprocess.run(
            [_cl(), "/nologo", "/O2", "/TC", str(source), f"/Fo{obj}", f"/Fe{output}"],
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
        )
    finally:
        obj.unlink(missing_ok=True)
    if result.returncode:
        raise RuntimeError(f"NATIVE ONEZIP GAME BUILD FAILED:\n{result.stdout}{result.stderr}")
    return output


@contextlib.contextmanager
def deny_write(path: Path):
    """Hold a real Windows sharing handle that denies concurrent writes."""
    if not WINDOWS:
        yield None
        return
    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel32.CreateFileW.argtypes = (wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD,
                                    wintypes.LPVOID, wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE)
    kernel32.CreateFileW.restype = wintypes.HANDLE
    kernel32.CloseHandle.argtypes = (wintypes.HANDLE,)
    kernel32.CloseHandle.restype = wintypes.BOOL
    handle = kernel32.CreateFileW(
        str(path),
        0x80000000,  # GENERIC_READ
        0x00000001 | 0x00000004,  # FILE_SHARE_READ | FILE_SHARE_DELETE
        None,
        3,  # OPEN_EXISTING
        0x08000000,  # FILE_FLAG_BACKUP_SEMANTICS
        None,
    )
    if handle == ctypes.c_void_p(-1).value:
        raise OSError(ctypes.get_last_error(), f"CreateFileW deny-write: {path}")
    try:
        yield handle
    finally:
        kernel32.CloseHandle(handle)


def normalized_path(path: Path) -> str:
    return str(path).replace("\\", "/")


def stop_process(pid: int) -> None:
    if not WINDOWS:
        os.kill(pid, 15)
        return
    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel32.OpenProcess.argtypes = (wintypes.DWORD, wintypes.BOOL, wintypes.DWORD)
    kernel32.OpenProcess.restype = wintypes.HANDLE
    kernel32.TerminateProcess.argtypes = (wintypes.HANDLE, wintypes.UINT)
    kernel32.TerminateProcess.restype = wintypes.BOOL
    kernel32.WaitForSingleObject.argtypes = (wintypes.HANDLE, wintypes.DWORD)
    kernel32.WaitForSingleObject.restype = wintypes.DWORD
    kernel32.CloseHandle.argtypes = (wintypes.HANDLE,)
    kernel32.CloseHandle.restype = wintypes.BOOL
    handle = kernel32.OpenProcess(0x0001 | 0x00100000, False, pid)  # PROCESS_TERMINATE | SYNCHRONIZE
    if not handle:
        error = ctypes.get_last_error()
        if error == 87:  # ERROR_INVALID_PARAMETER: the test process already exited
            return
        raise OSError(error, f"OpenProcess({pid})")
    try:
        if not kernel32.TerminateProcess(handle, 0):
            raise OSError(ctypes.get_last_error(), f"TerminateProcess({pid})")
        if kernel32.WaitForSingleObject(handle, 10000) != 0:
            raise TimeoutError(f"process {pid} did not stop")
    finally:
        kernel32.CloseHandle(handle)


def process_running(pid: int) -> bool:
    if not WINDOWS:
        proc = Path(f"/proc/{pid}")
        return proc.exists() and "Z" not in (proc / "stat").read_text().split()[2]
    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel32.OpenProcess.argtypes = (wintypes.DWORD, wintypes.BOOL, wintypes.DWORD)
    kernel32.OpenProcess.restype = wintypes.HANDLE
    kernel32.WaitForSingleObject.argtypes = (wintypes.HANDLE, wintypes.DWORD)
    kernel32.WaitForSingleObject.restype = wintypes.DWORD
    kernel32.CloseHandle.argtypes = (wintypes.HANDLE,)
    kernel32.CloseHandle.restype = wintypes.BOOL
    handle = kernel32.OpenProcess(0x00100000, False, pid)  # SYNCHRONIZE
    if not handle:
        return False
    try:
        return kernel32.WaitForSingleObject(handle, 0) == 0x00000102
    finally:
        kernel32.CloseHandle(handle)
