#!/usr/bin/env python3
"""Run grayTextureFor with real Godot CPU Images and a recording texture boundary.

Manual macOS fixture: reuse only an existing godot-cpp bindings build, compile a
small isolated extension, and never replace/build the game's library. No retail
files, window or GPU. --source-ref supplies an executable negative control.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
SOURCE = Path('engine/src/GodotDevice/GodotInGameHud.cpp')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bindings-build', type=Path, required=True)
    parser.add_argument('--scratch', type=Path, required=True, help='new nonexistent directory')
    parser.add_argument('--godot', type=Path, required=True)
    parser.add_argument('--source-ref', help='git ref to test instead of working source')
    args = parser.parse_args()
    if sys.platform != 'darwin':
        parser.error('this native fixture currently supports macOS only')
    build, scratch = args.bindings_build.resolve(), args.scratch.resolve()
    source = (subprocess.check_output(['git', 'show', f'{args.source_ref}:{SOURCE.as_posix()}'], cwd=ROOT, text=True)
              if args.source_ref else (ROOT / SOURCE).read_text())
    # Extract the whole unchanged production function body, not a rewritten copy.
    start = source.index('Ref<ImageTexture> HudDevice::grayTextureFor(')
    end = source.index('\nRef<Image> HudDevice::imageFor(', start)
    scratch.mkdir(parents=True, exist_ok=False)
    (scratch / 'GrayUnderTest.inc').write_text(source[start:end])
    shutil.copyfile(Path(__file__).with_name('hud_grayscale_probe.cpp'), scratch / 'probe.cpp')
    env = {k: v for k, v in os.environ.items() if k not in ('BFME1_INSTALL', 'BFME2_INSTALL', 'ROTWK_INSTALL')}
    for key in ('HOME', 'XDG_DATA_HOME', 'XDG_CONFIG_HOME', 'XDG_CACHE_HOME', 'OPENBFME_GAME_USER_DIR'):
        directory = scratch / key.lower()
        directory.mkdir()
        env[key] = str(directory)

    def run(command, name):
        (scratch / f'{name}.command.json').write_text(json.dumps(command, indent=2))
        result = subprocess.run(command, cwd=build, env=env, capture_output=True, text=True, timeout=180)
        output = result.stdout + result.stderr
        (scratch / f'{name}.log').write_text(output)
        if result.returncode or 'SCRIPT ERROR:' in output or 'ERROR:' in output:
            raise RuntimeError(f'{name}: exit={result.returncode}\n{output[-4000:]}')
        return output

    entry = next(c for c in json.loads((build / 'compile_commands.json').read_text())
                 if c['file'].endswith('/GodotInGameHud.cpp'))
    command = shlex.split(entry['command'])
    command[command.index('-c') + 1] = str(scratch / 'probe.cpp')
    command[command.index('-o') + 1] = str(scratch / 'probe.o')
    run(command, 'compile')
    arch = command[command.index('-arch') + 1]
    archives = list((build / 'bin').glob(f'libgodot-cpp.*.{arch}.a'))
    if len(archives) != 1:
        raise RuntimeError('expected one matching godot-cpp bindings archive')
    library = scratch / 'probe.dylib'
    target = [arg for arg in command if arg.startswith('-mmacosx-version-min=')]
    run([command[0], '-dynamiclib', '-arch', arch, *target, '-o', str(library), str(scratch / 'probe.o'), str(archives[0])], 'link')
    project = scratch / 'project'
    (project / '.godot').mkdir(parents=True)
    (project / '.godot/extension_list.cfg').write_text('res://probe.gdextension\n')
    (project / 'project.godot').write_text('config_version=5\n[application]\nconfig/name="HUD Grayscale Test"\n[debug]\nfile_logging/enable_file_logging=false\nfile_logging/enable_file_logging.pc=false\n')
    (project / 'probe.gdextension').write_text('[configuration]\nentry_symbol="hud_grayscale_init"\ncompatibility_minimum="4.7"\n[libraries]\nmacos="' + library.as_posix() + '"\n')
    (project / 'test.gd').write_text('extends SceneTree\nfunc _initialize():\n var checks: Dictionary = ClassDB.class_call_static("HudGrayscaleProbe", "run")\n print("GRAYSCALE RESULT ", JSON.stringify(checks))\n quit()\n')
    output = run([str(args.godot.resolve()), '--headless', '--path', str(project), '--script', 'res://test.gd'], 'run')
    checks = json.loads(next(line.removeprefix('GRAYSCALE RESULT ') for line in output.splitlines() if line.startswith('GRAYSCALE RESULT ')))
    (scratch / 'result.json').write_text(json.dumps({'checks': checks, 'source_ref': args.source_ref,
        'function_sha256': hashlib.sha256(source[start:end].encode()).hexdigest()}, indent=2))
    print(json.dumps(checks, indent=2))
    if not checks or not all(checks.values()):
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
