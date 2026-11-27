// OpenBFME. GPL-3.0.
// See GameLogic/Module/OneRingPenaltyUpdate.h for the target facts and the addresses.

#if defined(__GNUC__) || defined(__clang__)
// Local 169: RW 0x89D20E (SpecialObjectName)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "GameLogic/Module/OneRingPenaltyUpdate.h"

#include "Common/Audio/AudioRequests.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/StateHash.h"
#include "Common/Thing/ModuleFactory.h"
#include "Common/Thing/ThingFactory.h"
#include "GameLogic/Damage.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/EmotionModules.h"
#include "GameLogic/Module/InvisibilityModules.h"
#include "GameLogic/Module/OpenContain.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/SnapshotXfer.h"

#include <cstddef>
#include <memory>
#include <stdexcept>

namespace
{
typedef OneRingPenaltyUpdateModuleData Data;

const unsigned kStealthed = 0x0F;  // RW 0x89D660
const unsigned kCanStealth = 0x12; // RW 0x89D45F / 0x89D3ED
const int kEmotionPenalty = 5;     // RW 0x89D46C
const float kPi = 3.14159274f;     // RW 0xBDD388
const float kHalfPi = 1.57079637f; // RW 0xBD89D0
const char kFile[] = "OneRingPenaltyUpdate.cpp";

const char kStop[] =
	"[S-5604] OneRingPenaltyUpdate (RW 0x89D647, 0x89D2AC, 0x89D4D2, 0x89D41B, 0x89D3E4) calls and moves the Eye and pays the penalty; TimeFrozenFromPenalty +0x1C (row RW 0xC66B68 of table RW 0xC66B18, parser "
	"RW 0x73A429, defaults RW 0x89D1C1) is never read: ctor RW 0x89D21F, vtables RW 0xC669C8/C669B8, update RW 0x89D647 and helpers RW 0x89D0E3..0x89D712 read +0xC,+0x10,+0x14,+0x18 never +0x1C; RW 0x8CA072/RW 0x8CA12A belongs to a deploy module (table RW 0xC747E8 LowerDelay +0x18/RaiseDelay +0x1C, ctor RW 0x8C9B5C) and no other code looks up this module's data to read +0x1C (RotWK does not read TimeFrozenFromPenalty)";

void parseSound(INI *ini, void *instance, void *store, const void *userData)
{
	OpenContainModuleData::parseAudioEvent(ini, instance, store, userData); // RW 0x73B217
}

const FieldParse kFields[] = { // RW 0xC66B18, in the binary's row order (TimeFrozenFromPenalty is row RW 0xC66B68)
	{ "SpecialObjectName", INI::parseAsciiString, nullptr, (int)offsetof(Data, m_specialObjectName) },
	{ "RingTimeBeforeSpawning", INI::parseDurationUnsignedInt, nullptr, (int)offsetof(Data, m_ringTimeBeforeSpawning) },
	{ "TimeSpentRoamingAround", INI::parseDurationUnsignedInt, nullptr, (int)offsetof(Data, m_timeSpentRoamingAround) },
	{ "TimeRingPowerSuppressed", INI::parseDurationUnsignedInt, nullptr, (int)offsetof(Data, m_timeRingPowerSuppressed) },
	{ "StartingDistanceFromMe", INI::parseReal, nullptr, (int)offsetof(Data, m_startingDistanceFromMe) },
	{ "TimeFrozenFromPenalty", INI::parseDurationUnsignedInt, nullptr, (int)offsetof(Data, m_timeFrozenFromPenalty) },
	{ "DiscoveredSound", parseSound, nullptr, (int)offsetof(Data, m_discoveredSound) },
	{ nullptr, nullptr, nullptr, 0 }
};
} // namespace

void OneRingPenaltyUpdateModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kFields);
}

const char *OneRingPenaltyUpdate::stopLine()
{
	return kStop;
}

OneRingPenaltyUpdate::OneRingPenaltyUpdate(Thing *thing, const Data *data)
	: UpdateModule(thing, data)
	, m_data(data)
{
	setWakeFrame(getObject(), UPDATE_SLEEP(1)); // RW 0x89D27A
}

// RW 0x89D3E4
void OneRingPenaltyUpdate::endPenalty()
{
	m_penaltyFrame = 0;
	getObject()->setStatus(kCanStealth, true);
}

// RW 0x89D3F5
void OneRingPenaltyUpdate::killEye()
{
	if (Object *eye = getObject()->logic().findObjectByID(m_eye))
	{
		eye->kill(DAMAGE_UNRESISTABLE, 0);
		m_eye = INVALID_ID;
	}
}

// RW 0x89D41B
void OneRingPenaltyUpdate::payPenalty()
{
	Object *obj = getObject();
	m_ringFrame = 0;
	m_penaltyFrame = obj->logic().getFrame();
	if (StealthUpdate *stealth = StealthUpdate::of(*obj))
	{
		if (stealth->ringWorn())
		{
			stealth->toggleRing(); // RW 0x89D458
			obj->setStatus(kCanStealth, false);
			EmotionTrackerUpdate::requestEmotion(*obj, kEmotionPenalty, nullptr, 1); // RW 0x89D470
			if (!m_data->m_discoveredSound.empty())
			{
				AudioApi::playSoundForObject(m_data->m_discoveredSound, obj->getID()); // RW 0x89D487 .. 0x89D49F
			}
		}
	}
	killEye(); // RW 0x89D4B3
}

