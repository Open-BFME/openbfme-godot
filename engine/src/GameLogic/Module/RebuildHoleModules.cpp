// OpenBFME. GPL-3.0.
// See GameLogic/Module/RebuildHoleModules.h for the target facts, the donors and what is inference.

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "GameLogic/Module/RebuildHoleModules.h"

#include "Common/NumericState.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/StateHash.h"
#include "Common/Team.h"
#include "Common/Thing/ModuleFactory.h"
#include "Common/Thing/ThingFactory.h"
#include "Common/Thing/ThingTemplate.h"
#include "Common/Xfer.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/BitFlags.h"
#include "GameLogic/Construction.h"
#include "GameLogic/Damage.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/ActiveBody.h"
#include "GameLogic/Module/ConstructionModules.h"
#include "GameLogic/Module/DozerAIUpdate.h"
#include "GameLogic/Module/SpawnBehavior.h"
#include "GameLogic/Module/WorkerAIUpdate.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/ScriptEngine/ScriptEngine.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/SnapshotXfer.h"

#include <cstddef>
#include <memory>
#include <stdexcept>

namespace
{
#define RH_DIE_MUX_ROWS(T)                                                                                                                               \
	{ "DeathTypes", ParseDeathTypeFlags, nullptr, (int)offsetof(T, m_dieMux.m_deathTypes) },                                                              \
	{ "ExemptStatus", ParseObjectStatusMask, nullptr, (int)offsetof(T, m_dieMux.m_exemptStatus) },                                                        \
	{ "RequiredStatus", ParseObjectStatusMask, nullptr, (int)offsetof(T, m_dieMux.m_requiredStatus) },                                                    \
	{ "DamageAmountRequired", INI::parseReal, nullptr, (int)offsetof(T, m_dieMux.m_damageAmountRequired) },                                               \
	{ "MinKillerAngle", INI::parseAngleReal, nullptr, (int)offsetof(T, m_dieMux.m_minKillerAngle) },                                                      \
	{ "MaxKillerAngle", INI::parseAngleReal, nullptr, (int)offsetof(T, m_dieMux.m_maxKillerAngle) },                                                      \
	{ nullptr, nullptr, nullptr, 0 }

const FieldParse kExposeDieMux[] = { RH_DIE_MUX_ROWS(RebuildHoleExposeDieModuleData) }; // RW 0xC76BD8
#undef RH_DIE_MUX_ROWS

// RW 0xC616B8, in the binary's row order
const FieldParse kExposeDie[] = {
	{ "HoleName", INI::parseAsciiString, nullptr, (int)offsetof(RebuildHoleExposeDieModuleData, m_holeName) },               // RW 0x42EE5E, + 0x38
	{ "HoleMaxHealth", INI::parseReal, nullptr, (int)offsetof(RebuildHoleExposeDieModuleData, m_holeMaxHealth) },            // RW 0x42ED00, + 0x3C
	{ "TransferAttackers", INI::parseBool, nullptr, (int)offsetof(RebuildHoleExposeDieModuleData, m_transferAttackers) },    // RW 0x42E558, + 0x44
	{ "FadeInTimeSeconds", INI::parseReal, nullptr, (int)offsetof(RebuildHoleExposeDieModuleData, m_fadeInTimeSeconds) },    // RW 0x42ED00, + 0x40
	{ nullptr, nullptr, nullptr, 0 }
};

// RW 0xC60230, in the binary's row order
const FieldParse kRebuildHole[] = {
	{ "WorkerObjectName", INI::parseAsciiString, nullptr, (int)offsetof(RebuildHoleBehaviorModuleData, m_workerObjectName) },          // RW 0x42EE5E, + 0x10
	{ "WorkerRespawnDelay", INI::parseDurationReal, nullptr, (int)offsetof(RebuildHoleBehaviorModuleData, m_workerRespawnDelay) },     // RW 0x73A403, + 0x08
	{ "HoleHealthRegen%PerSecond", INI::parsePercentToReal, nullptr, (int)offsetof(RebuildHoleBehaviorModuleData, m_holeHealthRegen) }, // RW 0x42EEFA, + 0x0C
	{ nullptr, nullptr, nullptr, 0 }
};

const int kClientFramesPerSecond = 30; // RW 0xD9F60C
const int kLogicFramesPerSecond = 5;   // RW 0xD9F608

int statusBit(const char *name)
{
	const int bit = ObjectTemplateInfoBuilder::objectStatusIndex(name);
	if (bit < 0)
	{
		throw std::logic_error(std::string("RebuildHoleModules: the object status ") + name + " is not in the binary's table");
	}
	return bit;
}

// RW 0x794311(3, 0x3C) with RW 0x68D440: UNSELECTABLE and UNATTACKABLE on / off
void setHoleHidden(Object &hole, bool on)
{
	static const int unselectable = statusBit("UNSELECTABLE"), unattackable = statusBit("UNATTACKABLE");
	hole.setStatus((unsigned)unselectable, on);
	hole.setStatus((unsigned)unattackable, on);
}

// every object of the logic's list with an AI transfers its attack (RW 0x97338F getFirstObject, + 0x8C next, + 0x260 the AI, RW 0x66826E)
void transferAttackers(GameLogic &logic, ObjectID from, ObjectID to)
{
	for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		if (AIUpdateInterface *ai = o->getAIUpdateInterface())
		{
			ai->transferAttack(from, to);
		}
	}
}

