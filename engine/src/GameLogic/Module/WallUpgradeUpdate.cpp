// OpenBFME. GPL-3.0.
// See GameLogic/Module/WallUpgradeUpdate.h (editions: both RotWK 2.01 and BFME2 1.06).

#include "GameLogic/Module/WallUpgradeUpdate.h"
#include "GameLogic/ObjectTemplateInfo.h"

#include "Common/INI.h"
#include "Common/Thing/ModuleFactory.h"
#include "Common/Xfer.h"
#include "GameLogic/Construction.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/SnapshotXfer.h"

#include <memory>
#include <stdexcept>

namespace
{
constexpr int kRubbleCountdownFrames = 40; // 8 * 5 logic fps (RW 0x8AEAE6 / BFME2 0x4AB3EB)
} // namespace

void WallUpgradeUpdateModuleData::buildFieldParse(MultiIniFieldParse &)
{
}

WallUpgradeUpdate::WallUpgradeUpdate(Thing *thing, const WallUpgradeUpdateModuleData *data)
	: UpdateModule(thing, data)
	, m_data(data)
{
	friend_setNextCallFrame((UnsignedInt)UPDATE_SLEEP_NONE);
}

// RW 0x8AEAA9 / BFME2 0x4AB3AE: testModelCondition (RW 0x46E918) for 0x109, 0x108, 0x107
// (UPGRADE_TREBUCHET, UPGRADE_POSTERN_GATE, UPGRADE_GARRISON).
bool WallUpgradeUpdate::isWallUpgradeKind() const
{
	if (!getObject())
	{
		return false;
	}
	static const int kTreb = Construction::modelConditionIndex("UPGRADE_TREBUCHET");       // 0x109
	static const int kPostern = Construction::modelConditionIndex("UPGRADE_POSTERN_GATE"); // 0x108
	static const int kGarrison = Construction::modelConditionIndex("UPGRADE_GARRISON");    // 0x107
	return (kTreb >= 0 && getObject()->testModelCondition(kTreb)) ||
	       (kPostern >= 0 && getObject()->testModelCondition(kPostern)) ||
	       (kGarrison >= 0 && getObject()->testModelCondition(kGarrison));
}

// RW 0x8AEAE6 / BFME2 0x4AB3EB
void WallUpgradeUpdate::onBodyDamageStateChange(const DamageInfo *, BodyDamageType oldState, BodyDamageType newState)
{
	if (newState > oldState && newState == BODY_RUBBLE)
	{
		m_countdown = kRubbleCountdownFrames;
		setWakeFrame(getObject(), UPDATE_SLEEP_NONE); // RW 0x8AEB08 -> RW 0x850C32
	}
}

// RW 0x8AEB10 / BFME2 0x4AB415
void WallUpgradeUpdate::scanForBuildingAndPossess()
{
	if (m_attachedObjectID != 0 || !getObject())
	{
		return;
	}
	GameLogic &logic = getObject()->logic();
	static const int structKind = ObjectTemplateInfoBuilder::kindOfIndex("STRUCTURE");
	static const int wallWalkKind = ObjectTemplateInfoBuilder::kindOfIndex("WALK_ON_TOP_OF_WALL");
	if (structKind < 0 || wallWalkKind < 0)
	{
		return;
	}

	const Coord3D &myPos = *getObject()->getPosition();
	float bestDistSq = SimMath::mulf32(200.0f, 200.0f);
	ObjectID bestId = 0;

	for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		if (o == getObject() || o->isDestroyed())
		{
			continue;
		}
		if (o->isKindOf((unsigned)structKind) && o->isKindOf((unsigned)wallWalkKind))
		{
			const Coord3D &p = *o->getPosition();
			const float dx = SimMath::subf32(p.x, myPos.x);
			const float dy = SimMath::subf32(p.y, myPos.y);
			const float d2 = SimMath::addf32(SimMath::mulf32(dx, dx), SimMath::mulf32(dy, dy));
			if (d2 <= bestDistSq)
			{
				bestDistSq = d2;
				bestId = o->getID();
			}
		}
	}
	m_attachedObjectID = bestId;
}

