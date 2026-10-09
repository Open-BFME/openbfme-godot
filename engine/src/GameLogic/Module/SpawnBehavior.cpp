// OpenBFME. GPL-3.0.
// See GameLogic/Module/SpawnBehavior.h for the sources and what is inference.

#include "GameLogic/Module/SpawnBehavior.h"

#include "Common/GameCommon.h"
#include "Common/Player.h"
#include "Common/StateHash.h"
#include "Common/Thing/ModuleFactory.h"
#include "Common/Thing/ThingFactory.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/AI/AIMove.h"
#include "GameLogic/BitFlags.h"
#include "GameLogic/Construction.h"
#include "GameLogic/Damage.h"
#include "GameLogic/Economy.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/ExitInterface.h"
#include "GameLogic/Module/SlavedUpdate.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ObjectFilterMatch.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/SimMath.h"

#include <cstddef>
#include <stdexcept>

namespace
{
const char *const kStop =
	"[S-1425] SpawnBehavior (lane MOD-4): the spawning (RW 0x8633A4 / 0x862456 / 0x862B07), the orphan reclaim (RW 0x862A88 / 0x86255C), the deaths "
	"(RW 0x862DAB from Object::onDie RW 0x6938BD, RW 0x862865), onDelete, onDamage, ShareUpgrades and KillSpawnsBasedOnModelConditionState run; not ported: "
	"AggregateHealth (RW 0x862F4A), ExitByBudding (exit slot 3), the spawn interface's attack / idle orders (slots 3 .. 8, 10, 12), Player::onUnitCreated "
	"(RW 0x6AA688), the fade in (client); INFERENCE: the orphan search walks the logic's object list filtered to the player (RW 0x6ABABD walks its teams)";

const int kSpawnUpdateRate = LOGICFRAMES_PER_SECOND / 2; // RW 0xDE9008 (RW 0xBC5FF9: [0xD9F608] / 2)

int statusBit(const char *name)
{
	return ObjectTemplateInfoBuilder::objectStatusIndex(name);
}

const INITypeFlagListSpec kDamageTypeFlags = { TheDamageNames, 1 }; // RW 0x73A5C9: 1 << (type - 1) over RW 0xDA3960

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif
#define SB_OFF(m) (int)offsetof(SpawnBehaviorModuleData, m)
const FieldParse kSpawnDieMux[] = { // RW 0xC76BD8 (the data's + 0x2C)
	{ "DeathTypes", ParseDeathTypeFlags, nullptr, SB_OFF(m_dieMux.m_deathTypes) },
	{ "ExemptStatus", ParseObjectStatusMask, nullptr, SB_OFF(m_dieMux.m_exemptStatus) },
	{ "RequiredStatus", ParseObjectStatusMask, nullptr, SB_OFF(m_dieMux.m_requiredStatus) },
	{ "DamageAmountRequired", INI::parseReal, nullptr, SB_OFF(m_dieMux.m_damageAmountRequired) },
	{ "MinKillerAngle", INI::parseAngleReal, nullptr, SB_OFF(m_dieMux.m_minKillerAngle) },
	{ "MaxKillerAngle", INI::parseAngleReal, nullptr, SB_OFF(m_dieMux.m_maxKillerAngle) },
	{ nullptr, nullptr, nullptr, 0 },
};
const FieldParse kSpawnBehavior[] = { // RW 0xC06498
	{ "SpawnNumber", INI::parseInt, nullptr, SB_OFF(m_spawnNumber) },
	{ "SpawnReplaceDelay", INI::parseDurationUnsignedInt, nullptr, SB_OFF(m_spawnReplaceDelay) },
	{ "OneShot", INI::parseBool, nullptr, SB_OFF(m_oneShot) },
	{ "CanReclaimOrphans", INI::parseBool, nullptr, SB_OFF(m_canReclaimOrphans) },
	{ "AggregateHealth", INI::parseBool, nullptr, SB_OFF(m_aggregateHealth) },
	{ "ExitByBudding", INI::parseBool, nullptr, SB_OFF(m_exitByBudding) },
	{ "SpawnTemplateName", INI::parseAsciiStringVectorAppend, nullptr, SB_OFF(m_spawnTemplateNames) },
	{ "SpawnedRequireSpawner", INI::parseBool, nullptr, SB_OFF(m_spawnedRequireSpawner) },
	{ "PropagateDamageTypesToSlavesWhenExisting", INI::parseTypeFlagList, &kDamageTypeFlags, SB_OFF(m_propagateDamageTypes) },
	{ "InitialBurst", INI::parseInt, nullptr, SB_OFF(m_initialBurst) },
	{ "RespectCommandLimit", INI::parseBool, nullptr, SB_OFF(m_respectCommandLimit) },
	{ "FadeInTime", INI::parseUnsignedInt, nullptr, SB_OFF(m_fadeInTime) },
	{ "KillSpawnsBasedOnModelConditionState", INI::parseBool, nullptr, SB_OFF(m_killSpawnsBasedOnModelConditionState) },
	{ "ShareUpgrades", INI::parseBool, nullptr, SB_OFF(m_shareUpgrades) },
	{ "SpawnInsideBuilding", INI::parseBool, nullptr, SB_OFF(m_spawnInsideBuilding) },
	{ nullptr, nullptr, nullptr, 0 },
};
#undef SB_OFF
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

// RW 0x66137C (this = the candidate): x87 (x - px)^2 + (y - py)^2 at PC24, left wide
double distSq2D(const Object &c, const Coord3D &p)
{
	const Coord3D *o = c.getPosition();
	const double dx = SimMath::pc24SubW((double)o->x, (double)p.x), dy = SimMath::pc24SubW((double)o->y, (double)p.y);
	return SimMath::pc24AddW(SimMath::pc24MulW(dy, dy), SimMath::pc24MulW(dx, dx));
}
} // namespace

void SpawnBehaviorModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	// RW 0x6572BA: the upgrade mux (RW 0xC76AD8), the die mux (RW 0xC76BD8), the class's table (RW 0xC06498), in that order
	UpgradeModuleData::buildBaseFieldParse(p);
	p.add(kSpawnDieMux);
	p.add(kSpawnBehavior);
}

SpawnBehaviorInterface *SpawnBehaviorInterface::of(Object &obj)
{
	for (const std::unique_ptr<BehaviorModule> &m : obj.modules()) // RW 0x68C3A3: behaviour slot 0x78 of each module
	{
		if (SpawnBehaviorInterface *s = dynamic_cast<SpawnBehaviorInterface *>(m.get()))
		{
			return s;
		}
	}
	return nullptr;
}

void SpawnBehaviorInterface::notifyProducerOfDeath(Object &dead, const DieModuleInterface::Event &info)
{
	// RW 0x6938BD: the producer (+ 0x78) found by id; its spawn interface's onSpawnDeath. Without one the producer's contain's horde interface slot 0x34 is
	// told (HordeContain's member death: COMBAT-1 removes a dead member from its horde in Object::friend_onDie)
	Object *producer = dead.logic().findObjectByID(dead.getProducerID());
	if (!producer)
	{
		return;
	}
	if (SpawnBehaviorInterface *s = SpawnBehaviorInterface::of(*producer))
	{
		s->onSpawnDeath(dead.getID(), &info);
	}
}

SpawnBehavior::Stats &SpawnBehavior::stats()
{
	static thread_local Stats s;
	return s;
}

SpawnBehavior::SpawnBehavior(Thing *thing, const SpawnBehaviorModuleData *data)
	: UpdateModule(thing, data)
	, UpgradeMux(thing->asObject(), data)
	, m_data(data)
{
	// RW 0x862606
	m_oneShotCountdown = data->m_oneShot ? data->m_spawnNumber : -1;
	m_initialBurstTimes = data->m_initialBurst;
	m_aggregateHealth = data->m_aggregateHealth;
}

bool SpawnBehavior::shouldTryToSpawn()
{
	// RW 0x862456
	if (!m_active)
	{
		return false;
	}
	Object *obj = getObject();
	static const int kReconstructing = statusBit("RECONSTRUCTING"), kUnderConstruction = statusBit("UNDER_CONSTRUCTION"), kSold = statusBit("SOLD");
	static const int kJustBuilt = Construction::modelConditionIndex("JUST_BUILT");
	if (m_data->m_oneShot && obj->testStatus((unsigned)kReconstructing))
	{
		stopSpawning(); // spawn interface slot 9
		return false;
	}
	if (obj->testStatus((unsigned)kUnderConstruction) || obj->testStatus((unsigned)kSold) || obj->testModelCondition(kJustBuilt))
	{
		return false;
	}
	return !obj->isEffectivelyDead(); // + 0x458 bit 0
}

