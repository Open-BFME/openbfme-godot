"""Retail SAGE Geometry projection used for mouse picking.

SAGE authors every Object's selection/collision volume as a ``Geometry`` block
(``BOX``/``CYLINDER``/``SPHERE`` with ``GeometryMajorRadius``,
``GeometryMinorRadius``, ``GeometryHeight``) plus optional
``AdditionalGeometry`` pieces carrying a ``GeometryOffset``. Retail hit-tests a
click against that footprint. Without it in the compiled documents a runtime has
nothing to pick against but a guessed world-unit radius.

Oracle (PURE RETAIL rotwk tree,
``.private/retail-work/editions/rotwk/cache/effective-assets/data/ini``):
``object/goodfaction/structures/men/barracks.ini`` authors a CYLINDER 8.0 rally
probe plus four ``AdditionalGeometry`` BOX pieces -- 20 x 20 at offset
``X:-22 Y:-30`` and 45 x 50 at the origin, per model-state family -- so the
union footprint is 45 x 50 source half-extents.
"""

from __future__ import annotations

import unittest

from openbfme_importer.playable_structure_compiler import (
    compile_playable_structure_descriptor,
)
from openbfme_importer.playable_unit_compiler import (
    _geometry_footprint,
    _geometry_offset,
    compile_playable_unit_descriptor,
)

from importer.tests.test_playable_structure_compiler import _structure_documents


_BARRACKS_GEOMETRY = """
  Geometry              = CYLINDER
  GeometryMajorRadius   = 8.0
  GeometryMinorRadius   = 8.0
  GeometryHeight        = 10

  AdditionalGeometry    = BOX
  GeometryName          = Geom_Orig
  GeometryMajorRadius   = 20.0
  GeometryMinorRadius   = 20.0
  GeometryHeight        = 75.0
  GeometryOffset        = X:-22 Y:-30 Z:0

  AdditionalGeometry    = BOX
  GeometryName          = Geom_Orig
  GeometryMajorRadius   = 45.0
  GeometryMinorRadius   = 50.0
  GeometryHeight        = 40.0
  GeometryOffset        = X:0 Y:0 Z:0

  GeometryIsSmall       = No
"""

_INFANTRY_GEOMETRY = """
  Geometry = CYLINDER
  GeometryMajorRadius = 8.0
  GeometryMinorRadius = 8.0
  GeometryHeight = 19.2
  GeometryIsSmall = Yes
"""


def _with_object_geometry(
    documents: dict[str, bytes], object_name: str, geometry: str
) -> dict[str, bytes]:
    """Insert a Geometry block as the first body line of a fixture Object."""

    path = "data/ini/object/units/test_units.ini"
    text = documents[path].decode("utf-8")
    marker = f"\nObject {object_name}\n"
    if marker not in text:
        raise AssertionError(f"fixture has no Object {object_name}")
    patched = text.replace(marker, marker + geometry, 1)
    updated = dict(documents)
    updated[path] = patched.encode("utf-8")
    return updated


class GeometryOffsetTests(unittest.TestCase):
    def test_parses_authored_axis_triple(self) -> None:
        self.assertEqual(
            _geometry_offset("X:-22 Y:-30 Z:0"),
            {"x": -22.0, "y": -30.0, "z": 0.0},
        )

    def test_missing_axes_default_to_zero(self) -> None:
        self.assertEqual(_geometry_offset("X:12"), {"x": 12.0, "y": 0.0, "z": 0.0})

    def test_unparseable_offset_is_refused(self) -> None:
        self.assertIsNone(_geometry_offset("X:left Y:0 Z:0"))

    def test_empty_offset_is_absent(self) -> None:
        self.assertIsNone(_geometry_offset("   "))


