// OpenBFME. GPL-3.0.
// See GameLogic/Module/GiantBirdSlowDeathBehavior.h for the target facts and what is inference.

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "GameLogic/Module/GiantBirdSlowDeathBehavior.h"

#include "Common/StateHash.h"
#include "Common/Thing/ModuleFactory.h"
#include "Common/Xfer.h"
#include "GameLogic/Combat/CombatQueries.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/GiantBirdAIUpdate.h"
#include "GameLogic/Module/PhysicsBehavior.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/PartitionManager.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/SnapshotXfer.h"

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace
{
typedef GiantBirdSlowDeathBehaviorModuleData Data;

// stop S-5606 (the entry of #487, Gleb302596555): the fields that no code of the class reads
const char kStop[] =
	"[S-5606] GiantBirdSlowDeathBehavior (RW 0x86511C, 0x865009, 0x66A61A) crashes the flyer clear of CrashAvoidKindOfs; FXHitGround, OCLHitGround, "
	"DelayFromGroundToFinalDeath, CrashAvoidStrength and NeedToMaintainFlailingHeight are parsed and read by none of the class's functions (RotWK 2.01, BFME2 1.06)";

// RW 0x73A302 / 0x73A368: kept by name (nothing in the class reads them)
void parseName(INI *ini, void *, void *store, const void *)
{
	*static_cast<std::string *>(store) = ini->getNextToken();
}

// RW 0xC59430
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

// RW 0x70B8C7 with an empty forbidden mask: every bit of `required` is set
bool hasAllKinds(const Object &o, const KindOfMaskType &required)
{
	const KindOfMaskType &have = o.getKindOf();
	for (size_t i = 0; i < required.size(); ++i)
	{
		if ((required[i] & have[i]) != required[i])
		{
			return false;
		}
	}
	return true;
}
} // namespace

void GiantBirdSlowDeathBehaviorModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	SlowDeathBehaviorModuleData::buildFieldParse(p); // RW 0xC58A98 and the DieMux rows (0xC76BD8)
	p.add(kFields);                                  // RW 0xC59430
}

void GiantBirdSlowDeathBehavior::registerModule(ModuleFactory &modules)
{
	static const char kName[] = "GiantBirdSlowDeathBehavior";
	modules.bindTypedData<Data>(kName, MODULETYPE_BEHAVIOR);
	modules.bindModuleProc(kName, MODULETYPE_BEHAVIOR, [](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
		const Data *typed = dynamic_cast<const Data *>(data);
		if (!typed)
		{
			throw std::logic_error(std::string(kName) + ": the module data is not typed");
		}
		return std::make_unique<GiantBirdSlowDeathBehavior>(thing, typed);
	});
}

GiantBirdSlowDeathBehavior::GiantBirdSlowDeathBehavior(Thing *thing, const Data *data)
	: SlowDeathBehavior(thing, data)
	, m_birdData(data)
{
}

