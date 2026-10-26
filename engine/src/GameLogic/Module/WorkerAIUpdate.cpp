// OpenBFME. GPL-3.0.
// See GameLogic/Module/WorkerAIUpdate.h for the target facts.

#include "GameLogic/Module/WorkerAIUpdate.h"

#include "Common/NumericState.h"
#include "Common/Player.h"
#include "Common/ResourceGatheringManager.h"
#include "Common/StateHash.h"
#include "Common/Thing/ModuleFactory.h"
#include "Common/Thing/RawModuleData.h"
#include "Common/Xfer.h"
#include "GameLogic/AI/AIDock.h"
#include "GameLogic/AI/AIHarvest.h"
#include "GameLogic/AI/AIXfer.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/FindPositionAround.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/UnportedModule.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/WeaponState.h"
#include "GameLogic/AI/AIPathfindHost.h"
#include "Common/INI/HostRealText.h"

#include <cctype>
#include <cfloat>
#include <cstdio>
#include <stdexcept>

namespace
{
// ---- the module data (RW table 0xC058C8), read from the raw module lines as DozerAIUpdate reads its own -----------------------------
std::string trimmed(const std::string &s)
{
	const size_t b = s.find_first_not_of(" \t\r\n");
	const size_t e = s.find_last_not_of(" \t\r\n");
	return b == std::string::npos ? std::string() : s.substr(b, e - b + 1);
}

bool sameNoCase(const std::string &a, const char *b)
{
	size_t i = 0;
	for (; i < a.size() && b[i]; ++i)
	{
		if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i]))
		{
			return false;
		}
	}
	return i == a.size() && !b[i];
}

// the value text of `Field = value` (comments removed), "" when the body does not set it
std::string rawField(const ModuleData *data, const char *field)
{
	const RawModuleData *raw = dynamic_cast<const RawModuleData *>(data);
	if (!raw)
	{
		return std::string();
	}
	std::string found;
	for (const RawModuleData::Line &l : raw->lines())
	{
		std::string text = l.text;
		size_t cut = text.find(';');
		const size_t slashes = text.find("//");
		if (slashes != std::string::npos && (cut == std::string::npos || slashes < cut))
		{
			cut = slashes;
		}
		if (cut != std::string::npos)
		{
			text.erase(cut);
		}
		const size_t eq = text.find('=');
		if (eq != std::string::npos && sameNoCase(trimmed(text.substr(0, eq)), field))
		{
			found = trimmed(text.substr(eq + 1)); // the last line of a field wins (the parse writes the field again)
		}
	}
	return found;
}

[[noreturn]] void badValue(const char *field, const std::string &value)
{
	throw std::runtime_error(std::string("WorkerAIUpdate: ") + field + " = " + value + " is not a value its parser accepts");
}

// INI::parseInt (RW 0x42EC5E): sscanf %d
int parseIntField(const ModuleData *data, const char *field, int def)
{
	const std::string v = rawField(data, field);
	if (v.empty())
	{
		return def;
	}
	int out = 0;
	if (std::sscanf(v.c_str(), "%d", &out) != 1)
	{
		badValue(field, v);
	}
	return out;
}

// INI::parseReal (RW 0x42ED00): sscanf %f
float parseRealField(const ModuleData *data, const char *field, float def)
{
	const std::string v = rawField(data, field);
	if (v.empty())
	{
		return def;
	}
	char *end = nullptr;
	const float out = strtofPortable(v.c_str(), &end);
	if (end == v.c_str())
	{
		badValue(field, v);
	}
	return out;
}

// INI::parseBool (RW 0x42E558): Yes / No
bool parseBoolField(const ModuleData *data, const char *field, bool def)
{
	const std::string v = rawField(data, field);
	if (v.empty())
	{
		return def;
	}
	const std::string token = v.substr(0, v.find_first_of(" \t"));
	if (sameNoCase(token, "yes"))
	{
		return true;
	}
	if (sameNoCase(token, "no"))
	{
		return false;
	}
	badValue(field, v);
}

