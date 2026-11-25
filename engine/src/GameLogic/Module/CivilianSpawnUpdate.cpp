// OpenBFME. GPL-3.0.

#include "GameLogic/Module/CivilianSpawnUpdate.h"

#include "Common/INI.h"
#include "Common/Player.h"
#include "Common/Thing/ThingFactory.h"
#include "GameLogic/SnapshotXfer.h"
#include "Common/Thing/ModuleFactory.h"
#include "Common/Xfer.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Combat/CombatQueries.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/PartitionManager.h"
#include "GameLogic/ObjectFilterMatch.h"
#include "GameLogic/SimMath.h"

#include <stdexcept>

namespace {

// RW 0xC5EC68 (DONOR BFME2 0xB42570)
const FieldParse kCivilianSpawnUpdateFieldParse[] = {
	{ "SpawnDelayTime", INI::parseDurationUnsignedInt, nullptr, (int)offsetof(CivilianSpawnUpdateModuleData, m_spawnDelay) },
	{ "RunToFilter", ParseObjectFilter, nullptr, (int)offsetof(CivilianSpawnUpdateModuleData, m_runToFilter) },
	{ "MaximumDistance", INI::parseInt, nullptr, (int)offsetof(CivilianSpawnUpdateModuleData, m_maximumDistance) }, // RW 0x42EC5E
	{ "Civilian", INI::parseAsciiStringVectorAppend, nullptr, (int)offsetof(CivilianSpawnUpdateModuleData, m_civilians) },
	{ nullptr, nullptr, nullptr, 0 }
};

} // namespace

// RW 0x882C20 / DONOR BFME2 0x47F9C3
void CivilianSpawnUpdateModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kCivilianSpawnUpdateFieldParse);
}

// RW 0x882C31 / DONOR BFME2 0x47F9D4
CivilianSpawnUpdate::CivilianSpawnUpdate(Thing *thing, const CivilianSpawnUpdateModuleData *data)
	: UpdateModule(thing, data)
	, m_data(data)
{
	if (getObject() && m_data && m_data->m_spawnDelay > 0)
	{
		m_spawnFrame = getObject()->logic().random().getValue(m_data->m_spawnDelay / 2, m_data->m_spawnDelay, "CivilianSpawnUpdate.cpp", 0x45);
	}
	else
	{
		m_spawnFrame = 0;
	}
}

