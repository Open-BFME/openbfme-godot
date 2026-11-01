#!/usr/bin/env python3
"""Compare actual MultiMesh bytes/counts/order and palettes against a public baseline.

First configure/build the uninstrumented extension in a NEW output directory with
-DOPENBFME_GODOT_BIN=<private output>, at most -j2. This runner NEVER invokes Ninja:
it snapshots that build's link inputs, then recompiles only the instancer in scratch.
Both variants receive identical test-only fixture transport and resource diagnostics.
The baseline instancer is read directly from --baseline-ref, without algorithm edits.
Requires verified ROTWK_INSTALL/BFME2_INSTALL mounts for normal setup/housecolor.ini;
missing environment is SKIP (77), invalid mounts/parses fail. No retail bytes in Git.
"""
import argparse
import concurrent.futures
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]
CPP = 'engine/src/GodotDevice/GodotW3DInstancer.cpp'
HEADER = 'engine/src/GodotDevice/GodotW3DInstancer.h'
SCRIPTS = ['pose_buffer_test', 'pose_buffer_cull_query_test', 'pose_buffer_query_trigger_test']


def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def run(argv, directory, name, env=None, expected=0):
    if env is None:
        env = dict(os.environ, TMPDIR=str(directory / 'tmp'))
        Path(env['TMPDIR']).mkdir(exist_ok=True)
    (directory / (name + '.command.json')).write_text(json.dumps(argv, indent=2))
    result = subprocess.run(argv, cwd=directory, env=env, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True, timeout=300)
    (directory / (name + '.log')).write_text(result.stdout)
    (directory / (name + '.exit')).write_text(str(result.returncode) + '\n')
    if result.returncode != expected or 'SCRIPT ERROR:' in result.stdout:
        raise RuntimeError(f'{name}: exit {result.returncode}\n{result.stdout[-6000:]}')
    return result.stdout


def replace_once(text, old, new):
    if text.count(old) != 1:
        raise RuntimeError(f'expected one instrumentation site: {old[:100]!r}')
    return text.replace(old, new, 1)


