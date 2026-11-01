// OpenBFME. GPL-3.0.
//
// Lane BFME2CAMP1: the campaign's carry-over units at a mission's start (TheLinearCampaignManager + 0x10's LinearCampaign: its CarryoverUnit records
// spawned into the map), as far as the FIRST mission of a campaign needs them. MAP GOOD Rivendell's Glorfindel exists only through it: the map has no
// such object and its scripts name the one the campaign spawns (SET_REF_TO_NEREST_TEAM_OF_TYPE_OWNED_BY_PLAYER "Glorfindel").
//
// TARGET FACTS (RotWK game.dat; the BFME2 decomp is the same code 0x8C7 higher, LinearCampaign/LinearCampaignAddCarryover.cpp 0x1EC63D,
// reverse/attempts/0x001ecdb3.cpp beginCampaign, ArmyPlacer*.cpp, tier B same-shape):
//   * startCampaign RW 0x5EC63C(index, difficulty): a new LinearCampaign (ctor RW 0x5EB223: skill points + 0x10 = 0.0, sciences + 0x14 empty, the
//     upgrade bits + 0x20 (0x480) clear, the bounty share + 0xB0 = 0.0, the records + 0xB4 / the used records + 0xC4 empty), then beginCampaign RW
//     0x5EC4EC: one record (0xBC bytes, ctor RW 0x7803B6: experience + 8 = 0.0, upgrade mask + 0x10 clear, + 0xA4 zero) per CarryoverUnit, in INI order,
//     with its name (+ 4) and quantity + 0xA0 = 1; the hero count + 0xC0 (KindOf HERO) is only counted; the mission index 0, then the mission start;
//   * the mission's start: GameLogic::startNewGame RW 0x6315E8 -> RW 0x626423 (not when a save is loaded) -> RW 0x5EC0C7 (a current campaign) ->
//     addCarryoverStuffToMap RW 0x5EBD76:
//       1. the mission (RW 0x5EAADF; none: nothing); the used records cleared (RW 0x5EB533);
//       2. the first player of ThePlayerList in index order (RW 0x6A8755 / 0x6A85EE) whose type + 0x5C is 0 (human); none: nothing;
//       3. with the keep-score switch (TheGameLogic + 0x98) off: the player's rank reset (Player + 8 vslot 0xC = Player RW 0x6AEE11: RW 0x782774 and
//          resetSciences RW 0x6AED4B), resetSciences again (RW 0x6AED4B), addSkillPoints(+ 0x10, false) (RW 0x782AA4), Player + 8 + 0x18 (the scalar
//          base rank) = Player + 0x1C (the rank, RW 0x71C311), every science of + 0x14 bought (RW 0x6AE3CC), every upgrade bit of + 0x20 given (RW
//          0x66F218 -> RW 0x6AEE22(u, 2, 1)), the bounty share Player + 0x334 = + 0xB0 (RW 0x6AA847);
//       4. every record in order: not named by the mission's DelayCarryoverSpawningOf (RW 0x5EAFDD: case-sensitive) and quantity > 0: `quantity` times
//          CarryoverUnit::createSingleObject (RW 0x780172) for the player, each object made gets Object + 0x458 |= 0x10 (a carry-over object: read by
//          onCurrentMissionWon RW 0x5EBAEE) and joins a list; a record that made one moves to the used records (RW 0x5EB78F / 0x5EB411);
//       5. the army placer (RW 0x781992 / 0x782326(list, player)): GetStartPosWaypointLocations RW 0x781B17: "Player_1_Start" (always player 0 + 1)
//          and "Player_1_Start_Rally" (none: the rally is the start and the start moves x + 10.0, RW 0xBD83D8; no start: (0, 0, 0) and (1, 0, 0), the
//          answer ignored); PlaceObjects RW 0x781FAF: the formation "WalkOnTemplate" (TheFormationAssistant) about the start, turned to
//          atan2(rally - start) (RW 0x441BD2), each object teleportTo(start + its slot's offset, z = the start's, keepAI) (RW 0x696E63) and
//          setOrientation(angle) (RW 0x70C31E); no terrain snap, no group move;
//   * createSingleObject RW 0x780172: the template by name (RW 0x7800DC; none: nothing), TheThingFactory newObject on the player's default team (Player
//     + 0x30C) with no status (RW 0x6D165E), onBuildComplete (RW 0x68D252), the record's experience when above the tracker's (RW 0x79D8EF), the record's
//     upgrade mask OR-ed into the object's (RW 0x68CC6C) and updateUpgradeModules (RW 0x6936FE), + 0x488 = the record's + 0xA4, a horde's members the
//     same mask (RW 0x780156). The object is placed by the placer only.
//   * onCurrentMissionWon RW 0x5EBAEE (from the VICTORY / QUICKVICTORY actions, RW 0x7C45F8 / 0x7BE0C6, with a current campaign): every live object of
//     the logic's list (in list order) with the carry-over flag becomes a record appended to + 0xB4 (RW 0x6917BB: the name, for a HERO its
//     RespawnUpdate's RespawnAsTemplate when set; experience = tracker + 0x10, rank = tracker + 0x24, the upgrade mask Object + 0x28C, quantity 1), then
//     every player's revival list (in player order, then list order) adds a record for each entry with the flag (+ 0xB1; RW 0x780F9E: the entry's
//     template name, experience, rank, mask); the used records + 0xC4 are cleared; the first human player's skill points (+ 0x10 = Player + 0x14),
//     grantable sciences (+ 0x14, in the player's order), upgrades with PersistsInCampaign (+ 0x20) and bounty share (+ 0xB0) are kept. Records are
//     never merged; the records left in + 0xB4 come first;
//   * restartCurrentCampaignMission RW 0x5EC5D1 (a lost mission replayed): the used records are appended back to + 0xB4, + 0xC4 cleared;
//   * the delayed spawn RW 0x5EBF92 (CREATE_DELAYED_CARRYOVER_UNIT_AT_WAYPOINT RW 0x7CAA30): the first record of + 0xB4 whose name is in the types makes an
//     object for the player (createSingleObject), which gets the carry-over flag; the record is copied to + 0xC4 either way and leaves + 0xB4 when its
//     quantity is 1 or less (else - 1); the action places the object at the waypoint (setPosition) and names it; HAS_DELAYED_CARRYOVER_UNIT_OF_TYPE
//     (RW 0x7EB69A -> RW 0x5EA817) answers whether a record of + 0xB4 has one of the names (the player and the quantity are not read).
// WHAT THIS PORT DOES NOT (stop S-4904, S-852): the record's statistics block (+ 0xA4, Object + 0x488: missions, kills) is not kept (Object + 0x488 is not
// ported); a HORDE's LivingWorldOverloadTemplate name (RW 0x86D6FD) is not read (its own name is recorded, reported); the hero count + 0xC0 (no reader
// found) is not kept. INFERENCE: the formation of more than one object placed at the start is its slot 0 for each (S-4460, reported). The keep-score
// switch is not toggled: the score counts only from the game start's last step (ScoreKeeper::setCounting), after the spawn. TheAI's group of
// PlaceObjects (RW 0x70042A / 0x6FFB68) has no state in the port.
#pragma once

