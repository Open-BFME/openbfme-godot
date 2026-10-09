// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
// See GameLogic/Object/Contain/TunnelContainRuntime.h for the target facts, the donors and the stop.

#include "GameLogic/Object/Contain/TunnelContainRuntime.h"

#include "Common/NumericState.h"
#include "Common/Player.h"
#include "Common/StateHash.h"
#include "Common/Thing/ModuleFactory.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Damage.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameLogic/Object/Object.h"

#include <algorithm>
#include <stdexcept>

namespace
{
HordeContainInterface *hordeOfObject(const Object &o)
{
	ContainModuleInterface *c = const_cast<Object &>(o).getContain();
	return c ? c->getHordeContainInterface() : nullptr;
}

const FieldParse *tunnelFieldParse()
{
	static const FieldParse table[] = { // RW 0xC04E58
		{ "TimeForFullHeal", INI::parseDurationReal, nullptr, 0 },
		{ nullptr, nullptr, nullptr, 0 }
	};
	return table;
}
} // namespace

// lane GARRISON-2: RW Player + 0x308 (the player's constructor makes it; the port on first use, which no simulation step can tell apart)
TunnelTracker &Player::tunnelTracker()
{
	if (!m_tunnelTracker)
	{
		m_tunnelTracker = std::make_shared<TunnelTracker>();
	}
	return *m_tunnelTracker;
}

// ---------------------------------------------------------------------------------------------------------------------------------
// TunnelTracker
// ---------------------------------------------------------------------------------------------------------------------------------
int TunnelTracker::containMax(const GameLogic &logic)
{
	return logic.settings().maxTunnelCapacity; // GameData + 0xA98
}

bool TunnelTracker::isValidContainerFor(const Object *obj, bool checkCapacity, const GameLogic &logic) const
{
	static const int aircraft = CombatNames::kindOf("AIRCRAFT");
	static const int hordeMember = CombatNames::status("HORDE_MEMBER");
	if (!obj || obj->isKindOf((unsigned)aircraft)) // template + 0x109 bit 4
	{
		return false;
	}
	if (checkCapacity && !obj->testStatus((unsigned)hordeMember)) // status 0x26: a horde's member follows its horde in
	{
		return containCount() < containMax(logic); // signed
	}
	return true;
}

void TunnelTracker::removeFromContain(Object *obj)
{
	auto it = std::find(m_riders.begin(), m_riders.end(), obj);
	if (it != m_riders.end())
	{
		m_riders.erase(it);
	}
}

bool TunnelTracker::isInContainer(const Object *obj) const
{
	return std::find(m_riders.begin(), m_riders.end(), obj) != m_riders.end();
}

void TunnelTracker::onTunnelCreated(const Object &tunnel)
{
	++m_tunnelCount;
	m_tunnelIds.push_back(tunnel.getID());
}

bool TunnelTracker::onTunnelDestroyed(const Object &tunnel, GameLogic &logic)
{
	(void)logic;
	--m_tunnelCount;
	m_tunnelIds.remove(tunnel.getID()); // RW 0x87EDB7
	return m_tunnelCount == 0;
}

void TunnelTracker::healObjects(float framesForFullHeal, unsigned now)
{
	// RW 0x8FA30C forward over the riders with RW 0x8FA190 (the list does not change while it heals)
	const std::vector<Object *> riders(m_riders.begin(), m_riders.end());
	for (Object *obj : riders)
	{
		BodyModuleInterface *body = obj->getBodyModule();
		if (!body)
		{
			continue;
		}
		DamageInfo info; // RW 0x66365E
		info.m_input.m_damageType = DAMAGE_HEALING; // 7
		info.m_input.m_deathType = DEATH_NONE;      // 1
		const float elapsed = (float)(std::uint32_t)(now - obj->getContainedFrame()); // x87 fild of the unsigned difference
		info.m_input.m_amount = elapsed < framesForFullHeal ? NumericState::pc24Div(body->getMaxHealth(), framesForFullHeal) : body->getMaxHealth();
		body->attemptHealing(info); // body slot 4
		++m_healHits;
	}
}

void TunnelTracker::crc(StateHasher &h) const
{
	h.addU32((std::uint32_t)m_tunnelIds.size());
	for (ObjectID id : m_tunnelIds)
	{
		h.addU32(id);
	}
	h.addU32((std::uint32_t)m_riders.size());
	for (const Object *o : m_riders)
	{
		h.addU32(o->getID());
	}
	h.addU32(m_tunnelCount);
}

