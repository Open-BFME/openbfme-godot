// OpenBFME. GPL-3.0.
// See GameLogic/Module/GiantBirdSlowDeathBehavior.h for the target facts and the addresses.

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "GameLogic/Module/GiantBirdSlowDeathBehavior.h"

#include "Common/StateHash.h"
#include "Common/Thing/ModuleFactory.h"
#include "GameLogic/Combat/CombatQueries.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/GiantBirdAIUpdate.h"
#include "GameLogic/Module/PhysicsBehavior.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/PartitionManager.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/SnapshotXfer.h"

#include <cstddef>
#include <memory>
#include <stdexcept>
#include <vector>

namespace
{
typedef GiantBirdSlowDeathBehaviorModuleData Data;

const int kConditionMoving = 0x3D; // RW 0x86516D
const float kPi = 3.14159274f;     // RW 0xBDD388
const float kTwoPi = 6.28318548f;  // RW 0xBDD38C
const float kMinRadius = 10.0f;    // RW 0xBD83D8

const char kStop[] =
	"[S-5606] GiantBirdSlowDeathBehavior (RW 0x86511C, 0x865009, 0x66A61A) crashes the flyer clear of CrashAvoidKindOfs; FXHitGround, OCLHitGround and "
	"DelayFromGroundToFinalDeath are read by none of these functions (CrashAvoidStrength and NeedToMaintainFlailingHeight: RotWK reads neither, PORT-304)";

void parseName(INI *ini, void *, void *store, const void *)
{
	*static_cast<std::string *>(store) = ini->getNextToken(); // RW 0x73A302 / 0x73A368: the stores are the client's and the OCL lane's
}

// RW 0xC59430, in the binary's row order
const FieldParse kFields[] = {
	{ "FXHitGround", parseName, nullptr, (int)offsetof(Data, m_fxHitGround) },
	{ "OCLHitGround", parseName, nullptr, (int)offsetof(Data, m_oclHitGround) },
	{ "DelayFromGroundToFinalDeath", INI::parseDurationReal, nullptr, (int)offsetof(Data, m_delayFromGroundToFinalDeath) },
	{ "CrashAvoidKindOfs", ParseKindOfMask, nullptr, (int)offsetof(Data, m_crashAvoidKindOfs) },
	{ "CrashAvoidRadius", INI::parseReal, nullptr, (int)offsetof(Data, m_crashAvoidRadius) },
	{ "CrashAvoidStrength", INI::parseReal, nullptr, (int)offsetof(Data, m_crashAvoidStrength) },
	{ "NeedToMaintainFlailingHeight", INI::parseBool, nullptr, (int)offsetof(Data, m_needToMaintainFlailingHeight) },
	{ nullptr, nullptr, nullptr, 0 }
};

// RW 0x70C4FE with an empty clear mask
bool hasEveryKind(const Object &o, const KindOfMaskType &kinds)
{
	const KindOfMaskType &k = o.getKindOf();
	for (size_t i = 0; i < k.size(); ++i)
	{
		if ((k[i] & kinds[i]) != kinds[i])
		{
			return false;
		}
	}
	return true;
}
} // namespace

// RW 0x864F34 (GiantBirdSlowDeathBehavior::buildFieldParse)
void GiantBirdSlowDeathBehaviorModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	SlowDeathBehaviorModuleData::buildFieldParse(p); // RW 0xC58A98, 0xC76BD8
	p.add(kFields);                                  // RW 0xC59430
}

const char *GiantBirdSlowDeathBehavior::stopLine()
{
	return kStop;
}

GiantBirdSlowDeathBehavior::GiantBirdSlowDeathBehavior(Thing *thing, const Data *data)
	: SlowDeathBehavior(thing, data)
	, m_bird(data)
{
}

