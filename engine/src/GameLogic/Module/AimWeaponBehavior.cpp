// OpenBFME. GPL-3.0.
// See GameLogic/Module/AimWeaponBehavior.h for the target facts and what is inference.

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "GameLogic/Module/AimWeaponBehavior.h"

#include "Common/StateHash.h"
#include "Common/Thing/ModuleFactory.h"
#include "Common/Xfer.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/SnapshotXfer.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/WeaponState.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>

namespace
{
typedef AimWeaponBehaviorModuleData Data;

// TheModelConditionNames (RW 0x85E41E .. 0x85E434: word 5 bits 18 / 20 / 19; RW 0x85E464 / 0x85E37A)
const int kMcAimHigh = 0xB2;
const int kMcAimStraight = 0xB3;
const int kMcAimLow = 0xB4;
const int kMcAimNear = 0xB5;
const int kMcAimFar = 0xB6;

const float kLeastDistance = 10.0f; // RW 0xBD83D8

// RW 0xC58178
const FieldParse kFields[] = {
	{ "AimLowThreshold", INI::parseReal, nullptr, (int)offsetof(Data, m_aimLowThreshold) },
	{ "AimHighThreshold", INI::parseReal, nullptr, (int)offsetof(Data, m_aimHighThreshold) },
	{ "AimNearDistance", INI::parseReal, nullptr, (int)offsetof(Data, m_aimNearDistance) },
	{ "AimFarDistance", INI::parseReal, nullptr, (int)offsetof(Data, m_aimFarDistance) },
	{ nullptr, nullptr, nullptr, 0 }
};

void addBit(Object::ModelConditionBits &bits, int bit)
{
	bits[(size_t)bit >> 5] |= 1u << (bit & 31);
}

// RW 0x68D607 with one bit each
void clearAndSet(Object &obj, int clearBit, int setBit)
{
	Object::ModelConditionBits clear{};
	Object::ModelConditionBits set{};
	addBit(clear, clearBit);
	addBit(set, setBit);
	obj.clearAndSetModelConditionFlags(clear, set);
}
} // namespace

AimWeaponBehaviorModuleData::AimWeaponBehaviorModuleData()
	: m_aimFarDistance((std::numeric_limits<float>::max)()) // RW 0xBD1910
{
}

void AimWeaponBehaviorModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kFields);
}

void AimWeaponBehavior::registerModule(ModuleFactory &modules)
{
	static const char kName[] = "AimWeaponBehavior";
	modules.bindTypedData<Data>(kName, MODULETYPE_BEHAVIOR);
	modules.bindModuleProc(kName, MODULETYPE_BEHAVIOR, [](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
		const Data *typed = dynamic_cast<const Data *>(data);
		if (!typed)
		{
			throw std::logic_error(std::string(kName) + ": the module data is not typed");
		}
		return std::make_unique<AimWeaponBehavior>(thing, typed);
	});
}

// RW 0x85E29A
AimWeaponBehavior::AimWeaponBehavior(Thing *thing, const Data *data)
	: UpdateModule(thing, data)
	, m_data(data)
{
	setWakeFrame(getObject(), UPDATE_SLEEP(1)); // RW 0x85E2D9
}

