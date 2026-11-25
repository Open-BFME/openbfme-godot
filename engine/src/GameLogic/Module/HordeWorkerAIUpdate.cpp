// OpenBFME. GPL-3.0.
// See GameLogic/Module/HordeWorkerAIUpdate.h (lane MODGAP-2 / LOCAL-101; editions: both RotWK 2.01 and BFME2 1.06).

#include "GameLogic/Module/HordeWorkerAIUpdate.h"

#include "Common/StateHash.h"
#include "Common/Thing/ModuleFactory.h"
#include "Common/Xfer.h"
#include "GameLogic/Combat/CombatQueries.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/Module/DozerAIUpdate.h"
#include "GameLogic/Module/HordeContain.h"
#include "GameLogic/Module/UnportedModule.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ObjectTypes.h"
#include "GameLogic/SimMath.h"

#include <vector>

namespace
{
// RW 0x0086D3F3 (HordeContainInterface slot +0x94): true if any contained member has Object+0x110 bit 29 (status 61 = ENCLOSED).
bool hasEnclosedMember(const Object &hordeObj)
{
	const ContainModuleInterface *c = hordeObj.getContain();
	if (!c)
	{
		return false;
	}
	const ContainModuleInterface::ContainedItemsList *list = c->getContainedItemsList();
	if (!list)
	{
		return false;
	}
	constexpr unsigned kStatusEnclosed = 61u; // bit 29 of status word 1 (Object + 0x110)
	for (const Object *member : *list)
	{
		if (member && member->testStatus(kStatusEnclosed))
		{
			return true;
		}
	}
	return false;
}

// RW 0x0086F9CA (HordeContainInterface slot +0xA4): true if any member released into m_garrisonEntering is still alive in GameLogic.
bool hasLiveEnteringMember(const HordeContainInterface &hc, GameLogic &logic)
{
	for (ObjectID id : hc.membersEntering())
	{
		if (logic.findObjectByID(id) != nullptr)
		{
			return true;
		}
	}
	return false;
}

// RW 0x00403175 (Coord3D::normalize)
void normalizeCoord3D(Coord3D &v)
{
	const float len = (float)SimMath::length3d(v.x, v.y, v.z);
	if (len != 0.0f)
	{
		const float invLen = SimMath::divf32(1.0f, len);
		v.x = SimMath::mulf32(v.x, invLen);
		v.y = SimMath::mulf32(v.y, invLen);
		v.z = SimMath::mulf32(v.z, invLen);
	}
}
} // namespace

// RW 0x64EE0E / BFME2 0x49AD15
// RW 0x89E4DA (HordeWorkerAIUpdate::HordeWorkerAIUpdate)
HordeWorkerAIUpdate::HordeWorkerAIUpdate(Thing *thing, const ModuleData *data)
	: HordeAIUpdate(thing, data)
{
}

// RW 0x0082D7F2 (ActionManager::canRepairObject): checks used by HordeWorkerAIUpdate::privateRepair and bfmePrivateCommand42.
bool HordeWorkerAIUpdate::canRepairObject(const Object &me, const Object &target)
{
	if (me.getRelationship(target) != ALLIES) // RW 0x82D82D..0x82D835
	{
		return false;
	}
	if (target.isEffectivelyDead()) // RW 0x82D83B
	{
		return false;
	}
	if (target.isKindOfName("NO_COLLIDE") || target.isKindOfName("UNATTACKABLE")) // RW 0x82D85D
	{
		return false;
	}
	if (me.isUnderConstruction() || target.isUnderConstruction()) // RW 0x82D86B..0x82D887
	{
		return false;
	}
	if (!me.isKindOfName("DOZER")) // RW 0x82D89D (template + 0x109 & 0x40)
	{
		return false;
	}
	if (!target.isKindOfName("STRUCTURE")) // RW 0x82D8A6 (template + 0x108 & 0x80)
	{
		return false;
	}
	const BodyModuleInterface *body = target.getBodyModule();
	if (!body || !(body->getHealth() < body->getMaxHealth())) // RW 0x82D8AC..0x82D8CE
	{
		return false;
	}
	if (target.isKindOfName("CANNOT_BE_REPAIRED")) // RW 0x82D8D3
	{
		return false;
	}
	if (me.getContainedBy() != nullptr) // RW 0x82D8FF (Object + 0x27C)
	{
		return false;
	}
	return true;
}

