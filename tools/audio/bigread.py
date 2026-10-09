#!/usr/bin/env python3
"""Minimal BIG archive reader and retail mount-order model for the audio tools. Dev tooling only.

`Mount(rotwk_root, bfme2_root)` indexes the archives of both installs in the order the engine mounts them
(engine/src/Common/RetailArchivePolicy.h): every RotWK archive first, then every BFME2 archive; within an
install the canonical relative path (backslashes) sorted by strcmp and reversed; the first archive that holds a
virtual path wins. Archives are taken from engine/data/retail-archives/*.json (path names only; no hashing here).
"""
import glob
import json
import os
import struct

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
POLICY_DIR = os.path.join(REPO, "engine", "data", "retail-archives")


def read_big(path):
    """Returns [(virtual path lower-case with backslashes, offset, size)] of one BIG archive."""
    with open(path, "rb") as f:
        head = f.read(16)
        if head[:4] not in (b"BIGF", b"BIG4"):
            raise ValueError("not a BIG archive: " + path)
        count = struct.unpack(">I", head[8:12])[0]
        entries = []
        for _ in range(count):
            off, size = struct.unpack(">II", f.read(8))
            name = bytearray()
            while True:
                c = f.read(1)
                if c == b"\0" or not c:
                    break
                name += c
            entries.append((name.decode("latin1").lower().replace("/", "\\"), off, size))
    return entries


def _find_ci(root, rel):
    """Case-insensitive path below root."""
    cur = root
    for part in rel.replace("\\", "/").split("/"):
        hit = None
        for n in os.listdir(cur):
            if n.lower() == part.lower():
                hit = n
                break
        if hit is None:
            raise FileNotFoundError(os.path.join(cur, part))
        cur = os.path.join(cur, hit)
    return cur


def _policy_paths(policy_file):
    with open(os.path.join(POLICY_DIR, policy_file)) as f:
        return [a["path"] for a in json.load(f)["archives"]]


class Mount:
    def __init__(self, rotwk_root, bfme2_root):
        self.index = {}
        self.order = []
        for root, policy in ((rotwk_root, "rotwk-201-english-archives.json"), (bfme2_root, "bfme2-106-english-archives.json")):
            canon = sorted((p.replace("/", "\\") for p in _policy_paths(policy)), reverse=True)  # strcmp sort, then reverse
            for rel in canon:
                disk = _find_ci(root, rel)
                self.order.append(disk)
                for name, off, size in read_big(disk):
                    self.index.setdefault(name, (disk, off, size))  # first mounted wins

    def names(self, suffix):
        return sorted(n for n in self.index if n.endswith(suffix))

    def get(self, name):
        disk, off, size = self.index[name]
        with open(disk, "rb") as f:
            f.seek(off)
            return f.read(size)


def installs_from_env():
    rotwk = os.environ.get("ROTWK_INSTALL")
    bfme2 = os.environ.get("BFME2_INSTALL")
    if not rotwk or not bfme2:
        raise SystemExit("ROTWK_INSTALL and BFME2_INSTALL must be set")
    return rotwk, bfme2
