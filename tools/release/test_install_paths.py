#!/usr/bin/env python3
"""Synthetic first-run callbacks, launcher persistence and real argv delivery on Linux.

Run: python3 tools/release/test_install_paths.py --godot /path/to/godot
--first-run-ref REF repeats the UI regressions against an earlier revision (expected to fail).
The first-run service and signed version verifier are doubles; disk BIG checks live in test_install_paths.cpp.
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--godot", default=os.environ.get("GODOT", "godot"))
    parser.add_argument("--first-run-ref")
    args = parser.parse_args()
    if sys.platform != "linux":
        parser.error("the argv fixture is a Linux executable; Windows is cross-compiled separately")
    with tempfile.TemporaryDirectory(prefix="install-paths-") as tmp:
        base = Path(tmp)
        names = [
            "Program Files (x86)/EA Games/The Lord of the Rings, The Rise of the Witch-king",
            "players/Jürgen/Games", "players/Анна/Games", "players/玩家/Games",
            "OneDrive - Example/Документы/Games", "Applications/Game With Spaces.app/Contents/Resources",
            "leading and trailing/ leading folder", "punctuation/EA & Games (test)=one;two",
            "long/" + "/".join(["x" * 45] * 7), "trailing dot. /folder. ",
        ]
        real = base / "symlink target"
        real.mkdir()
        (base / "chosen link").symlink_to(real, target_is_directory=True)
        names.append("chosen link")
        cases = []
        for name in names:
            folder = base / name
            folder.mkdir(parents=True, exist_ok=True)
            second = folder / "Base Game"
            second.mkdir()
            user_dir = folder / "user data"
            user_dir.mkdir()
            (user_dir / "install-paths.cfg").write_text(
                f"ROTWK_INSTALL={folder}\nBFME2_INSTALL={second}\n", encoding="utf-8")
            launcher_dir = folder / "launcher data"
            bundled_dir = folder / "bundle with spaces"
            for exe_dir in [launcher_dir / "versions/v1.2.3/game", bundled_dir]:
                exe_dir.mkdir(parents=True)
                exe = exe_dir / "OpenBFME.x86_64"
                exe.write_text("#!/usr/bin/env python3\nimport json, os, sys\n"
                               "p = os.environ['OPENBFME_PATH_ARGV_OUTPUT']\n"
                               "with open(p + '.tmp', 'w', encoding='utf-8') as f:\n"
                               "    json.dump(sys.argv[1:], f, ensure_ascii=False)\n"
                               "os.replace(p + '.tmp', p)\n", encoding="utf-8")
                exe.chmod(0o755)
            cases.append(dict(folder=str(folder), second=str(second), user_dir=str(user_dir),
                              launcher_dir=str(launcher_dir), bundled_dir=str(bundled_dir), output=str(folder / "argv.json")))
        case_file = base / "cases.json"
        case_file.write_text(json.dumps(cases, ensure_ascii=False), encoding="utf-8")
        env = dict(os.environ, OPENBFME_PATH_CASES=str(case_file),
                   OPENBFME_TEST_OS_NAME="Synthetic", HOME=str(base / "home"),
                   XDG_DATA_HOME=str(base / "xdg"), GODOT_SILENCE_ROOT_WARNING="1")
        env.pop("ROTWK_INSTALL", None)
        env.pop("BFME2_INSTALL", None)
        # Only the screen script is copied: there is no extension, retail policy or real user data in this harness.
        project = base / "first-run"
        project.mkdir()
        (project / "project.godot").write_text(
            '[application]\nconfig/name="Synthetic path test"\n[autoload]\nRelease="*res://release.gd"\n')
        (project / "release.gd").write_text('extends Node\nvar info: RefCounted\nfunc display_line() -> String:\n\treturn "synthetic"\n')
        source = ROOT / "godot/scripts/release/first_run.gd"
        if args.first_run_ref:
            text = subprocess.check_output(["git", "show", f"{args.first_run_ref}:godot/scripts/release/first_run.gd"], cwd=ROOT)
            (project / "first_run.gd").write_bytes(text)
        else:
            shutil.copyfile(source, project / "first_run.gd")
        shutil.copyfile(ROOT / "godot/tests/install_paths_test.gd", project / "test.gd")
        def run(path: Path, script: str) -> int:
            try:
                result = subprocess.run([args.godot, "--headless", "--path", str(path), "--script", script],
                                        env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=30)
            except subprocess.TimeoutExpired as exc:
                print((exc.stdout or b"").decode("utf-8", errors="replace"), flush=True)
                print("PATH FAIL headless test timed out", flush=True)
                return 1
            print(result.stdout, end="")
            if "SCRIPT ERROR" in result.stdout or "Parse Error" in result.stdout:
                return 1
            return result.returncode
        first = run(project, "res://test.gd")
        if args.first_run_ref:
            return first
        launcher = run(ROOT / "launcher", "res://tests/install_paths_test.gd")
        return 1 if first or launcher else 0


if __name__ == "__main__":
    raise SystemExit(main())
