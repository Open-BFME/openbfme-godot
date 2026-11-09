// OpenBFME. GPL-3.0.
// See GameLogic/AI/WeaponCommands.h (lane FACT-A).

#include "GameLogic/AI/WeaponCommands.h"

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "GameLogic/AI/AICommands.h"
#include "GameLogic/AI/AIGroup.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/SlavedUpdate.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/WeaponNugget.h"
#include "GameLogic/Object/Object.h"

namespace
{
const char *const kLane = "FACT-A";

WeaponCommands::Stats &stats()
{
	static WeaponCommands::Stats s;
	return s;
}

const GameMessageArgument *arg(const GameMessage &m, size_t i, GameMessageArgumentDataType t)
{
	const GameMessageArgument *a = m.getArgument(i);
	return a && a->type == t ? a : nullptr;
}

bool atObject(GameLogic &logic, const GameMessage &m)
{
	const GameMessageArgument *slot = arg(m, 0, ARGUMENTDATATYPE_INTEGER);
	const GameMessageArgument *target = arg(m, 1, ARGUMENTDATATYPE_OBJECTID);
	const GameMessageArgument *shots = arg(m, 2, ARGUMENTDATATYPE_INTEGER);
	if (!slot || !target || !shots || !logic.players().getNthPlayer(m.getPlayerIndex()))
	{
		++stats().rejected;
		return false;
	}
	++stats().atObject;
	Object *victim = logic.findObjectByID(target->objectID);
	if (!victim)
	{
		return true; // RW 0x77A380
	}
	AIGroup group(logic, AICommands::selection(logic, m.getPlayerIndex()));
	if (!group.setWeaponLockForGroup(slot->integer, LOCKED_TEMPORARILY)) // RW 0x77A394 (RW 0x76FAB7)
	{
		return true;
	}
	++stats().lockedGroups;
	logic.noteStop(WeaponCommands::acceptanceStops()[0]);
	group.groupIdle(CMD_FROM_AI);                                         // RW 0x77A3A8
	(void)group.setWeaponLockForGroup(slot->integer, LOCKED_TEMPORARILY); // RW 0x77A3B2
	// RW 0x77A3C1: groupAttackObject(false, victim, maxShots, CMD_FROM_PLAYER); a horde attacks the horde of a member it was pointed at (as MSG_DO_ATTACK_OBJECT)
	for (Object *o : group.members())
	{
		AIUpdateInterface *ai = o->getAIUpdateInterface();
		if (!ai || o == victim)
		{
			continue;
		}
		Object *t = victim;
		if (o->isKindOf((unsigned)CombatNames::kinds().horde) && victim->getContainedBy() && victim->getContainedBy()->getContain()
			&& victim->getContainedBy()->getContain()->getHordeContainInterface())
		{
			t = victim->getContainedBy();
		}
		stats().attackOrders += ai->aiAttackObject(t, CMD_FROM_PLAYER, shots->integer) ? 1u : 0u; // the shot limit is forwarded (lane PLAY-2)
	}
	group.clearTemporarySpeedCaps(); // RW 0x77B00A -> 0x77A342 -> RW 0x76FD04
	return true;
}

bool atPosition(GameLogic &logic, const GameMessage &m, bool withLocation)
{
	const GameMessageArgument *slot = arg(m, 0, ARGUMENTDATATYPE_INTEGER);
	const GameMessageArgument *loc = withLocation ? arg(m, 1, ARGUMENTDATATYPE_LOCATION) : nullptr;
	const GameMessageArgument *shots = arg(m, withLocation ? 2 : 1, ARGUMENTDATATYPE_INTEGER);
	if (!slot || !shots || (withLocation && !loc) || !logic.players().getNthPlayer(m.getPlayerIndex()))
	{
		++stats().rejected;
		return false;
	}
	++(withLocation ? stats().atLocation : stats().here);
	AIGroup group(logic, AICommands::selection(logic, m.getPlayerIndex()));
	if (!group.setWeaponLockForGroup(slot->integer, LOCKED_TEMPORARILY)) // RW 0x77A327 / 0x77A414 (RW 0x76FAB7)
	{
		return true;
	}
	++stats().lockedGroups;
	logic.noteStop(WeaponCommands::acceptanceStops()[0]);
	if (withLocation)
	{
		group.groupAttackPosition(loc->location, shots->integer, CMD_FROM_PLAYER); // RW 0x77A33A (RW 0x77229A)
	}
	else
	{
		// RW 0x77A334: groupAttackPosition(NULL, ...): every member attacks its own position (BFME2 decomp AIGroupAttackTeam.cpp, RW 0x77229A)
		for (Object *o : group.members())
		{
			if (AIUpdateInterface *ai = o->getAIUpdateInterface())
			{
				ai->aiAttackPosition(*o->getPosition(), shots->integer, CMD_FROM_PLAYER);
			}
		}
	}
	stats().attackOrders += group.members().size();
	group.clearTemporarySpeedCaps(); // RW 0x77A342 -> RW 0x76FD04
	return true;
}
// RW 0x77B6EF .. 0x77B7C7: the status NO_AUTO_ACQUIRE (0x17) of an object; on a HORDE (template KindOf byte + 0x115 bit 5) through RW 0x748650 -> RW 0x293D3B,
// which sets it on the horde and every member (BFME2 decomp ObjectConditionAndPassengerWeaponSet.cpp, INFERENCE: the top object is the containing horde)
void setNoAutoAcquire(Object &o, bool on)
{
	static const int noAuto = CombatNames::status("NO_AUTO_ACQUIRE");
	Object *top = &o;
	if (o.getContainedBy() && o.getContainedBy()->isKindOf((unsigned)CombatNames::kinds().horde))
	{
		top = o.getContainedBy();
	}
	if (top->isKindOf((unsigned)CombatNames::kinds().horde) && top->getContain() && top->getContain()->getContainedItemsList())
	{
		for (Object *m : *top->getContain()->getContainedItemsList())
		{
			m->setStatus((unsigned)noAuto, on);
		}
	}
	top->setStatus((unsigned)noAuto, on);
}

// RW 0x693C53..0x693C7B: inspect each live weapon of the member; RW 0x6CBABB..0x6CBAC9 asks each nugget's slot 0x34.
bool hasSlaveAttackWeapon(const Object &o)
{
	const ObjectWeapons *weapons = o.getWeapons();
	if (!weapons)
	{
		return false;
	}
	for (int slot = 0; slot < WEAPONSLOT_COUNT; ++slot)
	{
		const Weapon *weapon = weapons->weaponInSlot(slot);
		const WeaponTemplate *t = weapon ? weapon->getTemplate() : nullptr;
		if (t)
		{
			for (const auto &n : t->m_nuggets)
			{
				if (n && n->kind() == NUGGET_SLAVE_ATTACK) // RW 0x910E97: vtable 0xC7B9B0 slot 0x34 returns true
				{
					return true;
				}
			}
		}
	}
	return false;
}

// MSG_TOGGLE_NO_AUTO_ACQUIRE (RW 0x77B6C4): per-member or source-based toggle; related-object lookup and AI state tests are in PORT18_RW.md.
bool toggleNoAutoAcquire(GameLogic &logic, const GameMessage &m)
{
	const GameMessageArgument *src = arg(m, 0, ARGUMENTDATATYPE_OBJECTID);
	if (!src || !logic.players().getNthPlayer(m.getPlayerIndex()))
	{
		++stats().rejected;
		return false;
	}
	++stats().noAutoAcquire;
	logic.noteStop(WeaponCommands::acceptanceStops()[1]);
	static const int noAuto = CombatNames::status("NO_AUTO_ACQUIRE");
	const bool perObject = src->objectID == INVALID_ID;
	bool value = false;
	if (!perObject)
	{
		if (Object *s = logic.findObjectByID(src->objectID))
		{
			value = !s->testStatus((unsigned)noAuto);
		}
	}
	Object *related = nullptr; // RW 0x77B6D7: initialized once, not at the start of each member iteration
	for (ObjectID id : AICommands::selection(logic, m.getPlayerIndex()))
	{
		Object *o = logic.findObjectByID(id);
		if (!o)
		{
			continue;
		}
		// RW 0x77B731..0x77B75A: overwrite the retained pointer only when the interface and the weapon test both succeed.
		if (SlavedUpdateInterface *slaved = SlavedUpdateInterface::of(*o))
		{
			Object *master = logic.findObjectByID(slaved->getSlaverID());
			if (master && hasSlaveAttackWeapon(*o)) // RW 0x693C2D requires a resolvable master
			{
				related = logic.findObjectByID(slaved->getSlaverID());
			}
		}
		const bool has = o->testStatus((unsigned)noAuto);
		if (perObject)
		{
			value = !has;
		}
		else if (has == value)
		{
			continue;
		}
		// RW 0x77B789 / 0x77B7B3: only a selected HORDE takes the group status path;
		// an individually selected member is set directly even when it belongs to a horde.
		if (o->isKindOf((unsigned)CombatNames::kinds().horde))
		{
			// RW 0x69458A..0x694591: the horde interface is mandatory, not just any contain.
			if (o->getContain() && o->getContain()->getHordeContainInterface())
			{
				o->setStatusAcrossHorde((unsigned)noAuto, value); // RW 0x748650 -> 0x694569
			}
		}
		else
		{
			o->setStatus((unsigned)noAuto, value);
		}
		if (value)
		{
			AIUpdateInterface *ai = o->getAIUpdateInterface();
			if (ai && ai->isStateActive())
			{
				ai->aiIdle(CMD_FROM_PLAYER);
			}
		}
		if (related)
		{
			// RW 0x77B7A6 / 0x77B7F2: set the related object directly, not through the horde path.
			related->setStatus((unsigned)noAuto, value);
			if (value)
			{
				AIUpdateInterface *ai = related->getAIUpdateInterface();
				if (ai && ai->isStateActive()) // RW 0x77B80E, AI vslot 0x1BC
				{
					ai->aiIdle(CMD_FROM_PLAYER);
				}
			}
		}
	}
	return true;
}
} // namespace

