from __future__ import annotations

import re
from pathlib import Path

import pytest

from openbfme_importer.sage_audio import normalize_faction_voice_event


ROOT = Path(__file__).resolve().parents[2]
PRIVATE_ROOT = ROOT / ".private" / "retail-work"
if not (PRIVATE_ROOT / "editions/rotwk/cache/effective-assets").is_dir() and ROOT.parent.name == "worktrees":
    PRIVATE_ROOT = ROOT.parents[2] / ".private" / "retail-work"
EFFECTIVE = (
    PRIVATE_ROOT
    / "editions"
    / "rotwk"
    / "cache"
    / "effective-assets"
)
MORDOR_UNITS = EFFECTIVE / "data/ini/object/evilfaction/units/mordor"


def _voice_fields(name: str) -> dict[str, str]:
    path = MORDOR_UNITS / name
    if not path.is_file():
        pytest.fail("pure RotWK 2.01 effective-assets oracle is not present")
    result: dict[str, str] = {}
    for field, value in re.findall(
        r"(?im)^\s*(VoiceSelect|VoiceMove|VoiceAttack)\s*=\s*([A-Za-z0-9_.+-]+)",
        path.read_text(encoding="latin-1"),
    ):
        result.setdefault(field, value)
    return result


@pytest.mark.parametrize(
    ("object_id", "source_ini", "expected"),
    [
        (
            "MordorGoblinSwordsman",
            "mordorgoblinswordsman.ini",
            {
                "VoiceSelect": "UrukVoiceSelect",
                "VoiceMove": "UrukVoiceMoveMS",
                "VoiceAttack": "UrukVoiceAttackMS",
            },
        ),
        (
            "MordorGoblinArcher",
            "mordorgoblinarcher.ini",
            {
                "VoiceSelect": "UrukVoiceSelect",
                "VoiceMove": "UrukVoiceMoveMS",
                "VoiceAttack": "UrukVoiceAttackMS",
            },
        ),
    ],
)
def test_mordor_goblin_voice_bindings_are_normalized_from_pure_rotwk_oracle(
    object_id: str,
    source_ini: str,
    expected: dict[str, str],
) -> None:
    authored = _voice_fields(source_ini)
    assert {field: authored[field] for field in expected} == expected
    assert {
        field: normalize_faction_voice_event(object_id, field, authored[field])
        for field in expected
    } == expected


def test_mordor_goblin_uruk_named_graph_is_authored_silent_or_orc_mixed() -> None:
    voice_ini = EFFECTIVE / "data/ini/voice.ini"
    sound_ini = EFFECTIVE / "data/ini/soundeffects.ini"
    if not voice_ini.is_file() or not sound_ini.is_file():
        pytest.fail("pure RotWK 2.01 voice oracle is not present")
    voice = voice_ini.read_text(encoding="latin-1")
    sound = sound_ini.read_text(encoding="latin-1")
    select = re.search(r"(?ims)^AudioEvent\s+UrukVoiceSelect\b(?P<body>.*?)^End\b", voice)
    move = re.search(r"(?ims)^Multisound\s+UrukVoiceMoveMS\b(?P<body>.*?)^End\b", voice)
    attack = re.search(r"(?ims)^Multisound\s+UrukVoiceAttackMS\b(?P<body>.*?)^End\b", voice)
    assert select and not re.search(r"(?im)^\s*Sounds\s*=", select.group("body"))
    assert move and "OrcVoiceMove2" in move.group("body")
    assert attack and "OrcVoiceAttack2" in attack.group("body")
    assert re.search(r"(?ims)^AudioEvent\s+OrcVoiceMove2\b.*?EUOrcPr_voiMov", sound)
    assert re.search(r"(?ims)^AudioEvent\s+OrcVoiceAttack2\b.*?EUOrcPr_voiAtt", sound)


def test_unrecognized_mordor_uruk_voice_binding_fails_closed() -> None:
    with pytest.raises(ValueError, match="cross-faction-voice-binding"):
        normalize_faction_voice_event(
            "MordorGoblinSwordsman", "VoiceGuard", "UrukVoiceGuard"
        )
