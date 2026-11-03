"""Lane BFME2MODE-0 (docs/EDITIONS.md): the first edition branch, checked against both game.dat files.

AIFarmKillSquad::initializeTeamTemplate (Open-BFME-2 .../TargetlessTactics/AIFarmKillSquad.cpp:310) writes the team template's priority:
RW 0x9BB49D `mov dword ptr [esi + 0x21C], imm32` and BFME2 0x9AD00B the same instruction with another value. The engine's values are
engine/src/GameLogic/SkirmishAI/SkirmishAIEditionRules.h. The function itself needs game singletons (TheX at [0xDFEEF8]), so the retail
instructions from after its base call up to and including that store run as a shim on a zeroed template (esi): RW 0x9BB48B..0x9BB4A7,
BFME2 0x9ACFF9..0x9AD015 (`lea eax, [esi + 0x2FC]`, the forbidden KindOf mask's three ORs, the priority). No retail byte is stored here: the
helper reads them from the installed game.dat at runtime.

Runs where the helper is built (build.bat on Windows; build_mingw.sh + Wine elsewhere) and BFME2_INSTALL / ROTWK_INSTALL name the installs.
"""
from __future__ import annotations

import hashlib
import re
import struct
import sys
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import oracle  # noqa: E402

RULES = HERE.parents[1] / "engine" / "src" / "GameLogic" / "SkirmishAI" / "SkirmishAIEditionRules.h"
CLEAN_BFME2_MD5 = "34391b253b0ee12d6cec1178f46d3bf9"  # the clean BFME2 1.06 game.dat (Open-BFME-2 PLAN.md)

# image tag, install variable, Windows default, shim start, the priority store's address (10 bytes: c7 86 1c 02 00 00 imm32), the rules constant
SITES = {
    "bfme2": ("BFME2_INSTALL", "F:/BFME2", 0x9ACFF9, 0x9AD00B, "kFarmKillPriorityBfme2"),
    "rotwk": ("ROTWK_INSTALL", "F:/RotWK", 0x9BB48B, 0x9BB49D, "kFarmKillPriorityRotWK"),
}
STORE_21C = bytes.fromhex("c7861c020000")  # mov dword ptr [esi + 0x21C], imm32


def engine_value(name: str) -> int:
    m = re.search(rf"constexpr int {name} = (\d+);", RULES.read_text(encoding="utf-8"))
    assert m, f"{RULES.name} has no {name}"
    return int(m.group(1))


def run_shim(tag: str):
    env, default, start, store, _ = SITES[tag]
    if oracle.find_host() is None:
        pytest.skip(f"{oracle.HOST} is not built (tools/retail_oracle/build.bat, or build_mingw.sh for Wine)")
    inst = oracle.install_dir(env, default)
    if inst is None:
        pytest.skip(f"{env} names no install with a game.dat")
    with oracle.Oracle(tag, inst / "game.dat") as o:
        instr = o.peek(store, 10)
        assert instr[:6] == STORE_21C, f"{tag} {store:#x} is not the priority store (another build of the game?)"
        code = o.peek(start, store + 10 - start) + b"\xc3"  # the retail run, then ret
        this = o.alloc(0x400)
        shim = o.code(code)
        r = o.call(shim, "regs", regs={"esi": this})
        assert r.esi == this and r.callee_pop == 0
        priority = o.peek32(this + 0x21C)
        mask = o.peek(this + 0x2FC, 0x14)
        caveats = o.caveats
    return priority, struct.unpack("<I", instr[6:])[0], mask, caveats, inst


def test_bfme2_farm_kill_priority_is_the_clean_game_dat_s_value():
    priority, imm, mask, caveats, inst = run_shim("bfme2")
    assert hashlib.md5((inst / "game.dat").read_bytes()).hexdigest() == CLEAN_BFME2_MD5, "not the clean BFME2 1.06 game.dat"
    assert caveats == []
    assert priority == imm == engine_value("kFarmKillPriorityBfme2") == 30
    # the forbidden KindOf mask BFME2 writes (+0x2FD |= 0x08, +0x307 |= 4, +0x30C |= 0x20); RotWK's +0x2FD byte is 0x48 (not ported: Phase 1)
    assert (mask[1], mask[0xB], mask[0x10]) == (0x08, 0x04, 0x20)


def test_rotwk_farm_kill_priority_is_the_rotwk_game_dat_s_value():
    priority, imm, mask, caveats, _ = run_shim("rotwk")
    assert priority == imm == engine_value("kFarmKillPriorityRotWK") == 500
    assert (mask[1], mask[0xB], mask[0x10]) == (0x48, 0x04, 0x20)
    # S-001: the installed RotWK game.dat is community-patched; this function is outside the patched sections (docs/XMAP.md)
    print("rotwk caveats:", caveats)


def test_wine_paths_and_command():
    if oracle.under_wine():
        assert oracle.host_path("/opt/games/BFME2/game.dat") == "Z:\\opt\\games\\BFME2\\game.dat"
        assert oracle.host_command(Path("/a/retail_oracle.exe")) == [oracle.os.environ.get("WINE", "wine"), "/a/retail_oracle.exe"]
        assert oracle.host_env()["WINEDLLOVERRIDES"] == "msvcr71=n;msvcp71=n"
    else:
        assert oracle.host_path("F:/BFME2/game.dat") == "F:/BFME2/game.dat"
        assert oracle.host_env() is None