Object *SpawnBehavior::findClosestOrphan()
{
	// RW 0x86255C
	Object *obj = getObject();
	GameLogic &logic = obj->logic();
	const Player *player = obj->getControllingPlayer();
	static const int kBurningDeath = Construction::modelConditionIndex("BURNINGDEATH"); // model condition 0x220
	Object *best = nullptr;
	double bestDist = 100000000.0; // RW 0xC58D30
	std::string previous;
	for (const std::string &name : m_data->m_spawnTemplateNames)
	{
		if (name == previous)
		{
			continue; // RW 0x8625A6: a name equal to the one before is not searched again
		}
		previous = name;
		const ThingTemplate *tt = logic.things().findTemplate(name); // RW 0x6D1305
		for (Object *c = logic.getFirstObject(); c; c = c->getNextObject()) // RW 0x6ABABD with RW 0x8623F9 (INFERENCE: the logic's list, S-1425)
		{
			if (c->getControllingPlayer() != player || !tt || !ObjectFilterMatch::isEquivalentTo(c->getTemplate(), tt)) // RW 0x73D5C2
			{
				continue;
			}
			if (c->getProducerID() != INVALID_ID || c->testModelCondition(kBurningDeath))
			{
				continue;
			}
			const double d = distSq2D(*c, *obj->getPosition());
			if (d < bestDist) // fcompi: the best above the distance
			{
				best = c;
				bestDist = (double)SimMath::fstpDword(d); // fst dword, then the float is kept
			}
		}
	}
	return best;
}

bool SpawnBehavior::reclaimOrphan()
{
	// RW 0x862A88
	Object *obj = getObject();
	if (!m_data->m_canReclaimOrphans)
	{
		return false;
	}
	Object *orphan = findClosestOrphan();
	if (!orphan)
	{
		return false;
	}
	orphan->setProducer(obj); // RW 0x68B6A1
	if (SlavedUpdateInterface *s = SlavedUpdateInterface::of(*orphan))
	{
		s->onEnslave(obj);
	}
	m_spawnIDs.push_back(orphan->getID());
	++stats().reclaimed;
	return true;
}

bool SpawnBehavior::createSpawn()
{
	// RW 0x862B07
	Object *obj = getObject();
	GameLogic &logic = obj->logic();
	ExitInterface *exit = obj->getObjectExitInterface(); // RW 0x68BB14
	if (!exit)
	{
		++stats().refusedNoExit;
		return false;
	}
	const ExitDoorType door = exit->reserveDoorForExit(nullptr, nullptr); // exit slot 1 (0, 0)
	if (door == DOOR_NONE_AVAILABLE)
	{
		return false;
	}
	if (m_data->m_spawnTemplateNames.empty())
	{
		logic.reportError("SpawnBehavior: " + obj->getTemplate()->getName() + " has no SpawnTemplateName");
		return false;
	}
	const ThingTemplate *tt = logic.things().findTemplate(m_data->m_spawnTemplateNames[m_nextTemplateName]); // RW 0x6D1305
	Player *player = obj->getControllingPlayer();
	if (m_data->m_respectCommandLimit && tt && player)
	{
		// RW 0x6A7F6A(1): the limit minus the usage (32-bit), against the template's CommandPoints (+ 0x628)
		const int cost = (int)Economy::templateInt(*tt, "CommandPoints");
		const int available = (int)((unsigned)logic.economy().commandPointLimit(*player) - (unsigned)player->commandPoints().getUsage());
		if (available < cost)
		{
			++stats().refusedCommandLimit;
			return false; // the reserved door is kept (as retail: no unreserve on this path)
		}
	}
	if (!tt)
	{
		logic.reportError("SpawnBehavior: no template " + m_data->m_spawnTemplateNames[m_nextTemplateName]);
		return false;
	}
	Object *spawn = logic.newObject(tt, obj->getTeam(), ObjectStatusMaskType{}); // RW 0x6D165E
	if (!spawn)
	{
		logic.reportError("SpawnBehavior: newObject made nothing for " + tt->getName());
		return false;
	}
	// Player::onUnitCreated (RW 0x6AA688): not ported (S-1425)
	if (m_data->m_spawnInsideBuilding)
	{
		if (AIUpdateInterface *ai = spawn->getAIUpdateInterface())
		{
			ai->mover().ignoreObstacle(obj->getID()); // RW 0x66831A
		}
	}
	// FadeInTime: the drawable's fade (RW 0x678F54 / 0x670AA2): client
	if (++m_nextTemplateName >= m_data->m_spawnTemplateNames.size())
	{
		m_nextTemplateName = 0;
	}
	spawn->setProducer(obj); // RW 0x68B6A1
	if (SlavedUpdateInterface *s = SlavedUpdateInterface::of(*spawn))
	{
		s->onEnslave(obj);
	}
	m_spawnIDs.push_back(spawn->getID());
	if (!m_data->m_exitByBudding)
	{
		exit->exitObjectViaDoor(spawn, door); // exit slot 2
	}
	else
	{
		++stats().unported; // ExitByBudding (exit slot 3, the initial burst through the producer's exit): no retail use
		logic.noteStop(kStop);
		exit->exitObjectViaDoor(spawn, door);
	}
	if (m_data->m_oneShot)
	{
		--m_oneShotCountdown;
	}
	m_spawnCount = m_spawnCount == -1 ? 1 : m_spawnCount + 1;
	++stats().spawned;
	return true;
}

