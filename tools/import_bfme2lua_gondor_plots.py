#!/usr/bin/env python3
"""Copy the seven local BFME1 Men floor DDS textures from BFME2Lua's BIG archive."""
import argparse
import struct
from pathlib import Path

TEXTURES = {
    'gbarcheryn_bib', 'gbbarracks_bib', 'gbarmorybib', 'gbbtltwrs_bib',
    'gbstablet', 'gbworkshop_bib', 'gbfarmt',
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True, help='BFME2Lua project root')
    parser.add_argument('--output', type=Path, default=Path('godot/mods/gondor_plots'))
    args = parser.parse_args()
    archive = args.source / 'assets/gondor-build-plots/!bfme1_gondor_plots.big'
    data = archive.read_bytes()
    if data[:4] not in (b'BIGF', b'BIG4'):
        raise SystemExit(f'Not a BIG archive: {archive}')
    at = 16
    files = {}
    for _ in range(struct.unpack_from('>I', data, 8)[0]):
        offset, size = struct.unpack_from('>II', data, at)
        at += 8
        end = data.index(0, at)
        name = Path(data[at:end].decode('ascii').replace('\\', '/')).name.lower()
        at = end + 1
        if name.removesuffix('.dds') in TEXTURES and name.endswith('.dds'):
            payload = data[offset:offset + size]
            if len(payload) != size or payload[:4] != b'DDS ':
                raise SystemExit(f'Invalid DDS entry: {name}')
            files[name] = payload
    if set(files) != {name + '.dds' for name in TEXTURES}:
        raise SystemExit('Archive does not contain all seven expected plot textures')
    args.output.mkdir(parents=True, exist_ok=True)
    for name, payload in sorted(files.items()):
        (args.output / name).write_bytes(payload)
    print(f'{args.output}: {len(files)} DDS textures, {sum(map(len, files.values()))} bytes')


if __name__ == '__main__':
    main()
