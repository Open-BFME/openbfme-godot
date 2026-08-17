"""The region-geometry converter, and the two ways it can be quietly wrong.

RETAIL SEPARATES ITS FIVE REGION EFFECTS BY GEOMETRY, NOT BY COLOUR.
``data/ini/livingworldregioneffects.ini`` names a different model for each:
``BordersEffect``/``LMR_Border`` and ``FilledOwnershipEffect``/``LMR_Fill`` for
ownership, ``RegionSelectionEffect``/``LMR_Edge`` for selection, and
``HomeRegionHighlight``/``LMR_Highlight`` for the home region. The bundle used to
carry only fill, border and territory, so a consumer had nothing to draw the last
two with and recoloured the ownership band instead. These tests hold the fix.

THE TWO FAILURES WORTH TESTING FOR are both silent:

1. A LAYER THAT SILENTLY COVERS FEWER REGIONS. `fill` covers 52 of retail's
   regions; a layer that covers 40 would still produce a bundle, still load, and
   simply leave twelve regions unmarked when selected. So the converter records
   PER-LAYER coverage and these tests assert a short layer is named rather than
   folded into the union figure.
2. THE WRONG FILE. ``art/w3d/lm/lmr_seledge.w3d`` is shipped and is named like a
   selection edge. It is not one: no effect references ``LMR_SelEdge``, RotWK
   ships no override (the catalog resolves it to the BFME2 layer), and its meshes
   are BFME2's region set. The converter carries that as a NAMED GAP and refuses
   to ship the layer; the regression pin at the bottom of this file re-measures
   all three claims against the real archives so the gap text cannot go stale.

Every synthetic test builds meshes through a fake catalog reader and a stubbed
decoder, because a real ``.w3d`` fixture would test the W3D scanner (which has
its own suite) rather than this module's matching and coverage logic.
"""

from __future__ import annotations

import json
import pathlib
import struct

import pytest

from openbfme_importer import livingmap_regions as regions
from openbfme_importer.livingmap_bundle import SubObject

# --- synthetic catalog --------------------------------------------------------


class _Entry:
    def __init__(self, name: str, size: int) -> None:
        self.name = name
        self.archive = "synthetic.big"
        self.offset = 0
        self.size = size
        self.precedence = 0


class _Reader:
    """The catalog surface :func:`build_bundle` uses, over an in-memory map."""

    def __init__(self, payloads: dict[str, bytes]) -> None:
        self._payloads = {key.casefold(): value for key, value in payloads.items()}

    def resolve(self, virtual_path: str):
        payload = self._payloads.get(virtual_path.casefold())
        if payload is None:
            return None
        return _Entry(virtual_path, len(payload))

    def read(self, entry) -> bytes:
        return self._payloads[entry.name.casefold()]


def _square(name: str, size: float = 10.0, offset: float = 0.0) -> SubObject:
    """One flat axis-aligned quad, the smallest thing with a real centroid."""
    positions = [
        offset, offset, 0.0,
        offset + size, offset, 0.0,
        offset + size, offset + size, 0.0,
        offset, offset + size, 0.0,
    ]
    return SubObject(
        name=name,
        bone_index=0,
        bone_name="ROOT",
        translation=(0.0, 0.0, 0.0),
        rotation=(0.0, 0.0, 0.0, 1.0),
        vertex_count=4,
        triangle_count=2,
        positions=positions,
        indices=[0, 1, 2, 0, 2, 3],
        bounds_min=(offset, offset, 0.0),
        bounds_max=(offset + size, offset + size, 0.0),
    )


def _document(region_ids, *, territories=("Arnor",), sub_objects=None) -> dict:
    sub_objects = sub_objects or {}
    return {
        "regionCampaigns": [
            {
                "name": "TheWarOfTheRing",
                "regions": [
                    {
                        "id": region_id,
                        "subObject": sub_objects.get(region_id, region_id),
                        "centerPoint": [1.0, 2.0],
                        "customCenterPoint": True,
                    }
                    for region_id in region_ids
                ],
                "territoryBonuses": [
                    {"effectName": name} for name in territories
                ],
            }
        ]
    }


