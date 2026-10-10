// OpenBFME. GPL-3.0.
// See GameLogic/Module/QueueProductionExitUpdate.h for the sources and what is not ported.

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "GameLogic/Module/QueueProductionExitUpdate.h"

#include "GameLogic/Module/ExitMath.h"

#include "Common/StateHash.h"
#include "Common/Thing/ModuleFactory.h"
#include "Common/Thing/ThingTemplate.h"
#include "Common/Player.h"
#include "Common/Team.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Map/TerrainLogic.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "Libraries/WWVegas/WWMath/quat.h"

#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
#define QP_OFF(member) (int)offsetof(QueueProductionExitUpdateModuleData, member)
// RW 0xC08158, in the binary's order
const FieldParse kQueueProductionExitFieldParse[] = {
	{ "UnitCreatePoint", INI::parseCoord3D, nullptr, QP_OFF(m_unitCreatePoint) },
	{ "PlacementViewAngle", INI::parseAngleReal, nullptr, QP_OFF(m_placementViewAngle) },
	{ "NaturalRallyPoint", INI::parseCoord3D, nullptr, QP_OFF(m_naturalRallyPoint) },
	{ "ExitDelay", INI::parseDurationUnsignedInt, nullptr, QP_OFF(m_exitDelay) },
	{ "AllowAirborneCreation", INI::parseBool, nullptr, QP_OFF(m_allowAirborneCreation) },
	{ "InitialBurst", INI::parseUnsignedInt, nullptr, QP_OFF(m_initialBurst) },
	{ "NoExitPath", INI::parseBool, nullptr, QP_OFF(m_noExitPath) },
	{ "CanRallyToSlaughter", INI::parseBool, nullptr, QP_OFF(m_canRallyToSlaughter) },
	{ "UseReturnToFormation", INI::parseBool, nullptr, QP_OFF(m_useReturnToFormation) },
	{ nullptr, nullptr, nullptr, 0 }
};
#undef QP_OFF

// the binary's own status / kind bit numbers, found by name (a mod's order cannot break them)
unsigned statusBit(const char *name)
{
	const int bit = ObjectTemplateInfoBuilder::objectStatusIndex(name);
	if (bit < 0)
	{
		throw std::logic_error(std::string("ObjectStatus name table lacks ") + name);
	}
	return (unsigned)bit;
}

// the object has an AIUpdateInterface (RW +0x260): one of its modules is an AI module (ModuleData slot 4)
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

HordeContainInterface *hordeOf(Object *o)
{
	return o && o->getContain() ? o->getContain()->getHordeContainInterface() : nullptr;
}

using ExitMath::Mat;
using ExitMath::chain4;
using ExitMath::mul;
} // namespace

void QueueProductionExitUpdateModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kQueueProductionExitFieldParse);
}

void QueueProductionExitUpdate::registerClass(ModuleFactory &modules)
{
	modules.bindTypedData<QueueProductionExitUpdateModuleData>("QueueProductionExitUpdate", MODULETYPE_BEHAVIOR);
	modules.bindModuleProc("QueueProductionExitUpdate", MODULETYPE_BEHAVIOR, [](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
		const QueueProductionExitUpdateModuleData *typed = dynamic_cast<const QueueProductionExitUpdateModuleData *>(data);
		if (!typed)
		{
			throw std::logic_error("QueueProductionExitUpdate: the module data is not typed");
		}
		return std::make_unique<QueueProductionExitUpdate>(thing, typed);
	});
}

// RW 0x8A3948
QueueProductionExitUpdate::QueueProductionExitUpdate(Thing *thing, const QueueProductionExitUpdateModuleData *data)
	: UpdateModule(thing, data)
	, m_data(data)
{
	m_currentBurstCount = data->m_initialBurst; // RW 0x8A3996: only when the module data exists
}

// RW 0x8A38BB
UpdateSleepTime QueueProductionExitUpdate::update()
{
	if (isFreeToExit())
	{
		m_currentDelay = 0;
	}
	else
	{
		--m_currentDelay;
	}
	return UPDATE_SLEEP_NONE;
}

// RW 0x8A3AA2
ExitDoorType QueueProductionExitUpdate::reserveDoorForExit(const ThingTemplate *, Object *)
{
	return isFreeToExit() ? DOOR_1 : DOOR_NONE_AVAILABLE;
}

