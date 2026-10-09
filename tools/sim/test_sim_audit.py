"""Tests of tools/sim/sim_audit.py: the compile-flag audit, the manifest classification, the type-aware AST check on fixture sources, and a real
configure of engine/ whose generated compile_commands.json is audited against the manifest (the coverage check the CI runs: configure first, then audit).

The AST tests need libclang (pip install libclang==18.1.1; OPENBFME_LIBCLANG_PYTHONPATH / OPENBFME_LIBCLANG_LIBRARY) and skip LOUDLY without it
(stop S-190). The configure tests need cmake and skip loudly without it."""
import json
import os
import shutil
import subprocess
import sys
import tempfile

import pytest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sim_audit  # noqa: E402

REPO = sim_audit.REPO
GNU = {"compiler_family": "gnu", "x86_32": False, "required_flags": ["-fno-fast-math", "-ffp-contract=off", "-frounding-math"], "sources": []}
GNU32 = dict(GNU, x86_32=True, required_flags=GNU["required_flags"] + ["-msse2", "-mfpmath=sse"])
MSVC = {"compiler_family": "msvc", "x86_32": False, "required_flags": ["/fp:strict"], "sources": []}
MSVC32 = dict(MSVC, x86_32=True, required_flags=["/fp:strict", "/arch:SSE2"])
GOOD = ["/usr/bin/c++", "-O3", "-fno-fast-math", "-ffp-contract=off", "-frounding-math", "-c", "a.cpp"]


# ---- the flag audit ---------------------------------------------------------------------------------------------------------------------------
def test_a_command_with_the_contract_passes():
    assert sim_audit.check_flags(GOOD, GNU) == []
    assert sim_audit.check_flags(["cl", "/O2", "/fp:strict", "/c", "a.cpp"], MSVC) == []
    assert sim_audit.check_flags(GOOD + ["-msse2", "-mfpmath=sse"], GNU32) == []
    assert sim_audit.check_flags(["cl", "/fp:strict", "/arch:SSE2"], MSVC32) == []


@pytest.mark.parametrize("flag", ["-fno-fast-math", "-ffp-contract=off", "-frounding-math"])
def test_a_missing_required_flag_is_reported(flag):
    args = [a for a in GOOD if a != flag]
    problems = sim_audit.check_flags(args, GNU)
    assert any("missing required flag " + flag in p for p in problems), problems


@pytest.mark.parametrize("bad", ["-ffast-math", "-Ofast", "-funsafe-math-optimizations", "-fassociative-math", "-freciprocal-math", "-ffinite-math-only",
                                  "-fno-signed-zeros", "-fno-trapping-math", "-fno-rounding-math", "-mfpmath=387", "-fexcess-precision=fast", "-flto",
                                  "-flto=auto", "-fuse-linker-plugin", "-ffp-contract=fast", "-ffp-contract=on"])
def test_a_conflicting_option_is_reported(bad):
    problems = sim_audit.check_flags(GOOD + [bad], GNU)
    assert problems, bad


def test_the_last_contraction_mode_wins():
    assert sim_audit.check_flags(["c++", "-ffp-contract=fast", "-ffp-contract=off", "-fno-fast-math", "-frounding-math"], GNU)  # fast is still a conflict
    args = ["c++", "-fno-fast-math", "-frounding-math", "-ffp-contract=off", "-ffp-contract=fast"]
    assert any("effective contraction" in p for p in sim_audit.check_flags(args, GNU))
    # a fast-math option after -fno-fast-math would win
    assert any("after -fno-fast-math" in p for p in sim_audit.check_flags(["c++", "-ffp-contract=off", "-frounding-math", "-fno-fast-math", "-ffast-math"], GNU))


def test_32_bit_x86_needs_sse_evaluation():
    assert any("-mfpmath=sse" in p for p in sim_audit.check_flags(GOOD + ["-msse2"], GNU32))
    assert any("/arch:SSE2" in p for p in sim_audit.check_flags(["cl", "/fp:strict"], MSVC32))
    assert any("arch:SSE" in p for p in sim_audit.check_flags(["cl", "/fp:strict", "/arch:SSE"], MSVC))


def test_msvc_modes_and_link_time_code_generation():
    assert any("fp:precise" in p for p in sim_audit.check_flags(["cl", "/fp:strict", "/fp:precise"], MSVC))
    assert any("fp:fast" in p for p in sim_audit.check_flags(["cl", "/fp:fast", "/fp:strict"], MSVC))  # present: a conflicting option
    assert any("link-time" in p for p in sim_audit.check_flags(["cl", "/fp:strict", "/GL"], MSVC))
    assert any("missing required flag /fp:strict" in p for p in sim_audit.check_flags(["cl", "/O2"], MSVC))


# ---- link options ---------------------------------------------------------------------------------------------------------------------------
@pytest.mark.parametrize("bad", ["-ffast-math", "-Ofast", "-ffp-contract=fast", "-funsafe-math-optimizations", "-flto", "-flto=auto"])
def test_a_conflicting_link_option_is_reported(bad):
    assert sim_audit.check_link_flags(["-O3", bad, "-Wl,--as-needed"], GNU), bad
    assert sim_audit.check_link_flags(["-O3", "-Wl,--as-needed", "-pthread"], GNU) == []
    assert sim_audit.check_link_flags(["/LTCG", "/DEBUG"], MSVC)
    assert sim_audit.check_link_flags(["/DEBUG", "/SUBSYSTEM:CONSOLE"], MSVC) == []