// RW 0x66A61A
bool GiantBirdSlowDeathBehavior::findClearPoint(const Object &obj, float radius, const Coord3D &in, Coord3D &out, const KindOfMaskType &kinds)
{
	const float own = CombatQueries::boundingCircleRadius(obj);
	const float r = kMinRadius > own ? kMinRadius : own; // RW 0x66A645 .. 0x66A64E
	PartitionFilterFn filter([&](Object &o) { return hasEveryKind(o, kinds); });
	const PartitionHits hits = obj.logic().partition().iterateObjectsInRange(in, radius, FROM_CENTER_2D, { &filter }, ITER_SORTED_NEAR_TO_FAR);
	if (!(radius >= r))
	{
		return false; // RW 0x66A6A6
	}
	const float inverse = SimMath::divf32(1.0f, r); // RW 0x66A6AC
	for (float ring = r; radius >= ring; ring = SimMath::addf32(ring, r))
	{
		const float step = SimMath::divf32(kTwoPi, SimMath::mulf32(SimMath::mulf32(ring, kTwoPi), inverse));
		for (float angle = -kPi; kPi >= angle; angle = SimMath::addf32(angle, step))
		{
			const Coord3D p{ SimMath::addf32(SimMath::mulf32(SimMath::sinf32(angle), ring), in.x), SimMath::addf32(SimMath::mulf32(SimMath::cosf32(angle), ring), in.y), in.z };
			bool clear = true;
			for (const PartitionHit &hit : hits)
			{
				const float rr = SimMath::addf32(CombatQueries::boundingCircleRadius(*hit.object), r);
				const float dy = SimMath::subf32(hit.object->getPosition()->y, p.y);
				const float dx = SimMath::subf32(hit.object->getPosition()->x, p.x);
				if (SimMath::mulf32(rr, rr) > SimMath::addf32(SimMath::mulf32(dy, dy), SimMath::mulf32(dx, dx)))
				{
					clear = false; // RW 0x66A76E
					break;
				}
			}
			if (clear)
			{
				out = p;
				return true;
			}
		}
	}
	return false;
}

// RW 0x86511C
void GiantBirdSlowDeathBehavior::beginSlowDeath(const DieModuleInterface::Event &event)
{
	SlowDeathBehavior::beginSlowDeath(event); // RW 0x860E93
	Object *obj = getObject();
	GameLogic &logic = obj->logic();
	Coord3D target = *obj->getPosition();
	PhysicsBehavior *physics = obj->physicsModule();
	if (AIUpdateInterface *ai = obj->getAIUpdateInterface())
	{
		if (obj->testModelCondition(kConditionMoving))
		{
			GiantBirdAIUpdate *bird = dynamic_cast<GiantBirdAIUpdate *>(ai); // slot 0x188: null but for a giant bird
			if (!bird)
			{
				return; // RW 0x865186
			}
			const float speed = SimMath::mulf32(bird->flight().speed, 5.0f); // RW 0xBDAE58
			if (speed > 50.0f)                                         // RW 0xBD88C4
			{
				const float angle = obj->getOrientation();
				target.x = SimMath::addf32(SimMath::mulf32(SimMath::cosf32(angle), speed), target.x);
				target.y = SimMath::addf32(SimMath::mulf32(SimMath::sinf32(angle), speed), target.y);
				if (physics)
				{
					physics->setAllowBouncing(true); // RW 0x8651DF: + 0x5D
				}
			}
		}
		findClearPoint(*obj, m_bird->m_crashAvoidRadius, target, target, m_bird->m_crashAvoidKindOfs); // RW 0x86520B
	}
	target.z = logic.getGroundHeight(target.x, target.y);
	m_crashTarget = target;
	if (physics) // RW reads + 0x264 unchecked
	{
		physics->flyTo(target, 0.0f, 30.0f); // RW 0x865253 (RW 0xBDAE54)
	}
	logic.noteStop(kStop);
}

// RW 0x865009
UpdateSleepTime GiantBirdSlowDeathBehavior::update()
{
	const PhysicsBehavior *physics = getObject()->physicsModule();
	if (physics && physics->hasBounced())
	{
		setHitGroundPending(); // RW 0x865018
	}
	if (isSlowDeathActivated())
	{
		SlowDeathBehavior::update(); // RW 0x865022
	}
	return UPDATE_SLEEP_NONE;
}

void GiantBirdSlowDeathBehavior::crc(StateHasher &h) const
{
	SlowDeathBehavior::crc(h);
	h.addFloat(m_crashTarget.x);
	h.addFloat(m_crashTarget.y);
	h.addFloat(m_crashTarget.z);
}

// RW 0x0086502B
void GiantBirdSlowDeathBehavior::xfer(Xfer &x)
{
	SnapshotXfer::version(x, 1, 1);
	SlowDeathBehavior::xfer(x);
	SnapshotXfer::coord3D(x, m_crashTarget);
}

void GiantBirdSlowDeathBehavior::registerClass(ModuleFactory &modules)
{
	modules.bindTypedData<Data>("GiantBirdSlowDeathBehavior", MODULETYPE_BEHAVIOR);
	modules.bindModuleProc("GiantBirdSlowDeathBehavior", MODULETYPE_BEHAVIOR, [](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
		const Data *typed = dynamic_cast<const Data *>(data);
		if (!typed)
		{
			throw std::logic_error("GiantBirdSlowDeathBehavior: the module data is not typed");
		}
		return std::make_unique<GiantBirdSlowDeathBehavior>(thing, typed);
	});
}
