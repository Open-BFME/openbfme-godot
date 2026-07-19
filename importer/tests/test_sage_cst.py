from __future__ import annotations

import unittest

from openbfme_importer.sage_cst import (
    SageAssignment,
    SageBlock,
    SageCstLimits,
    SageIncludeRef,
    SageScript,
    normalize_virtual_path,
    parse_sage_document,
    resolve_sage_documents,
    strip_sage_comments,
)


class SageCstTests(unittest.TestCase):
    def test_endless_module_header_is_retained_as_assignment(self) -> None:
        # BFME2 1.06 retail declares an empty draw module with no body and no
        # terminating End; sibling assignments and modules still belong to the
        # enclosing object, which must survive with the header retained.
        source = b"""
Object CreateAHero
  SelectPortrait = CPWanderer
  Draw = W3DScriptedModelDraw ModuleTag_DRAW
  OkToChangeModelColor = Yes
  KindOf = CREATE_A_HERO HERO PRELOAD
  Behavior = AutoAbilityBehavior ModuleTag_AutoAbilityBehavior
  End
End
ChildObject CreateAHeroMounted CreateAHero
  KindOf = CREATE_A_HERO HERO PRELOAD
End
"""
        document = parse_sage_document(source, "data/ini/object/createahero/createahero.ini")
        self.assertEqual(
            [(item.kind, item.name, item.parent) for item in document.objects],
            [
                ("Object", "CreateAHero", None),
                ("ChildObject", "CreateAHeroMounted", "CreateAHero"),
            ],
        )
        hero = document.objects[0]
        self.assertEqual(hero.values("Draw"), ("W3DScriptedModelDraw ModuleTag_DRAW",))
        draw = hero.assignments[1]
        self.assertEqual((draw.key, draw.has_equals), ("Draw", True))
        self.assertEqual(hero.values("KindOf"), ("CREATE_A_HERO HERO PRELOAD",))
        self.assertEqual(
            [(block.kind, block.instance_tag) for block in hero.blocks],
            [("AutoAbilityBehavior", "ModuleTag_AutoAbilityBehavior")],
        )

    def test_endless_module_header_before_eof_is_retained(self) -> None:
        document = parse_sage_document(
            b"Object Solo\n"
            b"  Draw = W3DScriptedModelDraw ModuleTag_DRAW\n"
            b"  KindOf = HERO\n"
            b"End\n",
            "data/ini/object/solo.ini",
        )
        solo = document.objects[0]
        self.assertEqual(solo.values("Draw"), ("W3DScriptedModelDraw ModuleTag_DRAW",))
        self.assertEqual(solo.values("KindOf"), ("HERO",))
        self.assertEqual(solo.blocks, ())

    def test_bare_state_block_with_unindented_body(self) -> None:
        # Retail ArmorSet blocks do not always indent their body, so block
        # kinds must be recognized by name rather than by indentation.
        source = b"""
Object Statue
  ArmorSet
  Conditions = None
  Armor = StructureArmor
  End
  KindOf = STRUCTURE
End
"""
        document = parse_sage_document(source, "data/ini/object/structures/statue.ini")
        statue = document.objects[0]
        armor_set = statue.blocks[0]
        self.assertEqual(armor_set.kind, "ArmorSet")
        self.assertEqual(armor_set.values("Conditions"), ("None",))
        self.assertEqual(armor_set.values("Armor"), ("StructureArmor",))
        self.assertEqual(statue.values("KindOf"), ("STRUCTURE",))

    def test_sound_upgrade_is_an_end_terminated_block(self) -> None:
        # Retail UpgradeSoundSelectorClientBehavior modules nest SoundUpgrade
        # blocks holding upgrade-gated voice overrides.
        source = b"""
Object Guardian
  ClientBehavior = UpgradeSoundSelectorClientBehavior ModuleTag_SoundSelector
    SoundUpgrade = Upgrade_SiegeHammer
      VoiceAttack = GuardianVoiceAttackHammer
    End
  End
  ClientBehavior = AnimationSoundClientBehavior ModuleTag_AnimAudio
  End
End
"""
        document = parse_sage_document(source, "data/ini/object/units/dwarven/guardian.ini")
        guardian = document.objects[0]
        self.assertEqual(len(guardian.blocks), 2)
        selector = guardian.blocks[0]
        self.assertEqual(selector.kind, "UpgradeSoundSelectorClientBehavior")
        (sound_upgrade,) = selector.blocks
        self.assertEqual(sound_upgrade.kind, "SoundUpgrade")
        self.assertEqual(sound_upgrade.header_key, "SoundUpgrade")
        self.assertEqual(sound_upgrade.header_tokens, ("Upgrade_SiegeHammer",))
        self.assertEqual(sound_upgrade.values("VoiceAttack"), ("GuardianVoiceAttackHammer",))
        self.assertEqual(guardian.blocks[1].kind, "AnimationSoundClientBehavior")

    def test_nested_sound_state_is_a_state_block(self) -> None:
        document = parse_sage_document(
            b"""
Object RetailHero
  ClientBehavior = ModelConditionSoundSelectorClientBehavior ModuleTag_Sound
    SoundState = MOUNTED
      VoiceMove = HeroVoiceMoveMounted
      VoiceSelect = HeroVoiceSelectMounted
    End
  End
End
""",
            "data/ini/object/units/retailhero.ini",
        )

        client = document.objects[0].blocks[0]
        sound_state = client.blocks[0]
        self.assertEqual(sound_state.kind, "SoundState")
        self.assertEqual(sound_state.header_key, "SoundState")
        self.assertEqual(sound_state.header_tokens, ("MOUNTED",))
        self.assertEqual(sound_state.values("VoiceMove"), ("HeroVoiceMoveMounted",))

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

    def test_assignment_shaped_scalars_do_not_become_blocks_from_indentation(self) -> None:
        # Retail files freely mix spaces and tabs at one semantic level.  A
        # CommandSet followed by a tab-indented WeaponSet must remain a scalar;
        # only explicit module/state grammar may turn an assignment into a block.
        source = (
            "Object GondorBarracks\n"
            "  CommandSet = GondorBarracksCommandSet\n"
            "\tWeaponSet\n"
            "\t\tConditions = None\n"
            "\tEnd\n"
            "End\n"
        ).encode("cp1252")

        root = parse_sage_document(source, "data/object/barracks.ini").objects[0]
        self.assertEqual([type(item) for item in root.items], [SageAssignment, SageBlock])
        self.assertEqual(root.assignments[0].key, "CommandSet")
        self.assertEqual(root.assignments[0].value, "GondorBarracksCommandSet")
        self.assertEqual(root.blocks[0].kind, "WeaponSet")
        self.assertEqual(root.blocks[0].values("Conditions"), ("None",))

    def test_retail_melee_behavior_is_an_explicit_end_terminated_block(self) -> None:
        source = (
            "Object GondorFighterHorde\n"
            "  Behavior = HordeContain ModuleTag_HordeContain\n"
            "    MeleeBehavior = Amoeba\n"
            "    End\n"
            "    InitialPayload = GondorFighter 15\n"
            "  End\n"
            "  Behavior = PhysicsBehavior ModuleTag_PhysicsBehavior\n"
            "    GravityMult = 1.0\n"
            "  End\n"
            "End\n"
        ).encode("cp1252")

        root = parse_sage_document(source, "data/object/menhordes.ini").objects[0]
        self.assertEqual([block.kind for block in root.blocks], ["HordeContain", "PhysicsBehavior"])
        contain = root.blocks[0]
        self.assertEqual([block.kind for block in contain.blocks], ["MeleeBehavior"])
        self.assertEqual(contain.blocks[0].header_tokens, ("Amoeba",))
        self.assertEqual(contain.values("InitialPayload"), ("GondorFighter 15",))

    def test_retail_override_emotion_is_a_block_but_plain_emotions_are_scalars(self) -> None:
        source = (
            "Object GondorHorde\n"
            "  Behavior = EmotionTrackerUpdate Module_EmotionTracker\n"
            "    AddEmotion = Terror_Base\n"
            "    AddEmotion = OVERRIDE Taunt_Base\n"
            "      AttributeModifier = GondorFighterTaunt\n"
            "    End\n"
            "    AddEmotion = Alert_Base\n"
            "  End\n"
            "End\n"
        ).encode("cp1252")

        root = parse_sage_document(source, "data/object/menhordes.ini").objects[0]
        tracker = root.blocks[0]
        self.assertEqual(tracker.values("AddEmotion"), ("Terror_Base", "Alert_Base"))
        self.assertEqual([block.kind for block in tracker.blocks], ["AddEmotion"])
        self.assertEqual(tracker.blocks[0].header_tokens, ("OVERRIDE", "Taunt_Base"))
        self.assertEqual(
            tracker.blocks[0].values("AttributeModifier"),
            ("GondorFighterTaunt",),
        )

    def test_retail_lod_options_are_explicit_end_terminated_blocks(self) -> None:
        source = (
            "Object GondorArcher\n"
            "  Draw = W3DHordeModelDraw ModuleTag_01\n"
            "    LodOptions = LOW\n"
            "      AllowMultipleModels = ALLOW_MULTIPLE_MODELS_LOW\n"
            "    End\n"
            "    LodOptions = HIGH\n"
            "      MaxRandomTextures = MAX_RANDOM_TEXTURES_HIGH\n"
            "    End\n"
            "  End\n"
            "End\n"
        ).encode("cp1252")

        root = parse_sage_document(source, "data/object/gondorarcher.ini").objects[0]
        draw = root.blocks[0]
        self.assertEqual([block.kind for block in draw.blocks], ["LodOptions", "LodOptions"])
        self.assertEqual([block.header_tokens for block in draw.blocks], [("LOW",), ("HIGH",)])

    def test_retail_bare_locomotor_set_survives_inconsistent_indentation(self) -> None:
        source = (
            "Object Civilian\n"
            "  LocomotorSet\n"
            "  Locomotor = GondorCivilianLocomotor\n"
            "  Condition = SET_PANIC\n"
            " End\n"
            "End\n"
        ).encode("cp1252")

        root = parse_sage_document(source, "data/object/civilian.ini").objects[0]
        self.assertEqual([block.kind for block in root.blocks], ["LocomotorSet"])
        self.assertEqual(root.blocks[0].values("Condition"), ("SET_PANIC",))

    def test_begin_script_body_is_opaque_bounded_and_provenanced(self) -> None:
        source = b"""
Object Scripted
  AnimationState = BUILD_PLACEMENT_CURSOR
    BeginScript
      CurDrawableHideSubObject(\"N_Window\")
      #include \"not-an-include-inside-script.inc\"
      End
    EndScript
    StateName = Cursor
  End
End
"""
        root = parse_sage_document(source, "data/object/scripted.ini").objects[0]
        state = root.blocks[0]
        self.assertEqual([type(item) for item in state.items], [SageScript, SageAssignment])
        script = state.scripts[0]
        self.assertEqual(
            [line.text for line in script.lines],
            [
                'CurDrawableHideSubObject("N_Window")',
                '#include "not-an-include-inside-script.inc"',
                "End",
            ],
        )
        self.assertEqual([line.ordinal for line in script.lines], [0, 1, 2])
        self.assertEqual(script.source_virtual_path, "data/object/scripted.ini")
        self.assertEqual(script.lines[0].source_virtual_path, script.source_virtual_path)
        self.assertLess(script.line, script.end_line)

        with self.assertRaisesRegex(ValueError, "node count"):
            parse_sage_document(
                source,
                "data/object/scripted.ini",
                limits=SageCstLimits(max_nodes=4),
            )

    def test_script_delimiter_failures_are_explicit(self) -> None:
        failures = (
            (b"EndScript\n", "stray script delimiter"),
            (b"Object O\n EndScript\nEnd\n", "stray EndScript"),
            (b"Object O\n BeginScript\n  BeginScript\n EndScript\nEnd\n", "nested BeginScript"),
            (b"Object O\n BeginScript\n  Statement\nEnd\n", "unterminated BeginScript"),
        )
        for source, message in failures:
            with self.subTest(message=message), self.assertRaisesRegex(ValueError, message):
                parse_sage_document(source, "scripts.ini")

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

    def test_body_includes_are_retained_and_fragments_are_spliced_with_provenance(self) -> None:
        documents = {
            "Data/Object/Men/barracks.ini": b"""
Object GondorBarracks
  Value = host-before
  #include "..\\..\\Includes\\building.inc"
  Draw = W3DScriptedModelDraw ModuleTag_Draw
    #include "../../../Shared/draw.inc"
  End
  Value = host-after
End
""",
            "data/includes/BUILDING.INC": b"""
Value = fragment
#include "nested.inc"
Behavior = ActiveBody ModuleTag_Body
  MaxHealth = 1000
End
""",
            "Data/Includes/nested.inc": b"Value = nested\n",
            "Shared/draw.inc": b"""
ModelConditionState = DAMAGED
  Model = GBBarracks_D1
End
""",
        }

        unresolved = parse_sage_document(
            documents["Data/Object/Men/barracks.ini"],
            "Data/Object/Men/barracks.ini",
        ).objects[0]
        self.assertEqual(len(unresolved.includes), 1)
        self.assertIsNone(unresolved.includes[0].resolved_virtual_path)

        result = resolve_sage_documents("data/object/men/BARRACKS.INI", documents)
        root = result.objects[0]
        self.assertEqual(
            root.values("Value"),
            ("host-before", "fragment", "nested", "host-after"),
        )
        self.assertEqual(
            [item.source_virtual_path for item in root.assignments if item.key == "Value"],
            [
                "Data/Object/Men/barracks.ini",
                "data/includes/BUILDING.INC",
                "Data/Includes/nested.inc",
                "Data/Object/Men/barracks.ini",
            ],
        )
        self.assertEqual([item.ordinal for item in root.assignments], [0, 1, 2, 3])
        self.assertEqual([item.key_ordinal for item in root.assignments], [0, 1, 2, 3])
        self.assertIsInstance(root.items[1], SageIncludeRef)
        self.assertEqual(root.items[1].resolved_virtual_path, "data/includes/BUILDING.INC")

        draw = next(block for block in root.blocks if block.header_key == "Draw")
        self.assertIsInstance(draw.items[0], SageIncludeRef)
        self.assertEqual(draw.blocks[0].source_virtual_path, "Shared/draw.inc")
        self.assertEqual(draw.blocks[0].values("Model"), ("GBBarracks_D1",))
        self.assertEqual(
            [item.resolved_virtual_path for item in result.includes],
            [
                "data/includes/BUILDING.INC",
                "Data/Includes/nested.inc",
                "Shared/draw.inc",
            ],
        )
        self.assertEqual(
            [item.virtual_path for item in result.fragments],
            [
                "data/includes/BUILDING.INC",
                "Data/Includes/nested.inc",
                "Shared/draw.inc",
            ],
        )

    def test_inline_include_failures_use_the_same_safe_graph_rules(self) -> None:
        with self.assertRaisesRegex(ValueError, "missing include"):
            resolve_sage_documents(
                "entry.ini",
                {"entry.ini": b'Object O\n #include "missing.inc"\nEnd\n'},
            )

        with self.assertRaisesRegex(ValueError, "include cycle"):
            resolve_sage_documents(
                "entry.ini",
                {
                    "entry.ini": b'Object O\n #include "a.inc"\nEnd\n',
                    "a.inc": b'#include "b.inc"\n',
                    "b.inc": b'#include "a.inc"\n',
                },
            )

        with self.assertRaisesRegex(ValueError, "case-ambiguous include"):
            resolve_sage_documents(
                "entry.ini",
                {
                    "entry.ini": b'Object O\n #include "shared.inc"\nEnd\n',
                    "Shared.inc": b"Value = One\n",
                    "shared.inc": b"Value = Two\n",
                },
            )

        with self.assertRaisesRegex(ValueError, "ambiguous relative/root include"):
            resolve_sage_documents(
                "dir/entry.ini",
                {
                    "dir/entry.ini": b'Object O\n #include "shared.inc"\nEnd\n',
                    "dir/shared.inc": b"Value = Relative\n",
                    "shared.inc": b"Value = Root\n",
                },
            )

        with self.assertRaisesRegex(ValueError, "include depth"):
            resolve_sage_documents(
                "entry.ini",
                {
                    "entry.ini": b'Object O\n #include "one.inc"\nEnd\n',
                    "one.inc": b"Value = One\n",
                },
                limits=SageCstLimits(max_include_depth=1),
            )

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
        parsed_include = parse_sage_document(
            b'Object HasInclude\n #include "body.ini"\nEnd', "inside.ini"
        ).objects[0].includes[0]
        self.assertEqual(parsed_include.virtual_path, "body.ini")
        self.assertIsNone(parsed_include.resolved_virtual_path)

    def test_header_parent_contract_is_validated(self) -> None:
        with self.assertRaisesRegex(ValueError, "unexpected parent"):
            parse_sage_document(b"Object Bad Parent\nEnd", "bad.ini")
        with self.assertRaisesRegex(ValueError, "lacks a parent"):
            parse_sage_document(b"ChildObject Bad\nEnd", "bad.ini")


if __name__ == "__main__":
    unittest.main()
