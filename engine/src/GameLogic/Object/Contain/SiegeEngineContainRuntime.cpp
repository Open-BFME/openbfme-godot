// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// SiegeEngineContain / HordeSiegeEngineContain at runtime. See GameLogic/Object/Contain/SiegeEngineContainRuntime.h for the target facts and the stops. Lane GARRISON-2.

#include "GameLogic/Object/Contain/SiegeEngineContainRuntime.h"

#include "Common/GameCommon.h"
#include "Common/Player.h"
#include "Common/StateHash.h"
#include "Common/Team.h"
#include "Common/Thing/ModuleFactory.h"
#include "Common/Thing/ThingFactory.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ObjectFilterMatch.h"
#include "GameLogic/SimMath.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace
{
const char *const kStopSiege =
	"[S-1105] SiegeEngineContain / HordeSiegeEngineContain (lane GARRISON-2): ported from RW 0x87ECC7 / 0x87FD0F (the crew: CrewFilter, CrewMax, the owner's "
	"objects), 0x87F341 / 0x87EFF8 (the crew list, CONTAINED, LOCOMOTORSET_CONTAINED, SIEGE_CONTAIN), 0x87ED55 (ObjectStatusOfCrew), 0x87F5B4 (the crew on its bones), "
	"0x87ED45 (the crew's speed: count x SpeedPercentPerCrew), 0x87EE53 / 0x87FFC7 (InitialCrew), 0x87EE17 (the crew destroyed with the engine) and 0x87F7B4 / "
	"0x8807CA (a crew member steps 50 away). Not ported: SiegeDeploySpecialPower, SiegeAIUpdate and DynamicPortalBehaviour (the docking against a wall: the deployed "
	"exit onto the wall is not reached), the wall layer (S-164), TransferSelection (client), the crew's team change on capture";

int statusBit(const char *name)
{
	return CombatNames::status(name);
}

void setCondition(Object &obj, int bit, bool on)
{
	if (bit >= 0 && obj.testModelCondition(bit) != on)
	{
		obj.setModelConditionState(bit, on);
	}
}

HordeContainInterface *hordeOf(const Object &obj)
{
	ContainModuleInterface *c = obj.getContain();
	return c ? c->getHordeContainInterface() : nullptr;
}

enum { WEAPONSET_CONTAINED = 20 };
} // namespace

template <class Base, bool Horde>
SiegeEngineContainT<Base, Horde>::SiegeEngineContainT(Thing *thing, const ModuleData *data, const SiegeEngineContainModuleData &siege, const DieMuxData &dieMux)
	: Base(thing, data, siege.m_transport, dieMux)
	, m_siege(&siege)
{
	// RW 0x8804FF .. 0x880529 (HordeSiegeEngineContain): the object's model condition word + 0x124 bit 17 (SIEGE_CONTAIN) is set at construction; SiegeEngineContain
	// leaves it
	if (Horde)
	{
		setCondition(*this->getObject(), CombatNames::modelCondition("SIEGE_CONTAIN"), true);
	}
	this->getObject()->logic().noteStop(kStopSiege);
}

template <class Base, bool Horde>
bool SiegeEngineContainT<Base, Horde>::isCrewCandidate(const Object &obj) const
{
	const Object *container = this->getObject();
	return ObjectFilterMatch::allows(container->logic(), m_siege->m_crewFilter, obj, container->getControllingPlayer()) && m_siege->m_crewMax >= 1;
}

// RW 0x87ECC7 / 0x87FD0F
template <class Base, bool Horde>
bool SiegeEngineContainT<Base, Horde>::isValidContainerFor(const Object &obj, bool checkCapacity, bool checkPath) const
{
	static const int temporarilyDefected = statusBit("TEMPORARILY_DEFECTED");
	if (obj.testStatus((unsigned)temporarilyDefected))
	{
		return false; // status 0x3E
	}
	const Object *container = this->getObject();
	if (isCrewCandidate(obj))
	{
		if ((!checkCapacity || m_crewCount < m_siege->m_crewMax) && obj.getControllingPlayer() == container->getControllingPlayer())
		{
			return true;
		}
		return TransportContain::isValidContainerFor(obj, checkCapacity, checkPath); // RW 0x86A002
	}
	return Base::isValidContainerFor(obj, checkCapacity, checkPath); // SiegeEngineContain: RW 0x86A002; HordeSiegeEngineContain: RW 0x87A48C
}