@pytest.fixture()
def build(tmp_path, monkeypatch):
    """Run ``build_bundle`` over synthetic models. Yields a callable.

    ``models`` maps a LAYER name to the list of mesh names that layer carries.
    """

    def _run(models: dict[str, list[str]], document: dict, layers=None) -> dict:
        payloads = {
            regions.LAYERS[layer]: layer.encode("ascii")
            for layer in models
        }
        decoded = {
            regions.LAYERS[layer]: [_square(name) for name in names]
            for layer, names in models.items()
        }

        monkeypatch.setattr(
            regions, "CatalogReader", lambda _path: _Reader(payloads)
        )
        monkeypatch.setattr(
            regions,
            "decode_livingmap",
            lambda _payload, virtual_path: decoded[virtual_path],
        )

        document_path = tmp_path / "document.json"
        document_path.write_text(json.dumps(document), encoding="utf-8")
        out = tmp_path / "bundle"
        return regions.build_bundle(
            tmp_path / "catalog.json",
            out,
            document_path=document_path,
            layers=tuple(models) if layers is None else layers,
        )

    return _run


# --- the layer table ----------------------------------------------------------


def test_retails_five_effect_geometries_are_all_known_layers():
    """Every model ``livingworldregioneffects.ini`` names must be convertible."""
    assert regions.LAYERS["fill"] == "art/w3d/lm/lmr_fill.w3d"
    assert regions.LAYERS["border"] == "art/w3d/lm/lmr_border.w3d"
    assert regions.LAYERS["edge"] == "art/w3d/lm/lmr_edge.w3d"
    assert regions.LAYERS["highlight"] == "art/w3d/lm/lmr_highlight.w3d"
    assert regions.LAYERS["territory"] == "art/w3d/lm/lmr_regfill.w3d"


def test_selection_and_home_region_geometry_are_converted_by_default():
    """The whole point of the change: a default bundle can draw all five effects.

    Pinned by VALUE rather than by "edge is in there somewhere", because a lane
    that drops one of these back out of the default set silently returns the
    screen to recolouring the ownership band.
    """
    assert regions.DEFAULT_LAYERS == ("fill", "border", "territory", "edge", "highlight")


def test_the_stale_seledge_asset_is_named_and_never_default():
    gap = regions.NAMED_GAPS["SELEDGE_IS_STALE_BFME2_GEOMETRY"]
    assert "lmr_seledge.w3d" in gap
    assert "LMR_SelEdge" in gap
    # The gap must point at the mesh that IS right, or the next lane rediscovers
    # the trap from scratch.
    assert "lmr_edge.w3d" in gap
    assert "seledge" not in regions.DEFAULT_LAYERS
    # Still convertible on request, so the measurement can be re-taken.
    assert regions.LAYERS["seledge"] == "art/w3d/lm/lmr_seledge.w3d"


def test_an_unknown_layer_is_refused_by_name(build):
    with pytest.raises(regions.RegionGeometryError) as error:
        build({"fill": ["MORDOR"]}, _document(["MORDOR"]), layers=("glow",))
    assert "unknown layer 'glow'" in str(error.value)


def test_a_layer_missing_from_the_catalog_is_refused_rather_than_skipped(build):
    with pytest.raises(regions.RegionGeometryError) as error:
        build({"fill": ["MORDOR"]}, _document(["MORDOR"]), layers=("fill", "edge"))
    assert "art/w3d/lm/lmr_edge.w3d is not in the catalog" in str(error.value)
    assert "edge layer" in str(error.value)


# --- per-region matching across the new layers --------------------------------


def test_every_layer_binds_its_mesh_to_the_same_region(build):
    manifest = build(
        {
            "fill": ["MORDOR", "ROHAN"],
            "border": ["MORDOR", "ROHAN"],
            "edge": ["MORDOR", "ROHAN"],
            "highlight": ["MORDOR", "ROHAN"],
        },
        _document(["MORDOR", "ROHAN"]),
    )
    bound = {
        (record["layer"], record["regionId"])
        for record in manifest["records"]
    }
    for layer in ("fill", "border", "edge", "highlight"):
        assert (layer, "MORDOR") in bound
        assert (layer, "ROHAN") in bound