// RW 0x8A39CE: association per axis as the binary computes it
bool QueueProductionExitUpdate::getExitPosition(Coord3D *out, float *angle) const
{
	const Object *o = getObject();
	if (!o)
	{
		return false;
	}
	const float x = m_data->m_unitCreatePoint.x, y = m_data->m_unitCreatePoint.y, z = m_data->m_unitCreatePoint.z;
	const Mat m(*o);
	out->x = chain4(mul(m.m02, z), mul(m.m01, y), mul(m.m00, x), m.m03);
	out->y = chain4(mul(m.m12, z), mul(m.m10, x), mul(m.m11, y), m.m13);
	out->z = chain4(mul(m.m21, y), mul(m.m22, z), mul(m.m20, x), m.m23);
	*angle = SimMath::addf32(m_data->m_placementViewAngle, o->getOrientation());
	return true;
}

// RW 0x88B2F0
bool QueueProductionExitUpdate::getNaturalRallyPoint(Coord3D *out, bool offset) const
{
	float px = m_data->m_naturalRallyPoint.x, py = m_data->m_naturalRallyPoint.y, pz = m_data->m_naturalRallyPoint.z;
	ExitMath::offsetRallyPoint(px, py, pz, offset, [](float v) { return BFME2_Inverse_Sqrt(v); });
	const Mat m(*getObject());
	ExitMath::transformRally(m, px, py, pz, out);
	return true;
}

