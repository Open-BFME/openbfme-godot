from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "importer"))

from openbfme_importer.faction_slice_profile import compose_faction_profile
from openbfme_importer.profile import ImportProfile


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--base-profile", type=Path, required=True)
    parser.add_argument("--report-root", type=Path, required=True)
    parser.add_argument("--faction", action="append", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--receipt", type=Path, required=True)
    args = parser.parse_args()
    base = json.loads(args.base_profile.read_text(encoding="utf-8"))
    profile, receipt = compose_faction_profile(base, args.report_root, args.faction)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.receipt.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(profile, indent=2, sort_keys=True, ensure_ascii=False) + "\n", encoding="utf-8")
    args.receipt.write_text(json.dumps(receipt, indent=2, sort_keys=True, ensure_ascii=False) + "\n", encoding="utf-8")
    ImportProfile.load(args.output)
    print(json.dumps({"profile": str(args.output), "receipt": str(args.receipt), "objectCount": len(receipt["objects"])}))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
