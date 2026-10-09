// OpenBFME. GPL-3.0.
// See GameLogic/Module/ProductionExitModules.h for the sources and what is not ported.

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "GameLogic/Module/ProductionExitModules.h"

#include "GameLogic/Module/ExitMath.h"

#include "Common/StateHash.h"
#include "Common/Thing/ModuleFactory.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Map/TerrainLogic.h"
#include "GameLogic/Object/Object.h"
#include "Libraries/WWVegas/WWMath/quat.h"

#include <cstddef>
#include <stdexcept>
#include <vector>

namespace
{
#define DP_OFF(member) (int)offsetof(DefaultProductionExitUpdateModuleData, member)
// RW 0xC086A8
const FieldParse kDefaultExitFieldParse[] = {
	{ "UnitCreatePoint", INI::parseCoord3D, nullptr, DP_OFF(m_unitCreatePoint) },
	{ "NaturalRallyPoint", INI::parseCoord3D, nullptr, DP_OFF(m_naturalRallyPoint) },
	{ nullptr, nullptr, nullptr, 0 }
};
#undef DP_OFF
// RW 0xC085E4
const FieldParse kSpawnPointFieldParse[] = {
	{ "SpawnPointBoneName", INI::parseAsciiString, nullptr, (int)offsetof(SpawnPointProductionExitUpdateModuleData, m_spawnPointBoneName) },
	{ nullptr, nullptr, nullptr, 0 }
};

bool hasAIModule(const Object &o)
{
	for (const std::unique_ptr<BehaviorModule> &m : o.modules())
	{
		if (m->getModuleData() && m->getModuleData()->isAiModuleData())
		{
			return true;
		}
	}
	return false;
}

using ExitMath::Mat;
using ExitMath::chain4;
using ExitMath::mul;

template <class Module, class Data>
void registerExit(ModuleFactory &modules, const char *name)
{
	modules.bindTypedData<Data>(name, MODULETYPE_BEHAVIOR);
	modules.bindModuleProc(name, MODULETYPE_BEHAVIOR, [name](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<::Module> {
		const Data *typed = dynamic_cast<const Data *>(data);
		if (!typed)
		{
			throw std::logic_error(std::string(name) + ": the module data is not typed");
		}
		return std::make_unique<Module>(thing, typed);
	});
}
} // namespace

void DefaultProductionExitUpdateModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kDefaultExitFieldParse);
}

void SpawnPointProductionExitUpdateModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kSpawnPointFieldParse);
}

SimpleProductionExitUpdate::SimpleProductionExitUpdate(Thing *thing, const DefaultProductionExitUpdateModuleData *data)
	: UpdateModule(thing, data)
	, m_data(data)
{
	friend_setNextCallFrame((UnsignedInt)UPDATE_SLEEP_FOREVER); // the constructor ends with setWakeFrame(FOREVER)
}

// RW 0x88B2F0 (the function the queue exit shares): the same arithmetic over this module's data
bool SimpleProductionExitUpdate::getNaturalRallyPoint(Coord3D *out, bool offset) const
{
	float px = m_data->m_naturalRallyPoint.x, py = m_data->m_naturalRallyPoint.y, pz = m_data->m_naturalRallyPoint.z;
	ExitMath::offsetRallyPoint(px, py, pz, offset, [](float v) { return BFME2_Inverse_Sqrt(v); });
	const Mat m(*getObject());
	ExitMath::transformRally(m, px, py, pz, out);
	return true;
}

void SimpleProductionExitUpdate::crc(StateHasher &h) const
{
	UpdateModule::crc(h);
	h.addFloat(m_rallyPoint.x);
	h.addFloat(m_rallyPoint.y);
	h.addFloat(m_rallyPoint.z);
	h.addBool(m_rallyPointExists);
}

void DefaultProductionExitUpdate::registerClass(ModuleFactory &modules)
{
	registerExit<DefaultProductionExitUpdate, DefaultProductionExitUpdateModuleData>(modules, "DefaultProductionExitUpdate");
}

