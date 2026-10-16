#!/usr/bin/env python3
"""Add the matching source-built arm64 Godot simulator library to a local template."""
from pathlib import Path
import argparse
import plistlib
import subprocess
import zipfile


def prepare(template: Path, library: Path, output: Path):
    subprocess.run(['xcrun', 'lipo', '-verify_arch', 'arm64', str(library)], check=True)
    output.parent.mkdir(parents=True, exist_ok=True)
    prefix = 'libgodot.ios.debug.xcframework/'
    identifier = 'ios-arm64-simulator'
    with zipfile.ZipFile(template) as src, zipfile.ZipFile(output.with_suffix('.partial'), 'w', zipfile.ZIP_DEFLATED) as dst:
        for info in src.infolist():
            if info.filename.startswith(prefix + identifier + '/'):
                continue
            data = src.read(info)
            if info.filename == prefix + 'Info.plist':
                plist = plistlib.loads(data)
                plist['AvailableLibraries'] = [entry for entry in plist['AvailableLibraries']
                    if entry.get('SupportedPlatformVariant') != 'simulator']
                plist['AvailableLibraries'].append({'LibraryIdentifier': identifier,
                    'LibraryPath': 'libgodot.a', 'SupportedArchitectures': ['arm64'],
                    'SupportedPlatform': 'ios', 'SupportedPlatformVariant': 'simulator'})
                data = plistlib.dumps(plist)
            dst.writestr(info, data)
        dst.write(library, prefix + identifier + '/libgodot.a')
    output.with_suffix('.partial').replace(output)


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--template', type=Path, required=True)
    p.add_argument('--library', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    prepare(a.template, a.library, a.output)
