from __future__ import annotations

from pathlib import Path

from openbfme_importer import paths


def test_flat_windows_install_names_are_first_class_candidates() -> None:
    relatives = {value.casefold() for value in paths._RETAIL_RELATIVE_DIRS}
    assert "bfme2" in relatives
    assert "rotwk" in relatives


def test_batch_resolver_accepts_the_flat_bfme2_layout_and_fails_bad_env() -> None:
    source = (
        Path(__file__).resolve().parents[2] / "tools" / "resolve-retail-install.bat"
    ).read_text(encoding="utf-8")
    assert "_OBFME_RELDIRS=BFME2;" in source
    invalid_env_block = source.split("if defined BFME2_INSTALL (", 1)[1].split(")", 1)[0]
    assert "exit /b 1" in invalid_env_block