def test_the_effective_link_options_of_a_generated_build_are_audited():
    with tempfile.TemporaryDirectory() as tmp:
        with open(os.path.join(tmp, "build.ninja"), "w") as f:
            f.write("build openbfme_tests: CXX_EXECUTABLE_LINKER__openbfme_tests_ a.o\n  LINK_FLAGS = -O3 -ffast-math\n  LINK_LIBRARIES = -lm\n"
                    "build libx.so: CXX_SHARED_LIBRARY_LINKER__x_ b.o\n  LINK_FLAGS = -O3\n")
        problems, checked = sim_audit.audit_link(tmp, GNU)
        assert checked == 2
        assert len(problems) == 1 and "openbfme_tests" in problems[0] and "-ffast-math" in problems[0]


def _entry(rel, extra=()):
    return {"directory": os.path.join(REPO, "build"), "file": os.path.join(sim_audit.ENGINE, rel), "arguments": ["c++"] + list(extra)}


def test_link_libraries_and_response_files_are_part_of_the_audited_link_command():
    """The review: `target_link_libraries(probe PRIVATE -ffast-math)` lands in LINK_LIBRARIES, which the audit did not read (the probe starts with MXCSR 9FC0)."""
    with tempfile.TemporaryDirectory() as tmp:
        with open(os.path.join(tmp, "build.ninja"), "w") as f:
            f.write("build probe: CXX_EXECUTABLE_LINKER__probe_ probe.o\n  LINK_FLAGS = -O3\n  LINK_LIBRARIES = -ffast-math -lm\n  LINK_PATH = -L/x\n"
                    "build viarsp: CXX_EXECUTABLE_LINKER__viarsp_ a.o\n  LINK_FLAGS = -O3\n  LINK_LIBRARIES = @CMakeFiles/viarsp.rsp\n"
                    "build clean: CXX_EXECUTABLE_LINKER__clean_ a.o\n  LINK_FLAGS = -O3\n  LINK_LIBRARIES = -lm -pthread\n")
        os.makedirs(os.path.join(tmp, "CMakeFiles"))
        with open(os.path.join(tmp, "CMakeFiles", "viarsp.rsp"), "w") as f:
            f.write("a.o @CMakeFiles/nested.rsp\n")
        with open(os.path.join(tmp, "CMakeFiles", "nested.rsp"), "w") as f:
            f.write("-lm -ffp-contract=fast\n")
        problems, _ = sim_audit.audit_link(tmp, GNU)
        text = "\n".join(problems)
        assert "probe: conflicting link option -ffast-math (in LINK_LIBRARIES)" in text
        assert "viarsp: conflicting link option -ffp-contract=fast" in text  # a nested response file
        assert "clean" not in text



def test_response_files_are_expanded_to_any_depth_and_unreadable_or_cyclic_ones_fail():
    """Round 5: expansion stopped silently at depth eight, so a ten-file chain hid -ffast-math (MXCSR 9FC0 at start-up) from configure and audit."""
    with tempfile.TemporaryDirectory() as tmp:
        os.makedirs(os.path.join(tmp, "r"))
        for i in range(12):
            with open(os.path.join(tmp, "r", "%d.rsp" % i), "w") as f:
                f.write(("@r/%d.rsp\n" % (i + 1)) if i < 11 else "-lm -ffast-math\n")
        with open(os.path.join(tmp, "r", "loop.rsp"), "w") as f:
            f.write("-lm @r/loop.rsp\n")
        with open(os.path.join(tmp, "build.ninja"), "w") as f:
            f.write("build deep: CXX_EXECUTABLE_LINKER__deep_ a.o\n  LINK_FLAGS = -O3\n  LINK_LIBRARIES = @r/0.rsp\n"
                    "build gone: CXX_EXECUTABLE_LINKER__gone_ a.o\n  LINK_FLAGS = -O3\n  LINK_LIBRARIES = @r/missing.rsp\n"
                    "build loop: CXX_EXECUTABLE_LINKER__loop_ a.o\n  LINK_FLAGS = -O3\n  LINK_LIBRARIES = @r/loop.rsp\n")
        problems, _ = sim_audit.audit_link(tmp, GNU)
        text = "\n".join(problems)
        assert "deep: conflicting link option -ffast-math (in LINK_LIBRARIES)" in text
        assert "gone: unreadable response file" in text
        assert "loop: response file includes itself" in text

PROBE_PROJECT = """cmake_minimum_required(VERSION 3.17)
project(Probe CXX)
include(%s)
add_executable(probe probe.cpp)
%s
openbfme_sim_register_target(probe)
%s
"""


def _probe(link_line, check_line):
    cmake = shutil.which("cmake")
    if not cmake:
        pytest.skip("cmake is not on PATH: the link fixture did NOT run")
    work = os.path.join(REPO, "workspace")
    os.makedirs(work, exist_ok=True)
    src = tempfile.mkdtemp(prefix="sim-probe-", dir=work)
    open(os.path.join(src, "probe.cpp"), "w").write("int main() { return 0; }\n")
    open(os.path.join(src, "CMakeLists.txt"), "w").write(
        PROBE_PROJECT % (os.path.join(REPO, "engine", "cmake", "SimFp.cmake").replace(os.sep, "/"), link_line, check_line))
    build = os.path.join(src, "build")
    result = subprocess.run([cmake, "-S", src, "-B", build, "-G", "Ninja"], capture_output=True, text=True)
    return src, build, result