// RW 0x008AEBA6 / DONOR BFME2 0x4AB4AB: findObjectByID(+0x28), !isEffectivelyDead (+0x458 bit 0),
// !testModelCondition(0x45 = ACTIVELY_BEING_CONSTRUCTED, RW 0x46E918).
bool WallUpgradeUpdate::canResetUpgrades() const
{
	if (m_attachedObjectID == 0 || !getObject())
	{
		return false;
	}
	Object *att = getObject()->logic().findObjectByID(m_attachedObjectID); // RW 0x8AEBAF -> RW 0x449681
	if (!att || att->isEffectivelyDead())                                  // RW 0x8AEBB4 .. 0x8AEBBF: Object + 0x458 bit 0
	{
		return false;
	}
	static const int kActivelyConstructed = Construction::modelConditionIndex("ACTIVELY_BEING_CONSTRUCTED"); // 0x45
	return !(kActivelyConstructed >= 0 && att->testModelCondition(kActivelyConstructed)); // RW 0x8AEBC1 -> RW 0x46E918(0x45)
}

// RW 0x8AEBD3 / BFME2 0x4AB4D8
UpdateSleepTime WallUpgradeUpdate::update()
{
	UpdateSleepTime ret = UPDATE_SLEEP_FOREVER;
	if (m_firstScan)
	{
		m_firstScan = false;
		scanForBuildingAndPossess();
	}
	if (m_countdown > 0)
	{
		if (--m_countdown == 0)
		{
			m_postCountdown = true;
		}
		else
		{
			ret = UPDATE_SLEEP_NONE;
		}
	}
	if (m_postCountdown)
	{
		if (canResetUpgrades())
		{
			if (Object *obj = getObject())
			{
				obj->resetUpgrades(); // RW 0x68E083 / BFME2 0x0028DA28
				if (auto *body = obj->getBodyModule())
				{
					body->resetDamageState(0); // RW 0x8C1BF9 / BFME2 slot 9
				}
				obj->setModelConditionState(62, false); // bit 62: POST_COLLAPSE
				obj->setStatus(0x13, false);
				obj->setStatus(3, false);
				obj->setStatus(0x56, false);
				obj->setStatus(4, false);
			}
			m_postCountdown = false;
		}
		else
		{
			ret = (UpdateSleepTime)5; // BfmeLogicFramesPerSecond
		}
	}
	return ret;
}

// RW 0x008AE96E: version (1, 1), UpdateModule::xfer (0x850C60), m_attachedObjectID (+0x28), m_countdown (+0x2C), m_firstScan (+0x30), m_postCountdown (+0x31)
void WallUpgradeUpdate::xfer(Xfer &x)
{
	SnapshotXfer::version(x, 1, 1);
	xferUpdateModule(x);
	x.xferObjectID(&m_attachedObjectID);
	x.xferInt(&m_countdown);
	x.xferBool(&m_firstScan);
	x.xferBool(&m_postCountdown);
}

// RW 0x008AE968 (getModuleName), RW 0x008AE9CD (getModuleNameKey), RW 0x008AEA8D / RW 0x008AE941 (dtor)
void WallUpgradeUpdate::registerClass(ModuleFactory &modules)
{
	modules.bindTypedData<WallUpgradeUpdateModuleData>("WallUpgradeUpdate", MODULETYPE_BEHAVIOR);
	modules.bindModuleProc("WallUpgradeUpdate", MODULETYPE_BEHAVIOR,
		[](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
			const auto *typed = dynamic_cast<const WallUpgradeUpdateModuleData *>(data);
			if (!typed)
			{
				throw std::logic_error("WallUpgradeUpdate: module data is not typed");
			}
			return std::make_unique<WallUpgradeUpdate>(thing, typed);
		});
}