// RW 0x0089E623 / BFME2 0x49AE5E: clears AIUpdateInterface+0x3CA when none of the three tracked targets exist.
void HordeWorkerAIUpdate::clearRepairFlagIfTargetsGone()
{
	Object *obj = getObject();
	if (!obj)
	{
		return;
	}
	GameLogic &logic = obj->logic();
	if (logic.findObjectByID(m_pendingRepairTarget) != nullptr)
	{
		return;
	}
	if (logic.findObjectByID(m_activeRepairTarget) != nullptr)
	{
		return;
	}
	if (logic.findObjectByID(m_command42Target) != nullptr)
	{
		return;
	}
	m_repairMoveFlag = false; // RW 0x89E660
}

// RW 0x0089E57C / BFME2 0x49ADB7: cancels active repair, returns released members to formation if any are alive, clears targets and +0x3CA.
void HordeWorkerAIUpdate::cancelRepair()
{
	if (m_activeRepairTarget != 0)
	{
		if (Object *obj = getObject())
		{
			if (ContainModuleInterface *c = obj->getContain())
			{
				if (HordeContainInterface *hc = c->getHordeContainInterface())
				{
					if (hasLiveEnteringMember(*hc, obj->logic())) // RW 0x89E59D (slot +0xA4, RW 0x0086F9CA)
					{
						hc->returnToFormation(true); // RW 0x89E5AD (slot +0x10, RW 0x008759FF)
					}
				}
			}
		}
	}
	m_pendingRepairTarget = 0;
	m_activeRepairTarget = 0;
	m_command42Target = 0;
	m_repairMoveFlag = false;
}

// RW 0x0089E884 / BFME2 0x49B0BF: records pending/active repair target, sets +0x3CA, and moves the horde to the edge of the target.
void HordeWorkerAIUpdate::moveToRepairTarget(Object *target, CommandSourceType cmdSource)
{
	Object *me = getObject();
	if (!me || !target)
	{
		return;
	}
	m_repairMoveFlag = true; // RW 0x89E897
	m_pendingRepairTarget = target->getID(); // RW 0x89E8A5
	m_activeRepairTarget = target->getID();  // RW 0x89E8AE

	const Coord3D *myPos = me->getPosition();
	const Coord3D *targetPos = target->getPosition();
	Coord3D diff{
		SimMath::subf32(myPos->x, targetPos->x),
		SimMath::subf32(myPos->y, targetPos->y),
		SimMath::subf32(myPos->z, targetPos->z)
	};
	normalizeCoord3D(diff); // RW 0x89E8E8 -> RW 0x403175

	const float sum = SimMath::addf32(CombatQueries::boundingCircleRadius(*target), CombatQueries::boundingCircleRadius(*me)); // RW 0x89E912..0x89E917
	const Coord3D dest{
		SimMath::addf32(targetPos->x, SimMath::mulf32(sum, diff.x)),
		SimMath::addf32(targetPos->y, SimMath::mulf32(sum, diff.y)),
		targetPos->z
	};

	// RW 0x89E95F calls AIUpdateInterface::privateMoveToPosition (RW 0x661F85) directly, bypassing aiDoCommand.
	m_internalCommand = true;
	aiMoveToPosition(dest, cmdSource);
	m_internalCommand = false;
}

void HordeWorkerAIUpdate::enterTargetDirectly(Object *target, CommandSourceType cmdSource)
{
	if (!target)
	{
		return;
	}
	// RW 0x89E41F -> AIUpdateInterface::privateEnter (RW 0x66D85F), bypassing HordeWorkerAIUpdate::aiDoCommand.
	m_internalCommand = true;
	HordeAIUpdate::aiEnter(target, cmdSource);
	m_internalCommand = false;
}