def instrument(source, header, adapter, candidate):
    """Observers only: no baseline invalidation, pose, palette or buffer payload edits."""
    header = replace_once(header, 'Dictionary get_model_report(int64_t model) const;',
                          'Dictionary get_model_report(int64_t model) const;\n\tArray get_draw_buffers() const;\n\tVector3 test_bone_position(int64_t instance, int bone) const;')
    header = replace_once(header, 'bool HasBatch = false;', 'bool HasBatch = false;\n\t\tbool BufferWritten = false;')
    source = replace_once(source, 'void W3DInstancer::_bind_methods()\n{', '''void W3DInstancer::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("get_draw_buffers"), &W3DInstancer::get_draw_buffers);
	ClassDB::bind_method(D_METHOD("test_bone_position", "instance", "bone"), &W3DInstancer::test_bone_position);
	ClassDB::bind_method(D_METHOD("set_instance_fogged", "instance", "fogged"), &W3DInstancer::set_instance_fogged);
	ClassDB::bind_method(D_METHOD("set_instance_sway", "instance", "type"), &W3DInstancer::set_instance_sway);''')
    source = replace_once(source, 'void W3DInstancer::write_buffers()\n{', '''void W3DInstancer::write_buffers()
{
	for (auto &m : Models) for (auto &d : m->Draws) d.BufferWritten = false;''')
    source = replace_once(source, 'd.MM->set_buffer(d.Buffer);', 'd.BufferWritten = true;\n\t\t\td.MM->set_buffer(d.Buffer);')
    getter = '''Vector3 W3DInstancer::test_bone_position(int64_t instance, int bone) const
{
	::Matrix3D xf;
	if (!get_bone_transform_native(instance, bone, xf)) return Vector3();
	const ::Vector3 t = xf.Get_Translation();
	return Vector3(t.X, t.Y, t.Z);
}

Array W3DInstancer::get_draw_buffers() const
{
	Array out;
	for (const auto &m : Models) for (const auto &d : m->Draws)
	{
		Dictionary item;
		item["model"] = toGodot(m->Name);
		item["subobject"] = toGodot(d.Sub->Name);
		item["buffer"] = d.Buffer;
		PackedInt32Array order;
		for (int id : d.Order) order.push_back(id);
		item["order"] = order;
		out.push_back(item);
	}
	return out;
}

'''
    source = replace_once(source, 'Dictionary W3DInstancer::get_stats() const', getter + 'Dictionary W3DInstancer::get_stats() const')
    stats = '''
	int64_t drawWrites = 0, bytes = 0;
	for (const auto &m : Models) for (const auto &d : m->Draws) if (d.BufferWritten)
	{
		++drawWrites;
		bytes += d.Buffer.size() * (int64_t)sizeof(float);
	}
	s["buffer_draw_writes"] = drawWrites;
	s["buffer_bytes"] = bytes;
'''
    if candidate:
        header = replace_once(header, 'bool PoseEvaluated = false;', 'int QueryPoseComparisons = 0;\n\t\tbool PoseEvaluated = false;')
        source = replace_once(source, '\t\tconst bool consumeQueries =', '\t\tm.QueryPoseComparisons = 0;\n\t\tconst bool consumeQueries =')
        source = replace_once(source, '\t\t\t\t\tif (inst.DrawPoseState[k]', '\t\t\t\t\t++m.QueryPoseComparisons;\n\t\t\t\t\tif (inst.DrawPoseState[k]')
        stats += '''
	int64_t pending = 0, comparisons = 0;
	for (const auto &m : Models) { pending += m->QueriedPoseIds.size(); comparisons += m->QueryPoseComparisons; }
	s["pending_query_poses"] = pending;
	s["query_pose_comparisons"] = comparisons;
'''
    source = replace_once(source, '\ts["buffer_writes"] = BufferWrites;', stats + '\ts["buffer_writes"] = BufferWrites;')
    source = '#include "' + str(adapter) + '"\n' + source
    source = replace_once(source, '\t\tAssets.reset(new WW3DAssetManager(*Source));',
                          '\t\tSource = pose_buffer_fixture_source(std::move(Source));\n\t\tAssets.reset(new WW3DAssetManager(*Source));')
    return source, header


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--baseline-ref', required=True, help='verified public main commit')
    parser.add_argument('--scratch', type=Path, required=True, help='new, nonexistent output directory')
    parser.add_argument('--godot', type=Path, required=True)
    parser.add_argument('--negative-controls', action='store_true', help='also compile two deliberately broken candidate variants')
    args = parser.parse_args()
    for name in ['ROTWK_INSTALL', 'BFME2_INSTALL']:
        if not os.environ.get(name):
            print(f'SKIP: set {name} to a verified retail install')
            return 77
    scratch = args.scratch.resolve()
    scratch.mkdir(parents=True, exist_ok=False)
    build = args.build_dir.resolve()
    command = next(c for c in json.loads((build / 'compile_commands.json').read_text())
                   if c['file'].endswith('/GodotW3DInstancer.cpp'))
    if Path(command['file']).resolve() != ROOT / CPP:
        raise RuntimeError('configure the input build from this checkout; foreign cached sources are not accepted')
    compiler = shlex.split(command['command'])[0]
    lines = (build / 'build.ninja').read_text().splitlines()
    index = next(i for i, line in enumerate(lines) if line.startswith('build ') and 'openbfme.linux.template_debug.x86_64.so: CXX_SHARED' in line)
    objects = shlex.split(lines[index].split(': ', 1)[1])[1:]
    objects = objects[:objects.index('|')]
    fields = {}
    for line in lines[index + 1:]:
        if not line.startswith('  '):
            break
        key, value = line.strip().split(' = ', 1)
        fields[key] = value
    inputs = scratch / 'link-inputs'
    inputs.mkdir()
    copied, hashes = {}, {}

    def snapshot(name):
        source = (build / name).resolve()
        if source not in copied:
            target = inputs / (str(len(copied)) + '-' + source.name)
            shutil.copyfile(source, target)
            target.chmod(0o444)
            copied[source] = target
            hashes[str(source)] = sha(target)
        return str(copied[source])

    linked_objects = [snapshot(o) for o in objects if not o.endswith('GodotW3DInstancer.cpp.o')]
    libraries = [snapshot(x) if x.endswith('.a') else x for x in shlex.split(fields['LINK_LIBRARIES'])]
    flags = ['-Wl,--version-script=' + snapshot(x.split('=', 1)[1]) if x.startswith('-Wl,--version-script=') else x
             for x in shlex.split(fields['LINK_FLAGS'])]
    (scratch / 'link-input-hashes.json').write_text(json.dumps(hashes, indent=2))
    current_cpp, current_h = (ROOT / CPP).read_text(), (ROOT / HEADER).read_text()
    baseline = subprocess.check_output(['git', 'rev-parse', args.baseline_ref + '^{commit}'], cwd=ROOT, text=True).strip()
    old_cpp = subprocess.check_output(['git', 'show', baseline + ':' + CPP], cwd=ROOT, text=True)
    old_h = subprocess.check_output(['git', 'show', baseline + ':' + HEADER], cwd=ROOT, text=True)
    adapter = scratch / 'PoseBufferFixtureSource.h'
    shutil.copyfile(ROOT / 'godot/tests/PoseBufferFixtureSource.h', adapter)
    source_hashes = {str(p.relative_to(ROOT)): sha(p) for p in [ROOT / CPP, ROOT / HEADER] + list((ROOT / 'godot/tests').glob('*pose_buffer*')) if p.is_file()}
    source_hashes['godot/tests/PoseBufferFixtureSource.h'] = sha(adapter)
    (scratch / 'source-hashes.json').write_text(json.dumps(source_hashes, indent=2))

    def build_variant(variant):
        folder = scratch / variant
        folder.mkdir()
        source = folder / 'source' / CPP
        header = folder / 'source' / HEADER
        source.parent.mkdir(parents=True)
        code, declarations = (old_cpp, old_h) if variant == 'baseline' else (current_cpp, current_h)
        if variant == 'no-consumed-cache':
            # Reproduce rejected cache lifecycle: a property rewrite consumes A without updating its comparison cache.
            begin = code.index('\t\t\t\t// Cache every consumed pose')
            end = code.index('\t\t\t\tif (!inst.HiddenSubs', begin)
            code = code[:begin] + code[end:]
            marker = '\tPoseEvaluations = (int)StaleScratch.size();'
            update_cache = '''
	for (size_t i : StaleScratch)
	{
		auto &inst = Instances[i];
		const auto &m = *Models[(size_t)inst.Model];
		for (size_t k = 0; k < m.Draws.size(); ++k)
		{
			const size_t bone = (size_t)m.Draws[k].Sub->BoneIndex;
			inst.DrawPoseState[k] = std::make_pair(inst.Pose.IsVisible[bone], inst.Pose.PivotFade[bone]);
		}
	}
'''
            code = replace_once(code, marker, update_cache + marker)
        if variant == 'no-query-consumption':
            code = replace_once(code, 'if (consumeQueries && !m.BuffersDirty)', 'if (false && consumeQueries && !m.BuffersDirty)')
        code, declarations = instrument(code, declarations, adapter, variant != 'baseline')
        source.write_text(code)
        header.write_text(declarations)
        argv = shlex.split(command['command'])
        argv[1:1] = ['-I' + str(folder / 'source/engine/src'), '-I' + str(ROOT / 'engine/src')]
        obj = folder / 'instancer.o'
        argv[argv.index('-o') + 1] = str(obj)
        argv[-1] = str(source)
        run(argv, folder, 'compile')
        library = folder / 'openbfme.linux.template_debug.x86_64.so'
        run([compiler, '-shared', '-o', str(library)] + flags + linked_objects + [str(obj)] + libraries, folder, 'link')
        library.chmod(0o444)
        (folder / 'instrumented-source-hashes.json').write_text(json.dumps({str(p.relative_to(folder)): sha(p) for p in [source, header]}, indent=2))
        print(variant, sha(library), flush=True)
        return library

    names = ['baseline', 'fixed'] + (['no-consumed-cache', 'no-query-consumption'] if args.negative_controls else [])
    with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
        variants = dict(zip(names, pool.map(build_variant, names)))
    fixture = scratch / 'fixture'
    run([compiler, '-std=c++17', '-I' + str(ROOT / 'engine/tests'), '-I' + str(ROOT / 'engine/src'),
         str(ROOT / 'godot/tests/pose_buffer_fixture.cpp'), '-o', str(fixture)], scratch, 'fixture-compile')
    run([str(fixture), str(scratch / 'fixtures')], scratch, 'fixture-generate')
    negative_results = {}
    for variant, library in variants.items():
        folder = scratch / variant
        project = folder / 'project'
        (project / '.godot').mkdir(parents=True)
        (project / 'project.godot').write_text('config_version=5\n[application]\nconfig/name="PoseBufferTest"\nconfig/features=PackedStringArray("4.7")\n[debug]\nfile_logging/enable_file_logging=false\n')
        (project / 'openbfme.gdextension').write_text('[configuration]\nentry_symbol="openbfme_library_init"\ncompatibility_minimum="4.7"\n[libraries]\nlinux.debug.x86_64="' + str(library) + '"\nlinux.release.x86_64="' + str(library) + '"\n')
        (project / '.godot/extension_list.cfg').write_text('res://openbfme.gdextension\n')
        env = dict(os.environ, OPENBFME_W3D_FIXTURE_ROOT=str(scratch / 'fixtures'),
                   XDG_DATA_HOME=str(folder / 'user-data'), XDG_CACHE_HOME=str(folder / 'cache'),
                   XDG_CONFIG_HOME=str(folder / 'config'), XDG_STATE_HOME=str(folder / 'state'),
                   XDG_RUNTIME_DIR=str(folder / 'runtime'), TMPDIR=str(folder / 'tmp'))
        Path(env['TMPDIR']).mkdir(exist_ok=True)
        Path(env['XDG_RUNTIME_DIR']).mkdir(mode=0o700)
        env.pop('DISPLAY', None)
        env.pop('WAYLAND_DISPLAY', None)
        negative = variant.startswith('no-')
        suites = ['pose_buffer_cull_query_test'] if variant == 'no-consumed-cache' else (['pose_buffer_query_trigger_test'] if negative else SCRIPTS)
        for script in suites:
            shutil.copyfile(ROOT / 'godot/tests' / (script + '.gd'), project / (script + '.gd'))
            argv = [str(args.godot.resolve()), '--headless', '--path', str(project), '--script', 'res://' + script + '.gd', '--',
                    '--snapshot=' + str(folder / (script + '.json'))]
            if variant == 'baseline' or negative:
                argv.append('--reference')
            output = run(argv, folder, script, env, expected=1 if negative else 0)
            print(output[-250:], flush=True)
            if negative:
                expected_failure = 'previously cached pose restores membership' if variant == 'no-consumed-cache' else 'queried membership consumed'
                if expected_failure not in output or 'FAIL ' not in output:
                    raise RuntimeError('negative control failed for an unexpected reason')
                negative_results[variant] = {'expected_failure': expected_failure, 'failures': sum(line.startswith('FAIL ') for line in output.splitlines())}
        if not negative:
            retail = (ROOT / 'godot/tests/perf3_cull_test.gd').read_text().replace('RetailIni.find_default_model', 'preload("res://retail_ini.gd").find_default_model')
            (project / 'perf3_cull_test.gd').write_text(retail)
            shutil.copyfile(ROOT / 'godot/scripts/retail_ini.gd', project / 'retail_ini.gd')
            output = run([str(args.godot.resolve()), '--headless', '--path', str(project), '--script', 'res://perf3_cull_test.gd'], folder, 'retail-cull', env)
            print(output[-100:], flush=True)
    counts = {}
    for script in SCRIPTS:
        before = json.loads((scratch / 'baseline' / (script + '.json')).read_text())
        after = json.loads((scratch / 'fixed' / (script + '.json')).read_text())
        if before != after:
            different = [a[0] for a, b in zip(before, after) if a != b]
            raise RuntimeError(f'{script}: exact resource/palette differential failed: {different}')
        counts[script] = len(after)
    submissions = {v: {row[0]: row[1:] for row in json.loads((scratch / v / 'pose_buffer_test.json.submissions.json').read_text())} for v in ['baseline', 'fixed']}
    for label, before, after in [('clock_bone_only', [2, 5, 1280], [1, 1, 256]),
                                 ('army_stationary_bones', [1, 2, 12800], [0, 0, 0]),
                                 ('army_bones_spare_capacity', [1, 2, 19200], [0, 0, 0])]:
        if submissions['baseline'][label] != before or submissions['fixed'][label] != after:
            raise RuntimeError(f'unexpected actual submissions for {label}')
    for name, digest in hashes.items():
        if sha(Path(name)) != digest:
            raise RuntimeError(f'input build changed during validation: {name}')
    result = {'baseline_ref': baseline, 'baseline_instancer_sha256': hashlib.sha256(old_cpp.encode()).hexdigest(),
              'baseline_header_sha256': hashlib.sha256(old_h.encode()).hexdigest(), 'identical_snapshots': counts, 'negative_controls': negative_results,
              'artifacts': {v: {'path': str(p), 'sha256': sha(p)} for v, p in variants.items()},
              'note': 'Diagnostic relinks reuse the explicitly hashed input build dependencies; only the instancer is rebuilt. Headless dummy CPU/API evidence, no GPU or timing acceptance.'}
    (scratch / 'result.json').write_text(json.dumps(result, indent=2))
    print('PASS', json.dumps(counts), flush=True)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