// TheScriptEngine RW 0x759467: a non-empty name moves to the object, with the script engine's name cache (ScriptEngine::transferObjectName)
void transferObjectName(const std::string &name, Object &to)
{
	to.logic().scriptEngine().transferObjectName(name, to);
}

template <class Runtime, class Data>
void bind(ModuleFactory &modules, const char *name)
{
	modules.bindTypedData<Data>(name, MODULETYPE_BEHAVIOR);
	modules.bindModuleProc(name, MODULETYPE_BEHAVIOR, [name](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
		const Data *typed = dynamic_cast<const Data *>(data);
		if (!typed)
		{
			throw std::logic_error(std::string(name) + ": the module data is not typed");
		}
		return std::make_unique<Runtime>(thing, typed);
	});
}
} // namespace

void RebuildHoleExposeDieModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kExposeDieMux); // RW 0xC76BD8 (extra 8)
	p.add(kExposeDie);    // RW 0xC616B8
}

void RebuildHoleBehaviorModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kRebuildHole); // RW 0xC60230
}

void RebuildHoleModules::registerModules(ModuleFactory &modules)
{
	bind<RebuildHoleExposeDie, RebuildHoleExposeDieModuleData>(modules, "RebuildHoleExposeDie"); // registration RW 0x658A4F
	bind<RebuildHoleBehavior, RebuildHoleBehaviorModuleData>(modules, "RebuildHoleBehavior");    // registration RW 0x658695
}

// ---- RebuildHoleExposeDie --------------------------------------------------------------------------------------------------------------------------------

