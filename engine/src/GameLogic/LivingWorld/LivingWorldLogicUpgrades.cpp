// OpenBFME. GPL-3.0.
//
// The UpgradeTroops building nugget of the War of the Ring (WOTR2 milestone 5; the armory): the marks, the automatic pass at the start of every orders
// phase, the human messages and the application when the orders phase ends. RotWK 2.01, caveat S-001; BFME2 twins in brackets.
//
// TARGET FACTS (RotWK, read from the disassembly):
//   * the nugget instance (interface vtable RW 0xC8C724): canUpgrade RW 0x9D3E5B = used < NumUpgradesPerTurn && the entry's thing is listed in
//     UpgradeableUnits (exact) && !FullyUpgraded; queue RW 0x9D3C16 `if used <= N: ++used`; unqueue RW 0x9D3C2E `if used > 0: --used`; reset RW 0x9D3DD4.
//     UpgradeIface RW 0x8E4A47: none while the building is under construction; the building's last nugget with the interface (RW 0x8E4C1E).
//   * markUpgrade RW 0x80ED8C [0x40C430]: an unmarked entry takes the building's id (+0xCC) and the building queues it; cancelUpgrade RW 0x80EDB8
//     [0x40C45C]: the building found by id unqueues it, the mark goes. Cancels: the 0x6C0 handler, disbanding (RW 0x6B4933), a member move into an
//     army in another region (LivingWorldArmy::AddEntry RW 0x71B2D0), and the building-destroyed observer in phase 0 (RW 0x6B7139).
//   * FullyUpgraded RW 0x80F237 [0x40C804]: no thing, or the entry holds the whole armory set: true; else the missing armory upgrades less those of
//     every LevelUpUpgrade module (the thing's set holds its triggers, not its conflicts) whose cap the entry's rank has reached: true when none is left.
//   * the automatic pass RW 0x6B5F1C [0x2B5334], entering phase 0 after the garrison merge, every player (no AI test): per region in campaign order
//     with an owner, MarkUnitsForUpgrades RW 0x6E4123: the owner's armies there (its army vector), each entry not fully upgraded (key order, marked
//     and lent ones too), STLport sort by rank descending (RW 0x6E40E0), then each armory in plot order marks the unmarked candidates it can take.
//   * leaving phase 0 (the listeners before the movement): every army applies its marked entries (RW 0x71AB1D -> RW 0x80F6F3 -> RW 0x80F0D8: the
//     whole armory set, then each LevelUpUpgrade the set triggers and no conflict blocks raises the rank by LevelsToGain up to LevelCap and sets the
//     experience to the new rank's RequiredExperience; the mark is cleared), every complete armory resets its count (RW 0x8E4ACD -> RW 0x8E4ABC).
//   * the human messages: 0x6BF RW 0x6B7BAA marks for GetArmoryToUpgradeTroop RW 0x6B7A17 (the army in the region, its owner the region's, the entry
//     not lent; the first armory in plot order that can take it); 0x6C0 RW 0x6B605F -> RW 0x6B4A1E cancels.
// INFERENCE [S-1326]: the plot's byte + 0x34 UpgradeBuildings also tests (RW 0x7F3F58) is not read; the entry's rank is written only by the
// application (no other writer was found, spec K U3).

#include "GameLogic/LivingWorld/LivingWorldLogic.h"

#include "GameLogic/LivingWorld/StlportAlgorithm.h"
#include "GameLogic/SimMath.h"

#include <algorithm>

