#!/usr/bin/env python3
"""Xcode pre-build: regenerate code resources from the configured main checkout."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import sys


def package_assets(config):
    """Run after resource copying and before Xcode signs the application."""
    destination = Path(os.environ['TARGET_BUILD_DIR']) / os.environ['UNLOCALIZED_RESOURCES_FOLDER_PATH'] / 'retail'
    enabled = os.environ.get('OPENBFME_BUNDLE_ASSETS', 'NO').upper() in ('YES', 'TRUE', '1')
    if not enabled:
        if destination.exists(): shutil.rmtree(destination)
        print('Game assets: external storage (bundling disabled)')
        return
    folders = {}
    for name in ('rotwk', 'bfme2'):
        value = os.environ.get('OPENBFME_' + name.upper() + '_ASSETS', '') or config.get(name + '_assets', '')
        folder = Path(value) if value else None
        if folder is None or not folder.is_dir() or not any(folder.glob('*.big')):
            raise SystemExit('Set OPENBFME_' + name.upper() + '_ASSETS to the installed game folder before enabling asset bundling')
        folders[name] = folder.resolve()
    for name, folder in folders.items():
        target = destination / name
        target.mkdir(parents=True, exist_ok=True)
        subprocess.run(['/usr/bin/rsync', '-a', '--delete', str(folder) + '/', str(target) + '/'], check=True)
    print('Game assets bundled in ' + str(destination))


def main():
    # Xcode launched from Finder has a minimal PATH, without Homebrew tools.
    os.environ['PATH'] = os.pathsep.join([
        '/opt/homebrew/bin', '/usr/local/bin',
        '/Applications/CMake.app/Contents/bin', os.environ.get('PATH', '')])
    for tool in ('cmake', 'ninja', 'python3'):
        if not shutil.which(tool):
            raise SystemExit('Missing build tool: ' + tool + '; install CMake, Ninja and Python 3')
    export = Path(__file__).resolve().parent
    config = json.loads((export / 'openbfme-source.json').read_text())
    if '--assets-only' in sys.argv:
        package_assets(config)
        return
    source = Path(os.environ.get('OPENBFME_SOURCE_DIR', config['source'])).resolve()
    godot = os.environ.get('GODOT', config['godot'])
    if not (source / 'tools/release/package_ios.py').is_file():
        raise SystemExit('Main checkout missing; set OPENBFME_SOURCE_DIR to openbfme-godot')
    simulator = os.environ.get('PLATFORM_NAME') == 'iphonesimulator' or os.environ.get('SDK_NAME', '').startswith('iphonesimulator')
    target_args = ['--simulator'] if simulator else []
    subprocess.run(['bash', str(source / 'tools/release/build_ios.sh')] + target_args, check=True)
    with tempfile.TemporaryDirectory(prefix='openbfme-ios-sync-') as temp:
        subprocess.run(['python3', str(source / 'tools/release/package_ios.py'),
                        '--godot', godot, '--output-dir', temp, '--no-source-sync'] + target_args, check=True)
        generated = Path(temp)
        shutil.copy2(generated / 'OpenBFME.pck', export / 'OpenBFME.pck')
        framework = Path('OpenBFME/dylibs/bin/openbfme.ios.template_debug.arm64.framework')
        shutil.copytree(generated / framework, export / framework, dirs_exist_ok=True)
        if simulator:
            # The stock template lacks arm64 simulator code. Copy the custom
            # Godot library and its platform manifest along with the extension.
            shutil.copytree(generated / 'OpenBFME.xcframework', export / 'OpenBFME.xcframework', dirs_exist_ok=True)


if __name__ == '__main__':
    main()
