// OpenBFME. GPL-3.0.
// See GameLogic/Module/ProductionExitModules.h for the sources and what is not ported.

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "GameLogic/Module/ProductionExitModules.h"
#include "GameLogic/SnapshotXfer.h"

#include "GameLogic/Module/ExitMath.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/SupplyTruckAIUpdate.h"

#include "Common/StateHash.h"
#include "Common/Thing/ModuleFactory.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/Combat/CombatState.h"
#include "GameLogic/Combat/WeaponDelivery.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/SimMath.h"
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
		// RW 0x8A9FC4 (lane LUMBER1): the new object's supply interface (AI vslot 0x17C) is told setForceWantingState(true): a worker made here goes harvesting
		if (AIUpdateInterface *ai = newObj->getAIUpdateInterface())
		{
			if (SupplyTruckAIInterface *sup = ai->getSupplyTruckAIInterface())
			{
				sup->setForceWantingState(true);
			}
		}
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

// RW 0x8A7363 (lane PRODUCTION1; BFME2 SpawnPointInitializeBonePositions.cpp, matched): the drawable's pristine bones `SpawnPointBoneName`01, 02, ... (RW 0x672A73
// (name, start index 1, no positions, the transforms, 10, 0) on the drawable, so with its model condition flags), each made a world transform by the object's
// (RW 0x70BCE7 convertBonePosToWorldPos(null, t, null, t): SSE, row r column c = ((T[r][2] * B[2][c] + T[r][1] * B[1][c]) + T[r][0] * B[0][c]), plus T[r][3] for
// the translation); the spawn point is that translation and its angle Get_Z_Rotation (RW 0xB261F0: fpatan(m[1][0], m[0][0]), stored as a float). The bones come
// from the logic's bone provider (RENDER-2's DrawableLaunchBones: the same pristine pose from template data and logic state, no drawable needed). Retail returns
// without marking the module initialised when the object has no drawable; here the module stays uninitialised (and reports once) when no provider is installed
// (a logic without W3D assets). A model without such bones gives no spawn point: initialised with none, no door, as in retail
void SpawnPointProductionExitUpdate::initializeBonePositions()
{
	Object *obj = getObject();
	ProjectileLaunchOffsets *provider = obj->logic().combat().launchOffsets();
	if (!provider)
	{
		if (!m_reported)
		{
			m_reported = true;
			obj->logic().reportError("SpawnPointProductionExitUpdate of " + obj->getTemplate()->getName() + ": no bone provider is installed (no W3D assets), so the bones '" +
				m_data->m_spawnPointBoneName + "' cannot be found: no unit can leave this producer");
		}
		return;
	}
	float bones[MAX_SPAWN_POINTS][12];
	int count = provider->multiLogicalBones(*obj, m_data->m_spawnPointBoneName, obj->getModelConditionBits(), MAX_SPAWN_POINTS, bones);
	if (count < 0)
	{
		count = 0;
	}
	const float *t = obj->getBasis();
	const Coord3D &p = *obj->getPosition();
	const float tt[3] = { p.x, p.y, p.z };
	m_spawnPointCount = count;
	for (int i = 0; i < count; ++i)
	{
		const float *b = bones[i];
		float w[3];
		for (int r = 0; r < 3; ++r)
		{
			const float v = SimMath::addf32(SimMath::mulf32(t[r * 3 + 2], b[11]), SimMath::mulf32(t[r * 3 + 1], b[7]));
			w[r] = SimMath::addf32(SimMath::addf32(v, SimMath::mulf32(t[r * 3 + 0], b[3])), tt[r]);
		}
		// the first column of the world rotation: rows 0 and 1
		const float m00 = SimMath::addf32(SimMath::addf32(SimMath::mulf32(t[2], b[8]), SimMath::mulf32(t[1], b[4])), SimMath::mulf32(t[0], b[0]));
		const float m10 = SimMath::addf32(SimMath::addf32(SimMath::mulf32(t[5], b[8]), SimMath::mulf32(t[4], b[4])), SimMath::mulf32(t[3], b[0]));
		m_worldCoordSpawnPoints[i] = Coord3D{ w[0], w[1], w[2] };
		m_worldAngleSpawnPoints[i] = (float)SimMath::atan2d(m10, m00);
	}
	m_bonesInitialized = true;
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
	newObj->setPosition(&m_worldCoordSpawnPoints[i]); // RW 0x70C201
	newObj->setOrientation(m_worldAngleSpawnPoints[i]); // RW 0x70C31E
	// RW 0x8A7550 .. 0x8A755A: the producer's layer goes to the new object (RW 0x68BBE0 / 0x68BB9D: the ground layer only in the port, S-362); lane PRODUCTION1:
	// RW 0x8A7568: TheAI's pathfinder addObjectToPathfindMap(newObj) (RW 0x6E85E9)
	if (AIWorld *world = getObject()->logic().aiWorld())
	{
		world->addObjectToPathfindMap(*newObj);
	}
	newObj->setDisabled(3, (UnsignedInt)UPDATE_SLEEP_FOREVER); // DISABLED_HELD (RW 0x692432: no end frame)
}

// ---- lane SAVE-4b: the snapshots (OpenBFME native, S-4190; retail's DefaultProductionExitUpdate::xfer RW 0x8A9D7C (1, 1) is the same coord3d + bool after
// UpdateModule::xfer) ------------------------------------------------------------------------------------------------------------------------------------
void SimpleProductionExitUpdate::xfer(Xfer &x)
{
	SnapshotXfer::version(x, 1, 1);
	xferUpdateModule(x);
	SnapshotXfer::coord3D(x, m_rallyPoint);
	x.xferBool(&m_rallyPointExists);
}

void DefaultProductionExitUpdate::xfer(Xfer &x)
{
	SnapshotXfer::version(x, 1, 1);
	SimpleProductionExitUpdate::xfer(x);
}

void SupplyCenterProductionExitUpdate::xfer(Xfer &x)
{
	SnapshotXfer::version(x, 1, 1);
	SimpleProductionExitUpdate::xfer(x);
}

// every spawn point slot (the count says how many are in use); the report flag is the report's
void SpawnPointProductionExitUpdate::xfer(Xfer &x)
{
	SnapshotXfer::version(x, 1, 1);
	xferUpdateModule(x);
	x.xferBool(&m_bonesInitialized);
	x.xferInt(&m_spawnPointCount);
	SnapshotXfer::checkRange(m_spawnPointCount, 0, MAX_SPAWN_POINTS, "a spawn point exit's point count");
	for (int i = 0; i < MAX_SPAWN_POINTS; ++i)
	{
		SnapshotXfer::coord3D(x, m_worldCoordSpawnPoints[i]);
		x.xferReal(&m_worldAngleSpawnPoints[i]);
		x.xferObjectID(&m_spawnPointOccupier[i]);
	}
}
