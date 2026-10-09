"""Minimal read-only view of the pure RotWK 2.01 archive mount, for the FX scanners.

Independent of the engine: BIG headers are parsed here (BIGF/BIG4: big-endian count at +8, then
{offset, size, NUL-terminated name} entries), the mount order is the pinned policy JSON order
(RotWK 2.01 archives first, then BFME2 1.06; the first archive that holds a path wins, paths compare
case-insensitively with '/' and '\\' equal). Install folders come from ROTWK_INSTALL / BFME2_INSTALL;
nothing retail is stored in git.
"""
from __future__ import annotations

import json
import os
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
POLICY_DIR = ROOT / "engine" / "data" / "retail-archives"
POLICIES = (("ROTWK_INSTALL", "rotwk-201-english-archives.json"), ("BFME2_INSTALL", "bfme2-106-english-archives.json"))


class MountError(RuntimeError):
    pass


def _find(install: Path, rel: str) -> Path:
    p = install / rel
    if p.exists():
        return p
    cur = install
    for part in rel.replace("\\", "/").split("/"):
        hits = [x for x in os.listdir(cur) if x.lower() == part.lower()]
        if not hits:
            raise MountError(f"archive {rel} not found under {install}")
        cur = cur / hits[0]
    return cur


class Mount:
    def __init__(self):
        self.files: dict[str, tuple[Path, int, int]] = {}
        for env, policy in POLICIES:
            install = os.environ.get(env)
            if not install:
                raise MountError(f"{env} is not set")
            for a in json.loads((POLICY_DIR / policy).read_text())["archives"]:
                self._add(_find(Path(install), a["path"]))

    def _add(self, path: Path) -> None:
        with open(path, "rb") as f:
            head = f.read(16)
            if head[:4] not in (b"BIGF", b"BIG4"):
                raise MountError(f"{path} is not a BIG archive")
            count = struct.unpack(">I", head[8:12])[0]
            for _ in range(count):
                off, size = struct.unpack(">II", f.read(8))
                name = bytearray()
                while True:
                    c = f.read(1)
                    if c in (b"", b"\0"):
                        break
                    name += c
                key = name.decode("latin-1").replace("/", "\\").lower()
                self.files.setdefault(key, (path, off, size))

    @staticmethod
    def key(path: str) -> str:
        return path.replace("/", "\\").lower()

    def exists(self, path: str) -> bool:
        return self.key(path) in self.files

    def read(self, path: str) -> bytes:
        hit = self.files.get(self.key(path))
        if hit is None:
            raise MountError(f"{path} is not in the mount")
        p, off, size = hit
        with open(p, "rb") as f:
            f.seek(off)
            return f.read(size)

    def list(self, prefix: str, suffix: str = "") -> list[str]:
        k = self.key(prefix)
        return sorted(n for n in self.files if n.startswith(k) and n.endswith(suffix.lower()))
