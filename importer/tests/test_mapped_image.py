from __future__ import annotations

import unittest

try:
    from openbfme_importer.mapped_image import (
        MAX_MAPPED_IMAGE_BLOCKS,
        MappedImageRecord,
        parse_mapped_images,
        resolve_mapped_image_texture_paths,
        resolve_mapped_image_texture_paths_partial,
        resolve_mapped_images,
        resolve_mapped_images_partial,
    )
except ModuleNotFoundError as exc:
    if exc.name != "openbfme_importer":
        raise
    from importer.openbfme_importer.mapped_image import (
        MAX_MAPPED_IMAGE_BLOCKS,
        MappedImageRecord,
        parse_mapped_images,
        resolve_mapped_image_texture_paths,
        resolve_mapped_image_texture_paths_partial,
        resolve_mapped_images,
        resolve_mapped_images_partial,
    )


def _block(
    image_id: str = "Portrait_Beta",
    *,
    texture: str = "UI\\Portrait Atlas.tga",
    width: str = "256",
    height: str = "128",
    coords: str = "Left:64 Top:0 Right:128 Bottom:64",
) -> bytes:
    return (
        f"MappedImage {image_id}\n"
        f"  Texture = \"{texture}\"\n"
        f"  TextureWidth = {width}\n"
        f"  TextureHeight = {height}\n"
        f"  Coords = {coords}\n"
        "  Status = NONE\n"
        "End\n"
    ).encode("cp1252")


