"""Locate the tester's own retail install for oracle tests.

Oracle tests compare our conversion against bytes read from a retail Battle for
Middle-earth II install. That install is *never* part of this repository, and
its location differs on every machine, so it must never be hardcoded here.

Resolution order:

1. ``BFME2_INSTALL`` — set this to the directory containing ``game.dat``.
2. Common Windows install locations (see
   :func:`openbfme_importer.paths.discover_retail_install`).

When no install is found the helpers return a path that does not exist, which
makes the ``pytest.mark.skipif`` guards in the oracle tests skip cleanly instead
of failing. That is the intended behaviour for contributors without the game.
"""

from __future__ import annotations

from pathlib import Path

try:
    from openbfme_importer.paths import discover_retail_install
except ImportError:  # pragma: no cover - importer package not on sys.path

    def discover_retail_install() -> Path | None:
        import os

        configured = os.environ.get("BFME2_INSTALL", "").strip()
        return Path(configured).expanduser() if configured else None


#: A path guaranteed not to exist, so skipif guards trigger.
_ABSENT = Path(__file__).resolve().parent / "_no-retail-install-configured"


def retail_install() -> Path:
    """Directory of the tester's retail install, or a non-existent path."""

    found = discover_retail_install()
    return found if found is not None else _ABSENT


def retail_file(*relative: str) -> Path:
    """A file inside the retail install, e.g. ``retail_file("game.dat")``."""

    return retail_install().joinpath(*relative)


def rotwk_install() -> Path:
    """Directory of the tester's Rise of the Witch-king install.

    ``ROTWK_INSTALL`` points at it directly. Otherwise RotWK is looked for
    beside the BFME2 install, which is where the layered-install path puts it.
    """

    import os

    configured = os.environ.get("ROTWK_INSTALL", "").strip()
    if configured:
        return Path(configured).expanduser()
    base = retail_install()
    sibling = base.parent / "The Lord of the Rings, The Rise of the Witch-king"
    if (sibling / "game.dat").is_file():
        return sibling
    return base.parent / "RotWK"


def rotwk_file(*relative: str) -> Path:
    """A file inside the RotWK install."""

    return rotwk_install().joinpath(*relative)


#: Convenience constant for the many oracles that read the main archive.
GAME_DAT = retail_file("game.dat")
