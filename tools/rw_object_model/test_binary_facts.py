"""Pins of the RotWK binary facts the object-model port cites (engine/src/Common/NameKeyGenerator.h,
Module.h, Thing/*.h). Runs only when RW_GAME_DAT is set; each assertion is one instruction or constant
read from the image, so a different binary (or a wrong citation) fails loudly. Caveat S-001: the image
is the community-modified RotWK game.dat."""
from __future__ import annotations

import os
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent))
pytestmark = pytest.mark.skipif(not os.environ.get("RW_GAME_DAT"), reason="RW_GAME_DAT not set (path of the RotWK game.dat)")


@pytest.fixture(scope="module")
def img():
    from rwimage import Image

    return Image()


def text(img, va, n=12):
    return [f"{i.mnemonic} {i.op_str}" for i in img.decode(va, n * 8)][:n]


def has(img, va, size, *needles):
    """every needle appears in the instructions decoded from va over (at least) `size` bytes"""
    body = [f"{i.mnemonic} {i.op_str}" for i in img.decode(va, max(size, 8) * 2 + 8)]
    return all(any(n in line for line in body) for n in needles)


def test_name_key_hash_is_h33_plus_signed_char(img):
    # RW 0x548538: imul eax,eax,0x21 ; movsx ecx,cl ; add eax,ecx
    t = text(img, 0x548538, 14)
    assert "imul eax, eax, 0x21" in t and "movsx ecx, cl" in t and "add eax, ecx" in t


def test_name_key_socket_count_and_chain_compare(img):
    # RW 0x548820: xor edx,edx ; mov ecx,0xafcf ; div ecx (unsigned)
    t = text(img, 0x548820, 5)
    assert "mov ecx, 0xafcf" in t and "div ecx" in t
    assert 0xAFCF == 45007
    # the chain compare is the CRT strcmp-class function at RW 0xA3CF40 (called with the bucket's name and the key text)
    assert has(img, 0x548831, 0x30, "call 0xa3cf40")


def test_name_key_constructor_zero_init_one(img):
    # constructor RW 0x548BBE stores edi (zeroed by xor edi,edi at 0x548B96) into m_nextID (+0x2bf48)
    assert has(img, 0x548B96, 8, "xor edi, edi")
    assert has(img, 0x548BBE, 8, "mov dword ptr [esi + 0x2bf48], edi")
    # init() RW 0x5486DF stores the constant 1
    assert has(img, 0x5486DF, 10, "mov dword ptr [esi + 0x2bf48], 1")
    # GameEngine::init calls vtable slot 1 (init) right after creating the generator: RW 0x63AE8A call [eax + 4]
    assert has(img, 0x63AE84, 10, "call dword ptr [eax + 4]")
    assert img.u32(0xBE95D0 + 4) == 0x5486AC  # slot 1 of the NameKeyGenerator vtable is init()


def test_new_key_takes_next_id_then_increments(img):
    # RW 0x548875-0x548883: mov ecx,[m_nextID]; mov [node+8],ecx; inc [m_nextID]
    t = text(img, 0x548878, 6)
    assert "lea eax, [edi + 0x2bf48]" in t and "inc dword ptr [eax]" in t


def test_decorated_module_key(img):
    # RW 0x655A0C: buf[0] = type + 0x30; strcpy(buf + 1, name); NAMEKEY(buf)
    t = text(img, 0x655A15, 4)
    assert "add al, 0x30" in t


def test_template_id_counter_starts_at_one_and_is_post_incremented(img):
    assert has(img, 0x6D1ACD, 8, "mov word ptr [esi + 0x10], 1")
    t = text(img, 0x6D2848, 10)
    assert "mov ax, word ptr [ebx + 0x10]" in t and "lea ecx, [eax + 1]" in t and "mov word ptr [edi + 0x5e8], ax" in t


def test_template_object_size_and_default_template_name(img):
    assert has(img, 0x6D27BD, 4, "push 0x650")
    assert img.cstr(0xC18698) == "DefaultThingTemplate"
    assert img.cstr(0xC186EC) == "ChildObject must come after the original Object (%s, %s)."
    assert img.cstr(0xC186B0) == "ObjectReskin must come after the original Object (%s, %s)."


def test_parse_object_definition_load_types(img):
    # RW 0x6D28C7: loadType == 2 marks the new template as an override; 0x6D28E2: type 5 reload; 0x6D2970: type 2 newOverride
    assert has(img, 0x6D28C7, 12, "cmp dword ptr [esi + 8], 2", "mov byte ptr [ebx + 8], 1")
    assert has(img, 0x6D28E2, 6, "cmp eax, 5")
    assert has(img, 0x6D2970, 6, "cmp eax, 2")
    # ChildObject: the field parse runs with load type 4, restored afterwards (RW 0x6D29D9, 0x6D29E5)
    assert has(img, 0x6D29CC, 0x20, "mov dword ptr [esi + 8], 4", "mov dword ptr [esi + 8], edi")


def test_object_and_audio_table_are_added_with_extra_0x124(img):
    t = text(img, 0x73C13C, 12)
    assert "push 0xda3df8" in t and "push 0x124" in t and "push 0xc26720" in t