// RW 0x89D2AC
void OneRingPenaltyUpdate::spawnEye()
{
	Object *obj = getObject();
	GameLogic &logic = obj->logic();
	const ThingTemplate *tmpl = logic.things().findTemplate(m_data->m_specialObjectName);
	if (!tmpl)
	{
		return; // RW 0x89D2D1
	}
	const Player *neutral = logic.players().getNeutralPlayer();
	Team *team = neutral ? neutral->getDefaultTeam() : obj->getTeam();
	Object *eye = logic.newObject(tmpl, team, ObjectStatusMaskType{});
	if (!eye)
	{
		return; // RW 0x89D320
	}
	m_eye = eye->getID();
	m_angle = logic.random().getValueReal(-kPi, kPi, kFile, 0xDA);
	const Coord3D &o = *obj->getPosition();
	Coord3D p;
	p.x = SimMath::addf32(o.x, SimMath::mulf32(SimMath::cosf32(m_angle), m_data->m_startingDistanceFromMe));
	p.y = SimMath::addf32(o.y, SimMath::mulf32(SimMath::sinf32(m_angle), m_data->m_startingDistanceFromMe));
	p.z = logic.getGroundHeight(p.x, p.y);
	eye->setPosition(&p);
}

// RW 0x89D4D2
void OneRingPenaltyUpdate::moveEye()
{
	Object *obj = getObject();
	GameLogic &logic = obj->logic();
	Object *eye = logic.findObjectByID(m_eye);
	const UnsignedInt start = m_ringFrame + m_data->m_ringTimeBeforeSpawning;
	const UnsignedInt end = start + m_data->m_timeSpentRoamingAround;
	const UnsignedInt frame = logic.getFrame();
	if (frame >= end)
	{
		if (eye)
		{
			eye->setPosition(obj->getPosition()); // RW 0x89D51A (RW calls it unchecked: the eye's id is live while it roams)
		}
		payPenalty();
		return;
	}
	AIUpdateInterface *ai = eye ? eye->getAIUpdateInterface() : nullptr;
	if (!ai || !ai->isIdle()) // RW 0x89D543 (slot 0x1B8)
	{
		return;
	}
	m_angle = SimMath::addf32(logic.random().getValueReal(-kHalfPi, kHalfPi, kFile, 0xB8), m_angle);
	const float d = m_data->m_startingDistanceFromMe;
	float radius = d;
	if (frame != start)
	{
		// RW 0x89D5AB .. 0x89D5E0: x87, float32 store
		const double t = (double)(frame - start) / (double)(end - start);
		radius = (float)(t * ((double)d * 0.5 - (double)d) + (double)d);
	}
	const Coord3D &o = *obj->getPosition();
	Coord3D p;
	p.x = SimMath::addf32(SimMath::mulf32(SimMath::cosf32(m_angle), radius), o.x);
	p.y = SimMath::addf32(SimMath::mulf32(SimMath::sinf32(m_angle), radius), o.y);
	p.z = logic.getGroundHeight(p.x, p.y);
	ai->aiMoveToPosition(p, CMD_FROM_AI); // RW 0x66C4CA
}

// RW 0x89D647
UpdateSleepTime OneRingPenaltyUpdate::update()
{
	Object *obj = getObject();
	bool worn = false;
	if (InvisibilityManager::isInvisible(*obj) || obj->testStatus(kStealthed))
	{
		const StealthUpdate *stealth = StealthUpdate::of(*obj);
		worn = stealth && stealth->ringWorn();
	}
	const UnsignedInt frame = obj->logic().getFrame();
	if (m_penaltyFrame != 0 && frame > m_penaltyFrame + m_data->m_timeRingPowerSuppressed)
	{
		endPenalty();
	}
	else if (m_ringFrame == 0)
	{
		if (worn)
		{
			m_ringFrame = frame;
		}
	}
	else if (!worn)
	{
		m_ringFrame = 0;
		if (m_eye != INVALID_ID)
		{
			killEye();
		}
	}
	else if (m_eye != INVALID_ID)
	{
		moveEye();
	}
	else if (frame > m_ringFrame + m_data->m_ringTimeBeforeSpawning)
	{
		spawnEye();
	}
	return UPDATE_SLEEP(5); // RW 0xD9F608
}

// RW 0x89D4C7
void OneRingPenaltyUpdate::onDie(const DieModuleInterface::Event &)
{
	killEye();
}

void OneRingPenaltyUpdate::crc(StateHasher &h) const
{
	UpdateModule::crc(h);
	h.addU32((std::uint32_t)m_eye);
	h.addU32(m_ringFrame);
	h.addFloat(m_angle);
	h.addU32(m_penaltyFrame);
}

// RW 0x0089D109 (UpdateModule::xfer, v1, m_eye +0x24, m_ringFrame +0x28, m_angle +0x2C, m_penaltyFrame +0x30)
void OneRingPenaltyUpdate::xfer(Xfer &x)
{
	SnapshotXfer::version(x, 1, 1);
	xferUpdateModule(x);
	x.xferObjectID(&m_eye);
	x.xferUnsignedInt(&m_ringFrame);
	x.xferReal(&m_angle);
	x.xferUnsignedInt(&m_penaltyFrame);
}

void OneRingPenaltyUpdate::registerClass(ModuleFactory &modules)
{
	modules.bindTypedData<Data>("OneRingPenaltyUpdate", MODULETYPE_BEHAVIOR);
	modules.bindModuleProc("OneRingPenaltyUpdate", MODULETYPE_BEHAVIOR, [](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
		const Data *typed = dynamic_cast<const Data *>(data);
		if (!typed)
		{
			throw std::logic_error("OneRingPenaltyUpdate: the module data is not typed");
		}
		return std::make_unique<OneRingPenaltyUpdate>(thing, typed);
	});
}
