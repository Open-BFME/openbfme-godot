// OpenBFME. GPL-3.0. See GameLogic/Module/DelayedLuaEventUpdate.h (fork issue 285, LOCAL-35).
// Local 169: RW 0x8AC3DC (DelayedLuaEventUpdateModuleData::buildFieldParse)
// Local 174: RW 0x8AC551 (DelayedLuaEventUpdate ??0DelayedLuaEventUpdate@@QAE@PAVThing@@PBVModuleData@@@Z)
#include "GameLogic/Module/DelayedLuaEventUpdate.h"

#include "Common/StateHash.h"
#include "Common/Thing/ModuleFactory.h"
#include "Common/Xfer.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/PartitionManager.h"
#include "GameLogic/SnapshotXfer.h"

#include <cstddef>
#include <memory>
#include <stdexcept>

namespace
{
void xferLuaEventArg(Xfer &x, LuaEventArg &a)
{
	SnapshotXfer::enumIn(x, a.kind, (std::int32_t)LuaEventArg::NONE, (std::int32_t)LuaEventArg::STRING, "DelayedLuaEventUpdate argument kind");
	switch (a.kind)
	{
	case LuaEventArg::REAL:
		x.xferReal(&a.real);
		break;
	case LuaEventArg::BOOLEAN:
		x.xferBool(&a.boolean);
		break;
	case LuaEventArg::OBJECT:
	{
		std::uint32_t id = (std::uint32_t)a.objectId;
		x.xferUnsignedInt(&id);
		a.objectId = (int)id;
		break;
	}
	case LuaEventArg::STRING:
		x.xferAsciiString(&a.text);
		break;
	case LuaEventArg::NONE:
		break;
	}
}

void crcLuaEventArg(StateHasher &h, const LuaEventArg &a)
{
	h.addI32((std::int32_t)a.kind);
	switch (a.kind)
	{
	case LuaEventArg::REAL:
		h.addFloat(a.real);
		break;
	case LuaEventArg::BOOLEAN:
		h.addBool(a.boolean);
		break;
	case LuaEventArg::OBJECT:
		h.addU32((std::uint32_t)a.objectId);
		break;
	case LuaEventArg::STRING:
		h.addString(a.text);
		break;
	case LuaEventArg::NONE:
		break;
	}
}
} // namespace

void DelayedLuaEventUpdateModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	// RW 0xC84858: no fields. The empty table is added so the typed data is parsed with the binary's (empty) rows.
	static const FieldParse kEmpty[] = { { nullptr, nullptr, nullptr, 0 } };
	p.add(kEmpty);
}

DelayedLuaEventUpdate::DelayedLuaEventUpdate(Thing *thing, const DelayedLuaEventUpdateModuleData *data)
	: UpdateModule(thing, data)
	, m_data(data)
{
	// RW 0x8AC560: sleep forever until configure() wakes it; the event, range and flags default to 0/false.
	setWakeFrame(getObject(), UPDATE_SLEEP_FOREVER);
	m_event = 0;
	m_range = 0.0f;
	m_includeAllies = false;
	m_includeEnemies = false;
}

void DelayedLuaEventUpdate::configure(std::uint32_t event, const LuaEventArgs &events, UpdateSleepTime sleep, float range, bool includeAllies, bool includeEnemies)
{
	// RW 0x8AC5E1: m_event, m_events, m_f74, m_f75, m_range, then setWakeFrame(sleep > 0 ? sleep : NONE).
	m_event = event;
	m_events = events;
	m_includeAllies = includeAllies;
	m_includeEnemies = includeEnemies;
	m_range = range;
	setWakeFrame(getObject(), (std::uint32_t)sleep > 0 ? sleep : UPDATE_SLEEP_NONE);
}


UpdateSleepTime DelayedLuaEventUpdate::update()
{
	// RW 0x8AC628: fire the event on every object in range (other than itself), then destroy the object.
	Object *obj = getObject();
	if (!obj)
	{
		return UPDATE_SLEEP_NONE;
	}
	GameLogic &logic = obj->logic();
	const int flags = (m_includeAllies ? 6 : 2) | (m_includeEnemies ? 1 : 0);
	PartitionFilterFn rel([&](Object &o) { return (flags & (1 << (int)obj->getRelationship(o))) != 0; }); // RW 0xC11DC0
	PartitionFilterFn alive([](Object &o) { return !o.isEffectivelyDead(); });                              // RW 0xC10E20
	PartitionFilterFn onMap([](Object &o) { return !o.isOffMap(); });                                       // RW 0xC0F374 (Object + 0x458 bit 3)
	const PartitionHits hits = logic.partition().iterateObjectsInRange(*obj->getPosition(), m_range, FROM_CENTER_2D, { &rel, &alive, &onMap }, ITER_SORTED_NEAR_TO_FAR);
	for (const PartitionHit &hit : hits)
	{
		if (hit.object != obj)
		{
			logic.dispatchScriptEvent((int)m_event, *hit.object, m_events); // RW 0x7379CB
		}
	}
	logic.destroyObject(obj); // RW 0x62BBAB
	return UPDATE_SLEEP_NONE;
}

void DelayedLuaEventUpdate::xfer(Xfer &x)
{
	// RW 0x8AC4E0: m_range (float), m_f74 (bool), m_f75 (bool), m_event (4 raw bytes), m_events (the DelayedLuaEventList xfer).
	SnapshotXfer::version(x, 1, 1);
	xferUpdateModule(x);
	x.xferReal(&m_range);
	x.xferBool(&m_includeAllies);
	x.xferBool(&m_includeEnemies);
	x.xferUnsignedInt(&m_event);
	for (LuaEventArg &arg : m_events.arg)
	{
		xferLuaEventArg(x, arg);
	}
}

void DelayedLuaEventUpdate::crc(StateHasher &hasher) const
{
	UpdateModule::crc(hasher);
	hasher.addU32(m_event);
	hasher.addFloat(m_range);
	hasher.addBool(m_includeAllies);
	hasher.addBool(m_includeEnemies);
	for (const LuaEventArg &arg : m_events.arg)
	{
		crcLuaEventArg(hasher, arg);
	}
}

void DelayedLuaEventUpdate::registerClass(ModuleFactory &modules)
{
	modules.bindTypedData<DelayedLuaEventUpdateModuleData>("DelayedLuaEventUpdate", MODULETYPE_BEHAVIOR);
	modules.bindModuleProc("DelayedLuaEventUpdate", MODULETYPE_BEHAVIOR,
		[](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
			const auto *typed = dynamic_cast<const DelayedLuaEventUpdateModuleData *>(data);
			if (!typed)
			{
				throw std::logic_error("DelayedLuaEventUpdate: the module data is not typed");
			}
			return std::make_unique<DelayedLuaEventUpdate>(thing, typed);
		});
}