// INI::parseDurationUnsignedInt (RW 0x73A429): ceil(ms * 0.005f) on the x87
unsigned parseDurationField(const ModuleData *data, const char *field)
{
	const std::string v = rawField(data, field);
	if (v.empty())
	{
		return 0;
	}
	unsigned ms = 0;
	if (std::sscanf(v.c_str(), "%u", &ms) != 1)
	{
		badValue(field, v);
	}
	return NumericState::ceilScaled(ms, LOGICFRAMES_PER_MSEC_REAL);
}

WorkerAIUpdate *workerOf(const AIState &s)
{
	return dynamic_cast<WorkerAIUpdate *>(&s.ai());
}

int modelCondition(const char *name)
{
	return CombatNames::modelCondition(name);
}

// ---- the supply-truck machine's conditions (RW 0x8AA363 .. 0x8AA472) ----------------------------------------------------------------
bool condForcedWanting(const AIState &s, std::uintptr_t) // RW 0x8AA363 isForcedIntoWantingState
{
	const SupplyTruckAIInterface *sup = static_cast<const AIUpdateInterface &>(s.ai()).getSupplyTruckAIInterface();
	return sup && sup->isForcedIntoWantingState();
}
bool condForcedBusy(const AIState &s, std::uintptr_t) // RW 0x8AA393 isForcedIntoBusyState
{
	const SupplyTruckAIInterface *sup = static_cast<const AIUpdateInterface &>(s.ai()).getSupplyTruckAIInterface();
	return sup && sup->isForcedIntoBusyState();
}
bool ownerDocking(const AIState &s, std::uintptr_t) // RW 0x8AA3C3
{
	return s.ai().aiStateType() == (unsigned)AI_DOCK;
}
bool ownerHarvesting(const AIState &s, std::uintptr_t) // RW 0x8AA3E6
{
	return s.ai().aiStateType() == (unsigned)AI_HARVEST;
}
bool ownerIdle(const AIState &s, std::uintptr_t) // RW 0x8AA409
{
	return s.ai().isIdle();
}
bool ownerAvailableForSupplying(const AIState &s, std::uintptr_t) // RW 0x8AA42E
{
	const SupplyTruckAIInterface *sup = static_cast<const AIUpdateInterface &>(s.ai()).getSupplyTruckAIInterface();
	return sup && sup->isAvailableForSupplying() && s.ai().isIdle();
}
bool ownerNotDockingOrIdle(const AIState &s, std::uintptr_t) // RW 0x8AA472
{
	const AIUpdateInterface &ai = s.ai();
	return !ai.isIdle() && ai.aiStateType() != (unsigned)AI_DOCK && ai.aiStateType() != (unsigned)AI_HARVEST;
}

// ---- the supply-truck machine's states ----------------------------------------------------------------------------------------------
// BUSY (RW 0x8AA29B): onEnter RW 0x8AA2B8
class SupplyTruckBusyState : public AIState
{
public:
	explicit SupplyTruckBusyState(AIStateMachine &m) : AIState(m, "SupplyTruckBusyState") {}
	StateReturnType onEnter() override
	{
		if (SupplyTruckAIInterface *sup = ai().getSupplyTruckAIInterface())
		{
			sup->setForceBusyState(false);
		}
		return STATE_CONTINUE;
	}
	StateReturnType update() override { return STATE_CONTINUE; }
	void xfer(Xfer &x) override { xferNoFields(x); } // RW 0x9F3A3C
};

// IDLE (RW 0x8AA2E4): onEnter RW 0x8AA301
class SupplyTruckIdleState : public AIState
{
public:
	explicit SupplyTruckIdleState(AIStateMachine &m) : AIState(m, "SupplyTruckIdleState") {}
	StateReturnType onEnter() override
	{
		if (WorkerAIInterface *w = ai().getWorkerAIInterface()) // AI vslot 0x190
		{
			w->exitingSupplyTruckState();
		}
		return STATE_CONTINUE;
	}
	StateReturnType update() override { return STATE_CONTINUE; }
	void xfer(Xfer &x) override { xferNoFields(x); }
};

