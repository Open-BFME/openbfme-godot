// OpenBFME. GPL-3.0.
// See GameLogic/Module/EntEnragedUpdate.h for the target facts and the addresses.

#if defined(__GNUC__) || defined(__clang__)
// Local 169: RW 0x8B660A (ScanDelayTime)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "GameLogic/Module/EntEnragedUpdate.h"

#include "Common/Player.h"
#include "Common/StateHash.h"
#include "Common/Thing/ModuleFactory.h"
#include "GameLogic/FXEvents.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/EmotionModules.h"
#include "GameLogic/Module/InvisibilityModules.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/PartitionManager.h"
#include "GameLogic/ObjectFilterMatch.h"
#include "GameLogic/SnapshotXfer.h"

#include <cstddef>
#include <memory>
#include <stdexcept>

namespace
{
typedef EntEnragedUpdateModuleData Data;

const unsigned kStatusAflame = 10;    // RW 0x8B6893
const unsigned kStatusEnraged = 0x36; // RW 0x8B68CC
const int kWeaponSetEnraged = 0x16;   // RW 0x8B68D5
const int kConditionAflame = 0x4E;    // RW 0x8B698F
const int kConditionWeaponSetEnraged = 0xFA; // RW 0x8B69E2
const int kConditionInitialEnraged = 0x13B;  // RW 0x8B6A04
const unsigned kDisabledTransition = 4;      // RW 0x8B6A1C

void parseFilter(INI *ini, void *instance, void *store, const void *)
{
	ObjectFilter f;
	ParseObjectFilter(ini, instance, &f, nullptr); // RW 0x76392F
	*static_cast<ObjectFilter *>(store) = std::move(f);
}

void parseFXName(INI *ini, void *, void *store, const void *)
{
	*static_cast<std::string *>(store) = ini->getNextToken(); // RW 0x73A302: the FX list store is the client's
}

// RW 0xC6D690, in the binary's row order
const FieldParse kFields[] = {
	{ "ScanDelayTime", INI::parseDurationUnsignedInt, nullptr, (int)offsetof(Data, m_scanDelayTime) },
	{ "EnragedTime", INI::parseDurationUnsignedInt, nullptr, (int)offsetof(Data, m_enragedTime) },
	{ "TimeUntilCanRageAgain", INI::parseDurationUnsignedInt, nullptr, (int)offsetof(Data, m_timeUntilCanRageAgain) },
	{ "ScanDistance", INI::parseReal, nullptr, (int)offsetof(Data, m_scanDistance) },
	{ "HatedObjectFilter", parseFilter, nullptr, (int)offsetof(Data, m_hatedObjectFilter) },
	{ "FriendlyDeadFilter", parseFilter, nullptr, (int)offsetof(Data, m_friendlyDeadFilter) },
	{ "EnragedTransitionTime", INI::parseDurationUnsignedInt, nullptr, (int)offsetof(Data, m_enragedTransitionTime) },
	{ "EnragedTransitionFX", parseFXName, nullptr, (int)offsetof(Data, m_enragedTransitionFX) },
	{ "EnragedOnBuffFX", parseFXName, nullptr, (int)offsetof(Data, m_enragedOnBuffFX) },
	{ "EnragedOffBuffFX", parseFXName, nullptr, (int)offsetof(Data, m_enragedOffBuffFX) },
	{ nullptr, nullptr, nullptr, 0 }
};

void playFX(Object &obj, const char *site, const std::string &fx)
{
	if (!fx.empty())
	{
		GameLogic &logic = obj.logic();
		logic.fxEvents().emit(FXEventLog::objectEvent(FXEvent::OBJECT_FX, site, logic.getFrame(), fx, obj)); // RW 0x4B1B5A(fx, obj, 0)
	}
}
} // namespace

EntEnragedUpdateModuleData::EntEnragedUpdateModuleData()
	: m_scanDelayTime(5) // BFME2 EntEnragedUpdateModuleData constructor: LOGICFRAMES_PER_SECOND
{
}

void EntEnragedUpdateModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kFields);
}