// RW 0x889AAF
void RebuildHoleExposeDie::onDie(const DieModuleInterface::Event &event)
{
	Object *us = getObject();
	GameLogic &logic = us->logic();
	bool angle = false;
	if (!m_data->m_dieMux.isDieApplicable(*us, event, &angle)) // RW 0x85FED5
	{
		return;
	}
	if (angle)
	{
		logic.noteStop("[S-980] RebuildHoleExposeDie of " + us->getTemplate()->getName() +
			": MinKillerAngle / MaxKillerAngle are not ported (the die applies as if the killer were in the window)");
	}
	Player *player = us->getControllingPlayer(); // RW 0x68B678 (read unchecked by RW)
	if (!player || player == logic.players().getNeutralPlayer() || player->isObserver() || player->isDefeated() || us->isUnderConstruction()) // RW 0x6AAC52
	{
		return;
	}
	const ThingTemplate *holeTemplate = logic.things().findTemplate(m_data->m_holeName); // RW 0x6D1305
	Team *team = us->getTeam();                                                          // + 0x31C
	if (!holeTemplate || !team)
	{
		// RW passes the null template / team to newObject unchecked
		logic.reportError("RebuildHoleExposeDie of " + us->getTemplate()->getName() + ": " +
			(!holeTemplate ? "HoleName '" + m_data->m_holeName + "' names no object template" : std::string("the object has no team")) + " [S-1427]");
		return;
	}
	Object *hole = logic.newObject(holeTemplate, team, ObjectStatusMaskType{}); // RW 0x6D165E
	if (!hole)
	{
		return;
	}
	++m_holes;
	hole->setPosition(us->getPosition());       // RW 0x70C201
	hole->setOrientation(us->getOrientation()); // RW 0x70C31E
	hole->setGeometryInfoFrom(*us); // RW 0x699349: the dead structure's GeometryInfo (Object + 0xA8), "keep our extents for the rebuilding process"
	transferObjectName(us->getName(), *hole); // RW 0x759467
	if (AIWorld *ai = logic.aiWorld())
	{
		ai->removeObjectFromPathfindMap(*us); // RW 0x6E85FB
		ai->addObjectToPathfindMap(*hole);    // RW 0x6E85E9
	}
	if (BodyModuleInterface *body = hole->getBodyModule()) // + 0x25C (RW calls it unchecked)
	{
		body->setMaxHealth(m_data->m_holeMaxHealth, 0); // vslot 0x5C, change type 0
	}
	if (RebuildHoleBehavior *rhbi = RebuildHoleBehavior::of(*hole)) // RW 0x886448
	{
		if (SpawnBehaviorInterface *spawn = SpawnBehaviorInterface::of(*us)) // RW 0x68C3A3
		{
			rhbi->setSpawnerWasActive(spawn->isSpawnerActive()); // slot 11 -> interface slot 4
		}
		rhbi->startRebuildProcess(static_cast<const ThingTemplate *>(us->getTemplate()), us->getID()); // slot 0
	}
	if (m_data->m_transferAttackers)
	{
		transferAttackers(logic, us->getID(), hole->getID());
	}
	if (m_data->m_fadeInTimeSeconds > 0.0f) // comiss RW 0xC1B594 (0.0), jbe
	{
		if (ObjectClientHooks *client = logic.clientHooks())
		{
			// x87 fild 30, fmul, ftol (truncation): the client's fade (RW 0x70E013 -> RW 0x670AA2)
			client->fadeIn(*hole, (UnsignedInt)SimMath::truncToInt32(NumericState::pc24Mul((float)kClientFramesPerSecond, m_data->m_fadeInTimeSeconds)));
		}
	}
}

void RebuildHoleExposeDie::crc(StateHasher &h) const
{
	h.addU32(m_holes);
}

void RebuildHoleExposeDie::xfer(Xfer &x)
{
	SnapshotXfer::version(x, 1, 1);
	xferBehaviorModule(x);
	x.xferUnsignedInt(&m_holes);
}

// ---- RebuildHoleBehavior ---------------------------------------------------------------------------------------------------------------------------------

// RW 0x8864EE: the fields zero, + 0x40 0.0, + 0x44 TRUE; no wake call (the update runs every frame)
RebuildHoleBehavior::RebuildHoleBehavior(Thing *thing, const RebuildHoleBehaviorModuleData *data)
	: UpdateModule(thing, data)
	, m_data(data)
{
}

RebuildHoleBehavior *RebuildHoleBehavior::of(const Object &obj)
{
	// RW 0x886448: the first module whose name key is "RebuildHoleBehavior" (module slot 0x10)
	for (const std::unique_ptr<BehaviorModule> &m : obj.modules())
	{
		if (RebuildHoleBehavior *r = dynamic_cast<RebuildHoleBehavior *>(m.get()))
		{
			return r;
		}
	}
	return nullptr;
}

// RW 0x8865F7
void RebuildHoleBehavior::newWorkerRespawnProcess(Object *existingWorker)
{
	Object *hole = getObject();
	GameLogic &logic = hole->logic();
	if (existingWorker)
	{
		logic.destroyObject(existingWorker); // RW 0x62BBAB
	}
	m_workerID = INVALID_ID;
	m_workerWaitCounter = (UnsignedInt)SimMath::truncToInt32(m_data->m_workerRespawnDelay); // x87 fld, RW 0xA3CFA4 (ftol)
	hole->maskObject(false);           // RW 0x68BEB1(0)
	setHoleHidden(*hole, false);       // RW 0x794311(3, 0x3C), RW 0x68D440(mask, 0)
	hole->setScriptSelectable(true);   // RW 0x68BE3C(1)
	if (AIWorld *ai = logic.aiWorld())
	{
		ai->addObjectToPathfindMap(*hole); // RW 0x6E85E9
	}
}

