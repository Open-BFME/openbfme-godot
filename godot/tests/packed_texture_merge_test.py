#!/usr/bin/env python3
"""Headless C++ packed-texture oracle and loader benchmark; no retail/GPU needed.

Uses the checkout's configured macOS compile flags and existing godot-cpp archive.
Only a scratch test extension is compiled, never the game or its installed library.
The baseline uses the previous pixel API loop and rereads the JPG. An optional
--baseline-ref uses the exact pre-change loader, adding only an ignored argument
for the test call signature. No retail files or GPU are needed.
"""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'engine/src/GodotDevice/GodotPackedTexture.cpp'
LEGACY = '''for (int y = 0; y < image->get_height(); ++y)
        for (int x = 0; x < image->get_width(); ++x) {
            const Color c = image->get_pixel(x, y), a = png->get_pixel(x, y);
            image->set_pixel(x, y, Color(c.r*a.r, c.g*a.g, c.b*a.b, a.a));
        }'''


def run(command, cwd, records, name, env, allow_decoder_errors=False):
    (records / f'{name}.command.json').write_text(json.dumps(command, indent=2))
    result = subprocess.run(command, cwd=cwd, capture_output=True, text=True, env=env, timeout=180)
    output = result.stdout + result.stderr
    (records / f'{name}.log').write_text(output)
    decoder_errors = {
        'ERROR: Condition "!success" is true. Returning: ERR_FILE_CORRUPT',
        'ERROR: Condition "err" is true. Returning: Ref<Image>()',
        'ERROR: Condition "image.is_null()" is true. Returning: ERR_PARSE_ERROR',
        'ERROR: Condition "buffer_size == 0" is true. Returning: ERR_INVALID_PARAMETER',
        "ERROR: Invalid or unsupported DDS texture file ''.",
        'ERROR: Failed to load DDS image.',
    }
    errors = [line for line in output.splitlines() if line.startswith('ERROR:')]
    unexpected = [line for line in errors if not allow_decoder_errors or line not in decoder_errors]
    if result.returncode or 'SCRIPT ERROR:' in output or 'SHADER ERROR:' in output or unexpected:
        raise RuntimeError(f'{name} exit={result.returncode}\n{output[-4000:]}')
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--scratch', type=Path, required=True)
    parser.add_argument('--bindings-source', type=Path, help='godot-cpp source if the configured include paths have moved')
    parser.add_argument('--godot', type=Path, required=True)
    parser.add_argument('--baseline-ref', help='exact pre-change loader revision for comparison')
    args = parser.parse_args()
    if sys.platform != 'darwin':
        parser.error('this isolated native runner currently supports macOS only')
    build, scratch = args.build_dir.resolve(), args.scratch.resolve()
    scratch.mkdir(parents=True, exist_ok=False)
    source = SOURCE.read_text()
    call = 'mergePackedRgba8(image, png);'
    if source.count(call) != 1:
        raise RuntimeError('expected one bulk merge call; counterfactual refused')
    (scratch / 'Candidate.cpp').write_text(source)
    if args.baseline_ref:
        baseline = subprocess.check_output(['git', 'show', f'{args.baseline_ref}:engine/src/GodotDevice/GodotPackedTexture.cpp'], cwd=ROOT, text=True)
        signature = 'bool *pairedPng)'
        if baseline.count(signature) != 1:
            raise RuntimeError('expected pre-change three-argument loader; refused baseline adaptation')
        baseline = baseline.replace(signature, 'bool *pairedPng, const PackedByteArray *)')
    else:
        read = 'if ((!preloadedJpg && !read(stem + ".jpg", data)) || image->load_jpg_from_buffer(preloadedJpg ? *preloadedJpg : data) != OK)'
        if source.count(read) != 1:
            raise RuntimeError('expected one preloaded JPG branch; counterfactual refused')
        baseline = source.replace(call, LEGACY).replace(read, 'if (!read(stem + ".jpg", data) || image->load_jpg_from_buffer(data) != OK)')
    (scratch / 'Baseline.cpp').write_text(baseline)
    env = {k: v for k, v in os.environ.items() if k not in ('BFME1_INSTALL', 'BFME2_INSTALL', 'ROTWK_INSTALL')}
    for key in ('HOME', 'XDG_DATA_HOME', 'XDG_CONFIG_HOME', 'XDG_CACHE_HOME', 'OPENBFME_GAME_USER_DIR'):
        directory = scratch / key.lower()
        directory.mkdir()
        env[key] = str(directory)
    shutil.copyfile(Path(__file__).with_name('packed_texture_merge_probe.cpp'), scratch / 'probe.cpp')
    entry = next(c for c in json.loads((build / 'compile_commands.json').read_text())
                 if c['file'].endswith('/GodotPackedTexture.cpp'))
    command = shlex.split(entry['command'])
    # Reuse the bindings flags/archive, but always compile this checkout's headers.
    command.insert(1, '-I' + str(ROOT / 'engine/src'))
    if args.bindings_source:
        bindings = args.bindings_source.resolve()
        command[1:1] = ['-I' + str(bindings / 'include'), '-I' + str(bindings / 'gdextension')]
    command[command.index('-c') + 1] = str(scratch / 'probe.cpp')
    command[command.index('-o') + 1] = str(scratch / 'probe.o')
    run(command, build, scratch, 'compile', env)
    arch = command[command.index('-arch') + 1]
    archives = list((build / 'bin').glob(f'libgodot-cpp.*.{arch}.a'))
    if len(archives) != 1:
        raise RuntimeError('expected one matching godot-cpp archive')
    library = scratch / 'probe.dylib'
    target = [arg for arg in command if arg.startswith('-mmacosx-version-min=')]
    run([command[0], '-dynamiclib', '-arch', arch, *target, '-o', str(library),
         str(scratch / 'probe.o'), str(archives[0])], build, scratch, 'link', env)
    project = scratch / 'project'
    (project / '.godot').mkdir(parents=True)
    (project / '.godot/extension_list.cfg').write_text('res://probe.gdextension\n')
    (project / 'project.godot').write_text('config_version=5\n[application]\nconfig/name="Packed Merge Test"\n[debug]\nfile_logging/enable_file_logging=false\nfile_logging/enable_file_logging.pc=false\n')
    (project / 'probe.gdextension').write_text('[configuration]\nentry_symbol="packed_merge_init"\ncompatibility_minimum="4.7"\n[libraries]\nmacos="' + library.as_posix() + '"\n')
    (project / 'test.gd').write_text('extends SceneTree\nfunc _initialize():\n var r: Dictionary = ClassDB.class_call_static("PackedMergeProbe", "run")\n print("PACKED RESULT ", JSON.stringify(r))\n var ok := true\n for key in r.checks: ok = ok and bool(r.checks[key])\n quit(0 if ok else 1)\n')
    start = datetime.datetime.now(datetime.timezone.utc).isoformat()
    output = run([str(args.godot.resolve()), '--headless', '--path', str(project),
                  '--script', 'res://test.gd'], scratch, scratch, 'run', env, allow_decoder_errors=True)
    end = datetime.datetime.now(datetime.timezone.utc).isoformat()
    result = json.loads(next(line.removeprefix('PACKED RESULT ') for line in output.splitlines()
                             if line.startswith('PACKED RESULT ')))
    if not result['checks'] or not all(result['checks'].values()):
        raise RuntimeError('failed oracle checks')
    result.update(start_utc=start, end_utc=end, source_sha256=hashlib.sha256(SOURCE.read_bytes()).hexdigest(), baseline_ref=args.baseline_ref, baseline_sha256=hashlib.sha256(baseline.encode()).hexdigest())
    (scratch / 'result.json').write_text(json.dumps(result, indent=2))
    # Corrupt PNG/JPEG/DDS controls intentionally emit decoder errors in run.log.
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
