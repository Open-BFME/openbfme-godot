#!/usr/bin/env python3
"""Exercise real W3DMaterialFactory ownership in a scratch test extension.

Requires an up-to-date native build from this checkout (macOS/Linux, Ninja).
Compiles only the test/factory translation unit and relinks existing objects;
never invokes a normal build or replaces the game's library. No retail assets
or GPU are required. --baseline-ref HEAD supplies the negative control.
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
MATERIAL = ROOT / 'engine/src/GodotDevice/GodotW3DMaterial.cpp'
HEADER = MATERIAL.with_suffix('.h')
TESTS = Path(__file__).resolve().parent


def run(command, cwd, name, expected=0, records=None, env=None):
    records = records or cwd
    (records / (name + '.command.json')).write_text(json.dumps(command, indent=2))
    result = subprocess.run(command, cwd=cwd, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True, timeout=180, env=env)
    (records / (name + '.log')).write_text(result.stdout)
    if result.returncode != expected or any(marker in result.stdout for marker in ('SCRIPT ERROR:', 'SHADER ERROR:', 'ERROR:')):
        raise RuntimeError(f'{name}: exit {result.returncode}\n{result.stdout[-6000:]}')
    return result.stdout


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--scratch', type=Path, required=True, help='new nonexistent directory')
    parser.add_argument('--godot', type=Path, required=True)
    parser.add_argument('--ninja', type=Path, required=True)
    parser.add_argument('--baseline-ref')
    parser.add_argument('--window', action='store_true', help='also exercise a real rendering device')
    args = parser.parse_args()
    build, scratch = args.build_dir.resolve(), args.scratch.resolve()
    scratch.mkdir(parents=True, exist_ok=False)
    env = {k: v for k, v in os.environ.items() if k not in ('BFME1_INSTALL', 'BFME2_INSTALL', 'ROTWK_INSTALL', 'OPENBFME_EDITION')}
    for key in ('HOME', 'XDG_DATA_HOME', 'XDG_CONFIG_HOME', 'XDG_CACHE_HOME', 'OPENBFME_GAME_USER_DIR'):
        directory = scratch / key.lower()
        directory.mkdir()
        env[key] = str(directory)
    entry = next(c for c in json.loads((build / 'compile_commands.json').read_text())
                 if c['file'].endswith('/GodotW3DMaterial.cpp'))
    if Path(entry['file']).resolve() != MATERIAL:
        raise RuntimeError('build must come from this checkout')
    compile_command = shlex.split(entry['command'])
    compile_command.insert(1, '-I' + str(ROOT / 'engine/src'))
    original_object = (build / compile_command[compile_command.index('-o') + 1]).resolve()
    if original_object.stat().st_mtime_ns < max(MATERIAL.stat().st_mtime_ns, HEADER.stat().st_mtime_ns):
        raise RuntimeError('build the current source/header first; stale objects are not accepted')
    source = MATERIAL.read_text()
    if args.baseline_ref:
        source = subprocess.check_output(['git', 'show', args.baseline_ref + ':' + str(MATERIAL.relative_to(ROOT))], cwd=ROOT, text=True)
        # The old implementation has no process cache metadata to inspect.
        # Every resource identity, text, material and lifetime check still runs.
        compile_command.append('-DSHADER_REUSE_BASELINE')
    original_policy = '''return RenderingServer::get_singleton()->get_current_rendering_driver_name() == "metal"
		? W3DSamplerPolicy::Metal16 : W3DSamplerPolicy::Anisotropic;'''
    if source.count(original_policy) != 1:
        raise RuntimeError('expected one sampler-policy query; test instrumentation refused')
    source = source.replace(original_policy, 'return ::testSamplerPolicy;', 1)
    if not args.baseline_ref:
        insert = 'entries[id] = owner;'
        if source.count(insert) != 1:
            raise RuntimeError('expected one cache insertion; metadata observation refused')
        source = source.replace(insert, insert + '\n\t::testCacheEntryCount = entries.size();', 1)
    (scratch / 'MaterialUnderTest.cpp').write_text(source)
    shutil.copyfile(TESTS / 'w3d_shader_reuse_probe.cpp', scratch / 'probe.cpp')
    compile_command[compile_command.index('-c') + 1] = str(scratch / 'probe.cpp')
    compile_command[compile_command.index('-o') + 1] = str(scratch / 'probe.o')
    run(compile_command, build, 'shader-reuse-compile', records=scratch, env=env)

    # Ninja supplies the configured toolchain and libraries, including platform
    # flags. Refuse shell compound operations beyond CMake's harmless : wrappers.
    commands = subprocess.check_output([str(args.ninja.resolve()), '-t', 'commands', 'openbfme'], cwd=build, text=True)
    link = shlex.split(commands.splitlines()[-1])
    if link[:2] == [':', '&&']: link = link[2:]
    if link[-2:] == ['&&', ':']: link = link[:-2]
    if any(token in ('&&', ';', '|') for token in link):
        raise RuntimeError('unsupported compound link command')
    library_suffix = Path(link[link.index('-o') + 1]).suffix
    library = scratch / ('shader-reuse' + library_suffix)
    link[link.index('-o') + 1] = str(library)
    matches = [i for i, token in enumerate(link) if token.endswith('/GodotW3DMaterial.cpp.o')]
    if len(matches) != 1: raise RuntimeError('expected one material link input')
    link[matches[0]] = str(scratch / 'probe.o')
    for i, token in enumerate(link):
        if token.startswith('-Wl,--version-script='):
            exports = scratch / 'exports.map'
            exports.write_text('{ global: shader_reuse_test_init; local: *; };\n')
            link[i] = '-Wl,--version-script=' + str(exports)
    run(link, build, 'shader-reuse-link', records=scratch, env=env)
    project = scratch / 'project'
    (project / '.godot').mkdir(parents=True)
    (project / '.godot/extension_list.cfg').write_text('res://shader-reuse.gdextension\n')
    (project / 'project.godot').write_text('config_version=5\n[application]\nconfig/name="OpenBFME Shader Reuse Test"\n[debug]\nfile_logging/enable_file_logging=false\nfile_logging/enable_file_logging.pc=false\n')
    (project / 'shader-reuse.gdextension').write_text('[configuration]\nentry_symbol="shader_reuse_test_init"\ncompatibility_minimum="4.7"\n[libraries]\nmacos="' + library.as_posix() + '"\nlinux="' + library.as_posix() + '"\n')
    shutil.copyfile(TESTS / 'w3d_shader_reuse_test.gd', project / 'test.gd')
    shutil.copytree(ROOT / 'godot/shaders', project / 'shaders')
    command = [str(args.godot.resolve())]
    command += ['--resolution', '320x240'] if args.window else ['--headless']
    output = run(command + ['--path', str(project), '--script', 'res://test.gd'], scratch,
                 'run', expected=1 if args.baseline_ref else 0, env=env)
    if args.baseline_ref:
        if 'SHADER REUSE FAIL same permutation across factories shares resource' not in output:
            raise RuntimeError('negative control did not fail the cross-factory assertion')
    elif 'SHADER REUSE PASS' not in output:
        raise RuntimeError('missing pass marker')
    (scratch / 'manifest.json').write_text(json.dumps({'baseline_ref': args.baseline_ref, 'window': args.window,
        'production_source_sha256': hashlib.sha256(MATERIAL.read_bytes()).hexdigest(),
        'header_sha256': hashlib.sha256(HEADER.read_bytes()).hexdigest(),
        'test_only_instrumentation': 'sampler query selects each policy; observe entry count after insertion; no cache algorithm edits'}, indent=2))
    print(output)
    return 0


if __name__ == '__main__':
    sys.exit(main())
