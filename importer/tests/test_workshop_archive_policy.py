from __future__ import annotations

import importlib.util
from pathlib import Path

import pytest


ROOT = Path(__file__).resolve().parents[2]
TOOL = ROOT / "tools" / "fetch-workshop-archive-policy.py"


def _module():
    spec = importlib.util.spec_from_file_location("workshop_policy", TOOL)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _entry():
    return {
        "Guid": "official-2",
        "Name": "Patch 2.02",
        "Version": "9.7.7",
        "CreationTime": 123,
        "Files": [
            {
                "Name": "lang\\english.big",
                "Md5": "b" * 32,
                "Size": 20,
                "Language": "EN",
                "Url": "https://example.invalid/en",
            },
            {
                "Name": "__patch.big",
                "Md5": "a" * 32,
                "Size": 30,
                "Language": "ALL",
                "Url": "https://example.invalid/all",
            },
            {
                "Name": "lang\\french.big",
                "Md5": "c" * 32,
                "Size": 40,
                "Language": "FR",
                "Url": "https://example.invalid/fr",
            },
        ],
        "Maps": [],
        "Factions": [],
    }


def test_build_policy_pins_identity_and_selects_english_archives() -> None:
    tool = _module()
    policy = tool.build_policy(
        _entry(),
        game="rotwk",
        patch="2.02 v9.7.7",
        language="EN",
        expected_name="Patch 2.02",
        expected_version="9.7.7",
    )
    assert [row["path"] for row in policy["archives"]] == [
        "__patch.big",
        "lang/english.big",
    ]
    assert len(policy["policySha256"]) == 64
    assert len(policy["package"]["receiptSha256"]) == 64


def test_build_policy_refuses_a_moving_version_and_unsafe_path() -> None:
    tool = _module()
    with pytest.raises(ValueError, match="identity changed"):
        tool.build_policy(
            _entry(),
            game="rotwk",
            patch="2.02",
            language="EN",
            expected_name="Patch 2.02",
            expected_version="9.7.8",
        )
    bad = _entry()
    bad["Files"][0]["Name"] = "../escape.big"
    with pytest.raises(ValueError, match="unsafe workshop path"):
        tool.build_policy(
            bad,
            game="rotwk",
            patch="2.02",
            language="EN",
            expected_name="Patch 2.02",
            expected_version="9.7.7",
        )


def test_build_policy_requires_md5_verified_local_file_for_zero_size(
    tmp_path: Path,
) -> None:
    tool = _module()
    entry = _entry()
    payload = b"BIGF" + (b"voice" * 8)
    archive = tmp_path / "lang" / "EnglishAudio.big"
    archive.parent.mkdir()
    archive.write_bytes(payload)
    import hashlib

    entry["Files"].append(
        {
            "Name": "lang\\EnglishAudio.big",
            "Md5": hashlib.md5(payload).hexdigest(),  # noqa: S324 - fixture contract
            "Size": 0,
            "Language": "EN NL NO PL SV TR",
            "Url": "https://example.invalid/audio",
        }
    )

    with pytest.raises(ValueError, match="install-root verification"):
        tool.build_policy(
            entry,
            game="rotwk",
            patch="2.01",
            language="EN",
            expected_name="Patch 2.02",
            expected_version="9.7.7",
        )

    policy = tool.build_policy(
        entry,
        game="rotwk",
        patch="2.01",
        language="EN",
        expected_name="Patch 2.02",
        expected_version="9.7.7",
        install_root=tmp_path,
    )
    audio = next(row for row in policy["archives"] if row["path"].endswith("Audio.big"))
    assert audio["size"] == len(payload)
    assert audio["language"] == "EN NL NO PL SV TR"