def test_the_subobject_spelling_bridge_applies_to_the_new_layers(build):
    """`Rhudaur`'s mesh is spelled `RHUDAUL` in EVERY model, edge included.

    Only the document's own ``subObject`` link may bridge that. If the bridge
    were fill-only, the selection edge would silently go missing for exactly one
    region - the failure least likely to be noticed.
    """
    manifest = build(
        {"fill": ["RHUDAUL"], "edge": ["RHUDAUL"], "highlight": ["RHUDAUL"]},
        _document(["Rhudaur"], sub_objects={"Rhudaur": "Rhudaul"}),
    )
    for record in manifest["records"]:
        assert record["regionId"] == "Rhudaur", record


def test_a_mesh_matching_no_region_is_listed_and_bound_to_nothing(build):
    """`lmr_edge.w3d` really does carry an authoring leftover named SHAPE32."""
    manifest = build(
        {"edge": ["MORDOR", "SHAPE32"]},
        _document(["MORDOR"]),
    )
    assert manifest["document"]["unmatchedMeshes"] == ["edge:SHAPE32"]
    stray = [r for r in manifest["records"] if r["mesh"] == "SHAPE32"][0]
    assert stray["regionId"] is None
    assert stray["territoryName"] is None


def test_an_impassable_mesh_is_never_matched_to_a_region(build):
    manifest = build(
        {"edge": ["IMPASSABLE"]},
        _document(["IMPASSABLE"]),
    )
    record = manifest["records"][0]
    assert record["impassable"] is True
    assert record["regionId"] is None
    # ...and it is not counted as coverage for the region that shares its name.
    assert manifest["layerCoverage"]["edge"]["regionsMatched"] == 0


# --- per-layer coverage, the thing that makes a short layer visible ------------


def test_a_layer_that_covers_fewer_regions_is_named_per_layer(build):
    """THE FAILURE THIS EXISTS TO CATCH.

    `fill` covers both regions; `edge` covers one. The union figure
    (``regionsMatched``) still says 2, which is correct for "is this region on
    the map" and WRONG for "can I draw its selection". Per-layer coverage is what
    keeps the second question answerable.
    """
    manifest = build(
        {"fill": ["MORDOR", "ROHAN"], "edge": ["MORDOR"]},
        _document(["MORDOR", "ROHAN"]),
    )
    assert manifest["document"]["regionsMatched"] == 2
    assert manifest["document"]["unmatchedRegions"] == []

    coverage = manifest["layerCoverage"]
    assert coverage["fill"]["regionsMatched"] == 2
    assert coverage["fill"]["regionsWithoutMesh"] == []
    assert coverage["edge"]["regionsMatched"] == 1
    assert coverage["edge"]["regionsWithoutMesh"] == ["ROHAN"]


def test_a_region_declared_in_two_campaigns_is_a_hole_only_once(build, tmp_path, monkeypatch):
    """A hole is a REGION, not a (region, campaign) pair.

    Retail's document declares the same region id under more than one campaign,
    and under more than one ``subObject`` spelling. Counting the pairs turned the
    real 38-region hole into a 76-region one - an inflated gap is as dishonest as
    a hidden one, and it is the kind of number that gets quoted.
    """
    document = {
        "regionCampaigns": [
            {
                "name": "TheWarOfTheRing",
                "regions": [{"id": "MORDOR", "subObject": "MORDOR"},
                            {"id": "ROHAN", "subObject": "ROHAN"}],
                "territoryBonuses": [],
            },
            {
                "name": "TheRiseOfTheWitchKing",
                "regions": [{"id": "MORDOR", "subObject": "MORDOR_B"},
                            {"id": "ROHAN", "subObject": "ROHAN"}],
                "territoryBonuses": [],
            },
        ]
    }
    # ROHAN is drawable; MORDOR is declared under TWO subObject spellings and
    # neither has a mesh, so it is ONE hole and must be listed once.
    manifest = build({"fill": ["ROHAN"]}, document)
    coverage = manifest["layerCoverage"]["fill"]
    assert coverage["regionsWithoutMesh"] == ["MORDOR"]
    assert manifest["document"]["unmatchedRegions"] == ["MORDOR"]