EntEnragedUpdate *EntEnragedUpdate::of(const Object &obj)
{
	return dynamic_cast<EntEnragedUpdate *>(obj.findModule("EntEnragedUpdate")); // RW 0x68BDA5
}

EntEnragedUpdate::EntEnragedUpdate(Thing *thing, const Data *data)
	: UpdateModule(thing, data)
	, m_data(data)
{
	m_scanCounter = (UnsignedInt)getObject()->logic().random().getValue(0, (int)data->m_scanDelayTime, "EntEnragedUpdate.cpp", 0x4C); // RW 0x8B6662
}

void EntEnragedUpdate::setForced(bool forced, bool once)
{
	m_forced = forced;
	if (forced)
	{
		m_suppressed = false;
	}
	m_forceOnce = once;
}

void EntEnragedUpdate::setSuppressed(bool on)
{
	m_suppressed = on;
	if (on)
	{
		m_forced = false;
	}
}

// RW 0x8B66CE
bool EntEnragedUpdate::friendDeadNear() const
{
	Object *obj = getObject();
	GameLogic &logic = obj->logic();
	const Player *owner = obj->getControllingPlayer();
	PartitionFilterFn filter([&](Object &o) { return ObjectFilterMatch::allows(logic, m_data->m_friendlyDeadFilter, o, owner); }); // RW 0xBE4CC8 (flag 1)
	PartitionFilterFn dead([](Object &o) { return o.isEffectivelyDead(); });                                                       // RW 0x660E83
	PartitionFilterFn allies([&](Object &o) { return obj->getRelationship(o) == ALLIES; });                                        // RW 0xC11DC0(obj, 4, 0)
	return logic.partition().getClosestObject(*obj->getPosition(), m_data->m_scanDistance, FROM_CENTER_3D, { &filter, &dead, &allies }) != nullptr;
}

// RW 0x8B6772
bool EntEnragedUpdate::hatedNear() const
{
	Object *obj = getObject();
	GameLogic &logic = obj->logic();
	const Player *owner = obj->getControllingPlayer();
	// RW 0x660D2C(obj, 0), allow RW 0x660D71 (AIClosestEnemy's StealthFilter)
	PartitionFilterFn visible([&](Object &o) {
		return EmotionModules::visibleToScans(o) || (owner && owner->getPlayerType() == PLAYER_COMPUTER && InvisibilityManager::isFiring(o));
	});
	PartitionFilterFn filter([&](Object &o) { return ObjectFilterMatch::allows(logic, m_data->m_hatedObjectFilter, o, owner); }); // RW 0xBE4CC8 (flag 1)
	PartitionFilterFn alive([](Object &o) { return !o.isEffectivelyDead(); });                                                     // RW 0x660E71
	return logic.partition().getClosestObject(*obj->getPosition(), m_data->m_scanDistance, FROM_CENTER_3D, { &visible, &filter, &alive }) != nullptr;
}