// RW 0x886831
void RebuildHoleBehavior::startRebuildProcess(const ThingTemplate *rebuild, ObjectID spawnerID)
{
	GameLogic &logic = getObject()->logic();
	if (const Object *spawner = logic.findObjectByID(spawnerID))
	{
		m_pricePaid = spawner->getBuildCostPaid(); // + 0x40 = the spawner's + 0x33C
	}
	m_rebuildTemplate = rebuild; // + 0x3C
	m_spawnerID = spawnerID;     // + 0x30
	newWorkerRespawnProcess(logic.findObjectByID(m_workerID));
}

// RW 0x8868C2
UpdateSleepTime RebuildHoleBehavior::update()
{
	Object *hole = getObject();
	GameLogic &logic = hole->logic();
	Object *reconstructing = nullptr;
	Object *worker = nullptr;
	if (m_workerID != INVALID_ID)
	{
		worker = logic.findObjectByID(m_workerID);
		if (!worker)
		{
			newWorkerRespawnProcess(nullptr);
		}
	}
	if (m_reconstructingID != INVALID_ID)
	{
		reconstructing = logic.findObjectByID(m_reconstructingID);
		if (!reconstructing)
		{
			newWorkerRespawnProcess(worker);
			m_reconstructingID = INVALID_ID;
		}
	}
	if (!worker && m_workerWaitCounter > 0 && --m_workerWaitCounter == 0)
	{
		Player *owner = hole->getControllingPlayer();
		if (!m_rebuildTemplate || !owner)
		{
			// RW passes them unchecked (a hole made outside RebuildHoleExposeDie has no template)
			logic.reportError("RebuildHoleBehavior of " + hole->getTemplate()->getName() + ": " +
				(!m_rebuildTemplate ? std::string("no rebuild template (startRebuildProcess never ran)") : std::string("the hole has no player")) + " [S-1427]");
			return UPDATE_SLEEP_FOREVER;
		}
		const Coord3D pos = *hole->getPosition();      // + 0x38
		const float angle = hole->getOrientation();    // + 0x44
		if (m_data->m_workerObjectName.empty())        // RW 0x401E64
		{
			if (AIWorld *ai = logic.aiWorld())
			{
				ai->removeObjectFromPathfindMap(*hole); // RW 0x6E85FB
			}
			reconstructing = Construction::buildObjectNow(logic, hole, *m_rebuildTemplate, pos, angle, *owner); // TheBuildAssistant slot 0x38 (RW 0x797796)
			if (!reconstructing)
			{
				return UPDATE_SLEEP_FOREVER; // RW 0x886AB9
			}
			if (GettingBuiltBehavior *gb = dynamic_cast<GettingBuiltBehavior *>(reconstructing->findModule("GettingBuiltBehavior"))) // RW 0x68C3E6
			{
				if (BodyModuleInterface *body = reconstructing->getBodyModule())
				{
					body->setInitialHealthPercent(1.0f); // vslot 0x58 (1.0, 0)
				}
				gb->setForceComplete(true);  // slot 10
				gb->startConstruction(true); // slot 4
			}
			Construction::setModelConditions(*reconstructing, {}, { "PARTIALLY_CONSTRUCTED", "ACTIVELY_BEING_CONSTRUCTED" }); // RW 0x5E417A(0x44, 0x45), RW 0x5E3BA5
			if (ObjectClientHooks *client = logic.clientHooks())
			{
				client->flushModelConditions(*reconstructing); // RW 0x70E013 / 0x67449C(1): the drawable (client)
			}
		}
		else
		{
			if (!m_workerTemplate)
			{
				m_workerTemplate = logic.things().findTemplate(m_data->m_workerObjectName); // RW 0x6D1305
			}
			Team *team = hole->getTeam(); // + 0x31C
			worker = m_workerTemplate && team ? logic.newObject(m_workerTemplate, team, ObjectStatusMaskType{}) : nullptr; // RW 0x6D165E
			if (!m_workerTemplate)
			{
				logic.reportError("RebuildHoleBehavior of " + hole->getTemplate()->getName() + ": WorkerObjectName '" + m_data->m_workerObjectName +
					"' names no object template [S-1427]");
			}
			if (worker)
			{
				worker->setPosition(&pos); // RW 0x70C201
				m_workerID = worker->getID();
				if (AIUpdateInterface *ai = worker->getAIUpdateInterface()) // + 0x260
				{
					if (!reconstructing)
					{
						if (AIWorld *world = logic.aiWorld())
						{
							world->removeObjectFromPathfindMap(*hole); // RW 0x6E85FB
						}
						// the AI's vslot 0x1F8 construct(template, position, angle, player, 1, 0)
						if (WorkerAIUpdate *w = dynamic_cast<WorkerAIUpdate *>(ai))
						{
							reconstructing = w->constructForRebuild(*m_rebuildTemplate, pos, angle, *owner); // RW 0x8AD6A4
						}
						else if (DozerAIUpdate *d = dynamic_cast<DozerAIUpdate *>(ai))
						{
							reconstructing = d->construct(*m_rebuildTemplate, pos, angle, *owner, true); // RW 0x88C217
						}
						else
						{
							// AIUpdateInterface's own slot 0x1F8 (RW: no structure for a non-builder AI)
							logic.reportError("RebuildHoleBehavior of " + hole->getTemplate()->getName() + ": the worker '" + m_data->m_workerObjectName +
								"' has no dozer or worker AI, so nothing rebuilds [S-1427]");
						}
						if (!reconstructing)
						{
							return UPDATE_SLEEP_FOREVER; // RW 0x886AB9
						}
					}
					else if (DozerAIUpdate *d = dynamic_cast<DozerAIUpdate *>(ai))
					{
						d->resumeConstruction(*reconstructing); // aiResumeConstruction(CMD_FROM_AI) RW 0x771526
					}
				}
				static const int unselectable = statusBit("UNSELECTABLE");
				worker->setStatus((unsigned)unselectable, true); // RW 0x6267C7(3), RW 0x68D440(mask, 1)
			}
		}
		if (reconstructing)
		{
			transferAttackers(logic, hole->getID(), reconstructing->getID());
			m_reconstructingID = reconstructing->getID();
			reconstructing->setProducer(hole);              // RW 0x68B6A1
			reconstructing->setBuildCostPaid(m_pricePaid);  // + 0x33C = + 0x40
			if (!m_spawnerWasActive)
			{
				if (SpawnBehaviorInterface *spawn = SpawnBehaviorInterface::of(*reconstructing)) // RW 0x68C3A3
				{
					spawn->stopSpawning(); // slot 9
				}
			}
			++m_rebuilds;
		}
		hole->maskObject(true);          // RW 0x68BEB1(1)
		setHoleHidden(*hole, true);      // RW 0x794311(3, 0x3C), RW 0x68D440(mask, 1)
		hole->setScriptSelectable(false); // RW 0x68BE3C(0)
	}
	if (BodyModuleInterface *body = hole->getBodyModule()) // + 0x25C (RW reads it unchecked)
	{
		if (body->getHealth() < body->getMaxHealth()) // fcomip, jbe
		{
			DamageInfo healing;
			// (HoleHealthRegen / 5) * max: x87 fidiv (fdivr) and fmulp, stored as a float (PC24)
			healing.m_input.m_amount = NumericState::pc24Mul(NumericState::pc24Div(m_data->m_holeHealthRegen, (float)kLogicFramesPerSecond), body->getMaxHealth());
			healing.m_input.m_sourceID = hole->getID();
			healing.m_input.m_damageType = DAMAGE_HEALING; // 7
			healing.m_input.m_deathType = DEATH_NONE;      // 1
			body->attemptHealing(healing);                 // vslot 4
		}
	}
	if (reconstructing)
	{
		static const int canceled = statusBit("BUILD_BEING_CANCELED"); // 0x56
		const bool cancelBuild = reconstructing->testStatus((unsigned)canceled);
		if (cancelBuild || !reconstructing->isUnderConstruction())
		{
			const bool gettingBuilt = reconstructing->findModule("GettingBuiltBehavior") != nullptr; // RW 0x68C3E6
			if (cancelBuild || !gettingBuilt || !reconstructing->isEffectivelyDead()) // + 0x458 bit 0
			{
				transferObjectName(hole->getName(), *reconstructing); // RW 0x759467
				if (worker)
				{
					logic.destroyObject(worker); // RW 0x62BBAB
				}
				hole->kill(DAMAGE_UNRESISTABLE, 0x16); // RW 0x698EC3(8, 0x16)
			}
		}
	}
	return UPDATE_SLEEP_NONE; // RW 0x886C45: 1
}

