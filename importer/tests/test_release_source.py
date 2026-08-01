"""The publish/update target must be resolved, never assumed."""

from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
if str(REPO_ROOT / "tools") not in sys.path:
    sys.path.insert(0, str(REPO_ROOT / "tools"))

import release_source  # noqa: E402


def _root_with(config: dict | None) -> tempfile.TemporaryDirectory:
    handle = tempfile.TemporaryDirectory()
    root = Path(handle.name)
    (root / "config").mkdir()
    if config is not None:
        (root / release_source.CONFIG_RELATIVE).write_text(
            json.dumps(config), encoding="utf-8"
        )
    return handle


class ReleaseSourceTests(unittest.TestCase):
    def test_unset_target_raises_rather_than_guessing(self) -> None:
        with _root_with({"host": "github.com", "repository": None}) as name:
            with self.assertRaises(release_source.ReleaseSourceUnset):
                release_source.resolve(Path(name), environ={})

    def test_missing_config_file_is_also_unset(self) -> None:
        with _root_with(None) as name:
            with self.assertRaises(release_source.ReleaseSourceUnset):
                release_source.resolve(Path(name), environ={})

    def test_environment_overrides_config(self) -> None:
        with _root_with({"repository": "config-owner/config-name"}) as name:
            source = release_source.resolve(
                Path(name), environ={"OPENBFME_RELEASE_REPOSITORY": "env-owner/env-name"}
            )
        self.assertEqual(source.repository, "env-owner/env-name")

    def test_config_is_used_when_no_environment_override(self) -> None:
        with _root_with({"repository": "config-owner/config-name"}) as name:
            source = release_source.resolve(Path(name), environ={})
        self.assertEqual(source.repository, "config-owner/config-name")
        self.assertEqual(
            source.clone_url, "https://github.com/config-owner/config-name.git"
        )

    def test_url_shaped_repository_is_rejected(self) -> None:
        with _root_with(None) as name:
            for value in (
                "https://github.com/owner/name",
                "owner/name/extra",
                "../../etc/passwd",
                "owner",
            ):
                with self.subTest(value=value):
                    with self.assertRaises(release_source.ReleaseSourceError):
                        release_source.resolve(
                            Path(name),
                            environ={"OPENBFME_RELEASE_REPOSITORY": value},
                        )

    def test_host_must_be_allow_listed(self) -> None:
        with _root_with(None) as name:
            with self.assertRaises(release_source.ReleaseSourceError):
                release_source.resolve(
                    Path(name),
                    environ={
                        "OPENBFME_RELEASE_REPOSITORY": "owner/name",
                        "OPENBFME_RELEASE_HOST": "evil.example",
                    },
                )

    def test_shipped_config_resolves_and_avoids_retired_targets(self) -> None:
        """The shipped default must be valid and must not be a retired repo."""

        data = release_source.load_config(REPO_ROOT)
        self.assertIn("host", data)
        repository = data.get("repository")
        self.assertIsInstance(repository, str)
        self.assertRegex(repository, release_source.REPOSITORY_PATTERN)

        # These targets are dead. Reintroducing either — even as a fallback —
        # would publish to the wrong place.
        retired = ("ancalgonn/open-bfme-engine", "open-bfme/openbmfe-godot")
        self.assertNotIn(repository.casefold(), retired)

    def test_target_is_only_named_in_the_config_file(self) -> None:
        """No resolver may carry an owner/name literal as a built-in default."""

        for module in (
            REPO_ROOT / "tools" / "release_source.py",
            REPO_ROOT / "tools" / "bfme-launcher-mcp" / "src" / "release-source.mjs",
        ):
            with self.subTest(module=module.name):
                text = module.read_text(encoding="utf-8").casefold()
                for token in ("ancalgonn", "openbfme-godot", "openbmfe-godot"):
                    self.assertNotIn(
                        token,
                        text,
                        f"{module.name} must resolve the target, not hardcode it.",
                    )


if __name__ == "__main__":
    unittest.main()
