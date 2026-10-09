// OpenBFME. GPL-3.0.
// See GameLogic/Module/ExtraUpdateModules.h for the target facts and the addresses.

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "GameLogic/Module/ExtraUpdateModules.h"

#include "Common/StateHash.h"
#include "GameLogic/BitFlags.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/Object.h"

#include <cstddef>

namespace
{
const FieldParse kDeletionUpdate[] = {
	{ "MinLifetime", INI::parseDurationUnsignedInt, nullptr, (int)offsetof(DeletionUpdateModuleData, m_minLifetime) },
	{ "MaxLifetime", INI::parseDurationUnsignedInt, nullptr, (int)offsetof(DeletionUpdateModuleData, m_maxLifetime) },
	{ nullptr, nullptr, nullptr, 0 }
};

// RW 0x4B8C21 / RW 0x6C9951: bit flag lists over the binary's name tables
void parseModelConditionWords(INI *ini, void *, void *store, const void *)
{
	ParseBitFlags(ini, static_cast<std::array<std::uint32_t, 19> *>(store)->data(), 19, TheModelConditionNames);
}
void parseWeaponSetWords(INI *ini, void *, void *store, const void *)
{
	ParseBitFlags(ini, static_cast<std::array<std::uint32_t, 4> *>(store)->data(), 4, TheWeaponConditionNames);
}
const FieldParse kMonitorConditionUpdate[] = {
	{ "ModelConditionFlags", parseModelConditionWords, nullptr, (int)offsetof(MonitorConditionUpdateModuleData, m_modelConditionFlags) },
	{ "ModelConditionCommandSet", INI::parseAsciiString, nullptr, (int)offsetof(MonitorConditionUpdateModuleData, m_modelConditionCommandSet) },
	{ "WeaponSetFlags", parseWeaponSetWords, nullptr, (int)offsetof(MonitorConditionUpdateModuleData, m_weaponSetFlags) },
	{ "WeaponToggleCommandSet", INI::parseAsciiString, nullptr, (int)offsetof(MonitorConditionUpdateModuleData, m_weaponToggleCommandSet) },
	{ nullptr, nullptr, nullptr, 0 }
};

template <size_t N>
bool anyCommon(const std::array<std::uint32_t, N> &a, const std::array<std::uint32_t, N> &b) // RW 0x6632E9 / 0x75CDC4
{
	for (size_t i = 0; i < N; ++i)
	{
		if (a[i] & b[i])
		{
			return true;
		}
	}
	return false;
}
} // namespace

void MonitorConditionUpdateModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kMonitorConditionUpdate); // RW 0xC64170
}

void DeletionUpdateModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kDeletionUpdate); // RW 0xC07AD0
}

// ---- DeletionUpdate -----------------------------------------------------------------------------------------------------------------
// RW 0x88B7B0 (calcSleepDelay RW 0x88B737)
DeletionUpdate::DeletionUpdate(Thing *thing, const DeletionUpdateModuleData *data)
	: UpdateModule(thing, data)
	, m_data(data)
{
	setWakeFrame(getObject(), UPDATE_SLEEP((int)calcSleepDelay(data->m_minLifetime, data->m_maxLifetime)));
}

unsigned DeletionUpdate::calcSleepDelay(unsigned minFrames, unsigned maxFrames)
{
	GameLogic &logic = getObject()->logic();
	int delay = logic.random().getValue((int)minFrames, (int)maxFrames, "DeletionUpdate.cpp", 0x37);
	if ((unsigned)delay < 1u)
	{
		delay = 1; // RW 0x88B751: `cmp eax, 1; jae`
	}
	m_dieFrame = logic.getFrame() + (unsigned)delay;
	return (unsigned)delay;
}

// RW 0x88B830 (lane SPELL-2)
void DeletionUpdate::setLifetimeRange(unsigned minFrames, unsigned maxFrames)
{
	setWakeFrame(getObject(), UPDATE_SLEEP((int)calcSleepDelay(minFrames, maxFrames)));
}

// RW 0x88B84F
UpdateSleepTime DeletionUpdate::update()
{
	Object *obj = getObject();
	obj->logic().destroyObject(obj); // RW 0x62BBAB
	return UPDATE_SLEEP_FOREVER;
}

void DeletionUpdate::crc(StateHasher &hasher) const
{
	UpdateModule::crc(hasher);
	hasher.addU32(m_dieFrame);
}

// ---- MonitorConditionUpdate ---------------------------------------------------------------------------------------------------------
// RW 0x894DF8 (the two monitored branches)
void MonitorConditionUpdate::swapTo(const std::string &target, const std::string &other)
{
	Object *obj = getObject();
	const std::string current = obj->getCommandSetName(); // RW 0x69156B
	if (current == target)
	{
		return;
	}
	if (current != other)
	{
		m_saved = current;
	}
	obj->setCommandSetOverride(target); // RW 0x693B94
	obj->logic().noteStop("[S-982] MonitorConditionUpdate: the command set override's skirmish AI notification (RW 0x693B94 -> 0x8E3D1E) is not ported");
}

UpdateSleepTime MonitorConditionUpdate::update()
{
	Object *obj = getObject();
	if (anyCommon(obj->getModelConditionBits(), m_data->m_modelConditionFlags))
	{
		swapTo(m_data->m_modelConditionCommandSet, m_data->m_weaponToggleCommandSet);
		return UPDATE_SLEEP_NONE;
	}
	if (anyCommon(obj->getWeaponSetFlags(), m_data->m_weaponSetFlags)) // Object + 0x38C (lane HUD-4: a weaponless horde keeps them too)
	{
		swapTo(m_data->m_weaponToggleCommandSet, m_data->m_modelConditionCommandSet);
		return UPDATE_SLEEP_NONE;
	}
	if (m_saved.empty() || m_saved == obj->getCommandSetName())
	{
		return UPDATE_SLEEP_NONE;
	}
	obj->setCommandSetOverride(m_saved); // RW 0x894EF5
	m_saved.clear();
	return UPDATE_SLEEP_NONE;
}

void MonitorConditionUpdate::crc(StateHasher &hasher) const
{
	UpdateModule::crc(hasher);
	hasher.addString(m_saved);
}