// WANTING (RW 0x8A9FEB): update RW 0x8AA94B
class SupplyTruckWantsToPickUpOrDeliverBoxesState : public AIState
{
public:
	explicit SupplyTruckWantsToPickUpOrDeliverBoxesState(AIStateMachine &m) : AIState(m, "SupplyTruckWantsToPickUpOrDeliverBoxesState") {}
	StateReturnType update() override;
	void xfer(Xfer &x) override { xferNoFields(x); }
};

StateReturnType SupplyTruckWantsToPickUpOrDeliverBoxesState::update()
{
	Object &obj = owner();
	AIUpdateInterface &a = ai();
	Player *player = obj.getControllingPlayer();
	SupplyTruckAIInterface *sup = a.getSupplyTruckAIInterface();
	if (!player || !sup || !sup->isAvailableForSupplying())
	{
		return STATE_FAILURE; // RW 0x8AA96F .. 0x8AA992 (Player + 0x304 is always made)
	}
	ResourceGatheringManager &rgm = player->resourceGatheringManager();
	Object *dock = nullptr;
	if (sup->getNumberBoxes() >= 1)
	{
		dock = rgm.findBestSupplyCenter(&obj); // RW 0x8FABC8
		if (!dock)
		{
			return STATE_CONTINUE;
		}
	}
	else
	{
		dock = rgm.findBestSupplyWarehouse(&obj); // RW 0x8FAA51
		if (!dock)
		{
			if (!sup->isHarvestTrees())
			{
				return STATE_CONTINUE;
			}
			Coord3D pos;
			if (rgm.findHarvestPosition(&obj, pos)) // RW 0x8FA834
			{
				a.aiHarvest(pos, CMD_FROM_AI); // RW 0x771655(pos, 2)
				return STATE_SUCCESS;
			}
			return STATE_CONTINUE;
		}
	}
	a.aiDock(dock, CMD_FROM_AI); // RW 0x66C609(dock, 2)
	return STATE_SUCCESS;
}

// REGROUPING (RW 0x8AA00E): onEnter RW 0x8AAA01
class SupplyTruckRegroupingState : public AIState
{
public:
	explicit SupplyTruckRegroupingState(AIStateMachine &m) : AIState(m, "RegroupingState") {}
	StateReturnType onEnter() override;
	StateReturnType update() override { return STATE_CONTINUE; }
	void xfer(Xfer &x) override { xferNoFields(x); }
};

// RW 0x6ABBB7: the player's object of the KindOf closest to `obj` in 2D (x87 dy^2 + dx^2, compared wide against the stored float best, strictly smaller wins)
Object *closestOwnedOfKind(Object &obj, const Player &player, const char *kind)
{
	const int bit = ObjectTemplateInfoBuilder::kindOfIndex(kind);
	if (bit < 0)
	{
		throw std::logic_error(std::string("RegroupingState: no KindOf ") + kind);
	}
	Object *best = nullptr;
	float bestD = FLT_MAX; // RW 0x6AA355
	const Coord3D &p = *obj.getPosition();
	for (Object *o = obj.logic().getFirstObject(); o; o = o->getNextObject()) // the player's objects (RW 0x6ABABD): the logic list in order (S-482)
	{
		if (o->getControllingPlayer() != &player || !o->isKindOf((unsigned)bit))
		{
			continue;
		}
		const Coord3D &c = *o->getPosition();
		const double dx = SimMath::pc24SubW((double)c.x, (double)p.x), dy = SimMath::pc24SubW((double)c.y, (double)p.y);
		const double d = SimMath::pc24AddW(SimMath::pc24MulW(dy, dy), SimMath::pc24MulW(dx, dx)); // RW 0x66137C
		if (d < (double)bestD)
		{
			best = o;
			bestD = SimMath::fstpDword(d);
		}
	}
	return best;
}

