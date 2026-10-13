// OpenBFME. GPL-3.0.
// See GameLogic/SkirmishAI/AIStatCollector.h for the RW facts and what is not ported.

#include "GameLogic/SkirmishAI/AIStatCollector.h"

#include "Common/StateHash.h"
#include "Common/Thing/KindOfTokens.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/DozerAIUpdate.h"
#include "GameLogic/Object/Object.h"

#include <algorithm>

namespace
{
// the KindOf bits the filters read (TheKindOfNames RW 0xDA0E68; template + 0x108 + bit / 8)
struct StatKinds
{
	int unattackable, immobile, structure, ship, canAttack, hero, support, dozer, expansionPad, notAutoAcquirable, rebuildHole, economyStructure, linkedToFlag,
		baseSite, supplyCenter;
};

const StatKinds &statKinds()
{
	static const StatKinds k = { KindOfTokens::indexOf("UNATTACKABLE"), KindOfTokens::indexOf("IMMOBILE"), KindOfTokens::indexOf("STRUCTURE"),
		KindOfTokens::indexOf("SHIP"), KindOfTokens::indexOf("CAN_ATTACK"), KindOfTokens::indexOf("HERO"), KindOfTokens::indexOf("SUPPORT"),
		KindOfTokens::indexOf("DOZER"), KindOfTokens::indexOf("EXPANSION_PAD"), KindOfTokens::indexOf("NOT_AUTOACQUIRABLE"), KindOfTokens::indexOf("REBUILD_HOLE"),
		KindOfTokens::indexOf("ECONOMY_STRUCTURE"), KindOfTokens::indexOf("LINKED_TO_FLAG"), KindOfTokens::indexOf("BASE_SITE"),
		KindOfTokens::indexOf("SUPPLY_GATHERING_CENTER") };
	return k;
}

bool is(const Object &o, int kind)
{
	return kind >= 0 && o.isKindOf((unsigned)kind);
}

// RW 0x88B897: the object's AI (+ 0x260) answers vslot 0x174 (a dozer interface) and vslot 0x190 (the supply interface): a WorkerAIUpdate
bool isWorker(const Object &o)
{
	const DozerAIUpdate *d = dynamic_cast<const DozerAIUpdate *>(o.getAIUpdateInterface());
	return d && d->isWorker();
}

// RW 0x99E818 (+ 0x10C & 0x440000, + 0x108 bit 7, + 0x120 bit 4 or + 0x10C bit 2)
bool economyFilter(const Object &o)
{
	const StatKinds &k = statKinds();
	return !is(o, k.linkedToFlag) && !is(o, k.unattackable) && is(o, k.structure) && (is(o, k.economyStructure) || is(o, k.supplyCenter));
}

// RW 0x99E8C8
bool structureFilter(const Object &o)
{
	const StatKinds &k = statKinds();
	return (!is(o, k.unattackable) || is(o, k.expansionPad)) && (!is(o, k.notAutoAcquirable) || is(o, k.rebuildHole)) && is(o, k.structure) &&
		!is(o, k.economyStructure) && !is(o, k.linkedToFlag) && !is(o, k.baseSite);
}

// RW 0x99E4C9 (+ 0x10E bit 6, + 0x108 & 0x84, + 0x11F bit 7; then + 0x108 bit 3, + 0x110 bit 26, + 0x120 bit 26, or + 0x108 bit 14 and not RW 0x88B897)
bool armyFilter(const Object &o)
{
	const StatKinds &k = statKinds();
	if (is(o, k.unattackable) || is(o, k.immobile) || is(o, k.structure) || is(o, k.ship))
	{
		return false;
	}
	return is(o, k.canAttack) || is(o, k.hero) || is(o, k.support) || (is(o, k.dozer) && !isWorker(o));
}

// the army list's choice inside RW 0x99E6C9 / 0x99E523: CAN_ATTACK, HERO or SUPPORT -> + 4, else the dozer list + 0x58
bool armyMain(const Object &o)
{
	const StatKinds &k = statKinds();
	return is(o, k.canAttack) || is(o, k.hero) || is(o, k.support);
}

// RW 0x99E467
bool shipFilter(const Object &o)
{
	const StatKinds &k = statKinds();
	return !is(o, k.unattackable) && !is(o, k.structure) && is(o, k.ship);
}

// RW 0x65BC83 / RW 0x6B4E3C + 0x8FF64D: erase the first entry with the id
bool eraseFirst(std::vector<ObjectID> &v, ObjectID id)
{
	auto it = std::find(v.begin(), v.end(), id);
	if (it == v.end())
	{
		return false;
	}
	v.erase(it);
	return true;
}

// RW 0x65BD07: drop the ids TheGameLogic no longer resolves (RW 0x449681)
void dropGone(GameLogic &logic, std::vector<ObjectID> &v)
{
	v.erase(std::remove_if(v.begin(), v.end(), [&logic](ObjectID id) { return logic.findObjectByID(id) == nullptr; }), v.end());
}

void hashList(StateHasher &h, const std::vector<ObjectID> &v)
{
	h.addU32((std::uint32_t)v.size());
	for (ObjectID id : v)
	{
		h.addU32(id);
	}
}
} // namespace