// ---------------------------------------------------------------------------------------------------------------------------------
// TunnelContain
// ---------------------------------------------------------------------------------------------------------------------------------
void TunnelContainBehaviorData::buildFieldParse(MultiIniFieldParse &p)
{
	HordeGarrisonContainBehaviorData::buildFieldParse(p); // RW 0x87D2A3 and the DieMux table
	p.add(tunnelFieldParse(), (unsigned)offsetof(TunnelContainBehaviorData, m_framesForFullHeal));
}

TunnelContain::TunnelContain(Thing *thing, const ModuleData *data, const TunnelContainBehaviorData &tunnel)
	: GarrisonContain(thing, data, tunnel.horde.m_garrison, tunnel.m_dieMux, &tunnel.horde)
	, m_tunnel(&tunnel)
{
	getObject()->logic().noteStop(stopLine()); // the stop reaches GameLogic::report().stops in every game that has a tunnel
}

void TunnelContain::registerClasses(ModuleFactory &modules)
{
	modules.bindTypedData<TunnelContainBehaviorData>("TunnelContain", MODULETYPE_BEHAVIOR);
	modules.bindModuleProc("TunnelContain", MODULETYPE_BEHAVIOR, [](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
		const TunnelContainBehaviorData *d = dynamic_cast<const TunnelContainBehaviorData *>(data);
		if (!d)
		{
			throw std::logic_error("TunnelContain: the module data is not typed (TunnelContainBehaviorData)");
		}
		return std::make_unique<TunnelContain>(thing, data, *d);
	});
}

const char *TunnelContain::stopLine()
{
	return "[S-1107] TunnelContain (lane GARRISON-2): ported from RW 0x880E29 .. 0x881404 and the player's TunnelTracker RW 0x8FA152 .. 0x8FA4E8 (the network's "
	       "riders, MaxTunnelCapacity, onBuildComplete, the heal, the last tunnel lets everyone out); not ported: the tracker's nemesis (RW 0x8FA702, read by the "
	       "AI's tunnel network guard state, not ported), the safe occlusion frame and the unload sound of a rider that leaves (client), the exposed stealth units";
}

TunnelTracker *TunnelContain::tracker() const
{
	Player *p = getObject()->getControllingPlayer();
	return p ? &p->tunnelTracker() : nullptr;
}

void TunnelContain::onBuildComplete()
{
	if (!m_doBuildComplete) // create slot 0xC RW 0x880E22: + 0x9E4
	{
		return;
	}
	m_doBuildComplete = false;
	if (TunnelTracker *t = tracker())
	{
		t->onTunnelCreated(*getObject());
		m_registered = true;
	}
}

const ContainModuleInterface::ContainedItemsList *TunnelContain::getContainedItemsList() const
{
	TunnelTracker *t = tracker();
	return t ? &t->riders() : &m_contained; // RW 0x8810AB (an unowned tunnel has no network: its own, empty list)
}

unsigned TunnelContain::getContainCount() const
{
	TunnelTracker *t = tracker();
	return t ? (unsigned)t->containCount() : 0u; // RW 0x881072
}

int TunnelContain::getContainMax() const
{
	return TunnelTracker::containMax(getObject()->logic()); // RW 0x881098
}

bool TunnelContain::isValidContainerFor(const Object &obj, bool checkCapacity, bool checkPath) const
{
	// RW 0x88103B: HordeGarrisonContain's test without the capacity, then the network's
	if (!GarrisonContain::isValidContainerFor(obj, false, checkPath))
	{
		return false;
	}
	TunnelTracker *t = tracker();
	return t && t->isValidContainerFor(&obj, checkCapacity, getObject()->logic());
}

bool TunnelContain::isContainedHere(const Object &obj) const
{
	TunnelTracker *t = tracker();
	return t && t->isInContainer(&obj); // RW 0x8811DD
}

void TunnelContain::addToContainList(Object *obj)
{
	if (TunnelTracker *t = tracker())
	{
		t->addToContainList(obj); // RW 0x880F0E
	}
}

void TunnelContain::removeFromContain(Object *obj)
{
	GarrisonContain::removeFromContain(obj); // RW 0x87D04B
	if (TunnelTracker *t = tracker())
	{
		t->removeFromContain(obj); // RW 0x880F4E .. 0x880F6D (a no-op for an object not in the network)
	}
}