// RW 0x8A3DD5
void QueueProductionExitUpdate::exitObjectViaDoor(Object *newObj, ExitDoorType)
{
	Object *producer = getObject();
	if (!producer)
	{
		return;
	}
	GameLogic &logic = producer->logic();
	static const unsigned kLeavingFactory = statusBit("IS_LEAVING_FACTORY");
	static const unsigned kUnselectable = statusBit("UNSELECTABLE");
	static const unsigned kUnderConstruction = statusBit("UNDER_CONSTRUCTION");
	// (1) the create point (RW 0x8A3DF4 .. 0x8A3E85: this site's association differs from getExitPosition)
	const float cx = m_data->m_unitCreatePoint.x, cy = m_data->m_unitCreatePoint.y, cz = m_data->m_unitCreatePoint.z;
	const Mat m(*producer);
	Coord3D pos;
	pos.x = chain4(mul(m.m01, cy), mul(m.m02, cz), mul(m.m00, cx), m.m03);
	pos.y = chain4(mul(m.m10, cx), mul(m.m11, cy), mul(m.m12, cz), m.m13);
	pos.z = chain4(mul(m.m20, cx), mul(m.m21, cy), mul(m.m22, cz), m.m23);
	bool creationInAir = false;
	if (const TerrainLogic *terrain = logic.terrain())
	{
		// RW 0x8A3EBF TerrainLogic::isUnderwater(x, y, &z): writes the water height into z whenever standing water covers (x, y), returns true only
		// when that water is above the ground (RW 0x67DAE6, INFERENCE from the research notes)
		float waterZ = 0.0f;
		bool underwater = false;
		const float ground = terrain->getGroundHeight(pos.x, pos.y, nullptr);
		if (terrain->getStandingWaterHeight(pos.x, pos.y, waterZ))
		{
			pos.z = waterZ;
			underwater = waterZ > ground;
		}
		if (!underwater)
		{
			if (pos.z > SimMath::addf32(ground, 1.0f)) // RW 0xBD1908 = 1.0f
			{
				creationInAir = true;
				if (!m_data->m_allowAirborneCreation)
				{
					pos.z = ground;
				}
			}
		}
	}
	const bool ai = hasAIModule(*newObj);
	const bool isGiantBird = ai && newObj->isKindOfName("GIANT_BIRD"); // RW asks the AI (vslot 0x168); the KindOf is the same predicate PU uses for the door (INFERENCE)
	if (isGiantBird)
	{
		pos.z = SimMath::addf32(pos.z, 250.0f); // RW 0xBDD764 (B1: 500)
	}
	newObj->setPosition(&pos);
	newObj->setOrientation(SimMath::addf32(m_data->m_placementViewAngle, producer->getOrientation()));
	(void)creationInAir; // the physics kick (RW 0x792DBD applyMotiveForce, preferred locomotor height > 0.01) is not ported (S-202)
	// RW 0x625E0A: the producer's colour index goes to the new object; RW 0x6E85E9: the pathfind map (not ported, S-202)
	// (4) horde member hookup
	Object *host = logic.findObjectByID(m_lastExitId);
	HordeContainInterface *hci = hordeOf(host);
	if (hci)
	{
		newObj->setProducer(host);
		hci->acceptCreatedMember(newObj);
		newObj->setTeam(host->getTeam());
	}
	// (5) the exit path and the AI command
	const bool isHordeKind = newObj->isKindOfName("HORDE");
	if (ai)
	{
		std::vector<Coord3D> path;
		Coord3D tmp{}; // RW [ebp-0x44]: the last point written (the member's slot, the natural rally point or the rally point), the clearing's destination
		if (hci)
		{
			Coord3D nrp;
			getNaturalRallyPoint(&nrp, true);
			Coord3D mid;
			mid.x = SimMath::addf32(SimMath::mulf32(SimMath::subf32(nrp.x, pos.x), 0.5f), pos.x); // RW 0xBD869C = 0.5f
			mid.y = SimMath::addf32(SimMath::mulf32(SimMath::subf32(nrp.y, pos.y), 0.5f), pos.y);
			mid.z = SimMath::addf32(SimMath::mulf32(SimMath::subf32(nrp.z, pos.z), 0.5f), pos.z);
			path.push_back(mid);
			const Coord3D member = hci->getMemberFormationPosition(newObj);
			path.push_back(member);
			path.push_back(member); // pushed twice (RW 0x8A412D, 0x8A413E)
			tmp = member;
		}
		else
		{
			Coord3D t;
			getNaturalRallyPoint(&t, true);
			path.push_back(t); // snapPosition (RW 0x6EF225) is the pathfinder's: not applied (S-202)
			tmp = t;
		}
		const bool useRally = !isHordeKind && host == nullptr;
		if (m_rallyPointExists && useRally)
		{
			// lane MOVE-3: RW 0x8A4189 .. 0x8A41C4: the rally point is adjusted for the new object (adjustDestination RW 0x6FE456 with its AI's locomotor set,
			// no group destination) unless it was created in the air (the GIANT_BIRD lift); a rally point the pathfinder cannot adjust is not appended
			Coord3D rally = m_rallyPoint;
			bool append = true;
			AIWorld *world = logic.aiWorld();
			AIUpdateInterface *nai = newObj->getAIUpdateInterface();
			if (!isGiantBird && world && world->mapReady() && nai)
			{
				append = world->pathfinder().adjustDestination(world->adapterFor(*newObj), nai->locomotorInfo(), &rally, nullptr);
			}
			if (append)
			{
				path.push_back(rally);
			}
			tmp = rally; // RW 0x8A4193: the rally point is copied to [ebp-0x44] before the adjustment; a failed adjustment leaves it there, not appended
		}
		if (m_data->m_noExitPath && !(m_rallyPointExists && useRally))
		{
			newObj->setStatus(kLeavingFactory, false);
			AICommand c;
			c.type = AICMD_IDLE;
			logic.aiCommands().issue(*newObj, std::move(c));
		}
		else
		{
			newObj->clearDisabled(3); // DISABLED_HELD, RW 0x692443
			AICommand c;
			c.type = AICMD_FOLLOW_EXIT_PRODUCTION_PATH;
			c.path = std::move(path);
			logic.aiCommands().issue(*newObj, std::move(c)); // RW 0x8A4214
			// lane MOVE-3 r2 / r4: RW 0x8A4219 .. 0x8A424F: after the exit command the AI's ignored obstacle (RW 0x662DA5) is saved, replaced by the horde the new
			// object joins (when it joins one, RW 0x662D98 with the horde's id), the allies on the cell line from where the new object stands to [ebp-0x44] are
			// asked to move away (RW 0x6F85A6, which reads the ignored obstacle from the AI) and the saved id is put back. A member of the previous horde standing
			// at the natural rally point hands the request to its horde (RW 0x6F53AF / 0x66DA5F), which steps aside
			AIWorld *world = logic.aiWorld();
			AIUpdateInterface *nai = newObj->getAIUpdateInterface();
			if (world && nai)
			{
				AIMover &mover = nai->mover();
				const PathfindObjectID saved = mover.ignoredObstacleID();
				if (host)
				{
					mover.ignoreObstacle((PathfindObjectID)host->getID());
				}
				world->moveAlliesAwayFromDestination(*newObj, *newObj->getPosition(), tmp, (ObjectID)mover.ignoredObstacleID());
				mover.ignoreObstacle(saved);
			}
		}
	}
	// (6) the produced object is a horde: it goes to the natural rally point and its members are produced next from the same queue entry
	if (isHordeKind)
	{
		Coord3D t;
		getNaturalRallyPoint(&t, true);
		newObj->setPosition(&t); // the aiIdle variant of RW 0x696E63
		if (ai)
		{
			AICommand c;
			c.type = AICMD_IDLE;
			c.source = newObj->testStatus(kLeavingFactory) ? CMD_FROM_AI : CMD_FROM_PLAYER;
			logic.aiCommands().issue(*newObj, std::move(c));
		}
		m_lastExitId = newObj->getID();
		newObj->setStatus(kUnselectable, true);
		newObj->setStatus(kUnderConstruction, true);
	}
	// (7) gating
	m_currentDelay = m_data->m_exitDelay;
	if (m_currentBurstCount)
	{
		--m_currentBurstCount;
	}
}