StateReturnType SupplyTruckRegroupingState::onEnter()
{
	Object &obj = owner();
	AIUpdateInterface &a = ai();
	Player *player = obj.getControllingPlayer();
	if (!player)
	{
		return STATE_FAILURE;
	}
	a.mover().ignoreObstacle(PATHFIND_INVALID_ID); // RW 0x66831A(0)
	SupplyTruckAIInterface *sup = a.getSupplyTruckAIInterface();
	if (!sup)
	{
		return STATE_FAILURE;
	}
	sup->setForceWantingState(sup->getNumberBoxes() > 0); // RW 0x8AAA5B .. 0x8AAA6D
	Object *home = closestOwnedOfKind(obj, *player, "SUPPLY_GATHERING_CENTER");
	if (!home)
	{
		home = closestOwnedOfKind(obj, *player, "COMMANDCENTER");
	}
	if (!home)
	{
		home = closestOwnedOfKind(obj, *player, "STRUCTURE");
	}
	if (!home)
	{
		return STATE_FAILURE;
	}
	FindPositionOptions options;
	options.minRadius = 0.0f;
	options.maxRadius = 100.0f; // RW 0xBD88D8
	options.startAngle = RANDOM_START_ANGLE;
	options.maxZDelta = 1e10f; // RW 0xBF7328
	Coord3D dest;
	if (!FindPosition::findPositionAround(obj.logic(), *home->getPosition(), options, dest)) // RW 0x68592D
	{
		return STATE_FAILURE;
	}
	a.aiMoveToPosition(dest, CMD_FROM_AI); // RW 0x66C4CA(dest, 2)
	return STATE_CONTINUE;
}

// DOCKING (RW 0x8AA031) and HARVESTING (RW 0x8AA054): onEnter RW 0x8AA33B
class SupplyTruckDockingState : public AIState
{
public:
	SupplyTruckDockingState(AIStateMachine &m, const char *name) : AIState(m, name) {}
	StateReturnType onEnter() override
	{
		SupplyTruckAIInterface *sup = ai().getSupplyTruckAIInterface();
		if (!sup)
		{
			return STATE_FAILURE;
		}
		sup->setForceWantingState(false); // after the first dock, the normal path takes over
		return STATE_CONTINUE;
	}
	StateReturnType update() override { return STATE_CONTINUE; }
	void xfer(Xfer &x) override { xferNoFields(x); }
};

// ---- the worker machine (RW 0x8AD46E) -----------------------------------------------------------------------------------------------
// RW 0x8AD085
bool supplyTruckSubMachineWantsToEnter(const AIState &s, std::uintptr_t)
{
	const AIUpdateInterface &ai = s.ai();
	const SupplyTruckAIInterface *sup = ai.getSupplyTruckAIInterface();
	const unsigned t = ai.aiStateType();
	return (sup && sup->isForcedIntoWantingState()) || t == (unsigned)AI_DOCK || t == (unsigned)AI_HARVEST;
}
// RW 0x8AD0CB
bool supplyTruckSubMachineReadyToLeave(const AIState &s, std::uintptr_t)
{
	const WorkerAIUpdate *w = workerOf(s);
	return w && !supplyTruckSubMachineWantsToEnter(s, 0) && w->isSupplyTruckBrainActiveAndBusy();
}

// both states share the vtable RW 0xC6AB28: onEnter / update continue, onExit nothing (ZH resets the other brain)
class WorkerActState : public AIState
{
public:
	WorkerActState(AIStateMachine &m, const char *name) : AIState(m, name) {}
	StateReturnType update() override { return STATE_CONTINUE; }
	void xfer(Xfer &x) override { xferNoFields(x); }
};
} // namespace

