#!/usr/bin/env python3
"""Select intact verified retail archives and menu movies for a compact local package."""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
POLICIES = {'rotwk': 'rotwk-201', 'bfme2': 'bfme2-106'}


def compact_paths(folder, game):
    folder = Path(folder)
    policy = json.loads((ROOT / f'engine/data/retail-archives/{POLICIES[game]}-english-archives.json').read_text())
    files = {p.relative_to(folder).as_posix().lower(): p for p in folder.rglob('*') if p.is_file()}
    selected = set()
    for archive in policy['archives']:
        key = archive['path'].replace('\\', '/').lower()
        path = files.get(key)
        if path is None or path.stat().st_size != archive['size']:
            raise ValueError(f'{game}: missing or wrong-size required archive: {key}')
        selected.add(path.relative_to(folder).as_posix())
    for key, path in files.items():
        # gi.dat provides game metadata; UI movies remain available. Campaign CSxx
        # videos, publisher intros, Windows binaries and installers are omitted.
        if key == 'gi.dat' or (key.startswith('data/movies/') and path.suffix.lower() == '.vp6'
            and (path.stem.lower().startswith(('movie', 'credits', 'maplivingmap'))
                 or path.stem.lower() in ('loadingring', 'smallring') or 'flag_with_alpha' in path.stem.lower())):
            selected.add(path.relative_to(folder).as_posix())
    return selected
