"""Tests for the xfer inventory scanner on hand-assembled x86 (the code shapes MSVC emits in RotWK),
plus pins on the real binary that SKIP LOUDLY without RW_GAME_DAT and the xmap."""
import os
import struct
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import xfer_inventory as xi  # noqa: E402

VA = 0x500000


def rel32(at, target):
    """E8 rel32 from instruction address `at` (5 bytes long)."""
    return b"\xe8" + struct.pack("<i", target - (at + 5))


def seq(rep):
    return [c.text for c in rep.calls]


def test_version_helper_shape_rw_404f15():
    # push ebp; mov ebp,esp; push ecx; mov eax,[ecx]; lea edx,[ebp-4]; push edx;
    # mov byte [ebp-4],1; mov byte [ebp-3],1; call [eax+0x28]; leave; ret
    code = bytes.fromhex("55 8bec 51 8b01 8d55fc 52 c645fc01 c645fd01 ff5028 c9 c3")
    rep = xi.analyze_function(code, VA, xi.Context(), xfer_in_ecx=True)
    assert rep.versions == [(1, 1)]
    assert seq(rep) == ["ver(1, 1)"]


def test_esp_frame_stores_before_a_push_and_named_callees():
    base_xfer = 0x600000
    code = bytearray()
    code += bytes.fromhex("56")                 # push esi
    code += bytes.fromhex("8b742408")           # mov esi,[esp+8]      the Xfer*
    code += bytes.fromhex("51")                 # push ecx             a 4-byte local
    code += bytes.fromhex("c6042402")           # mov byte [esp],2     earliest
    code += bytes.fromhex("c644240105")         # mov byte [esp+1],5   current
    code += bytes.fromhex("8b06")               # mov eax,[esi]
    code += bytes.fromhex("8d0c24")             # lea ecx,[esp]
    code += bytes.fromhex("51")                 # push ecx
    code += bytes.fromhex("8bce")               # mov ecx,esi
    code += bytes.fromhex("ff5028")             # call [eax+0x28]      version
    code += bytes.fromhex("8b06")               # mov eax,[esi]
    code += bytes.fromhex("8d4f10")             # lea ecx,[edi+0x10]
    code += bytes.fromhex("51")                 # push ecx
    code += bytes.fromhex("8bce")               # mov ecx,esi
    code += bytes.fromhex("ff5070")             # call [eax+0x70]      real
    code += bytes.fromhex("8b06")               # mov eax,[esi]
    code += bytes.fromhex("8bce")               # mov ecx,esi
    code += bytes.fromhex("ff506c")             # call [eax+0x6c]      AsciiString
    code += bytes.fromhex("56")                 # push esi
    code += rel32(VA + len(code), base_xfer)    # call Base::xfer
    code += bytes.fromhex("595ec20400")         # pop ecx; pop esi; ret 4
    ctx = xi.Context(xfer_names={base_xfer: "Base::xfer"})
    rep = xi.analyze_function(bytes(code), VA, ctx)
    assert rep.window == (2, 5)
    assert seq(rep) == ["ver(2, 5)", "real", "astr", "->Base::xfer"]
    assert rep.problems == []


def test_word_store_and_register_constants():
    code = bytearray()
    code += bytes.fromhex("55 8bec 83ec08 56")       # push ebp; mov ebp,esp; sub esp,8; push esi
    code += bytes.fromhex("8b7508")                  # mov esi,[ebp+8]
    code += bytes.fromhex("66c745f80103")            # mov word [ebp-8],0x0301  -> (1, 3)
    code += bytes.fromhex("8b06 8d4df8 51 8bce ff5028")
    code += bytes.fromhex("33c0 40")                 # xor eax,eax; inc eax
    code += bytes.fromhex("8845fc")                  # mov [ebp-4],al          earliest 1
    code += bytes.fromhex("c645fd04")                # mov byte [ebp-3],4      current 4
    code += bytes.fromhex("8b06 8d4dfc 51 8bce ff5028")
    code += bytes.fromhex("5e c9 c20400")
    rep = xi.analyze_function(bytes(code), VA, xi.Context())
    assert rep.versions == [(1, 3), (1, 4)]
    assert rep.window == (1, 3)


def test_eh_prolog_frame_rw_697f28_shape():
    code = bytearray()
    code += bytes.fromhex("b8 00 10 b8 00")          # mov eax, handler
    code += rel32(VA + len(code), xi.EH_PROLOG_VA)   # call __EH_prolog
    code += bytes.fromhex("81ec ec000000 53 56")     # sub esp,0xec; push ebx; push esi
    code += bytes.fromhex("8b7508 8b06 8bd9 57")     # mov esi,[ebp+8]; mov eax,[esi]; mov ebx,ecx; push edi
    code += bytes.fromhex("8d4df0 51 8bce")          # lea ecx,[ebp-0x10]; push ecx; mov ecx,esi
    code += bytes.fromhex("c645f001 c645f11d")       # mov byte [ebp-0x10],1; mov byte [ebp-0xf],0x1d
    code += bytes.fromhex("ff5028")                  # call [eax+0x28]
    code += bytes.fromhex("8b06 680c4ec000 8bce ff5014")   # BLOK "BehaviorModule"
    code += bytes.fromhex("8b06 57 8bce ff5030")     # snapshot
    code += bytes.fromhex("8b06 8bce ff5018")        # EBLK
    code += bytes.fromhex("8b06 680c4ec000 8bce ff501c")   # skipBLOK
    code += bytes.fromhex("c3")
    rep = xi.analyze_function(bytes(code), VA, xi.Context())
    assert rep.window == (1, 29)
    assert seq(rep) == ["ver(1, 29)", "BLOK", "snapshot", "EBLK", "skipBLOK"]