#include "Common/Upgrade.h"
#include "GameLogic/ObjectTypes.h"

#include <string>
#include <vector>

class GameLogic;
class Object;
class Player;
class PlayerList;
class StateHasher;
class ThingTemplate;
struct Coord3D;

namespace CampaignCarryover
{
// one record of the campaign (RW 0xBC bytes) without its statistics block (+ 0xA4, not kept: see above)
struct Record
{
	std::string templateName;  ///< + 4
	float experience = 0.0f;   ///< + 8
	int rank = 0;              ///< + 0xC
	UpgradeMaskType upgrades;  ///< + 0x10
	int quantity = 0;          ///< + 0xA0
};

// the campaign's carry-over state (the LinearCampaign's fields this port keeps)
struct State
{
	std::vector<Record> records;          ///< + 0xB4: spawned at the next mission start (unless delayed) or by the delayed spawn
	std::vector<Record> used;             ///< + 0xC4: the records spawned this mission (a replay puts them back)
	float skillPoints = 0.0f;             ///< + 0x10
	std::vector<std::string> sciences;    ///< + 0x14 (by name)
	std::vector<std::string> upgrades;    ///< + 0x20 (by name: the upgrades with PersistsInCampaign)
	float bountyShare = 0.0f;             ///< + 0xB0

	bool operator==(const State &o) const;
	// an OpenBFME text form (the campaign's progress between missions, its saves); parse fails on anything malformed
	std::string serialize() const;
	static bool parse(const std::string &text, State &out, std::string *error);
	void crc(StateHasher &h) const;
};

// beginCampaign RW 0x5EC4EC: the campaign's records from its CarryoverUnit names (INI order), each quantity 1
State beginCampaign(const std::vector<std::string> &carryoverUnits);

// RW 0x5EAFDD: the mission delays the record (its DelayCarryoverSpawningOf names it, case-sensitive)
bool isDelayed(const std::vector<std::string> &delayed, const Record &r);

struct SpawnResult
{
	Player *player = nullptr;           ///< the human player the units went to (null: none, nothing done)
	std::vector<Object *> objects;      ///< the objects made, in record order
	std::vector<std::string> stops;     ///< what was reported
};

// RW 0x5EBD76: the player's reset to the campaign's values, the records' objects, the placer; `state` is updated (+ 0xB4 / + 0xC4)
SpawnResult addCarryoverStuffToMap(GameLogic &logic, PlayerList &players, State &state, const std::vector<std::string> &delayed);

// RW 0x5EBAEE: the won mission's survivors and the human player's data into `state`
void onCurrentMissionWon(GameLogic &logic, PlayerList &players, State &state);

// RW 0x5EC5D1's records part: the used records back after the left ones
void restartMission(State &state);

// RW 0x5EBF92: the delayed spawn of the first record named by `types` for `player` (null when no record or no object; the record moves either way)
Object *spawnDelayed(GameLogic &logic, State &state, const std::vector<std::string> &types, Player &player);

// RW 0x5EA817 (HAS_DELAYED_CARRYOVER_UNIT_OF_TYPE): a record of + 0xB4 named by `types`
bool hasDelayed(const State &state, const std::vector<std::string> &types);

// RW 0x781B17: "Player_1_Start" / "Player_1_Start_Rally"; false without the start waypoint (start (0, 0, 0), rally (1, 0, 0))
bool startPosWaypointLocations(GameLogic &logic, Coord3D &start, Coord3D &rally);
} // namespace CampaignCarryover