@pytest.mark.parametrize("link_line", ["target_link_libraries(probe PRIVATE -ffast-math)", "target_link_libraries(probe PUBLIC -ffp-contract=fast)",
                                       "target_link_libraries(probe INTERFACE -Ofast)", "target_link_options(probe PRIVATE -ffast-math)",
                                       "target_link_libraries(probe PRIVATE -flto)"])
def test_the_configure_check_rejects_forbidden_options_given_through_target_link_libraries(link_line):
    src, _build, result = _probe(link_line, "openbfme_sim_check_targets()")
    try:
        assert result.returncode != 0, link_line
        assert "SimFp.cmake" in result.stdout + result.stderr, result.stderr
    finally:
        shutil.rmtree(src, ignore_errors=True)


def test_the_generated_link_command_of_a_probe_that_skipped_the_check_fails_the_audit():
    src, build, result = _probe("target_link_libraries(probe PRIVATE -ffast-math)", "")  # no configure check: only the audit stands between it and the binary
    try:
        assert result.returncode == 0, result.stdout + result.stderr
        problems, checked = sim_audit.audit_link(build, GNU)
        assert checked == 1
        assert any("-ffast-math" in p and "LINK_LIBRARIES" in p for p in problems), problems
    finally:
        shutil.rmtree(src, ignore_errors=True)


# ---- the classification of every translation unit ---------------------------------------------------------------------------------------------
def _policy():
    return json.load(open(sim_audit.POLICY_PATH, encoding="utf-8"))


def test_every_translation_unit_is_in_the_manifest_or_in_the_reviewed_exclusions():
    policy = _policy()
    manifest = dict(GNU, sources=["src/GameLogic/Object/Weapon.cpp", "src/GameLogic/Object/Missing.cpp"])
    entries = [_entry("src/GameLogic/Object/Weapon.cpp"), _entry("src/Common/INI/NewSimulationParser.cpp"), _entry("src/GameClient/MapUtil.cpp"),
               _entry("src/Libraries/WWVegas/WW3D2/htree.cpp"), _entry("src/GameLogic/NewSimulation.cpp")]
    problems, built = sim_audit.audit_manifest(entries, manifest, policy, os.path.join(REPO, "build"))
    text = "\n".join(problems)
    # the review's example: a new INI parser nobody classified
    assert "unclassified source src/Common/INI/NewSimulationParser.cpp" in text
    assert "unclassified source src/GameLogic/NewSimulation.cpp" in text
    assert "unclassified source src/GameClient/MapUtil.cpp" in text  # (MapUtil is in the real manifest, not in this synthetic one)
    assert "manifest source src/GameLogic/Object/Missing.cpp has no compile command" in text
    assert "htree" not in text  # reviewed exclusion: rendering library
    assert "Weapon.cpp has no compile command" not in text


def test_a_source_in_both_lists_and_an_exclusion_without_a_reason_are_errors():
    policy = _policy()
    manifest = dict(GNU, sources=["src/Libraries/WWVegas/WW3D2/htree.cpp"])
    problems, _ = sim_audit.audit_manifest([_entry("src/Libraries/WWVegas/WW3D2/htree.cpp")], manifest, policy, os.path.join(REPO, "build"))
    assert any("also matched by the exclusion" in p for p in problems)
    bad = dict(policy, excluded=[{"glob": "src/Libraries/WWVegas/*", "reason": " "}])
    problems, _ = sim_audit.audit_manifest([], dict(GNU, sources=[]), bad, os.path.join(REPO, "build"))
    assert any("has no reason" in p for p in problems)
    stale = dict(policy, excluded=[{"glob": "src/NoSuchDirectory/*", "reason": "x"}])
    problems, _ = sim_audit.audit_manifest([], dict(GNU, sources=[]), stale, os.path.join(REPO, "build"))
    assert any("matches no source file" in p for p in problems)


def test_a_new_godot_device_file_fails_until_it_is_classified():
    policy = _policy()
    manifest = dict(GNU, sources=["src/GodotDevice/GodotGameWorld.cpp"])
    entries = [_entry("src/GodotDevice/GodotGameWorld.cpp"), _entry("src/GodotDevice/GodotNewThing.cpp"), _entry("src/GodotDevice/GodotMapTerrain.cpp")]
    problems, _ = sim_audit.audit_manifest(entries, manifest, policy, os.path.join(REPO, "build"))
    text = "\n".join(problems)
    assert "unclassified source src/GodotDevice/GodotNewThing.cpp" in text
    assert "GodotMapTerrain" not in text and "GodotGameWorld" not in text


