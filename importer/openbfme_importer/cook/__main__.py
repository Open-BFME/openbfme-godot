"""One-command entry point for the complete OpenBFME bundle cook."""

from __future__ import annotations

import sys

from ._bundle import run_cli


def main() -> int:
    return run_cli("bundle", __doc__, None)


if __name__ == "__main__":
    sys.exit(main())
