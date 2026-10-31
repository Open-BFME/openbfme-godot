#!/usr/bin/env python3
"""Lane SDK-1: turn the models and textures of a mod archive (.big) into an asset pack folder (engine GameClient/AssetPacks.h, docs/MODS.md).

    python3 tools/mod/asset_pack_from_big.py <archive.big> <pack folder>

An HD archive (for example the All In One launcher's "RotWK 2.02 HD Edition") mounted as a mod is game data: the lobby and the replays
fingerprint it, so everyone in a game needs it. Its .w3d models and .dds / .tga / .png / .jpg textures as an asset pack only change what
is drawn, so they work online next to players without it. Everything else in the archive (INI, scripts, asset.dat, .db files, ...) is left
out and listed. When the archive holds one texture name twice (art\\compiledtextures\\xx\\a.dds and art\\textures\\a.tga), the one the game's
texture lookup takes first is kept (Libraries/WWVegas/WW3D2/textureloader.cpp: the compiled .dds, then the compiled .tga a model names, then
art\\textures\\<name>.tga). An archive path that could leave the pack folder (absolute, a drive, '..') stops the tool before it
writes. The pack keeps the archive's folders. The game checks each model against the one it replaces when it starts.
"""
import os
import struct
import sys

TEXTURES = {"dds", "tga", "png", "jpg"}


def read_big(path: str):
    """[(name, offset, size)] of a BIGF / BIG4 archive"""
    with open(path, "rb") as f:
        head = f.read(16)
        if head[:4] not in (b"BIGF", b"BIG4"):
            raise SystemExit(f"{path}: not a .big archive")
        count = struct.unpack(">I", head[8:12])[0]
        index_end = struct.unpack(">I", head[12:16])[0]
        index = f.read(max(0, index_end - 16))
    out, at = [], 0
    for _ in range(count):
        off, size = struct.unpack(">II", index[at:at + 8])
        end = index.index(b"\0", at + 8)
        out.append((index[at + 8:end].decode("latin-1"), off, size))
        at = end + 1
    return out


def rank(name: str) -> int:
    """the texture lookup's order for one stem (lower = taken first). A model names a .tga: the lookup tries the compiled .dds, the
    compiled .tga, then art\\textures\\<name>.tga; any other file of the stem is not on that path, so it comes last"""
    n = name.lower().replace("/", "\\")
    if n.startswith("art\\compiledtextures\\") and n.endswith(".dds"):
        return 0
    if n.startswith("art\\compiledtextures\\") and n.endswith(".tga"):
        return 1
    return 2 if n.startswith("art\\textures\\") and n.endswith(".tga") else 3


def safe_parts(name: str):
    """the archive path as folder parts, or None when it could leave the pack folder (absolute, a drive, '..', empty parts)"""
    parts = name.replace("\\", "/").split("/")
    if not name or name.startswith(("/", "\\")) or ":" in name or any(p in ("", ".", "..") for p in parts):
        return None
    return parts


def plan(entries):
    """(kept [(name, offset, size)], left out [(name, why)])"""
    kept, left, textures = [], [], {}
    models = {}
    for name, off, size in entries:
        base = name.replace("\\", "/").rsplit("/", 1)[-1]
        ext = base.rsplit(".", 1)[-1].lower() if "." in base else ""
        if ext == "w3d":
            key = base.lower()
            if key in models:
                left.append((name, "the same model as " + models[key][0]))
                continue
            models[key] = (name, off, size)
        elif ext in TEXTURES:
            key = base.rsplit(".", 1)[0].lower()
            old = textures.get(key)
            if old is None or rank(name) < rank(old[0]):
                if old is not None:
                    left.append((old[0], "the game's texture lookup takes " + name + " first"))
                textures[key] = (name, off, size)
            else:
                left.append((name, "the game's texture lookup takes " + old[0] + " first"))
        else:
            left.append((name, "not a model or texture (game data stays in the mod)"))
    kept = sorted(list(models.values()) + list(textures.values()))
    return kept, left


def main(argv) -> int:
    if len(argv) != 3:
        print(__doc__.strip().splitlines()[2].strip())
        return 2
    big, pack = argv[1], argv[2]
    if os.path.exists(pack) and os.listdir(pack):
        print(f"{pack} is not empty: give a new folder")
        return 2
    kept, left = plan(read_big(big))
    root = os.path.realpath(pack)
    for name, _, _ in kept:  # every path is checked before anything is written
        parts = safe_parts(name)
        dest = os.path.realpath(os.path.join(root, *parts)) if parts else ""
        if not parts or os.path.commonpath([root, dest]) != root:
            print(f"{big}: the archive path {name!r} would leave the pack folder: nothing written")
            return 1
    with open(big, "rb") as f:
        for name, off, size in kept:
            dest = os.path.join(root, *safe_parts(name))
            os.makedirs(os.path.dirname(dest), exist_ok=True)
            f.seek(off)
            data = f.read(size)
            if len(data) != size:
                print(f"{big}: {name} is cut short")
                return 1
            with open(dest, "wb") as out:
                out.write(data)
    for name, why in left:
        print(f"left out: {name}: {why}")
    print(f"asset pack {pack}: {sum(1 for n, _, _ in kept if n.lower().endswith('.w3d'))} models, "
          f"{sum(1 for n, _, _ in kept if not n.lower().endswith('.w3d'))} textures; {len(left)} files left out")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