void AIStatCollector::registerObject(const Object &obj)
{
	// RW 0x8E4591
	const ObjectID id = obj.getID();
	bool any = false;
	if (economyFilter(obj))
	{
		m_economy.push_back(id); // RW 0x99E84E -> RW 0x65BD58 (listeners mask 2: none ported)
		any = true;
	}
	if (structureFilter(obj))
	{
		// RW 0x99EA19: EXPANSION_PAD (template + 0x123 bit 0) -> + 0x10, else + 4 (listeners mask 1: none ported)
		(is(obj, statKinds().expansionPad) ? m_pads : m_structures).push_back(id);
		any = true;
	}
	// RW 0x8E45DC .. 0x8E45F0: the position sampler (+ 0x10) is not ported
	if (armyFilter(obj))
	{
		(armyMain(obj) ? m_army : m_dozers).push_back(id); // RW 0x99E6C9 (listeners mask 4: none ported)
		any = true;
	}
	else if (shipFilter(obj))
	{
		m_ships.push_back(id); // RW 0x99E493
		any = true;
	}
	const ThingTemplate *t = obj.getTemplate();
	if (any)
	{
		if (t)
		{
			++m_counts[t->getName()]; // RW 0x8E4622 .. 0x8E465F: a new key starts at 1
		}
	}
	else if (is(obj, statKinds().hero))
	{
		m_heroes.push_back(id); // RW 0x8E4661 .. 0x8E467A
	}
}

void AIStatCollector::unRegisterObject(const Object &obj)
{
	// RW 0x8E480C
	const ObjectID id = obj.getID();
	bool any = false;
	if (economyFilter(obj) && eraseFirst(m_economy, id))
	{
		any = true; // RW 0x99E871
	}
	if (structureFilter(obj) && eraseFirst(is(obj, statKinds().expansionPad) ? m_pads : m_structures, id))
	{
		any = true; // RW 0x99E924
	}
	// RW 0x8E4871: a removed structure / economy entry leaves the position sampler (not ported)
	if (armyFilter(obj) ? eraseFirst(armyMain(obj) ? m_army : m_dozers, id) : (shipFilter(obj) && eraseFirst(m_ships, id)))
	{
		any = true; // RW 0x99E523, else RW 0x99E4B2
	}
	const ThingTemplate *t = obj.getTemplate();
	if (any)
	{
		if (t)
		{
			auto it = m_counts.find(t->getName());
			if (it != m_counts.end())
			{
				--it->second; // RW 0x8E48A9: the key stays
			}
		}
	}
	else if (is(obj, statKinds().hero))
	{
		eraseFirst(m_heroes, id); // RW 0x8E48B3 .. 0x8E48D6
	}
}

void AIStatCollector::update(GameLogic &logic)
{
	// RW 0x8E3CF3: the army list (+ 4; RW 0x99E6B9, the idle army record follows in AITacticalAI::updateIdleArmy), the ships (RW 0x99E446), the structures (+ 4)
	// and the economy list
	dropGone(logic, m_army);
	dropGone(logic, m_ships);
	dropGone(logic, m_structures);
	dropGone(logic, m_economy);
}

int AIStatCollector::countOf(const std::string &templateName) const
{
	auto it = m_counts.find(templateName);
	return it == m_counts.end() ? 0 : it->second; // RW 0x8E3FD6
}

void AIStatCollector::crc(StateHasher &h) const
{
	h.addU32(0x41495354u); // "AIST"
	h.addI32(m_playerIndex);
	for (const std::vector<ObjectID> *l : { &m_army, &m_dozers, &m_ships, &m_structures, &m_pads, &m_economy, &m_heroes })
	{
		hashList(h, *l);
	}
	h.addU32((std::uint32_t)m_counts.size());
	for (const auto &kv : m_counts)
	{
		h.addString(kv.first);
		h.addI32(kv.second);
	}
}