def test_helper_call_on_the_xfer_and_an_unreadable_window_is_reported():
    helper = 0x404F15
    other = 0x700000
    code = bytearray()
    code += bytes.fromhex("8b4c2404")                # mov ecx,[esp+4]
    code += rel32(VA + len(code), helper)            # call Version1 (ecx = xfer)
    code += bytes.fromhex("ff742404")                # push dword [esp+4]  (the xfer)
    code += rel32(VA + len(code), other)             # call a helper that takes the xfer
    code += bytes.fromhex("8b442404 8b10 8d4c2408 51 8bc8 ff5228")  # version with no stores
    code += bytes.fromhex("c20400")
    ctx = xi.Context(version_helpers={helper: (1, 1)}, func_names={other: "XferObjectID"})
    rep = xi.analyze_function(bytes(code), VA, ctx)
    assert seq(rep)[:2] == ["ver(1, 1)", "call:XferObjectID"]
    assert rep.versions[1] is None
    assert rep.problems and rep.problems[0].startswith("version window not read at")


def test_calls_on_other_objects_are_not_transfers():
    # the Xfer is on the stack, but a call through some other object's vtable +0x28 is not a version
    code = bytes.fromhex("8b01 8d542404 52 ff5028 c20400")   # mov eax,[ecx]; lea edx,[esp+4]; push edx; call [eax+0x28]
    rep = xi.analyze_function(code, VA, xi.Context())
    assert rep.versions == [] and rep.calls == []


def test_compress_runs():
    calls = [xi.Call("slot", t, 0) for t in ("u16", "u16", "u16", "astr", "u16")]
    assert xi.compress(calls) == "u16x3 astr u16"


# ------------------------------------------------------------------------------- the real binary

def _retail():
    dat = os.environ.get("RW_GAME_DAT")
    xmap = os.environ.get("OPENBFME_XMAP") or str(xi.main_checkout() / "workspace/rebuild/xmap/rw_map.csv")
    if not dat or not Path(dat).is_file() or not Path(xmap).is_file():
        pytest.skip("SKIP: RW_GAME_DAT (RotWK 2.01 game.dat) or the xmap rw_map.csv is missing; "
                    "the retail pins of the xfer inventory did not run")
    return xi.Binary(dat), xi.load_map(xmap)


@pytest.fixture(scope="module")
def inventory():
    binary, rows = _retail()
    inv, helpers = xi.build_inventory(binary, rows)
    return {r["_va"]: rep for r, rep in inv}, helpers


def test_retail_version_windows(inventory):
    """Pins read off the RotWK disassembly by hand (SAVES-RETAIL.md cites each)."""
    inv, helpers = inventory
    assert helpers[0x404F15] == (1, 1)                       # Xfer::Version1
    assert inv[0x697F28].window == (1, 29)                   # Object::xfer (BFME2: 26)
    assert inv[0x82B8E4].window == (1, 2)                    # GameStateMap::DoXfer
    assert inv[0x6DEFD0].window == (1, 3)                    # SaveGameInfo (CHUNK_GameState)
    assert inv[0x46253F].window == (1, 1)                    # W3DTerrainLogic::xfer
    assert inv[0x6DCE1C].window == (1, 1)                    # GameState::xfer (CHUNK_GameState)
    assert [c.text for c in inv[0x6DCE1C].calls] == ["ver(1, 1)", "snapshot"]


def test_retail_object_xfer_keeps_the_module_skip(inventory):
    inv, _ = inventory
    s = [c.text for c in inv[0x697F28].calls]
    i = s.index("skipBLOK")
    # save arm: tag name, BLOK, the module, EBLK; load arm: tag name, then skip (module gone) or read it
    assert s[i - 5:i + 4] == ["astr", "BLOK", "snapshot", "EBLK", "astr", "skipBLOK", "BLOK", "snapshot", "EBLK"]


def test_retail_save_game_info_order(inventory):
    inv, _ = inventory
    s = [c.text for c in inv[0x6DEFD0].calls][:18]
    # both arms of `if (version < 2)` appear: the old astr + i32 before IsAutoSaveOrNot
    assert s == ["ver(1, 3)", "call:XferSaveFileType", "astr"] + ["u16"] * 8 + \
        ["ustr", "astr", "astr", "i32", "call:Rva002DB9EFGet", "ustr", "ustr"]
