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


def validate_material_source(entry, material=MATERIAL):
    # Mount aliases can have different resolved paths but identify the same file.
    source = Path(entry['directory']) / entry['file']
    if not source.samefile(material):
        raise RuntimeError('build must come from this checkout')


def test_validate_material_source(tmp_path):
    material = tmp_path / 'GodotW3DMaterial.cpp'
    material.write_text('source')
    alias = tmp_path / 'second-mount' / material.name
    alias.parent.mkdir()
    alias.hardlink_to(material)
    assert alias.resolve() != material.resolve()
    validate_material_source({'directory': str(tmp_path), 'file': str(material)}, material)
    validate_material_source({'directory': str(tmp_path), 'file': str(alias)}, material)
    validate_material_source({'directory': str(alias.parent), 'file': alias.name}, material)
    symlink = tmp_path / 'symlink.cpp'
    symlink.symlink_to(material)
    validate_material_source({'directory': str(tmp_path), 'file': str(symlink)}, material)
    other = tmp_path / 'other-checkout.cpp'
    shutil.copyfile(material, other)
    try:
        validate_material_source({'directory': str(tmp_path), 'file': str(other)}, material)
    except RuntimeError as error:
        assert str(error) == 'build must come from this checkout'
    else:
        raise AssertionError('a separate checkout with identical bytes was accepted')


def extract_link_command(command):
    """Select the compiler operation, never execute CMake's post-build chain."""
    lexer = shlex.shlex(command, posix=True, punctuation_chars=';&|<>')
    lexer.whitespace_split = True
    lexer.commenters = ''
    operations, operation = [], []
    for token in lexer:
        if token and all(c in ';&|<>' for c in token):
            operations.append(operation)
            operation = []
        else:
            operation.append(token)
    operations.append(operation)
    links = [op for op in operations if '-o' in op and
             any(token.endswith('/GodotW3DMaterial.cpp.o') for token in op)]
    if len(links) != 1:
        raise RuntimeError('expected exactly one material link operation')
    return links[0]


def validate_baseline(output):
    expected = {
        'same permutation across factories shares resource',
        'Metal policy shares only its own domain',
        '128 permutation and sampler requests share across factories',
    }
    prefix = 'SHADER REUSE FAIL '
    failed = {line[len(prefix):] for line in output.splitlines() if line.startswith(prefix)}
    if failed != expected:
        raise RuntimeError(f'negative control failures differ: expected {sorted(expected)}, got {sorted(failed)}')


def test_extract_link_command():
    link = ['/usr/bin/c++', '-dynamiclib', 'CMakeFiles/openbfme.dir/src/GodotDevice/GodotW3DMaterial.cpp.o',
            '-o', 'bin with spaces/openbfme.dylib', 'bindings.a']
    command = ': && ' + shlex.join(link)
    for suffix in (' && :', ' && cd build && lipo input.dylib -thin arm64 -output shipped.dylib',
                   ' && objcopy --only-keep-debug shipped.so shipped.so.debug && objcopy --strip-all shipped.so'):
        assert extract_link_command(command + suffix) == link
    for invalid in ('lipo input.dylib -output shipped.dylib', command + ' && ' + shlex.join(link)):
        try:
            extract_link_command(invalid)
        except RuntimeError:
            continue
        raise AssertionError('missing or ambiguous link operation was accepted')


def test_validate_baseline():
    failures = ['same permutation across factories shares resource', 'Metal policy shares only its own domain',
                '128 permutation and sampler requests share across factories']
    output = '\n'.join('SHADER REUSE FAIL ' + name for name in failures) + '\nSHADER REUSE FAIL\n'
    validate_baseline(output)
    for invalid in (output + 'SHADER REUSE FAIL recreated shader preserves exact generated text\n',
                    output + 'SHADER REUSE FAIL factory-only shader released immediately\n',
                    '\n'.join(output.splitlines()[1:])):
        try:
            validate_baseline(invalid)
        except RuntimeError:
            continue
        raise AssertionError('unexpected or missing baseline failure was accepted')


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
    validate_material_source(entry)
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

    # Keep the configured toolchain/libraries, but omit post-build lipo/objcopy
    # operations: they target the installed outputs, not this scratch library.
    commands = subprocess.check_output([str(args.ninja.resolve()), '-t', 'commands', 'openbfme'], cwd=build, text=True)
    link = extract_link_command(commands.splitlines()[-1])
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
        validate_baseline(output)
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
