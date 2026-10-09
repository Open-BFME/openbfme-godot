// OpenBFME. GPL-3.0.
//
// HeroSystem (lane HERO-1): the player-level parts of recruiting and reviving heroes that are not a module: the hero list at the player's game start, the
// revive record of a dying hero and the object made from a record. The list itself is Common/PlayerHeroList.h; the modules are
// GameLogic/Module/HeroModules.h; production's type 3 entries are GameLogic/Module/ProductionUpdate.cpp.
//
// TARGET FACTS (RotWK game.dat, caveat S-001):
//   * initPlayer (RW 0x6B16EC, from the player's game start RW 0x6B183D): GameLogic + 0x114 == 3 and not RW 0x5FF924 (a campaign or the Living World):
//     each BuildableHeroesMP name of the player's template (template + 0x18C) that names a template becomes a purchase record (a name that names nothing
//     is skipped, RW 0x6B1756 tests the find); otherwise (a campaign, WotR) the records come from the campaign's saved heroes (not ported: S-852).
//   * the revive record of a dying hero (RW 0x781792 -> 0x780DD4, object, autoSpawn): experience, rank and base rank of its tracker, its upgrade mask, the
//     RespawnUpdate rule's cost (a CREATE_A_HERO object: calcCostToBuild(player, 0, -1) * TheCreateAHeroSystem + 0x1CC (HeroRevivalDiscount) / 100, lane HERO-2) and time for the tracker's rank,
//     the object's name and RespawnAsTemplate (or its own template); AutoSpawn zeroes the cost. Appended; the index is size - 1.
//   * produce (RW 0x78142F, production id, position): the record with the id; its template by name (none: null, the record stays); newObject(template, the
//     owner's default team, no status); setPosition; the record's object name; then, for a revive record or an experience above 1.0, the tracker gains
//     experience - 1.0 (addExperiencePoints(x, false, false, false, false) with GameLogic + 0x98 cleared around it), the base rank is restored (RW 0x79D745),
//     the tracker's leveled byte from the record (S-852) and the object's upgrade mask ORs the record's (RW 0x68CC6C: the bits only, no upgrade module
//     runs); a CREATE_A_HERO object's Create-A-Hero data (RW 0x809FFB, S-852); RespawnUpdate + 0x41 = not a revive record; the AI call RW 0x70E013 /
//     0x67449C (S-852); SCIENCE_GandalftheWhite owned and KindOf GANDALF: ModifierList SpellBookGandalfWhite (and RW 0x68B934, S-852); SCIENCE_Anduril
//     owned and KindOf ARAGORN: ModifierList SpellBookAnduril; the record leaves the list (RW 0x7813FF).

#pragma once

#include "Common/GameCommon.h"
#include "GameLogic/ObjectTypes.h"

#include <cstdint>
#include <string>
#include <vector>

class GameLogic;
class Object;
class Player;
class RespawnUpdate;
struct Coord3D;

namespace HeroSystem
{
void initPlayer(GameLogic &logic, Player &player);                                         ///< RW 0x6B16EC
int addDeadHero(Player &player, Object &obj, const RespawnUpdate &respawn, bool autoSpawn);  ///< RW 0x781792
Object *produce(GameLogic &logic, Player &player, std::uint32_t productionID, const Coord3D &pos); ///< RW 0x78142F
std::vector<std::string> stopLines();
} // namespace HeroSystem