UpdateSleepTime SpawnBehavior::update()
{
	// RW 0x8633A4
	Object *obj = getObject();
	GameLogic &logic = obj->logic();
	if (m_aggregateHealth)
	{
		++stats().unported; // RW 0x862F4A: no retail object sets AggregateHealth
		logic.noteStop(kStop);
	}
	if (m_data->m_activationMask.any() && !isAlreadyUpgraded()) // the upgrade mux (+ 0x30) slot 0
	{
		return UPDATE_SLEEP_NONE;
	}
	if (m_data->m_shareUpgrades)
	{
		for (ObjectID id : m_spawnIDs)
		{
			Object *s = logic.findObjectByID(id);
			if (s && !(obj->getUpgradeMask() == s->getUpgradeMask()))
			{
				s->friend_orUpgradeMask(obj->getUpgradeMask()); // RW 0x68CC6C
				s->updateUpgradeModules();                     // RW 0x6936FE
			}
		}
	}
	if (!m_initialized)
	{
		m_initialized = true;
		const bool hasProducer = obj->getProducerID() != INVALID_ID; // + 0x78
		int burst = m_initialBurstTimes;
		for (int i = 0; i < m_data->m_spawnNumber; ++i)
		{
			if (m_data->m_initialBurst < 1)
			{
				m_replacementFrames.push_back((unsigned)i);
			}
			else
			{
				if (hasProducer && burst > 0)
				{
					--burst; // a local count only (RW 0x863493)
				}
				m_replacementFrames.push_back(hasProducer ? 1u : 0u);
			}
		}
	}
	if (--m_framesToWait < 1)
	{
		m_framesToWait = kSpawnUpdateRate;
		if (shouldTryToSpawn())
		{
			const unsigned now = logic.getFrame();
			for (auto it = m_replacementFrames.begin(); it != m_replacementFrames.end();)
			{
				if (*it < now && (reclaimOrphan() || createSpawn()))
				{
					it = m_replacementFrames.erase(it);
				}
				else
				{
					++it;
				}
			}
			if (m_data->m_oneShot && m_oneShotCountdown < 1)
			{
				stopSpawning();
			}
			if (m_data->m_killSpawnsBasedOnModelConditionState)
			{
				stopSpawning();
			}
		}
	}
	return UPDATE_SLEEP_NONE;
}

void SpawnBehavior::onSpawnDeath(ObjectID deadSpawn, const DieModuleInterface::Event *info)
{
	// RW 0x862DAB
	for (auto it = m_spawnIDs.begin(); it != m_spawnIDs.end(); ++it)
	{
		if (*it != deadSpawn)
		{
			continue;
		}
		Object *obj = getObject();
		GameLogic &logic = obj->logic();
		m_replacementFrames.push_back(m_data->m_spawnReplaceDelay + logic.getFrame());
		m_spawnIDs.erase(it);
		++stats().spawnDeaths;
		if (--m_spawnCount == 0 && m_aggregateHealth)
		{
			if (info)
			{
				if (Object *killer = logic.findObjectByID(info->sourceId))
				{
					killer->scoreTheKill(*obj, 1); // RW 0x6955BC
				}
			}
			obj->kill(0); // RW 0x698EC3(8, 0)
		}
		return;
	}
}

void SpawnBehavior::onDie(const DieModuleInterface::Event &event)
{
	// RW 0x862865
	Object *obj = getObject();
	if (!m_data->m_dieMux.isDieApplicable(*obj, event)) // RW 0x8D29A9
	{
		return;
	}
	GameLogic &logic = obj->logic();
	const std::list<ObjectID> ids = m_spawnIDs;
	for (ObjectID id : ids)
	{
		if (Object *s = logic.findObjectByID(id))
		{
			if (SlavedUpdateInterface *si = SlavedUpdateInterface::of(*s))
			{
				si->onSlaverDie(&event);
			}
			s->setProducer(nullptr); // RW 0x68B6A1(0)
		}
	}
	if (m_data->m_spawnedRequireSpawner)
	{
		for (ObjectID id : ids)
		{
			Object *s = logic.findObjectByID(id);
			if (s && !s->isEffectivelyDead())
			{
				s->kill(0); // RW 0x698EC3(8, 0)
			}
		}
	}
}