// RW 0x85E30B
UpdateSleepTime AimWeaponBehavior::update()
{
	Object *obj = getObject();
	const AIUpdateInterface *ai = obj->getAIUpdateInterface();
	const ObjectWeapons *weapons = obj->getWeapons();
	const Weapon *weapon = weapons ? weapons->currentWeapon() : nullptr; // RW 0x68B58C(0)
	if (!ai || !weapon)
	{
		return UPDATE_SLEEP_FOREVER;
	}
	bool changed = false;
	const UnsignedInt frame = obj->logic().getFrame();
	const bool attacking = ai->isStateActive()                                                  // AI vslot 0x1BC
		|| (weapon->whenPreAttackFinished() <= frame && frame < weapon->followThruEnd()); // RW 0x6CA2C3
	if (weapon->getTemplate()->m_isAimingWeapon && attacking)                             // template + 0x156
	{
		const Coord3D &target = weapons->drawTargetPosition(); // Object + 0x3A8
		const Coord3D &pos = *obj->getPosition();
		const float dy = SimMath::sseSub(target.y, pos.y);
		const float dx = SimMath::sseSub(target.x, pos.x);
		const float dz = SimMath::sseSub(target.z, pos.z);
		// RW 0x85E3B7 .. 0x85E3E1, x87 at PC24: the squares and their sum stay in the registers (the fstp qword of a PC24 value is exact), the CRT sqrt (RW
		// 0xA3CF96), fstp dword
		const double squares = SimMath::pc24AddW(SimMath::pc24MulW((double)dy, (double)dy), SimMath::pc24MulW((double)dx, (double)dx));
		const float length = SimMath::fstpDword(NumericState::sqrtPC24(squares));
		const float distance = kLeastDistance > length ? kLeastDistance : length; // RW 0x85E3E8 .. 0x85E400
		const float slope = SimMath::sseDiv(dz, distance);
		const int bit = slope > m_data->m_aimHighThreshold ? kMcAimHigh : m_data->m_aimLowThreshold > slope ? kMcAimLow : kMcAimStraight;
		m_aiming = true;
		if (!obj->testModelCondition(bit))
		{
			obj->setModelConditionState(bit, true); // RW 0x85E445, RW 0x68B53C: the other two stay
		}
		if (m_data->m_aimNearDistance > distance)
		{
			clearAndSet(*obj, kMcAimFar, kMcAimNear); // RW 0x85E464 .. 0x85E4AC
		}
		else if (distance > m_data->m_aimFarDistance)
		{
			clearAndSet(*obj, kMcAimNear, kMcAimFar); // RW 0x85E487 .. 0x85E4AC
		}
		changed = true;
	}
	if (m_aiming && !attacking)
	{
		m_aiming = false;
		Object::ModelConditionBits clear{};
		for (const int bit : { kMcAimNear, kMcAimFar, kMcAimLow, kMcAimStraight, kMcAimHigh }) // RW 0x85E4C1 .. 0x85E4DF
		{
			addBit(clear, bit);
		}
		obj->clearAndSetModelConditionFlags(clear, Object::ModelConditionBits{}); // RW 0x5E3B79
		changed = true;
	}
	if (changed)
	{
		obj->flushDrawableModelConditions(); // RW 0x67449C(0)
	}
	return UPDATE_SLEEP_NONE;
}

void AimWeaponBehavior::crc(StateHasher &h) const
{
	UpdateModule::crc(h);
	h.addBool(m_aiming);
	// the input of update that no other logic reads (Object + 0x3A8, kept in ObjectWeapons and hashed here only)
	if (const ObjectWeapons *weapons = getObject()->getWeapons())
	{
		const Coord3D &target = weapons->drawTargetPosition();
		h.addFloat(target.x);
		h.addFloat(target.y);
		h.addFloat(target.z);
	}
}

// RW 0x85E1C7's field (OpenBFME native snapshot, S-4190). Version 2 is the port's block. Version 1 is the block an OpenBFME build wrote while the class was an unported shell (UnportedUpdateModule::xfer: the UpdateModule block, asleep for good, and the shell's call count, a report counter that is dropped): such a module never
// aimed, so it loads with the flag clear and keeps the shell's sleep (the save's scheduler block holds it in the sleeping vector)
void AimWeaponBehavior::xfer(Xfer &x)
{
	XferVersion v{ 1, 2 };
	x.xferVersion(v);
	xferUpdateModule(x); // RW 0x850C60
	if (v.current < 2)
	{
		std::uint32_t shellCalls = 0;
		x.xferUnsignedInt(&shellCalls);
		m_aiming = false;
		return;
	}
	x.xferBool(&m_aiming);
}
