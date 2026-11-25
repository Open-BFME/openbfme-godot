// OpenBFME. GPL-3.0.
//
// Both editions: RW 0x008A3950, BFME2 0x0089F9F0. See StrafeAreaUpdate.h.

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "GameLogic/Module/StrafeAreaUpdate.h"

#include "Common/Thing/ModuleFactory.h"
#include "GameLogic/AI/AICommandSink.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/Damage.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/WeaponStores.h"

#include "GameLogic/SimMath.h"
#include <cstddef>
#include <memory>

namespace
{
#define STRAFE_ROW(name, parse, field) { name, parse, nullptr, (int)offsetof(StrafeAreaUpdateModuleData, field) }
const FieldParse kStrafeAreaUpdateFields[] = {
	STRAFE_ROW("WeaponName", INI::parseAsciiString, m_weaponName),
	STRAFE_ROW("StrafeAreaRadius", INI::parseReal, m_strafeAreaRadius),
	STRAFE_ROW("Sweepfrequency", INI::parseReal, m_sweepFrequency), // retail's spelling (RW table 0xC31968)
	STRAFE_ROW("SweepAmplitude", INI::parseReal, m_sweepAmplitude),
	STRAFE_ROW("Slope", INI::parseReal, m_slope),
	STRAFE_ROW("InitialSweepPhase", INI::parseReal, m_initialSweepPhase),
	{ nullptr, nullptr, nullptr, 0 }
};
#undef STRAFE_ROW

constexpr int kFiringConditionBit = 44; // FIRING_A in ModelConditionNames.inc (retail bit 37 / condition 37)
} // namespace

namespace StrafeAreaUpdateTables
{
const FieldParse *strafe()
{
	return kStrafeAreaUpdateFields;
}
} // namespace StrafeAreaUpdateTables

// RW 0x7A81E8 (StrafeAreaUpdateModuleData::StrafeAreaUpdateModuleData), RW 0x7A825D (~StrafeAreaUpdateModuleData)
StrafeAreaUpdateModuleData::StrafeAreaUpdateModuleData()
	: m_weaponName("")
	, m_strafeAreaRadius(150.0f)
	, m_sweepFrequency(0.4f)
	, m_sweepAmplitude(100.0f)
	, m_slope(50.0f)
	, m_initialSweepPhase(0.0f)
{
}

// RW 0x7A80A0
void StrafeAreaUpdateModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kStrafeAreaUpdateFields);
}

StrafeAreaUpdate::StrafeAreaUpdate(Thing *thing, const ModuleData *moduleData)
	: UpdateModule(thing, moduleData), m_data(dynamic_cast<const StrafeAreaUpdateModuleData *>(moduleData))
{
	m_strafeTarget = Coord3D{};
	m_strafeStart = Coord3D{};
	m_sweepPhase = m_data ? m_data->m_initialSweepPhase : 0.0f;
	m_state = 0;
	m_started = false;
	m_nextFireFrame = 0;
}

const char *StrafeAreaUpdate::stopLine()
{
	return "[S-6026] StrafeAreaUpdate (RW 0x8A3950 / BFME2 0x89F9F0): strafe run AI logic, ground sweep and temp weapon dispatch; aircraft 3D spline trajectory under S-140";
}

bool StrafeAreaUpdate::initiateStrafe(const Coord3D *from, const Coord3D *to)
{
	Object *obj = getObject();
	if (!obj || !from || !to)
	{
		return false;
	}
	AIUpdateInterface *ai = obj->getAIUpdateInterface();
	if (!ai)
	{
		return false;
	}

	m_strafeStart = *from;
	m_strafeTarget = *to;
	m_sweepPhase = m_data ? m_data->m_initialSweepPhase : 0.0f;
	m_state = 0;
	m_started = false;

	Coord3D delta{ SimMath::subf32(m_strafeTarget.x, m_strafeStart.x), SimMath::subf32(m_strafeTarget.y, m_strafeStart.y), SimMath::subf32(m_strafeTarget.z, m_strafeStart.z) };
	float angle = SimMath::fstpDword(SimMath::atan2d(delta.y, delta.x));
	obj->setOrientation(angle);

	ai->aiMoveToPosition(m_strafeTarget, CMD_FROM_AI);
	return true;
}

