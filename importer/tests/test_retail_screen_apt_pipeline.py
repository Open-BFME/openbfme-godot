"""The screen converter, driven the way the pipeline drives it.

Queue Q117.  The importer can cook a screen and Godot can draw one; this is the
seam between them - `sage-apt-screen-runtime`, the converter case a profile
names to get a screen into a content pack.

Two things are worth a gate here and neither is about pixels.  A screen lane
has 62 movies rather than one pinned scene, so the movie is an OPTION and the
declared output must be the path that movie derives - stating it twice is what
stops a profile cooking ScoreScreen into SpellStore's slot.  And the frame a
screen is shown at is a declared policy, so a contract selected by any other
rule must be refused HERE as well as in the runtime: a policy change should
never be able to reach a pack quietly.
"""

from __future__ import annotations

from pathlib import Path

import pytest

from openbfme_importer.catalog import CatalogEntry
from openbfme_importer.pipeline import ImportPipeline, RESOURCE_BUNDLE_CONVERTERS
from openbfme_importer.profile import (
    ALLOWED_CONVERTERS,
    ResolvedResource,
    ResourceRule,
)
from openbfme_importer.retail_screen_apt_convert import screen_bundle_virtual_paths

REPO = Path(__file__).resolve().parents[2]
ASSETS = REPO / "workspace/retail-work/cache/effective-assets"

pytestmark = pytest.mark.skipif(
    not ASSETS.is_dir(), reason="private effective-assets oracle is not present"
)

CONVERTER = "sage-apt-screen-runtime"


def _resource(movie: str, output: str, options: dict) -> ResolvedResource:
    paths = screen_bundle_virtual_paths(ASSETS, movie)
    rule = ResourceRule(
        id=f"screen-apt-{movie.casefold()}",
        kind="ui",
        patterns=tuple(paths),
        required=True,
        converter=CONVERTER,
        output=output,
        limit=len(paths),
        expected_count=len(paths),
        options=options,
    )
    entries = tuple(
        CatalogEntry(
            archive=f"apt/{movie.casefold()}.big",
            name=path,
            offset=0,
            size=ASSETS.joinpath(*path.split("/")).stat().st_size,
            precedence=0,
        )
        for path in paths
    )
    return ResolvedResource(
        rule=rule, entries=entries, missing_patterns=(), count_error=None
    )


def _extracted(movie: str) -> dict:
    return {
        (f"apt/{movie.casefold()}.big", path.casefold()): {
            "source_path": str(ASSETS.joinpath(*path.split("/")))
        }
        for path in screen_bundle_virtual_paths(ASSETS, movie)
    }


def _convert(pipeline, movie: str, pack_root: Path, **overrides):
    output = overrides.pop(
        "output", f"data/ui/screens/{movie.casefold()}/scene-contract.json"
    )
    options = overrides.pop("options", {"movie": movie})
    return pipeline._convert_screen_apt_runtime_bundle(
        _resource(movie, output, options),
        _extracted(movie),
        output,
        options,
        pack_root,
    )


def test_the_converter_is_registered_everywhere_it_must_be() -> None:
    """A converter the profile schema rejects can never be used at all."""

    assert CONVERTER in RESOURCE_BUNDLE_CONVERTERS
    assert CONVERTER in ALLOWED_CONVERTERS


def test_a_screen_lands_in_the_pack_with_its_atlas(tmp_path: Path) -> None:
    pipeline = ImportPipeline.__new__(ImportPipeline)
    outputs = _convert(pipeline, "SpellStore", tmp_path)

    written = sorted(path.relative_to(tmp_path).as_posix() for path in outputs)
    assert written == [
        "assets/ui/screens/spellstore/apt-spellstore-1-6f967901b486.png",
        "data/ui/screens/spellstore/scene-contract.json",
    ]
    assert all(path.is_file() for path in outputs)
    # The staging directory never survives a successful cook.
    assert not list(tmp_path.glob(".screen-apt-runtime-*"))


def test_the_declared_output_must_match_the_movie(tmp_path: Path) -> None:
    """Stating the movie twice is what stops a profile crossing the wires."""

    pipeline = ImportPipeline.__new__(ImportPipeline)
    with pytest.raises(ValueError, match="output must be"):
        _convert(
            pipeline,
            "SpellStore",
            tmp_path,
            output="data/ui/screens/scorescreen/scene-contract.json",
        )


def test_an_unsafe_or_absent_movie_option_is_refused(tmp_path: Path) -> None:
    pipeline = ImportPipeline.__new__(ImportPipeline)
    for bad in ("", "../Palantir", "Spell Store", "x" * 65):
        with pytest.raises(ValueError, match="bare movie identifier"):
            _convert(
                pipeline,
                "SpellStore",
                tmp_path,
                options={"movie": bad},
                output="data/ui/screens/spellstore/scene-contract.json",
            )
    with pytest.raises(ValueError, match="unsupported option"):
        _convert(
            pipeline,
            "SpellStore",
            tmp_path,
            options={"movie": "SpellStore", "frame": 3},
        )


