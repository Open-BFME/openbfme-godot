from __future__ import annotations

from openbfme_importer.spellbook_compiler import compile_rank_science_grants


def test_rank_science_purchase_points_contract_keeps_authored_receipts() -> None:
    rows = compile_rank_science_grants(
        {
            "data/ini/rank.ini": b"""
Rank 1
  SciencePurchasePointsGranted = 5
End
Rank 2
  SciencePurchasePointsGranted = PLAYER_PURCHASE_POINTS_GRANTED
End
""",
            "data/ini/gamedata.ini": b"#define PLAYER_PURCHASE_POINTS_GRANTED 1\n",
        }
    )

    assert rows == [
        {
            "rank": 1,
            "sciencePurchasePointsGranted": {
                "authored": "5",
                "value": 5,
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
                "line": 6,
            },
        },
    ]

