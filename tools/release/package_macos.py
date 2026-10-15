#!/usr/bin/env python3
"""Export a self-contained, ad-hoc signed OpenBFME.app using the installed Godot editor."""
import argparse
import copy
import os
from pathlib import Path
import platform
import plistlib
import re
import shutil
import subprocess
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[2]


def run(*args):
    subprocess.run([str(a) for a in args], check=True)


def host_template(template, arch, build):
    """The exporter expects architecture-named binaries; official archives contain universal ones."""
    with zipfile.ZipFile(template) as source:
        wanted = f'macos_template.app/Contents/MacOS/godot_macos_debug.{arch}'
        if wanted in source.namelist():
            return template
        target = build / 'export-templates' / (template.parent.name + '-' + arch + '.zip')
        target.parent.mkdir(parents=True, exist_ok=True)
        if target.is_file() and target.stat().st_mtime >= template.stat().st_mtime:
            return target
        with tempfile.TemporaryDirectory(dir=build) as temp, zipfile.ZipFile(target.with_suffix('.partial'), 'w', zipfile.ZIP_DEFLATED) as out:
            for entry in source.infolist():
                data = source.read(entry)
                info = copy.copy(entry)
                if '/Contents/MacOS/godot_macos_' in entry.filename and entry.filename.endswith('.universal'):
                    universal, thin = Path(temp) / 'universal', Path(temp) / 'thin'
                    universal.write_bytes(data)
                    run('xcrun', 'lipo', universal, '-thin', arch, '-output', thin)
                    data = thin.read_bytes()
                    info.filename = entry.filename.removesuffix('.universal') + '.' + arch
                out.writestr(info, data)
        target.with_suffix('.partial').replace(target)
        return target


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, default=ROOT / 'engine/build')
    parser.add_argument('--godot', default=os.environ.get('GODOT', '/Applications/Godot.app/Contents/MacOS/Godot'))
    parser.add_argument('--output', type=Path)
    parser.add_argument('--template', type=Path, help='Optional existing macos.zip export template')
    args = parser.parse_args()
    if platform.system() != 'Darwin':
        parser.error('macOS packaging must run on macOS')
    build = args.build_dir.resolve()
    output = (args.output or build / 'macos/OpenBFME.app').resolve()
    if output.suffix != '.app':
        parser.error('output must end in .app')
    godot = shutil.which(args.godot) or args.godot
    version = subprocess.check_output([godot, '--version'], text=True).strip().split('.')
    channel = next((i for i, part in enumerate(version) if not part.isdigit()), len(version))
    tag = '.'.join(version[:channel]) + '-' + version[channel]
    template = args.template or Path(os.environ.get('GODOT_MACOS_TEMPLATE', build / f'export-templates/{tag}/macos.zip'))
    template = template.resolve()
    if not template.is_file():
        run('python3', ROOT / 'tools/release/fetch_macos_template.py', '--version', tag, '--output', template)
    arch = 'arm64' if platform.machine() == 'arm64' else 'x86_64'
    template = host_template(template, arch, build)
    library = ROOT / f'godot/bin/openbfme.macos.template_debug.{arch}.dylib'
    if not library.is_file():
        parser.error(f'missing native library; run build_macos.sh first: {library}')
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='macos-export-', dir=build) as temp:
        stage = Path(temp) / 'project'
        shutil.copytree(ROOT / 'godot', stage, ignore=shutil.ignore_patterns('.godot', 'bin'))
        (stage / 'bin').mkdir()
        shutil.copy2(library, stage / 'bin' / library.name)
        presets = (stage / 'export_presets.cfg').read_text()
        marker = '[preset.2.options]'
        before, options = presets.split(marker, 1)
        options = re.sub(r'^custom_template/debug=.*$', 'custom_template/debug=' + '"' + str(template) + '"', options, flags=re.M)
        options = re.sub(r'^binary_format/architecture=.*$', f'binary_format/architecture="{arch}"', options, flags=re.M)
        (stage / 'export_presets.cfg').write_text(before + marker + options)
        app = Path(temp) / 'OpenBFME.app'
        run(godot, '--headless', '--path', stage, '--editor', '--import', '--quit')
        run(godot, '--headless', '--path', stage, '--export-debug', 'macOS', app)
        info_path = app / 'Contents/Info.plist'
        info = plistlib.loads(info_path.read_bytes())
        info['LSMinimumSystemVersion'] = '26.0'
        info['CFBundleIconFile'] = 'second-age.icns'
        info_path.write_bytes(plistlib.dumps(info))
        shutil.copy2(ROOT / 'godot/assets/second-age.icns', app / 'Contents/Resources/second-age.icns')
        executable = app / 'Contents/MacOS' / info['CFBundleExecutable']
        if not executable.is_file() or not os.access(executable, os.X_OK):
            raise RuntimeError('Exported app executable is missing or not executable')
        if not list(app.rglob('*.pck')) or not list(app.rglob(library.name)):
            raise RuntimeError('Exported app is missing its project pack or native extension')
        run('xcrun', 'lipo', '-verify_arch', arch, executable)
        for dylib in app.rglob('*.dylib'):
            run('codesign', '--force', '--sign', '-', '--timestamp=none', dylib)
        run('codesign', '--force', '--deep', '--sign', '-', '--timestamp=none', app)
        run('codesign', '--verify', '--deep', '--strict', app)
        # Only replace a previous generated OpenBFME bundle, after the new export passes verification.
        if output.exists():
            old_info = plistlib.loads((output / 'Contents/Info.plist').read_bytes())
            if old_info.get('CFBundleIdentifier') != 'org.openbfme.game':
                raise RuntimeError(f'Refusing to replace a different application: {output}')
            shutil.rmtree(output)
        shutil.move(str(app), str(output))
    print(f'BUILT APP: {output}')
    print('Double-click the app in Finder to play.')


if __name__ == '__main__':
    main()
