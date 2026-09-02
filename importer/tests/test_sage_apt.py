from __future__ import annotations

import hashlib
import struct
import unittest

from openbfme_importer.sage_apt import (
    AptParseError,
    parse_apt_constants,
    parse_apt_dat,
    parse_apt_geometry,
    parse_apt_movie,
    parse_tga_identity,
    parse_wnd_layout,
)


def _const(entry_offset: int = 12, strings: tuple[str, ...] = ()) -> bytes:
    data = bytearray(b"Apt constant file\x1a\x00\x00")
    data.extend(struct.pack("<III", entry_offset, len(strings), 32))
    string_offset = 32 + len(strings) * 8
    encoded = []
    for value in strings:
        raw = value.encode("cp1252") + b"\0"
        data.extend(struct.pack("<II", 1, string_offset))
        encoded.append(raw)
        string_offset += len(raw)
    for raw in encoded:
        data.extend(raw)
    return bytes(data)


def _apt(*, with_action: bool = False, duplicate_exports: bool = False) -> bytes:
    data = bytearray(384)
    data[:12] = b"Apt Data:7\x1a\x00"
    entry = 12
    struct.pack_into("<II", data, entry, 9, 0x09876543)
    movie = entry + 8
    frame_table = 80
    character_table = 96
    import_table = 100
    export_table = 116
    string_a = 180
    string_b = 196
    item_table = 220
    item = 240
    struct.pack_into("<iI", data, movie, 1, frame_table)
    struct.pack_into("<I", data, movie + 8, 0)
    struct.pack_into("<iI", data, movie + 12, 1, character_table)
    struct.pack_into("<III", data, movie + 20, 1024, 768, 33)
    struct.pack_into("<iI", data, movie + 32, 1, import_table)
    struct.pack_into("<iI", data, movie + 40, 2 if duplicate_exports else 1, export_table)
    struct.pack_into("<iI", data, frame_table, 1 if with_action else 0, item_table)
    struct.pack_into("<I", data, character_table, entry)
    struct.pack_into("<IIII", data, import_table, string_a, string_b, 4, 0)
    struct.pack_into("<II", data, export_table, string_b, 4)
    if duplicate_exports:
        struct.pack_into("<II", data, export_table + 8, string_b, 7)
    data[string_a : string_a + 8] = b"Library\0"
    data[string_b : string_b + 7] = b"Symbol\0"
    if with_action:
        struct.pack_into("<I", data, item_table, item)
        struct.pack_into("<II", data, item, 1, 260)
    return bytes(data)


