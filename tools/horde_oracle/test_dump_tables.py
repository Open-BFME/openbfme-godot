"""The committed golden tables equal a fresh dump of the binary (skipped loudly when ROTWK_INSTALL is unset)."""
from __future__ import annotations

import os
import sys
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
sys.path.insert(0, str(HERE))

import dump_tables  # noqa: E402

DATA = ROOT / "engine" / "tests" / "data" / "horde1"


def test_committed_tables_match_the_binary():
    install = os.environ.get("ROTWK_INSTALL")
    if not install:
        pytest.skip("SKIPPED LOUDLY: ROTWK_INSTALL is not set (the golden FieldParse tables cannot be re-dumped)")
    fresh = dump_tables.build(Path(install))
    assert fresh, "no tables dumped"
    for fname, text in fresh.items():
        assert (DATA / fname).read_text() == text, fname


def test_row_counts_are_the_spec_counts():
    # spec horde-and-movement.md 1.2 / 1.5: Locomotor 88 rows, HordeContain 43
    for fname, rows in (("table_locomotor.tsv", 88), ("table_horde_contain.tsv", 43), ("table_locomotor_set_entry.tsv", 3)):
        assert len((DATA / fname).read_text().splitlines()) == rows


def test_generated_name_registry_is_current():
    import gen_names

    assert gen_names.OUT.exists() and gen_names.OUT.read_text() == gen_names.render(), "run tools/horde_oracle/gen_names.py"