namespace LivingWorld
{

namespace
{
void setError(std::string *error, const std::string &text)
{
	if (error)
	{
		*error = text;
	}
}

bool containsAll(const std::vector<std::string> &set, const std::vector<std::string> &of)
{
	for (const std::string &u : of)
	{
		if (std::find(set.begin(), set.end(), u) == set.end())
		{
			return false;
		}
	}
	return true;
}

bool intersects(const std::vector<std::string> &a, const std::vector<std::string> &b)
{
	for (const std::string &u : b)
	{
		if (std::find(a.begin(), a.end(), u) != a.end())
		{
			return true;
		}
	}
	return false;
}

// an army's member indices in key order (the summary's {key, entry} vector, RW 0x80F61C)
std::vector<size_t> byKey(const Army &a)
{
	std::vector<size_t> idx(a.members.size());
	for (size_t i = 0; i < idx.size(); ++i)
	{
		idx[i] = i;
	}
	std::sort(idx.begin(), idx.end(), [&](size_t x, size_t y) { return a.members[x].key < a.members[y].key; });
	return idx;
}
} // namespace

const BuildingNugget *LivingWorldLogic::armoryOf(const Building &b) const
{
	if (b.turnsLeft > 0)
	{
		return nullptr; // + 0x20 == -1: under construction
	}
	const BuildingTemplate *t = buildingTemplate(b);
	const BuildingNugget *found = nullptr;
	if (t)
	{
		for (const BuildingNugget &n : t->nuggets)
		{
			found = n.kind == BuildingNugget::UPGRADE_TROOPS ? &n : found;
		}
	}
	return found;
}

std::vector<Building *> LivingWorldLogic::upgradeBuildings(int region)
{
	std::vector<Building *> out;
	for (Building &b : m_regions[(size_t)region].buildings)
	{
		if (armoryOf(b))
		{
			out.push_back(&b);
		}
	}
	std::sort(out.begin(), out.end(), [](const Building *x, const Building *y) { return x->slot < y->slot; }); // the plot vector + 0x190
	return out;
}

bool LivingWorldLogic::fullyUpgraded(const ArmyMember &e) const
{
	const UnitInfo u = m_units ? m_units(e.thingTemplate) : UnitInfo();
	if (!u.known || containsAll(e.upgrades, u.armoryUpgrades))
	{
		return true;
	}
	std::vector<std::string> missing;
	for (const std::string &a : u.armoryUpgrades)
	{
		if (std::find(e.upgrades.begin(), e.upgrades.end(), a) == e.upgrades.end())
		{
			missing.push_back(a);
		}
	}
	for (const UnitInfo::LevelUp &lu : u.levelUps)
	{
		if (!containsAll(u.armoryUpgrades, lu.triggeredBy) || intersects(u.armoryUpgrades, lu.conflictsWith))
		{
			continue;
		}
		if (e.rank >= lu.levelCap)
		{
			missing.erase(std::remove_if(missing.begin(), missing.end(),
							  [&](const std::string &m) { return std::find(lu.triggeredBy.begin(), lu.triggeredBy.end(), m) != lu.triggeredBy.end(); }),
				missing.end());
		}
	}
	return missing.empty();
}

bool LivingWorldLogic::canUpgradeWith(const Building &b, const ArmyMember &e) const
{
	const BuildingNugget *n = armoryOf(b);
	if (!n || n->numUpgradesPerTurn <= b.upgradesUsed)
	{
		return false;
	}
	return std::find(n->upgradeableUnits.begin(), n->upgradeableUnits.end(), e.thingTemplate) != n->upgradeableUnits.end() && !fullyUpgraded(e);
}

Building *LivingWorldLogic::findBuildingById(int id)
{
	for (RegionState &r : m_regions)
	{
		for (Building &b : r.buildings)
		{
			if (b.id == id)
			{
				return &b;
			}
		}
	}
	return nullptr;
}

void LivingWorldLogic::markUpgrade(ArmyMember &e, Building &b)
{
	if (e.upgradeBuilding != 0)
	{
		return;
	}
	e.upgradeBuilding = b.id;
	if (const BuildingNugget *n = armoryOf(b))
	{
		if (b.upgradesUsed <= n->numUpgradesPerTurn)
		{
			++b.upgradesUsed;
		}
	}
}

void LivingWorldLogic::cancelUpgrade(ArmyMember &e)
{
	if (e.upgradeBuilding == 0)
	{
		return;
	}
	if (Building *b = findBuildingById(e.upgradeBuilding))
	{
		if (armoryOf(*b) && b->upgradesUsed > 0)
		{
			--b->upgradesUsed;
		}
	}
	e.upgradeBuilding = 0;
}

void LivingWorldLogic::applyUpgrade(ArmyMember &e) const
{
	if (e.upgradeBuilding == 0)
	{
		return;
	}
	const UnitInfo u = m_units ? m_units(e.thingTemplate) : UnitInfo();
	if (u.known)
	{
		for (const std::string &a : u.armoryUpgrades)
		{
			if (std::find(e.upgrades.begin(), e.upgrades.end(), a) == e.upgrades.end())
			{
				e.upgrades.push_back(a);
			}
		}
		std::sort(e.upgrades.begin(), e.upgrades.end());
		for (const UnitInfo::LevelUp &lu : u.levelUps)
		{
			if (!containsAll(e.upgrades, lu.triggeredBy) || intersects(e.upgrades, lu.conflictsWith) || e.rank >= lu.levelCap)
			{
				continue;
			}
			const int gain = std::min(lu.levelsToGain, lu.levelCap - e.rank);
			if (gain > 0)
			{
				e.rank += gain;
				for (const UnitInfo::Level &l : u.levels) // ExperienceLevelFor RW 0x689509: the first level of that rank
				{
					if (l.rank == e.rank)
					{
						e.experience = SimMath::sseFromInt32(l.requiredExperience); // RW 0x688F08: int -> float
						break;
					}
				}
			}
		}
	}
	e.upgradeBuilding = 0;
}

void LivingWorldLogic::autoMarkUnitsForUpgrades()
{
	for (int r = 0; r < (int)m_regions.size(); ++r)
	{
		const int owner = m_regions[(size_t)r].owner;
		if (owner < 0)
		{
			continue;
		}
		const std::vector<Building *> armories = upgradeBuildings(r);
		if (armories.empty())
		{
			continue;
		}
		std::vector<ArmyMember *> cands;
		for (Army &a : m_armies)
		{
			if (a.owner != owner || a.region != r)
			{
				continue;
			}
			for (size_t i : byKey(a))
			{
				if (!fullyUpgraded(a.members[i]))
				{
					cands.push_back(&a.members[i]);
				}
			}
		}
		if (cands.empty())
		{
			continue;
		}
		Stlport::sort(cands, [](const ArmyMember *x, const ArmyMember *y) { return x->rank > y->rank; }); // RW 0x6E40E0
		for (Building *b : armories)
		{
			for (ArmyMember *e : cands)
			{
				if (e->upgradeBuilding == 0 && canUpgradeWith(*b, *e))
				{
					markUpgrade(*e, *b);
				}
			}
		}
	}
}

void LivingWorldLogic::endOfOrdersUpgrades()
{
	for (Army &a : m_armies)
	{
		for (size_t i : byKey(a))
		{
			applyUpgrade(a.members[i]);
		}
	}
	for (RegionState &r : m_regions)
	{
		for (Building &b : r.buildings)
		{
			if (armoryOf(b))
			{
				b.upgradesUsed = 0;
			}
		}
	}
}

bool LivingWorldLogic::canUpgradeArmyMember(int armyId, int memberKey) const
{
	const Army *a = findArmy(armyId);
	if (!a || m_phase != PHASE_ORDERS || a->region < 0 || m_regions[(size_t)a->region].owner != a->owner)
	{
		return false;
	}
	const ArmyMember *e = nullptr;
	for (const ArmyMember &m : a->members)
	{
		e = m.key == memberKey ? &m : e;
	}
	if (!e || e->homeArmy != 0 || e->upgradeBuilding != 0)
	{
		return false;
	}
	for (const Building &b : m_regions[(size_t)a->region].buildings)
	{
		if (canUpgradeWith(b, *e))
		{
			return true;
		}
	}
	return false;
}

bool LivingWorldLogic::orderUpgradeArmyMember(int player, int armyId, int memberKey, std::string *error)
{
	const Army *found = findArmy(armyId);
	if (!found || found->owner != player || !canUpgradeArmyMember(armyId, memberKey))
	{
		setError(error, "that member cannot be upgraded");
		return false;
	}
	for (Army &a : m_armies)
	{
		if (a.id != armyId)
		{
			continue;
		}
		for (ArmyMember &e : a.members)
		{
			if (e.key != memberKey)
			{
				continue;
			}
			for (Building *b : upgradeBuildings(a.region))
			{
				if (canUpgradeWith(*b, e))
				{
					markUpgrade(e, *b);
					log(m_players[(size_t)player].name + " marks " + e.thingTemplate + " of army " + std::to_string(armyId) + " for upgrades");
					return true;
				}
			}
		}
	}
	setError(error, "that member cannot be upgraded");
	return false;
}

bool LivingWorldLogic::cancelUpgradeArmyMember(int player, int armyId, int memberKey, std::string *error)
{
	for (Army &a : m_armies)
	{
		if (a.id != armyId || a.owner != player || m_phase != PHASE_ORDERS)
		{
			continue;
		}
		for (ArmyMember &e : a.members)
		{
			if (e.key == memberKey && e.upgradeBuilding != 0)
			{
				cancelUpgrade(e);
				return true;
			}
		}
	}
	setError(error, "that member is not marked for upgrades");
	return false;
}

void LivingWorldLogic::upgradeCapacity(int player, int region, int *left, int *perTurn) const
{
	*left = 0;
	*perTurn = 0;
	if (region < 0 || region >= (int)m_regions.size() || m_regions[(size_t)region].owner != player)
	{
		return;
	}
	for (const Building &b : m_regions[(size_t)region].buildings)
	{
		if (const BuildingNugget *n = armoryOf(b))
		{
			*left += n->numUpgradesPerTurn - b.upgradesUsed; // vslot 2
			*perTurn += n->numUpgradesPerTurn;               // vslot 0
		}
	}
}

void LivingWorldLogic::regionStats(int player, int region, int out[3][2]) const
{
	for (int i = 0; i < 3; ++i)
	{
		out[i][0] = out[i][1] = 0;
	}
	if (region < 0 || region >= (int)m_regions.size())
	{
		return;
	}
	const RegionState &rs = m_regions[(size_t)region];
	const Region &r = m_regionCampaign->regions[(size_t)region];
	out[0][0] = (int)rs.buildings.size();
	out[0][1] = (int)r.buildingSpots.size();
	int left = 0, perTurn = 0;
	upgradeCapacity(player, region, &left, &perTurn);
	out[1][0] = perTurn - left;
	out[1][1] = perTurn;
	for (const Army &a : m_armies)
	{
		if (a.owner == player && a.region == region && a.heroTemplate.empty())
		{
			out[2][0] = membersCommandPoints(a);
			break;
		}
	}
	out[2][1] = (rs.owner >= 0 && rs.owner != player && !hostile(rs.owner, player)) ? r.allyCPLimit : r.cpLimit;
}

} // namespace LivingWorld
