from __future__ import annotations

import unittest

from openbfme_importer.sage_cst import (
    SageAssignment,
    SageBlock,
    SageCstLimits,
    normalize_virtual_path,
    parse_sage_document,
    resolve_sage_documents,
    strip_sage_comments,
)


class SageCstTests(unittest.TestCase):
    def test_nested_object_cst_preserves_order_repeats_tags_conditions_and_provenance(self) -> None:
        source = """
Object GondorUnit
  SelectPortrait = UPGondor_Unit
  Draw = W3DScriptedModelDraw ModuleTag_Draw
    DefaultModelConditionState
      Model = GUUnit_SKN
    End
    ModelConditionState = DAMAGED REALLYDAMAGED
      Model = GUUnit_D1
      Model = GUUnit_D2
    End
  End
  Behavior = SlowDeathBehavior ModuleTag_Death
    DeathTypes = ALL
    Sound = \"quoted;//not-comment\" ; actual comment
  End
  RemoveModule ModuleTag_Old
End
ChildObject GondorChild GondorUnit
  SelectPortrait = UPGondor_Child
End
ObjectReskin GondorReskin GondorUnit
  ButtonImage = UIGondor_Reskin
End
""".encode("cp1252")

        document = parse_sage_document(source, r"Data\INI\Object\men.ini")
        self.assertEqual(document.virtual_path, "Data/INI/Object/men.ini")
        self.assertEqual([item.name for item in document.objects], [
            "GondorUnit",
            "GondorChild",
            "GondorReskin",
        ])

        root = document.objects[0]
        self.assertEqual((root.kind, root.parent), ("Object", None))
        self.assertEqual(root.source_virtual_path, "Data/INI/Object/men.ini")
        self.assertEqual(
            [type(item) for item in root.items],
            [SageAssignment, SageBlock, SageBlock, SageAssignment],
        )
        draw = root.blocks[0]
        self.assertEqual(draw.header_key, "Draw")
        self.assertEqual(draw.kind, "W3DScriptedModelDraw")
        self.assertEqual(draw.instance_tag, "ModuleTag_Draw")
        self.assertEqual(draw.header_tokens, ("W3DScriptedModelDraw", "ModuleTag_Draw"))

        default_state, damage_state = draw.blocks
        self.assertEqual(default_state.kind, "DefaultModelConditionState")
        self.assertEqual(default_state.model_condition_tokens, ())
        self.assertEqual(damage_state.kind, "ModelConditionState")
        self.assertEqual(damage_state.model_condition_tokens, ("DAMAGED", "REALLYDAMAGED"))
        self.assertEqual([item.value for item in damage_state.assignments], ["GUUnit_D1", "GUUnit_D2"])
        self.assertEqual([item.ordinal for item in damage_state.assignments], [0, 1])
        self.assertEqual([item.key_ordinal for item in damage_state.assignments], [0, 1])
        self.assertEqual([item.item_ordinal for item in damage_state.assignments], [0, 1])
        self.assertTrue(all(item.source_virtual_path == document.virtual_path for item in damage_state.assignments))

        death = root.blocks[1]
        self.assertEqual((death.kind, death.instance_tag), ("SlowDeathBehavior", "ModuleTag_Death"))
        self.assertEqual(death.values("Sound"), ('"quoted;//not-comment"',))
        remove = root.assignments[-1]
        self.assertFalse(remove.has_equals)
        self.assertEqual((remove.key, remove.value), ("RemoveModule", "ModuleTag_Old"))
        self.assertEqual((document.objects[1].kind, document.objects[1].parent), ("ChildObject", "GondorUnit"))
        self.assertEqual((document.objects[2].kind, document.objects[2].parent), ("ObjectReskin", "GondorUnit"))

    def test_comment_stripping_honors_quotes_and_escaped_quotes(self) -> None:
        self.assertEqual(strip_sage_comments('Value = "a;//b" // tail'), 'Value = "a;//b"')
        self.assertEqual(strip_sage_comments("Value = 'a;//b' ; tail"), "Value = 'a;//b'")
        self.assertEqual(
            strip_sage_comments(r'Value = "a\";//b" ; tail'),
            r'Value = "a\";//b"',
        )

    def test_cp1252_values_and_empty_assignments_are_preserved(self) -> None:
        source = "Object Café\n  DisplayName = Café\n  Empty =\nEnd\n".encode("cp1252")
        item = parse_sage_document(source, "café.ini").objects[0]
        self.assertEqual(item.name, "Café")
        self.assertEqual(item.values("DisplayName"), ("Café",))
        self.assertEqual(item.values("Empty"), ("",))

    def test_include_resolution_is_inline_relative_root_safe_and_case_insensitive(self) -> None:
        documents = {
            "Data/entry.ini": b"""
Object Before
End
#include "sub\\one.ini" ; comment
Object After
End
""",
            "data/SUB/ONE.INI": b"""
Object Included
  Value = "https://example.invalid/a;//b"
End
#include "Data/shared.ini"
""",
            "DATA/shared.ini": b"ChildObject Shared Included\nEnd\n",
        }
        result = resolve_sage_documents("DATA/ENTRY.INI", documents)
        self.assertEqual([item.name for item in result.objects], ["Before", "Included", "Shared", "After"])
        self.assertEqual(
            [item.source_virtual_path for item in result.objects],
            ["Data/entry.ini", "data/SUB/ONE.INI", "DATA/shared.ini", "Data/entry.ini"],
        )
        self.assertEqual([item.resolved_virtual_path for item in result.includes], [
            "data/SUB/ONE.INI",
            "DATA/shared.ini",
        ])
        self.assertEqual([item.virtual_path for item in result.includes], [
            "data/SUB/ONE.INI",
            "DATA/shared.ini",
        ])
        self.assertEqual([item.virtual_path for item in result.documents], [
            "Data/entry.ini",
            "data/SUB/ONE.INI",
            "DATA/shared.ini",
        ])

    def test_parent_relative_include_is_normalized_without_host_access(self) -> None:
        documents = {
            "data/object/main.ini": b'#include "../shared.ini"\nObject Main\nEnd\n',
            "data/shared.ini": b"Object Shared\nEnd\n",
        }
        result = resolve_sage_documents("data/object/main.ini", documents)
        self.assertEqual([item.name for item in result.objects], ["Shared", "Main"])

    def test_include_failures_are_explicit(self) -> None:
        with self.assertRaisesRegex(ValueError, "missing include"):
            resolve_sage_documents("entry.ini", {"entry.ini": b'#include "missing.ini"'})

        with self.assertRaisesRegex(ValueError, "include cycle"):
            resolve_sage_documents(
                "a.ini",
                {
                    "a.ini": b'#include "b.ini"',
                    "b.ini": b'#include "a.ini"',
                },
            )

        with self.assertRaisesRegex(ValueError, "case-ambiguous include"):
            resolve_sage_documents(
                "entry.ini",
                {
                    "entry.ini": b'#include "shared.ini"',
                    "Shared.ini": b"Object One\nEnd",
                    "shared.ini": b"Object Two\nEnd",
                },
            )

        with self.assertRaisesRegex(ValueError, "ambiguous relative/root include"):
            resolve_sage_documents(
                "dir/entry.ini",
                {
                    "dir/entry.ini": b'#include "shared.ini"',
                    "dir/shared.ini": b"Object Relative\nEnd",
                    "shared.ini": b"Object Root\nEnd",
                },
            )

    def test_path_normalization_rejects_absolute_escape_and_nul(self) -> None:
        self.assertEqual(normalize_virtual_path(r"Data\.\INI\x.ini"), "Data/INI/x.ini")
        self.assertEqual(
            normalize_virtual_path("../shared.ini", base_virtual_path="Data/Object/a.ini"),
            "Data/shared.ini",
        )
        for path in ("C:/retail/a.ini", "/retail/a.ini", "../../a.ini", "bad\0.ini"):
            with self.subTest(path=path), self.assertRaisesRegex(ValueError, "unsafe|NUL"):
                normalize_virtual_path(path)

    def test_resource_bounds_and_structural_errors(self) -> None:
        with self.assertRaisesRegex(ValueError, "NUL"):
            parse_sage_document(b"Object Bad\0\nEnd", "bad.ini")
        with self.assertRaisesRegex(ValueError, "byte limit"):
            parse_sage_document(
                b"Object TooLarge\nEnd\n",
                "large.ini",
                limits=SageCstLimits(max_source_bytes=4),
            )
        with self.assertRaisesRegex(ValueError, "nesting depth"):
            parse_sage_document(
                b"Object Deep\n Draw = Kind ModuleTag_X\n  Model = X\n End\nEnd",
                "deep.ini",
                limits=SageCstLimits(max_depth=1),
            )
        with self.assertRaisesRegex(ValueError, "node count"):
            parse_sage_document(
                b"Object One\nEnd\nObject Two\nEnd",
                "nodes.ini",
                limits=SageCstLimits(max_nodes=1),
            )
        with self.assertRaisesRegex(ValueError, "assignment count"):
            parse_sage_document(
                b"Object Fields\n A = 1\n B = 2\nEnd",
                "assign.ini",
                limits=SageCstLimits(max_assignments=1),
            )
        with self.assertRaisesRegex(ValueError, "include count"):
            parse_sage_document(
                b'#include "a.ini"\n#include "b.ini"',
                "entry.ini",
                limits=SageCstLimits(max_includes=1),
            )
        with self.assertRaisesRegex(ValueError, "include depth"):
            resolve_sage_documents(
                "a.ini",
                {"a.ini": b'#include "b.ini"', "b.ini": b"Object B\nEnd"},
                limits=SageCstLimits(max_include_depth=1),
            )

        with self.assertRaisesRegex(ValueError, "unterminated Object"):
            parse_sage_document(b"Object Missing\n Value = 1", "missing.ini")
        with self.assertRaisesRegex(ValueError, "stray End"):
            parse_sage_document(b"End", "stray.ini")
        with self.assertRaisesRegex(ValueError, "ambiguous bare statement"):
            parse_sage_document(b"Object Ambiguous\n EmptyThing\nEnd", "ambiguous.ini")
        with self.assertRaisesRegex(ValueError, "inside object scope"):
            parse_sage_document(
                b'Object BadInclude\n #include "body.ini"\nEnd', "inside.ini"
            )

    def test_header_parent_contract_is_validated(self) -> None:
        with self.assertRaisesRegex(ValueError, "unexpected parent"):
            parse_sage_document(b"Object Bad Parent\nEnd", "bad.ini")
        with self.assertRaisesRegex(ValueError, "lacks a parent"):
            parse_sage_document(b"ChildObject Bad\nEnd", "bad.ini")


if __name__ == "__main__":
    unittest.main()

