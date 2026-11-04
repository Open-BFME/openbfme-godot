// OpenBFME. GPL-3.0.
// See GameLogic/Module/ThreatFinderUpdate.h for the RW facts.

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "GameLogic/Module/ThreatFinderUpdate.h"

#include "Common/Dict.h"
#include "Common/INI.h"
#include "Common/StateHash.h"
#include "Common/Thing/ModuleFactory.h"
#include "Common/Xfer.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SkirmishAI/AIThreatFinder.h"
#include "GameLogic/SnapshotXfer.h"

#include <cstddef>
#include <stdexcept>

namespace
{
typedef ThreatFinderUpdateModuleData Data;

// RW 0xC4C320: DefaultRadius, parseReal RW 0x42ED00, at + 0x08
const FieldParse kFields[] = {
	{ "DefaultRadius", INI::parseReal, nullptr, (int)offsetof(Data, m_defaultRadius) },
	{ nullptr, nullptr, nullptr, 0 },
};

constexpr const char *kRadiusProperty = "objectThreatFinderRadius"; // RW 0xDA2F24
} // namespace

void ThreatFinderUpdateModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kFields);
}

// RW 0x7EDD3D: the UpdateModule constructor, finder null
ThreatFinderUpdate::ThreatFinderUpdate(Thing *thing, const ThreatFinderUpdateModuleData *data)
	: UpdateModule(thing, data)
	, m_data(data)
{
	if (!data)
	{
		throw std::logic_error("ThreatFinderUpdate: no module data");
	}
}

ThreatFinderUpdate::~ThreatFinderUpdate()
{
	destroyFinder();
}

// RW 0x7EDECE: the finder named by the object's script name; the map property overrides DefaultRadius
void ThreatFinderUpdate::createFinder(const Dict *properties)
{
	destroyFinder();
	Object *obj = getObject();
	if (!obj)
	{
		return;
	}
	const std::string &name = obj->getName(); // Object + 0x88
	m_finder = std::make_unique<AIThreatFinder>(name, false);
	float radius = m_data->m_defaultRadius; // RW 0x7EDF51: the data's DefaultRadius when the property is absent
	bool exists = false;
	if (properties)
	{
		const float fromMap = properties->getReal(kRadiusProperty, &exists); // RW 0x714ACA
		if (exists)
		{
			radius = fromMap;
		}
	}
	m_finder->setRadius(radius);
	if (!name.empty())
	{
		obj->logic().threatFinders().add(name, *m_finder); // RW 0x7EDEA4 -> RW 0x7EE33A
	}
}

// RW 0x7EDE52 / 0x7EDFFF: the name's entry goes before the finder does
void ThreatFinderUpdate::destroyFinder()
{
	if (!m_finder)
	{
		return;
	}
	Object *obj = getObject();
	const std::string name = m_finder->getName();
	if (obj && !name.empty())
	{
		obj->logic().threatFinders().remove(name, *m_finder);
	}
	m_finder.reset();
}

// RW 0x7EDDB6: copy the object's position into the finder (the map area follows the marker every frame); return 1
UpdateSleepTime ThreatFinderUpdate::update()
{
	Object *obj = getObject();
	if (m_finder && obj)
	{
		m_finder->setPosition(*obj->getPosition());
	}
	return UPDATE_SLEEP_NONE; // RW 0x7EDDF6: inc eax -> 1
}

void ThreatFinderUpdate::onMapPlacement(const Dict *properties)
{
	createFinder(properties);
}

void ThreatFinderUpdate::onDelete()
{
	destroyFinder();
}

// RW 0x7EDF70: UpdateModule::xfer, then on load make the finder when the snapshot's object was not map-placed, then the finder's own block RW 0x7EDC93
void ThreatFinderUpdate::xfer(Xfer &x)
{
	SnapshotXfer::version(x, 1, 1);
	xferUpdateModule(x);
	// the block's shape: a finder that exists (the map placement made one) or not. Retail creates one on load when the snapshot's object
	// has none (RW 0x7EDFBC .. 0x7EDFD0) and then always xfers it; an explicit flag keeps a save from making one (OpenBFME native, S-4190)
	bool hasFinder = m_finder != nullptr;
	x.xferBool(&hasFinder);
	if (x.isLoading())
	{
		if (hasFinder && !m_finder)
		{
			createFinder(nullptr); // the object's name and the data's DefaultRadius, as the load path RW 0x7EDFBC .. 0x7EDFD0 does
		}
		else if (!hasFinder && m_finder)
		{
			destroyFinder();
		}
	}
	if (!m_finder)
	{
		return;
	}
	const std::string oldName = m_finder->getName();
	m_finder->xfer(x);
	if (x.isLoading() && oldName != m_finder->getName())
	{
		// a saved name that differs from the object's: the manager follows the finder's loaded name
		Object *obj = getObject();
		if (obj)
		{
			ThreatFinderManager &manager = obj->logic().threatFinders();
			if (!oldName.empty())
			{
				manager.remove(oldName, *m_finder);
			}
			if (!m_finder->getName().empty())
			{
				manager.add(m_finder->getName(), *m_finder);
			}
		}
	}
}

void ThreatFinderUpdate::crc(StateHasher &hasher) const
{
	hasher.addU32(0x54465531u); // "TFU1"
	hasher.addBool(m_finder != nullptr);
	if (m_finder)
	{
		m_finder->crc(hasher);
	}
}

void ThreatFinderUpdate::registerModule(ModuleFactory &modules)
{
	static const char kName[] = "ThreatFinderUpdate";
	modules.bindTypedData<ThreatFinderUpdateModuleData>(kName, MODULETYPE_BEHAVIOR);
	modules.bindModuleProc(kName, MODULETYPE_BEHAVIOR, [](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
		const ThreatFinderUpdateModuleData *typed = dynamic_cast<const ThreatFinderUpdateModuleData *>(data);
		if (!typed)
		{
			throw std::logic_error("ThreatFinderUpdate: the module data is not typed");
		}
		return std::make_unique<ThreatFinderUpdate>(thing, typed);
	});
}
