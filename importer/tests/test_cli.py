from __future__ import annotations

import contextlib
import hashlib
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock

from openbfme_importer import cli


class CliTests(unittest.TestCase):
    def test_generate_faction_profile_parser_requires_install(self) -> None:
        parser = cli.build_parser()
        args = parser.parse_args(
            ["generate-faction-profile", "--install", "C:/BFME2", "--reindex"]
        )
        self.assertEqual(args.command, "generate-faction-profile")
        self.assertEqual(args.install, "C:/BFME2")
        self.assertTrue(args.reindex)

        with contextlib.redirect_stderr(io.StringIO()):
            with self.assertRaises(SystemExit) as raised:
                parser.parse_args(["generate-faction-profile"])
        self.assertEqual(raised.exception.code, 2)

    def test_generate_faction_profile_writes_deterministic_private_profile(self) -> None:
        profile = {
            "format": 1,
            "id": "synthetic-private-profile",
            "pack": {"id": "synthetic-private-pack"},
            "resources": [
                {"id": "safe-resource-0"},
                {"id": "safe-resource-1"},
            ],
        }
        expected_payload = (
            json.dumps(profile, indent=2, sort_keys=True, ensure_ascii=False) + "\n"
        ).encode("utf-8")

        with tempfile.TemporaryDirectory() as raw:
            state_root = Path(raw) / "external-state"
            stdout = io.StringIO()
            with (
                mock.patch.object(cli, "_load_or_build_catalog", return_value=object()),
                mock.patch.object(
                    cli, "build_men_leaf_profile", return_value=profile
                ) as build_profile,
                contextlib.redirect_stdout(stdout),
            ):
                result = cli.main(
                    [
                        "--json",
                        "--state-root",
                        str(state_root),
                        "generate-faction-profile",
                        "--install",
                        "C:/BFME2",
                    ]
                )

            self.assertEqual(result, 0)
            build_profile.assert_called_once()
            output = json.loads(stdout.getvalue())
            expected_path = (
                state_root / "profiles" / "men-command-leaves.generated.json"
            )
            self.assertEqual(
                output,
                {
                    "ready": True,
                    "profile": str(expected_path),
                    "profile_sha256": hashlib.sha256(expected_payload).hexdigest(),
                    "resource_count": 2,
                },
            )
            self.assertEqual(expected_path.read_bytes(), expected_payload)

    def test_generate_map_profile_writes_deterministic_private_profile(self) -> None:
        profile = {
            "format": 1,
            "id": "synthetic-map-profile",
            "pack": {"id": "synthetic-map-pack"},
            "resources": [{"id": "map-resource"}],
            "runtime_data": {"data/maps.json": {"maps": [{"id": "map-one"}]}},
        }
        expected_payload = (
            json.dumps(profile, indent=2, sort_keys=True, ensure_ascii=False) + "\n"
        ).encode("utf-8")
        with tempfile.TemporaryDirectory() as raw:
            state_root = Path(raw) / "private-state"
            stdout = io.StringIO()
            with (
                mock.patch.object(cli, "_load_or_build_catalog", return_value=object()),
                mock.patch.object(cli, "build_five_map_profile", return_value=profile),
                contextlib.redirect_stdout(stdout),
            ):
                result = cli.main(
                    [
                        "--json",
                        "--state-root",
                        str(state_root),
                        "generate-map-profile",
                        "--install",
                        "C:/BFME2",
                    ]
                )
            self.assertEqual(result, 0)
            output = json.loads(stdout.getvalue())
            expected_path = state_root / "profiles" / "five-maps.generated.json"
            self.assertEqual(output["map_count"], 1)
            self.assertEqual(output["resource_count"], 1)
            self.assertEqual(
                output["profile_sha256"], hashlib.sha256(expected_payload).hexdigest()
            )
            self.assertEqual(expected_path.read_bytes(), expected_payload)


if __name__ == "__main__":
    unittest.main()
