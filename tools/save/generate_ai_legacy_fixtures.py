#!/usr/bin/env python3
"""Regenerate synthetic AI save fixtures with historical snapshot writers.

Requires the repository's full local history and an already built native Ninja
engine/build. Does not download assets or use the current snapshot writer for
legacy layouts. v3 uses v4's historical writer with its current version set to 3;
its existing version branches omit exactly the v4 additions.
"""
import hashlib
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / "engine/build"
SOURCES = [
    (1, "f9e43bd3aa"), (2, "09648916d2"), (3, "cf2073caa9"),
    (4, "cf2073caa9"), (5, "5b5cab5d63"), (6, "b90a7d7e64"),
    (7, "465f8a39e0"), (8, "c4e4abfb44"), (9, "96fcda87a9"),
]
SNAPSHOT = "engine/src/GameLogic/SkirmishAI/SkirmishAISnapshot.cpp"


def output(args, **kwargs):
    return subprocess.check_output(args, text=True, **kwargs)


def main():
    commands = output(["ninja", "-C", str(BUILD), "-t", "commands",
                       "CMakeFiles/openbfme_core.dir/src/GameLogic/SkirmishAI/SkirmishAISnapshot.cpp.o"])
    compile_args = shlex.split(commands.splitlines()[-1])
    # Keep the configured compiler/numeric/architecture/include flags, replacing
    # only dependency-file bookkeeping, source and output paths.
    flags = []
    i = 0
    while i < len(compile_args):
        arg = compile_args[i]
        if arg in ("-MT", "-MF", "-o", "-c"):
            i += 2
        elif arg == "-MD":
            i += 1
        else:
            flags.append(arg)
            i += 1
    link = shlex.split(output(["ninja", "-C", str(BUILD), "-t", "commands", "openbfme_tests"]).splitlines()[-1])
    libraries = link[link.index("libopenbfme_core.a"):]
    if "&&" in libraries:
        libraries = libraries[:libraries.index("&&")]
    pieces = ["// OpenBFME. GPL-3.0. Generated synthetic internal saves, NOT retail bytes.\n",
              "// Regenerate: python3 tools/save/generate_ai_legacy_fixtures.py\n",
              "#pragma once\n#include <string_view>\nnamespace legacy_ai_save {\n",
              "struct Fixture { int version; std::string_view hex; };\ninline constexpr Fixture fixtures[] = {\n"]
    with tempfile.TemporaryDirectory(prefix="ai-legacy-") as temp:
        temp = Path(temp)
        main_source = temp / "main.cpp"
        main_source.write_text('#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN\n#include "doctest.h"\n'
            '#include "LegacyAISaveTestUtil.h"\n#include <fstream>\n'
            'TEST_CASE("generate synthetic legacy AI save") { legacy_ai_save::Scenario s; '
            'XferSave x; x.beginBlock(); s.manager().xfer(x); x.endBlock(); '
            'std::ofstream f("legacy.bin", std::ios::binary); '
            'f.write((const char*)x.data().data(), x.data().size()); }\n')
        includes = ["-I" + str(ROOT / "engine/tests"), "-I" + str(ROOT / "engine/thirdparty/doctest")]
        main_object = temp / "main.o"
        subprocess.run(flags + includes + ["-c", str(main_source), "-o", str(main_object)], cwd=BUILD, check=True)
        for version, commit in SOURCES:
            source = output(["git", "show", f"{commit}:{SNAPSHOT}"], cwd=ROOT)
            if version == 3:
                source = source.replace("XferVersion v{ 3, 4 };", "XferVersion v{ 3, 3 };")
            if version < 5:
                # TerrainTrees now references this helper. Supply only that
                # symbol so the linker never pulls in the current AI writer.
                current = (ROOT / SNAPSHOT).read_text()
                start = current.index("void AIPositionSampler::xfer(")
                end = current.index("void AIStatCollector::xfer(", start)
                source += "\n#include <cmath>\n" + current[start:end]
            source_file = temp / "snapshot.cpp"
            source_file.write_text(source)
            source_object = temp / "snapshot.o"
            subprocess.run(flags + ["-c", str(source_file), "-o", str(source_object)], cwd=BUILD, check=True)
            executable = temp / "writer"
            subprocess.run([flags[0], "-o", str(executable), str(main_object), str(source_object)] + libraries, cwd=BUILD, check=True)
            subprocess.run([str(executable)], cwd=temp, check=True, stdout=subprocess.DEVNULL)
            data = (temp / "legacy.bin").read_bytes()
            if data[8] != version:
                raise RuntimeError(f"expected layout {version}, got {data[8]}")
            pieces.append(f"// v{version}: {commit}, {len(data)} bytes, sha256 {hashlib.sha256(data).hexdigest()}\n")
            pieces.append(f"{{{version},\n")
            encoded = data.hex()
            pieces.extend(f'"{encoded[i:i+120]}"\n' for i in range(0, len(encoded), 120))
            pieces.append("},\n")
            print(f"generated v{version} ({len(data)} bytes)", flush=True)
    pieces.append("};\n}\n")
    (ROOT / "engine/tests/LegacyAISaveFixtures.h").write_text("".join(pieces))


if __name__ == "__main__":
    main()
