import pytest

from tools.doctest_counts import counts


def case(name, body="", passed=1, failed=0, success="true"):
    return (f'<TestCase name="{name}" filename="test.cpp" line="1">{body}'
            f'<OverallResultsAsserts successes="{passed}" failures="{failed}" test_case_success="{success}"/></TestCase>')


def summary(passed, failed=0, skipped=0):
    return f'<OverallResultsTestCases successes="{passed}" failures="{failed}" skipped="{skipped}"/>'


def test_early_returns_are_not_passes_and_unescaped_stdout_is_allowed():
    text = (case("executed") + case("retail", "SKIP: needs <retail> & files", passed=0)
            + case("partial", '<Message><Text>SKIP LOUDLY: unavailable</Text></Message>', passed=2)
            + case("movie", "SKIP camp2 VP6 retail: not installed", passed=0)
            + '<TestCase name="disabled" skipped="true"/>' + summary(4, skipped=1))
    got = counts(text)
    assert (got["passed"], got["failed"], got["skipped"], got["assertions"]) == (1, 0, 4, 3)
    assert got["runtime_skipped"] == 3
    assert got["runtime_skip_cases"][1]["assertions"] == 2


def test_failure_takes_precedence_over_a_partial_skip():
    got = counts(case("failed", "SKIP: later phase", failed=1, success="false") + summary(0, 1))
    assert got["failed"] == 1
    assert got["runtime_skipped"] == 0
    assert got["failed_assertions"] == 1


def test_zero_assertion_cases_are_exposed_not_silently_called_retail():
    got = counts(case("no assertions", passed=0) + summary(1))
    assert got["zero_assertion_passes"] == ["no assertions"]
    assert got["passed"] == 1


def test_greater_than_in_quoted_test_names_is_legal_xml():
    assert counts(case("a -> b &amp; c") + summary(1))["passed"] == 1


def test_audited_silent_returns_are_counted_as_skips():
    got = counts(case("retail", passed=0) + summary(1), {"retail": "guarded by RW_GAME_DAT"})
    assert (got["passed"], got["runtime_skipped"], got["inactive_skipped"]) == (0, 1, 1)
    assert not got["zero_assertion_passes"]


def test_stale_inactive_inventory_is_rejected():
    with pytest.raises(ValueError, match="missing from run"):
        counts(summary(0), ["absent"])
    with pytest.raises(ValueError, match="executed assertions"):
        counts(case("active") + summary(1), ["active"])


@pytest.mark.parametrize("text", [case("a"), case("a") + summary(2), '<TestCase name="a"></TestCase>' + summary(1)])
def test_incomplete_or_inconsistent_logs_fail_closed(text):
    with pytest.raises(ValueError):
        counts(text)
