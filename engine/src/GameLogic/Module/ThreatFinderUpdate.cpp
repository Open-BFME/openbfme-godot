// OpenBFME. GPL-3.0.
// See GameLogic/Module/ThreatFinderUpdate.h for the RW facts, the BFME2 cross-check, the inference and what is not ported (S-7184).

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "GameLogic/Module/ThreatFinderUpdate.h"

#include "Common/GameCommon.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/StateHash.h"
#include "Common/Thing/ModuleFactory.h"
#include "Common/Thing/ThingTemplate.h"
#include "Common/Xfer.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/PartitionManager.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/SkirmishAI/AIThreatFinder.h"
#include "GameLogic/SnapshotXfer.h"

#include <cstddef>
#include <stdexcept>
#include <utility>

namespace
{
// the field table RW 0xC4C320 (field-tables.json): one row, DefaultRadius at + 0x08, parseReal RW 0x42ED00
const FieldParse kFields[] = { { "DefaultRadius", INI::parseReal, nullptr, (int)offsetof(ThreatFinderUpdateModuleData, m_radius) },
	{ nullptr, nullptr, nullptr, 0 } };

// the map property of RW 0x7EDECE (its literal: INFERENCE BFME2 0x00C0953C)
const char *const kRadiusKey = "objectThreatFinderRadius";
const char *const kStop =
	"[S-7184] ThreatFinderUpdate (PORT 17, ported: create RW 0x64F96A, slot 45 RW 0x7EDECE, update RW 0x7EDDB6, TheThreatFinderManager RW 0xDE8888, "
	"the script action RW 0x7C68AA): NOT ported: the condition that reads the same finders (RW 0x608BCA), which is ScriptConditions.cpp's (PORT 16), so "
	"a map script can set a counter from a named threat area but cannot test the area in a condition; INFERENCE: the record's category comes from "
	"ThreatBreakdown's AIKindOf through the ported RotWK reader (AIThreatFinder::threatCategory, RW 0x530)";
} // namespace

// ---- the module ----------------------------------------------------------------------------------------------------------------------------
void ThreatFinderUpdateModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kFields);
}

void ThreatFinderUpdate::registerModule(ModuleFactory &modules)
{
	static const char kName[] = "ThreatFinderUpdate";
	modules.bindTypedData<ThreatFinderUpdateModuleData>(kName, MODULETYPE_BEHAVIOR);
	modules.bindModuleProc(kName, MODULETYPE_BEHAVIOR,
		[](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
			const ThreatFinderUpdateModuleData *typed = dynamic_cast<const ThreatFinderUpdateModuleData *>(data);
			if (!typed)
			{
				throw std::logic_error(std::string(kName) + ": the module data is not typed");
			}
			return std::make_unique<ThreatFinderUpdate>(thing, typed);
		});
}

std::vector<std::string> ThreatFinderUpdate::stopLines()
{
	return { kStop };
}

// RW 0x7EDD3D: no finder until the map's properties arrive (the + 0x20 heap pointer is null)
ThreatFinderUpdate::ThreatFinderUpdate(Thing *thing, const ThreatFinderUpdateModuleData *data)
	: UpdateModule(thing, data)
	, m_data(data)
{
}

// RW 0x7EDF5B: the finder goes; it leaves the manager by its own name first (INFERENCE BFME2 0x003ECDB7)
ThreatFinderUpdate::~ThreatFinderUpdate()
{
	if (m_finder)
	{
		m_finder->removeFromManager(getObject()->logic());
		m_finder.reset();
	}
}

// RW 0x7EDDB6: the owner's position into the finder, every frame
UpdateSleepTime ThreatFinderUpdate::update()
{
	Object *obj = getObject();
	if (m_finder)
	{
		m_finder->setPosition(*obj->getPosition());
	}
	return UPDATE_SLEEP_NONE; // the answer is 1
}

// INFERENCE BFME2 0x003ECE33: the finder of the owner's name, the radius from the map object's properties or the data's
void ThreatFinderUpdate::makeFinder(const Dict *properties)
{
	Object *obj = getObject();
	m_finder = std::make_unique<ThreatFinder>(obj->getName(), false);
	m_finder->registerWithManager(obj->logic());
	bool exists = false;
	const float radius = properties->getReal(kRadiusKey, &exists);
	m_finder->setRadius(exists ? radius : m_data->m_radius);
}

// slot 45 (RW 0x7EDECE): with the map object's properties a finder; with none (a structure built on a plot, RW 0x858A94) nothing
void ThreatFinderUpdate::onMapPlacement(const Dict *properties)
{
	if (!properties)
	{
		return;
	}
	makeFinder(properties);
}

void ThreatFinderUpdate::crc(StateHasher &h) const
{
	UpdateModule::crc(h);
	h.addBool(m_finder != nullptr);
	if (m_finder)
	{
		m_finder->crc(h);
	}
}

// RW 0x7EDF70: version (1, 1), UpdateModule::xfer, then the finder (INFERENCE BFME2 0x003ECED5). Retail's own stream makes the finder on a load without
// asking, which is only symmetric for a module that has one; the port writes that one bit first (OpenBFME native, S-4190), so a module built on a plot (RW
// 0x858A94: no finder) round-trips as it is
void ThreatFinderUpdate::xfer(Xfer &x)
{
	SnapshotXfer::version(x, 1, 1);
	xferUpdateModule(x); // RW 0x850C60
	bool hasFinder = m_finder != nullptr;
	x.xferBool(&hasFinder);
	if (hasFinder && x.isLoading())
	{
		// the finder is made again (named Object + 0x88, "count the structures") and registered in TheThreatFinderManager
		Object *obj = getObject();
		m_finder = std::make_unique<ThreatFinder>(obj->getName(), false);
		m_finder->registerWithManager(obj->logic());
	}
	if (!hasFinder)
	{
		if (x.isLoading())
		{
			m_finder.reset();
		}
		return;
	}
	m_finder->xfer(x);
}