// RW 0x0089E669 / BFME2 0x49AEA4
UpdateSleepTime HordeWorkerAIUpdate::update()
{
	Object *obj = getObject();
	if (!obj) // RW 0x89E670
	{
		return UPDATE_SLEEP_FOREVER;
	}

	ContainModuleInterface *contain = obj->getContain();
	HordeContainInterface *hc = contain ? contain->getHordeContainInterface() : nullptr;
	if (!hc) // RW 0x89E688
	{
		return UPDATE_SLEEP_FOREVER;
	}

	GameLogic &logic = obj->logic();
	clearRepairFlagIfTargetsGone(); // RW 0x89E69C -> RW 0x0089E623

	if (m_pendingRepairTarget != 0) // RW 0x89E6A1
	{
		Object *target = logic.findObjectByID(m_activeRepairTarget); // RW 0x89E6AE
		if (!target || target->isEffectivelyDead()) // RW 0x89E6BF..0x89E6CA
		{
			m_internalCommand = true;
			aiIdle(CMD_FROM_AI); // RW 0x89E731 -> RW 0x5E821A
			m_internalCommand = false;
			m_pendingRepairTarget = 0; // RW 0x89E717
			return UPDATE_SLEEP_NONE;
		}

		if (isIdle() && !hasEnclosedMember(*obj)) // RW 0x89E6D4 (slot +0x1B8), RW 0x89E6E6 (slot +0x94 RW 0x0086D3F3)
		{
			if (Object *repairTarget = logic.findObjectByID(m_activeRepairTarget)) // RW 0x89E6F4..0x89E707
			{
				privateRepair(repairTarget, CMD_FROM_AI); // RW 0x89E711 (vslot +0xB0)
			}
			m_pendingRepairTarget = 0; // RW 0x89E717
			return UPDATE_SLEEP_NONE;
		}

		return HordeAIUpdate::update(); // RW 0x89E7BC -> RW 0x89E2D0
	}

	if (m_activeRepairTarget != 0) // RW 0x89E738
	{
		Object *target = logic.findObjectByID(m_activeRepairTarget); // RW 0x89E742
		BodyModuleInterface *body = target ? target->getBodyModule() : nullptr;
		const bool fullHealth = body && body->getMaxHealth() > 0.0f && !(body->getHealth() < body->getMaxHealth()); // RW 0x89E765..0x89E772 (healthRatio == 1.0f)
		if (!target || target->isEffectivelyDead() || fullHealth)
		{
			hc->returnToFormation(true); // RW 0x89E77A (slot +0x10, RW 0x008759FF)
			m_activeRepairTarget = 0;    // RW 0x89E77D
		}
		return UPDATE_SLEEP_NONE;
	}

	if (m_command42Target != 0) // RW 0x89E786
	{
		if (Object *target = logic.findObjectByID(m_command42Target)) // RW 0x89E790
		{
			enterTargetDirectly(target, CMD_FROM_AI); // RW 0x89E7A5 -> RW 0x89E41F (HordeAIUpdate::privateEnter)
		}
		m_command42Target = 0; // RW 0x89E7AA
		return UPDATE_SLEEP_NONE;
	}

	return HordeAIUpdate::update(); // RW 0x89E7BC -> RW 0x89E2D0
}

// RW 0x0089E7C1 / BFME2 0x49AFFC
void HordeWorkerAIUpdate::privateRepair(Object *obj, CommandSourceType cmdSource)
{
	Object *me = getObject();
	if (!me || !obj || !canRepairObject(*me, *obj)) // RW 0x89E7E2 -> RW 0x82D7F2
	{
		return;
	}

	ContainModuleInterface *c = me->getContain();
	HordeContainInterface *hc = c ? c->getHordeContainInterface() : nullptr;
	if (!c || !hc) // RW 0x89E7F2..0x89E7FB
	{
		return;
	}

	const ContainModuleInterface::ContainedItemsList *l = c->getContainedItemsList();
	if (!l)
	{
		return;
	}

	// RW 0x89E812 (RW 0x76CB49): copy list because releaseMemberForGarrison mutates the contain list.
	const std::vector<Object *> members(l->begin(), l->end());
	for (Object *member : members)
	{
		if (!member || !member->isKindOfName("DOZER")) // RW 0x89E82A (template + 0x109 & 0x40)
		{
			continue;
		}
		hc->releaseMemberForGarrison(member); // RW 0x89E838 (slot +0xA8, RW 0x0086EBF4)
		if (DozerAIUpdate *d = dynamic_cast<DozerAIUpdate *>(member->getAIUpdateInterface()))
		{
			(void)cmdSource;
			d->repair(*obj); // RW 0x89E84C -> RW 0x7714C1; preserve the source-aware repair gate
		}
		m_activeRepairTarget = obj->getID(); // RW 0x89E85A
	}
}