std::string WorkerAIUpdate::stopLine()
{
	return "[S-5203] WorkerAIUpdate's supply half (lane LUMBER1, RW 0x8ADAA0): the AI byte + 0x3CC that ends the update early is not part of the port (S-583); "
		   "the mine clearing's box drop (RW 0x8AE798's tail) is tested when the command is accepted (retail: after it runs); SuppliesDepletedVoice and the "
		   "drawable's supply status are client events";
}

WorkerAIUpdate::WorkerAIUpdate(Thing *thing, const ModuleData *data)
	: DozerAIUpdate(thing, data)
{
	m_isWorker = true;
	m_maxBoxes = parseIntField(data, "MaxBoxes", 0);
	m_centerDelay = parseDurationField(data, "SupplyCenterActionDelay");
	m_warehouseDelay = parseDurationField(data, "SupplyWarehouseActionDelay");
	m_warehouseScanDistance = parseRealField(data, "SupplyWarehouseScanDistance", 0.0f);
	m_harvestTrees = parseBoolField(data, "HarvestTrees", false);
	m_harvestActivationRange = parseRealField(data, "HarvestActivationRange", 0.0f);
	m_harvestPreparationTime = parseDurationField(data, "HarvestPreparationTime");
	m_harvestActionTime = parseDurationField(data, "HarvestActionTime");
	if (Object *obj = getObject())
	{
		obj->logic().noteStop(stopLine());
	}
}

WorkerAIUpdate::~WorkerAIUpdate()
{
	// RW 0x8AD122 deletes the machines (+ 0x588, + 0x58C, + 0x584): the supply-truck and worker machines go without their states' exits (StateMachine
	// RW 0x8DB944 after halt-less deletion keeps nothing of the object: the states hold nothing)
	if (m_supplyMachine)
	{
		m_supplyMachine->haltWithoutExit();
	}
	if (m_workerMachine)
	{
		m_workerMachine->haltWithoutExit();
	}
}

void WorkerAIUpdate::registerClass(ModuleFactory &modules)
{
	modules.bindModuleProc("WorkerAIUpdate", MODULETYPE_BEHAVIOR, [](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &info) -> std::unique_ptr<Module> {
		if (!thing->asObject() || !thing->asObject()->logic().aiWorld())
		{
			return makeUnportedModule(thing, data, info.name, info.type, info.interfaceMask);
		}
		return std::make_unique<WorkerAIUpdate>(thing, data);
	});
}

std::unique_ptr<AIStateMachine> WorkerAIUpdate::makeWorkerMachine()
{
	std::unique_ptr<AIStateMachine> m = std::make_unique<AIStateMachine>(*this, "WorkerStateMachine");
	m->defineState(AS_DOZER, std::make_unique<WorkerActState>(*m, "ActAsDozerState"), INVALID_STATE_ID, INVALID_STATE_ID,
		{ AIStateCondition{ supplyTruckSubMachineWantsToEnter, AS_SUPPLY_TRUCK, 0 } }); // RW 0xC6AF28
	m->defineState(AS_SUPPLY_TRUCK, std::make_unique<WorkerActState>(*m, "ActAsSupplyTruckState"), INVALID_STATE_ID, INVALID_STATE_ID,
		{ AIStateCondition{ supplyTruckSubMachineReadyToLeave, AS_DOZER, 0 } }); // RW 0xC6AF10
	return m;
}