def test_parse_module_name_rules(img):
    # RW 0x73F297: Body is userData 0x3E7; 0x73F2B9 / 0x73F2E8: the BODY bit 0x20 tests
    assert has(img, 0x73F297, 6, "cmp edi, 0x3e7")
    assert has(img, 0x73F2B9, 3, "test al, 0x20")
    assert has(img, 0x73F2E8, 3, "test al, 0x20")
    assert img.cstr(0xC27254) == "Only Body allowed here" and img.cstr(0xC2723C) == "No Body allowed here"
    # load type 2: the mode byte (+0x608) must be 1; load type 4 skips the clearing
    assert has(img, 0x73F30C, 0x20, "cmp eax, 2", "cmp byte ptr [esi + 0x608], 1")
    assert has(img, 0x73F33D, 4, "cmp eax, 4")
    # clearCopiedFromDefault on the four lists at +0x2e4, +0x2f0, +0x2fc, +0x308
    assert has(img, 0x73F345, 0x34, "lea ecx, [esi + 0x2e4]", "lea ecx, [esi + 0x2f0]", "lea ecx, [esi + 0x2fc]", "lea ecx, [esi + 0x308]")
    # vtable slot 4 (+0x10) clears AI modules; slot 7 (+0x1c) is consulted only for a ChildObject (sete [ebp + 0xc] on type == 4)
    assert has(img, 0x73F488, 0x40, "cmp dword ptr [ebx + 8], 4", "call dword ptr [eax + 0x10]", "call dword ptr [eax + 0x1c]")
    assert has(img, 0x73F4C1, 6, "cmp byte ptr [esi + 0x608], 2")


def test_clear_copied_from_default_predicate(img):
    # RW 0x73E222: (mask & arg) && copiedFromDefault (+0x10) && !inheritable (+0x11)
    assert has(img, 0x73E21E, 0x20, "test dword ptr [eax + 0xc], edx", "cmp byte ptr [eax + 0x10], 0", "cmp byte ptr [eax + 0x11], 0")


def test_nugget_is_20_bytes_and_tags_compare_with_the_string_compare(img):
    assert has(img, 0x73DD05, 3, "add esi, 0x14")
    assert has(img, 0x73DCF5, 12, "lea ecx, [esi + 4]", "call 0x4065aa")


def test_add_module_info_checks_four_lists_and_requires_same_class_for_draw(img):
    assert has(img, 0x73EF55, 8, "lea ecx, [edi + 0x2e4]")
    assert has(img, 0x73EF8D, 8, "lea ecx, [edi + 0x2f0]")
    assert has(img, 0x73EFAC, 12, "call 0x4065aa")  # draw: compare the class names
    assert has(img, 0x73F0B2, 8, "lea ecx, [edi + 0x2fc]")
    assert has(img, 0x73F0EE, 8, "lea ecx, [edi + 0x308]")
    for va, kind in ((0xC26FC8, "behavior"), (0xC26EA8, "draw"), (0xC26D80, "update"), (0xC26C50, "client behavior")):
        assert f"already on {kind} module" in img.cstr(va, 600)


def test_overridable_assignment_copies_nothing(img):
    # RW 0x92BB7C: mov eax, ecx ; ret 4 (the Overridable base's operator=); copyFrom keeps id (+0x5e8), name (+0x64), link (+0x494)
    t = text(img, 0x92BB7C, 2)
    assert t == ["mov eax, ecx", "ret 4"]
    assert has(img, 0x7405D0, 0x40, "mov bx, word ptr [esi + 0x5e8]", "mov edi, dword ptr [esi + 0x494]", "lea eax, [esi + 0x64]")


def test_set_copied_from_default_sets_both_flags_and_all_four_lists(img):
    assert has(img, 0x73CEEA, 0x40, "mov byte ptr [esi + 0x5f9], 1", "mov byte ptr [esi + 0x5fa], 1", "lea ecx, [esi + 0x308]")


def test_armor_and_weapon_sets_clear_once_after_a_copy(img):
    assert has(img, 0x73FAD5, 8, "lea eax, [esi + 0x5f9]", "cmp byte ptr [eax], 1")
    assert has(img, 0x73ED26, 8, "lea eax, [esi + 0x5fa]", "cmp byte ptr [eax], 1")


def test_module_editing_keywords_use_the_object_table_only(img):
    for va in (0x73BEFB, 0x73E8C9):
        assert has(img, va, 2, "push 0xda3df8")
    assert img.cstr(0xC26ACC) == "Expected oldMode to be MODULEPARSE_NORMAL"


def test_module_registry_has_330_sites_and_the_keys_are_name_keys(img):
    from module_registry import ADD_MODULE, _callers

    assert len(_callers(img, ADD_MODULE)) == 330
    # createData of the base ModuleData: initFromINI(data, NULL table) -> only End is accepted
    assert has(img, 0x708258, 0x14, "push 0", "call 0x42db80")


def test_script_block_reader_ends_at_endscript_and_has_no_eof_test(img):
    assert img.cstr(0xBD3D0C) == "ENDSCRIPT"
    body = [f"{i.mnemonic} {i.op_str}" for i in img.decode(0x42D423, 0x30)]
    assert any("call 0x42d371" in b for b in body)
    assert not any("0x430" in b for b in body)  # no [esi + 0x430] (end-of-file flag) test inside the reader loop
