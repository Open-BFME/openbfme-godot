from __future__ import annotations

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
from types import SimpleNamespace
import unittest
from unittest import mock

from openbfme_importer import bootstrap
from openbfme_importer.big import sha256_file
from openbfme_importer.pipeline import ImportPipeline
from openbfme_importer.tools import directory_tree_sha256


def _fake_blender_tree(state_root: Path) -> tuple[Path, Path]:
    root = state_root / "tools" / "blender-4.2.0-windows-x64"
    root.mkdir(parents=True)
    executable = root / "blender.exe"
    executable.write_bytes(b"pinned blender executable")
    source = root / "4.2" / "scripts" / "modules" / "module.py"
    source.parent.mkdir(parents=True)
    source.write_text("VALUE = 1\n", encoding="utf-8")
    (root / "readme.html").write_text("pinned distribution\n", encoding="utf-8")
    return executable, source


def _fake_plugin_tree(root: Path) -> tuple[Path, Path]:
    plugin = root / "tools" / "OpenSAGE.BlenderPlugin"
    (plugin / ".git").mkdir(parents=True)
    source = plugin / "io_mesh_w3d" / "__init__.py"
    source.parent.mkdir()
    source.write_text("PLUGIN = 1\n", encoding="utf-8")
    updater = plugin / "io_mesh_w3d" / "blender_addon_updater"
    updater.mkdir()
    (updater / ".git").write_text("gitdir: pinned\n", encoding="utf-8")
    return plugin, source


def _exact_plugin_git(
    command: list[str],
    *,
    cwd: Path | None = None,
) -> str:
    if "--show-toplevel" in command:
        # Simulate a well-formed checkout: discovery stops exactly at the
        # directory the caller asked about.
        return str(Path(cwd).resolve())
    del cwd
    if "status" in command:
        return ""
    if "-C" in command:
        return bootstrap.PLUGIN_SUBMODULE_COMMIT
    return bootstrap.PLUGIN_COMMIT


