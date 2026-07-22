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


def default_godot_content_root() -> Path:
    configured = os.environ.get("OPENBFME_CONTENT_ROOT", "").strip()
    if configured:
        return Path(configured).expanduser().resolve()
    return (repo_root_from_module() / ".private" / "content-packs").resolve()
