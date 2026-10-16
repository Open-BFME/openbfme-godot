#!/usr/bin/env python3
"""Select intact verified retail archives and menu movies for a compact local package."""
import json
from pathlib import Path, PurePosixPath

ROOT = Path(__file__).resolve().parents[2]
POLICIES = {'rotwk': 'rotwk-201', 'bfme2': 'bfme2-106'}


def compact_names(sizes, game):
    """The compact subset of `sizes` ({relative posix name: size}): every required archive (which must be present with its pinned size)
    and the extras below. Also the AIO download's compact set (tools/release/aio_pins.py, the iOS app)."""
    policy = json.loads((ROOT / f'engine/data/retail-archives/{POLICIES[game]}-english-archives.json').read_text())
    names = {name.lower(): name for name in sizes}
    selected = set()
    for archive in policy['archives']:
        key = archive['path'].replace('\\', '/').lower()
        name = names.get(key)
        if name is None or sizes[name] != archive['size']:
            raise ValueError(f'{game}: missing or wrong-size required archive: {key}')
        selected.add(name)
    for key, name in names.items():
        path = PurePosixPath(key)
        # gi.dat provides game metadata; UI movies remain available. Campaign CSxx
        # videos, publisher intros, Windows binaries and installers are omitted.
        if key == 'gi.dat' or (key.startswith('data/movies/') and path.suffix == '.vp6'
            and (path.stem.startswith(('movie', 'credits', 'maplivingmap'))
                 or path.stem in ('loadingring', 'smallring') or 'flag_with_alpha' in path.stem)):
            selected.add(name)
    return selected


def compact_paths(folder, game):
    folder = Path(folder)
    sizes = {p.relative_to(folder).as_posix(): p.stat().st_size for p in folder.rglob('*') if p.is_file()}
    return compact_names(sizes, game)
