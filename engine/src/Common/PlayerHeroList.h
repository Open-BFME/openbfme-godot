// OpenBFME. GPL-3.0.
//
// PlayerHeroList (lane HERO-1): RotWK's per-player list of the heroes a player can recruit or revive (RW Player + 0x758). The class name is descriptive
// (INFERENCE: no retail symbol names it); BFME1 and Zero Hour have no such list (heroes there are UNIT_BUILD buttons), so every fact is the target's.
//
// TARGET FACTS (RotWK game.dat, caveat S-001), read from the disassembly:
//   * layout: + 4 / + 8 a vector of 0xE8-byte records (begin / end), + 0x10 the owning player. The record (RW 0x780713 from a template, RW 0x780DD4 from a
//     dying object, copy RW 0x6E267E):
//       + 0x08 experience (float, the dying object's tracker + 0x10), + 0x0C rank (tracker + 0x24; from a template RW 0x73CE2A: 1 unless a create module
//       names a level), + 0x10 base rank (tracker helper + 0x0C, RW 0x79D102), + 0x14 .. + 0xA3 the object's upgrade mask (Object + 0x28C),
//       + 0xA4 cost (template BuildCost, RW 0x780713; RespawnUpdate's rule cost for the level, RW 0x8B3C07), + 0xA8 the frame production started (-1 = not
//       in production), + 0xAC time in whole seconds (trunc(BuildTime) RW 0xA3CFA4; the rule's Time / 1000, RW 0x8B3C1C), + 0xB0 the record is of a dead
//       hero (revive) rather than one never built (purchase), + 0xB1 the object's flag + 0x458 bit 4, + 0xB4 the production id (-1 none), + 0xB8 / + 0xBC
//       Object + 0x47C / + 0x480, + 0xC0 a copy of Object + 0x488 (0x18 bytes), + 0xD8 the tracker's level cap (+ 0x28, RW 0x8B3744 writes it after the
//       record is made), + 0xDC a float (1.0; 0.998 while in production, RW 0x7812B2), + 0xE0 the object's name, + 0xE4 the template name (RespawnUpdate's
//       RespawnAsTemplate when set, RW 0x8B316B);
//   * filled at the player's game start (RW 0x6B16EC, called from RW 0x6B183D): in a game of kind 3 (GameLogic + 0x114) that is neither a campaign nor
//     the Living World (RW 0x5FF924), one purchase record per PlayerTemplate BuildableHeroesMP name that names a template (RW 0x781801), in list order;
//     BuildableHeroListUpgrade appends BuildableRingHeroesMP the same way (RW 0x8BC4C6); a hero's RespawnUpdate appends a revive record at its death
//     (RW 0x781792 -> RW 0x780DD4; AutoSpawn rules zero the cost);
//   * cost (RW 0x780AD3 -> 0x780614): trunc((float)BuildAssistant::calcCostToBuild(template, owner, no producer, record cost) * m) where m is 1.0, or with a
//     producer its ProductionUpdate's hero multiplier (slot 28, RW 0x8A0A31: the product of the CostMultiplier of the ProductionModifier entries with
//     HeroRevive for a revive record or HeroPurchase for a purchase record whose RequiredUpgrade is empty or the producer's); a Brutal AI owner's discount
//     (RW 0x6AA61B == 3 and RW 0x6A950B, AI data + 0x95C) is stop S-204, as for units;
//   * time in frames (RW 0x780B72 -> 0x780687): the same with calcTimeToBuild(template, owner, no producer, record seconds) and the TimeMultiplier (slot 29,
//     RW 0x8A0AB0), Brutal AI data + 0x960 (S-204);
//   * startProduction (RW 0x7812B2, index, production id): refused when a record already has the id or the index is out of range; a record not in production
//     starts: + 0xDC = 0.998, + 0xB4 = id, + 0xA8 = the logic frame; true. cancelProduction (RW 0x780C64, id): the record in production goes back
//     (+ 0xA8 = -1, + 0xB4 = -1, + 0xDC = 1.0). The progress (RW 0x780C9F, index, producer): 0.0 for no record or one not in production, 1.0 when the
//     time is below 1, else (frame - start) / time in x87 extended precision (the unsigned frame corrected by 2^32 when negative as int);
//   * a producer's type 3 entries whose player cannot afford the command points of the record's template push the start frame one frame on each update
//     (RW 0x8A0669: the production pauses);
//   * findIndex (RW 0x78131E, template, production id, nth): the index of the record of the template (by name) with that production id, or with id -1 the
//     nth record of the template; -1 when none.
// The object made from a record (RW 0x78142F) is GameLogic/Module/RespawnUpdate.h's HeroRevive::produce.

