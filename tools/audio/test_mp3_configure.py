"""Exercise the real fixture CMake module without compilers or audio tools."""
import hashlib
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys

import pytest

ROOT = Path(__file__).resolve().parents[2]


@pytest.fixture
def project(tmp_path):
    cmake, ninja = shutil.which("cmake"), shutil.which("ninja")
    assert cmake and ninja, "MP3 configure tests require CMake and Ninja"
    source = tmp_path / "source with spaces"
    engine = source / "engine"
    data = engine / "tests/data/audio"
    data.mkdir(parents=True)
    tools = source / "tools/audio"
    tools.mkdir(parents=True)
    shutil.copy2(ROOT / "tools/audio/gen_mp3_fixtures.py", tools)
    shutil.copy2(ROOT / "engine/tests/data/audio/mp3_golden.tsv", data)
    engine.joinpath("CMakeLists.txt").write_text(
        'cmake_minimum_required(VERSION 3.17)\nproject(FixtureCheck NONE)\n'
        'option(OPENBFME_BUILD_TESTS "Build tests" ON)\n'
        'if(OPENBFME_BUILD_TESTS)\n'
        f'include("{(ROOT / "engine/cmake/AudioFixtures.cmake").as_posix()}")\n'
        'add_custom_target(openbfme_tests)\n'
        'endif()\n'
        'file(MAKE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../godot/bin")\n'
        'add_custom_target(openbfme COMMAND "${CMAKE_COMMAND}" -E touch\n'
        ' "${CMAKE_CURRENT_SOURCE_DIR}/../godot/bin/openbfme.windows.template_debug.x86_64.dll"\n'
        ' "${CMAKE_CURRENT_SOURCE_DIR}/../godot/bin/openbfme.windows.template_debug.x86_64.pdb"\n'
        ' "${CMAKE_CURRENT_SOURCE_DIR}/../godot/bin/openbfme.linux.template_debug.x86_64.so")\n'
        'add_custom_target(openbfme_peer)\n')
    build = tmp_path / "build with spaces"
    empty = tmp_path / "empty tools"
    empty.mkdir()
    base = [cmake, "-S", str(engine), "-B", str(build), "-G", "Ninja",
            f"-DCMAKE_MAKE_PROGRAM={ninja}", f"-DPython3_EXECUTABLE={sys.executable}",
            f"-DCMAKE_FIND_ROOT_PATH={empty}", "-DCMAKE_FIND_ROOT_PATH_MODE_PROGRAM=ONLY"]
    payloads = {}
    rows = []
    for row in (ROOT / "engine/tests/data/audio/mp3_fixtures.tsv").read_text().splitlines():
        if row.startswith("#") or not row:
            continue
        name = row.split("\t")[0]
        payload = ("synthetic configure-test payload " + name).encode()
        payloads[name + ".mp3.bin"] = payload
        rows.append(f"{name}\t{len(payload)}\t{hashlib.sha256(payload).hexdigest()}")
    data.joinpath("mp3_fixtures.tsv").write_text("\n".join(rows) + "\n")
    env = {k: v for k, v in os.environ.items() if k != "OPENBFME_AUDIO_FIXTURES"}
    return cmake, base, build, payloads, env


def run(project, *flags):
    return subprocess.run([*project[1], *flags], env=project[4], capture_output=True, text=True)


def populate(path, payloads):
    path.mkdir(parents=True)
    for name, data in payloads.items():
        path.joinpath(name).write_bytes(data)


@pytest.mark.parametrize("location", ["default", "cache", "environment"])
def test_valid_fixtures_need_no_tools_or_disable_flag(project, tmp_path, location):
    path = project[2] / "audio_fixtures" if location == "default" else tmp_path / "copied fixtures"
    populate(path, project[3])
    flags = []
    if location == "cache":
        flags.append(f"-DOPENBFME_AUDIO_FIXTURES={path}")
    elif location == "environment":
        project[4]["OPENBFME_AUDIO_FIXTURES"] = str(path)
    result = run(project, *flags)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "MP3 reference fixtures validated" in result.stdout
    build = subprocess.run([project[0], "--build", str(project[2])],
                           env=project[4], capture_output=True, text=True)
    assert build.returncode == 0, build.stdout + build.stderr
    assert {p.name: p.read_bytes() for p in path.iterdir()} == project[3]