void TunnelContain::removeAllContained()
{
	// RW 0x881367: a copy of the network's riders; a HORDE rider's members first (RW 0x880D7A through its contain's iterate), then the rider
	TunnelTracker *t = tracker();
	if (!t)
	{
		return;
	}
	static const int horde = CombatNames::kindOf("HORDE");
	const std::vector<Object *> riders(t->riders().begin(), t->riders().end());
	for (Object *r : riders)
	{
		if (r->isKindOf((unsigned)horde) && r->getContain() && r->getContain()->getContainedItemsList())
		{
			const ContainedItemsList &m = *r->getContain()->getContainedItemsList();
			const std::vector<Object *> members(m.begin(), m.end());
			for (Object *member : members)
			{
				removeFromContain(member);
			}
		}
		removeFromContain(r);
	}
}

void TunnelContain::onContaining(Object *obj, bool wasSelected)
{
	GarrisonContain::onContaining(obj, wasSelected); // RW 0x87BEC8
	obj->setDisabled(3, (UnsignedInt)UPDATE_SLEEP_FOREVER); // RW 0x692432(3): DISABLED_HELD
}

void TunnelContain::onRemoving(Object *obj)
{
	GarrisonContain::onRemoving(obj); // RW 0x87BFA6
	obj->clearDisabled(3);            // RW 0x692443(3)
	if (!obj->isInWorld() && !obj->isDestroyed())
	{
		obj->logic().friend_containEnterWorld(*obj); // RW 0x68E31F (+ 0x474)
	}
	obj->setPosition(getObject()->getPosition()); // RW 0x70C201: out at this tunnel
	obj->setDrawableHidden(false);                // RW 0x6718FB(0); the safe occlusion frame (+ 0x444) and the unload sound are the client's (S-1107)
}

UpdateSleepTime TunnelContain::update()
{
	GarrisonContain::update(); // RW 0x87C8DF
	TunnelTracker *t = tracker();
	if (!t)
	{
		return UPDATE_SLEEP_FOREVER;
	}
	if (garrison().m_doHealing) // data + 0x98 HealObjects
	{
		t->healObjects(m_tunnel->m_framesForFullHeal, getObject()->logic().getFrame());
	}
	// RW 0x881302 .. 0x881357: the last attacker of the tunnel becomes the network's nemesis (S-1107)
	return UPDATE_SLEEP_NONE;
}

void TunnelContain::leaveNetwork()
{
	// RW 0x88121A
	if (!m_registered)
	{
		return;
	}
	TunnelTracker *t = tracker();
	if (!t)
	{
		return;
	}
	Object *self = getObject();
	if (!t->onTunnelDestroyed(*self, self->logic()))
	{
		// RW 0x8FA4E8: the riders that entered by this tunnel count as entered by the first tunnel left (RW 0x6901AE)
		if (Object *valid = self->logic().findObjectByID(t->tunnelIds().front()))
		{
			if (TunnelContain *v = dynamic_cast<TunnelContain *>(valid->getContain()))
			{
				const std::vector<Object *> riders(t->riders().begin(), t->riders().end());
				for (Object *r : riders)
				{
					if (r->getContainedBy() == self)
					{
						v->objectOnContainedBy(r);
					}
				}
			}
		}
		// RW 0x881105: a HORDE rider whose AI goal is this tunnel and whose members are all in comes out here
		static const int horde = CombatNames::kindOf("HORDE");
		const std::vector<Object *> riders(t->riders().begin(), t->riders().end());
		for (Object *r : riders)
		{
			AIUpdateInterface *ai = r->getAIUpdateInterface();
			if (!ai || ai->stateMachine().goalObject() != self || !r->isKindOf((unsigned)horde))
			{
				continue;
			}
			HordeContainInterface *h = hordeOfObject(*r);
			if (h && !h->allMembersEntered()) // horde slot 0xF4 false
			{
				removeFromContain(r);
			}
		}
	}
	else
	{
		removeAllContained(); // the last tunnel: everyone comes out (slot 0xA8 RW 0x881367)
	}
	m_registered = false;
}

void TunnelContain::onDie(const DieModuleInterface::Event &event)
{
	if (dieMux().isDieApplicable(*getObject(), event)) // RW 0x8D29A9
	{
		leaveNetwork();
	}
}

void TunnelContain::onDelete()
{
	OpenContain::onDelete(); // RW 0x86708F (this module's own list: empty, the riders are the network's)
	leaveNetwork();
}

void TunnelContain::crc(StateHasher &h) const
{
	GarrisonContain::crc(h);
	h.addBool(m_doBuildComplete);
	h.addBool(m_registered);
	if (const Player *p = getObject()->getControllingPlayer())
	{
		h.addBool(p->tunnelTrackerIfAny() != nullptr);
		if (const TunnelTracker *t = p->tunnelTrackerIfAny())
		{
			t->crc(h);
		}
	}
}
