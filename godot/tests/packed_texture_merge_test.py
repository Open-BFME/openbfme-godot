#!/usr/bin/env python3
"""Headless C++ packed-texture oracle and loader benchmark; no retail/GPU needed.

Uses configured macOS/Linux/Windows compile flags and an existing godot-cpp archive.
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


def compile_arguments(entry):
    if 'arguments' in entry:
        return list(entry['arguments'])
    if sys.platform != 'win32':
        return shlex.split(entry['command'])
    # CMake's Windows command uses Windows quoting, not POSIX shell escaping.
    import ctypes
    from ctypes import wintypes
    argc = ctypes.c_int()
    split = ctypes.windll.shell32.CommandLineToArgvW
    split.argtypes = [wintypes.LPCWSTR, ctypes.POINTER(ctypes.c_int)]
    split.restype = ctypes.POINTER(wintypes.LPWSTR)
    argv = split(entry['command'], ctypes.byref(argc))
    if not argv:
        raise RuntimeError('cannot parse compilation command')
    try:
        return [argv[i] for i in range(argc.value)]
    finally:
        free = ctypes.windll.kernel32.LocalFree
        free.argtypes = [ctypes.c_void_p]
        free(argv)


def native_commands(command, scratch, archive, platform):
    """Reuse the configured compiler/target flags; write every output in scratch."""
    if platform not in ('darwin', 'linux', 'win32'):
        raise RuntimeError('supported native hosts: macOS, Linux, Windows')
    command = list(command)
    msvc = command[0].replace('\\', '/').rsplit('/', 1)[-1].lower() in ('cl', 'cl.exe', 'clang-cl', 'clang-cl.exe')
    obj = scratch / ('probe.obj' if msvc else 'probe.o')
    suffix = {'darwin': '.dylib', 'linux': '.so', 'win32': '.dll'}[platform]
    library = scratch / ('probe' + suffix)
    if msvc:
        outputs = [i for i, arg in enumerate(command) if arg.lower().startswith(('/fo', '-fo'))]
        if len(outputs) != 1:
            raise RuntimeError('expected one MSVC object output')
        command[outputs[0]] = '/Fo' + str(obj)
        command = [arg for arg in command if not arg.lower().startswith(('/fd', '-fd'))]
        command += ['/Fd' + str(scratch / 'compile.pdb')]
        sources = [i for i, arg in enumerate(command) if arg.endswith('GodotPackedTexture.cpp')]
        if len(sources) != 1:
            raise RuntimeError('expected one packed loader compile input')
        command[sources[0]] = str(scratch / 'probe.cpp')
        runtime = [arg for arg in command if arg.lower() in ('/md', '/mdd', '/mt', '/mtd')]
        link = [command[0], '/LD', *runtime, str(obj), str(archive), '/link',
                '/OUT:' + str(library), '/IMPLIB:' + str(scratch / 'probe.lib'),
                '/PDB:' + str(scratch / 'probe.pdb')]
    else:
        command[command.index('-c') + 1] = str(scratch / 'probe.cpp')
        command[command.index('-o') + 1] = str(obj)
        # A compile database can contain dependency output flags. Do not write
        # those files back into the configured build when compiling the probe.
        for flag in ('-MF', '-MT', '-MQ'):
            while flag in command:
                i = command.index(flag)
                del command[i:i + 2]
        command = [arg for arg in command if arg not in ('-MD', '-MMD', '-MP')]
        link = list(command)
        link.remove('-c')
        link[link.index(str(scratch / 'probe.cpp'))] = str(obj)
        link[link.index('-o') + 1] = str(library)
        link += [str(archive), '-dynamiclib' if platform == 'darwin' else '-shared']
        if platform == 'linux':
            link.append('-pthread')
    return command, link, library


def bindings_archive(build, platform, explicit=None):
    if explicit:
        if not explicit.is_file():
            raise RuntimeError('bindings library does not exist')
        return explicit.resolve()
    system = {'darwin': 'macos', 'linux': 'linux', 'win32': 'windows'}[platform]
    archives = [p for p in (build / 'bin').glob('*godot-cpp*')
                if system in p.name.split('.') and p.suffix in ('.a', '.lib')]
    if len(archives) != 1:
        raise RuntimeError('expected one godot-cpp archive; select it with --bindings-library')
    return archives[0]


def test_native_commands(tmp_path):
    scratch = tmp_path / 'scratch with spaces'
    archive = tmp_path / 'bindings.a'
    base = ['c++', '-Iheaders with spaces', '-fPIC', '-o', 'old.o', '-c', 'GodotPackedTexture.cpp']
    for platform, suffix, shared in [('darwin', '.dylib', '-dynamiclib'), ('linux', '.so', '-shared'), ('win32', '.dll', '-shared')]:
        flags = ['-arch', 'arm64', '-mmacosx-version-min=13.0'] if platform == 'darwin' else ['--sysroot=/toolchain', '-m64']
        original = base + flags + ['-MMD', '-MF', 'old.d', '-MT', 'old.o']
        compile_cmd, link, library = native_commands(original, scratch, archive, platform)
        assert library == scratch / ('probe' + suffix)
        assert compile_cmd[compile_cmd.index('-o') + 1] == str(scratch / 'probe.o')
        assert compile_cmd[compile_cmd.index('-c') + 1] == str(scratch / 'probe.cpp')
        assert link[link.index('-o') + 1] == str(library)
        assert shared in link and '-c' not in link and str(archive) in link
        assert '-MF' not in compile_cmd and 'old.d' not in compile_cmd and '-MMD' not in compile_cmd
        assert all(flag in link for flag in flags)
        assert ('-pthread' in link) == (platform == 'linux')
        assert original == base + flags + ['-MMD', '-MF', 'old.d', '-MT', 'old.o']
    command = ['C:/Program Files/MSVC/cl.exe', '/nologo', '/MDd', '/FoOld.obj', '/FdOld.pdb', '/c', 'C:/tree/GodotPackedTexture.cpp']
    compile_cmd, link, library = native_commands(command, scratch, tmp_path / 'bindings.lib', 'win32')
    assert '/Fo' + str(scratch / 'probe.obj') in compile_cmd
    assert '/Fd' + str(scratch / 'compile.pdb') in compile_cmd
    assert '/FdOld.pdb' not in compile_cmd and '/FoOld.obj' not in compile_cmd
    assert '/MDd' in link and '/LD' in link and '/OUT:' + str(library) in link
    assert '/IMPLIB:' + str(scratch / 'probe.lib') in link
    assert '/PDB:' + str(scratch / 'probe.pdb') in link
    assert str(scratch / 'probe.cpp') not in link


def test_bindings_archive(tmp_path):
    (tmp_path / 'bin').mkdir()
    archive = tmp_path / 'bin/libgodot-cpp.linux.template_debug.x86_64.a'
    archive.touch()
    assert bindings_archive(tmp_path, 'linux') == archive
    (tmp_path / 'bin/libgodot-cpp.linux.template_release.x86_64.a').touch()
    try:
        bindings_archive(tmp_path, 'linux')
    except RuntimeError as error:
        assert '--bindings-library' in str(error)
    else:
        raise AssertionError('ambiguous archive accepted')
    assert bindings_archive(tmp_path, 'linux', archive) == archive.resolve()
    arguments = ['compiler', 'path with spaces', 'input.cpp']
    assert compile_arguments({'arguments': arguments}) == arguments
    assert compile_arguments({'arguments': arguments}) is not arguments


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
        'ERROR: Packed texture RGBA8 image and mask byte counts differ',
    }
    errors = [line for line in output.splitlines() if line.startswith('ERROR:')]
    unexpected = [line for line in errors if not allow_decoder_errors or line not in decoder_errors]
    if result.returncode or 'SCRIPT ERROR:' in output or 'SHADER ERROR:' in output or unexpected:
        raise RuntimeError(f'{name} exit={result.returncode}\n{output[-4000:]}')
    return output


def validate_mask_errors(output):
    message = 'ERROR: Packed texture RGBA8 image and mask byte counts differ'
    if output.splitlines().count(message) != 2:
        raise RuntimeError('expected exactly two packed-mask size diagnostics')


def test_validate_mask_errors():
    message = 'ERROR: Packed texture RGBA8 image and mask byte counts differ\n'
    validate_mask_errors(message * 2)
    for output in ('', message, message * 3, message.replace('byte counts', 'sizes') * 2):
        try:
            validate_mask_errors(output)
        except RuntimeError:
            continue
        raise AssertionError('missing, extra or altered size diagnostic accepted')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--scratch', type=Path, required=True)
    parser.add_argument('--bindings-source', type=Path, help='godot-cpp source if the configured include paths have moved')
    parser.add_argument('--bindings-library', type=Path, help='matching built godot-cpp archive, required if discovery is ambiguous')
    parser.add_argument('--godot', type=Path, required=True)
    parser.add_argument('--baseline-ref', help='exact pre-change loader revision for comparison')
    args = parser.parse_args()
    if sys.platform not in ('darwin', 'linux', 'win32'):
        parser.error('supported native hosts: macOS, Linux, Windows')
    build, scratch = args.build_dir.resolve(), args.scratch.resolve()
    scratch.mkdir(parents=True, exist_ok=False)
    source = SOURCE.read_text()
    call = 'if (!mergePackedRgba8(image, png))\n\t\t\treturn Ref<godot::Image>();'
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
    env = {k: v for k, v in os.environ.items() if k not in ('BFME1_INSTALL', 'BFME2_INSTALL', 'ROTWK_INSTALL', 'OPENBFME_EDITION')}
    for key in ('HOME', 'USERPROFILE', 'APPDATA', 'LOCALAPPDATA', 'XDG_DATA_HOME', 'XDG_CONFIG_HOME', 'XDG_CACHE_HOME', 'OPENBFME_GAME_USER_DIR'):
        directory = scratch / key.lower()
        directory.mkdir()
        env[key] = str(directory)
    shutil.copyfile(Path(__file__).with_name('packed_texture_merge_probe.cpp'), scratch / 'probe.cpp')
    entry = next(c for c in json.loads((build / 'compile_commands.json').read_text())
                 if c['file'].replace('\\', '/').endswith('/GodotPackedTexture.cpp'))
    command = compile_arguments(entry)
    # Reuse the bindings flags/archive, but always compile this checkout's headers.
    command.insert(1, '-I' + str(ROOT / 'engine/src'))
    if args.bindings_source:
        bindings = args.bindings_source.resolve()
        command[1:1] = ['-I' + str(bindings / 'include'), '-I' + str(bindings / 'gdextension')]
    archive = bindings_archive(build, sys.platform, args.bindings_library)
    command, link, library = native_commands(command, scratch, archive, sys.platform)
    directory = Path(entry['directory'])
    run(command, directory, scratch, 'compile', env)
    run(link, directory, scratch, 'link', env)
    project = scratch / 'project'
    (project / '.godot').mkdir(parents=True)
    (project / '.godot/extension_list.cfg').write_text('res://probe.gdextension\n')
    (project / 'project.godot').write_text('config_version=5\n[application]\nconfig/name="Packed Merge Test"\n[debug]\nfile_logging/enable_file_logging=false\nfile_logging/enable_file_logging.pc=false\n')
    host = {'darwin': 'macos', 'linux': 'linux', 'win32': 'windows'}[sys.platform]
    (project / 'probe.gdextension').write_text('[configuration]\nentry_symbol="packed_merge_init"\ncompatibility_minimum="4.7"\n[libraries]\n' + host + '="' + library.as_posix() + '"\n')
    (project / 'test.gd').write_text('extends SceneTree\nfunc _initialize():\n var r: Dictionary = ClassDB.class_call_static("PackedMergeProbe", "run")\n print("PACKED RESULT ", JSON.stringify(r))\n var ok := true\n for key in r.checks: ok = ok and bool(r.checks[key])\n quit(0 if ok else 1)\n')
    start = datetime.datetime.now(datetime.timezone.utc).isoformat()
    output = run([str(args.godot.resolve()), '--headless', '--path', str(project),
                  '--script', 'res://test.gd'], scratch, scratch, 'run', env, allow_decoder_errors=True)
    validate_mask_errors(output)
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
