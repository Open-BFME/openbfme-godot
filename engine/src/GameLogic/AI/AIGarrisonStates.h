// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// The AI's way into a container and out of it (lane GARRISON-1): the enter / exit states of RotWK's AI state machine and ActionManager::canEnterObject.
//
// TARGET FACTS (RotWK game.dat, caveat S-001; the state ids are the ones RotWK's machine constructor RW 0x753755 defines, read from its defineState calls RW 0x8DBE1D):
//   * 56 AIMoveToPositionAndEnterState (vtable RW 0xC29D00): onEnter RW 0x751A8A (the goal is the container's EntryOffset, contain slot 0x15C, then AIMoveToState's
//     onEnter RW 0x74DADA), update RW 0x756452 (canEnterObject(mode 0, checkPath) or FAILURE; a CAN_ENTER_ANYTHING object of another player near an occupied container
//     makes the riders leave (slot 0x80, RW 0x7564D5: nearer than RW 0xDA4B60); the move's update RW 0x755572; a container of another player that holds riders keeps
//     the state going; on arrival the AI command 0x17 (RW 0x66C5A4) and SUCCESS);
//   * 15 AIEnterState (RW 0xC294F0): onEnter RW 0x7515E1 (the timeout = now + GameData WaitToForceMemberToEnterDelay (+ 0x1234), canEnterObject(mode 0) or FAILURE,
//     the goal is the EntryPosition (slot 0x158), the container is told the object wants to enter (slot 0x44(obj, 0)), the locomotor may stand on invalid cells
//     (+ 0x44 bit 1), the move's onEnter), update RW 0x756067 (canEnterObject or FAILURE; a HELD object succeeds; the move; once the move stopped, within the
//     container's bounding radius of the EntryPosition or after the timeout: contain->addToContain(obj), SUCCESS), onExit RW 0x752B0F (the move's onExit, the
//     locomotor flag, slot 0x44(obj, 2));
//   * 52 AIHordeEnterState (RW 0xC27A70): onEnter RW 0x747C37 (the container's contain is not a horde contain and accepts the horde with the capacity test;
//     the horde's members are released and walk to the container (horde slot 0x7C, RW 0x875C93); a container that is not IMMOBILE holds the horde DISABLED type 8
//     (RW 0x6936C7); the horde is added (slot 0x9C)), update RW 0x7433E3 (SUCCESS once no member is on the way, else horde slot 0xFC), onExit RW 0x747CD1;
//   * 38 AIExitState (RW 0xC27C48): onEnter RW 0x74352D (slot 0x44(obj, 1)), update RW 0x74356A (the container's exit interface: busy -> CONTINUE, a door -> exit
//     through it), onExit RW 0x752BF4 (slot 0x44(obj, 2));
//   * 53 AIHordeExitState (RW 0xC27AC8): onEnter RW 0x743437 (slot 0x44(horde, 1)), update RW 0x747D04 (the exit interface: busy -> CONTINUE; a door: the exit, then
//     SUCCESS once the horde is out), onExit RW 0x74347D;
//   * ActionManager::canEnterObject RW 0x82CBD5 and its shroud test RW 0x82C167; Object::isMobile RW 0x690E97.
// INFERENCE / NOT PORTED (stop S-1101): the collide-module branch of canEnterObject (RW 0x82CD2E: a collide module that would like to collide with the container
// lets the object in; counted, taken as no), the heal contain's full-health test (slot 0x18, false for every contain this lane ports), the SHIP branches
// (RW 0x751668 / 0x75661B / 0x82CED4), the AIEnterState test of a contained container (RW 0x756088 .. 0x7560A5), the enemy container's attack fallback (RW 0x82CFD4),
// the members' AI slot 0x1C4 / RW 0x5E821A of RW 0x875C93, the group manager (GameData + 0x11CA) of MSG_ENTER.

#pragma once

#include "GameLogic/AI/AIMove.h"
#include "GameLogic/AI/AIStateMachine.h"

#include <memory>
#include <string>
#include <vector>

class Object;

// RotWK's machine ids (RW 0x753755); see AIStateType for the ones the movement lane defined
enum AIGarrisonStateType
{
	AI_ENTER = 15,
	AI_EXIT = 38,
	AI_HORDE_ENTER = 52,
	AI_HORDE_EXIT = 53,
	AI_MOVE_TO_POSITION_AND_ENTER = 56
};

namespace GarrisonRules
{
// RW 0x690E97 Object::isMobile: not IMMOBILE, no DISABLED type but 8, not STONED, not a deployed SIEGE_TOWER
bool isMobile(const Object &obj);
// RW 0x82CBD5 ActionManager::canEnterObject(obj, container, source, mode, checkPath, *refusedBecauseOccupied)
bool canEnterObject(Object &obj, Object *container, CommandSourceType source, int mode, bool checkPath, bool *occupied = nullptr);
// the stop line S-1101
const char *stopLine();
// the counter of the collide-module branch not evaluated
unsigned long long &collideBranchSkipped();
} // namespace GarrisonRules

// the five states (the AI module's machine defines them, AIUpdateInterface::makeStateMachine)
std::unique_ptr<AIState> makeMoveToPositionAndEnterState(AIStateMachine &machine);
std::unique_ptr<AIState> makeEnterState(AIStateMachine &machine);
std::unique_ptr<AIState> makeHordeEnterState(AIStateMachine &machine);
std::unique_ptr<AIState> makeExitState(AIStateMachine &machine);
std::unique_ptr<AIState> makeHordeExitState(AIStateMachine &machine);
