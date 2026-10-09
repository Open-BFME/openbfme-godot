"""Textual lockstep guard for the WEAPON-1 simulation sources (the cheap guard that needs no compiler; the real coverage checks are tools/sim/sim_audit.py
and tools/sim/test_sim_audit.py: configure, audit the compile commands against the manifest, type-aware AST scan of sources and headers).

No direct libm call (sqrt, floor, ceil, fabs, sin, cos, pow, ...) and no <cmath> include may appear in the weapon sources: floor / ceil / fabs / sqrt go
through the shared facade (GameLogic/SimMath.h -> Common/NumericState.h) and the float arithmetic through the sse* / pc24*W operations. The list of files
is read from the OPENBFME_SIM_SOURCES manifest in engine/CMakeLists.txt (the WEAPON-1 block), not hard coded; the CMake files must be structurally
sound (balanced parentheses and if / endif: a union-merged file was not)."""
import os
import re

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ENGINE = os.path.join(REPO, "engine")
LIBM = re.compile(r"(?<![\w.])(?:std::)?(sqrt|sqrtf|sin|cos|tan|asin|acos|atan|atan2|pow|exp|log|log10|hypot|fmod|floor|ceil|round|trunc|cbrt|fabs|abs|ldexp|frexp|modf)f?\s*\(")
CMATH = re.compile(r"#\s*include\s*<(cmath|math\.h)>")


def cmake_text():
    return open(os.path.join(ENGINE, "CMakeLists.txt"), encoding="utf-8").read()


def manifest_block(title):
    """The sources of the list(APPEND OPENBFME_SIM_SOURCES ...) block that follows the comment `# ---- <title> sources`."""
    text = cmake_text()
    i = text.index("# ---- %s sources" % title)
    j = text.index("list(APPEND OPENBFME_SIM_SOURCES", i)
    k = text.index(")", j)
    return re.findall(r"(src/\S+\.cpp)", text[j:k])


def weapon_files():
    return [os.path.join(ENGINE, p) for p in manifest_block("WEAPON-1")]


def code_lines(path):
    text = open(path, encoding="utf-8").read()
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    for n, line in enumerate(text.splitlines(), 1):
        line = re.sub(r"//.*", "", line)
        line = re.sub(r'"(?:[^"\\]|\\.)*"', '""', line)
        yield n, line


def test_the_weapon_block_of_the_manifest_lists_all_ten_sources():
    names = sorted(os.path.basename(p) for p in manifest_block("WEAPON-1"))
    assert names == sorted(["WeaponNames.cpp", "Weapon.cpp", "WeaponNugget.cpp", "WeaponSet.cpp", "Armor.cpp", "ArmorSet.cpp", "DamageFX.cpp", "Damage.cpp",
                            "WeaponStores.cpp", "WeaponState.cpp"])
    for p in weapon_files():
        assert os.path.exists(p), p


def test_no_direct_libm_in_the_weapon_sources():
    hits = []
    for f in weapon_files():
        for n, line in code_lines(f):
            if LIBM.search(line) or CMATH.search(line):
                hits.append("%s:%d: %s" % (os.path.relpath(f, REPO), n, line.strip()))
    assert not hits, "direct libm use (route through SimMath / NumericState):\n" + "\n".join(hits)


def test_the_scan_pattern_finds_what_it_should():
    assert LIBM.search("x = std::floor(a);")
    assert LIBM.search("double d = fabs(a)")
    assert CMATH.search("#include <cmath>")
    assert not LIBM.search("NumericState::floorD(a)")
    assert not LIBM.search("m_absoluteValue(x)")


def strip_cmake(text):
    """The text without comments and the contents of quoted arguments (a small scanner: a `#` inside a string is not a comment)."""
    out, i, n = [], 0, len(text)
    while i < n:
        c = text[i]
        if c == "#":
            while i < n and text[i] != "\n":
                i += 1
        elif c == '"':
            i += 1
            while i < n and text[i] != '"':
                i += 2 if text[i] == "\\" else 1
            i += 1
            out.append('""')
        else:
            out.append(c)
            i += 1
    return "".join(out)


def test_the_cmake_files_are_structurally_sound():
    """Balanced parentheses and if/endif/foreach/function nesting in every CMake file of engine/ (a union-merged block left an `if(NOT MSVC)` open)."""
    files = [os.path.join(ENGINE, "CMakeLists.txt")] + [os.path.join(ENGINE, "cmake", n) for n in sorted(os.listdir(os.path.join(ENGINE, "cmake"))) if n.endswith(".cmake")]
    for f in files:
        text = strip_cmake(open(f, encoding="utf-8").read())
        assert text.count("(") == text.count(")"), f
        stack = []
        for m in re.finditer(r"(?im)^\s*(if|foreach|function|macro|while|endif|endforeach|endfunction|endmacro|endwhile)\s*\(", text):
            word = m.group(1).lower()
            if word.startswith("end"):
                assert stack and stack.pop() == word[3:], "%s: unbalanced %s" % (f, word)
            else:
                stack.append(word)
        assert not stack, "%s: unclosed %s" % (f, stack)


def test_the_build_has_one_floating_point_contract_and_no_per_file_lists():
    """The flags live in the openbfme_sim_fp interface target (engine/cmake/SimFp.cmake) and are linked PUBLIC by the core and the Lua library; there
    are no per-file COMPILE_OPTIONS lists that a new source could be missing from. The effective flags of the generated compile commands are
    audited by tools/sim/test_sim_audit.py."""
    cmake = cmake_text()
    sim = open(os.path.join(ENGINE, "cmake", "SimFp.cmake"), encoding="utf-8").read()
    lua = open(os.path.join(ENGINE, "cmake", "Lua.cmake"), encoding="utf-8").read()
    assert "ffp-contract=off" not in cmake and "/fp:strict" not in cmake and "fp-contract" not in lua
    assert "set_source_files_properties" not in cmake
    for needle in ("/fp:strict", "/arch:SSE2", "-fno-fast-math", "-ffp-contract=off", "-frounding-math", "-msse2", "-mfpmath=sse"):
        assert needle in sim, needle
    assert "openbfme_sim_register_target(openbfme_core)" in cmake
    assert "openbfme_sim_register_target(openbfme_lua)" in lua
    assert "target_sources(openbfme_core PRIVATE ${OPENBFME_SIM_SOURCES})" in cmake


def test_compile_commands_are_exported_before_any_subdirectory_is_added():
    """CMAKE_EXPORT_COMPILE_COMMANDS is read when a target is created: set after add_subdirectory(godot-cpp) the third-party targets are missing (and the
    audit's third-party exclusion is never exercised)."""
    cmake = cmake_text()
    assert cmake.index("set(CMAKE_EXPORT_COMPILE_COMMANDS ON)") < cmake.index("add_subdirectory(")