def test_the_policy_is_the_reviewed_list():
    policy = _policy()
    assert set(policy) >= {"excluded", "facade_files", "facade_functions"}
    for item in policy["excluded"]:
        assert item["reason"].strip(), item
    # adding a registered facade implementation (an exemption from the AST check) is a reviewed change of THIS list
    assert policy["facade_files"] == ["src/Common/System/NumericState.cpp", "src/Common/NumericState.h"]
    assert policy["facade_functions"] == ["SimMath::sqrtd", "SimMath::sinCosDet", "SimMath::atan2d", "SimMath::cosd", "SimMath::sind", "SimMath::cosf32",
                                          "SimMath::sinf32", "SimMath::atanDetUnit", "SimMath::acosDet"]
    # GodotDevice: no blanket exclusion, every file is classified on its own; the game world (game-start inputs, object creation, commands) is audited
    assert not any(i["glob"].rstrip("*").rstrip("/") == "src/GodotDevice" or i["glob"] == "src/GodotDevice/*" for i in policy["excluded"])
    assert not any(fnmatch_("src/GodotDevice/GodotGameWorld.cpp", i["glob"]) for i in policy["excluded"])
    for name in ("GodotAptPlayer", "GodotFXPlayer", "GodotLogicClock", "GodotMapObjectBuilder", "GodotMapTerrain", "GodotPathfindView", "GodotRetailFileSystem",
                 "GodotW3DInstancer", "GodotW3DMaterial", "GodotW3DModelBuilder", "register_types"):
        assert any(i["glob"] == "src/GodotDevice/%s.cpp" % name for i in policy["excluded"]), name
    on_disk = sorted(n for n in os.listdir(os.path.join(sim_audit.ENGINE, "src", "GodotDevice")) if n.endswith(".cpp"))
    for n in on_disk:  # every .cpp of the directory is classified: excluded by name, or a manifested simulation-input file (the game world, its hero commands (HERO-1), its garrison commands (GARRISON-1), the new-game message marshalling)
        assert n in ("GodotGameWorld.cpp", "GodotGameWorldHeroes.cpp", "GodotGameWorldStealth.cpp", "GodotGameWorldGarrison.cpp", "GodotGameStart.cpp") or any(i["glob"] == "src/GodotDevice/" + n for i in policy["excluded"]), n
    # the simulation data loaders are simulation, not excluded
    for src in ("src/Common/INI/INI.cpp", "src/Common/Thing/ThingTemplate.cpp", "src/GameLogic/Object/Weapon.cpp"):
        assert not any(fnmatch_(src, i["glob"]) for i in policy["excluded"]), src


def fnmatch_(name, glob):
    import fnmatch
    return fnmatch.fnmatch(name, glob)


# ---- the baseline: stable identities, ratchet only ------------------------------------------------------------------------------------------
def _v(file, kind, func, expr, line=1):
    return (file, line, 1, kind, func, expr)


def test_baseline_identities_ignore_line_numbers_but_not_expressions():
    a = sim_audit.identities([_v("a.cpp", "float-arithmetic", "f", "x * y", 10), _v("a.cpp", "float-arithmetic", "f", "x * y", 20)])
    assert a == {"a.cpp": {"float-arithmetic | f | x * y": 2}}
    moved = sim_audit.identities([_v("a.cpp", "float-arithmetic", "f", "x * y", 99), _v("a.cpp", "float-arithmetic", "f", "x * y", 5)])
    assert moved == a
    problems, stale = sim_audit.compare_baseline(moved, a)
    assert problems == [] and stale == []


def test_swapping_one_violation_for_another_is_caught():
    pinned = sim_audit.identities([_v("a.cpp", "float-arithmetic", "f", "x * y")])
    swapped = sim_audit.identities([_v("a.cpp", "float-arithmetic", "f", "x + y")])  # same file, same kind, same count
    problems, stale = sim_audit.compare_baseline(swapped, pinned)
    assert problems and "x + y" in problems[0]
    assert stale and "x * y" in stale[0]
    # a count-only baseline would have accepted this


def test_a_pin_above_the_current_count_is_stale_and_must_be_lowered():
    pinned = {"a.cpp": {"float-arithmetic | f | x * y": 3}}
    problems, stale = sim_audit.compare_baseline({"a.cpp": {"float-arithmetic | f | x * y": 2}}, pinned)
    assert problems == [] and stale


def test_update_baseline_only_removes_pins():
    pinned = {"a.cpp": {"float-arithmetic | f | x * y": 3, "math-call | g | std::sqrt ( v )": 1}, "b.cpp": {"float-arithmetic | h | p - q": 1}}
    current = {"a.cpp": {"float-arithmetic | f | x * y": 2}}
    lowered = sim_audit.lowered_baseline(current, pinned)
    assert lowered == {"a.cpp": {"float-arithmetic | f | x * y": 2}}
    for worse in ({"a.cpp": {"float-arithmetic | f | x * y": 4}}, {"a.cpp": {"float-arithmetic | f | x * z": 1}}, {"c.cpp": {"math-call | f | std::floor ( a )": 1}}):
        with pytest.raises(ValueError):
            sim_audit.lowered_baseline(worse, pinned)


def test_migrating_the_old_count_baseline_refuses_increases_unless_told_to():
    current = {"a.cpp": {"float-arithmetic | f | x * y": 3}}
    assert sim_audit.migrate_baseline(current, {"a.cpp": {"float-arithmetic": 3}}) == current
    with pytest.raises(ValueError):
        sim_audit.migrate_baseline(current, {"a.cpp": {"float-arithmetic": 2}})
    assert sim_audit.migrate_baseline(current, {"a.cpp": {"float-arithmetic": 2}}, allow_increase=True) == current


def test_the_committed_baseline_is_valid_and_the_weapon_lane_is_clean():
    baseline = sim_audit.load_baseline()  # validates version 2
    for file in baseline["files"]:
        assert os.path.exists(os.path.join(sim_audit.ENGINE, file)), file
    for name in ("Weapon", "WeaponState", "WeaponNugget", "WeaponSet", "Armor", "ArmorSet", "DamageFX", "Damage", "WeaponStores"):
        assert "src/GameLogic/Object/%s.cpp" % name not in baseline["files"]
    for bad in ({}, {"version": 1, "files": {}}, {"version": 2, "files": {"a.cpp": {}}}, {"version": 2, "files": {"a.cpp": {"no separators": 1}}}):
        with pytest.raises(ValueError):
            sim_audit.validate_baseline(bad)