// RW 0x007A8368 (UpdateInterface slot 0): sleep forever without AI (0x7A8379 .. 0x7A8397), approach (state 0, 0x7A86A6),
// sweep/fire + exit on ai->isIdle() (state 1, 0x7A8422 .. 0x7A86A4), kill on distance > radius && ai->isIdle() && !isEffectivelyDead() (state 2, 0x7A83E3 .. 0x7A841D).
UpdateSleepTime StrafeAreaUpdate::update()
{
	if (!m_data)
	{
		return UPDATE_SLEEP_FOREVER; // RW 0x007A8392
	}
	Object *obj = getObject();
	if (!obj)
	{
		return UPDATE_SLEEP_FOREVER; // RW 0x007A8392
	}
	AIUpdateInterface *ai = obj->getAIUpdateInterface();
	if (!ai)
	{
		return UPDATE_SLEEP_FOREVER; // RW 0x007A8379 .. 0x007A8397
	}

	const Coord3D *posPtr = obj->getPosition();
	const Coord3D myPos = posPtr ? *posPtr : Coord3D{};

	Coord3D delta{ SimMath::subf32(m_strafeTarget.x, myPos.x), SimMath::subf32(m_strafeTarget.y, myPos.y), 0.0f };
	float distance = SimMath::length2d(delta.x, delta.y);

	switch (m_state)
	{
	case 0:
		ai->chooseLocomotorSet(LOCOMOTORSET_PANIC); // or 6: panic / fast flight
		if (distance <= SimMath::addf32(m_data->m_strafeAreaRadius, SimMath::mulf32(m_data->m_slope, 2.0f)))
		{
			obj->setModelConditionState(kFiringConditionBit, true);
		}
		if (distance <= SimMath::addf32(m_data->m_strafeAreaRadius, m_data->m_slope))
		{
			ai->chooseLocomotorSet(LOCOMOTORSET_NORMAL);
			m_state = 1;
			m_nextFireFrame = obj->logic().getFrame() + 12;
		}
		break;

	case 1:
	{
		if (!m_data->m_weaponName.empty() && TheWeaponStore)
		{
			const WeaponTemplate *weapon = TheWeaponStore->findWeaponTemplate(m_data->m_weaponName);
			if (weapon && obj->logic().getFrame() >= m_nextFireFrame)
			{
				Coord3D firingPos = myPos;
				float curAngle = obj->getOrientation();
				Coord3D dir{ SimMath::cosf32(curAngle), SimMath::sinf32(curAngle), 0.0f };
				Coord3D perp{ dir.y, SimMath::subf32(0.0f, dir.x), 0.0f };

				m_sweepPhase = SimMath::addf32(m_sweepPhase, m_data->m_sweepFrequency);
				float offset = SimMath::mulf32(SimMath::sinf32(m_sweepPhase), m_data->m_sweepAmplitude);

				perp.x = SimMath::mulf32(perp.x, offset);
				perp.y = SimMath::mulf32(perp.y, offset);

				dir.x = SimMath::mulf32(dir.x, m_data->m_slope);
				dir.y = SimMath::mulf32(dir.y, m_data->m_slope);

				firingPos.x = SimMath::addf32(firingPos.x, SimMath::addf32(dir.x, perp.x));
				firingPos.y = SimMath::addf32(firingPos.y, SimMath::addf32(dir.y, perp.y));
				firingPos.z = obj->logic().getGroundHeight(firingPos.x, firingPos.y);

				ObjectWeapons::createAndFireTempWeapon(weapon, obj, firingPos);
			}
		}

		obj->setModelConditionState(kFiringConditionBit, true);

		if (m_started && distance > SimMath::subf32(m_data->m_strafeAreaRadius, m_data->m_slope))
		{
			m_state = 2;
			ai->chooseLocomotorSet(LOCOMOTORSET_PANIC);
			obj->setModelConditionState(kFiringConditionBit, false);
			break;
		}

		if (distance < SimMath::divf32(m_data->m_strafeAreaRadius, 12.0f) && ai->isIdle()) // RW 0x007A85D2 .. 0x007A85F7 (vcall +0x1B8)
		{
			Coord3D onward{ SimMath::subf32(m_strafeTarget.x, m_strafeStart.x), SimMath::subf32(m_strafeTarget.y, m_strafeStart.y), 0.0f };
			float onLen = SimMath::length2d(onward.x, onward.y);
			if (onLen > 0.0001f)
			{
				onward.x = SimMath::divf32(onward.x, onLen);
				onward.y = SimMath::divf32(onward.y, onLen);
			}
			onward.x = SimMath::addf32(SimMath::mulf32(onward.x, 500.0f), m_strafeTarget.x);
			onward.y = SimMath::addf32(SimMath::mulf32(onward.y, 500.0f), m_strafeTarget.y);
			onward.z = obj->logic().getGroundHeight(onward.x, onward.y);

			ai->aiMoveToPosition(onward, CMD_FROM_AI);
			m_started = true;
		}
		break;
	}

	case 2:
		if (distance > m_data->m_strafeAreaRadius && ai->isIdle() && !obj->isEffectivelyDead()) // RW 0x007A83E3 .. 0x007A841D (vcall +0x1B8, +0x458 bit 0)
		{
			obj->kill(DAMAGE_UNRESISTABLE, DEATH_NORMAL);
		}
		break;
	}

	return UPDATE_SLEEP_NONE;
}

