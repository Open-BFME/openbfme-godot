// OpenBFME. GPL-3.0.
//
// WOTR2 milestone 7: what a War of the Ring battle's objects carry in retail (Object + 0x47C the army id, + 0x480 the spawn id, + 0x484 the Living World
// building id; the hero records' + 0xB8 army id and + 0xBC spawn id) kept beside the objects: ObjectID -> tag, (player index, hero template) -> tag.
//   * set: the battle start's army entries (ArmySummaryEntry::createObject RW 0x80EDF1 -> GameLogic RW 0x625E0A: the army id, the entry's spawn id), the
//     starting units with the garrison id (RW 0x8125FC's override), the owner's base buildings with the garrison id and the building id (RW 0x629F34), the
//     hero records the battle player's list gets from the War of the Ring player (RW 0x6E42DA: the garrison id, the record's spawn id), a hero's death
//     (its object's tag on its record), a hero produced from a record (the record's tag on the object);
//   * a destroyed region building (Object::onDie RW 0x6991F3: + 0x484 != 0) is listed for the strategic region (LivingWorldRegion::DestroyBuilding RW 0x7F32F4).
// Saved in the snapshot (obfme:GameLogic version 3, written only when the table is in use) and hashed only when it is in use, so a skirmish's snapshot
// and hashes are byte-identical to before. An object made in the
// battle by anything else (production, building, hordes) has no tag: the result gives it its player's garrison [S-1329 inference: retail copies the
// maker's id at each creation site, RotWK sites in spec R 5.1; what a porter or a region building makes is the garrison's]

#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

struct LivingWorldBattleTags
{
	struct Tag
	{
		int armyId = 0;
		std::string spawnId;
		int buildingId = 0;
	};
	// WOTR2 milestone 7 (step 5): a reinforcement army (ArmySummary + 0x2C state: 2 counting, 3 ready, 4 called in; + 0x34 the arrival frame) with the
	// entries it spawns and its walk-on waypoint choices (getSpawnPositions RW 0x80F754)
	struct Member
	{
		std::string thingTemplate;
		int memberIndex = 0;
		int quantity = 0;
		float experience = 0.0f;
		std::vector<std::string> upgrades;
		std::string spawnId;
	};
	struct Reinforcement
	{
		int armyId = 0;
		int playerIndex = -1;
		int state = 2;
		std::uint32_t arrivalFrame = 0;
		std::vector<Member> members;
		std::vector<std::pair<std::string, std::string>> spawnPoints;
	};
	bool active = false; // a War of the Ring battle runs
	std::vector<Reinforcement> reinforcements;
	std::vector<int> arrivals; // the armies called in, in order (the client's LW:ReinforcementsArrived for the local player's)
	std::map<std::uint32_t, Tag> objects;
	std::map<std::pair<int, std::string>, Tag> heroes;
	std::vector<int> destroyedBuildings; // Living World building ids, in the order they died

	void clear() { *this = LivingWorldBattleTags(); }
	bool inUse() const { return active || !objects.empty() || !heroes.empty() || !destroyedBuildings.empty() || !reinforcements.empty(); }
	Reinforcement *reinforcement(int armyId)
	{
		for (Reinforcement &r : reinforcements)
		{
			if (r.armyId == armyId)
			{
				return &r;
			}
		}
		return nullptr;
	}
	const Tag *object(std::uint32_t id) const
	{
		auto it = objects.find(id);
		return it == objects.end() ? nullptr : &it->second;
	}
	const Tag *hero(int player, const std::string &templateName) const
	{
		auto it = heroes.find({ player, templateName });
		return it == heroes.end() ? nullptr : &it->second;
	}
};