class GeometryFootprintTests(unittest.TestCase):
    def test_barracks_union_matches_retail_half_extents(self) -> None:
        pieces = [
            {
                "role": "primary",
                "shape": "CYLINDER",
                "majorRadius": {"authored": "8.0", "value": 8.0},
                "minorRadius": {"authored": "8.0", "value": 8.0},
            },
            {
                "role": "additional",
                "shape": "BOX",
                "majorRadius": {"authored": "20.0", "value": 20.0},
                "minorRadius": {"authored": "20.0", "value": 20.0},
                "offset": {"x": -22.0, "y": -30.0, "z": 0.0},
            },
            {
                "role": "additional",
                "shape": "BOX",
                "majorRadius": {"authored": "45.0", "value": 45.0},
                "minorRadius": {"authored": "50.0", "value": 50.0},
                "offset": {"x": 0.0, "y": 0.0, "z": 0.0},
            },
        ]
        self.assertEqual(
            _geometry_footprint(pieces),
            {"majorRadius": 45.0, "minorRadius": 50.0, "radius": 50.0},
        )

    def test_radius_is_the_largest_half_extent_not_the_half_diagonal(self) -> None:
        # A half-diagonal circle over 45 x 50 would be 67.3 source units and
        # bulge well past the silhouette -- exactly the over-picking this
        # projection exists to end.
        footprint = _geometry_footprint(
            [
                {
                    "majorRadius": {"authored": "45.0", "value": 45.0},
                    "minorRadius": {"authored": "50.0", "value": 50.0},
                }
            ]
        )
        assert footprint is not None
        self.assertEqual(footprint["radius"], 50.0)

    def test_unresolved_scalars_leave_the_piece_unmeasured(self) -> None:
        self.assertIsNone(
            _geometry_footprint(
                [{"majorRadius": {"authored": "SOME_UNRESOLVED_CONSTANT"}}]
            )
        )

    def test_no_pieces_yield_no_footprint(self) -> None:
        self.assertIsNone(_geometry_footprint([]))


class StructureGeometryContractTests(unittest.TestCase):
    def test_structure_descriptor_carries_the_authored_footprint(self) -> None:
        documents = _with_object_geometry(
            _structure_documents(), "TestKeep", _BARRACKS_GEOMETRY
        )

        descriptor = compile_playable_structure_descriptor("TestKeep", documents)

        geometry = descriptor["gameplay"]["geometry"]
        self.assertEqual(geometry["objectId"], "TestKeep")
        self.assertEqual(geometry["shape"], "CYLINDER")
        self.assertEqual(geometry["majorRadius"], {"authored": "8.0", "value": 8.0})
        self.assertIs(geometry["isSmall"], False)
        self.assertEqual(
            geometry["footprint"],
            {"majorRadius": 45.0, "minorRadius": 50.0, "radius": 50.0},
        )
        roles = [piece["role"] for piece in geometry["pieces"]]
        self.assertEqual(roles, ["primary", "additional", "additional"])
        self.assertEqual(geometry["pieces"][1]["name"], "Geom_Orig")
        self.assertEqual(
            geometry["pieces"][1]["offset"], {"x": -22.0, "y": -30.0, "z": 0.0}
        )

    def test_structure_without_geometry_carries_no_row(self) -> None:
        descriptor = compile_playable_structure_descriptor(
            "TestKeep", _structure_documents()
        )

        self.assertNotIn("geometry", descriptor["gameplay"])


class UnitGeometryContractTests(unittest.TestCase):
    def test_horde_descriptor_carries_the_member_footprint(self) -> None:
        # The horde container authors no Geometry of its own; retail picks a
        # horde by hit-testing one member's body.
        documents = _with_object_geometry(
            _structure_documents(), "InfantryMember", _INFANTRY_GEOMETRY
        )

        descriptor = compile_playable_unit_descriptor("InfantryHorde", documents)

        geometry = descriptor["gameplay"]["geometry"]
        self.assertEqual(geometry["objectId"], "InfantryMember")
        self.assertEqual(geometry["shape"], "CYLINDER")
        self.assertIs(geometry["isSmall"], True)
        self.assertEqual(geometry["height"], {"authored": "19.2", "value": 19.2})
        self.assertEqual(
            geometry["footprint"],
            {"majorRadius": 8.0, "minorRadius": 8.0, "radius": 8.0},
        )

    def test_unit_without_geometry_carries_no_row(self) -> None:
        descriptor = compile_playable_unit_descriptor(
            "InfantryHorde", _structure_documents()
        )

        self.assertNotIn("geometry", descriptor["gameplay"])


if __name__ == "__main__":  # pragma: no cover - direct execution helper
    unittest.main()