# ---- the AST check on fixtures --------------------------------------------------------------------------------------------------------------
@pytest.fixture(scope="module")
def cindex():
    mod = sim_audit.import_clang()
    if mod is None:
        pytest.skip("libclang is not installed (pip install libclang==18.1.1; OPENBFME_LIBCLANG_PYTHONPATH): the type-aware AST check did NOT run (stop S-190)")
    return mod


FIXTURE = """#include <mathdecl.h>
#include "hdr.h"
float clean(float a, float b) { return a < b ? a : b; }
int ints(int a, int b) { return a * b + a / 3 - 1; }
double widen(float a) { return a; }
float bad_mul(float a, float b) { return a * b; }
double bad_add(double a) { double s = a; s += 1.0; return s; }
int bad_cast(float f) { return (int)f; }
int bad_implicit(float f) { int i = f; return i; }
double bad_call(double x) { return std::sqrt(x); }
void bad_compound_int(int &n, float f) { n += f; }
float bad_neg_ok(float a) { return -a; }
template <typename T> T tmpl(T a, T b) { return a + b; }
template <typename T> int tmplInt(T a) { return 3 + 4; }
namespace Facade { double sqrtd(double x) { return std::sqrt(x); } }
float viaHeader(float a) { return headerMul(a, a); }
"""
HEADER = "inline float headerMul(float a, float b) { return a * b; }\n"
MATHDECL = "namespace std { double sqrt(double); float floor(float); }\n"

MACROS = """#define MUL(a, b) ((a) * (b))
#define HALF_OF_X 0.5f * x
#define ADD(a, b) a + b
#define SCALE_ASSIGN(v, k) v *= k
#define DIV_BY(a, b) ((a) / (b))
#define INT_MUL(a, b) ((a) * (b))
float function_like(float x, float y) { return MUL(x, y); }
float object_like(float x) { return HALF_OF_X; }
float nested(float x, float y) { return ADD(MUL(x, y), 1.0f); }
void assign(float &v) { SCALE_ASSIGN(v, 2.0f); }
double divide(double a, double b) { return DIV_BY(a, b); }
int integers(int a, int b) { return INT_MUL(a, b) + ADD(a, b); }
"""


def _scan(cindex, tmp, source, policy_extra=None, header=HEADER, extra_sys=None):
    src = os.path.join(tmp, "src")
    sysdir = os.path.join(tmp, "sys")
    os.makedirs(src, exist_ok=True)
    os.makedirs(sysdir, exist_ok=True)
    open(os.path.join(src, "fixture.cpp"), "w").write(source)
    open(os.path.join(src, "hdr.h"), "w").write(header)
    open(os.path.join(sysdir, "mathdecl.h"), "w").write(MATHDECL)
    for name, text in (extra_sys or {}).items():
        open(os.path.join(sysdir, name), "w").write(text)
    policy = {"facade_files": [], "facade_functions": ["Facade::sqrtd"]}
    policy.update(policy_extra or {})
    args = ["-x", "c++", "-std=c++17", "-isystem", sysdir, "-I", src]
    return sim_audit.scan_tu(cindex, os.path.join(src, "fixture.cpp"), args, policy, root=src, base=src)


def _by_line(violations):
    lines = {}
    for (file, line, _col, kind, _func, _expr) in violations:
        lines.setdefault((file, line), set()).add(kind)
    return lines


def test_the_ast_check_finds_exactly_the_floating_point_work(cindex):
    with tempfile.TemporaryDirectory() as tmp:
        violations, errors = _scan(cindex, tmp, FIXTURE)
        assert errors == []
        expect = {
            ("fixture.cpp", 6): {"float-arithmetic"},
            ("fixture.cpp", 7): {"float-arithmetic"},
            ("fixture.cpp", 8): {"float-to-int"},
            ("fixture.cpp", 9): {"float-to-int"},
            ("fixture.cpp", 10): {"math-call"},
            ("fixture.cpp", 11): {"float-arithmetic"},  # a compound assignment from a float operand into an int
            ("fixture.cpp", 13): {"dependent-arithmetic"},
            ("hdr.h", 1): {"float-arithmetic"},          # the HEADER is checked too
        }
        assert _by_line(violations) == expect, _by_line(violations)
        # not found: comparison, integer arithmetic, float -> double widening, negation, the registered facade function
        funcs = {v[4] for v in violations}
        assert "bad_mul" in funcs and "tmpl" in funcs and "headerMul" in funcs


def test_arithmetic_inside_macro_expansions_is_found(cindex):
    """The review: function-like and object-like multiplication macros returned zero violations (the operator was recovered from source tokens)."""
    with tempfile.TemporaryDirectory() as tmp:
        violations, errors = _scan(cindex, tmp, MACROS)
        assert errors == []
        found = _by_line(violations)
        for line in (7, 8, 9, 10, 11):
            assert found.get(("fixture.cpp", line)) == {"float-arithmetic"}, (line, found)
        assert ("fixture.cpp", 12) not in found  # integer arithmetic through macros stays allowed
        exprs = [v[5] for v in violations if v[1] == 7]
        assert exprs and "*" in exprs[0]


def _identities(violations):
    return sim_audit.identities(violations)