#pragma once

#include "Common/Upgrade.h"
#include "GameLogic/ObjectTypes.h"

#include <cstdint>
#include <string>
#include <vector>

class GameLogic;
class Object;
class Player;
class StateHasher;
class ThingTemplate;

struct HeroRecord
{
	float experience = 0.0f;        ///< + 0x08
	int rank = 0;                   ///< + 0x0C
	int baseRank = 0;               ///< + 0x10
	UpgradeMaskType upgradeMask;    ///< + 0x14
	unsigned cost = 0;              ///< + 0xA4
	std::int32_t startFrame = -1;   ///< + 0xA8 (-1: not in production)
	int seconds = 0;                ///< + 0xAC
	bool dead = false;              ///< + 0xB0 (a revive record)
	bool objectFlag = false;        ///< + 0xB1 (Object + 0x458 bit 4: not identified, carried)
	std::uint32_t productionID = 0xFFFFFFFFu; ///< + 0xB4 (-1 none; 0 in a record made from a dying object, RW 0x780DD4)
	int levelCap = 0;               ///< + 0xD8
	float uiFactor = 1.0f;          ///< + 0xDC
	std::string objectName;         ///< + 0xE0
	std::string templateName;       ///< + 0xE4
};

class PlayerHeroList
{
public:
	const std::vector<HeroRecord> &records() const { return m_records; }
	size_t size() const { return m_records.size(); }
	const HeroRecord *at(int index) const;            ///< RW 0x7808AB (null out of range)
	HeroRecord *at(int index);
	HeroRecord *findByProductionID(std::uint32_t id); ///< RW 0x7808DB
	const HeroRecord *findByProductionID(std::uint32_t id) const;

	void clear() { m_records.clear(); }
	void addPurchase(const ThingTemplate &tt);        ///< RW 0x781801 (-> 0x780713)
	int addRecord(const HeroRecord &r);               ///< push_back (RW 0x6E40A6); returns the new index (RW 0x781792 returns size - 1)
	void erase(size_t index) { m_records.erase(m_records.begin() + (long)index); } ///< RW 0x6E33C5

	const ThingTemplate *templateAt(GameLogic &logic, int index) const; ///< RW 0x780C2F / 0x780D9F: findTemplate(record name)
	bool isAvailable(int index) const;                ///< RW 0x780C46: the record exists and is not in production
	int costAt(GameLogic &logic, const Player &owner, int index, const Object *producer) const; ///< RW 0x780AD3
	int framesForProductionID(GameLogic &logic, const Player &owner, std::uint32_t id, const Object *producer) const; ///< RW 0x780B72
	int framesAt(GameLogic &logic, const Player &owner, int index, const Object *producer) const; ///< RW 0x780C11
	bool startProduction(int index, std::uint32_t id, UnsignedInt frame); ///< RW 0x7812B2
	bool cancelProduction(std::uint32_t id);          ///< RW 0x780C64
	double progressAt(GameLogic &logic, const Player &owner, int index, const Object *producer) const; ///< RW 0x780C9F
	int findIndex(const ThingTemplate &tt, std::uint32_t productionID, int nth) const; ///< RW 0x78131E

	void crc(StateHasher &h) const;

private:
	int recordCost(GameLogic &logic, const Player &owner, const HeroRecord &r, const Object *producer) const;   ///< RW 0x780614
	int recordFrames(GameLogic &logic, const Player &owner, const HeroRecord &r, const Object *producer) const; ///< RW 0x780687

	std::vector<HeroRecord> m_records;
};