// RW 0x87F341 / 0x880377
template <class Base, bool Horde>
void SiegeEngineContainT<Base, Horde>::addToContainList(Object *obj)
{
	static const int siegeContain = CombatNames::modelCondition("SIEGE_CONTAIN");
	static const int weaponStateContained = CombatNames::modelCondition("WEAPONSTATE_CONTAINED");
	static const int mine = CombatNames::kindOf("MINE");
	if (!obj || obj->isDestroyed())
	{
		return; // RW 0x87F34D: status 0 (DESTROYED)
	}
	if (!isCrewCandidate(*obj))
	{
		setCondition(*obj, siegeContain, true); // + 0x124 bit 17
		OpenContain::addToContainList(obj);      // RW 0x866987
		if (obj->isKindOf((unsigned)mine))
		{
			m_hasMinePassenger = true; // RW 0x87F3A5: template + 0x10E bit 7 (KindOf MINE)
		}
		return;
	}
	m_crew.push_back(obj);
	++m_crewCount;
	ObjectWeapons *w = obj->getWeapons();
	if (w && w->hasWeaponSetFor(WEAPONSET_CONTAINED))
	{
		w->setWeaponSetFlag(WEAPONSET_CONTAINED, true);    // RW 0x691059(0x14)
		setCondition(*obj, weaponStateContained, true);   // + 0x128 bit 18
	}
	if (!Horde)
	{
		if (AIUpdateInterface *ai = obj->getAIUpdateInterface())
		{
			ai->chooseLocomotorSet(LOCOMOTORSET_CONTAINED); // AI slot 0x238 (RW 0x6680B2) with 10
		}
	}
}

// RW 0x87EFF8 / 0x87FE19 (main slot 0x34)
template <class Base, bool Horde>
void SiegeEngineContainT<Base, Horde>::removeFromContainList(Object *obj)
{
	static const int siegeContain = CombatNames::modelCondition("SIEGE_CONTAIN");
	static const int weaponStateContained = CombatNames::modelCondition("WEAPONSTATE_CONTAINED");
	auto it = std::find(m_crew.begin(), m_crew.end(), obj);
	if (!isCrewCandidate(*obj) || it == m_crew.end())
	{
		setCondition(*obj, siegeContain, false);
		OpenContain::removeFromContainList(obj); // RW 0x865E78
		return;
	}
	m_crew.erase(it); // RW 0x87EDB7
	--m_crewCount;
	if (ObjectWeapons *w = obj->getWeapons())
	{
		w->setWeaponSetFlag(WEAPONSET_CONTAINED, false); // RW 0x691106(0x14)
	}
	setCondition(*obj, weaponStateContained, false);
	if (!Horde)
	{
		if (AIUpdateInterface *ai = obj->getAIUpdateInterface())
		{
			ai->chooseLocomotorSet(LOCOMOTORSET_NORMAL); // AI slot 0x238(0)
		}
	}
}

// RW 0x87FEDA (contain slot 0xE8)
template <class Base, bool Horde>
bool SiegeEngineContainT<Base, Horde>::isContainedHere(const Object &obj) const
{
	return std::find(m_crew.begin(), m_crew.end(), &obj) != m_crew.end() || OpenContain::isContainedHere(obj);
}

// RW 0x87ED55 (contain slot 0xB0): a CrewFilter object's statuses are ObjectStatusOfCrew (data + 0x1A4)
template <class Base, bool Horde>
const ObjectStatusMaskType &SiegeEngineContainT<Base, Horde>::statusMaskFor(const Object *obj) const
{
	const Object *container = this->getObject();
	if (obj && ObjectFilterMatch::allows(container->logic(), m_siege->m_crewFilter, *obj, container->getControllingPlayer()))
	{
		return m_siege->m_objectStatusOfCrew.mask;
	}
	return OpenContain::statusMaskFor(obj);
}

// RW 0x87F5B4 / 0x8805CA: the crew on its bones, then the riders (the base's redeploy)
template <class Base, bool Horde>
void SiegeEngineContainT<Base, Horde>::redeployOccupants()
{
	const std::vector<Object *> crew(m_crew.begin(), m_crew.end());
	this->putRidersAtBones(crew);
	Base::redeployOccupants();
}

// RW 0x87ED45 (contain slot 0xD4): fild count; fmul SpeedPercentPerCrew
template <class Base, bool Horde>
float SiegeEngineContainT<Base, Horde>::getCrewPowerMultiplier() const
{
	return (float)SimMath::mulD((double)m_crewCount, (double)m_siege->m_speedPercentPerCrew);
}

// RW 0x87F099 / 0x87FEA4
template <class Base, bool Horde>
void SiegeEngineContainT<Base, Horde>::removeAllContained()
{
	while (!m_crew.empty())
	{
		Object *c = m_crew.front();
		this->removeFromContain(c); // contain slot 0xA4
		if (!m_crew.empty() && m_crew.front() == c)
		{
			m_crew.pop_front(); // a removal that took another path (never in the ported classes)
			--m_crewCount;
		}
	}
	OpenContain::removeAllContained(); // RW 0x866675
}

