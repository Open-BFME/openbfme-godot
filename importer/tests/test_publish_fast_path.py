"""Fast gates for the publish-path speedups.

Every change these cover trades a repeated read for a remembered one. The
tests exist to prove the remembered answer is byte-identical to the answer the
extra read would have produced, and that the checks that used to ride on those
reads still fail closed.
"""

from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path

from openbfme_importer import sage_particles
from openbfme_importer.pipeline import (
    _bundle_digest_with_files,
    _canonical_pack_inventory,
    _copy_file_with_digest,
    _fold_bundle_digest,
    audit_pack,
    bundle_digest,
)
from openbfme_importer.big import sha256_file
from openbfme_importer.util import write_json_atomic


PARTICLE_DOCUMENT = b"""
ParticleSystem FirstOne
  Priority = ALWAYS_RENDER
End

FXParticleSystem SecondOne
  System
    Priority = ALWAYS_RENDER
  End
End
"""


def _make_pack(root: Path) -> dict:
    write_json_atomic(
        root / "pack.json",
        {
            "id": "test-fast-path-pack",
            "version": "1",
            "local_retail_import": False,
            "redistributable": False,
            "profile_build_complete": True,
        },
    )
    write_json_atomic(root / "data" / "objects.json", {"objects": []})
    (root / "data" / "blob.bin").write_bytes(b"\x01\x02\x03" * 4096)
    manifest = {
        "format": 1,
        "redistributable": False,
        "entries": [],
        "bundle_files": _canonical_pack_inventory(root),
    }
    write_json_atomic(root / "provenance" / "manifest.json", manifest)
    write_json_atomic(root / "provenance" / "audit.json", audit_pack(root, light=False))
    return manifest


class ParticleParseMemoTests(unittest.TestCase):
    def test_repeated_parse_of_the_same_bytes_is_memoized_and_identical(self) -> None:
        sage_particles._parse_particle_definitions_memoized.cache_clear()
        reference = sage_particles.parse_particle_definitions(PARTICLE_DOCUMENT)

        first = sage_particles.parse_particle_definition(
            PARTICLE_DOCUMENT, "FirstOne"
        )
        second = sage_particles.parse_particle_definition(
            PARTICLE_DOCUMENT, "SecondOne"
        )
        third = sage_particles.parse_particle_definition(
            PARTICLE_DOCUMENT, "FirstOne"
        )

        info = sage_particles._parse_particle_definitions_memoized.cache_info()
        self.assertEqual(info.misses, 1, "the document must be parsed exactly once")
        self.assertGreaterEqual(info.hits, 2)
        self.assertEqual(first, reference[0])
        self.assertEqual(second, reference[1])
        self.assertEqual(third, first)

    def test_different_bytes_are_not_served_from_the_memo(self) -> None:
        sage_particles._parse_particle_definitions_memoized.cache_clear()
        sage_particles.parse_particle_definition(PARTICLE_DOCUMENT, "FirstOne")
        other = PARTICLE_DOCUMENT.replace(b"FirstOne", b"RenamedOne")

        with self.assertRaises(ValueError):
            sage_particles.parse_particle_definition(other, "FirstOne")
        self.assertEqual(
            sage_particles.parse_particle_definition(other, "RenamedOne").name,
            "RenamedOne",
        )


class BundleDigestReuseTests(unittest.TestCase):
    def test_digest_folded_from_the_inventory_matches_a_full_tree_walk(self) -> None:
        """The exact fold `build()` records instead of re-reading the pack."""

        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw) / "pack"
            manifest = _make_pack(root)
            provenance_rows = [
                {
                    "path": relative,
                    "size": (root / relative).stat().st_size,
                    "sha256": sha256_file(root / relative),
                }
                for relative in (
                    "provenance/audit.json",
                    "provenance/manifest.json",
                )
            ]
            folded = _fold_bundle_digest(
                (row["path"], row["size"], row["sha256"])
                for row in sorted(
                    [*manifest["bundle_files"], *provenance_rows],
                    key=lambda item: str(item["path"]),
                )
            )
            self.assertEqual(folded, bundle_digest(root))

    def test_single_pass_helper_agrees_with_bundle_digest(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw) / "pack"
            _make_pack(root)
            digest, files = _bundle_digest_with_files(root)
            self.assertEqual(digest, bundle_digest(root))
            self.assertIn(root.resolve() / "data" / "blob.bin", files)