void WeaponCommands::registerHandlers(GameLogicDispatch &d)
{
	d.registerHandler(MSG_TOGGLE_NO_AUTO_ACQUIRE, kLane, toggleNoAutoAcquire);
	d.registerHandler(MSG_DO_WEAPON, kLane, [](GameLogic &l, const GameMessage &m) { return atPosition(l, m, false); });
	d.registerHandler(MSG_DO_WEAPON_AT_LOCATION, kLane, [](GameLogic &l, const GameMessage &m) { return atPosition(l, m, true); });
	d.registerHandler(MSG_DO_WEAPON_AT_OBJECT, kLane, atObject);
}

const WeaponCommands::Stats &WeaponCommands::stats()
{
	return ::stats();
}

std::vector<std::string> WeaponCommands::acceptanceStops()
{
	return {
		"[S-1982] fire-weapon commands (FACT-A, RW 0x77A304 / 0x77A3CB / 0x77A34C): the button's weapon slot is locked for the group, MSG_DO_WEAPON(_AT_LOCATION) "
		"attacks the position with MaxShotsToFire (AIGroup::groupAttackPosition, lane PLAY-2), MSG_DO_WEAPON_AT_OBJECT attacks the target with its "
		"MaxShotsToFire; not ported: the riders of a container (RW 0x77229A / 0x77208A hand "
		"the order to riders allowed to fire) and the members' distance order of groupAttackObject",
		"[S-1983] MSG_TOGGLE_NO_AUTO_ACQUIRE (FACT-A, RW 0x77B6C4): the group's NO_AUTO_ACQUIRE status toggles (a horde with its members) and a member turned on stops "
		"when its AI state is active, including the related object (behavior slot 26, RW 0x68CBE2); not ported: the button's image toggle (RW 0x75D366, client)",
	};
}
