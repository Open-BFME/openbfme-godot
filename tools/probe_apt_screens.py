"""Answer one question cheaply: can the pinned APT record reader see a screen?

`retail_hud_apt_convert.py` is a PALANTIR instrument - fixed MOVIE_CLOSURE,
fixed external-movie loads, Palantir-specific topology contracts. Before
committing to a lane that cooks the other retail screens (queue Q117) it is
worth knowing whether their bytes are even the same shape. They are: every
screen below decodes with the same fixed PlaceObject record
(kind==3, flags, depth, characterId, 2x2 matrix, translation, name pointer)
that the Palantir converter reads.

    python tools/probe_apt_screens.py [MovieName ...]

Prints, per movie: byte size, decoded placements, and the named clips - which
is enough to see that MainMenu really does carry SoloPlayNav/OptionsNav and
QuitMenu really does carry Save/Load/Restart/ExitMission.
"""

from __future__ import annotations

import os
import struct
import sys

ROOT = os.path.join(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
    "workspace", "retail-work", "cache", "effective-assets",
)
DEFAULT = (
    "MainMenu", "Skirmish", "Options", "QuitMenu", "ScoreScreen",
    "SpellStore", "LoadScreen", "SaveLoad", "MenuFrameAndBg", "Palantir",
)


def probe(name: str) -> tuple[int, int, list[str]]:
    data = open(os.path.join(ROOT, name + ".apt"), "rb").read()

    def text(offset: int) -> str | None:
        if offset <= 0 or offset >= len(data):
            return None
        end = data.find(b"\0", offset)
        return data[offset:end].decode("latin-1", "replace")

    placements = 0
    named: list[str] = []
    for offset in range(0, len(data) - 64, 4):
        if struct.unpack_from("<I", data, offset)[0] != 3:
            continue
        flags = struct.unpack_from("<I", data, offset + 4)[0]
        if flags & ~0xFF:
            continue
        depth, character = struct.unpack_from("<ii", data, offset + 8)
        if not 0 <= depth < 5000 or not -1 <= character < 5000:
            continue
        matrix = struct.unpack_from("<4f", data, offset + 16)
        translation = struct.unpack_from("<2f", data, offset + 32)
        if any(abs(value) > 1e5 for value in matrix + translation):
            continue
        placements += 1
        if flags & 0x20:
            clip = text(struct.unpack_from("<I", data, offset + 52)[0])
            if clip and clip.isprintable() and 1 < len(clip) < 40:
                named.append(clip)
    return len(data), placements, named


def main(argv: list[str]) -> int:
    for name in argv[1:] or list(DEFAULT):
        try:
            size, placements, named = probe(name)
        except OSError as error:
            print(f"{name:<16} UNREADABLE {error}")
            continue
        print(
            f"{name:<16} {size:>7} bytes  placements={placements:<5} "
            f"named={len(named):<4} e.g. {', '.join(named[:6])}"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