// RW 0x87FDAD
template <class Base, bool Horde>
void SiegeEngineContainT<Base, Horde>::onRemoving(Object *obj)
{
	static const int siegeContain = CombatNames::modelCondition("SIEGE_CONTAIN");
	if (!obj)
	{
		return;
	}
	setCondition(*obj, siegeContain, false); // + 0x126 bit 1
	TransportContain::onRemoving(obj);       // RW 0x86ABDC
}

// RW 0x87EBF1
template <class Base, bool Horde>
bool SiegeEngineContainT<Base, Horde>::isSpecificRiderFreeToExit(const Object &rider) const
{
	if (m_exitBlocked)
	{
		return false;
	}
	return TransportContain::isSpecificRiderFreeToExit(rider); // RW 0x86A2D0
}

// RW 0x87EE17 / 0x87FDDD
template <class Base, bool Horde>
void SiegeEngineContainT<Base, Horde>::onDelete()
{
	OpenContain::onDelete(); // RW 0x86708F
	const std::vector<Object *> crew(m_crew.begin(), m_crew.end());
	for (Object *c : crew)
	{
		c->friend_setContainedBy(nullptr); // + 0x27C
		c->logic().destroyObject(c);        // RW 0x62BBAB
	}
}

// RW 0x87EE53 / 0x87FFC7
template <class Base, bool Horde>
void SiegeEngineContainT<Base, Horde>::createPayload()
{
	static const int justBuilt = CombatNames::modelCondition("JUST_BUILT");
	Object *container = this->getObject();
	GameLogic &logic = container->logic();
	const ThingTemplate *t = m_siege->m_initialCrewName.empty() ? nullptr : logic.things().findTemplate(m_siege->m_initialCrewName);
	if (!m_siege->m_initialCrewName.empty() && !t)
	{
		logic.reportError("SiegeEngineContain of " + container->getTemplate()->getName() + ": InitialCrew " + m_siege->m_initialCrewName + " is not a template");
	}
	Player *owner = container->getControllingPlayer();
	if (t && owner)
	{
		// RW 0x87EEB2 / 0x87FFF5: enableLoadSounds off and on around the crew (client side)
		for (int i = 0; i < m_siege->m_initialCrewCount; ++i)
		{
			Object *o = logic.newObject(t, owner->getDefaultTeam(), ObjectStatusMaskType{}); // RW 0x6D165E(t, player + 0x30C)
			if (!o)
			{
				continue;
			}
			o->setPosition(container->getPosition());
			if (!this->isValidContainerFor(*o, true, false))
			{
				continue; // RW 0x87EEEE: the object stays where it was made
			}
			float fade = 0.0f;
			if (const FieldValue *v = static_cast<const ThingTemplate *>(o->getTemplate())->getFinalOverride()->findField("BuildFadeInOnCreateTime"))
			{
				if (const float *f = std::get_if<float>(v))
				{
					fade = *f;
				}
			}
			const int frames = SimMath::truncToInt32(SimMath::mulf32((float)LOGICFRAMES_PER_SECOND, fade)); // RW 0x87EF05: (int)(5 * tt + 0x354), x87 then truncation
			if (frames > 0)
			{
				o->setSpecialModelConditionState(justBuilt, (UnsignedInt)frames); // RW 0x68B581(0xDA, frames)
			}
			// RW 0x87EF26 .. 0x87EF92: a named container names its crew "<name><i>" (the script name, + 0x88: the port has no object names)
			if (Horde)
			{
				this->openContainAdd(o); // RW 0x880121: OpenContain RW 0x8674C2 directly
			}
			else
			{
				this->addToContain(o); // RW 0x87EFC9: contain slot 0x9C
			}
		}
	}
	TransportContain::createPayload(); // RW 0x86A1FA
}

// RW 0x87F7B4 / 0x880B9C
template <class Base, bool Horde>
void SiegeEngineContainT<Base, Horde>::exitObjectViaDoor(Object *obj, int door)
{
	if (!obj || m_exitBlocked)
	{
		return;
	}
	if (Horde)
	{
		// RW 0x880B9C: a horde rider: its members out of the world first, then every member (released from the horde, slot 0xA8), each through RW 0x8807CA, then the
		// horde object
		if (HordeContainInterface *h = hordeOf(*obj))
		{
			std::vector<Object *> members;
			if (const ContainModuleInterface::ContainedItemsList *items = obj->getContain()->getContainedItemsList())
			{
				members.assign(items->begin(), items->end());
			}
			for (Object *m : members)
			{
				if (!m->isInWorld())
				{
					h->releaseMemberForGarrison(m);
					exitOne(m, door);
				}
			}
			for (Object *m : members)
			{
				h->releaseMemberForGarrison(m);
				exitOne(m, door);
			}
		}
	}
	exitOne(obj, door);
}