def test_a_changed_source_aggregate_is_refused(tmp_path: Path) -> None:
    """The option is the profile's second opinion on what it is cooking."""

    pipeline = ImportPipeline.__new__(ImportPipeline)
    with pytest.raises(RuntimeError, match="source aggregate changed"):
        _convert(
            pipeline,
            "SpellStore",
            tmp_path,
            options={
                "movie": "SpellStore",
                "expectedSourceAggregateSha256": "0" * 64,
            },
        )
    # ...and it passes when it is the digest the tree actually holds.
    outputs = _convert(
        pipeline,
        "SpellStore",
        tmp_path,
        options={
            "movie": "SpellStore",
            "expectedSourceAggregateSha256": (
                "87a81965753ce14da688ebe283104fec7c8c567d4a5e7d4ff3dfedf820afcdf6"
            ),
        },
    )
    assert len(outputs) == 2


def test_output_that_would_overwrite_pack_content_is_refused(tmp_path: Path) -> None:
    pipeline = ImportPipeline.__new__(ImportPipeline)
    existing = tmp_path / "data/ui/screens/spellstore/scene-contract.json"
    existing.parent.mkdir(parents=True)
    existing.write_text("{}", encoding="utf-8")
    with pytest.raises(RuntimeError, match="collides with pack output"):
        _convert(pipeline, "SpellStore", tmp_path)
    # The refusal leaves the pack exactly as it found it.
    assert existing.read_text(encoding="utf-8") == "{}"
    assert not list(tmp_path.glob(".screen-apt-runtime-*"))


def test_an_unextracted_source_is_a_refusal_not_a_partial_cook(
    tmp_path: Path,
) -> None:
    pipeline = ImportPipeline.__new__(ImportPipeline)
    extracted = _extracted("SpellStore")
    extracted.pop(next(iter(extracted)))
    output = "data/ui/screens/spellstore/scene-contract.json"
    with pytest.raises(RuntimeError, match="was not extracted"):
        pipeline._convert_screen_apt_runtime_bundle(
            _resource("SpellStore", output, {"movie": "SpellStore"}),
            extracted,
            output,
            {"movie": "SpellStore"},
            tmp_path,
        )


def test_a_screen_with_imports_cooks_through_the_pipeline(tmp_path: Path) -> None:
    """ScoreScreen needs MenuExport and GameWindowGadgets to be a scene."""

    pipeline = ImportPipeline.__new__(ImportPipeline)
    outputs = _convert(pipeline, "ScoreScreen", tmp_path)
    relative = sorted(path.relative_to(tmp_path).as_posix() for path in outputs)
    assert "data/ui/screens/scorescreen/scene-contract.json" in relative
    # Six atlases across the three-movie closure, one contract.
    assert len(relative) == 7
    assert all(
        path.startswith("assets/ui/screens/")
        for path in relative
        if path.endswith(".png")
    )


def test_a_contract_carrying_a_different_priority_is_refused(tmp_path: Path) -> None:
    """What the converter can and cannot lock, stated precisely.

    It CANNOT catch a coordinated source edit to `OPEN_LABEL_PRIORITY`: the
    producer and the verifier would both honour the new order and agree. Only
    the tuple pin in `test_retail_screen_apt_convert.py` catches that, and it
    is a code review question, not a runtime one.

    What it DOES catch is a contract that disagrees with the policy in force -
    a stale contract cooked under an older priority, or a hand-edited one. That
    is the reachable failure, and it is refused rather than shipped.
    """

    from openbfme_importer import retail_screen_apt_convert as convert

    pipeline = ImportPipeline.__new__(ImportPipeline)
    real = convert.build_screen_scene

    def stale(root, movie, *, frame=None):
        contract = real(root, movie, frame=frame)
        selection = dict(contract["frameSelection"])
        selection["priority"] = ["_show", "_open", "_init", "_fadeIn"]
        contract["frameSelection"] = selection
        return contract

    convert.build_screen_scene = stale
    try:
        with pytest.raises(RuntimeError, match="frame priority is not the declared"):
            _convert(pipeline, "SpellStore", tmp_path / "stale")
    finally:
        convert.build_screen_scene = real


def test_a_contract_whose_label_contradicts_its_labels_is_refused(
    tmp_path: Path,
) -> None:
    """A frame chosen by anything other than the declared priority is refused."""

    from openbfme_importer import retail_screen_apt_convert as convert

    pipeline = ImportPipeline.__new__(ImportPipeline)
    real = convert.build_screen_scene

    def lying(root, movie, *, frame=None):
        contract = real(root, movie, frame=frame)
        selection = dict(contract["frameSelection"])
        # Claim the authored-open rule while binding a different label.
        selection["label"] = "_close"
        selection["frame"] = 99
        contract["frameSelection"] = selection
        return contract

    convert.build_screen_scene = lying
    try:
        with pytest.raises(RuntimeError, match="where the declared priority selects"):
            _convert(pipeline, "SpellStore", tmp_path / "lying")
    finally:
        convert.build_screen_scene = real


def test_an_absolute_source_path_cannot_escape_the_staging_tree(
    tmp_path: Path,
) -> None:
    """A drive-qualified virtual path is the escape a prefix check misses."""

    from openbfme_importer.retail_screen_apt_convert import (
        ScreenAptConvertError,
        convert_screen_apt_bundle,
    )

    for hostile in ("C:/Windows/evil.apt", "../escape.apt", "/etc/passwd", "~/x.apt"):
        with pytest.raises(ScreenAptConvertError, match="unsafe"):
            convert_screen_apt_bundle(
                {hostile: b"stub"}, "SpellStore", tmp_path / hostile[:3].strip(":/")
            )