// RW 0x886878
void RebuildHoleBehavior::onDie(const DieModuleInterface::Event &)
{
	if (m_workerID != INVALID_ID)
	{
		GameLogic &logic = getObject()->logic();
		if (Object *worker = logic.findObjectByID(m_workerID))
		{
			logic.destroyObject(worker); // RW 0x62BBAB
			m_workerID = INVALID_ID;
		}
	}
}

// RW 0x8867BB (the destructor): the same as onDie
void RebuildHoleBehavior::onDelete()
{
	if (m_workerID != INVALID_ID)
	{
		GameLogic &logic = getObject()->logic();
		if (Object *worker = logic.findObjectByID(m_workerID))
		{
			logic.destroyObject(worker);
			m_workerID = INVALID_ID;
		}
	}
}

void RebuildHoleBehavior::crc(StateHasher &h) const
{
	UpdateModule::crc(h);
	h.addU32(m_workerID);
	h.addU32(m_reconstructingID);
	h.addU32(m_spawnerID);
	h.addU32(m_workerWaitCounter);
	h.addU32(m_workerTemplate ? m_workerTemplate->getTemplateID() : 0u);
	h.addU32(m_rebuildTemplate ? m_rebuildTemplate->getTemplateID() : 0u);
	h.addFloat(m_pricePaid);
	h.addBool(m_spawnerWasActive);
	h.addU32(m_rebuilds);
}