// RW 0x8A3D3C
void QueueProductionExitUpdate::exitObjectByBudding(Object *newObj, Object *buddingHost)
{
	Object *producer = getObject();
	const Object *from = buddingHost ? buddingHost : producer;
	newObj->setPosition(from->getPosition());
	newObj->setOrientation(from->getOrientation());
	if (hasAIModule(*newObj))
	{
		AICommand c;
		c.type = AICMD_MOVE_TO_POSITION;
		c.path.push_back(*newObj->getPosition());
		producer->logic().aiCommands().issue(*newObj, std::move(c));
	}
	m_currentDelay = m_data->m_exitDelay;
	if (m_currentBurstCount)
	{
		--m_currentBurstCount;
	}
}

// RW 0x8A3BCC; queryRallyOverride (RW 0x8A3AB4) needs the partition manager's closest Slaughter contain: none exists yet (S-202), so no override
void QueueProductionExitUpdate::setRallyPoint(const Coord3D *pos)
{
	m_rallyPoint = *pos;
	m_rallyPointExists = true;
}

// RW 0x8A3BF8
void QueueProductionExitUpdate::releaseLastExit()
{
	Object *producer = getObject();
	GameLogic &logic = producer->logic();
	Object *host = logic.findObjectByID(m_lastExitId);
	if (!host)
	{
		return;
	}
	static const unsigned kUnselectable = statusBit("UNSELECTABLE");
	static const unsigned kUnderConstruction = statusBit("UNDER_CONSTRUCTION");
	m_lastExitId = INVALID_ID;
	HordeContainInterface *hci = hordeOf(host);
	if (!hci)
	{
		host->setStatus(kUnselectable, false);
		host->setStatus(kUnderConstruction, false);
		return;
	}
	// RW 0x8A3C4A: the return-to-formation call is skipped only when `c && !UseReturnToFormation`; c's second part (RW 0x6A950B over a vector of the
	// player manager) is not recovered (S-202), c is the first part: the controlling player is a human
	Player *pl = producer->getControllingPlayer();
	const bool c = pl != nullptr && pl->getPlayerType() == PLAYER_HUMAN;
	if (!(c && !m_data->m_useReturnToFormation))
	{
		AICommand cmd;
		cmd.type = AICMD_HORDE_RETURN_TO_FORMATION;
		logic.aiCommands().issue(*host, std::move(cmd));
	}
	host->setStatus(kUnselectable, false);
	host->setStatus(kUnderConstruction, false);
	if (!m_rallyPointExists)
	{
		return;
	}
	if (hasAIModule(*host))
	{
		AICommand cmd;
		cmd.type = AICMD_MOVE_TO_POSITION;
		cmd.path.push_back(m_rallyPoint);
		logic.aiCommands().issue(*host, std::move(cmd));
	}
}

void QueueProductionExitUpdate::crc(StateHasher &h) const
{
	UpdateModule::crc(h);
	h.addU32(m_currentDelay);
	h.addFloat(m_rallyPoint.x);
	h.addFloat(m_rallyPoint.y);
	h.addFloat(m_rallyPoint.z);
	h.addBool(m_rallyPointExists);
	h.addU32(m_currentBurstCount);
	h.addU32(m_lastExitId);
}
