#!/usr/bin/env python3
"""Import multiplayer maps and visual assets from a local 2.02 BIG, preserving retail campaigns.

The generated pack is local/untracked. No global balance INIs are imported.
"""
import argparse
import re
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent / 'audio'))
from bigread import Mount, read_big


def unescape(value):
    return re.sub(rb'_([0-9a-fA-F]{2})', lambda m: bytes([int(m[1], 16)]), value)


def blocks(data):
    return {unescape(m[1]).decode('latin1').lower(): m[0]
            for m in re.finditer(rb'(?mi)^MapCache\s+(\S+)[^\n]*\n.*?^END\s*$', data, re.S)}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source', type=Path, required=True)
    p.add_argument('--strings', type=Path, help='2.02 language BIG (defaults to lang/englishpatch202.big beside source)')
    p.add_argument('--rotwk', required=True)
    p.add_argument('--bfme2', required=True)
    p.add_argument('--output', type=Path, default=Path('godot/mods/rotwk_202_maps.big'))
    p.add_argument('--buckland-snow', type=Path, help='Generated Buckland Snow directory (stock Buckland layout)')
    args = p.parse_args()
    retail = Mount(args.rotwk, args.bfme2)
    index = {n: (o, s) for n, o, s in read_big(args.source)}
    language = args.strings or args.source.parent / 'lang/englishpatch202.big'
    with language.open('rb') as stream:
        entry = next((o, s) for n, o, s in read_big(language) if n == 'data\\lotr.str')
        stream.seek(entry[0])
        strings = stream.read(entry[1])
    labels = {m[1].lower(): m[0] for m in re.finditer(
        rb'(?mis)^(Map:[^\r\n]+)\r?\n.*?^END\b', strings)}
    with args.source.open('rb') as src:
        def read(name):
            off, size = index[name]
            src.seek(off)
            data = src.read(size)
            if len(data) != size:
                raise ValueError('Truncated entry: ' + name)
            return data
        cache = blocks(retail.get('maps\\mapcache.ini'))
        additions = {n: b for n, b in blocks(read('maps\\mapcache.ini')).items()
                     if re.search(rb'(?mi)^\s*isMultiplayer\s*=\s*yes\s*$', b)}
        dirs = {n.rsplit('\\', 1)[0] + '\\' for n in additions}
        files = {n: read(n) for n in index if n.startswith('maps\\')
                 and n.rsplit('\\', 1)[0] + '\\' in dirs}
        # Include the patch's visual dependency set, including replacements used by its models.
        # Terrain INIs map map-file texture names to their image files; they contain visual definitions.
        assets = {n: read(n) for n in index if n.startswith('art\\') or n in (
            'data\\ini\\terrain.ini', 'data\\ini\\default\\terrain.ini')}
        files.update(assets)
        for name, block in additions.items():
            if name not in files:
                raise ValueError('Map cache entry has no map: ' + name)
            # Supply the patch's map labels locally, without replacing global UI text.
            string_file = name.rsplit('\\', 1)[0] + '\\map.str'
            local = files.get(string_file, b'')
            for value in re.findall(rb'(?mi)^\s*(?:displayName|description)\s*=\s*([^\r\n]+)', block):
                label = unescape(value.strip()).decode('utf-16le').lstrip('$').encode('utf-8')
                if b':' not in label:
                    continue  # already literal text in the map cache
                if re.search(rb'(?mi)^' + re.escape(label) + rb'\s*$', local):
                    continue
                if label.lower() not in labels:
                    raise ValueError('Missing 2.02 map text: ' + label.decode())
                local += b'\n\n' + labels[label.lower()] + b'\n'
            files[string_file] = local
            cache[name] = block
        if args.buckland_snow:
            title = 'Buckland Snow'
            base = 'maps\\buckland snow\\'
            map_name = base + 'buckland snow.map'
            for leaf in (title + '.map', title + '_art.tga', title + '_pic.tga', 'map.ini'):
                files[base + leaf.lower()] = (args.buckland_snow / leaf).read_bytes()
            # The generator preserves stock Buckland's dimensions and starting positions.
            block = cache['maps\\map wor buckland\\map wor buckland.map']
            def field(key, value):
                nonlocal block
                block = re.sub(rb'(?mi)^(\s*' + key + rb'\s*=)[^\r\n]*', lambda m: m[1] + b' ' + value, block)
            escaped = ''.join('_%02X' % b for b in map_name.encode()).encode()
            block = re.sub(rb'(?m)^MapCache\s+\S+', b'MapCache ' + escaped, block)
            field(b'fileSize', str(len(files[map_name])).encode())
            field(b'fileCRC', b'0')  # unknown SAGE checksum; never reuse the stock map's checksum
            field(b'isOfficial', b'no')
            for key, text in [(b'displayName', title), (b'description', 'Generated winter version of Buckland, with snow terrain and falling snow. Supports four players.')]:
                field(key, ''.join('_%02X' % b for b in text.encode('utf-16le')).encode())
            cache[map_name] = block
        files['maps\\mapcache.ini'] = b'\n\n'.join(cache.values()) + b'\n'
    header_size = 16 + sum(8 + len(n.encode('latin1')) + 1 for n in files)
    directory, content = bytearray(), bytearray()
    for name, data in sorted(files.items()):
        directory += struct.pack('>II', header_size + len(content), len(data)) + name.encode('latin1') + b'\0'
        content += data
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(b'BIGF' + struct.pack('>III', header_size + len(content), len(files), header_size) + directory + content)
    print(f'{args.output}: {len(additions)} multiplayer maps, {len(assets)} visual assets, {len(files)} files, {args.output.stat().st_size} bytes')


if __name__ == '__main__':
    main()
