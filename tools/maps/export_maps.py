#!/usr/bin/env python3
"""Export the mounted in-game map catalog and rendered previews without modifying retail data."""
import argparse
import hashlib
import html
import json
import os
from pathlib import Path
import subprocess
import tempfile

PROJECT = Path(__file__).resolve().parents[2] / 'godot'


def map_id(key):
    """Stable filesystem-safe identity, including full catalog path to avoid name collisions."""
    return hashlib.sha256(key.encode('utf-8')).hexdigest()[:24]


def map_stem(key):
    return Path(key.replace("\\", "/")).stem.casefold()


def select_maps(catalog, keys, all_maps):
    if all_maps:
        return catalog
    wanted = set(keys)
    available = {entry['key'] for entry in catalog}
    missing = wanted - available
    if missing:
        raise ValueError('Unknown map keys: ' + ', '.join(sorted(missing)))
    return [entry for entry in catalog if entry['key'] in wanted]


def run_worker(godot, request, temporary, timeout, metadata_only):
    request_path = temporary / 'request.json'
    request_path.write_text(json.dumps(request), encoding='utf-8')
    output = Path(request['output'])
    output.unlink(missing_ok=True)
    command = [godot, '--path', str(PROJECT)]
    if metadata_only:
        command.append('--headless')
    command += ['--script', 'res://scripts/map_export.gd', '--', str(request_path)]
    process = subprocess.run(command, capture_output=True, text=True, timeout=timeout)
    (temporary / 'worker.log').write_text(process.stdout + process.stderr, encoding='utf-8')
    if process.returncode or not output.exists():
        raise RuntimeError(f'Godot worker failed ({process.returncode}):\n' + (process.stdout + process.stderr)[-6000:])
    result = json.loads(output.read_text(encoding='utf-8'))
    return result


def write_gallery(output, manifest):
    cards = []
    for item in manifest['maps']:
        entry = item['map']
        title = html.escape(entry['name'])
        image = item.get('preview', {}).get('image')
        preview = f'<img loading="lazy" src="{image}" alt="{title}">' if image else '<p>No preview available</p>'
        cards.append(f'<article><h2>{title}</h2>{preview}<p>{html.escape(entry.get("description", ""))}</p>'
                     f'<p>{entry["players"]} players · {"Official" if entry["official"] else "Custom"}</p>'
                     f'<a href="{item["id"]}.json">Map metadata</a></article>')
    (output / 'index.html').write_text('<!doctype html><meta charset="utf-8"><title>OpenBFME maps</title>'
        '<style>body{background:#10191e;color:#eee;font:16px system-ui;margin:2rem}'
        'main{display:grid;grid-template-columns:repeat(auto-fit,minmax(320px,1fr));gap:2rem}'
        'article{background:#203039;padding:1rem}img{width:100%}a{color:#e8c36d}</style>'
        '<h1>OpenBFME maps</h1><main>' + ''.join(cards) + '</main>', encoding='utf-8')


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--godot', default=os.environ.get('GODOT', 'godot'))
    parser.add_argument('--out', required=True, type=Path)
    selection = parser.add_mutually_exclusive_group(required=True)
    selection.add_argument('--all', action='store_true')
    selection.add_argument('--map', action='append', dest='keys', help='Exact catalog key; repeat for several maps')
    selection.add_argument('--maps-file', type=Path, help='JSON array of exact catalog keys')
    selection.add_argument('--list', action='store_true', help='Save catalog.json and print keys without exporting')
    parser.add_argument('--metadata-only', action='store_true')
    parser.add_argument('--width', type=int, default=1600)
    parser.add_argument('--height', type=int, default=1000)
    parser.add_argument('--timeout', type=int, default=300, help='Seconds per worker')
    args = parser.parse_args(argv)
    if not (64 <= args.width <= 8192 and 64 <= args.height <= 8192) or args.timeout <= 0:
        parser.error('Dimensions must be 64–8192 and timeout must be positive')
    output = args.out.resolve()
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='openbfme-map-export-') as directory:
        temporary = Path(directory)
        try:
            catalog = run_worker(args.godot, {'mode': 'catalog', 'output': str(output / 'catalog.json'),
                'profile': str(temporary / 'skirmish.ini')}, temporary, args.timeout, True)
            if args.list:
                for entry in catalog['maps']:
                    print(f'{entry["key"]}\t{entry["name"]}')
                return 0
            keys = json.loads(args.maps_file.read_text(encoding='utf-8')) if args.maps_file else args.keys
            if keys is not None and (not isinstance(keys, list) or not all(isinstance(key, str) for key in keys)):
                raise ValueError('Map selection must be a JSON array of strings')
            entries = select_maps(catalog['maps'], keys or [], args.all)
            if not entries:
                raise ValueError('No maps selected')
            manifest = {'schema_version': 1, 'content_signature': catalog['content_signature'], 'maps': []}
            failed = False
            for entry in entries:
                identity = map_id(entry['key'])
                request = {'mode': 'map', 'entry': entry, 'output': str(output / (identity + '.json')),
                    'image': str(output / (identity + '.png')), 'metadata_only': args.metadata_only,
                    'width': args.width, 'height': args.height}
                # Remove stale screenshots so failed reruns never display an earlier export.
                Path(request['image']).unlink(missing_ok=True)
                try:
                    # Native map builders resolve stems; never silently export the wrong map.
                    if sum(map_stem(candidate['key']) == map_stem(entry['key']) for candidate in catalog['maps']) > 1:
                        raise ValueError('Ambiguous map basename in mounted catalog: ' + entry['key'])
                    result = run_worker(args.godot, request, temporary, args.timeout, args.metadata_only)
                except (RuntimeError, subprocess.TimeoutExpired, OSError, ValueError) as error:
                    result = {'schema_version': 1, 'ok': False, 'map': entry, 'errors': [str(error)]}
                    Path(request['output']).write_text(json.dumps(result, indent=2), encoding='utf-8')
                result['id'] = identity
                complete = result.get('ok', False) and (args.metadata_only or result.get('preview', {}).get('status') == 'screenshot')
                failed |= not complete
                manifest['maps'].append(result)
                print(f'{entry["key"]}: {"ok" if complete else "incomplete (see JSON)"}', flush=True)
                (output / 'maps.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
                write_gallery(output, manifest)
            return 1 if failed else 0
        except (OSError, RuntimeError, ValueError, subprocess.TimeoutExpired) as error:
            print(str(error))
            return 1


if __name__ == '__main__':
    raise SystemExit(main())
