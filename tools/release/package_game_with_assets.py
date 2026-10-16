#!/usr/bin/env python3
"""Create a portable macOS app containing the user's BFME2 and RotWK installs."""
import argparse
import os
from pathlib import Path
import plistlib
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--app', type=Path, default=ROOT / 'engine/build/macos/OpenBFME.app')
    parser.add_argument('--rotwk', type=Path, default=os.environ.get('ROTWK_INSTALL'))
    parser.add_argument('--bfme2', type=Path, default=os.environ.get('BFME2_INSTALL'))
    parser.add_argument('--output', type=Path, default=ROOT / 'engine/build/macos-with-assets/OpenBFME.app')
    parser.add_argument('--dry-run', action='store_true', help='Check inputs and report size without copying')
    args = parser.parse_args()
    sources = {}
    for name in ('rotwk', 'bfme2'):
        folder = getattr(args, name)
        if folder is None:
            parser.error(f'--{name} or {name.upper()}_INSTALL is required')
        folder = folder.resolve()
        if not folder.is_dir() or not any(p.suffix.lower() == '.big' for p in folder.iterdir()):
            parser.error(f'{name}: expected an installed game folder containing .big archives: {folder}')
        sources[name] = folder
    app, output = args.app.resolve(), args.output.resolve()
    info = app / 'Contents/Info.plist'
    if not info.is_file() or plistlib.loads(info.read_bytes()).get('CFBundleIdentifier') != 'org.openbfme.game':
        parser.error('build OpenBFME.app first with tools/release/build_macos.sh')
    if output.suffix != '.app' or output.exists():
        parser.error('output must be a new .app path (existing output is never overwritten)')
    for source in [app, *sources.values()]:
        if output == source or source in output.parents:
            parser.error('output must be outside all input folders')
    total = sum(p.stat().st_size for folder in [app, *sources.values()] for p in folder.rglob('*') if p.is_file())
    print(f'App plus installs: {total / 1024**3:.2f} GiB')
    for name, path in sources.items():
        print(f'{name}: {path}')
    print(f'Output: {output}')
    if args.dry_run:
        return
    output.parent.mkdir(parents=True, exist_ok=True)
    if shutil.disk_usage(output.parent).free < total:
        parser.error('not enough free space for the app and assets')
    with tempfile.TemporaryDirectory(prefix='asset-package-', dir=output.parent) as temp:
        staged = Path(temp) / 'OpenBFME.app'
        shutil.copytree(app, staged)
        retail = staged / 'Contents/Resources/retail'
        for name, folder in sources.items():
            print(f'Copying {name}...', flush=True)
            # Dereference links so the resulting app does not depend on the source install.
            shutil.copytree(folder, retail / name, symlinks=False)
        subprocess.run(['codesign', '--force', '--deep', '--sign', '-', '--timestamp=none', str(staged)], check=True)
        subprocess.run(['codesign', '--verify', '--deep', '--strict', str(staged)], check=True)
        staged.rename(output)
    print(f'BUILT APP WITH ASSETS: {output}')


if __name__ == '__main__':
    main()
