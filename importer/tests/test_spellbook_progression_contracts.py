from __future__ import annotations

import pytest

from openbfme_importer.spellbook_compiler import (
    SpellbookCompilerError,
    compile_rank_science_grants,
)

_GAMEDATA = (
    b"#define PLAYER_PURCHASE_POINTS_GRANTED 1\n"
    b"#define PLAYER_SKILL_POINTS_DELTA_DEFAULT 60\n"
)


def test_rank_science_purchase_points_contract_keeps_authored_receipts() -> None:
    rows = compile_rank_science_grants(
        {
            "data/ini/rank.ini": b"""
Rank 1
  SkillPointsNeededDefault = 0
  SciencePurchasePointsGranted = 5
End
Rank 2
  SkillPointsNeededDefault = 100
  SciencePurchasePointsGranted = PLAYER_PURCHASE_POINTS_GRANTED
End
""",
            "data/ini/gamedata.ini": _GAMEDATA,
        }
    )

    assert rows == [
        {
            "rank": 1,
            "sciencePurchasePointsGranted": {
                "authored": "5",
                "value": 5,
                "sourceIni": "data/ini/rank.ini",
                "line": 4,
            },
            "skillPointsNeededDefault": {
                "authored": "0",
                "value": 0,
                "sourceIni": "data/ini/rank.ini",
                "line": 3,
            },
        },
        {
            "rank": 2,
            "sciencePurchasePointsGranted": {
                "authored": "PLAYER_PURCHASE_POINTS_GRANTED",
                "value": 1,
                "sourceIni": "data/ini/rank.ini",
                "line": 8,
            },
            "skillPointsNeededDefault": {
                "authored": "100",
                "value": 100,
                "sourceIni": "data/ini/rank.ini",
                "line": 7,
            },
        },
    ]


def test_rank_ladder_resolves_authored_multiply_thresholds() -> None:
    # RotWK authors every rank above the first as
    # `#MULTIPLY( PLAYER_SKILL_POINTS_DELTA_DEFAULT n )`; a ladder that cannot
    # resolve those has no rank-up trigger and therefore no grant.
    rows = compile_rank_science_grants(
        {
            "data/ini/rank.ini": b"""
Rank 1
  SkillPointsNeededDefault = 0
  SciencePurchasePointsGranted = 5
End
Rank 2
  SkillPointsNeededDefault = #MULTIPLY( PLAYER_SKILL_POINTS_DELTA_DEFAULT 1 )
  SciencePurchasePointsGranted = PLAYER_PURCHASE_POINTS_GRANTED
End
Rank 3
  SkillPointsNeededDefault = #MULTIPLY( PLAYER_SKILL_POINTS_DELTA_DEFAULT 2 )
  SciencePurchasePointsGranted = PLAYER_PURCHASE_POINTS_GRANTED
End
""",
            "data/ini/gamedata.ini": _GAMEDATA,
        }
    )

    assert [int(row["skillPointsNeededDefault"]["value"]) for row in rows] == [0, 60, 120]
    assert rows[2]["skillPointsNeededDefault"]["authored"] == (
        "#MULTIPLY( PLAYER_SKILL_POINTS_DELTA_DEFAULT 2 )"
    )


def test_rank_ladder_rejects_a_rank_without_an_authored_threshold() -> None:
    with pytest.raises(SpellbookCompilerError, match="SkillPointsNeededDefault"):
        compile_rank_science_grants(
            {
                "data/ini/rank.ini": b"""
Rank 1
  SciencePurchasePointsGranted = 5
End
""",
                "data/ini/gamedata.ini": _GAMEDATA,
            }
        )


def test_rank_ladder_rejects_a_descending_threshold() -> None:
    with pytest.raises(SpellbookCompilerError, match="ascend"):
        compile_rank_science_grants(
            {
                "data/ini/rank.ini": b"""
Rank 1
  SkillPointsNeededDefault = 60
  SciencePurchasePointsGranted = 5
End
Rank 2
  SkillPointsNeededDefault = 30
  SciencePurchasePointsGranted = 1
End
""",
                "data/ini/gamedata.ini": _GAMEDATA,
            }
        )


def test_rank_ladder_rejects_an_unresolved_threshold_expression() -> None:
    with pytest.raises(SpellbookCompilerError, match="unresolved SkillPointsNeededDefault"):
        compile_rank_science_grants(
            {
                "data/ini/rank.ini": b"""
Rank 1
  SkillPointsNeededDefault = SOME_UNDECLARED_DELTA
  SciencePurchasePointsGranted = 5
End
""",
                "data/ini/gamedata.ini": _GAMEDATA,
            }
        )
