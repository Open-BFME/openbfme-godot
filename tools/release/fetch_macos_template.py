#!/usr/bin/env python3
"""Cache only macos.zip from Godot's official export-template archive (HTTP ranges)."""
import argparse
import io
import json
from pathlib import Path
import re
import subprocess
import tempfile
import zipfile


def fetch(url, start=None, end=None):
    with tempfile.TemporaryDirectory() as temp:
        headers = Path(temp) / 'headers'
        cmd = ['curl', '-L', '--fail', '-s', '--show-error', '--retry', '2', '-D', str(headers)]
        if start is not None:
            cmd += ['--range', f'{start}-{end}']
        result = subprocess.run(cmd + [url], check=True, stdout=subprocess.PIPE)
        if start is not None:
            ranges = re.findall(r'content-range:\s*bytes\s+(\d+)-(\d+)/(\d+)', headers.read_text(), re.I)
            if not ranges or tuple(map(int, ranges[-1][:2])) != (start, end) or len(result.stdout) != end - start + 1:
                raise RuntimeError('Server did not honor the requested archive range')
        return result.stdout


class RemoteZip(io.RawIOBase):
    def __init__(self, url, size):
        self.url, self.size, self.position = url, size, 0

    def seekable(self):
        return True

    def seek(self, offset, whence=0):
        self.position = (0 if whence == 0 else self.position if whence == 1 else self.size) + offset
        return self.position

    def tell(self):
        return self.position

    def read(self, count=-1):
        count = min(count if count >= 0 else self.size, self.size - self.position)
        if count <= 0:
            return b''
        data = fetch(self.url, self.position, self.position + count - 1)
        self.position += len(data)
        return data


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--version', required=True, help='Godot version tag, e.g. 4.7-stable')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if not re.fullmatch(r'\d+\.\d+(?:\.\d+)?-[A-Za-z0-9.]+', args.version):
        parser.error('invalid Godot version tag')
    if args.output.is_file() and zipfile.is_zipfile(args.output):
        print(f'Using cached macOS template: {args.output}')
        return
    release = json.loads(fetch(f'https://api.github.com/repos/godotengine/godot-builds/releases/tags/{args.version}'))
    name = f'Godot_v{args.version}_export_templates.tpz'
    asset = next(a for a in release['assets'] if a['name'] == name)
    print(f'Downloading macos.zip from official {name}', flush=True)
    with zipfile.ZipFile(RemoteZip(asset['browser_download_url'], asset['size'])) as archive:
        info = archive.getinfo('templates/macos.zip')
        if info.file_size > 512 * 1024 * 1024:
            raise RuntimeError('Unexpectedly large macOS template')
        data = archive.read(info)  # ZipFile validates the member CRC as it extracts.
    with zipfile.ZipFile(io.BytesIO(data)) as template:
        if not any(n.endswith('/Contents/Info.plist') for n in template.namelist()):
            raise RuntimeError('Downloaded template has no app bundle')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    partial = args.output.with_suffix('.partial')
    partial.write_bytes(data)
    partial.replace(args.output)
    print(f'Cached macOS template: {args.output}')


if __name__ == '__main__':
    main()