// RW 0x66A61A. TARGET FACTS: r = the object's bounding sphere radius (Object + 0xBC), at least 10.0 (RW 0xBD83D8); the objects with every kind of `kinds` within `radius` of
// `pos` (RW 0xA39340: FROM_CENTER_2D, near to far); the rings ring = r, 2 r, ... while radius >= ring: the angle from -pi in steps of 2 pi / ((ring * 2 pi) * (1 / r)) up to pi;
// the spot x = fsin(angle) * ring + x, y = fcos(angle) * ring + y (RW 0x42F4D0 / 0x42F4E0, x87 PC24, one store each); it is free when for no object (its + 0xBC + r)^2 is
// above dy^2 + dx^2 (SSE). The first free spot is the result
bool GiantBirdSlowDeathBehavior::findClearCrashPlace(const Object &obj, float radius, const Coord3D &pos, const KindOfMaskType &kinds, Coord3D &out)
{
	const float own = CombatQueries::boundingSphereRadius(obj);
	const float r = 10.0f > own ? 10.0f : own;
	PartitionFilterFn kind([&](Object &o) { return hasAllKinds(o, kinds); }); // RW 0x660DB2
	const PartitionHits hits = obj.logic().partition().iterateObjectsInRange(pos, radius, FROM_CENTER_2D, { &kind }, ITER_SORTED_NEAR_TO_FAR);
	const float kPi = 3.1415927410125732f, kTwoPi = 6.2831854820251465f; // RW 0xBDD388 / 0xBDD38C
	const float inverse = SimMath::divf32(1.0f, r);
	for (float ring = r; radius >= ring; ring = SimMath::addf32(ring, r))
	{
		const float step = SimMath::divf32(kTwoPi, SimMath::mulf32(SimMath::mulf32(ring, kTwoPi), inverse));
		for (float angle = -kPi; kPi >= angle; angle = SimMath::addf32(angle, step))
		{
			Coord3D spot = pos;
			spot.x = SimMath::fstpDword(SimMath::pc24AddW(SimMath::pc24MulW(SimMath::sind(angle), (double)ring), (double)spot.x));
			spot.y = SimMath::fstpDword(SimMath::pc24AddW(SimMath::pc24MulW(SimMath::cosd(angle), (double)ring), (double)spot.y));
			bool isFree = true;
			for (const PartitionHit &hit : hits)
			{
				const float reach = SimMath::addf32(CombatQueries::boundingSphereRadius(*hit.object), r);
				const float dy = SimMath::subf32(hit.object->getPosition()->y, spot.y);
				const float dx = SimMath::subf32(hit.object->getPosition()->x, spot.x);
				if (SimMath::mulf32(reach, reach) > SimMath::addf32(SimMath::mulf32(dy, dy), SimMath::mulf32(dx, dx)))
				{
					isFree = false;
					break;
				}
			}
			if (isFree)
			{
				out = spot;
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
	PhysicsBehavior *phys = PhysicsBehavior::find(*obj); // Object + 0x264
	Coord3D place = *obj->getPosition();
	if (AIUpdateInterface *ai = obj->getAIUpdateInterface()) // Object + 0x260
	{
		if (m_movingAtDeath) // RW 0x865171: MOVING (see CombatModules.h)
		{
			const GiantBirdAIUpdate *bird = dynamic_cast<const GiantBirdAIUpdate *>(ai); // AI vslot 0x188
			if (!bird)
			{
				return; // RW 0x865186
			}
			const float ahead = SimMath::mulf32(bird->flight().speed, 5.0f);
			if (ahead > 50.0f)
			{
				const float angle = obj->getOrientation();
				place.x = SimMath::fstpDword(SimMath::pc24AddW(SimMath::pc24MulW(SimMath::cosd(angle), (double)ahead), (double)place.x));
				place.y = SimMath::fstpDword(SimMath::pc24AddW(SimMath::pc24MulW(SimMath::sind(angle), (double)ahead), (double)place.y));
				if (phys)
				{
					phys->setAllowBouncing(true); // RW 0x8651DF
				}
			}
		}
		findClearCrashPlace(*obj, m_birdData->m_crashAvoidRadius, place, m_birdData->m_crashAvoidKindOfs, place); // RW 0x86520B
	}
	place.z = obj->logic().getGroundHeight(place.x, place.y); // RW 0x865229
	m_crashPlace = place;
	if (phys)
	{
		phys->flyTo(place, 0.0f, 30.0f); // RW 0x865253
	}
	obj->logic().noteStop(kStop);
}

// RW 0x865009
UpdateSleepTime GiantBirdSlowDeathBehavior::update()
{
	const PhysicsBehavior *phys = PhysicsBehavior::find(*getObject());
	if (phys && phys->bounced())
	{
		setHitGroundPending();
	}
	if (isSlowDeathActivated())
	{
		(void)SlowDeathBehavior::update(); // RW 0x860B39
	}
	return UPDATE_SLEEP_NONE;
}

void GiantBirdSlowDeathBehavior::crc(StateHasher &h) const
{
	SlowDeathBehavior::crc(h);
	h.addFloat(m_crashPlace.x);
	h.addFloat(m_crashPlace.y);
	h.addFloat(m_crashPlace.z);
}

// RW 0x86502B (OpenBFME native snapshot, S-4190; + 0x50, + 0x60 and + 0x64 never change). Version 2 is the port's block. Version 1 is the block an OpenBFME build wrote while the class was an unported shell (UnportedUpdateModule::xfer: the UpdateModule block, asleep for good, and the shell's call count, a report counter that is dropped): such a
// module never began a death, so it keeps the constructor's state and the shell's sleep
void GiantBirdSlowDeathBehavior::xfer(Xfer &x)
{
	XferVersion v{ 1, 2 };
	x.xferVersion(v);
	if (v.current < 2)
	{
		xferUpdateModule(x);
		std::uint32_t shellCalls = 0;
		x.xferUnsignedInt(&shellCalls);
		return;
	}
	SlowDeathBehavior::xfer(x);
	x.xferReal(&m_crashPlace.x);
	x.xferReal(&m_crashPlace.y);
	x.xferReal(&m_crashPlace.z);
}
