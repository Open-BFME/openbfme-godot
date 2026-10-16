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
import xml.etree.ElementTree as ET
from fetch_macos_template import fetch, RemoteZip

ROOT = Path(__file__).resolve().parents[2]


def install_source_sync(project, source, godot):
    """Refresh generated code resources before Xcode compiles or copies them."""
    config = project.parent / 'openbfme-source.json'
    config.write_text(json.dumps({'source': str(source), 'godot': godot}, indent=2) + '\n')
    script = project.parent / 'sync-openbfme.py'
    shutil.copy2(ROOT / 'tools/release/sync_ios_export.py', script)
    text = (project / 'project.pbxproj').read_text()
    phase = 'BF0000000000000000000101'
    entry = f'''/* Begin PBXShellScriptBuildPhase section */
{phase} /* Refresh main repository code */ = {{
    isa = PBXShellScriptBuildPhase;
    alwaysOutOfDate = 1;
    buildActionMask = 2147483647;
    files = ();
    inputPaths = ();
    outputPaths = ();
    name = "Refresh main repository code";
    runOnlyForDeploymentPostprocessing = 0;
    shellPath = /bin/sh;
    shellScript = "python3 \\"$SRCROOT/sync-openbfme.py\\"";
}};
/* End PBXShellScriptBuildPhase section */
'''
    text = text.replace('/* Begin PBXSourcesBuildPhase section */', entry + '\n/* Begin PBXSourcesBuildPhase section */')
    assets_phase = 'BF0000000000000000000102'
    asset_entry = entry.replace(phase, assets_phase).replace('Refresh main repository code', 'Package optional game assets')
    asset_entry = re.sub(r'    shellScript = .*;', lambda _: '    shellScript = ' + json.dumps('python3 "$SRCROOT/sync-openbfme.py" --assets-only') + ';', asset_entry)
    text = text.replace('/* Begin PBXSourcesBuildPhase section */', asset_entry + '\n/* Begin PBXSourcesBuildPhase section */')
    text = re.sub(r'(buildPhases = \(.*?)(\);)', lambda m: m[1] + assets_phase + ',\n' + m[2], text, count=1, flags=re.S)
    text = text.replace('buildPhases = (', 'buildPhases = (\n' + phase + ',', 1)
    text = text.replace('buildSettings = {', 'buildSettings = {\n OPENBFME_BUNDLE_ASSETS = NO;\n OPENBFME_ROTWK_ASSETS = \"\";\n OPENBFME_BFME2_ASSETS = \"\";\n ENABLE_USER_SCRIPT_SANDBOXING = NO;')
    (project / 'project.pbxproj').write_text(text)


def configure_launch_screen(storyboard):
    if not storyboard.exists():
        return
    tree = ET.parse(storyboard)
    view = tree.find('.//viewController/view')
    for image in view.findall('.//imageView'):
        image.set('hidden', 'YES')
    label = ET.SubElement(view.find('subviews'), 'label', {
        'text': 'Starting OpenBFME…', 'textAlignment': 'center',
        'translatesAutoresizingMaskIntoConstraints': 'NO', 'id': 'OBF-start-label'})
    ET.SubElement(label, 'fontDescription', {'key': 'fontDescription', 'type': 'system', 'pointSize': '28'})
    ET.SubElement(label, 'color', {'key': 'textColor', 'white': '1', 'alpha': '1', 'colorSpace': 'custom', 'customColorSpace': 'genericGamma22GrayColorSpace'})
    for axis in ('centerX', 'centerY'):
        ET.SubElement(view.find('constraints'), 'constraint', {
            'firstItem': 'OBF-start-label', 'firstAttribute': axis,
            'secondItem': view.get('id'), 'secondAttribute': axis, 'id': 'OBF-' + axis})
    tree.write(storyboard, encoding='UTF-8', xml_declaration=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--godot', default=os.environ.get('GODOT', '/Applications/Godot.app/Contents/MacOS/Godot'))
    parser.add_argument('--team-id', default=os.environ.get('APPLE_TEAM_ID', ''), help='Optional Apple development team ID')
    parser.add_argument('--build-dir', type=Path, default=ROOT / 'engine/build-ios')
    parser.add_argument('--output-dir', type=Path, help='New Xcode export directory')
    parser.add_argument('--simulator', action='store_true', help='Export the arm64 iOS Simulator framework')
    parser.add_argument('--no-source-sync', action='store_true', help=argparse.SUPPRESS)
    args = parser.parse_args()
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
    if args.simulator:
        from prepare_ios_simulator import prepare
        library = ROOT / 'workspace/godot-ios-source/bin/libgodot.ios.template_debug.arm64.simulator.a'
        if not library.is_file():
            parser.error('Build the arm64 Godot simulator library first (docs/IOS.md)')
        arm_template = build / 'ios-arm64-simulator.zip'
        if not arm_template.exists() or arm_template.stat().st_mtime < max(library.stat().st_mtime, template.stat().st_mtime):
            prepare(template, library, arm_template)
        template = arm_template
    framework = ROOT / ('godot/bin/ios-simulator' if args.simulator else 'godot/bin') / 'openbfme.ios.template_debug.arm64.framework'
    if not framework.is_dir():
        parser.error('run tools/release/build_ios.sh first')
    output = (args.output_dir.resolve() if args.output_dir else build / 'export') / 'OpenBFME.xcodeproj'
    if output.exists():
        parser.error(f'export already exists: {output}; select a new --build-dir')
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='ios-export-', dir=build) as temp:
        stage = Path(temp) / 'project'
        shutil.copytree(ROOT / 'godot', stage, ignore=shutil.ignore_patterns('.godot', 'bin', 'mods', 'retail'))
        project_config = stage / 'project.godot'
        project_config.write_text(project_config.read_text().replace(
            'run/main_scene="res://scenes/game.tscn"',
            'run/main_scene="res://scenes/ios_startup.tscn"'))
        if args.simulator:
            project_config.write_text(project_config.read_text().replace(
                'renderer/rendering_method.ios="mobile"', 'renderer/rendering_method.ios="gl_compatibility"'))
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
    if not args.no_source_sync:
        install_source_sync(output, ROOT, args.godot)
    project = output / 'project.pbxproj'
    text = project.read_text().replace('buildSettings = {',
        'buildSettings = {\n "ARCHS[sdk=iphonesimulator*]" = arm64;')
    project.write_text(text)
    # Allow opening provider/removable-storage documents in place; never import
    # multi-gigabyte archives into the application's Documents directory.
    info = output.parent / 'OpenBFME/OpenBFME-Info.plist'
    plist = plistlib.loads(info.read_bytes())
    plist['LSSupportsOpeningDocumentsInPlace'] = True
    plist['UIFileSharingEnabled'] = True
    info.write_bytes(plistlib.dumps(plist))
    # Keep launch feedback visible before Godot can render its first frame.
    configure_launch_screen(output.parent / 'OpenBFME/Launch Screen.storyboard')
    print(f'EXPORTED: {output}')


if __name__ == '__main__':
    main()