def test_coverage_counts_meshes_and_impassable_masses_separately(build):
    manifest = build(
        {"fill": ["MORDOR", "IMPASSABLE01", "IMPASSABLE02"]},
        _document(["MORDOR"]),
    )
    coverage = manifest["layerCoverage"]["fill"]
    assert coverage["meshes"] == 3
    assert coverage["impassableMeshes"] == 2
    assert coverage["regionsMatched"] == 1
    assert coverage["unmatchedMeshes"] == []


def test_the_territory_layer_is_counted_as_territories_not_regions(build):
    """A territory and a region can share a name and are different things."""
    manifest = build(
        {"territory": ["ARNOR"]},
        _document(["MORDOR"], territories=("Arnor",)),
    )
    coverage = manifest["layerCoverage"]["territory"]
    assert coverage["territoriesMatched"] == 1
    assert coverage["regionsMatched"] == 0
    assert coverage["regionsWithoutMesh"] == []
    assert manifest["records"][0]["territoryName"] == "Arnor"
    assert manifest["records"][0]["regionId"] is None


def test_the_named_gaps_reach_the_manifest_verbatim(build):
    manifest = build({"fill": ["MORDOR"]}, _document(["MORDOR"]))
    named = {gap["name"]: gap["text"] for gap in manifest["namedGaps"]}
    assert named == regions.NAMED_GAPS


# --- the geometry block -------------------------------------------------------


def test_every_layers_geometry_is_addressable_in_the_shared_block(build, tmp_path):
    """Offsets must stay honest once four layers share one ``regions.bin``.

    Read back with the same little-endian float32/uint32 contract the Godot
    loader uses, so an offset that drifts is caught here and not as a garbled
    territory on screen.
    """
    manifest = build(
        {"fill": ["MORDOR"], "edge": ["MORDOR"], "highlight": ["MORDOR"]},
        _document(["MORDOR"]),
    )
    blob = (tmp_path / "bundle" / "regions.bin").read_bytes()
    assert len(blob) == manifest["meshBin"]["bytes"]
    seen_offsets = set()
    for record in manifest["records"]:
        assert record["positionBytes"] == record["vertexCount"] * 12
        start = record["positionOffset"]
        floats = struct.unpack_from(
            f"<{record['vertexCount'] * 3}f", blob, start
        )
        assert floats[:3] == (0.0, 0.0, 0.0)
        indices = struct.unpack_from(
            f"<{record['indexCount']}I", blob, record["indexOffset"]
        )
        assert max(indices) < record["vertexCount"]
        # No two meshes may share a slice.
        assert start not in seen_offsets
        seen_offsets.add(start)


def test_the_derived_centroid_is_computed_for_every_layer(build):
    manifest = build(
        {"fill": ["MORDOR"], "edge": ["MORDOR"]},
        _document(["MORDOR"]),
    )
    for record in manifest["records"]:
        assert record["derivedCentroid"] == pytest.approx([5.0, 5.0])


# --- the regression pin against the real archives -----------------------------


_WANTED = (
    ("the RotWK catalog", pathlib.PurePath("catalog", "rotwk.json")),
    (
        "the converted living-world document",
        pathlib.PurePath("reports", "rotwk-living-world.json"),
    ),
)


def workspace_root() -> pathlib.Path | None:
    for parent in pathlib.Path(__file__).resolve().parents:
        root = parent / "workspace" / "retail-work"
        if all((root / relative).is_file() for _what, relative in _WANTED):
            return root
    return None


