#!/usr/bin/env python3
"""Resolve the RotWK layered install root (RotWK expansion + BFME2 base).

RotWK's own terrain.big is thin; most multiplayer map terrain textures live in
the BFME2 base. The private layered-install tree (junctions) is the supported
way to present both installs as one catalog:

  .private/retail-work/editions/rotwk/layered-install/
    layer-0-rotwk -> <RotWK install>
    layer-1-bfme2 -> <BFME2 install>

Lower layer numbers win on conflicts; missing RotWK assets fall through to
BFME2. Returns None when the layered tree is incomplete so callers fail closed
or fall back intentionally.
"""

from __future__ import annotations

from pathlib import Path


def layered_rotwk_install(state_root: Path | str) -> Path | None:
    root = Path(state_root).expanduser().resolve() / "editions" / "rotwk" / "layered-install"
    rotwk = root / "layer-0-rotwk" / "game.dat"
    bfme2 = root / "layer-1-bfme2" / "game.dat"
    if rotwk.is_file() and bfme2.is_file():
        return root
    return None


def ensure_layered_rotwk_install(
    state_root: Path | str,
    *,
    rotwk_install: Path | str,
    bfme2_install: Path | str | None = None,
) -> Path:
    """Return layered install path, creating junctions when missing.

    Does not invent assets. Requires real game.dat at both installs.
    """
    state = Path(state_root).expanduser().resolve()
    existing = layered_rotwk_install(state)
    if existing is not None:
        return existing

    rotwk = Path(rotwk_install).expanduser().resolve()
    if not (rotwk / "game.dat").is_file():
        raise FileNotFoundError(f"RotWK game.dat missing: {rotwk}")

    bfme2: Path | None = None
    if bfme2_install is not None:
        candidate = Path(bfme2_install).expanduser().resolve()
        if (candidate / "game.dat").is_file():
            bfme2 = candidate
    if bfme2 is None:
        for probe in (
            Path("D:/BFME2"),
            Path("C:/BFME2"),
            Path("D:/Games/BFME2"),
            rotwk.parent / "BFME2",
            rotwk.parent / "The Battle for Middle-earth II",
        ):
            if (probe / "game.dat").is_file():
                bfme2 = probe.resolve()
                break
    if bfme2 is None:
        raise FileNotFoundError(
            "BFME2 game.dat not found; layered RotWK terrain cook requires the base install"
        )

    root = state / "editions" / "rotwk" / "layered-install"
    root.mkdir(parents=True, exist_ok=True)
    _ensure_junction(root / "layer-0-rotwk", rotwk)
    _ensure_junction(root / "layer-1-bfme2", bfme2)
    ready = layered_rotwk_install(state)
    if ready is None:
        raise RuntimeError(f"failed to materialize layered install at {root}")
    return ready


def _ensure_junction(link: Path, target: Path) -> None:
    import os
    import subprocess

    if link.exists() or link.is_symlink():
        # Accept an existing junction/symlink that already points at target.
        try:
            if link.resolve() == target.resolve():
                return
        except OSError:
            pass
        raise RuntimeError(f"layered path exists but is not the expected junction: {link}")

    if os.name == "nt":
        # Directory junction (no admin required for local volumes).
        completed = subprocess.run(
            ["cmd", "/c", "mklink", "/J", str(link), str(target)],
            capture_output=True,
            text=True,
            check=False,
        )
        if completed.returncode != 0:
            raise RuntimeError(
                f"mklink /J failed for {link} -> {target}: "
                f"{completed.stdout} {completed.stderr}"
            )
        return
    link.symlink_to(target, target_is_directory=True)


if __name__ == "__main__":
    import sys

    state = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(".private/retail-work")
    path = layered_rotwk_install(state)
    if path is None:
        print("LAYERED_INSTALL missing")
        raise SystemExit(2)
    print(path)
