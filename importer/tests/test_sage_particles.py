from __future__ import annotations

from hashlib import sha256
import unittest
from unittest.mock import patch

from openbfme_importer.sage_particles import (
    ParticleAssignment,
    ParticleBlock,
    parse_particle_definition,
    parse_particle_definitions,
    select_particle_definition,
)


class SageParticleTests(unittest.TestCase):
    def test_flat_definition_preserves_order_comments_quotes_and_exact_hash(
        self,
    ) -> None:
        source = (
            b"ParticleSystem LegalSafeRipple\r\n"
            b"  Priority = LOW ; ordinary comment\r\n"
            b'  ParticleName = "fixture;//ripple.png" // trailing comment\r\n'
            b"  BurstCount = 1 2\r\n"
            b"End\r\n"
        )
        definition = parse_particle_definition(
            source, "legalsaferipple", kind="particlesystem"
        )

        self.assertEqual(definition.kind, "ParticleSystem")
        self.assertEqual(definition.name, "LegalSafeRipple")
        self.assertEqual(
            [(entry.field, entry.value) for entry in definition.assignments()],
            [
                ("Priority", "LOW"),
                ("ParticleName", '"fixture;//ripple.png"'),
                ("BurstCount", "1 2"),
            ],
        )
        self.assertEqual(definition.source.start_line, 1)
        self.assertEqual(definition.source.end_line, 5)
        self.assertEqual(definition.source.byte_length, len(source))
        self.assertEqual(definition.source.sha256, sha256(source).hexdigest())

    def test_fx_definition_preserves_bare_and_assignment_shaped_nested_blocks(
        self,
    ) -> None:
        source = b"""FXParticleSystem FixtureWake
  System
    Priority = LOW
    ParticleName = FixtureWake.png
  End
  Color = DefaultColor
    Color1 = R:1 G:2 B:3 0
  End
  EmissionVolume = PointEmissionVolume
  End
  Draw = DefaultDraw
  End
End
"""
        definition = parse_particle_definition(source, "FixtureWake")

        self.assertTrue(
            all(isinstance(entry, ParticleBlock) for entry in definition.entries)
        )
        blocks = definition.blocks()
        self.assertEqual(
            [(block.field, block.selector) for block in blocks],
            [
                ("System", None),
                ("Color", "DefaultColor"),
                ("EmissionVolume", "PointEmissionVolume"),
                ("Draw", "DefaultDraw"),
            ],
        )
        self.assertEqual(
            [
                (item.field, item.value)
                for item in definition.assignments(recursive=True)
            ],
            [
                ("Priority", "LOW"),
                ("ParticleName", "FixtureWake.png"),
                ("Color1", "R:1 G:2 B:3 0"),
            ],
        )
        self.assertEqual(blocks[2].entries, ())
        self.assertEqual(blocks[3].entries, ())

    def test_interleaved_entries_and_recursive_walk_remain_authored_order(self) -> None:
        # FXParticleSystem assignment-shaped sections come from the engine's
        # fixed module vocabulary (Update, Physics, ...); bare headers such
        # as Child still nest structurally.
        source = b"""FXParticleSystem OrderedFixture
  First = one
  Update = DefaultUpdate
    NestedFirst = two
    Child
      Deep = three
    End
    NestedLast = four
  End
  Last = five
End
"""
        definition = parse_particle_definition(source, "OrderedFixture")

        self.assertIsInstance(definition.entries[0], ParticleAssignment)
        self.assertIsInstance(definition.entries[1], ParticleBlock)
        self.assertIsInstance(definition.entries[2], ParticleAssignment)
        self.assertEqual(
            [item.value for item in definition.assignments(recursive=True)],
            ["one", "two", "three", "four", "five"],
        )
        self.assertEqual(
            [block.field for block in definition.blocks(recursive=True)],
            ["Update", "Child"],
        )

    def test_parse_is_deterministic_and_retains_duplicate_evidence(self) -> None:
        block = b"ParticleSystem DuplicateFixture\n  Count = 1\nEnd\n"
        source = block + block

        first = parse_particle_definitions(source)
        second = parse_particle_definitions(source)
        self.assertEqual(first, second)
        self.assertEqual(len(first), 2)
        with self.assertRaisesRegex(ValueError, "ambiguous particle definition"):
            select_particle_definition(first, "DuplicateFixture", kind="ParticleSystem")

    def test_kind_ambiguity_is_rejected_unless_kind_is_selected(self) -> None:
        source = b"""ParticleSystem SameName
  Count = 1
End
FXParticleSystem SameName
  Count = 2
End
"""
        definitions = parse_particle_definitions(source)

        with self.assertRaisesRegex(ValueError, "ambiguous particle definition"):
            select_particle_definition(definitions, "SameName")
        selected = select_particle_definition(
            definitions, "SameName", kind="FXParticleSystem"
        )
        self.assertEqual(selected.assignments()[0].value, "2")

    def test_missing_and_unsafe_requests_fail_closed(self) -> None:
        definitions = parse_particle_definitions(
            b"ParticleSystem Existing\n  Count = 1\nEnd\n"
        )
        with self.assertRaisesRegex(ValueError, "missing"):
            select_particle_definition(definitions, "Absent")
        with self.assertRaisesRegex(ValueError, "unsafe"):
            select_particle_definition(definitions, "../Existing")
        with self.assertRaisesRegex(ValueError, "unsupported particle definition kind"):
            select_particle_definition(definitions, "Existing", kind="Object")

    def test_unbalanced_and_unterminated_structures_fail_closed(self) -> None:
        invalid_sources = (
            (b"End\n", "unbalanced top-level End"),
            (b"ParticleSystem Missing\n  Count = 1\n", "unterminated"),
            (
                b"ParticleSystem One\n  Count = 1\nParticleSystem Two\nEnd\n",
                "unterminated",
            ),
        )
        for source, message in invalid_sources:
            with self.subTest(message=message):
                with self.assertRaisesRegex(ValueError, message):
                    parse_particle_definitions(source)

    def test_mis_indented_end_closes_the_innermost_block_like_retail(self) -> None:
        # RotWK 2.01 retail ships mis-indented closers (fxparticlesystem.ini
        # AngSanctumCharge05); SAGE's reader is indentation-blind, so End
        # always closes the innermost open block.
        definitions = parse_particle_definitions(
            b"FXParticleSystem BadIndent\n  System\n    Count = 1\n End\nEnd\n"
        )

        self.assertEqual(len(definitions), 1)
        (block,) = definitions[0].entries
        self.assertEqual(block.field, "System")
        self.assertEqual(block.assignments()[0].field, "Count")

    def test_fx_section_membership_is_structural_not_indentation(self) -> None:
        # RotWK authors section children at the section header's own column
        # (AngSanctumCharge05Sml EmissionVolume); entries belong to the
        # innermost open block until its End, and a trailing assignment whose
        # value is not a lone identifier never opens a section.
        definitions = parse_particle_definitions(
            b"FXParticleSystem Sloppy\n"
            b"  EmissionVolume = LineEmissionVolume\n"
            b"  StartPoint = X:0 Y:0 Z:-180\n"
            b"  EndPoint = X:0 Y:0 Z:-20\n"
            b"  End\n"
            b"End\n"
        )

        self.assertEqual(len(definitions), 1)
        (volume,) = definitions[0].entries
        self.assertEqual(volume.field, "EmissionVolume")
        self.assertEqual(volume.selector, "LineEmissionVolume")
        self.assertEqual(
            [item.field for item in volume.assignments()],
            ["StartPoint", "EndPoint"],
        )

    def test_malformed_names_assignments_quotes_and_top_level_text_fail_closed(
        self,
    ) -> None:
        invalid_sources = (
            (b"ParticleSystem ../Unsafe\nEnd\n", "unsafe ParticleSystem name"),
            (
                b"ParticleSystem Safe\n  ../Field = value\nEnd\n",
                "unsafe particle field",
            ),
            (b"ParticleSystem Safe\n  Empty =\nEnd\n", "empty value"),
            (
                b'ParticleSystem Safe\n  Name = "unterminated\nEnd\n',
                "unterminated quoted",
            ),
            (b"Object NotAParticle\nEnd\n", "unsupported top-level input"),
            (
                b"ParticleSystem Too Many Tokens\nEnd\n",
                "malformed particle definition header",
            ),
        )
        for source, message in invalid_sources:
            with self.subTest(message=message):
                with self.assertRaisesRegex(ValueError, message):
                    parse_particle_definitions(source)

    def test_type_nul_control_and_size_limits_fail_closed(self) -> None:
        with self.assertRaisesRegex(TypeError, "must be bytes"):
            parse_particle_definitions("ParticleSystem Text\nEnd\n")  # type: ignore[arg-type]
        with self.assertRaisesRegex(ValueError, "NUL"):
            parse_particle_definitions(b"ParticleSystem Nul\0\nEnd\n")
        with self.assertRaisesRegex(ValueError, "control character"):
            parse_particle_definitions(b"ParticleSystem Bad\x01\nEnd\n")
        with patch("openbfme_importer.sage_particles.MAX_PARTICLE_SOURCE_BYTES", 8):
            with self.assertRaisesRegex(ValueError, "byte limit"):
                parse_particle_definitions(b"123456789")
        with patch("openbfme_importer.sage_particles.MAX_PARTICLE_LINE_BYTES", 8):
            with self.assertRaisesRegex(ValueError, "line 1 exceeds"):
                parse_particle_definitions(b"ParticleSystem Long\nEnd\n")

    def test_entry_and_nesting_limits_fail_closed(self) -> None:
        source = b"ParticleSystem Counted\n  One = 1\n  Two = 2\nEnd\n"
        with patch(
            "openbfme_importer.sage_particles.MAX_PARTICLE_ENTRIES_PER_DEFINITION",
            1,
        ):
            with self.assertRaisesRegex(ValueError, "entry limit"):
                parse_particle_definitions(source)
        with patch("openbfme_importer.sage_particles.MAX_PARTICLE_TOTAL_ENTRIES", 1):
            with self.assertRaisesRegex(ValueError, "total entry limit"):
                parse_particle_definitions(source)
        nested = b"""FXParticleSystem Nested
  One
    Two
      Value = 1
    End
  End
End
"""
        with patch("openbfme_importer.sage_particles.MAX_PARTICLE_NESTING", 1):
            with self.assertRaisesRegex(ValueError, "nesting exceeds"):
                parse_particle_definitions(nested)


if __name__ == "__main__":
    unittest.main()
