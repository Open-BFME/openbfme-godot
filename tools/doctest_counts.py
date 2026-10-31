#!/usr/bin/env python3
"""Count doctest XML runs, including the suite's printed SKIP early returns.

Capture stdout with --reporters=xml (not --out): retail tests print directly to
stdout, inside their TestCase element. This is intentionally not a general XML
parser: those prints can contain unescaped characters. The XML result tags are
still authoritative, and counts must reconcile with the run's final summary.
"""
import argparse
import json
from pathlib import Path
import re
import xml.etree.ElementTree as ET


def counts(text, inactive_cases=()):
    result = dict(passed=0, failed=0, runtime_skipped=0, inactive_skipped=0,
                  doctest_skipped=0, assertions=0, failed_assertions=0)
    skipped, zero_assertion = [], []
    inactive_seen = set()
    for match in re.finditer(r'<TestCase\b((?:[^">]|"[^"]*")*?)(?:/>|>(.*?)</TestCase>)', text, re.S):
        header = ET.fromstring("<TestCase" + match[1] + "/>").attrib
        if header.get("skipped") == "true":
            result["doctest_skipped"] += 1
            continue
        body = match[2] or ""
        tags = re.findall(r"<OverallResultsAsserts\b[^>]*/>", body)
        if not tags:
            raise ValueError("incomplete test case: " + header["name"])
        stats = ET.fromstring(tags[-1]).attrib
        successes, failures = int(stats["successes"]), int(stats["failures"])
        inactive = header["name"] in inactive_cases
        if inactive:
            if successes or failures:
                raise ValueError("audited inactive case executed assertions: " + header["name"])
            inactive_seen.add(header["name"])
        result["assertions"] += successes + failures
        result["failed_assertions"] += failures
        if stats["test_case_success"] != "true":
            result["failed"] += 1
        elif inactive or re.search(r"\bSKIP\b", body):
            result["runtime_skipped"] += 1
            result["inactive_skipped"] += int(inactive)
            skipped.append(dict(name=header["name"], file=header["filename"], assertions=successes))
        else:
            result["passed"] += 1
            if not successes:
                zero_assertion.append(header["name"])
    if inactive_seen != set(inactive_cases):
        raise ValueError("audited inactive cases missing from run: " + ", ".join(sorted(set(inactive_cases) - inactive_seen)))
    summaries = re.findall(r"<OverallResultsTestCases\b[^>]*/>", text)
    if not summaries:
        raise ValueError("incomplete run: no final summary")
    summary = ET.fromstring(summaries[-1]).attrib
    if (int(summary["successes"]) != result["passed"] + result["runtime_skipped"]
            or int(summary["failures"]) != result["failed"]
            or int(summary["skipped"]) != result["doctest_skipped"]):
        raise ValueError("test case counts do not reconcile with doctest summary")
    result["skipped"] = result["runtime_skipped"] + result["doctest_skipped"]
    result["runtime_skip_cases"] = skipped
    result["zero_assertion_passes"] = zero_assertion
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log")
    parser.add_argument("--inactive-cases", help="JSON object of audited silent early-return case names and reasons")
    args = parser.parse_args()
    inactive = json.loads(Path(args.inactive_cases).read_text(encoding="utf-8")) if args.inactive_cases else {}
    print(json.dumps(counts(Path(args.log).read_text(encoding="utf-8"), inactive), indent=2))


if __name__ == "__main__":
    main()