template <class Base, bool Horde>
void SiegeEngineContainT<Base, Horde>::exitOne(Object *obj, int door)
{
	Object *container = this->getObject();
	if (isCrewCandidate(*obj))
	{
		// RW 0x87FA62 .. 0x87FB76 / 0x880848 .. 0x880961: out of the contain, 50 away from the engine along the line from it (z 0), the exit path (CMD_FROM_AI)
		if (Horde)
		{
			this->openContainRemove(obj); // RW 0x880852: OpenContain RW 0x8665BE
		}
		else
		{
			this->removeFromContain(obj);
		}
		const Coord3D &p = *obj->getPosition();
		const Coord3D &c = *container->getPosition();
		float dx = SimMath::subf32(p.x, c.x), dy = SimMath::subf32(p.y, c.y), dz = SimMath::subf32(p.z, c.z);
		const float len = (float)SimMath::length3d(dx, dy, dz);
		if (len > 0.0f)
		{
			dx = SimMath::divf32(dx, len); // Coord3D::normalize
			dy = SimMath::divf32(dy, len);
		}
		Coord3D target{ SimMath::addf32(SimMath::mulf32(dx, 50.0f), p.x), SimMath::addf32(SimMath::mulf32(dy, 50.0f), p.y), 0.0f }; // [0xBD88C4] = 50
		if (AIUpdateInterface *ai = obj->getAIUpdateInterface())
		{
			// RW 0x87FAD8 .. 0x87FB6A: no obstacle ignored (RW 0x66831A(0)), the next path frame now + 30 (AI + 0x178, S-1103), the destination adjustment RW 0x6F3C87
			// (S-1100), the exit path [target, target] (RW 0x77113C)
			ai->mover().ignoreObstacle(PATHFIND_INVALID_ID);
			std::vector<Coord3D> path{ target, target };
			ai->aiFollowPath(path, container, CMD_FROM_AI, true);
		}
		return;
	}
	// a rider: the deployed branch (SiegeDeploySpecialPower's deployed state, RW 0x8C9C01: the ladder's "Ladder04" / the tower's front on the wall layer) is not
	// reached: SiegeDeploySpecialPower is not ported (S-1103)
	Base::exitObjectViaDoor(obj, door); // OpenContain RW 0x8682D9 / HordeTransportContain RW 0x87A89E
}

template <class Base, bool Horde>
void SiegeEngineContainT<Base, Horde>::crc(StateHasher &h) const
{
	Base::crc(h);
	h.addU32((std::uint32_t)m_crew.size());
	for (const Object *o : m_crew)
	{
		h.addU32(o->getID());
	}
	h.addI32(m_crewCount);
	h.addBool(m_hasMinePassenger);
	h.addBool(m_exitBlocked);
}

template class SiegeEngineContainT<TransportContain, false>;
template class SiegeEngineContainT<HordeTransportContain, true>;

void SiegeEngineContainRegistry::registerClasses(ModuleFactory &modules)
{
	modules.bindTypedData<SiegeEngineContainBehaviorData>("SiegeEngineContain", MODULETYPE_BEHAVIOR);
	modules.bindTypedData<HordeSiegeEngineContainBehaviorData>("HordeSiegeEngineContain", MODULETYPE_BEHAVIOR);
	modules.bindModuleProc("SiegeEngineContain", MODULETYPE_BEHAVIOR, [](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
		const SiegeEngineContainBehaviorData *d = dynamic_cast<const SiegeEngineContainBehaviorData *>(data);
		if (!d)
		{
			throw std::logic_error("SiegeEngineContain: the module data is not typed (SiegeEngineContainBehaviorData)");
		}
		return std::make_unique<SiegeEngineContain>(thing, data, d->siege, d->m_dieMux);
	});
	modules.bindModuleProc("HordeSiegeEngineContain", MODULETYPE_BEHAVIOR, [](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
		const HordeSiegeEngineContainBehaviorData *d = dynamic_cast<const HordeSiegeEngineContainBehaviorData *>(data);
		if (!d)
		{
			throw std::logic_error("HordeSiegeEngineContain: the module data is not typed (HordeSiegeEngineContainBehaviorData)");
		}
		return std::make_unique<HordeSiegeEngineContain>(thing, data, d->siege, d->m_dieMux);
	});
}

std::vector<std::string> SiegeEngineContainRegistry::stopLines()
{
	return { kStopSiege };
}