// RW 0x007A814E (slot 3): versions 1..4, UpdateModule::xfer (RW 0x00850C60), m_strafeTarget (+0x20), m_strafeStart (+0x2C), m_sweepPhase (+0x38), m_state (+0x3C), m_started (+0x40), m_nextFireFrame (+0x44).
void StrafeAreaUpdate::xfer(Xfer &x)
{
	XferVersion v{ 1, 4 };
	x.xferVersion(v);
	xferUpdateModule(x);

	if (v.current >= 2)
	{
		SnapshotXfer::coord3D(x, m_strafeTarget);
		SnapshotXfer::coord3D(x, m_strafeStart);
		x.xferReal(&m_sweepPhase);
		SnapshotXfer::enumIn(x, m_state, 0, 2, "StrafeAreaUpdate m_state"); // +0x3C: 0 approach, 1 strafe / fire, 2 exit / kill
	}
	if (v.current >= 3)
	{
		x.xferBool(&m_started);
	}
	if (v.current >= 4)
	{
		x.xferUnsignedInt(&m_nextFireFrame);
	}
}

void StrafeAreaUpdate::crc(StateHasher &hasher) const
{
	UpdateModule::crc(hasher);
	hasher.addFloat(m_strafeTarget.x);
	hasher.addFloat(m_strafeTarget.y);
	hasher.addFloat(m_strafeTarget.z);
	hasher.addFloat(m_strafeStart.x);
	hasher.addFloat(m_strafeStart.y);
	hasher.addFloat(m_strafeStart.z);
	hasher.addFloat(m_sweepPhase);
	hasher.addI32(m_state);
	hasher.addBool(m_started);
	hasher.addU32(m_nextFireFrame);
}

void StrafeAreaUpdate::registerClass(ModuleFactory &modules)
{
	modules.bindTypedData<StrafeAreaUpdateModuleData>("StrafeAreaUpdate", MODULETYPE_BEHAVIOR);
	modules.bindModuleProc("StrafeAreaUpdate", MODULETYPE_BEHAVIOR, [](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
		const auto *typed = dynamic_cast<const StrafeAreaUpdateModuleData *>(data);
		return std::make_unique<StrafeAreaUpdate>(thing, typed);
	});
}