// RW 0x8AA6E6
std::unique_ptr<AIStateMachine> WorkerAIUpdate::makeSupplyTruckMachine()
{
	std::unique_ptr<AIStateMachine> m = std::make_unique<AIStateMachine>(*this, "SupplyTruckStateMachine");
	const std::vector<AIStateCondition> busy = { { ownerIdle, ST_IDLE, 0 }, { ownerDocking, ST_DOCKING, 0 }, { ownerHarvesting, ST_HARVESTING, 0 } }; // RW 0xC69D80
	const std::vector<AIStateCondition> idle = { { condForcedBusy, ST_BUSY, 0 }, { condForcedWanting, ST_WANTING, 0 }, { ownerDocking, ST_DOCKING, 0 },
		{ ownerHarvesting, ST_HARVESTING, 0 }, { ownerNotDockingOrIdle, ST_BUSY, 0 } }; // RW 0xC69DE0
	const std::vector<AIStateCondition> wanting = { { ownerDocking, ST_DOCKING, 0 }, { ownerHarvesting, ST_HARVESTING, 0 }, { ownerNotDockingOrIdle, ST_BUSY, 0 } }; // RW 0xC69DB0
	const std::vector<AIStateCondition> docking = { { condForcedBusy, ST_BUSY, 0 }, { ownerAvailableForSupplying, ST_WANTING, 0 },
		{ ownerNotDockingOrIdle, ST_BUSY, 0 } }; // RW 0xC69D50
	// order matters: the first state is the default state
	m->defineState(ST_BUSY, std::make_unique<SupplyTruckBusyState>(*m), ST_BUSY, ST_BUSY, busy);
	m->defineState(ST_IDLE, std::make_unique<SupplyTruckIdleState>(*m), ST_BUSY, ST_BUSY, idle);
	m->defineState(ST_WANTING, std::make_unique<SupplyTruckWantsToPickUpOrDeliverBoxesState>(*m), ST_BUSY, ST_REGROUPING, wanting);
	m->defineState(ST_REGROUPING, std::make_unique<SupplyTruckRegroupingState>(*m), ST_WANTING, ST_BUSY, busy);
	m->defineState(ST_DOCKING, std::make_unique<SupplyTruckDockingState>(*m, "DockingState"), ST_BUSY, ST_BUSY, docking);
	m->defineState(ST_HARVESTING, std::make_unique<SupplyTruckDockingState>(*m, "HarvestingState"), ST_BUSY, ST_BUSY, docking);
	return m;
}

// RW 0x8AD5CA
void WorkerAIUpdate::createMachines()
{
	if (m_workerMachine)
	{
		return;
	}
	m_workerMachine = makeWorkerMachine();
	if (m_primaryState < 0)
	{
		primarySetState(PRIMARY_IDLE); // the dozer machine (RW 0x88C06B) and its default state
	}
	if (!m_supplyMachine)
	{
		m_supplyMachine = makeSupplyTruckMachine();
		m_supplyMachine->initDefaultState();
	}
	m_workerMachine->initDefaultState(); // last: its conditions read the other two
}

bool WorkerAIUpdate::isSupplyTruckBrainActiveAndBusy() const
{
	return m_workerMachine && m_supplyMachine && m_workerMachine->currentStateId() == (unsigned)AS_SUPPLY_TRUCK &&
	       m_supplyMachine->currentStateId() == (unsigned)ST_BUSY;
}

// RW 0x8AE020
UpdateSleepTime WorkerAIUpdate::update()
{
	createMachines();
	AIUpdateInterface::update(); // RW 0x66E58F
	if (getObject()->isEffectivelyDead())
	{
		return UPDATE_SLEEP_NONE; // (and the AI byte + 0x3CC, S-583)
	}
	m_workerMachine->updateStateMachine();
	if (m_workerMachine->currentStateId() == (unsigned)AS_DOZER)
	{
		dozerMachineStep(); // the repair check and the dozer machine
	}
	else
	{
		m_supplyMachine->updateStateMachine(); // no mine-clearing weapon set flag (ZH's)
	}
	return UPDATE_SLEEP_NONE;
}

// RW 0x8AE461
void WorkerAIUpdate::exitingSupplyTruckState()
{
	if (m_workerMachine && m_workerMachine->currentStateId() == (unsigned)AS_SUPPLY_TRUCK)
	{
		aiIdle(CMD_FROM_AI);                 // RW 0x5E821A(2)
		m_workerMachine->setState(AS_DOZER); // vslot 0x20
	}
}