NESTED = """#define EPSILON 0.0001f
#define INNER(x) ((x) + 1.0f)
#define OUTER(x) (INNER(x) * 2.0f)
#define OUTER_SUB(x) (INNER(x) - 2.0f)
float nested_mul(float x) { return OUTER(x); }
float nested_sub(float x) { return OUTER_SUB(x); }
float object_mul() { return EPSILON * EPSILON; }
float object_div() { return EPSILON / EPSILON; }
float chain(float a, float b, float c) { return a + b + c; }
"""


def test_nested_macro_nodes_sharing_an_expansion_location_are_all_kept(cindex):
    """The review: nested macro nodes were keyed by (file, line, column, kind) and overwrote each other: changing the outer operator changed nothing."""
    with tempfile.TemporaryDirectory() as tmp:
        violations, errors = _scan(cindex, tmp, NESTED)
        assert errors == []
        by_line = {}
        for v in violations:
            by_line.setdefault(v[1], []).append(v)
        assert len(by_line[5]) == 2 and len(by_line[6]) == 2  # inner and outer node of each nested expansion
        assert len(by_line[9]) == 2                           # a + b + c: two nodes that start at the same column
        mul = {v[5] for v in by_line[5]}
        sub = {v[5] for v in by_line[6]}
        assert mul != sub and any("*" in e for e in mul) and any(" - " in e for e in sub)
        # the object-like macro: multiplication and division of EPSILON are different identities
        assert {v[5] for v in by_line[7]} != {v[5] for v in by_line[8]}
        assert any("0.0001f" in v[5] for v in by_line[7])


def test_a_swapped_operator_changes_the_identity_even_with_the_same_count(cindex):
    """The Fast_Slerp case: `WWMATH_EPSILON * WWMATH_EPSILON` replaced by a division gave 58 detections before and after and no baseline difference."""
    with tempfile.TemporaryDirectory() as tmp:
        before, _ = _scan(cindex, tmp, "#define EPS 0.0001f\nfloat f() { return EPS * EPS; }\n")
        after, _ = _scan(cindex, tmp, "#define EPS 0.0001f\nfloat f() { return EPS / EPS; }\n")
        a, b = _identities(before), _identities(after)
        assert sum(map(len, a.values())) == sum(map(len, b.values())) == 1
        assert a != b
        problems, stale = sim_audit.compare_baseline(b, a)
        assert problems and stale


def test_identities_are_complete_a_swap_after_the_old_truncation_point_is_caught(cindex):
    """The review: expressions were cut at 160 characters, so a swap behind that point passed against the pin."""
    with tempfile.TemporaryDirectory() as tmp:
        terms = " + ".join("a%d" % i for i in range(60))
        params = ", ".join("float a%d" % i for i in range(60))
        before, _ = _scan(cindex, tmp, "float f(%s, float t) { return (%s) * t; }\n" % (params, terms))
        after, _ = _scan(cindex, tmp, "float f(%s, float t) { return (%s) / t; }\n" % (params, terms))
        ia, ib = _identities(before), _identities(after)
        assert max(len(i) for v in ia.values() for i in v) > 300  # longer than the display limit
        assert sum(map(len, ia.values())) == sum(map(len, ib.values())) == 60  # 59 additions and the final operation: the SAME count
        problems, stale = sim_audit.compare_baseline(ib, ia)
        assert problems and stale
        assert len(sim_audit.shortened("x" * 500)) == 160  # only the DISPLAY form is shortened


def test_a_clean_translation_unit_has_no_violations(cindex):
    with tempfile.TemporaryDirectory() as tmp:
        clean = "float f(float a, float b) { return a < b ? a : -b; }\nint g(int a) { return a * 3; }\nbool h(float a) { return a != 0; }\n"
        violations, errors = _scan(cindex, tmp, clean)
        assert errors == [] and [v for v in violations if v[0] == "fixture.cpp"] == []



def test_coverage_is_the_parsed_unit_and_its_inclusions_not_the_files_with_nodes(cindex):
    """Round 5: a scanned file that became empty dropped out of the coverage, so its obsolete pins passed and survived updating."""
    with tempfile.TemporaryDirectory() as tmp:
        src = os.path.join(tmp, "src")
        os.makedirs(src)
        open(os.path.join(src, "empty.h"), "w").write("// emptied\n")
        open(os.path.join(src, "unit.cpp"), "w").write('#include "empty.h"\n')
        visited = set()
        violations, errors = sim_audit.scan_tu(cindex, os.path.join(src, "unit.cpp"), ["-x", "c++", "-std=c++17", "-I", src], {"facade_files": [], "facade_functions": []},
                                               root=src, base=src, visited=visited)
        assert errors == [] and violations == []
        assert visited == {"unit.cpp", "empty.h"}
        problems, stale = sim_audit.compare_baseline({}, {"empty.h": {"x*y": 1}}, visited)
        assert problems == [] and len(stale) == 1 and stale[0].startswith("empty.h: pin no longer matches")

def test_registered_facade_files_are_exempt_by_path(cindex):
    with tempfile.TemporaryDirectory() as tmp:
        violations, _ = _scan(cindex, tmp, FIXTURE, {"facade_files": ["fixture.cpp", "hdr.h"]})
        assert violations == []


def test_a_parse_error_in_a_project_file_is_reported(cindex):
    with tempfile.TemporaryDirectory() as tmp:
        _, errors = _scan(cindex, tmp, "float f(float a) { return a * undeclared_name; }\n")
        assert errors and "undeclared_name" in errors[0]