void SupplyCenterProductionExitUpdate::registerClass(ModuleFactory &modules)
{
	registerExit<SupplyCenterProductionExitUpdate, DefaultProductionExitUpdateModuleData>(modules, "SupplyCenterProductionExitUpdate");
}

// RW 0x88B479
void DefaultProductionExitUpdate::exitObjectViaDoor(Object *newObj, ExitDoorType)
{
	Object *producer = getObject();
	if (!producer)
	{
		return;
	}
	GameLogic &logic = producer->logic();
	const float x = m_data->m_unitCreatePoint.x, y = m_data->m_unitCreatePoint.y, z = m_data->m_unitCreatePoint.z;
	const Mat m(*producer);
	Coord3D pos;
	pos.x = chain4(mul(m.m01, y), mul(m.m02, z), mul(m.m00, x), m.m03);
	pos.y = chain4(mul(m.m10, x), mul(m.m11, y), mul(m.m12, z), m.m13);
	pos.z = logic.terrain() ? logic.terrain()->getGroundHeight(pos.x, pos.y, nullptr) : 0.0f; // getLayerHeight on the producer's layer (layers are PATH-1's)
	newObj->setPosition(&pos);
	newObj->setOrientation(producer->getOrientation());
	std::vector<Coord3D> path;
	Coord3D t;
	getNaturalRallyPoint(&t, true);
	path.push_back(t);
	const bool ai = hasAIModule(*newObj);
	if (m_rallyPointExists && ai)
	{
		path.push_back(m_rallyPoint); // adjustDestination (RW 0x6FE456) when the AI moves on the ground: pathfinder, not applied (S-202)
	}
	if (ai)
	{
		AICommand c;
		c.type = AICMD_FOLLOW_EXIT_PRODUCTION_PATH;
		c.path = std::move(path);
		c.target = producer->getID();
		logic.aiCommands().issue(*newObj, std::move(c));
	}
}

// RW 0x8A9DBA
void SupplyCenterProductionExitUpdate::exitObjectViaDoor(Object *newObj, ExitDoorType)
{
	Object *producer = getObject();
	if (!producer)
	{
		return;
	}
	GameLogic &logic = producer->logic();
	const float x = m_data->m_unitCreatePoint.x, y = m_data->m_unitCreatePoint.y, z = m_data->m_unitCreatePoint.z;
	const Mat m(*producer);
	Coord3D pos;
	pos.x = chain4(mul(m.m00, x), mul(m.m02, z), mul(m.m01, y), m.m03);
	pos.y = chain4(mul(m.m10, x), mul(m.m11, y), mul(m.m12, z), m.m13);
	pos.z = logic.terrain() ? logic.terrain()->getGroundHeight(pos.x, pos.y, nullptr) : 0.0f;
	newObj->setPosition(&pos);
	newObj->setOrientation(producer->getOrientation());
	// the natural rally point with no offset, through the full matrix
	const float nx = m_data->m_naturalRallyPoint.x, ny = m_data->m_naturalRallyPoint.y, nz = m_data->m_naturalRallyPoint.z;
	Coord3D rp;
	rp.x = chain4(mul(m.m02, nz), mul(m.m01, ny), mul(m.m00, nx), m.m03);
	rp.y = chain4(mul(m.m10, nx), mul(m.m12, nz), mul(m.m11, ny), m.m13);
	rp.z = chain4(mul(m.m21, ny), mul(m.m22, nz), mul(m.m20, nx), m.m23);
	if (hasAIModule(*newObj))
	{
		AICommand c;
		c.type = AICMD_FOLLOW_EXIT_PRODUCTION_PATH;
		c.path.push_back(rp);
		if (m_rallyPointExists)
		{
			c.path.push_back(m_rallyPoint); // appended raw: no adjustDestination
		}
		c.target = producer->getID();
		logic.aiCommands().issue(*newObj, std::move(c));
		// RW 0x8A9E70: a supply truck AI is told setForceWantingState(true): the supply truck AI is not ported (S-209)
	}
}

