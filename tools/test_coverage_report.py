import json

import pytest

from tools.coverage_report import directories, metric, read_summary, render, total


def counts(lc=2, lt=5, bc=1, bt=4):
    return dict(line_covered=lc, line_total=lt, branch_covered=bc, branch_total=bt)


def test_scope_excludes_vendor_and_normalizes_paths(tmp_path):
    path = tmp_path / "summary.json"
    path.write_text(json.dumps({"files": [dict(filename=name, **counts()) for name in
        ["engine\\src\\Common\\x.cpp", "engine/src/vendor/x.cpp", "engine/src/thirdparty/x.h", "engine/tests/x.cpp"]]}))
    assert read_summary(path) == {"engine/src/Common/x.cpp": counts()}


def test_totals_and_directories_do_not_average_percentages():
    files = {"engine/src/A/a.cpp": counts(), "engine/src/A/b.cpp": counts(0, 1, 0, 2)}
    assert total(files) == counts(2, 6, 1, 6)
    assert directories(files) == {"engine/src/A": counts(2, 6, 1, 6)}
    assert metric(counts(), "line") == "2/5 (40.00%)"
    assert metric(counts(0, 0, 0, 0), "branch") == "— (0/0)"


def test_simulation_cannot_be_selected_even_outside_gamelogic():
    name = "engine/src/Common/Recorder.cpp"
    with pytest.raises(ValueError, match="simulation"):
        render({name: counts()}, {name: counts()}, {name}, [name], {})
    name = "engine/src/GameLogic/Anything.cpp"
    with pytest.raises(ValueError, match="simulation"):
        render({name: counts()}, {name: counts()}, set(), [name], {})


def test_retail_only_must_be_measured_and_zero():
    name = "engine/src/Common/test.cpp"
    with pytest.raises(ValueError, match="measured at zero"):
        render({}, {}, set(), [], {name: "retail"})
    with pytest.raises(ValueError, match="measured at zero"):
        render({name: counts()}, {name: counts()}, set(), [], {name: "retail"})
    with pytest.raises(ValueError, match="measured at zero"):
        render({name: counts()}, {name: counts(0)}, set(), [], {name: "retail"})
    with pytest.raises(ValueError, match="measured at zero"):
        render({}, {name: counts(0)}, set(), [], {name: "retail"})


def test_new_or_unbuilt_sources_are_not_reported_as_zero():
    name = "engine/src/Common/new.cpp"
    unbuilt = "engine/src/GodotDevice/unbuilt.cpp"
    result = render({}, {name: counts()}, set(), [], {}, [name, unbuilt])
    assert "not measured" in result
    assert f"| `{unbuilt}` | not measured |" in result
    assert "| `Overall` | — (0/0) | 2/5 (40.00%)" in result
    with pytest.raises(ValueError, match="not measured"):
        render({}, {name: counts()}, set(), [name], {})


def test_verified_retail_only_excluded_from_overall_not_file_table():
    a, b = "engine/src/Common/a.cpp", "engine/src/Common/b.cpp"
    files = {a: counts(), b: counts(0, 10, 0, 20)}
    report = render(files, files, set(), [a], {b: "requires runtime retail mount"})
    assert "| `Overall` | 2/5 (40.00%)" in report
    assert f"| `{b}` | 0/10 (0.00%)" in report