// RW 0x886669's fields in its order (OpenBFME native snapshot, S-4190), then the port's counter
void RebuildHoleBehavior::xfer(Xfer &x)
{
	SnapshotXfer::version(x, 1, 1);
	xferUpdateModule(x); // RW 0x850C60
	x.xferObjectID(&m_workerID);
	x.xferObjectID(&m_reconstructingID);
	x.xferObjectID(&m_spawnerID);
	x.xferUnsignedInt(&m_workerWaitCounter);
	GameLogic &logic = getObject()->logic();
	SnapshotXfer::thingTemplate(x, logic, m_workerTemplate);
	SnapshotXfer::thingTemplate(x, logic, m_rebuildTemplate);
	x.xferBool(&m_spawnerWasActive);
	x.xferReal(&m_pricePaid);
	x.xferUnsignedInt(&m_rebuilds);
}

std::vector<std::string> RebuildHoleBehavior::acceptanceStops()
{
	return {
		"[S-1427] RebuildHoleExposeDie / RebuildHoleBehavior (lane MODCOMPAT-MOD1, ported from RW 0x889AAF / 0x8868C2 / 0x886831 / 0x8865F7 / 0x886878): a "
		"live, non-neutral player's dead structure leaves its HoleName object, which rebuilds it after WorkerRespawnDelay (by itself through BuildAssistant "
		"slot 0x38, or through its WorkerObjectName worker's construct with the rebuild byte: RECONSTRUCTING, no price), heals and dies when the structure "
		"is done; the hole carries the dead structure's geometry (RW 0x699349). Not ported: Object::maskObject's "
		"readers (RW 0x6C91EB / 0x81E691 / 0x83BDAF; the masked hole is UNSELECTABLE and UNATTACKABLE) and its deselection (client), a player's or AI's "
		"worker order through RW 0x8AD6A4's non-rebuild branch (it runs DozerAIUpdate's phantom); the fades are the client's",
	};
}