// RW 0x0089E990 / BFME2 0x49B1CB (HordeWorkerAIUpdate::privateEnter / command 0x42)
void HordeWorkerAIUpdate::bfmePrivateCommand42(Object *obj, CommandSourceType cmdSource)
{
	Object *me = getObject();
	if (!me || !obj)
	{
		return;
	}
	BodyModuleInterface *body = obj->getBodyModule();
	const bool damaged = body && body->getMaxHealth() > 0.0f && (body->getHealth() < body->getMaxHealth()); // RW 0x89E9A4..0x89E9AD (healthRatio < 1.0f)
	if (damaged && canRepairObject(*me, *obj)) // RW 0x89E9BE -> RW 0x82D7F2
	{
		m_command42Target = obj->getID(); // RW 0x89E9D1
		moveToRepairTarget(obj, cmdSource); // RW 0x89E9D7 -> RW 0x0089E884
	}
	if (m_command42Target == 0) // RW 0x89E9DC
	{
		enterTargetDirectly(obj, cmdSource); // RW 0x89E9EC -> RW 0x89E41F
	}
}

// RW 0x0089E9F8 / BFME2 0x49B231 (HordeWorkerAIUpdate::aiDoCommand)
void HordeWorkerAIUpdate::commandAccepted(CommandSourceType source, int command, Object *target)
{
	if (m_internalCommand)
	{
		HordeAIUpdate::commandAccepted(source, command, target);
		return;
	}

	if (command == 0x13 && target && m_activeRepairTarget == target->getID()) // RW 0x89EA13..0x89EA28
	{
		return;
	}

	cancelRepair(); // RW 0x89EA2C -> RW 0x0089E57C

	if (command == 0x13) // RW 0x89EA33
	{
		if (target)
		{
			moveToRepairTarget(target, source); // RW 0x89EA5E -> RW 0x0089E884
		}
		return;
	}
	if (command == 0x42) // RW 0x89EA38
	{
		if (target)
		{
			bfmePrivateCommand42(target, source); // RW 0x89EA51 -> RW 0x0089E990
		}
		return;
	}

	HordeAIUpdate::commandAccepted(source, command, target); // RW 0x89EA40 -> RW 0x89E169
}

// RW 0x0089E5CC / BFME2 0x49AE07
void HordeWorkerAIUpdate::xfer(Xfer &x)
{
	XferVersion v{ 2, 2 };
	x.xferVersion(v);
	HordeAIUpdate::xfer(x);
	x.xferObjectID(&m_pendingRepairTarget);
	x.xferObjectID(&m_activeRepairTarget);
	x.xferObjectID(&m_command42Target);
	x.xferBool(&m_flag);
	if (v.current >= 2)
	{
		x.xferBool(&m_repairMoveFlag);
	}
}

void HordeWorkerAIUpdate::crc(StateHasher &h) const
{
	HordeAIUpdate::crc(h);
	h.addU32(m_pendingRepairTarget);
	h.addU32(m_activeRepairTarget);
	h.addU32(m_command42Target);
	h.addBool(m_flag);
	h.addBool(m_repairMoveFlag);
}

void HordeWorkerAIUpdate::registerClass(ModuleFactory &modules)
{
	modules.bindModuleProc("HordeWorkerAIUpdate", MODULETYPE_BEHAVIOR, [](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &info) -> std::unique_ptr<Module> {
		if (!thing->asObject() || !thing->asObject()->logic().aiWorld())
		{
			return makeUnportedModule(thing, data, info.name, info.type, info.interfaceMask);
		}
		return std::make_unique<HordeWorkerAIUpdate>(thing, data);
	});
}