// Object RW 0x690E4E: CARRYING (85) while a box is held, a change notified (RW 0x68B53C); the drawable's supply status is the client's
void WorkerAIUpdate::updateSupplyStatus()
{
	const int carrying = modelCondition("CARRYING");
	Object *obj = getObject();
	const bool want = m_numberBoxes >= 1;
	if (obj->testModelCondition(carrying) != want)
	{
		obj->setModelConditionState(carrying, want);
	}
}

// RW 0x8ACFBC
bool WorkerAIUpdate::loseOneBox()
{
	if (m_numberBoxes == 0)
	{
		return false;
	}
	--m_numberBoxes;
	updateSupplyStatus();
	return true;
}

// RW 0x8AD334
bool WorkerAIUpdate::gainOneBox(int remainingStock)
{
	if (m_maxBoxes <= m_numberBoxes)
	{
		return false;
	}
	++m_numberBoxes;
	if (remainingStock == 0)
	{
		// RW 0x8AD383 .. 0x8AD443: the best warehouse (its search erases the ids of missing warehouses) decides whether SuppliesDepletedVoice plays: client audio
		if (Player *p = getObject()->getControllingPlayer())
		{
			p->resourceGatheringManager().findBestSupplyWarehouse(getObject());
		}
	}
	updateSupplyStatus();
	return true;
}

// RW 0x8ACCED
bool WorkerAIUpdate::isCurrentlyFerryingSupplies() const
{
	const int s = supplyTruckState();
	return s == ST_WANTING || s == ST_DOCKING;
}

// RW 0x8ACDEB
float WorkerAIUpdate::getWarehouseScanDistance() const
{
	const Player *p = getObject()->getControllingPlayer();
	if (p && p->getPlayerType() == PLAYER_COMPUTER)
	{
		return NumericState::fstpDword(NumericState::pc24AddW((double)m_warehouseScanDistance, (double)m_warehouseScanDistance)); // x87 fadd st, st (exact)
	}
	return m_warehouseScanDistance;
}

// RW 0x8ACD36
unsigned WorkerAIUpdate::getActionDelayForDock(const Object *dock) const
{
	if (!dock)
	{
		return 0;
	}
	if (dock->findModule("SupplyWarehouseDockUpdate"))
	{
		return m_warehouseDelay;
	}
	if (dock->findModule("SupplyCenterDockUpdate"))
	{
		return m_centerDelay;
	}
	return 0;
}

// RW 0x8AE798 before the gate: ACTIVELY_CONSTRUCTING (DozerAIUpdate's setWorking, after the gate in the port), HARVEST_PREPARATION and HARVEST_ACTION cleared
void WorkerAIUpdate::commandArriving()
{
	Object *obj = getObject();
	for (const char *name : { "HARVEST_PREPARATION", "HARVEST_ACTION" })
	{
		const int bit = modelCondition(name);
		if (obj->testModelCondition(bit))
		{
			obj->setModelConditionState(bit, false);
		}
	}
}

// RW 0x8AE798 after the gate
void WorkerAIUpdate::commandAccepted(CommandSourceType source, int command, Object *target)
{
	createMachines();
	DozerAIUpdate::commandAccepted(source, command, target);
	// the tail: a worker clearing mines (RW 0x664453: IS_ATTACKING and a WEAPON_ANTI_MINE current weapon) drops its boxes (no CARRYING change)
	if (m_numberBoxes > 0)
	{
		Object *obj = getObject();
		static const int attacking = ObjectTemplateInfoBuilder::objectStatusIndex("IS_ATTACKING");
		const ObjectWeapons *w = obj->getWeapons();
		const Weapon *current = w ? w->currentWeapon() : nullptr;
		if (attacking >= 0 && obj->testStatus((unsigned)attacking) && current && (current->getTemplate()->m_antiMask & WEAPON_ANTI_MINE))
		{
			m_numberBoxes = 0;
		}
	}
}

