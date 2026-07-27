from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest
from unittest import mock

from openbfme_importer.bootstrap import FFMPEG_EXE_SHA256
from openbfme_importer.native_corpus import (
    NativeCorpusDependencyError,
    PINNED_FFMPEG_VERSION,
    _resolve_ffmpeg,
)
from openbfme_importer.pipeline import (
    RELEASE_IDENTITY_RELATIVE,
    _audit_recipe,
    _importer_recipe_report,
)
from openbfme_importer.tools import (
    discover_executable,
    git_revision,
    git_revision_at_exact_root,
    git_worktree_clean,
    git_worktree_clean_at_exact_root,
)


def _write_bundle_source(root: Path) -> None:
    """Lay down the importer files a recipe hashes. No retail bytes."""

    package = root / "importer" / "openbfme_importer"
    package.mkdir(parents=True)
    (package / "entry.py").write_text("# fixture\n", encoding="utf-8")


def _stamp(root: Path, **overrides: object) -> Path:
    identity = {
        "schema": "openbfme.bundled-source-identity",
        "schemaVersion": 1,
        "commit": "a" * 40,
        "sourceClean": True,
    }
    identity.update(overrides)
    path = root / RELEASE_IDENTITY_RELATIVE
    path.write_text(json.dumps(identity), encoding="utf-8")
    return path