void SpawnPointProductionExitUpdate::crc(StateHasher &h) const
{
	UpdateModule::crc(h);
	h.addBool(m_bonesInitialized);
	h.addI32(m_spawnPointCount);
	for (int i = 0; i < m_spawnPointCount; ++i)
	{
		h.addFloat(m_worldCoordSpawnPoints[i].x);
		h.addFloat(m_worldCoordSpawnPoints[i].y);
		h.addFloat(m_worldCoordSpawnPoints[i].z);
		h.addFloat(m_worldAngleSpawnPoints[i]);
		h.addU32(m_spawnPointOccupier[i]);
	}
}

SpawnPointProductionExitUpdate::SpawnPointProductionExitUpdate(Thing *thing, const SpawnPointProductionExitUpdateModuleData *data)
	: UpdateModule(thing, data)
	, m_data(data)
{
	friend_setNextCallFrame((UnsignedInt)UPDATE_SLEEP_FOREVER);
}

void SpawnPointProductionExitUpdate::registerClass(ModuleFactory &modules)
{
	registerExit<SpawnPointProductionExitUpdate, SpawnPointProductionExitUpdateModuleData>(modules, "SpawnPointProductionExitUpdate");
}

// RW 0x8A7363: the bones come from the drawable's pristine bone positions (RW 0x672A73); no drawable bone data exists in the logic layer (S-209), so the
// module stays uninitialised exactly as retail's does for an object without a drawable
void SpawnPointProductionExitUpdate::initializeBonePositions()
{
	if (!m_reported)
	{
		m_reported = true;
		getObject()->logic().reportError("SpawnPointProductionExitUpdate of " + getObject()->getTemplate()->getName() + ": the drawable's pristine bones '" + m_data->m_spawnPointBoneName +
			"' are not available to the logic layer (S-209): no unit can leave this producer");
	}
}

// RW 0x8A757D
void SpawnPointProductionExitUpdate::revalidateOccupiers()
{
	GameLogic &logic = getObject()->logic();
	for (int i = 0; i < m_spawnPointCount; ++i)
	{
		if (m_spawnPointOccupier[i] != INVALID_ID && !logic.findObjectByID(m_spawnPointOccupier[i]))
		{
			m_spawnPointOccupier[i] = INVALID_ID;
		}
	}
}

// RW 0x8A75B4
ExitDoorType SpawnPointProductionExitUpdate::reserveDoorForExit(const ThingTemplate *, Object *)
{
	if (!m_bonesInitialized)
	{
		initializeBonePositions();
		if (!m_bonesInitialized)
		{
			return DOOR_NONE_AVAILABLE;
		}
	}
	revalidateOccupiers();
	for (int i = 0; i < m_spawnPointCount; ++i)
	{
		if (m_spawnPointOccupier[i] == INVALID_ID)
		{
			return DOOR_1;
		}
	}
	return DOOR_NONE_AVAILABLE;
}

// RW 0x8A74B7
void SpawnPointProductionExitUpdate::exitObjectViaDoor(Object *newObj, ExitDoorType)
{
	if (!m_bonesInitialized)
	{
		initializeBonePositions();
	}
	if (!getObject())
	{
		return;
	}
	int i = 0;
	while (i < m_spawnPointCount && m_spawnPointOccupier[i] != INVALID_ID)
	{
		++i;
	}
	if (i == m_spawnPointCount)
	{
		return;
	}
	m_spawnPointOccupier[i] = newObj->getID();
	newObj->setPosition(&m_worldCoordSpawnPoints[i]);
	newObj->setOrientation(m_worldAngleSpawnPoints[i]);
	newObj->setDisabled(3, (UnsignedInt)UPDATE_SLEEP_FOREVER); // DISABLED_HELD (RW 0x692432: no end frame)
}
