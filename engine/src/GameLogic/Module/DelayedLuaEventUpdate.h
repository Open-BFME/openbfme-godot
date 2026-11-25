// OpenBFME. GPL-3.0.
//
// DelayedLuaEventUpdate (fork issue 285, LOCAL-35; both editions: BFME2 1.06 ctor 0x004A8E7B, xfer 0x004A8E0A, ModuleData ctor 0x004A8D06).
// The damage warning (GenericDamageWarning in system.ini) fires one Lua event, once, on every object in a range, then
// destroys itself. The event and the argument list are configured by the caller (the damage warning system, RW 0x90DC46)
// through configure(); the dispatch goes through GameLogic::dispatchScriptEvent (RW 0x7379CB).
//
// TARGET FACTS (RotWK game.dat, read through tools/mod/rw.py dis):
// - name() RW 0x8AC469 ("DelayedLuaEventUpdate" @ 0xC0B748), getModuleNameKey() RW 0x8AC46F.
// - ModuleData buildFieldParse RW 0x8AC4C9 (table RW 0xC84858 is empty: no fields).
// - ctor RW 0x8AC560: base UpdateModule, m_events ctor, setWakeFrame(object, UPDATE_SLEEP_FOREVER), m_event=0,
//   m_range=0.0f, m_f74=false, m_f75=false.
// - configure RW 0x8AC5E1: m_event=f20, m_events=events (DelayedLuaEventList::operator= RW 0x8AC4B4), m_f74=f74, m_f75=f75, m_range=range,
//   setWakeFrame(object, sleep > 0 ? sleep : UPDATE_SLEEP_NONE).
// - update() RW 0x8AC628: flags = m_f74 ? 6 : 2; if m_f75 flags |= 1; iterateObjectsInRange(obj.pos, m_range,
//   FROM_CENTER_2D, {relationship(flags), alive, !offMap}, NEAR_TO_FAR); for each other != self
//   TheLuaScriptEngine->dispatch(m_event, other, m_events) (RW 0x7379CB); then TheGameLogic->destroyObject(object).
//   The retail caller 0x90DC46 passes event slot 9 (LUAEVENT_DamageIncoming).
// - xfer RW 0x8AC4E0: UpdateModule::xfer, light-CRC early-out, Version(1,1), m_range (float), m_f74 (bool), m_f75 (bool),
//   m_event (4 raw bytes), m_events (the DelayedLuaEventList xfer).
//
// DONOR BFME2 1.06 (workspace/reference/open-bfme-2, GameLogic/Object/Update/DelayedLuaEventUpdate.cpp):
// ctor 0x004A8E7B, update 0x004A8F52, xfer 0x004A8E0A, configure 0x004A8F0B. The behaviour matches RW.
// Local 171: RW 0x64EDF1 (DelayedLuaEventUpdate newInstance), RW 0x64EE29 (DelayedLuaEventUpdate newData)

#pragma once

#include "GameLogic/Module/UpdateModule.h"
#include "GameLogic/ScriptEngine/LuaHost.h"

#include <cstdint>

class ModuleFactory;
class MultiIniFieldParse;

class DelayedLuaEventUpdateModuleData : public ModuleData
{
public:
	static void buildFieldParse(MultiIniFieldParse &p); // RW 0xC84858: empty
};

// RW 0x008AC5C5 (scalar deleting dtor -> RW 0x008AC420)
class DelayedLuaEventUpdate : public UpdateModule
{
public:
	OBFME_MODULE_XFER(DelayedLuaEventUpdate)

	DelayedLuaEventUpdate(Thing *thing, const DelayedLuaEventUpdateModuleData *data);

	// RW 0x8AC5E1 (the retail caller 0x90DC46 configures the event and wakes the module)
	void configure(std::uint32_t event, const LuaEventArgs &events, UpdateSleepTime sleep, float range, bool includeAllies, bool includeEnemies);

	UpdateSleepTime update() override;
	void crc(StateHasher &hasher) const override;

	static void registerClass(ModuleFactory &modules);

private:
	const DelayedLuaEventUpdateModuleData *m_data = nullptr;
	std::uint32_t m_event = 0;    ///< +0x20 the internal event slot (RW 0x90DC46 passes 9 = DamageIncoming)
	LuaEventArgs m_events;        ///< +0x24 the three argument records (RW DelayedLuaEventList, 0x4C bytes)
	float m_range = 0.0f;         ///< +0x70
	bool m_includeAllies = false; ///< +0x74 (RW m_f74: flags 6 instead of 2)
	bool m_includeEnemies = false;///< +0x75 (RW m_f75: flags |= 1)
};
