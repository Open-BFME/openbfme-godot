"""Focused end-to-end contract for the bounded worktree cleanup tool."""

from __future__ import annotations

import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "tools" / "audit-worktrees.ps1"
POWERSHELL = Path(os.environ.get("SystemRoot", r"C:\Windows")) / "System32/WindowsPowerShell/v1.0/powershell.exe"


def run(*argv: object, cwd: Path, check: bool = True) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(
        [str(value) for value in argv], cwd=cwd, stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, encoding="utf-8",
    )
    if check and result.returncode != 0:
        raise AssertionError(result.stderr or result.stdout)
    return result


def git(root: Path, *args: str) -> subprocess.CompletedProcess[str]:
    return run("git", "-c", "core.hooksPath=NUL", "-C", root, *args, cwd=root)


def tool(root: Path, action: str, *, item_id: str | None = None, check: bool = True):
    report = root / "workspace/logs/P0-REPO-001/worktrees.json"
    argv: list[object] = [
        POWERSHELL, "-NoLogo", "-NoProfile", "-NonInteractive",
        "-ExecutionPolicy", "Bypass", "-File", SCRIPT, action, "-Out", report,
    ]
    if item_id:
        argv.extend(["-Id", item_id])
    return run(*argv, cwd=root, check=check), report


class WorktreeCleanupContract(unittest.TestCase):
    def test_inventory_archive_and_guarded_remove(self) -> None:
        with tempfile.TemporaryDirectory(prefix="openbfme-worktrees-") as temporary:
            root = Path(temporary) / "repo"
            lanes = Path(temporary) / "lanes"
            root.mkdir(); lanes.mkdir()
            git(root, "init", "-b", "main")
            git(root, "config", "user.name", "Fixture")
            git(root, "config", "user.email", "fixture@example.invalid")
            (root / ".gitignore").write_text("workspace/\n", encoding="utf-8")
            (root / "base.txt").write_text("base\n", encoding="utf-8")
            git(root, "add", "--", ".gitignore", "base.txt")
            git(root, "commit", "-m", "base")

            paths = {name: lanes / name for name in ("clean", "dirty", "unique", "ignored", "detached")}
            for name, path in paths.items():
                git(root, "worktree", "add", "-b", name, path, "main")
            (paths["dirty"] / "base.txt").write_text("changed\n", encoding="utf-8")
            (paths["dirty"] / "untracked.txt").write_text("preserve\n", encoding="utf-8")
            (paths["ignored"] / "workspace").mkdir()
            (paths["ignored"] / "workspace/private.bin").write_bytes(b"private")
            (paths["unique"] / "unique.txt").write_text("unique\n", encoding="utf-8")
            git(paths["unique"], "add", "--", "unique.txt")
            git(paths["unique"], "commit", "-m", "unique")
            (paths["detached"] / "detached.txt").write_text("detached\n", encoding="utf-8")
            git(paths["detached"], "add", "--", "detached.txt")
            git(paths["detached"], "commit", "-m", "detached")
            git(paths["detached"], "checkout", "--detach")
            shutil.rmtree(paths["detached"])

            completed, report = tool(root, "inventory")
            self.assertIn("WORKTREE_INVENTORY PASS total=6", completed.stdout)
            inventory = json.loads(report.read_text(encoding="utf-8"))
            by_path = {Path(row["path"]): row for row in inventory["worktrees"]}
            self.assertEqual(by_path[root]["classification"], "main")
            self.assertEqual(by_path[paths["clean"]]["classification"], "removable")
            self.assertEqual(by_path[paths["dirty"]]["classification"], "preserve-dirty")
            self.assertEqual(by_path[paths["unique"]]["classification"], "preserve-unmerged")
            self.assertEqual(by_path[paths["ignored"]]["classification"], "preserve-ignored")
            self.assertEqual(by_path[paths["detached"]]["classification"], "preserve-missing")

            refused, _ = tool(root, "remove", item_id=by_path[root]["id"], check=False)
            self.assertNotEqual(refused.returncode, 0)
            refused, _ = tool(root, "remove", item_id=by_path[paths["dirty"]]["id"], check=False)
            self.assertIn("requires a completed archive", refused.stderr)
            refused, _ = tool(root, "remove", item_id=by_path[paths["ignored"]]["id"], check=False)
            self.assertIn("requires a completed archive", refused.stderr)

            for name in ("dirty", "unique", "ignored", "detached"):
                row = by_path[paths[name]]
                archived, _ = tool(root, "archive", item_id=row["id"])
                self.assertIn("WORKTREE_ARCHIVE PASS", archived.stdout)
                archive = root / "workspace/archive/worktrees" / row["id"]
                self.assertTrue((archive / "complete.json").is_file())
                if name == "dirty":
                    self.assertEqual((archive / "files/untracked.txt").read_text(), "preserve\n")
                    self.assertIn("changed", (archive / "tracked.patch").read_text())
                    (paths[name] / "late.txt").write_text("late\n", encoding="utf-8")
                    refused, _ = tool(root, "remove", item_id=row["id"], check=False)
                    self.assertIn("Archive does not match current worktree state", refused.stderr)
                    (paths[name] / "late.txt").unlink()
                elif name == "ignored":
                    self.assertEqual((archive / "files/workspace/private.bin").read_bytes(), b"private")
                else:
                    self.assertTrue((archive / "history.bundle").is_file())
                    git(root, "bundle", "verify", archive / "history.bundle")
                removed, _ = tool(root, "remove", item_id=row["id"])
                self.assertIn("WORKTREE_REMOVE PASS", removed.stdout)
                self.assertFalse(paths[name].exists())

            clean_id = by_path[paths["clean"]]["id"]
            removed, _ = tool(root, "remove", item_id=clean_id)
            self.assertIn("WORKTREE_REMOVE PASS", removed.stdout)
            self.assertFalse(paths["clean"].exists())


if __name__ == "__main__":
    program = unittest.main(exit=False)
    if program.result.wasSuccessful():
        print("WORKTREE_AUDIT_TESTS PASS")
    else:
        raise SystemExit(1)