@pytest.mark.parametrize(
    "layer, virtual_path, meshes, matched",
    [
        ("fill", "art/w3d/lm/lmr_fill.w3d", 62, 52),
        ("border", "art/w3d/lm/lmr_border.w3d", 52, 52),
        ("edge", "art/w3d/lm/lmr_edge.w3d", 54, 52),
        ("highlight", "art/w3d/lm/lmr_highlight.w3d", 52, 52),
        ("territory", "art/w3d/lm/lmr_regfill.w3d", 7, 0),
    ],
)
def test_pin_the_real_layers_cover_retails_own_regions(
    tmp_path, layer, virtual_path, meshes, matched
):
    """MEASURED AGAINST THE REAL ARCHIVES, not against a fixture.

    The claim being pinned is that ``edge`` and ``highlight`` cover EVERY one of
    the 52 playable RotWK regions - the same 52 that ``fill`` and ``border``
    cover. If either fell short, the screen would draw a selection mark for some
    regions and nothing for others, which is worse than the uniform substitute it
    replaced.
    """
    root = workspace_root()
    if root is None:
        pytest.skip(
            "no workspace/retail-work carrying catalog/rotwk.json and "
            "reports/rotwk-living-world.json; this pin measures retail bytes"
        )
    manifest = regions.build_bundle(
        root / "catalog" / "rotwk.json",
        tmp_path / "bundle",
        document_path=root / "reports" / "rotwk-living-world.json",
        layers=(layer,),
    )
    source = manifest["sources"][0]
    assert source["virtualPath"] == virtual_path
    assert source["meshes"] == meshes
    # THE TWO-ASSET-LAYER TRAP. RotWK overrides every one of these models, so the
    # winning entry must come from the RotWK layer. A converter that started
    # serving BFME2 bytes would draw BFME2's region shapes on RotWK's map, and
    # nothing else in the bundle would look wrong.
    assert "rotwk" in source["archive"], source["archive"]
    coverage = manifest["layerCoverage"][layer]
    assert coverage["regionsMatched"] == matched
    # The per-layer hole list must agree with the union one for a layer that
    # covers everything `fill` does, and must be a list of REGIONS rather than of
    # region-campaign pairs (the document declares 38 placeholders across two
    # campaigns, which counted as 76 before this was deduplicated).
    if layer != "territory":
        assert coverage["regionsWithoutMesh"] == manifest["document"]["unmatchedRegions"]
        assert len(coverage["regionsWithoutMesh"]) == 38


def test_pin_the_seledge_gap_still_describes_the_shipped_asset(tmp_path):
    """RE-MEASURE THE GAP rather than trusting its own sentence.

    Three claims are checked against the archives: RotWK ships no override (the
    winner comes from the BFME2 layer), the model does not cover retail's region
    set, and it carries BFME2 region names that are not RotWK regions.
    """
    root = workspace_root()
    if root is None:
        pytest.skip(
            "no workspace/retail-work carrying catalog/rotwk.json and "
            "reports/rotwk-living-world.json; this pin measures retail bytes"
        )
    manifest = regions.build_bundle(
        root / "catalog" / "rotwk.json",
        tmp_path / "bundle",
        document_path=root / "reports" / "rotwk-living-world.json",
        layers=("seledge",),
    )
    source = manifest["sources"][0]
    assert source["virtualPath"] == "art/w3d/lm/lmr_seledge.w3d"
    assert "bfme2" in source["archive"], source["archive"]

    coverage = manifest["layerCoverage"]["seledge"]
    assert coverage["regionsMatched"] == 33
    assert coverage["unmatchedMeshes"] == [
        "ARNOR", "BUCKLAND", "GONDOR", "OSGILIATH", "THE_DEAD_MARSHE",
    ]
    # And the converted `edge` layer is the one with full coverage, which is what
    # the gap text tells a reader to use instead.
    edge = regions.build_bundle(
        root / "catalog" / "rotwk.json",
        tmp_path / "edge-bundle",
        document_path=root / "reports" / "rotwk-living-world.json",
        layers=("edge",),
    )
    assert edge["layerCoverage"]["edge"]["regionsMatched"] == 52