// RW 0x8ACE75
void WorkerAIUpdate::privateHarvest(const Coord3D &pos, CommandSourceType source)
{
	AIUpdateInterface::privateHarvest(pos, source);
	if (source == CMD_FROM_PLAYER)
	{
		m_harvestPosition = pos;
		m_hasHarvestPosition = true;
	}
}

// RW 0x8ACE4A
void WorkerAIUpdate::privateDock(Object *dock, CommandSourceType source)
{
	AIUpdateInterface::privateDock(dock, source);
	if (source == CMD_FROM_PLAYER && dock)
	{
		m_preferredDock = dock->getID();
	}
}

void WorkerAIUpdate::crc(StateHasher &h) const
{
	DozerAIUpdate::crc(h);
	// the supply half is hashed once it is engaged (a box, a force flag, a preferred dock, a harvest position or tree, the supply-truck mode): a builder that
	// never harvests (GettingBuiltBehavior's workers) keeps the hash it had before lane LUMBER1
	const bool engaged = m_numberBoxes != 0 || m_forceWanting || m_forceBusy || m_unknown581 || m_preferredDock != INVALID_ID || m_hasHarvestPosition ||
	                     m_currentTree != 0 || (m_workerMachine && m_workerMachine->currentStateId() != (unsigned)AS_DOZER);
	if (!engaged)
	{
		return;
	}
	h.addU32(0x5A1);
	h.addI32(m_numberBoxes);
	h.addU32(m_preferredDock);
	h.addFloat(m_harvestPosition.x);
	h.addFloat(m_harvestPosition.y);
	h.addFloat(m_harvestPosition.z);
	h.addBool(m_hasHarvestPosition);
	h.addI32(m_currentTree);
	h.addBool(m_forceWanting);
	h.addBool(m_unknown581);
	h.addBool(m_forceBusy);
	h.addBool(m_workerMachine != nullptr);
	if (m_workerMachine)
	{
		m_workerMachine->crc(h);
	}
	h.addBool(m_supplyMachine != nullptr);
	if (m_supplyMachine)
	{
		m_supplyMachine->crc(h);
	}
}

void WorkerAIUpdate::xfer(Xfer &x)
{
	DozerAIUpdate::xfer(x); // the module list's part is the dozer's (the AI state is xferAIStateBody's, obfme:AIWorld)
}

// the supply half of RW 0x8AD8D0 in its order: the supply-truck machine, the preferred dock, the harvest position and flag, the boxes, the three bytes
// (+ 0x580, + 0x582, + 0x581), the worker machine, the current tree (the building sound handle is DozerAIUpdate's)
void WorkerAIUpdate::xferSupplyBody(Xfer &x)
{
	XferVersion v{ 1, 1 };
	x.xferVersion(v);
	const bool loading = x.isLoading();
	bool hasMachines = m_workerMachine != nullptr && m_supplyMachine != nullptr;
	x.xferBool(&hasMachines);
	std::unique_ptr<AIStateMachine> supply, worker; // made and loaded complete, then attached
	if (hasMachines)
	{
		if (loading)
		{
			supply = makeSupplyTruckMachine();
			supply->xfer(x);
		}
		else
		{
			m_supplyMachine->xfer(x);
		}
	}
	AIXfer::objectId(x, m_preferredDock, "worker ai: the preferred dock");
	AIXfer::coord(x, m_harvestPosition);
	x.xferBool(&m_hasHarvestPosition);
	x.xferInt(&m_numberBoxes);
	x.xferBool(&m_forceWanting);
	x.xferBool(&m_forceBusy);
	x.xferBool(&m_unknown581);
	if (hasMachines)
	{
		if (loading)
		{
			worker = makeWorkerMachine();
			worker->xfer(x);
		}
		else
		{
			m_workerMachine->xfer(x);
		}
	}
	x.xferInt(&m_currentTree);
	if (loading)
	{
		AIXfer::inRange(m_numberBoxes, 0, m_maxBoxes, "worker ai: the boxes");
		m_supplyMachine = std::move(supply);
		m_workerMachine = std::move(worker);
	}
}
