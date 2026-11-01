// OpenBFME. GPL-3.0.
// See GameLogic/Module/CritterEmitterUpdate.h for the target facts, the donors and what is inference.

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "GameLogic/Module/CritterEmitterUpdate.h"

#include "Common/AsciiString.h"
#include "Common/INIException.h"
#include "Common/StateHash.h"
#include "Common/Thing/ModuleFactory.h"
#include "Common/Thing/ThingTemplate.h"
#include "Common/Xfer.h"
#include "GameClient/FXList.h"
#include "GameLogic/FXEvents.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ObjectCreationList.h"
#include "GameLogic/SnapshotXfer.h"

#include <cstddef>
#include <stdexcept>

namespace
{
typedef CritterEmitterUpdateModuleData Data;

const char kFile[] = "CritterEmitter.cpp"; // RW 0xC75550 (the retail source path's file)

// RW 0x73B549: every token of the line, each TheFXListStore->findFXList (RW 0x5E20A2) pushed, NULL included (an unknown name that is not "None" only
// logs, behind the log flag RW 0x4380F0)
void parseFXLists(INI *ini, void *, void *store, const void *)
{
	if (!TheFXListStore)
	{
		throw INIException(3, "TheFXListStore==NULL");
	}
	std::vector<std::string> &out = *static_cast<std::vector<std::string> *>(store);
	for (const char *t = ini->getNextTokenOrNull(); t; t = ini->getNextTokenOrNull())
	{
		out.push_back(TheFXListStore->findFXList(t) ? std::string(t) : std::string());
	}
}

// RW 0x8CD3A8 (stored from offset 0 at + 0x14): every token pushed as findObjectCreationList (RW 0x5EFFE7) would push it (resolved at the call)
void parseSpawnObjects(INI *ini, void *, void *store, const void *)
{
	std::vector<std::string> &out = static_cast<Data *>(store)->m_spawnLists;
	for (const char *t = ini->getNextTokenOrNull(); t; t = ini->getNextTokenOrNull())
	{
		out.push_back(t);
	}
}

// RW 0xC75640
const FieldParse kFields[] = {
	{ "FX", parseFXLists, nullptr, (int)offsetof(Data, m_fxLists) },
	{ "ReloadTime", INI::parseDurationUnsignedInt, nullptr, (int)offsetof(Data, m_reloadTime) },
	{ "SpawnObject", parseSpawnObjects, nullptr, 0 },
	{ nullptr, nullptr, nullptr, 0 }
};
} // namespace

void CritterEmitterUpdateModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kFields);
}

void CritterEmitterUpdate::registerModule(ModuleFactory &modules)
{
	static const char kName[] = "CritterEmitterUpdate";
	modules.bindTypedData<Data>(kName, MODULETYPE_BEHAVIOR);
	modules.bindModuleProc(kName, MODULETYPE_BEHAVIOR, [](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
		const Data *typed = dynamic_cast<const Data *>(data);
		if (!typed)
		{
			throw std::logic_error(std::string(kName) + ": the module data is not typed");
		}
		return std::make_unique<CritterEmitterUpdate>(thing, typed);
	});
}

// RW 0x8CD19A: + 0x24 = 0, + 0x25 = 1; no wake frame (the update runs from the next frame on)
CritterEmitterUpdate::CritterEmitterUpdate(Thing *thing, const Data *data)
	: UpdateModule(thing, data)
	, m_data(data)
{
}

CritterEmitterUpdate::~CritterEmitterUpdate() = default;

// RW 0x8CD297
void CritterEmitterUpdate::onCollide(Object *, const Coord3D *, const Coord3D *)
{
	if (m_collided)
	{
		return;
	}
	Object *obj = getObject();
	GameLogic &logic = obj->logic();
	const int fxCount = (int)m_data->m_fxLists.size();
	if (fxCount > 0)
	{
		const int which = logic.random().getValue(0, fxCount - 1, kFile, 127); // RW 0x6D328E
		const std::string &fx = m_data->m_fxLists[(size_t)which];
		if (FXEventLog::isFXName(fx)) // RW 0x4B1B5A: a NULL list does nothing
		{
			logic.fxEvents().emit(FXEventLog::objectEvent(FXEvent::OBJECT_FX, "CritterEmitterUpdate", logic.getFrame(), fx, *obj)); // doFXObj(fx, obj, 0)
		}
	}
	const int oclCount = (int)m_data->m_spawnLists.size();
	if (oclCount > 0)
	{
		const int which = logic.random().getValue(0, oclCount - 1, kFile, 138); // RW 0x6D328E
		const ObjectCreationList *ocl = TheObjectCreationListStore ? TheObjectCreationListStore->findObjectCreationList(m_data->m_spawnLists[(size_t)which]) : nullptr;
		if (ocl)
		{
			ocl->create(logic, obj, *obj->getPosition()); // RW 0x5F0126(ocl, obj, 0, 0)
		}
	}
	m_collided = true;
	++m_emissions;
}

// RW 0x8CD1F6
UpdateSleepTime CritterEmitterUpdate::update()
{
	Object *obj = getObject();
	if (m_collided)
	{
		obj->friend_setOutOfCollisionManager(true); // RW 0xB6C480: TheCollisionManager removes the object
		m_collided = false;
		m_inManagerSinceCreation = false;
		return (UpdateSleepTime)m_data->m_reloadTime; // data + 0x20
	}
	if (!m_inManagerSinceCreation)
	{
		obj->friend_setOutOfCollisionManager(false); // RW 0xB6C470: added when it has no record
	}
	return UPDATE_SLEEP_NONE; // RW returns 1
}

void CritterEmitterUpdate::crc(StateHasher &hasher) const
{
	UpdateModule::crc(hasher);
	hasher.addBool(m_collided);
	hasher.addBool(m_inManagerSinceCreation);
	hasher.addBool(getObject()->isOutOfCollisionManager());
	hasher.addU64(m_emissions);
}

// RW 0x8CD256: UpdateModule, version (1, 1), + 0x24, + 0x25; then (OpenBFME) the object's collision manager state and the emission counter
void CritterEmitterUpdate::xfer(Xfer &x)
{
	SnapshotXfer::version(x, 1, 1);
	xferUpdateModule(x); // RW 0x850C60
	x.xferBool(&m_collided);
	x.xferBool(&m_inManagerSinceCreation);
	bool out = getObject()->isOutOfCollisionManager();
	x.xferBool(&out);
	if (x.isLoading())
	{
		getObject()->friend_setOutOfCollisionManager(out);
	}
	SnapshotXfer::u64(x, m_emissions);
}
