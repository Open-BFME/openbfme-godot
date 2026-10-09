"""Lockstep guard for the PATH-1 simulation sources: no direct libm call (sqrt, sin, cos, atan2, pow, floor, ...) may appear in them.
Square roots, floors and trigonometry go through GameLogic/SimMath.h (IEEE-exact sqrt, floorToInt, deterministic sinCosDet), so that
Windows and Linux builds give the same bits. Run: python -m pytest tools/path -q"""
import os
import re

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
AI = os.path.join(REPO, "engine", "src", "GameLogic", "AI")
FILES = sorted(
    [os.path.join(AI, f) for f in os.listdir(AI) if f.endswith((".cpp", ".h"))]
    + [
        os.path.join(REPO, "engine", "src", "GameLogic", "Map", "TerrainPathfindSource.cpp"),
        os.path.join(REPO, "engine", "src", "GameClient", "MapPathfindObjects.cpp"),
    ]
)
LIBM = re.compile(r"(?<![\w.])(?:std::)?(sqrt|sqrtf|sin|cos|tan|asin|acos|atan|atan2|pow|exp|log|log10|hypot|fmod|floor|ceil|round|trunc|cbrt)f?\s*\(")


def code_lines(path):
    text = open(path, encoding="utf-8").read()
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    for n, line in enumerate(text.splitlines(), 1):
        line = re.sub(r"//.*", "", line)
        line = re.sub(r'"(?:[^"\\]|\\.)*"', '""', line)
        yield n, line


def test_the_scan_covers_the_pathfinder_sources():
    names = {os.path.basename(f) for f in FILES}
    assert {"AIPathfindSearch.cpp", "AIPathfindPath.cpp", "AIMove.cpp", "AIPathfindFootprints.cpp", "TerrainPathfindSource.cpp"} <= names


def test_no_direct_libm_in_the_simulation_sources():
    hits = []
    for f in FILES:
        for n, line in code_lines(f):
            m = LIBM.search(line)
            if m:
                hits.append("%s:%d: %s" % (os.path.relpath(f, REPO), n, line.strip()))
    assert not hits, "direct libm calls (use SimMath):\n" + "\n".join(hits)


def test_the_scan_pattern_finds_what_it_should():
    assert LIBM.search("x = std::sqrt(a);")
    assert LIBM.search("int i = (int)std::floor(f);")
    assert LIBM.search("c = cos(a)")
    assert not LIBM.search("SimMath::cosDet(a)")
    assert not LIBM.search("length2d(a, b)")
    assert not LIBM.search("m_lookAhead(sqrtValue)")