class AptParserTests(unittest.TestCase):
    def test_const_dat_geometry_tga_and_wnd_are_canonical(self) -> None:
        constants = parse_apt_constants(
            _const(strings=("Zed", "alpha")), "fixture.const"
        )
        self.assertEqual(constants["strings"], ["alpha", "Zed"])

        dat = parse_apt_dat(
            b"; authored\r\n17=0 0 15 32\r\n289->1\r\n", "fixture.dat"
        )
        self.assertEqual(dat["mappingCount"], 2)
        self.assertEqual(dat["mappings"][0]["bounds"], [0, 0, 15, 32])

        geometry = parse_apt_geometry(
            b"s tc:255:255:255:255:1:1:0:0:1:0:0\n"
            b"t0:0:10:0:0:10\nc\n",
            "fixture_geometry/1.ru",
        )
        self.assertEqual(geometry["triangleCount"], 1)
        self.assertEqual(geometry["imageIds"], [1])
        self.assertEqual(geometry["bounds"], [0.0, 0.0, 10.0, 10.0])

        tga = bytearray(22)
        struct.pack_into("<BBB", tga, 0, 0, 0, 2)
        struct.pack_into("<HHBB", tga, 12, 1, 1, 32, 0x28)
        self.assertEqual(parse_tga_identity(bytes(tga), "atlas.tga")["alphaBits"], 8)

        wnd = parse_wnd_layout(
            b"FILE_VERSION = 2;\nWINDOW\n"
            b"WINDOWTYPE = USER;\n"
            b"SCREENRECT = UPPERLEFT: 0 416, BOTTOMRIGHT: 800 600, "
            b"CREATIONRESOLUTION: 800 600;\n"
            b'NAME = "ControlBar.wnd:Root";\nSTATUS = ENABLED+BORDER;\n'
            b"STYLE = USER;\nSYSTEMCALLBACK = \"RootSystem\";\n"
            b"INPUTCALLBACK = \"[None]\";\nTOOLTIPCALLBACK = \"[None]\";\n"
            b"DRAWCALLBACK = \"RootDraw\";\nCHILD\nWINDOW\n"
            b"WINDOWTYPE = USER;\n"
            b"SCREENRECT = UPPERLEFT: 1 2, BOTTOMRIGHT: 3 4, "
            b"CREATIONRESOLUTION: 800 600;\n"
            b'NAME = "ControlBar.wnd:Child";\nSTATUS = ENABLED;\nSTYLE = USER;\n'
            b"SYSTEMCALLBACK = \"[None]\";\nINPUTCALLBACK = \"[None]\";\n"
            b"TOOLTIPCALLBACK = \"[None]\";\nDRAWCALLBACK = \"[None]\";\n"
            b"END\nEND\n",
            "window/controlbar.wnd",
        )
        self.assertEqual(wnd["windowCount"], 2)
        self.assertEqual(wnd["windows"][1]["parentIndex"], 0)
        self.assertEqual(wnd["callbacks"], ["RootDraw", "RootSystem"])

    def test_movie_structure_and_script_handoff_are_bounded(self) -> None:
        constants = parse_apt_constants(_const(), "fixture.const")
        data = _apt(with_action=True)
        movie = parse_apt_movie(data, constants, "fixture.apt")
        self.assertEqual(movie["root"]["frameCount"], 1)
        self.assertEqual(movie["imports"][0]["movie"], "Library")
        self.assertEqual(movie["exports"][0]["symbol"], "Symbol")
        handoff = movie["timelines"][0]["items"][0]["handoff"]
        start, end = handoff["byteRange"]
        self.assertEqual(handoff["sha256"], hashlib.sha256(data[start:end]).hexdigest())
        self.assertEqual(handoff["semantics"], "action-bytecode-not-executed")
        self.assertEqual(
            movie["unsupportedSemantics"]["actionScriptExecution"], "forbidden"
        )

    def test_duplicate_exports_are_preserved_as_unresolved_source_order(self) -> None:
        movie = parse_apt_movie(
            _apt(duplicate_exports=True),
            parse_apt_constants(_const(), "fixture.const"),
            "fixture.apt",
        )
        self.assertEqual(len(movie["exports"]), 2)
        self.assertEqual(movie["duplicateExports"][0]["characterIdsInSourceOrder"], [4, 7])
        self.assertIn("unresolved", movie["duplicateExports"][0]["semantics"])

    def test_bounds_malformed_records_and_unknown_execution_fail_closed(self) -> None:
        with self.assertRaisesRegex(AptParseError, "CONST size"):
            parse_apt_constants(b"short", "bad.const")
        with self.assertRaisesRegex(AptParseError, "unsupported DAT"):
            parse_apt_dat(b"1=https://example.invalid/x\n", "bad.dat")
        with self.assertRaisesRegex(AptParseError, "triangle outside"):
            parse_apt_geometry(b"t0:0:1:0:0:1\n", "bad.ru")
        bad = bytearray(_apt())
        struct.pack_into("<I", bad, 16, 0)
        with self.assertRaisesRegex(AptParseError, "signature"):
            parse_apt_movie(
                bytes(bad), parse_apt_constants(_const(), "fixture.const"), "bad.apt"
            )
        with self.assertRaisesRegex(AptParseError, "authored resolution"):
            parse_wnd_layout(
                b"FILE_VERSION = 2;\nWINDOW\nWINDOWTYPE = USER;\n"
                b"SCREENRECT = UPPERLEFT: 0 0, BOTTOMRIGHT: 1 1, "
                b"CREATIONRESOLUTION: 0 1080;\n",
                "bad.wnd",
            )


if __name__ == "__main__":
    unittest.main()