void SpawnBehavior::onDelete()
{
	// RW 0x86280D
	if (!m_data->m_spawnedRequireSpawner)
	{
		return;
	}
	GameLogic &logic = getObject()->logic();
	const std::list<ObjectID> ids = m_spawnIDs;
	for (ObjectID id : ids)
	{
		Object *s = logic.findObjectByID(id);
		if (s)
		{
			s->setProducer(nullptr); // RW 0x68B6A1(0)
			if (!s->isEffectivelyDead())
			{
				logic.destroyObject(s); // RW 0x62BBAB
			}
		}
	}
}

void SpawnBehavior::onDamage(const DamageInfo &info)
{
	// RW 0x862EA3
	GameLogic &logic = getObject()->logic();
	const std::list<ObjectID> ids = m_spawnIDs;
	for (ObjectID id : ids)
	{
		if (Object *s = logic.findObjectByID(id))
		{
			if (SlavedUpdateInterface *si = SlavedUpdateInterface::of(*s))
			{
				si->onSlaverDamage(info); // slaved slot 3
			}
		}
	}
}

void SpawnBehavior::killSpawns(int count)
{
	// RW 0x863225: nothing when count is above the spawn count or the count is 0; else the first `count` live spawns of the list die
	if (count > m_spawnCount || m_spawnCount == 0)
	{
		return;
	}
	GameLogic &logic = getObject()->logic();
	int killed = 0;
	const std::list<ObjectID> ids = m_spawnIDs;
	for (ObjectID id : ids)
	{
		Object *s = logic.findObjectByID(id);
		if (s && !s->isEffectivelyDead())
		{
			s->kill(0);
			if (++killed == count)
			{
				return;
			}
		}
	}
}

void SpawnBehavior::onBodyDamageStateChange(BodyDamageType oldState, BodyDamageType newState)
{
	// RW 0x863585
	if (!m_data->m_killSpawnsBasedOnModelConditionState)
	{
		return;
	}
	const int count = m_spawnCount;
	if (oldState == BODY_PRISTINE)
	{
		if (newState == BODY_DAMAGED)
		{
			killSpawns(count / 3);
		}
	}
	else if (oldState == BODY_DAMAGED)
	{
		if (newState == BODY_REALLYDAMAGED)
		{
			killSpawns(count / 2);
		}
		else if (newState == BODY_PRISTINE)
		{
			for (int n = m_data->m_spawnNumber - count; n > 0; --n)
			{
				createSpawn();
			}
		}
	}
	else if (oldState == BODY_REALLYDAMAGED && newState == BODY_DAMAGED)
	{
		for (int n = (m_data->m_spawnNumber - count) / 2; n > 0; --n)
		{
			createSpawn();
		}
	}
}

void SpawnBehavior::crc(StateHasher &h) const
{
	UpdateModule::crc(h);
	crcMux(h);
	h.addI32(m_oneShotCountdown);
	h.addI32(m_framesToWait);
	h.addU32((std::uint32_t)m_replacementFrames.size());
	for (unsigned f : m_replacementFrames)
	{
		h.addU32(f);
	}
	h.addU32((std::uint32_t)m_spawnIDs.size());
	for (ObjectID id : m_spawnIDs)
	{
		h.addU32((std::uint32_t)id);
	}
	h.addBool(m_active);
	h.addBool(m_aggregateHealth);
	h.addBool(m_initialized);
	h.addI32(m_spawnCount);
	h.addI32(m_selfTaskingSpawnCount);
	h.addI32(m_initialBurstTimes);
	h.addU32((std::uint32_t)m_nextTemplateName);
}

void SpawnBehavior::registerClass(ModuleFactory &modules)
{
	const char *name = "SpawnBehavior";
	modules.bindTypedData<SpawnBehaviorModuleData>(name, MODULETYPE_BEHAVIOR);
	modules.bindModuleProc(name, MODULETYPE_BEHAVIOR, [name](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
		const SpawnBehaviorModuleData *typed = dynamic_cast<const SpawnBehaviorModuleData *>(data);
		if (!typed)
		{
			throw std::logic_error(std::string(name) + ": the module data is not typed");
		}
		return std::make_unique<SpawnBehavior>(thing, typed);
	});
}

std::vector<std::string> SpawnBehavior::stopLines()
{
	return { kStop };
}
