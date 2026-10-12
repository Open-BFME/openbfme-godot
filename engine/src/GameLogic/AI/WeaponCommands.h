// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// The fire-weapon commands of a command button (lane FACT-A): MSG_DO_WEAPON, MSG_DO_WEAPON_AT_LOCATION, MSG_DO_WEAPON_AT_OBJECT, the messages of a FIRE_WEAPON
// button, and MSG_TOGGLE_NO_AUTO_ACQUIRE (the trebuchets' and catapults' hold-fire toggle, see the .cpp) (Gimli's axe throw, the trebuchets' and catapults' Bombard, the archer hordes' ArcherBombard). One handler per type on GameLogicDispatch.
//
// TARGET FACTS (RotWK game.dat, caveat S-001; the dispatcher RW 0x779A3D; the group is the issuing player's selection minus what it does not control):
//   * MSG_DO_WEAPON 1037 (RW 0x77A304): { int WeaponSlot, int MaxShotsToFire }; AIGroup::setWeaponLockForGroup(slot, LOCKED_TEMPORARILY) (RW 0x76FAB7, BFME2 decomp
//     AIGroupAttackTeam.cpp, tier A) and, when some member locked, AIGroup::groupAttackPosition(NULL, maxShots, CMD_FROM_PLAYER) (RW 0x77229A: each member at its
//     own position) and RW 0x76FD04 (every member's AI + 0x2C = -1.0).
//   * MSG_DO_WEAPON_AT_LOCATION 1038 (RW 0x77A3CB): { int WeaponSlot, location, int MaxShotsToFire }: the same with the location.
//   * MSG_DO_WEAPON_AT_OBJECT 1039 (RW 0x77A34C): { int WeaponSlot, objectID, int MaxShotsToFire }; a target that does not exist ends it; the lock, and when some
//     member locked: groupIdle(CMD_FROM_AI) (RW 0x771F49), the lock again, then groupAttackObject(forced = false, target, maxShots, CMD_FROM_PLAYER) (RW 0x77208A,
//     ZH AIGroup::groupAttackObjectPrivate: every member with an AI that is not the target attacks it, a horde through its own branch).
// The position attack is AIGroup::groupAttackPosition / AIUpdateInterface::aiAttackPosition (lane PLAY-2). NOT PORTED (stop S-1982): the containers' riders (a member that lets its riders fire hands the
// order to them) and the members' distance order of groupAttackObject

#pragma once

#include "GameLogic/GameMessage.h"
#include "GameLogic/GameLogicDispatch.h"

#include <string>
#include <vector>

class GameLogic;

namespace WeaponCommands
{
void registerHandlers(GameLogicDispatch &dispatcher);

struct Stats
{
	unsigned long long atObject = 0, atLocation = 0, here = 0, lockedGroups = 0, attackOrders = 0, noAutoAcquire = 0, rejected = 0;
};
const Stats &stats();
std::vector<std::string> acceptanceStops();
} // namespace WeaponCommands
