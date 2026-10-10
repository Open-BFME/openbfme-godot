"""Pins of the update phases of the RotWK binary's update modules (lane IDLE-1 r2, update_phases.py). The port files a module in GameLogic's
updates[getUpdatePhase()]: these are the classes whose vslot 0x30 is not the default 2, read from the binary. Runs only when RW_GAME_DAT is set
(caveat S-001: the community-modified RotWK game.dat)."""
from __future__ import annotations

import os
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent))
pytestmark = pytest.mark.skipif(not os.environ.get("RW_GAME_DAT"), reason="RW_GAME_DAT not set (path of the RotWK game.dat)")

PHASE_0 = {"AIUpdateInterface", "AnimalAIUpdate", "AssaultTransportAIUpdate", "DeployStyleAIUpdate", "DozerAIUpdate", "EmotionTrackerUpdate",
           "GiantBirdAIUpdate", "HordeAIUpdate", "HordeWorkerAIUpdate", "SiegeAIUpdate", "SupplyTruckAIUpdate", "TransportAIUpdate", "WanderAIUpdate",
           "WorkerAIUpdate"}
PHASE_1 = {"AODHordeContain", "HordeContain", "HorseHordeContain"}
PHASE_3 = {"FiringTrackerHelper", "WeaponStatusHelper"}


@pytest.fixture(scope="module")
def phases():
    from rwimage import Image
    import update_phases

    return update_phases.update_phases(Image())


def test_phase_bodies(phases):
    from rwimage import Image
    import update_phases

    img = Image()
    assert update_phases.phase_body(img, 0x851E97) == 0  # xor eax, eax; ret
    assert update_phases.phase_body(img, 0x490AC4) == 1  # xor eax, eax; inc eax; ret
    assert update_phases.phase_body(img, 0x8311B1) == 3  # push 3; pop eax; ret


def test_every_update_class_resolves(phases):
    assert [n for n, p in phases.items() if p is None] == []
    assert len(phases) == 191  # 184 registered classes with the update interface and the 7 helpers


def test_non_default_phases(phases):
    assert {n for n, p in phases.items() if p == 0} == PHASE_0
    assert {n for n, p in phases.items() if p == 1} == PHASE_1
    assert {n for n, p in phases.items() if p == 3} == PHASE_3
    assert all(p == 2 for n, p in phases.items() if n not in PHASE_0 | PHASE_1 | PHASE_3)