def test_a_parse_error_in_an_external_header_is_reported_too(cindex):
    """The review: a GCC-buildable fixture whose external (system) header fails under Clang returned no audit errors."""
    with tempfile.TemporaryDirectory() as tmp:
        source = '#include <broken.h>\nint f() { return brokenValue(); }\n'
        _, errors = _scan(cindex, tmp, source, extra_sys={"broken.h": "int brokenValue() { return gcc_only_builtin_that_clang_lacks(1); }\n"})
        assert errors, "an unparsable external header must fail the audit"
        assert "gcc_only_builtin_that_clang_lacks" in errors[0] and "broken.h" in errors[0]
        # a unit that parses cleanly has none
        _, errors = _scan(cindex, tmp, "int f() { return 1; }\n")
        assert errors == []


# ---- configure the real tree and audit the generated compile commands ---------------------------------------------------------------------------
def _configure(extra=()):
    cmake = shutil.which("cmake")
    if not cmake:
        pytest.skip("cmake is not on PATH: the configure-time coverage check did NOT run")
    work = os.path.join(REPO, "workspace")
    os.makedirs(work, exist_ok=True)  # under the repository (workspace/ is ignored): the Steam Deck cmake wrapper only sees $HOME
    build = tempfile.mkdtemp(prefix="sim-audit-", dir=work)
    cmd = [cmake, "-S", os.path.join(REPO, "engine"), "-B", build, "-G", "Ninja", "-DOPENBFME_BUILD_GODOT=OFF"] + list(extra)
    result = subprocess.run(cmd, capture_output=True, text=True)
    return build, result


def test_the_real_tree_configures_registers_the_weapon_sources_and_passes_the_flag_audit():
    build, result = _configure()
    try:
        assert result.returncode == 0, result.stdout + result.stderr  # (the union-merged CMakeLists.txt did not configure)
        manifest = json.load(open(os.path.join(build, "sim_manifest.json"), encoding="utf-8"))
        for name in ("WeaponNames", "Object/Weapon", "Object/WeaponNugget", "Object/WeaponSet", "Object/Armor", "Object/ArmorSet", "Object/DamageFX",
                     "Object/Damage", "Object/WeaponStores", "Object/WeaponState"):
            assert "src/GameLogic/%s.cpp" % name in manifest["sources"], name
        assert "src/Common/System/NumericState.cpp" in manifest["sources"]
        entries = json.load(open(os.path.join(build, "compile_commands.json"), encoding="utf-8"))
        policy = json.load(open(sim_audit.POLICY_PATH, encoding="utf-8"))
        assert sim_audit.audit_flags(entries, manifest, build) == []
        problems, built = sim_audit.audit_manifest(entries, manifest, policy, build)
        assert problems == []
        # every weapon source and the Lua library really are compiled with the contract (not only the manifest's claim)
        assert "src/GameLogic/Object/WeaponState.cpp" in built
        assert any(os.path.basename(e["file"]) == "lvm.c" for e in entries)
        assert "<build>/lua-ea/lvm.c" in built and "<build>/generated/EmbeddedData.gen.cpp" in built  # project generated and staged sources stay audited
        # PROD-1 and the skirmish slot logic (the GameInfo the logic consumes at game start) are simulation sources; the FX and GUI shell are excluded with reasons
        for name in ("GameLogic/Module/ProductionUpdate", "GameLogic/GameLogicDispatch", "Common/BuildAssistant", "GameClient/GUI/Skirmish/SkirmishSetup",
                     "GameClient/GUI/Skirmish/SkirmishGameSetup", "GameClient/GUI/Skirmish/IniSkirmishSetupSource"):
            assert "src/%s.cpp" % name in manifest["sources"], name
        for name in ("GameClient/FXList", "GameClient/ParticleSys", "GameClient/GUI/Shell/Shell", "GameClient/GUI/AptScreens/AptSkirmish"):
            assert "src/%s.cpp" % name in built and "src/%s.cpp" % name not in manifest["sources"], name
        # the data loaders are simulation sources
        for name in ("Common/INI/INI", "Common/INI/INIFieldParsers", "Common/Thing/ThingTemplate", "Common/Thing/ThingFactory"):
            assert "src/%s.cpp" % name in manifest["sources"], name
    finally:
        shutil.rmtree(build, ignore_errors=True)


GODOT_CPP = os.path.join(REPO, "engine", "thirdparty", "godot-cpp", "CMakeLists.txt")


def test_the_godot_enabled_configuration_audits_cleanly_and_skips_the_generated_bindings():
    """The review: the Godot build's audit reported 3168 spurious missing-flag findings for godot-cpp's generated gen/ sources."""
    if not os.path.exists(GODOT_CPP):
        pytest.skip("engine/thirdparty/godot-cpp is not initialised: the Godot-enabled audit configuration did NOT run")
    build, result = _configure(["-DOPENBFME_BUILD_GODOT=ON"])
    try:
        assert result.returncode == 0, result.stdout + result.stderr
        manifest = json.load(open(os.path.join(build, "sim_manifest.json"), encoding="utf-8"))
        entries = json.load(open(os.path.join(build, "compile_commands.json"), encoding="utf-8"))
        policy = json.load(open(sim_audit.POLICY_PATH, encoding="utf-8"))
        gen = [e for e in entries if os.sep + "thirdparty" + os.sep + "godot-cpp" + os.sep in os.path.join(e.get("directory", ""), e["file"])]
        assert gen, "the Godot configuration should contain godot-cpp compile commands"
        assert sim_audit.audit_flags(entries, manifest, build) == []
        problems, built = sim_audit.audit_manifest(entries, manifest, policy, build)
        assert problems == []
        # the game world (game-start inputs, object creation, commands) is a simulation-input source and audited; the rest of the directory is classified per file
        assert "src/GodotDevice/GodotGameWorld.cpp" in built and "src/GodotDevice/GodotGameWorld.cpp" in manifest["sources"]
        assert "src/GodotDevice/GodotMapTerrain.cpp" in built and "src/GodotDevice/GodotMapTerrain.cpp" not in manifest["sources"]
        projects = [p for p, _e in sim_audit.project_entries(entries, build) if "godot-cpp" in p]
        assert projects == []
    finally:
        shutil.rmtree(build, ignore_errors=True)