class AuditKnownDigestTests(unittest.TestCase):
    def test_known_digests_produce_the_same_verdict_as_a_full_rehash(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw) / "pack"
            _make_pack(root)
            _digest, files = _bundle_digest_with_files(root)
            self.assertEqual(
                audit_pack(root, light=False, known_digests=files),
                audit_pack(root, light=False),
            )

    def test_a_wrong_supplied_digest_is_still_a_hash_mismatch(self) -> None:
        """The inventory comparison is not bypassed by supplying digests."""

        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw) / "pack"
            _make_pack(root)
            _digest, files = _bundle_digest_with_files(root)
            poisoned = dict(files)
            poisoned[root.resolve() / "data" / "blob.bin"] = "0" * 64
            result = audit_pack(root, light=False, known_digests=poisoned)
            self.assertFalse(result["valid"])
            self.assertTrue(
                any("hash mismatch" in error for error in result["errors"]),
                result["errors"],
            )

    def test_tampered_bytes_are_caught_without_supplied_digests(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw) / "pack"
            _make_pack(root)
            (root / "data" / "blob.bin").write_bytes(b"\x09" * (3 * 4096))
            result = audit_pack(root, light=False)
            self.assertFalse(result["valid"])


class ProfileFoldCopyContractTests(unittest.TestCase):
    """The in-place fold is opt-in; every external caller keeps a copy.

    Composing a faction is a fold, so a deep copy per object re-copies the
    whole growing profile N times (measured 3.1-4.2x on compose). Making that
    optional is only safe while the DEFAULT stays "copy", because outside
    callers rely on their profile surviving the call untouched.
    """

    def test_every_extender_defaults_to_copying(self) -> None:
        import inspect

        from openbfme_importer.faction_slice_profile import (
            _add_spellbook,
            _add_structure,
        )
        from openbfme_importer.playable_unit_import import extend_profile_with_unit

        for function in (extend_profile_with_unit, _add_structure, _add_spellbook):
            with self.subTest(function=function.__name__):
                parameter = inspect.signature(function).parameters["copy"]
                self.assertEqual(parameter.default, True)
                self.assertEqual(parameter.kind, inspect.Parameter.KEYWORD_ONLY)

    def test_owned_mapping_returns_the_caller_object_for_a_dict(self) -> None:
        from openbfme_importer.playable_unit_import import _owned_mapping

        original = {"resources": [], "runtime_data": {}, "pack": {"files": {}}}
        self.assertIs(_owned_mapping(original), original)

        class _View(dict):
            pass

        view = _View(original)
        self.assertIs(_owned_mapping(view), view)

        from types import MappingProxyType

        proxy = MappingProxyType(original)
        owned = _owned_mapping(proxy)
        self.assertIsNot(owned, proxy)
        self.assertEqual(owned, original)
        # A shallow dict of a proxy still shares the nested containers, which
        # is what makes the in-place extend reach the caller's structures.
        self.assertIs(owned["resources"], original["resources"])


class CopyWithDigestTests(unittest.TestCase):
    def test_copy_reports_the_bytes_it_wrote(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            source = Path(raw) / "source.bin"
            destination = Path(raw) / "destination.bin"
            payload = bytes(range(256)) * 5000
            source.write_bytes(payload)
            written, digest = _copy_file_with_digest(source, destination)
            self.assertEqual(written, len(payload))
            self.assertEqual(destination.read_bytes(), payload)
            self.assertEqual(digest, sha256_file(source))
            self.assertEqual(digest, sha256_file(destination))


if __name__ == "__main__":
    unittest.main()
