from __future__ import annotations

import unittest

from openbfme_importer.sage_ini import parse_flat_named_blocks, parse_object_definitions


class SageIniTests(unittest.TestCase):
    def test_flat_blocks_strip_comments_and_preserve_repeated_assignments(self) -> None:
        source = b"""
CommandSet MenSet
  1 = Command_One ; comment
  2 = Command_Two // comment
End
CommandSet OtherSet
  1 = Command_Other
End
"""
        blocks = parse_flat_named_blocks(source, "CommandSet")
        self.assertEqual([block.name for block in blocks], ["MenSet", "OtherSet"])
        self.assertEqual(blocks[0].assignments, (("1", "Command_One"), ("2", "Command_Two")))

    def test_object_split_preserves_parent_and_nested_scalar_assignments(self) -> None:
        source = b"""
Object MenPorter
  CommandSet = MenPorterCommandSet
  Behavior = ProductionUpdate ModuleTag
    ThingTemplate = MenFortress
  End
End
ChildObject MenPorterChild MenPorter
  CommandSet = ChildSet
End
"""
        blocks = parse_object_definitions(source)
        self.assertEqual([block.name for block in blocks], ["MenPorter", "MenPorterChild"])
        self.assertIsNone(blocks[0].parent)
        self.assertEqual(blocks[1].parent, "MenPorter")
        self.assertEqual(blocks[0].values("CommandSet"), ("MenPorterCommandSet",))
        self.assertEqual(blocks[0].values("ThingTemplate"), ("MenFortress",))

    def test_parsers_reject_nul_and_unterminated_flat_block(self) -> None:
        with self.assertRaisesRegex(ValueError, "NUL"):
            parse_object_definitions(b"Object Bad\0\nEnd")
        with self.assertRaisesRegex(ValueError, "unterminated CommandSet"):
            parse_flat_named_blocks(b"CommandSet MissingEnd\n1 = Command_Test", "CommandSet")


if __name__ == "__main__":
    unittest.main()