@pytest.mark.parametrize("override", ["-DCMAKE_EXE_LINKER_FLAGS=-ffast-math", "-DCMAKE_SHARED_LINKER_FLAGS=-ffast-math", "-DCMAKE_MODULE_LINKER_FLAGS=-ffast-math",
                                      "-DCMAKE_SHARED_LINKER_FLAGS=-ffp-contract=fast", "-DCMAKE_EXE_LINKER_FLAGS_RELEASE=-Ofast",
                                      "-DCMAKE_EXE_LINKER_FLAGS=-flto"])
def test_conflicting_linker_flags_fail_at_configure_time(override):
    """The review: linking with -ffast-math changes the start-up MXCSR from 1F80 to 9FC0 (crtfastmath.o), and was accepted."""
    build, result = _configure([override])
    try:
        assert result.returncode != 0, override
        assert "SimFp.cmake" in result.stdout + result.stderr, result.stderr
    finally:
        shutil.rmtree(build, ignore_errors=True)


def test_directory_wide_link_and_compile_options_fail_at_configure_time():
    work = os.path.join(REPO, "workspace")
    os.makedirs(work, exist_ok=True)
    for opt in ("add_link_options(-ffast-math)", "add_compile_options(-ffp-contract=fast)"):
        include = tempfile.NamedTemporaryFile("w", suffix=".cmake", dir=work, delete=False)
        include.write(opt + "\n")
        include.close()
        try:
            build, result = _configure(["-DCMAKE_PROJECT_INCLUDE=" + include.name])
            try:
                assert result.returncode != 0, opt
                assert "SimFp.cmake" in result.stdout + result.stderr, result.stderr
            finally:
                shutil.rmtree(build, ignore_errors=True)
        finally:
            os.unlink(include.name)


@pytest.mark.parametrize("override", ["-DCMAKE_CXX_FLAGS=-ffast-math", "-DCMAKE_CXX_FLAGS=-ffp-contract=fast", "-DCMAKE_C_FLAGS=-Ofast",
                                      "-DCMAKE_CXX_FLAGS=-flto", "-DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON", "-DCMAKE_CXX_FLAGS_RELEASE=-mfpmath=387"])
def test_conflicting_overrides_fail_at_configure_time(override):
    build, result = _configure([override])
    try:
        assert result.returncode != 0, override
        assert "SimFp.cmake" in result.stdout + result.stderr, result.stderr
    finally:
        shutil.rmtree(build, ignore_errors=True)


def test_repin_refuses_a_rising_count_and_allows_a_pure_reexpression():
    old = {"a.cpp": {"float-arithmetic | f | x * y": 2}}
    same = {"a.cpp": {"float-arithmetic | f | (x * y)": 2}}
    assert sim_audit.repin_baseline(same, old) == same
    with pytest.raises(ValueError):
        sim_audit.repin_baseline({"a.cpp": {"float-arithmetic | f | (x * y)": 3}}, old)
    with pytest.raises(ValueError):
        sim_audit.repin_baseline({"new.cpp": {"math-call | f | sqrt(x)": 1}}, old)
    assert sim_audit.repin_baseline({"new.cpp": {"math-call | f | sqrt(x)": 1}}, old, adopt=("new.cpp",))


def test_stale_pins_of_files_this_build_did_not_scan_are_not_judged():
    pinned = {"src/GodotDevice/GodotGameWorld.cpp": {"float-arithmetic | f | (x * y)": 1}}
    problems, stale = sim_audit.compare_baseline({}, pinned, scanned={"src/GameLogic/Weapon.cpp"})
    assert problems == [] and stale == []
    problems, stale = sim_audit.compare_baseline({}, pinned, scanned={"src/GodotDevice/GodotGameWorld.cpp"})
    assert stale
    assert sim_audit.lowered_baseline({}, pinned, scanned={"x.cpp"}) == pinned


def test_the_tool_stops_are_registered_in_docs_stops_md():
    stops = open(os.path.join(REPO, "docs", "STOPS.md"), encoding="utf-8").read()
    for sid in sim_audit.STOPS:
        assert "| %s |" % sid in stops, sid
    for sid in ("S-230", "S-231", "S-232", "S-233", "S-234"):
        assert "| %s |" % sid in stops, sid


def test_stops_md_rows_are_unique_and_sorted():
    import re
    ids = re.findall(r"^\| (S-\d+) ", open(os.path.join(REPO, "docs", "STOPS.md"), encoding="utf-8").read(), flags=re.M)
    assert len(ids) == len(set(ids)), "duplicate stop id"
    assert ids == sorted(ids), "stop rows out of order"