// RW 0x882D5B / DONOR BFME2 0x47FAFE (CivilianSpawnUpdate.cpp:306)
UpdateSleepTime CivilianSpawnUpdate::update()
{
	Object *obj = getObject();
	if (!obj || !m_data || m_data->m_civilians.empty())
	{
		return UPDATE_SLEEP_FOREVER;
	}
	GameLogic &logic = obj->logic();
	int idx = logic.random().getValue(0, (int)m_data->m_civilians.size() - 1, "CivilianSpawnUpdate.cpp", 0x55);
	const ThingTemplate *unitTemplate = logic.things().findTemplate(m_data->m_civilians[idx]);
	if (!unitTemplate)
	{
		return UPDATE_SLEEP_FOREVER;
	}

	// RW 0x882E0C: cvtsi2ss xmm0, dword ptr [edi + 0x10]
	const PartitionHits hits = logic.partition().iterateObjectsInRange(
		*obj->getPosition(), static_cast<float>(m_data->m_maximumDistance), FROM_CENTER_2D, {}, ITER_FASTEST);

	std::vector<Object *> candidates;
	candidates.reserve(hits.size());
	Player *owner = obj->getControllingPlayer();
	for (const PartitionHit &h : hits)
	{
		Object *cand = h.object;
		if (!cand || cand == obj || cand->isEffectivelyDead() || !cand->isInWorld())
		{
			continue;
		}
		if (ObjectFilterMatch::allows(logic, m_data->m_runToFilter, *cand, owner))
		{
			candidates.push_back(cand);
		}
	}

	if (candidates.empty())
	{
		return (UpdateSleepTime)m_spawnFrame;
	}

	int pick = logic.random().getValue(0, (int)candidates.size() - 1, "CivilianSpawnUpdate.cpp", 0x74);
	Object *runToObject = candidates[pick];

	Object *newObj = logic.newObject(unitTemplate, obj->getTeam(), ObjectStatusMaskType{});
	if (!newObj)
	{
		return UPDATE_SLEEP_FOREVER;
	}

	const float angle = obj->getOrientation();
	const float cosA = (float)SimMath::cosd(angle);
	const float sinA = (float)SimMath::sind(angle);
	const float selfHalfR = SimMath::mulf32(CombatQueries::boundingSphereRadius(*obj), 0.5f);

	Coord3D spawnPos = *obj->getPosition();
	spawnPos.x = SimMath::addf32(spawnPos.x, SimMath::mulf32(cosA, selfHalfR));
	spawnPos.y = SimMath::addf32(spawnPos.y, SimMath::mulf32(sinA, selfHalfR));
	newObj->setPosition(&spawnPos);

	AIUpdateInterface *ai = newObj->getAIUpdateInterface();
	if (!ai)
	{
		return UPDATE_SLEEP_FOREVER;
	}

	ai->chooseLocomotorSet(4); // LOCOMOTORSET_PANIC
	newObj->setModelConditionState(CombatNames::modelCondition("PANICKING"), true);

	const float targetAngle = runToObject->getOrientation();
	const float targetCosA = (float)SimMath::cosd(targetAngle);
	const float targetSinA = (float)SimMath::sind(targetAngle);
	const float targetHalfR = SimMath::mulf32(CombatQueries::boundingSphereRadius(*runToObject), 0.5f);

	Coord3D targetPos = *runToObject->getPosition();
	targetPos.x = SimMath::addf32(targetPos.x, SimMath::mulf32(targetCosA, targetHalfR));
	targetPos.y = SimMath::addf32(targetPos.y, SimMath::mulf32(targetSinA, targetHalfR));

	ai->mover().ignoreObstacle(runToObject->getID());

	bool pathExists = true;
	if (AIWorld *world = logic.aiWorld())
	{
		pathExists = world->pathfinder().quickDoesPathExist(ai->adapter(), newObj->getPosition(), &targetPos, nullptr);
	}

	if (pathExists)
	{
		ai->aiMoveToPosition(targetPos, CMD_FROM_AI);
	}
	else
	{
		logic.destroyObject(newObj);
	}

	ai->mover().ignoreObstacle(PATHFIND_INVALID_ID);
	return (UpdateSleepTime)m_spawnFrame;
}

// RW 0x89A1AC (DONOR BFME2 0x47FA40)
void CivilianSpawnUpdate::xfer(Xfer &x)
{
	XferVersion v{ 1, 2 };
	x.xferVersion(v);
	if (v.current >= 2)
	{
		xferUpdateModule(x);
	}
}

// RW 0x882C31 vtable slot 1 -> crc
void CivilianSpawnUpdate::crc(StateHasher &hasher) const
{
	UpdateModule::crc(hasher);
}

// RW 0x6593F7 / DONOR BFME2 0x65824C
void CivilianSpawnUpdate::registerClass(ModuleFactory &modules)
{
	modules.bindTypedData<CivilianSpawnUpdateModuleData>("CivilianSpawnUpdate", MODULETYPE_BEHAVIOR);
	modules.bindModuleProc(
		"CivilianSpawnUpdate",
		MODULETYPE_BEHAVIOR,
		[](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
			const auto *typed = dynamic_cast<const CivilianSpawnUpdateModuleData *>(data);
			if (!typed)
			{
				throw std::logic_error("CivilianSpawnUpdate: the module data is not typed");
			}
			return std::make_unique<CivilianSpawnUpdate>(thing, typed);
		});
}