@pytest.mark.parametrize("invalid", [False, True])
def test_no_tools_and_missing_or_invalid_fixtures_warn_once_but_build(project, invalid):
    if invalid:
        path = project[2] / "audio_fixtures"
        populate(path, project[3])
        path.joinpath("mono_44k.mp3.bin").write_bytes(b"invalid")
    result = run(project)
    assert result.returncode == 0, result.stdout + result.stderr
    assert result.stderr.count("CMake Warning") == 1
    assert "five MP3 audio test cases will FAIL" in result.stderr
    assert "Missing MP3 fixture tool: lame" in result.stderr
    assert "Missing MP3 fixture tool: mpg123" in result.stderr
    assert 'gen_mp3_fixtures.py' in result.stderr
    assert '--out' in result.stderr
    build = subprocess.run([project[0], "--build", str(project[2])],
                           env=project[4], capture_output=True, text=True)
    assert build.returncode == 0, build.stdout + build.stderr
    assert "Missing MP3 fixture tool: lame" in project[2].joinpath("audio_fixtures_status.txt").read_text()


@pytest.mark.parametrize("valid", [False, True])
def test_lame_4_is_rejected_only_when_generation_is_needed(project, tmp_path, valid):
    marker = tmp_path / "encoder ran"
    fake = tmp_path / ("lame.bat" if os.name == "nt" else "lame")
    fake.write_text((f'@echo off\necho invoked>"{marker}"\necho LAME version 4.0\n' if os.name == "nt" else
                     f'#!/bin/sh\necho invoked > "{marker}"\necho "LAME version 4.0"\n'))
    fake.chmod(0o755)
    path = project[2] / "audio_fixtures"
    if valid:
        populate(path, project[3])
    result = run(project, f"-DOPENBFME_LAME={fake}")
    if valid:
        assert result.returncode == 0, result.stdout + result.stderr
        assert not marker.exists(), "a verified fixture directory must not probe or run lame 4"
        assert {p.name: p.read_bytes() for p in path.iterdir()} == project[3]
    else:
        assert result.returncode == 0, result.stdout + result.stderr
        assert result.stderr.count("CMake Warning") == 1
        assert "lame 4.0 found, fixtures need 3.100" in result.stderr
        assert "lame 4.0 found, fixtures need 3.100" in project[2].joinpath("audio_fixtures_status.txt").read_text()
        assert not path.exists()


def test_release_only_never_runs_fixture_checks(project):
    result = run(project, "-DOPENBFME_BUILD_TESTS=OFF", "-DCMAKE_DISABLE_FIND_PACKAGE_Python3=TRUE")
    assert result.returncode == 0, result.stdout + result.stderr
    assert "MP3" not in result.stdout + result.stderr
    assert "audio_mp3_fixtures" not in project[2].joinpath("build.ninja").read_text()