// RW 0x8B6866
UpdateSleepTime EntEnragedUpdate::update()
{
	Object *obj = getObject();
	if (obj->isEffectivelyDead())
	{
		return UPDATE_SLEEP_FOREVER; // RW 0x8B687B
	}
	GameLogic &logic = obj->logic();
	if (m_enraged && ((m_rageEndFrame <= logic.getFrame() && !obj->testStatus(kStatusAflame)) || m_suppressed))
	{
		if (m_forceOnce)
		{
			m_forced = false;
		}
		if (!m_forced)
		{
			obj->setStatus(kStatusEnraged, false);
			obj->setWeaponSetFlag(kWeaponSetEnraged, false);
			if (AIUpdateInterface *ai = obj->getAIUpdateInterface())
			{
				ai->chooseLocomotorSet(LOCOMOTORSET_NORMAL); // slot 0x238(0)
			}
			m_rageEndFrame = (UnsignedInt)UPDATE_SLEEP_FOREVER;
			m_nextRageFrame = logic.getFrame() + m_data->m_timeUntilCanRageAgain;
			m_enraged = false;
			m_wasAflame = false;
			playFX(*obj, "EntEnragedUpdate EnragedOffBuffFX", m_data->m_enragedOffBuffFX);
		}
	}
	if (m_enraged)
	{
		return UPDATE_SLEEP_NONE; // RW 0x8B6924
	}
	if (m_nextRageFrame >= logic.getFrame() && !m_forced)
	{
		return UPDATE_SLEEP_NONE; // RW 0x8B6936 .. 0x8B693C
	}
	// both scans always run (RW 0x8B694C .. 0x8B695C)
	bool want = friendDeadNear();
	if (hatedNear())
	{
		want = true;
	}
	if (const BodyModuleInterface *body = obj->getBodyModule())
	{
		const float maxHealth = body->getMaxHealth();
		const double fraction = maxHealth > 0.0f ? (double)body->getHealth() / (double)maxHealth : 0.0; // RW 0x8C1D75
		if (0.5 > fraction)
		{
			want = true;
		}
	}
	const bool aflameCondition = obj->testModelCondition(kConditionAflame);
	if (m_wasAflame)
	{
		if (!aflameCondition)
		{
			want = true;
		}
	}
	else if (aflameCondition)
	{
		m_wasAflame = true;
	}
	if (obj->testStatus(kStatusAflame))
	{
		want = false;
	}
	if (m_forced)
	{
		want = true;
	}
	if (m_suppressed)
	{
		want = false;
	}
	if (!want)
	{
		return UPDATE_SLEEP_NONE;
	}
	if (!obj->testModelCondition(kConditionWeaponSetEnraged) && !obj->testStatus(kStatusAflame))
	{
		obj->setSpecialModelConditionState(kConditionInitialEnraged, m_data->m_enragedTransitionTime); // RW 0x68B581
		obj->setDisabled(kDisabledTransition, logic.getFrame() + m_data->m_enragedTransitionTime); // RW 0x6907F1
		playFX(*obj, "EntEnragedUpdate EnragedTransitionFX", m_data->m_enragedTransitionFX);
	}
	obj->setStatus(kStatusEnraged, true);
	obj->setWeaponSetFlag(kWeaponSetEnraged, true);
	if (AIUpdateInterface *ai = obj->getAIUpdateInterface())
	{
		ai->chooseLocomotorSet(LOCOMOTORSET_ENRAGED); // slot 0x238(8)
	}
	m_enraged = true;
	m_rageEndFrame = logic.getFrame() + m_data->m_enragedTime;
	playFX(*obj, "EntEnragedUpdate EnragedOnBuffFX", m_data->m_enragedOnBuffFX);
	return UPDATE_SLEEP_NONE;
}

void EntEnragedUpdate::crc(StateHasher &h) const
{
	UpdateModule::crc(h);
	h.addU32(m_scanCounter);
	h.addU32(m_rageEndFrame);
	h.addU32(m_nextRageFrame);
	h.addBool(m_enraged);
	h.addBool(m_suppressed);
	h.addBool(m_wasAflame);
	h.addBool(m_forced);
	h.addBool(m_forceOnce);
}

// RW 0x8B64CA's fields (OpenBFME native snapshot, S-4190)
void EntEnragedUpdate::xfer(Xfer &x)
{
	SnapshotXfer::version(x, 1, 1);
	xferUpdateModule(x);
	x.xferUnsignedInt(&m_scanCounter);
	x.xferBool(&m_enraged);
	x.xferUnsignedInt(&m_rageEndFrame);
	x.xferBool(&m_forced);
	x.xferBool(&m_suppressed);
	x.xferBool(&m_forceOnce);
	x.xferUnsignedInt(&m_nextRageFrame);
	x.xferBool(&m_wasAflame);
}

void EntEnragedUpdate::registerClass(ModuleFactory &modules)
{
	modules.bindTypedData<Data>("EntEnragedUpdate", MODULETYPE_BEHAVIOR);
	modules.bindModuleProc("EntEnragedUpdate", MODULETYPE_BEHAVIOR, [](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
		const Data *typed = dynamic_cast<const Data *>(data);
		if (!typed)
		{
			throw std::logic_error("EntEnragedUpdate: the module data is not typed");
		}
		return std::make_unique<EntEnragedUpdate>(thing, typed);
	});
}
