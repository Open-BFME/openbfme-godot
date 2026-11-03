// OpenBFME. GPL-3.0.
// See GameLogic/LivingWorld/LivingWorldAutoResolveBattle.h.  Lane WOTR-2.

#include "GameLogic/LivingWorld/LivingWorldAutoResolveBattle.h"

#include "Common/NumericState.h"
#include "Common/RandomValue.h"
#include "GameLogic/LivingWorld/StlportAlgorithm.h"
#include "GameLogic/SimMath.h"

#include <algorithm>
#include <deque>
#include <map>
#include <stdexcept>

namespace LivingWorld
{

struct AutoResolveBattle::Unit
{
	AutoResolveUnitSpec spec;
	const UnitInfo *info = nullptr; // null: no ThingTemplate of that name (RW 0x9A38A1 +0x2C null)
	const AutoResolveWeapon *weapon = nullptr;
	const AutoResolveArmor *armor = nullptr;
	const AutoResolveBody *body = nullptr;
	const AutoResolveCombatChain *chain = nullptr;
	const AutoResolveLeadership *leadership = nullptr;
	int type = AR_INVALID;
	int level = 0;          // summary entry +0x0C
	int record = 0;         // +0x30
	float hp = 0.0f;        // +0x0C
	float startHp = 0.0f;   // +0x10
	float maxHp = 0.0f;     // +0x14
	bool attacked = false;  // +0x18
	bool rankedUp = false;  // WOTR2 milestone 5: gained a rank above 1 here (cleared after the creation's gainLevel, RW 0x9EE028)
	const AutoResolveLeadership::Bonus *leader = nullptr; // +0x1C
	Unit *target = nullptr; // +0x20
};

struct AutoResolveBattle::Record
{
	AutoResolvePlayerSpec spec;
	std::vector<std::pair<int, size_t>> armies; // the multimap round -> army (RW 0x6B7DDE: upper_bound insert, equal rounds keep their order)
	std::optional<SciencePurchasePointTracker> tracker;    // battle + 0x48 (lane WOTR-2)
	const AutoResolveBonusSchedule::Bonus *science = nullptr; // battle + 0x54
};

namespace
{
// RW 0x8FC5C1: a unit goes before another when its level is higher
struct LevelGreater
{
	template <class U>
	bool operator()(const U *a, const U *b) const
	{
		return b->level < a->level;
	}
};

bool hasAll(const std::vector<std::string> &have, const std::vector<std::string> &need)
{
	for (const std::string &n : need)
	{
		if (std::find(have.begin(), have.end(), n) == have.end())
		{
			return false;
		}
	}
	return true;
}

bool hasAny(const std::vector<std::string> &have, const std::vector<std::string> &any)
{
	for (const std::string &n : any)
	{
		if (std::find(have.begin(), have.end(), n) != have.end())
		{
			return true;
		}
	}
	return false;
}

const UnitInfo::AutoResolveSet *currentSet(const std::vector<UnitInfo::AutoResolveSet> &sets, const std::vector<std::string> &upgrades)
{
	for (const UnitInfo::AutoResolveSet &s : sets) // RW 0x73C222 / 0x73C1E5 with the test RW 0x73BE09
	{
		if (!hasAny(upgrades, s.excludedUpgrades) && hasAll(upgrades, s.requiredUpgrades))
		{
			return &s;
		}
	}
	return nullptr;
}

// the attacker heap element {unit, type, priority} and its comparator (RW 0x8FB983: "priority greater")
template <class U>
struct Attacker
{
	U *unit = nullptr;
	AutoResolveCombatChain::Target target;
};
struct PriorityGreater
{
	template <class A>
	bool operator()(const A &a, const A &b) const
	{
		return a.target.priority > b.target.priority;
	}
};
} // namespace

int UnitInfo::levelForExperience(float experience) const
{
	const int points = SimMath::truncToInt32(experience); // RW 0x79D141: (int)trunc(experience)
	int rank = 0;
	for (const Level &l : levels)
	{
		if (l.requiredExperience > points)
		{
			break;
		}
		rank = l.rank;
	}
	return rank;
}

int UnitInfo::awardForExperience(float experience) const
{
	const int points = SimMath::truncToInt32(experience);
	int award = 0;
	for (const Level &l : levels)
	{
		if (l.requiredExperience > points)
		{
			break;
		}
		award = l.experienceAward;
	}
	return award;
}

AutoResolveBattle::AutoResolveBattle(const AutoResolveStores &stores, const UnitInfoSource &units, const std::vector<AutoResolvePlayerSpec> &players,
	GameLogicRandom &rng)
	: m_stores(stores)
	, m_unitInfo(units)
	, m_rng(rng)
{
	// SetUpAutoResolveBattle (RW 0x6BD3F3): the records, each player's armies into its multimap
	for (const AutoResolvePlayerSpec &p : players)
	{
		Record r;
		r.spec = p;
		if (p.side != 0 && p.side != 1)
		{
			throw std::logic_error("AutoResolveBattle: a player side must be 0 or 1");
		}
		const AutoResolveReinforcementSchedule &schedule = m_stores.scheduleForSide(p.side);
		int counted = 0;
		for (size_t a = 0; a < p.armies.size(); ++a)
		{
			const int round = p.armies[a].garrison ? 0 : schedule.roundForArmyNumber(counted++); // RW 0x8244C0
			auto at = std::upper_bound(r.armies.begin(), r.armies.end(), round, [](int v, const std::pair<int, size_t> &e) { return v < e.first; });
			r.armies.insert(at, std::make_pair(round, a));
		}
		m_records.push_back(r);
	}
	// the ctor (RW 0x8FF0AC)
	m_round = 0;
	for (size_t i = 0; i < m_records.size(); ++i)
	{
		Record &r = m_records[i];
		r.tracker = r.spec.rankTracker; // RW 0x8FF19B: the copy (RW 0x8FAD90), none without the record's + 0x28
		updateScienceBonus(i);          // RW 0x8FF1C2
		addUnits(m_sides[r.spec.side], r.spec.extraUnits, (int)i);
	}
	for (size_t i = 0; i < m_records.size(); ++i) // buildReinforcementMaps (RW 0x8FEED6)
	{
		const Record &r = m_records[i];
		auto &maps = m_reinforcements[r.spec.side];
		for (const auto &e : r.armies)
		{
			auto it = std::lower_bound(maps.begin(), maps.end(), e.first, [](const std::pair<int, std::vector<Unit *>> &m, int v) { return m.first < v; });
			if (it == maps.end() || it->first != e.first)
			{
				it = maps.insert(it, std::make_pair(e.first, std::vector<Unit *>()));
			}
			addUnits(it->second, r.spec.armies[e.second].units, (int)i);
		}
	}
	addUnitsForCurrentRound();
	checkWinner();
}

AutoResolveBattle::~AutoResolveBattle() = default;

AutoResolveBattle::Unit *AutoResolveBattle::makeUnit(const AutoResolveUnitSpec &spec, int record)
{
	auto u = std::make_unique<Unit>();
	u->spec = spec;
	u->record = record;
	const UnitInfo info = m_unitInfo(spec.thingTemplate);
	if (info.known)
	{
		m_storage.push_back(std::move(u)); // keep ownership first: the info below points into a stable copy
		Unit *p = m_storage.back().get();
		auto copy = std::make_unique<UnitInfo>(info);
		p->info = copy.get();
		m_infos.push_back(std::move(copy));
		p->type = info.autoResolveUnitType;
		if (p->type < 0 || p->type >= AR_UNIT_TYPE_COUNT)
		{
			throw std::logic_error("AutoResolveBattle: " + spec.thingTemplate + " has an AutoResolveUnitType out of range");
		}
		const UnitInfo::AutoResolveSet *w = currentSet(info.autoResolveWeapon, spec.upgrades);
		const UnitInfo::AutoResolveSet *a = currentSet(info.autoResolveArmor, spec.upgrades);
		p->weapon = w ? m_stores.findWeapon(w->name) : &m_stores.defaultWeapon();
		p->armor = a ? m_stores.findArmor(a->name) : &m_stores.defaultArmor();
		p->body = info.autoResolveBody.empty() ? &m_stores.defaultBody() : m_stores.findBody(info.autoResolveBody);
		p->chain = info.autoResolveCombatChain.empty() ? &m_stores.defaultCombatChain() : m_stores.findCombatChain(info.autoResolveCombatChain);
		p->leadership = info.autoResolveLeadership.empty() ? nullptr : m_stores.findLeadership(info.autoResolveLeadership);
		if (!p->weapon || !p->armor || !p->body || !p->chain || (!info.autoResolveLeadership.empty() && !p->leadership))
		{
			// retail resolves these names while the object INI loads (an unknown one is an INI error there)
			throw std::runtime_error("[S-1323] " + spec.thingTemplate + " names an AutoResolve template the data does not define");
		}
		if (!info.levels.empty() && info.levelForExperience(p->spec.experience) == 0)
		{
			p->spec.experience = (float)info.levels.front().requiredExperience; // RW 0x9EE094: no level reached -> gainLevel (experience = RequiredExperience)
		}
		p->level = info.levelForExperience(p->spec.experience);
		p->maxHp = maxHitpoints(*p);
		p->hp = p->maxHp;
		p->startHp = p->maxHp;
		return p;
	}
	m_dropped.push_back(spec.thingTemplate);
	return nullptr;
}

void AutoResolveBattle::addUnits(std::vector<Unit *> &dest, const std::vector<AutoResolveUnitSpec> &specs, int record)
{
	// RW 0x8FE33B: append each entry's unit (an entry without a ThingTemplate is dropped again), then sort the whole vector
	for (const AutoResolveUnitSpec &s : specs)
	{
		Unit *u = makeUnit(s, record);
		if (u)
		{
			dest.push_back(u);
		}
	}
	Stlport::sort(dest, LevelGreater());
}

void AutoResolveBattle::addUnitsForCurrentRound()
{
	// RW 0x8FDEE9
	for (int side = 0; side < 2; ++side)
	{
		auto &maps = m_reinforcements[side];
		auto it = std::lower_bound(maps.begin(), maps.end(), m_round, [](const std::pair<int, std::vector<Unit *>> &m, int v) { return m.first < v; });
		if (it == maps.end() || it->first != m_round)
		{
			continue;
		}
		std::vector<Unit *> joining = it->second;
		maps.erase(it);
		std::vector<Unit *> &units = m_sides[side];
		const size_t old = units.size();
		units.insert(units.end(), joining.begin(), joining.end());
		std::inplace_merge(units.begin(), units.begin() + (std::ptrdiff_t)old, units.end(), LevelGreater()); // stable: the same result as STLport's
	}
}

float AutoResolveBattle::maxHitpoints(const Unit &u) const
{
	return u.body->hitpointsForLevel(u.level); // RW 0x9A359B
}

void AutoResolveBattle::update()
{
	// RW 0x8FEDF2
	if (m_winner != -1)
	{
		return;
	}
	++m_round;
	addUnitsForCurrentRound();
	for (int side = 0; side < 2; ++side) // RW 0x8FAF8E -> RW 0x9A35B1
	{
		for (Unit *u : m_sides[side])
		{
			u->startHp = u->hp;
			u->maxHp = maxHitpoints(*u);
			u->attacked = false;
		}
	}
	assignLeadership();
	bool resolved = true;
	do
	{
		chooseTargets();
		resolved = attackRound();
	} while (!resolved);
	completeRound();
	for (int side = 0; side < 2; ++side) // RW 0x8FEB3C
	{
		Stlport::sort(m_sides[side], LevelGreater());
	}
	checkWinner();
}

void AutoResolveBattle::runToEnd()
{
	while (m_winner == -1)
	{
		update();
	}
}

void AutoResolveBattle::assignLeadership()
{
	// RW 0x8FEC1C
	struct Entry
	{
		const AutoResolveLeadership::Bonus *bonus;
		const AutoResolveLeadership *leadership;
		const Unit *leader;
	};
	for (int side = 0; side < 2; ++side)
	{
		std::vector<Unit *> &units = m_sides[side];
		std::vector<Entry> entries;
		for (Unit *u : units)
		{
			if (!u->leadership)
			{
				continue;
			}
			const AutoResolveLeadership::Bonus *b = u->leadership->bonusForLevel(u->level);
			if (b)
			{
				entries.push_back(Entry{ b, u->leadership, u });
			}
		}
		Stlport::sort(entries, [](const Entry &a, const Entry &b) { return b.bonus->priority < a.bonus->priority; }); // RW 0x8FCE3F
		for (const Entry &e : entries)
		{
			int remaining = e.bonus->maximumUnitsAffected;
			const bool front = e.leadership->affectsHigherLevelFirst;
			const int n = (int)units.size();
			for (int i = 0; remaining > 0 && i < n; ++i)
			{
				Unit *u = units[(size_t)(front ? i : n - 1 - i)];
				if (u->leader == nullptr && (e.leadership->affects & (1u << u->type)) != 0 && u != e.leader && u->hp > 0.0f)
				{
					u->leader = e.bonus;
					--remaining;
				}
			}
		}
	}
}

void AutoResolveBattle::chooseTargets()
{
	// RW 0x8FE401
	typedef Attacker<Unit> A;
	struct Listed
	{
		Unit *unit;
		int count;
	};
	std::vector<A> heaps[2];
	std::deque<Listed> lists[AR_UNIT_TYPE_COUNT][2];
	int attackers[2] = { 0, 0 };
	int targets[2] = { 0, 0 };
	for (int side = 0; side < 2; ++side)
	{
		for (Unit *u : m_sides[side])
		{
			u->target = nullptr;
			if (!u->attacked && u->startHp > 0.0f && u->weapon->canDamage()) // RW 0x9A3573
			{
				++attackers[side];
				A a;
				a.unit = u;
				if (u->chain->highestPriorityTarget(&a.target))
				{
					heaps[side].push_back(a);
					Stlport::pushHeap(heaps[side], PriorityGreater());
				}
			}
			if (u->hp > 0.0f && u->body->canBeAttacked) // RW 0x9A34B5
			{
				++targets[side];
				lists[u->type][side].push_back(Listed{ u, 0 });
			}
		}
	}
	for (int side = 0; side < 2; ++side)
	{
		const int enemy = side == 0 ? 1 : 0;
		if (targets[enemy] == 0)
		{
			continue;
		}
		const int quota = attackers[side] / targets[enemy];
		int remainder = attackers[side] % targets[enemy];
		std::vector<A> &heap = heaps[side];
		while (!heap.empty())
		{
			A top = heap.front();
			Stlport::popHeap(heap, PriorityGreater());
			heap.pop_back();
			bool assigned = false;
			std::deque<Listed> &list = lists[top.target.type][enemy];
			while (!list.empty())
			{
				Listed &l = list.front();
				if (l.count < quota || (l.count == quota && remainder > 0))
				{
					assigned = true;
					if (l.count == quota)
					{
						--remainder;
					}
					++l.count;
					top.unit->target = l.unit;
					break;
				}
				list.pop_front();
			}
			if (!assigned)
			{
				A next;
				next.unit = top.unit;
				if (top.unit->chain->nextTarget(top.target, &next.target))
				{
					heap.push_back(next);
					Stlport::pushHeap(heap, PriorityGreater());
				}
			}
		}
	}
}

int AutoResolveBattle::experienceValue(const Unit &u) const
{
	// RW 0x9A35DE (see the header's inference): the award of the level the target's experience reached, times the entry's count; a horde's members
	const float xp = u.spec.experience;
	if (u.info->horde && !u.info->hordePayload.empty())
	{
		int total = 0;
		for (const auto &m : u.info->hordePayload)
		{
			const UnitInfo member = m_unitInfo(m.first);
			total += member.awardForExperience(xp) * m.second * u.spec.count;
		}
		return total;
	}
	return u.info->awardForExperience(xp) * u.spec.count;
}

void AutoResolveBattle::addExperience(Unit &u, int points)
{
	// RW 0x9A34F0 -> RW 0x9EE04A: addExperiencePoints((float)points, ...) then the level check; the summary entry takes the new level (RW 0x9EE009)
	u.spec.experience = SimMath::addf32(u.spec.experience, (float)points);
	const int before = u.level;
	u.level = u.info->levelForExperience(u.spec.experience);
	if (u.level > before && u.level > 1)
	{
		u.rankedUp = true;
	}
}

bool AutoResolveBattle::attackRound()
{
	// RW 0x8FE6AF
	bool resolved = true;
	for (int side = 0; side < 2; ++side)
	{
		for (Unit *u : m_sides[side])
		{
			if (u->attacked || !(u->startHp > 0.0f) || !u->weapon->canDamage() || !u->target)
			{
				continue;
			}
			Unit *t = u->target;
			if (!(t->hp > 0.0f))
			{
				resolved = false;
				continue;
			}
			bool hit = true;
			const bool noMiss = false; // RW 0x6B3025: the logic's +0xB4 flag with the tutorial running [inference: no tutorial]
			if (!noMiss)
			{
				++m_draws;
				const int roll = m_rng.getValue(1, 100, "LivingWorldAutoResolveBattle.cpp", 0x331);
				hit = roll > u->weapon->missPercentChance; // RW 0x8FE752: cmp roll, miss; jle (a miss)
			}
			if (hit)
			{
				const float maxHp = maxHitpoints(*u);
				const float dmg = u->weapon->damageForRound(u->level, u->startHp, maxHp, t->type);
				const float leaderWeapon = u->leader ? u->leader->weapon : 1.0f;
				const float armorScalar = t->armor->damageScalar(u->type);
				const float leaderArmor = t->leader ? t->leader->armor : 1.0f;
				Record &ra = m_records[(size_t)u->record];
				const Record &rt = m_records[(size_t)t->record];
				const float sciWeapon = ra.science ? ra.science->weapon : 1.0f; // RW 0x8FE80E: + 0x54 [record] + 4 / + 8
				const float sciArmor = rt.science ? rt.science->armor : 1.0f;
				float f = SimMath::mulf32(rt.spec.armor, ra.spec.weapon); // RW 0x8FE844 .. 0x8FE86E (SSE, this order)
				f = SimMath::mulf32(f, sciArmor);
				f = SimMath::mulf32(f, sciWeapon);
				f = SimMath::mulf32(f, leaderArmor);
				f = SimMath::mulf32(f, armorScalar);
				f = SimMath::mulf32(f, leaderWeapon);
				f = SimMath::mulf32(f, dmg);
				t->hp = SimMath::subf32(t->hp, f);
				if (!(0.0f < t->hp))
				{
					float xp = SimMath::mulf32(ra.spec.experience, (float)experienceValue(*t)); // RW 0x8FE89F
					if (ra.tracker)
					{
						ra.tracker->addSkillPoints(xp, true); // RW 0x8FE8C6 (lane WOTR-2)
					}
					if (u->leader)
					{
						xp = SimMath::mulf32(u->leader->experience, xp); // RW 0x8FE8D7
					}
					addExperience(*u, SimMath::truncToInt32(xp)); // RW 0x8FE8E8 cvttss2si
					++m_kills[side];
					m_killEvents.push_back(KillEvent{ u->record, t->record, t->spec }); // RW 0x8FE8F4
				}
			}
			u->attacked = true;
		}
	}
	return resolved;
}

void AutoResolveBattle::removeUnit(std::vector<Unit *> &side, size_t index)
{
	// RW 0x8FE971
	const Unit *u = side[index];
	if (u->spec.buildingSlot >= 0)
	{
		m_destroyedBuildings.push_back(DestroyedBuilding{ u->spec.buildingRegion, u->spec.buildingSlot });
	}
	if (u->info && u->info->hero && u->spec.armyId != 0) // RW 0x8FE9B6: KindOf HERO (thing + 0x113 bit 2) and an army (+ 0x28) -> battle + 0x6C
	{
		m_deadHeroes.push_back(u->spec);
	}
	side.erase(side.begin() + (std::ptrdiff_t)index);
}

void AutoResolveBattle::completeRound()
{
	// RW 0x8FEA4D
	auto isDead = [](const Unit *u) { return !u->info || (!(u->hp > 0.0f) && !u->body->leaveInArmySummary); }; // RW 0x9A34CD
	for (int side = 0; side < 2; ++side)
	{
		std::vector<Unit *> &units = m_sides[side];
		for (size_t i = units.size(); i-- > 0;)
		{
			Unit *u = units[i];
			if (isDead(u))
			{
				removeUnit(units, i);
				continue;
			}
			// RW 0x9A360F
			const float newMax = maxHitpoints(*u);
			if (newMax != u->maxHp)
			{
				if (u->maxHp != 0.0f)
				{
					u->hp = SimMath::mulf32(SimMath::divf32(newMax, u->maxHp), u->hp);
				}
			}
			if (m_round < 500 && u->hp > 0.0f)
			{
				const float h = NumericState::pc24Add(u->body->regenForLevel(u->level), u->hp); // x87 fadd (PC24)
				u->hp = newMax < h ? newMax : h;
			}
			u->leader = nullptr;
		}
	}
	for (size_t i = 0; i < m_records.size(); ++i) // RW 0x8FEAB5
	{
		updateScienceBonus(i);
	}
	if (m_round > 999)
	{
		m_winner = m_sides[0].size() < m_sides[1].size() ? 1 : 0;
		const int loser = m_winner == 0 ? 1 : 0;
		std::vector<Unit *> &units = m_sides[loser];
		for (size_t i = units.size(); i-- > 0;)
		{
			units[i]->hp = 0.0f;
			if (isDead(units[i]))
			{
				removeUnit(units, i);
			}
		}
	}
}

void AutoResolveBattle::updateScienceBonus(size_t record)
{
	// RW 0x8FB695 (lane WOTR-2)
	Record &r = m_records[record];
	r.science = (r.tracker && r.spec.scienceSchedule) ? r.spec.scienceSchedule->bonusFor(r.tracker->getSciencePurchasePoints()) : nullptr;
}

const SciencePurchasePointTracker *AutoResolveBattle::recordTracker(size_t record) const
{
	const Record &r = m_records.at(record);
	return r.tracker ? &*r.tracker : nullptr;
}

const AutoResolveBonusSchedule::Bonus *AutoResolveBattle::scienceBonus(size_t record) const
{
	return m_records.at(record).science;
}

void AutoResolveBattle::checkWinner()
{
	// RW 0x8FAFB8
	bool alive[2] = { false, false };
	for (int side = 0; side < 2; ++side)
	{
		for (const Unit *u : m_sides[side])
		{
			if (u->hp > 0.0f && u->body->canBeAttacked)
			{
				alive[side] = true;
				break;
			}
		}
	}
	if (!alive[1])
	{
		m_winner = 0;
	}
	else if (!alive[0])
	{
		m_winner = 1;
	}
}

std::vector<AutoResolveBattle::Survivor> AutoResolveBattle::survivors() const
{
	std::vector<Survivor> out;
	for (int side = 0; side < 2; ++side)
	{
		for (const Unit *u : m_sides[side])
		{
			Survivor s;
			s.spec = u->spec;
			s.record = u->record;
			s.hp = u->hp;
			s.level = u->level;
			s.rankedUp = u->rankedUp;
			out.push_back(s);
		}
	}
	return out;
}

std::vector<int> AutoResolveBattle::enteredArmies() const
{
	std::vector<int> out;
	for (const Record &r : m_records)
	{
		for (const auto &e : r.armies)
		{
			if (e.first <= m_round) // RW 0x8FC8A9: the multimap walk stops at the first round past the battle's
			{
				out.push_back(r.spec.armies[e.second].armyId);
			}
		}
	}
	return out;
}

} // namespace LivingWorld