def test_valid_fixtures_work_without_python_too(project):
    populate(project[2] / "audio_fixtures", project[3])
    result = run(project, "-DCMAKE_DISABLE_FIND_PACKAGE_Python3=TRUE")
    assert result.returncode == 0, result.stdout + result.stderr
    result = subprocess.run([project[0], "--build", str(project[2])],
                            env=project[4], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr


def test_build_does_not_reject_fixtures_corrupted_after_configure(project):
    path = project[2] / "audio_fixtures"
    populate(path, project[3])
    result = run(project)
    assert result.returncode == 0, result.stdout + result.stderr
    original = path.joinpath("mono_44k.mp3.bin").read_bytes()
    path.joinpath("mono_44k.mp3.bin").write_bytes(bytes([original[0] ^ 1]) + original[1:])
    result = subprocess.run([project[0], "--build", str(project[2])],
                            env=project[4], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr


@pytest.mark.parametrize("argument", ["", "Release"])
def test_msvc_wrapper_configure_command_needs_no_audio_tools(project, argument):
    script = (ROOT / "build.bat").read_text()
    assert 'set "BUILD_TYPE=Release"' in script
    assert '--release-only' not in script
    line = next(line for line in script.splitlines() if line.startswith("cmake -S "))
    source = Path(project[1][2]).parent
    command = line.replace("%ROOT%", source.as_posix() + "/").replace("%BUILD_DIR%", str(project[2]))
    command = command.replace("%BUILD_TYPE%", argument or "Release")
    result = subprocess.run([project[0], "--no-warn-unused-cli", *shlex.split(command)[1:], *project[1][7:]],
                            env=project[4], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
    assert result.stderr.count("CMake Warning") == 1
    assert "Missing MP3 fixture tool: lame" in result.stderr
    build = subprocess.run([project[0], "--build", str(project[2]), "--target", "openbfme", "openbfme_tests"],
                           env=project[4], capture_output=True, text=True)
    assert build.returncode == 0, build.stdout + build.stderr


def test_macos_packaging_configure_needs_no_audio_tools(project, monkeypatch):
    from tools.release import package_macos
    monkeypatch.setattr(package_macos, "REPO", Path(project[1][2]).parent)
    args = package_macos.cmake_configure_args(project[2], project[2] / "extension", "test")
    result = subprocess.run([project[0], "--no-warn-unused-cli", *args[1:], *project[1][7:]],
                            env=project[4], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "MP3" not in result.stdout + result.stderr


@pytest.mark.skipif(os.name == "nt", reason="These release scripts run on Linux hosts")
@pytest.mark.parametrize("platform", ["windows", "linux"])
def test_real_release_scripts_build_without_audio_tools(project, tmp_path, platform):
    """Run the real shell entry points and fixture module; stub only SDKs, compilation and package audits."""
    source = Path(project[1][2]).parent
    release = source / "tools/release"
    release.mkdir(parents=True)
    script = ROOT / f"tools/release/build_{platform}.sh"
    shutil.copy2(script, release)
    text = script.read_text()
    toolchain = source / "engine/cmake/toolchains"
    toolchain.mkdir(parents=True)
    shutil.copy2(ROOT / "engine/cmake/toolchains/windows-x64-llvm-mingw.cmake", toolchain)
    bins = tmp_path / "host tools"
    bins.mkdir()
    for name in ("dirname", "mkdir", "flock", "rm", "awk", "tr"):
        bins.joinpath(name).symlink_to(shutil.which(name))

    def executable(path, body):
        path.write_text(body)
        path.chmod(0o755)

    executable(bins / "git", "#!/bin/sh\nexit 0\n")
    flags = [*project[1][7:], "-DCMAKE_FIND_USE_CMAKE_SYSTEM_PATH=FALSE",
             "-DCMAKE_FIND_USE_SYSTEM_ENVIRONMENT_PATH=FALSE"]
    executable(bins / "cmake", f'#!{sys.executable}\nimport os, sys\n'
               f'flags = [] if sys.argv[1] == "--build" else ["--no-warn-unused-cli", *{flags!r}]\n'
               f'os.execv({project[0]!r}, [{project[0]!r}, *sys.argv[1:], *flags])\n')
    executable(bins / "python3", f'#!{sys.executable}\nimport os, sys\n'
               'name = os.path.basename(sys.argv[1])\n'
               'if name == "elf_glibc.py": print("none")\n'
               'elif name not in ("pdb_tools.py", "audit_package.py"):\n'
               f'    os.execv({sys.executable!r}, [{sys.executable!r}, *sys.argv[1:]])\n')
    env = {**project[4], "PATH": str(bins), "OPENBFME_WIN_DIR": str(project[2].parent / "cross"),
           "OPENBFME_LINUX_DIR": str(project[2]), "OPENBFME_SDK_CACHE": str(tmp_path / "sdk")}
    if platform == "windows":
        tag = re.search(r"^LLVM_MINGW_TAG=(.*)$", text, re.M)[1]
        name = re.search(r"^LLVM_MINGW_NAME=(.*)$", text, re.M)[1].replace("${LLVM_MINGW_TAG}", tag)
        sdk_bin = Path(env["OPENBFME_WIN_DIR"]) / name / "bin"
        sdk_bin.mkdir(parents=True)
        executable(sdk_bin / "x86_64-w64-mingw32-clang++", "#!/bin/sh\nexit 0\n")
    else:
        version = re.search(r"^SDK_RELEASE=(.*)$", text, re.M)[1]
        name = re.search(r"^SDK_NAME=(.*)$", text, re.M)[1]
        sdk = Path(env["OPENBFME_SDK_CACHE"]) / version / name
        sdk.joinpath("bin").mkdir(parents=True)
        sdk.joinpath(".openbfme-ready").touch()
        executable(sdk / "bin/x86_64-godot-linux-gnu-nm", "#!/bin/sh\necho '0 T openbfme_library_init'\n")
    result = subprocess.run(["/bin/bash", str(release / script.name), *(["--no-export"] if platform == "windows" else [])],
                            cwd=source, env=env, capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "BUILT:" in result.stdout
    if platform == "windows":
        assert result.stderr.count("CMake Warning") == 1
        assert "Missing MP3 fixture tool: lame" in result.stderr
    else:
        assert "MP3" not in result.stdout + result.stderr