class BlenderToolCacheTests(unittest.TestCase):
    def test_prepare_removes_only_bounded_cache_then_attests_exact_tree(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            state_root = Path(raw) / "state"
            executable, source = _fake_blender_tree(state_root)
            expected_executable = sha256_file(executable)
            expected_tree = directory_tree_sha256(executable.parent)
            cache_root = source.parent / "__pycache__"
            nested = cache_root / "nested"
            nested.mkdir(parents=True)
            (cache_root / "module.cpython-312.pyc").write_bytes(b"generated")
            (nested / "cache.data").write_bytes(b"also generated")
            standalone = source.parent / "legacy.pyo"
            standalone.write_bytes(b"generated")

            with (
                mock.patch.object(bootstrap, "BLENDER_EXE_SHA256", expected_executable),
                mock.patch.object(bootstrap, "BLENDER_TREE_SHA256", expected_tree),
            ):
                self.assertEqual(
                    bootstrap.prepare_blender_portable_tree(state_root),
                    expected_tree,
                )

            self.assertFalse(cache_root.exists())
            self.assertFalse(standalone.exists())
            self.assertEqual(source.read_text(encoding="utf-8"), "VALUE = 1\n")
            self.assertEqual(directory_tree_sha256(executable.parent), expected_tree)

    def test_prepare_fails_closed_on_source_and_executable_mutation(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            state_root = Path(raw) / "state"
            executable, source = _fake_blender_tree(state_root)
            executable_bytes = executable.read_bytes()
            expected_executable = sha256_file(executable)
            expected_tree = directory_tree_sha256(executable.parent)

            with (
                mock.patch.object(bootstrap, "BLENDER_EXE_SHA256", expected_executable),
                mock.patch.object(bootstrap, "BLENDER_TREE_SHA256", expected_tree),
            ):
                source.write_text("VALUE = 2\n", encoding="utf-8")
                with self.assertRaisesRegex(RuntimeError, "portable tree differs"):
                    bootstrap.prepare_blender_portable_tree(state_root)

                source.write_text("VALUE = 1\n", encoding="utf-8")
                executable.write_bytes(executable_bytes + b"tampered")
                with self.assertRaisesRegex(RuntimeError, "executable hash mismatch"):
                    bootstrap.prepare_blender_portable_tree(state_root)

    def test_override_tree_is_attested_but_never_purged(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            base = Path(raw)
            state_root = base / "state"
            override_root = base / "override"
            override_root.mkdir()
            executable = override_root / "blender.exe"
            executable.write_bytes(b"override executable")
            cache = override_root / "__pycache__" / "override.pyc"
            cache.parent.mkdir()
            cache.write_bytes(b"generated")

            with (
                mock.patch.object(
                    bootstrap, "BLENDER_EXE_SHA256", sha256_file(executable)
                ),
                mock.patch.object(bootstrap, "BLENDER_TREE_SHA256", "0" * 64),
            ):
                with self.assertRaisesRegex(RuntimeError, "portable tree differs"):
                    bootstrap.prepare_blender_portable_tree(state_root, executable)

            self.assertEqual(cache.read_bytes(), b"generated")

    def test_purge_refuses_link_without_touching_external_target(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            base = Path(raw)
            state_root = base / "state"
            executable, source = _fake_blender_tree(state_root)
            del executable
            outside = base / "outside"
            outside.mkdir()
            external = outside / "keep.pyc"
            external.write_bytes(b"do not delete")
            cache_link = source.parent / "__pycache__"
            try:
                os.symlink(outside, cache_link, target_is_directory=True)
            except OSError as exc:
                self.skipTest(f"directory symlinks unavailable: {exc}")

            with self.assertRaisesRegex(RuntimeError, "link or junction"):
                bootstrap._purge_blender_python_caches(
                    state_root / "tools" / "blender-4.2.0-windows-x64",
                    "Blender portable tree",
                )
            self.assertEqual(external.read_bytes(), b"do not delete")

    def test_purge_bounds_are_checked_before_any_removal(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            state_root = Path(raw) / "state"
            executable, source = _fake_blender_tree(state_root)
            cache_root = source.parent / "__pycache__"
            cache_root.mkdir()
            first = cache_root / "first.pyc"
            second = cache_root / "second.pyc"
            first.write_bytes(b"one")
            second.write_bytes(b"two")

            with mock.patch.object(bootstrap, "BLENDER_CACHE_PURGE_MAX_FILES", 1):
                with self.assertRaisesRegex(RuntimeError, "exceeds the bounded purge"):
                    bootstrap._purge_blender_python_caches(
                        executable.parent, "Blender portable tree"
                    )

            self.assertTrue(first.is_file())
            self.assertTrue(second.is_file())

    def test_tool_status_is_observational_and_does_not_remove_cache(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            state_root = Path(raw) / "state"
            executable, source = _fake_blender_tree(state_root)
            expected_executable = sha256_file(executable)
            expected_tree = directory_tree_sha256(executable.parent)
            cache = source.parent / "__pycache__" / "module.pyc"
            cache.parent.mkdir()
            cache.write_bytes(b"generated")

            with (
                mock.patch.object(bootstrap, "BLENDER_EXE_SHA256", expected_executable),
                mock.patch.object(bootstrap, "BLENDER_TREE_SHA256", expected_tree),
                mock.patch.object(bootstrap, "shutil", wraps=bootstrap.shutil) as shutil_mock,
            ):
                shutil_mock.which.return_value = None
                status = bootstrap.tool_status(state_root)

            self.assertTrue(status["checks"]["blender"])
            self.assertFalse(status["checks"]["blender_tree"])
            self.assertEqual(cache.read_bytes(), b"generated")

    def test_pinned_plugin_bootstrap_recovers_caches_then_attests_git(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            state_root = Path(raw) / "state"
            plugin, source = _fake_plugin_tree(state_root)
            cache = source.parent / "__pycache__" / "module.pyc"
            cache.parent.mkdir()
            cache.write_bytes(b"generated")

            with (
                mock.patch.object(bootstrap.shutil, "which", return_value="git"),
                mock.patch.object(bootstrap, "_run", side_effect=_exact_plugin_git),
            ):
                self.assertEqual(
                    bootstrap._checkout_plugin(state_root / "tools"),
                    plugin,
                )

            self.assertFalse(cache.exists())
            self.assertEqual(source.read_text(encoding="utf-8"), "PLUGIN = 1\n")

    def test_plugin_status_is_observational_and_does_not_remove_cache(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            state_root = Path(raw) / "state"
            plugin, source = _fake_plugin_tree(state_root)
            del plugin
            cache = source.parent / "__pycache__" / "module.pyc"
            cache.parent.mkdir()
            cache.write_bytes(b"generated")

            with (
                mock.patch.object(bootstrap.shutil, "which", return_value="git"),
                mock.patch.object(bootstrap, "_run", side_effect=_exact_plugin_git),
            ):
                status = bootstrap.tool_status(state_root)

            self.assertFalse(status["checks"]["opensage_w3d_plugin"])
            self.assertEqual(cache.read_bytes(), b"generated")

    def test_plugin_override_is_attested_but_never_purged(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            base = Path(raw)
            state_root = base / "state"
            plugin, source = _fake_plugin_tree(base / "override-state")
            cache = source.parent / "__pycache__" / "module.pyc"
            cache.parent.mkdir()
            cache.write_bytes(b"generated")

            with (
                mock.patch.object(bootstrap.shutil, "which", return_value="git"),
                mock.patch.object(bootstrap, "_run", side_effect=_exact_plugin_git),
            ):
                with self.assertRaisesRegex(RuntimeError, "generated Python bytecode"):
                    bootstrap.prepare_opensage_plugin_checkout(state_root, plugin)

            self.assertEqual(cache.read_bytes(), b"generated")

    def test_plugin_prepare_rejects_commit_and_source_drift(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            state_root = Path(raw) / "state"
            plugin, source = _fake_plugin_tree(state_root)

            def dirty_git(command: list[str], *, cwd: Path | None = None) -> str:
                result = _exact_plugin_git(command, cwd=cwd)
                if "status" in command:
                    return " M io_mesh_w3d/__init__.py"
                return result

            with (
                mock.patch.object(bootstrap.shutil, "which", return_value="git"),
                mock.patch.object(bootstrap, "_run", side_effect=dirty_git),
            ):
                source.write_text("PLUGIN = 2\n", encoding="utf-8")
                with self.assertRaisesRegex(RuntimeError, "worktree is dirty"):
                    bootstrap._checkout_plugin(state_root / "tools")

            def wrong_commit(command: list[str], *, cwd: Path | None = None) -> str:
                if "--show-toplevel" in command:
                    return str(Path(cwd).resolve())
                if "status" in command:
                    return ""
                if "-C" in command:
                    return bootstrap.PLUGIN_SUBMODULE_COMMIT
                return "0" * 40

            with (
                mock.patch.object(bootstrap.shutil, "which", return_value="git"),
                mock.patch.object(bootstrap, "_run", side_effect=wrong_commit),
            ):
                with self.assertRaisesRegex(RuntimeError, "pinned commit"):
                    bootstrap._checkout_plugin(state_root / "tools")

    def test_w3d_preflight_defers_expensive_attestation_to_batch(self) -> None:
        pipeline = object.__new__(ImportPipeline)
        pipeline.state_root = Path("state")
        pipeline._blender_tree_verified = False
        pipeline._python_runtime_report = {}
        resolved = SimpleNamespace(
            resources=[SimpleNamespace(rule=SimpleNamespace(converter="w3d-bundle"))],
            profile=SimpleNamespace(id="test-profile"),
        )
        events: list[str] = []
        checks = {
            "python": True,
            "python_runtime": True,
            "blender": True,
            "blender_tree": True,
            "opensage_w3d_plugin": True,
        }

        with (
            mock.patch.object(
                bootstrap,
                "prepare_blender_portable_tree",
                side_effect=lambda *_args: events.append("prepare-blender"),
            ),
            mock.patch.object(
                bootstrap,
                "prepare_opensage_plugin_checkout",
                side_effect=lambda *_args: events.append("prepare-plugin"),
            ),
            mock.patch.object(
                bootstrap,
                "tool_status",
                side_effect=lambda *_args, **kwargs: (
                    events.append(f"status:{kwargs['skip_w3d_attestation']}")
                    or {"checks": checks, "python_runtime": {"version": "test"}}
                ),
            ),
        ):
            pipeline._verify_required_tools(resolved)

        self.assertEqual(events, ["status:True"])
        self.assertFalse(pipeline._blender_tree_verified)
        self.assertEqual(pipeline._python_runtime_report, {"version": "test"})

    def test_w3d_execution_prepares_blender_then_plugin(self) -> None:
        pipeline = object.__new__(ImportPipeline)
        pipeline.state_root = Path("state")
        pipeline._blender_tree_verified = False
        blender = Path("blender.exe")
        plugin = Path("plugin")
        events: list[tuple[str, Path]] = []

        with (
            mock.patch.object(
                bootstrap,
                "prepare_blender_portable_tree",
                side_effect=lambda _state, selected: events.append(
                    ("prepare-blender", selected)
                ),
            ),
            mock.patch.object(
                bootstrap,
                "prepare_opensage_plugin_checkout",
                side_effect=lambda _state, selected: events.append(
                    ("prepare-plugin", selected)
                ),
            ),
        ):
            pipeline._prepare_w3d_execution_tools(blender, plugin)

        self.assertEqual(
            events,
            [("prepare-blender", blender), ("prepare-plugin", plugin)],
        )
        self.assertTrue(pipeline._blender_tree_verified)


def _init_enclosing_repo(root: Path) -> str:
    """Create a real one-commit Git repository and return its HEAD."""

    git = shutil.which("git")
    if not git:
        raise unittest.SkipTest("git is not available")
    subprocess.run([git, "init", "--quiet"], cwd=root, check=True, timeout=60)
    (root / "seed.txt").write_text("seed\n", encoding="utf-8")
    subprocess.run([git, "add", "seed.txt"], cwd=root, check=True, timeout=60)
    subprocess.run(
        [
            git,
            "-c",
            "user.email=fixture@example.invalid",
            "-c",
            "user.name=fixture",
            "-c",
            "commit.gpgsign=false",
            "commit",
            "--quiet",
            "-m",
            "seed",
        ],
        cwd=root,
        check=True,
        timeout=60,
    )
    head = subprocess.run(
        [git, "rev-parse", "HEAD"],
        cwd=root,
        check=True,
        capture_output=True,
        text=True,
        timeout=60,
    )
    return head.stdout.strip().casefold()


class PluginProvenanceExactRootTests(unittest.TestCase):
    """The attestation that feeds _w3d_final_attestation must not inherit an
    enclosing checkout's identity through Git's upward repository discovery."""

    def test_plugin_nested_in_a_checkout_does_not_inherit_its_head(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            outer = Path(raw).resolve()
            head = _init_enclosing_repo(outer)
            plugin = outer / "tools" / "OpenSAGE.BlenderPlugin"
            # A .git that EXISTS but is not a valid repository passes the
            # existence guard while git discovery keeps walking upward.
            (plugin / ".git").mkdir(parents=True)
            package = plugin / "io_mesh_w3d"
            package.mkdir()
            (package / "__init__.py").write_text("PLUGIN = 1\n", encoding="utf-8")
            (package / "blender_addon_updater").mkdir()

            git = shutil.which("git")
            if not git:
                raise unittest.SkipTest("git is not available")
            # Control: the raw walking call the exact-root gate replaces
            # really does answer with the enclosing checkout's HEAD, so the
            # refusal asserted below cannot pass vacuously.
            walked = bootstrap._run([git, "rev-parse", "HEAD"], cwd=plugin)
            self.assertEqual(walked.casefold(), head)

            with self.assertRaisesRegex(
                RuntimeError,
                "OpenSAGE W3D plugin is not itself a Git repository root",
            ):
                bootstrap._attest_opensage_plugin_checkout(plugin)

    def test_updater_submodule_must_be_its_own_repository_root(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            plugin = Path(raw).resolve() / "OpenSAGE.BlenderPlugin"
            plugin.mkdir()
            head = _init_enclosing_repo(plugin)
            package = plugin / "io_mesh_w3d"
            package.mkdir()
            (package / "__init__.py").write_text("PLUGIN = 1\n", encoding="utf-8")
            updater = package / "blender_addon_updater"
            # Invalid .git: discovery walks up to the plugin repository, so
            # the submodule commit would silently equal the plugin's HEAD.
            (updater / ".git").mkdir(parents=True)

            git = shutil.which("git")
            if not git:
                raise unittest.SkipTest("git is not available")
            # Control: the walking submodule call inherits the plugin's HEAD.
            walked = bootstrap._run(
                [git, "-C", "io_mesh_w3d/blender_addon_updater", "rev-parse", "HEAD"],
                cwd=plugin,
            )
            self.assertEqual(walked.casefold(), head)

            with self.assertRaisesRegex(
                RuntimeError,
                "updater submodule is not itself a Git repository root",
            ):
                bootstrap._attest_opensage_plugin_checkout(plugin)

    def test_plugin_status_never_inherits_an_enclosing_checkouts_commit(self) -> None:
        """Even if an enclosing checkout happens to sit at the pinned commit,
        tool_status must not report the plugin ready off that coincidence."""

        with tempfile.TemporaryDirectory() as raw:
            state_root = Path(raw) / "state"
            _fake_plugin_tree(state_root)

            def walking_git(command: list[str], *, cwd: Path | None = None) -> str:
                if "--show-toplevel" in command:
                    # Discovery escaped: the top-level is an enclosing
                    # checkout, not the directory asked about. Every other
                    # answer matches the pins exactly, so only the exact-root
                    # gate stands between this and a false "ready".
                    return str(Path(raw).resolve())
                return _exact_plugin_git(command, cwd=cwd)

            with (
                mock.patch.object(bootstrap.shutil, "which", return_value="git"),
                mock.patch.object(bootstrap, "_run", side_effect=walking_git),
            ):
                status = bootstrap.tool_status(state_root)

            self.assertFalse(status["checks"]["opensage_w3d_plugin"])


if __name__ == "__main__":
    unittest.main()
