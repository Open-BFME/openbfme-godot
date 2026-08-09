"""Contained private-workspace path policy for retail-derived content."""

from __future__ import annotations

import os
from pathlib import Path
from pathlib import PurePosixPath
import re


WINDOWS_DEVICE_NAMES = {
    "con",
    "prn",
    "aux",
    "nul",
    *{f"com{index}" for index in range(1, 10)},
    *{f"lpt{index}" for index in range(1, 10)},
    # Superscript digit COM/LPT forms rejected by pathlib on Windows.
    "com¹",
    "com²",
    "com³",
    "lpt¹",
    "lpt²",
    "lpt³",
}

# pathlib treats these as reserved prefixes (not only exact stems).
_WINDOWS_RESERVED_PREFIXES = ("conin$", "conout$")


def _is_windows_reserved_part(part: str) -> bool:
    """Fast equivalent of PureWindowsPath(part).is_reserved() for our checks.

    Avoids constructing pathlib objects per path component (hot on 40k-file walks).
    """

    # Match pathlib: strip extension and trailing spaces before the first space.
    stem = part.split(".", 1)[0].split(" ", 1)[0].casefold()
    if stem in WINDOWS_DEVICE_NAMES:
        return True
    return any(stem == prefix or stem.startswith(prefix) for prefix in _WINDOWS_RESERVED_PREFIXES)


def expand_windows_long_path(path: Path) -> Path:
    """Expand Windows 8.3 short path segments (e.g. RUNNER~1) to long form.

    GitHub-hosted Windows runners sometimes hand back mixed short/long forms of
    the same temp directory from Path.resolve(). relative_to() and path equality
    then fail even when the filesystem entry is identical. No-op on non-Windows
    and when the path does not exist or expansion is unavailable.
    """

    if os.name != "nt":
        return path
    try:
        import ctypes
        from ctypes import wintypes

        get_long = ctypes.windll.kernel32.GetLongPathNameW
        get_long.argtypes = [wintypes.LPCWSTR, wintypes.LPWSTR, wintypes.DWORD]
        get_long.restype = wintypes.DWORD
        text = str(path)
        size = get_long(text, None, 0)
        if size == 0:
            return path
        buffer = ctypes.create_unicode_buffer(size)
        written = get_long(text, buffer, size)
        if written == 0:
            return path
        return Path(buffer.value)
    except Exception:
        return path


def safe_relative_parts(value: str) -> tuple[str, ...]:
    normalized = value.replace("\\", "/")
    if not normalized or normalized.startswith("/") or normalized.startswith("~"):
        raise ValueError(f"unsafe relative path: {value!r}")
    if re.match(r"^[A-Za-z]:", normalized):
        raise ValueError(f"absolute drive path is not allowed: {value!r}")
    parts = PurePosixPath(normalized).parts
    if not parts:
        raise ValueError(f"empty relative path: {value!r}")
    for part in parts:
        if part in {"", ".", ".."}:
            raise ValueError(f"relative path traversal is not allowed: {value!r}")
        if ":" in part or any(ord(character) < 32 for character in part):
            raise ValueError(f"stream/control characters are not allowed: {value!r}")
        if part.endswith(".") or part.endswith(" "):
            raise ValueError(f"trailing dot/space is not allowed: {value!r}")
        if _is_windows_reserved_part(part):
            raise ValueError(f"Windows device path is not allowed: {value!r}")
    return tuple(parts)


def default_state_root() -> Path:
    configured = os.environ.get("OPENBFME_IMPORT_ROOT", "").strip()
    if configured:
        return Path(configured).expanduser().resolve()
    return (repo_root_from_module() / ".private" / "retail-work").resolve()


def ensure_external_to_repo(path: Path, repo_root: Path) -> Path:
    """Accept external roots or the one ignored private root in the checkout.

    The compatibility project now keeps its private retail working set beside
    the code for development. Containment remains narrow: no retail-derived
    output may be written anywhere else inside the repository.
    """

    resolved = path.expanduser().resolve()
    repository = repo_root.expanduser().resolve()
    try:
        relative = resolved.relative_to(repository)
    except ValueError:
        return resolved
    private_root = (repository / ".private").resolve()
    if resolved == private_root or private_root in resolved.parents:
        return resolved
    raise ValueError(
        "retail-derived output inside the repository must stay under its "
        f"ignored .private directory: {relative}"
    )


def repo_root_from_module() -> Path:
    return Path(__file__).resolve().parents[2]


#: Locations a stock BFME2 / RotWK install is normally found at on Windows.
#: Relative to each of the drives/roots probed by :func:`retail_install_candidates`.
_RETAIL_RELATIVE_DIRS: tuple[str, ...] = (
    "Electronic Arts/The Battle for Middle-earth II",
    "EA Games/The Battle for Middle-earth II",
    "Electronic Arts/The Lord of the Rings, The Rise of the Witch-king",
    "EA Games/The Lord of the Rings, The Rise of the Witch-king",
    "Steam/steamapps/common/The Battle for Middle-earth II",
    "GOG Galaxy/Games/The Battle for Middle-earth II",
)

#: A file that must exist for a directory to be accepted as a retail install.
RETAIL_INSTALL_MARKER = "game.dat"


def retail_install_candidates() -> list[Path]:
    """Machine-neutral guesses for where a player's retail install lives.

    Deliberately contains no developer-specific path: every entry is derived
    from this machine's own environment (program-files roots, fixed drives).
    """

    roots: list[Path] = []
    for variable in ("ProgramFiles(x86)", "ProgramFiles", "ProgramW6432"):
        value = os.environ.get(variable, "").strip()
        if value:
            roots.append(Path(value))
    # Games are commonly installed to a secondary data drive rather than C:.
    if os.name == "nt":
        for letter in "CDEFGH":
            drive = Path(f"{letter}:/")
            if drive.exists():
                roots.append(drive)
                roots.append(drive / "Games")

    candidates: list[Path] = []
    seen: set[Path] = set()
    for root in roots:
        for relative in _RETAIL_RELATIVE_DIRS:
            candidate = root / relative
            if candidate not in seen:
                seen.add(candidate)
                candidates.append(candidate)
    return candidates


def discover_retail_install() -> Path | None:
    """Return the first plausible retail install, or ``None`` if none is found.

    ``BFME2_INSTALL`` always wins so a player (or CI) can point at any path.
    """

    configured = os.environ.get("BFME2_INSTALL", "").strip()
    if configured:
        return Path(configured).expanduser()
    for candidate in retail_install_candidates():
        try:
            if (candidate / RETAIL_INSTALL_MARKER).is_file():
                return candidate.resolve()
        except OSError:
            continue
    return None


def default_retail_install() -> Path:
    """Best-effort retail install path for callers that require *some* path.

    Prefer :func:`discover_retail_install` and handle ``None`` where you can
    report a useful error; this exists only for legacy positional defaults.
    """

    found = discover_retail_install()
    if found is not None:
        return found
    candidates = retail_install_candidates()
    return candidates[0] if candidates else Path("BFME2")


def default_godot_content_root() -> Path:
    configured = os.environ.get("OPENBFME_CONTENT_ROOT", "").strip()
    if configured:
        return Path(configured).expanduser().resolve()
    return (repo_root_from_module() / ".private" / "content-packs").resolve()