def _init_repo(root: Path) -> str:
    """Create a real one-commit Git repository and return its HEAD."""

    git = shutil.which("git")
    if not git:
        raise unittest.SkipTest("git is not available")
    base = [
        git,
        "-c",
        "user.email=fixture@example.invalid",
        "-c",
        "user.name=fixture",
        "-c",
        "commit.gpgsign=false",
    ]
    subprocess.run([git, "init", "--quiet"], cwd=root, check=True, timeout=60)
    (root / "seed.txt").write_text("seed\n", encoding="utf-8")
    subprocess.run([git, "add", "seed.txt"], cwd=root, check=True, timeout=60)
    subprocess.run(
        [*base, "commit", "--quiet", "-m", "seed"], cwd=root, check=True, timeout=60
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


class ReleaseBundleContractTests(unittest.TestCase):
    def test_custom_state_ffmpeg_precedes_machine_path(self) -> None:
        """The import state's pinned FFmpeg wins over anything on PATH.

        Tool discovery moved out of ``ImportPipeline`` into
        :func:`openbfme_importer.tools.discover_executable`, which derives the
        pinned location from the configured state root. Precedence is asserted
        here; :meth:`test_machine_ffmpeg_is_refused_by_pinned_hash` asserts the
        stronger guarantee that took over when precedence is not enough.
        """

        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            state = root / "state"
            pinned = state / "tools" / "ffmpeg-8.1.1" / "bin" / "ffmpeg.exe"
            pinned.parent.mkdir(parents=True)
            pinned.write_bytes(b"pinned")
            machine = root / "machine-ffmpeg.exe"
            machine.write_bytes(b"machine")

            with (
                mock.patch.dict(
                    os.environ, {"OPENBFME_IMPORT_ROOT": str(state)}, clear=False
                ),
                mock.patch("shutil.which", return_value=str(machine)) as which,
            ):
                os.environ.pop("OPENBFME_FFMPEG", None)
                self.assertEqual(
                    discover_executable("ffmpeg", "OPENBFME_FFMPEG"),
                    pinned.resolve(),
                )
                # Control: with no pinned ffprobe beside it, PATH *is* reached,
                # so the assertion below is about precedence, not a dead mock.
                os.environ.pop("OPENBFME_FFPROBE", None)
                self.assertEqual(
                    discover_executable("ffprobe", "OPENBFME_FFPROBE"),
                    machine.resolve(),
                )

            # PATH was never consulted while the state root held a pinned build.
            self.assertEqual(
                [call.args[0] for call in which.call_args_list], ["ffprobe"]
            )

    def test_machine_ffmpeg_is_refused_by_pinned_hash(self) -> None:
        """A machine FFmpeg standing in for the pinned one fails loudly.

        This is the property the old ``ImportPipeline._ffmpeg_executable``
        precedence test was really guarding, and it is now enforced by content
        rather than by lookup order: whichever binary discovery hands back is
        hashed against ``FFMPEG_EXE_SHA256`` and rejected on a mismatch. That
        closes the recorded incident where a state root that never reached tool
        discovery let a PATH FFmpeg through.
        """

        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            machine = root / "machine-ffmpeg.exe"
            machine.write_bytes(b"machine ffmpeg, not the pinned build")
            self.assertNotEqual(
                hashlib.sha256(machine.read_bytes()).hexdigest(),
                FFMPEG_EXE_SHA256,
            )
            expected = re.escape(
                f"does not match the pinned {PINNED_FFMPEG_VERSION} hash"
            )

            # Handed the wrong binary explicitly.
            with self.assertRaisesRegex(NativeCorpusDependencyError, expected):
                _resolve_ffmpeg(machine)

            # Handed the wrong binary by discovery - the incident shape.
            with mock.patch.dict(
                os.environ, {"OPENBFME_FFMPEG": str(machine)}, clear=False
            ):
                with self.assertRaisesRegex(NativeCorpusDependencyError, expected):
                    _resolve_ffmpeg(None)

    def test_bundled_recipe_uses_validated_release_identity(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            (root / "importer" / "openbfme_importer").mkdir(parents=True)
            (root / "importer" / "openbfme_importer" / "entry.py").write_text(
                "# fixture\n", encoding="utf-8"
            )
            (root / "release-identity.json").write_text(
                json.dumps(
                    {
                        "schema": "openbfme.bundled-source-identity",
                        "schemaVersion": 1,
                        "commit": "a" * 40,
                        "sourceClean": True,
                    }
                ),
                encoding="utf-8",
            )
            with mock.patch(
                "openbfme_importer.pipeline.repo_root_from_module",
                return_value=root,
            ):
                report = _importer_recipe_report()

            self.assertEqual(report["git_commit"], "a" * 40)
            self.assertTrue(report["git_worktree_clean"])

    def test_invalid_bundled_release_identity_fails_closed(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            (root / "importer" / "openbfme_importer").mkdir(parents=True)
            (root / "importer" / "openbfme_importer" / "entry.py").write_text(
                "# fixture\n", encoding="utf-8"
            )
            (root / "release-identity.json").write_text(
                json.dumps(
                    {
                        "schema": "openbfme.bundled-source-identity",
                        "schemaVersion": 1,
                        "commit": "a" * 40,
                        "sourceClean": False,
                    }
                ),
                encoding="utf-8",
            )
            with mock.patch(
                "openbfme_importer.pipeline.repo_root_from_module",
                return_value=root,
            ):
                with self.assertRaisesRegex(RuntimeError, "release identity"):
                    _importer_recipe_report()


class ReleaseIdentityProvenanceTests(unittest.TestCase):
    """The stamp is authoritative; Git is a fallback that must be exact-root."""

    def test_every_malformed_stamp_fails_closed(self) -> None:
        cases = {
            "wrong schema": {"schema": "openbfme.something-else"},
            "future schemaVersion": {"schemaVersion": 2},
            "string schemaVersion": {"schemaVersion": "1"},
            "short commit": {"commit": "a" * 39},
            "non-hex commit": {"commit": "z" * 40},
            "missing commit": {"commit": None},
            "dirty source": {"sourceClean": False},
            "truthy-but-not-true clean": {"sourceClean": 1},
        }
        for label, override in cases.items():
            with self.subTest(case=label):
                with tempfile.TemporaryDirectory() as raw:
                    root = Path(raw).resolve()
                    _write_bundle_source(root)
                    _stamp(root, **override)
                    with mock.patch(
                        "openbfme_importer.pipeline.repo_root_from_module",
                        return_value=root,
                    ):
                        with self.assertRaisesRegex(RuntimeError, "release identity"):
                            _importer_recipe_report()

    def test_unparseable_stamp_fails_closed(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw).resolve()
            _write_bundle_source(root)
            (root / RELEASE_IDENTITY_RELATIVE).write_text("{ not json", encoding="utf-8")
            with mock.patch(
                "openbfme_importer.pipeline.repo_root_from_module", return_value=root
            ):
                with self.assertRaisesRegex(RuntimeError, "release identity"):
                    _importer_recipe_report()

    def test_stamp_is_covered_by_the_recipe_hash(self) -> None:
        """A self-attestation outside the integrity hash attests to nothing."""

        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw).resolve()
            _write_bundle_source(root)
            _stamp(root)
            with mock.patch(
                "openbfme_importer.pipeline.repo_root_from_module", return_value=root
            ):
                first = _importer_recipe_report()
                self.assertIn(
                    RELEASE_IDENTITY_RELATIVE,
                    [entry["path"] for entry in first["files"]],
                )
                # Restamping with a different commit must move the tree digest.
                _stamp(root, commit="b" * 40)
                second = _importer_recipe_report()
            self.assertEqual(second["git_commit"], "b" * 40)
            self.assertNotEqual(first["tree_sha256"], second["tree_sha256"])

    def test_stamped_bundle_reports_release_identity_as_its_source(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw).resolve()
            _write_bundle_source(root)
            _stamp(root)
            with mock.patch(
                "openbfme_importer.pipeline.repo_root_from_module", return_value=root
            ):
                report = _importer_recipe_report()
        self.assertEqual(report["provenance_source"], "release-identity")

    def test_bundle_nested_in_a_checkout_does_not_inherit_its_commit(self) -> None:
        """The footgun this fix exists for: a launcher unpacked into a repo."""

        with tempfile.TemporaryDirectory() as raw:
            outer = Path(raw).resolve()
            head = _init_repo(outer)
            bundle = outer / "launcher"
            bundle.mkdir()
            _write_bundle_source(bundle)

            # Control: the old behaviour really is wrong here, so the
            # assertions below are not passing vacuously.
            self.assertEqual(git_revision(bundle), head)
            # Control: exact-root lookup still answers for a real checkout.
            self.assertEqual(git_revision_at_exact_root(outer), head)

            self.assertIsNone(git_revision_at_exact_root(bundle))
            with mock.patch(
                "openbfme_importer.pipeline.repo_root_from_module",
                return_value=bundle,
            ):
                report = _importer_recipe_report()

        self.assertIsNone(report["git_commit"])
        self.assertNotEqual(report["git_commit"], head)
        self.assertFalse(report["git_worktree_clean"])
        self.assertEqual(report["provenance_source"], "git-exact-root")

    def test_worktree_cleanliness_is_exact_root_only(self) -> None:
        """A clean/dirty verdict inherited from an enclosing checkout is the
        same class of lie as a borrowed commit."""

        with tempfile.TemporaryDirectory() as raw:
            outer = Path(raw).resolve()
            _init_repo(outer)
            nested = outer / "launcher"
            # Empty directories are invisible to git status, so the enclosing
            # checkout stays clean and the walking answer is a plausible lie.
            nested.mkdir()

            # Control: the walking check really does inherit the enclosing
            # checkout's verdict, so the refusal below is not vacuous.
            self.assertTrue(git_worktree_clean(outer))
            self.assertTrue(git_worktree_clean(nested))

            # Exact root still answers; a nested non-repository refuses.
            self.assertTrue(git_worktree_clean_at_exact_root(outer))
            self.assertFalse(git_worktree_clean_at_exact_root(nested))

    def test_real_checkout_reports_git_exact_root(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw).resolve()
            head = _init_repo(root)
            _write_bundle_source(root)
            with mock.patch(
                "openbfme_importer.pipeline.repo_root_from_module", return_value=root
            ):
                report = _importer_recipe_report()
        self.assertEqual(report["git_commit"], head)
        self.assertEqual(report["provenance_source"], "git-exact-root")
        # seed.txt plus the fixture package are untracked/committed but the
        # tree is dirty only if something is uncommitted; either way the flag
        # must be a real boolean derived from the same exact root.
        self.assertIsInstance(report["git_worktree_clean"], bool)

    def test_recipe_records_the_requirements_pin_it_actually_hashed(self) -> None:
        cases = {
            ("requirements-release-win.txt",): [
                "importer/requirements-release-win.txt"
            ],
            ("requirements-win.txt",): ["importer/requirements-win.txt"],
            ("requirements-release-win.txt", "requirements-win.txt"): [
                "importer/requirements-release-win.txt",
                "importer/requirements-win.txt",
            ],
        }
        for present, expected in cases.items():
            with self.subTest(present=present):
                with tempfile.TemporaryDirectory() as raw:
                    root = Path(raw).resolve()
                    _write_bundle_source(root)
                    for name in present:
                        (root / "importer" / name).write_text(
                            f"# {name}\n", encoding="utf-8"
                        )
                    _stamp(root)
                    with mock.patch(
                        "openbfme_importer.pipeline.repo_root_from_module",
                        return_value=root,
                    ):
                        report = _importer_recipe_report()
                self.assertEqual(report["requirements_files"], expected)
                hashed = {entry["path"] for entry in report["files"]}
                self.assertTrue(set(expected) <= hashed)


class RecipeAuditSelfDescriptionTests(unittest.TestCase):
    """Two packs with the same commit must be tellable apart."""

    @staticmethod
    def _recipe(**overrides: object) -> dict[str, object]:
        files = [
            {"path": "importer/requirements-win.txt", "size": 1, "sha256": "a" * 64},
            {"path": RELEASE_IDENTITY_RELATIVE, "size": 2, "sha256": "b" * 64},
        ]
        digest = hashlib.sha256()
        for entry in files:
            digest.update(entry["path"].encode("utf-8"))
            digest.update(b"\0")
            digest.update(str(entry["size"]).encode("ascii"))
            digest.update(b"\0")
            digest.update(entry["sha256"].encode("ascii"))
            digest.update(b"\n")
        recipe: dict[str, object] = {
            "tree_sha256": digest.hexdigest(),
            "files": files,
            "git_commit": "c" * 40,
            "git_worktree_clean": True,
            "provenance_source": "release-identity",
            "requirements_files": ["importer/requirements-win.txt"],
        }
        recipe.update(overrides)
        return recipe

    def test_well_formed_recipe_audits_clean(self) -> None:
        errors: list[str] = []
        _audit_recipe(self._recipe(), errors)
        self.assertEqual(errors, [])

    def test_missing_provenance_source_is_an_error(self) -> None:
        # Includes unhashable JSON shapes: a tampered manifest must produce an
        # audit error, never a TypeError out of the audit itself.
        for value in (None, "", "guessed", "git", 1, ["release-identity"], {"a": 1}):
            with self.subTest(value=value):
                recipe = self._recipe()
                if value is None:
                    recipe.pop("provenance_source")
                else:
                    recipe["provenance_source"] = value
                errors: list[str] = []
                _audit_recipe(recipe, errors)
                self.assertTrue(
                    any("how its commit was established" in item for item in errors),
                    errors,
                )

    def test_release_identity_claim_must_be_inside_the_inventory(self) -> None:
        recipe = self._recipe()
        recipe["files"] = [recipe["files"][0]]  # type: ignore[index]
        errors: list[str] = []
        _audit_recipe(recipe, errors)
        self.assertTrue(
            any("did not hash" in item and "release identity" in item for item in errors),
            errors,
        )

    def test_requirements_pin_must_be_declared_and_hashed(self) -> None:
        missing: list[str] = []
        _audit_recipe(self._recipe(requirements_files=[]), missing)
        self.assertTrue(
            any("does not name the requirements pin" in item for item in missing),
            missing,
        )

        unhashed: list[str] = []
        _audit_recipe(
            self._recipe(requirements_files=["importer/requirements-release-win.txt"]),
            unhashed,
        )
        self.assertTrue(
            any("names a requirements pin it did not hash" in item for item in unhashed),
            unhashed,
        )

    def test_audit_still_refuses_a_recipe_with_no_commit(self) -> None:
        errors: list[str] = []
        _audit_recipe(self._recipe(git_commit=None), errors)
        self.assertTrue(
            any("no exact git commit" in item for item in errors), errors
        )


if __name__ == "__main__":
    unittest.main()
