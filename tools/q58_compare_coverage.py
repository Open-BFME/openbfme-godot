"""Byte-identity diff between two faction convert outputs.

usage: compare_coverage.py <coverageA.json> <artifactsA> <coverageB.json> <artifactsB>
"""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
import sys

EPHEMERAL_ROW = {"cacheHit", "convertElapsedMs"}
EPHEMERAL_SUMMARY = {
    "cacheHits",
    "convertWorkers",
    "convertLoopMs",
    "objectElapsedMsTotal",
    "objectElapsedMsP50",
    "objectElapsedMsP95",
    "objectsPerSecond",
    "slowestObjects",
}


def manifest(root: Path) -> dict[str, str]:
    out: dict[str, str] = {}
    if not root.is_dir():
        return out
    for path in sorted(root.rglob("*.json")):
        if path.is_file():
            out[path.relative_to(root).as_posix().casefold()] = hashlib.sha256(
                path.read_bytes()
            ).hexdigest()
    return out


def main() -> int:
    a_cov, a_art, b_cov, b_art = (Path(value) for value in sys.argv[1:5])
    a = json.loads(a_cov.read_text(encoding="utf-8"))
    b = json.loads(b_cov.read_text(encoding="utf-8"))
    ma, mb = manifest(a_art), manifest(b_art)
    only_a = sorted(set(ma) - set(mb))
    only_b = sorted(set(mb) - set(ma))
    differing = sorted(k for k in set(ma) & set(mb) if ma[k] != mb[k])
    print(
        f"ARTIFACT_FILES a={len(ma)} b={len(mb)}   ONLY_A {len(only_a)}   "
        f"ONLY_B {len(only_b)}   DIFFERING {len(differing)}"
    )
    for key in (only_a + only_b + differing)[:10]:
        print(f"   {key}")
    print(f"COVERAGE_AGGREGATE_EQUAL {a.get('aggregateSha256') == b.get('aggregateSha256')}")
    print(
        f"PLAN_AGGREGATE_EQUAL {a.get('planAggregateSha256') == b.get('planAggregateSha256')}"
    )
    rows_a = {str(row["id"]): row for row in a.get("objects", [])}
    rows_b = {str(row["id"]): row for row in b.get("objects", [])}
    row_diff = []
    for key in sorted(set(rows_a) | set(rows_b)):
        ra = {k: v for k, v in rows_a.get(key, {}).items() if k not in EPHEMERAL_ROW}
        rb = {k: v for k, v in rows_b.get(key, {}).items() if k not in EPHEMERAL_ROW}
        if ra != rb:
            row_diff.append(key)
    print(
        f"COVERAGE_ROWS a={len(rows_a)} b={len(rows_b)} "
        f"differing={len(row_diff)} {row_diff[:5]}"
    )
    print(
        "COVERAGE_ROW_ORDER_EQUAL "
        f"{[r['id'] for r in a.get('objects', [])] == [r['id'] for r in b.get('objects', [])]}"
    )
    body_a = {k: v for k, v in a.items() if k not in {"summary"}}
    body_b = {k: v for k, v in b.items() if k not in {"summary"}}
    for body in (body_a, body_b):
        body["objects"] = [
            {k: v for k, v in row.items() if k not in EPHEMERAL_ROW}
            for row in body.get("objects", [])
        ]
    sum_a = {k: v for k, v in a.get("summary", {}).items() if k not in EPHEMERAL_SUMMARY}
    sum_b = {k: v for k, v in b.get("summary", {}).items() if k not in EPHEMERAL_SUMMARY}
    body_a["summary"], body_b["summary"] = sum_a, sum_b
    canon = lambda v: json.dumps(v, sort_keys=True, separators=(",", ":"), ensure_ascii=False)
    print(f"COVERAGE_DOCUMENT_EQUAL {canon(body_a) == canon(body_b)}")
    identical = (
        not only_a
        and not only_b
        and not differing
        and not row_diff
        and a.get("aggregateSha256") == b.get("aggregateSha256")
        and a.get("planAggregateSha256") == b.get("planAggregateSha256")
        and canon(body_a) == canon(body_b)
    )
    print(f"IDENTICAL {identical}")
    return 0 if identical else 1


if __name__ == "__main__":
    raise SystemExit(main())
