#!/usr/bin/env python3
"""Export OpenBFME's iOS Xcode project (device signing is configured in Xcode)."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import re
import plistlib
import zipfile
from fetch_macos_template import fetch, RemoteZip
from compact_retail_assets import compact_paths

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--godot', default=os.environ.get('GODOT', '/Applications/Godot.app/Contents/MacOS/Godot'))
    parser.add_argument('--team-id', default=os.environ.get('APPLE_TEAM_ID', ''), help='Optional Apple development team ID')
    parser.add_argument('--build-dir', type=Path, default=ROOT / 'engine/build-ios')
    parser.add_argument('--rotwk', type=Path, default=os.environ.get('ROTWK_INSTALL'))
    parser.add_argument('--bfme2', type=Path, default=os.environ.get('BFME2_INSTALL'))
    parser.add_argument('--output-dir', type=Path, help='New Xcode export directory')
    parser.add_argument("--compact-assets", action="store_true", help="Omit campaign videos, publisher intros and unused Windows install files")
    args = parser.parse_args()
    installs = {}
    if args.rotwk or args.bfme2:
        for name in ('rotwk', 'bfme2'):
            folder = getattr(args, name)
            if folder is None or not folder.is_dir() or not any(p.suffix.lower() == '.big' for p in folder.iterdir()):
                parser.error(f'--{name} must name an installed game folder containing .big archives')
            installs[name] = folder.resolve()
    build = args.build_dir.resolve()
    build.mkdir(parents=True, exist_ok=True)
    template = build / 'ios.zip'
    if not template.exists():
        version = subprocess.check_output([args.godot, '--version'], text=True).strip().split('.')
        channel = next(i for i, part in enumerate(version) if not part.isdigit())
        tag = '.'.join(version[:channel]) + '-' + version[channel]
        release = json.loads(fetch(f'https://api.github.com/repos/godotengine/godot-builds/releases/tags/{tag}'))
        asset = next(a for a in release['assets'] if a['name'] == f'Godot_v{tag}_export_templates.tpz')
        with zipfile.ZipFile(RemoteZip(asset['browser_download_url'], asset['size'])) as archive:
            data = archive.read('templates/ios.zip')
        partial = template.with_suffix('.partial')
        partial.write_bytes(data)
        partial.replace(template)
    framework = ROOT / 'godot/bin/openbfme.ios.template_debug.arm64.framework'
    if not framework.is_dir():
        parser.error('run tools/release/build_ios.sh first')
    output = (args.output_dir.resolve() if args.output_dir else build / 'export') / 'OpenBFME.xcodeproj'
    if output.exists():
        parser.error(f'export already exists: {output}; select a new --build-dir')
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='ios-export-', dir=build) as temp:
        stage = Path(temp) / 'project'
        shutil.copytree(ROOT / 'godot', stage, ignore=shutil.ignore_patterns('.godot', 'bin'))
        (stage / 'bin').mkdir()
        shutil.copytree(framework, stage / 'bin' / framework.name)
        # The editor imports scripts using the host extension before exporting the iOS one.
        host = ROOT / 'godot/bin/openbfme.macos.template_debug.arm64.dylib'
        shutil.copy2(host, stage / 'bin' / host.name)
        presets = stage / 'export_presets.cfg'
        before, options = presets.read_text().split('[preset.3.options]', 1)
        options = options.replace('custom_template/debug=""', f'custom_template/debug="{template}"', 1)
        options = options.replace('custom_template/release=""', f'custom_template/release="{template}"', 1)
        # Godot requires a nonempty team even for project-only export. Strip this
        # temporary placeholder from the generated project before any Xcode build.
        team = args.team_id or 'UNSIGNED00'
        options = options.replace('application/app_store_team_id=""', f'application/app_store_team_id="{team}"', 1)
        presets.write_text(before + '[preset.3.options]' + options)
        subprocess.run([args.godot, '--headless', '--path', str(stage), '--editor', '--import', '--quit'], check=True)
        subprocess.run([args.godot, '--headless', '--path', str(stage), '--export-debug', 'iOS', str(output)], check=True)
    # The official template references SDL's UIKit idiom queries without defining them.
    dummy = output.parent / 'OpenBFME/dummy.cpp'
    dummy.write_text(dummy.read_text() + (Path(__file__).parent / 'ios_sdl_uikit.cpp').read_text())
    if not args.team_id:
        project = output / 'project.pbxproj'
        project.write_text(project.read_text().replace('UNSIGNED00', '""'))
    # iOS apps keep resources beside their executable, unlike macOS Contents/Resources.
    # A folder reference copies loose movies and archives intact; they stay outside the PCK.
    if installs:
        retail = output.parent / 'retail'
        for name, folder in installs.items():
            print(f'Bundling {name}...', flush=True)
            if args.compact_assets:
                for relative in sorted(compact_paths(folder, name)):
                    target = retail / name / relative
                    target.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copy2(folder / relative, target)
            else:
                shutil.copytree(folder, retail / name, symlinks=False)
        project = output / 'project.pbxproj'
        text = project.read_text()
        build_id, ref_id = 'BF0000000000000000000001', 'BF0000000000000000000002'
        text = text.replace('/* End PBXBuildFile section */',
            f'{build_id} = {{isa = PBXBuildFile; fileRef = {ref_id}; }};\n/* End PBXBuildFile section */')
        text = text.replace('/* End PBXFileReference section */',
            f'{ref_id} = {{isa = PBXFileReference; lastKnownFileType = folder; path = retail; sourceTree = SOURCE_ROOT; }};\n/* End PBXFileReference section */')
        marker = text.index('/* Begin PBXResourcesBuildPhase section */')
        before, after = text[:marker], text[marker:]
        after = re.sub(r'(files = \()', r'\1\n' + build_id + ',', after, count=1)
        project.write_text(before + after)
    # Allow opening provider/removable-storage documents in place; never import
    # multi-gigabyte archives into the application's Documents directory.
    info = output.parent / 'OpenBFME/OpenBFME-Info.plist'
    plist = plistlib.loads(info.read_bytes())
    plist['LSSupportsOpeningDocumentsInPlace'] = True
    plist['UIFileSharingEnabled'] = True
    info.write_bytes(plistlib.dumps(plist))
    # Apple's native launch storyboard is separate from Godot's disabled boot splash.
    storyboard = output.parent / 'OpenBFME/Launch Screen.storyboard'
    if storyboard.exists():
        text = storyboard.read_text()
        text = re.sub(r'<imageView\b', '<imageView hidden="YES"', text)
        storyboard.write_text(text)
    print(f'EXPORTED: {output}')


if __name__ == '__main__':
    main()