class MappedImageTests(unittest.TestCase):
    def test_parse_returns_canonical_payload_free_records(self) -> None:
        source = (
            b"; synthetic fixture\n"
            + _block()
            + _block(
                "portrait_alpha",
                texture="UI/Atlas.tga",
                coords="Bottom:32, Right:48, Top:16, Left:8",
            )
        )
        records = parse_mapped_images(source)

        self.assertEqual([record.id for record in records], ["portrait_alpha", "Portrait_Beta"])
        self.assertEqual(
            records[1],
            MappedImageRecord(
                id="Portrait_Beta",
                texture="UI/Portrait Atlas.tga",
                texture_width=256,
                texture_height=128,
                left=64,
                top=0,
                right=128,
                bottom=64,
            ),
        )
        self.assertEqual(
            records[1].neutral(),
            {
                "id": "Portrait_Beta",
                "texture": "UI/Portrait Atlas.tga",
                "textureWidth": 256,
                "textureHeight": 128,
                "coords": {"left": 64, "top": 0, "right": 128, "bottom": 64},
            },
        )
        self.assertFalse(hasattr(records[1], "source"))
        self.assertFalse(hasattr(records[1], "source_path"))

    def test_source_proven_identifier_and_colonless_coord_forms(self) -> None:
        records = parse_mapped_images(
            _block("Hero'sPortrait")
            + _block(
                "Large-Portrait Alternate",
                coords="Left:8 Top:16 Right:48 Bottom 32",
            )
        )
        self.assertEqual(
            [record.id for record in records],
            ["Hero'sPortrait", "Large-Portrait Alternate"],
        )
        self.assertEqual(records[1].bottom, 32)
        self.assertEqual(
            resolve_mapped_images(
                [_block("Large-Portrait Alternate")],
                ["large-portrait alternate"],
            )[0].id,
            "Large-Portrait Alternate",
        )

        with self.assertRaisesRegex(ValueError, "too many colonless Coords fields"):
            parse_mapped_images(
                _block(coords="Left 8 Top 16 Right:48 Bottom:32")
            )
        with self.assertRaisesRegex(ValueError, "malformed MappedImage header"):
            parse_mapped_images(b"MappedImage Invalid:Identifier\nEnd\n")
        with self.assertRaisesRegex(ValueError, "unsafe MappedImage request identifier"):
            resolve_mapped_images([_block("One")], ["One "])

    def test_resolver_is_case_insensitive_exact_and_deterministic(self) -> None:
        first = _block("Zulu", texture="z.tga") + _block("Alpha", texture="a.tga")
        second = _block("Unused", texture="unused.tga")
        forward = resolve_mapped_images([first, second], ["zULU", "alpha"])
        reverse = resolve_mapped_images([second, first], ["alpha", "ZULU"])
        self.assertEqual(forward, reverse)
        self.assertEqual([item.id for item in forward], ["Alpha", "Zulu"])

    def test_compiled_texture_paths_follow_exact_bfme2_convention(self) -> None:
        records = parse_mapped_images(
            _block("One", texture="StrategicImages_001.tga")
            + _block("Two", texture="BuildingRadialButtons_168.tga")
        )
        resolved = resolve_mapped_image_texture_paths(
            records,
            [
                "art/compiledtextures/bu/BuildingRadialButtons_168.dds",
                "art/compiledtextures/st/StrategicImages_001.dds",
                "elsewhere/StrategicImages_001.dds",
            ],
        )
        self.assertEqual(
            resolved,
            {
                "BuildingRadialButtons_168.tga": (
                    "art/compiledtextures/bu/BuildingRadialButtons_168.dds"
                ),
                "StrategicImages_001.tga": (
                    "art/compiledtextures/st/StrategicImages_001.dds"
                ),
            },
        )
        with self.assertRaisesRegex(ValueError, "unresolved MappedImage compiled texture"):
            resolve_mapped_image_texture_paths(records, [])
        partial, missing = resolve_mapped_image_texture_paths_partial(
            records,
            ["art/compiledtextures/st/StrategicImages_001.dds"],
        )
        self.assertEqual(
            partial,
            {
                "StrategicImages_001.tga": (
                    "art/compiledtextures/st/StrategicImages_001.dds"
                )
            },
        )
        self.assertEqual(missing, ("BuildingRadialButtons_168.tga",))

        authored = parse_mapped_images(_block("Strategic", texture="AptComponents_005.tga"))
        self.assertEqual(
            resolve_mapped_image_texture_paths(
                authored,
                ["art/compiledtextures/ap/aptcomponents_005.tga"],
            ),
            {"AptComponents_005.tga": "art/compiledtextures/ap/aptcomponents_005.tga"},
        )

    def test_rejects_duplicate_and_ambiguous_definitions(self) -> None:
        with self.assertRaisesRegex(ValueError, "duplicate MappedImage definition"):
            parse_mapped_images(_block("One") + _block("oNe"))
        with self.assertRaisesRegex(ValueError, "duplicate Texture field"):
            parse_mapped_images(
                _block().replace(b"  TextureWidth", b"  Texture = other.tga\n  TextureWidth")
            )
        # Duplicate definitions resolve to the LAST parsed one — retail's
        # INI::parseMappedImageDefinition re-initializes the SAME image object
        # on a name hit (decomp oracle INIMappedImage.cpp), so later wins,
        # across documents and inside one document alike.
        self.assertEqual(
            resolve_mapped_images(
                [_block("One", texture="a.tga"), _block("ONE", texture="b.tga")],
                ["one"],
            )[0].texture,
            "b.tga",
        )
        self.assertEqual(
            resolve_mapped_images(
                [_block("One", texture="a.tga") + _block("ONE", texture="b.tga")],
                ["one"],
            )[0].texture,
            "b.tga",
        )
        self.assertEqual(
            resolve_mapped_images(
                [_block("One") + _block("Unused") + _block("UNUSED")], ["one"]
            )[0].id,
            "One",
        )
        with self.assertRaisesRegex(ValueError, "unresolved MappedImage definition"):
            resolve_mapped_images([_block("One")], ["Missing"])

    def test_partial_resolution_keeps_exact_gap_diagnostics(self) -> None:
        resolution = resolve_mapped_images_partial(
            [_block("One") + _block("Duplicate"), _block("DUPLICATE")],
            ["Missing", "duplicate", "one"],
        )
        self.assertEqual(
            sorted(record.id for record in resolution.records), ["DUPLICATE", "One"]
        )
        self.assertEqual(resolution.missing_ids, ("Missing",))
        # last-parsed wins (retail semantics); nothing is ambiguous any more
        self.assertEqual(resolution.ambiguous_ids, ())

    def test_rejects_unsafe_texture_paths_and_missing_fields(self) -> None:
        for texture in (
            "../atlas.tga",
            "/root/atlas.tga",
            "C:\\atlas.tga",
            "UI/../atlas.tga",
            "UI/NUL.tga",
        ):
            with self.subTest(texture=texture), self.assertRaisesRegex(ValueError, "unsafe Texture path"):
                parse_mapped_images(_block(texture=texture))
        with self.assertRaisesRegex(ValueError, "missing required field.*textureheight"):
            parse_mapped_images(
                _block().replace(b"  TextureHeight = 128\n", b"")
            )

    def test_rejects_invalid_dimensions_and_crops(self) -> None:
        cases = [
            ("nonfinite-width", {"width": "nan"}, "nonfinite TextureWidth"),
            ("negative-width", {"width": "-1"}, "invalid TextureWidth"),
            ("zero-height", {"height": "0"}, "invalid TextureHeight"),
            ("fractional", {"width": "255.5"}, "non-integral TextureWidth"),
            (
                "nonfinite-coord",
                {"coords": "Left:0 Top:0 Right:NaN Bottom:32"},
                "nonfinite right",
            ),
            (
                "negative-coord",
                {"coords": "Left:-1 Top:0 Right:32 Bottom:32"},
                "negative crop coordinates",
            ),
            (
                "inverted",
                {"coords": "Left:32 Top:0 Right:31 Bottom:32"},
                "inverted or empty crop",
            ),
            (
                "out-of-bounds",
                {"coords": "Left:0 Top:0 Right:257 Bottom:32"},
                "out of bounds",
            ),
            (
                "duplicate-coord",
                {"coords": "Left:0 Left:1 Top:0 Right:32 Bottom:32"},
                "duplicate Coords field",
            ),
            (
                "missing-coord",
                {"coords": "Left:0 Top:0 Right:32"},
                "missing Coords field",
            ),
        ]
        for name, kwargs, message in cases:
            with self.subTest(name=name), self.assertRaisesRegex(ValueError, message):
                parse_mapped_images(_block(**kwargs))

    def test_rejects_unclosed_malformed_nul_and_excessive_inputs(self) -> None:
        with self.assertRaisesRegex(ValueError, "unterminated MappedImage block"):
            parse_mapped_images(_block()[:-4])
        with self.assertRaisesRegex(ValueError, "unterminated MappedImage"):
            parse_mapped_images(_block()[:-4] + _block("Second"))
        with self.assertRaisesRegex(ValueError, "malformed MappedImage header"):
            parse_mapped_images(b"MappedImage\nEnd\n")
        with self.assertRaisesRegex(ValueError, "NUL"):
            parse_mapped_images(_block() + b"\0")
        excessive = b"".join(
            _block(f"Image{index}", texture="atlas.tga")
            for index in range(MAX_MAPPED_IMAGE_BLOCKS + 1)
        )
        with self.assertRaisesRegex(ValueError, "block count exceeds limit"):
            parse_mapped_images(excessive)
        with self.assertRaisesRegex(ValueError, "duplicate MappedImage request identifier"):
            resolve_mapped_images([_block("One")], ["One", "one"])


if __name__ == "__main__":
    unittest.main()
