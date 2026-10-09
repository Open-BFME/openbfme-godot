// OpenBFME. GPL-3.0.
// See GameLogic/Module/DozerAIUpdate.h for the sources of every rule.

#include "GameLogic/Module/DozerAIUpdate.h"
#include "GameLogic/SkirmishAI/SkirmishAIManager.h"

#include "Common/Audio/AudioRequests.h"
#include "Common/NumericState.h"
#include "Common/Player.h"
#include "Common/StateHash.h"
#include "Common/Thing/KindOfTokens.h"
#include "Common/Thing/ModuleFactory.h"
#include "Common/Thing/RawModuleData.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/BuildPlacement.h"
#include "GameLogic/Combat/CombatQueries.h"
#include "GameLogic/Construction.h"
#include "GameLogic/FindPositionAround.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/Module/ActiveBody.h"
#include "GameLogic/Module/ConstructionModules.h"
#include "GameLogic/Module/UnportedModule.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/UnitSpecificSound.h"
#include "Common/BuildAssistant.h"
#include "Libraries/WWVegas/WWMath/quat.h"
#include "Common/INI/HostRealText.h"

#include <cstdlib>

namespace
{
constexpr int kExitWithSuccess = 9998;  // RW 0x270E
constexpr int kExitWithFailure = 9999;  // RW 0x270F
constexpr float kEndDockDistance = 50.0f;   // RW 0xBD88C4
constexpr float kFindMaxRadius = 100.0f;    // RW 0xBD88D8
constexpr float kGroundMaxZDelta = 10.0f;   // RW 0xBD83D8
constexpr float kActionSlop = 15.0f;        // RW 0xBDC6CC
constexpr float kMinActionTolerance = 70.0f; // RW 0xC61BC4

// the value text of `Field = value` in the raw lines of a module body ("" when absent); comments removed
std::string rawFieldValue(const ModuleData *data, const char *field)
{
	const RawModuleData *raw = dynamic_cast<const RawModuleData *>(data);
	if (!raw)
	{
		return std::string();
	}
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
		if (eq == std::string::npos)
		{
			continue;
		}
		auto trim = [](std::string s) {
			const size_t b = s.find_first_not_of(" \t\r\n");
			const size_t e = s.find_last_not_of(" \t\r\n");
			return b == std::string::npos ? std::string() : s.substr(b, e - b + 1);
		};
		if (trim(text.substr(0, eq)) == field)
		{
			return trim(text.substr(eq + 1));
		}
	}
	return std::string();
}

float scanFloat(const std::string &s)
{
	return strtofPortable(s.c_str(), nullptr); // retail: sscanf "%f" (lane WIN-1)
}

// RW 0x68BEED: the object's current locomotor has the AIR surface (template + 0x14 bit 3)
bool usesAirborneLocomotor(const Object &obj)
{
	const AIUpdateInterface *ai = obj.getAIUpdateInterface();
	return ai && ai->curLocomotor() && (ai->curLocomotor()->getTemplate().m_surfaces & (unsigned)LOCOMOTORSURFACE_AIR);
}

// RW 0x6CA525 (Object, the dozer): (sqrt(dx^2 + dy^2) - its bounding circle radius), 0 when negative, squared (x87 PC24 to the fst dword, then mulss)
float edgeDistanceSquaredTo(const Object &obj, const Coord3D &pos)
{
	const Coord3D &a = *obj.getPosition();
	const double dx = SimMath::pc24SubW((double)a.x, (double)pos.x);
	const double dy = SimMath::pc24SubW((double)a.y, (double)pos.y);
	const double sum = SimMath::pc24AddW(SimMath::pc24MulW(dy, dy), SimMath::pc24MulW(dx, dx));
	const float v = SimMath::fstpDword(SimMath::pc24SubW(SimMath::sqrtPC24(sum), (double)CombatQueries::boundingCircleRadius(obj)));
	return 0.0f > v ? 0.0f : SimMath::sseMul(v, v);
}

// RW 0x405406 Coord2D::toAngle: acos of x / length clamped to [-1, 1] (SSE), negated when y < 0; 0 for a zero vector
float toAngle(float x, float y)
{
	const float len = SimMath::length2d(x, y); // RW 0x4032DD
	if (len == 0.0f)
	{
		return 0.0f;
	}
	float c = SimMath::sseDiv(x, len);
	if (-1.0f > c)
	{
		c = -1.0f;
	}
	else if (c > 1.0f)
	{
		c = 1.0f;
	}
	const double a = SimMath::acosDet((double)c); // MSVCR71 acos (RW 0x42F4F0 -> 0xA3D60E; S-167)
	return 0.0f > y ? (float)-a : (float)a;
}

bool sameOwnerUnfinished(const Object &dozer, const Object &structure)
{
	return !structure.isDestroyed() && structure.isUnderConstruction() && structure.getControllingPlayer() == dozer.getControllingPlayer();
}

// the stand-in of the ActionManager's canRepairObject (RW 0x82D7F2, not ported: S-1282): the dozer's owner's finished, living structure
bool canRepair(const Object &dozer, const Object *structure)
{
	return structure && !structure->isDestroyed() && !structure->isEffectivelyDead() && !structure->isUnderConstruction() &&
	       structure->getControllingPlayer() == dozer.getControllingPlayer();
}
} // namespace

DozerAIUpdate::DozerAIUpdate(Thing *thing, const ModuleData *data)
	: AIUpdateInterface(thing, data)
{
	// the three fields of the class (RW table 0xC61EB0); the module data is raw, so the values are read from its lines (stop S-070: unvalidated here)
	const std::string percent = rawFieldValue(data, "RepairHealthPercentPerSecond");
	if (!percent.empty())
	{
		m_repairPercentPerSecond = NumericState::pc24Mul(scanFloat(percent.substr(0, percent.find('%'))), 0.01f); // parsePercentToReal: value * 0.01f
	}
	const std::string bored = rawFieldValue(data, "BoredTime");
	if (!bored.empty())
	{
		m_boredTime = NumericState::pc24Mul(0.005f, scanFloat(bored)); // parseDurationReal RW 0x73A403
	}
	const std::string range = rawFieldValue(data, "BoredRange");
	if (!range.empty())
	{
		m_boredRange = scanFloat(range);
	}
}

void DozerAIUpdate::registerClass(ModuleFactory &modules)
{
	modules.bindModuleProc("DozerAIUpdate", MODULETYPE_BEHAVIOR, [](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &info) -> std::unique_ptr<Module> {
		// a logic without an AIWorld has nothing to move on: the explicit unported module, as AIUpdateInterface::registerClasses does
		if (!thing->asObject() || !thing->asObject()->logic().aiWorld())
		{
			return makeUnportedModule(thing, data, info.name, info.type, info.interfaceMask);
		}
		return std::make_unique<DozerAIUpdate>(thing, data);
	});
}

void DozerAIUpdate::registerWorkerClass(ModuleFactory &modules)
{
	modules.bindModuleProc("WorkerAIUpdate", MODULETYPE_BEHAVIOR, [](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &info) -> std::unique_ptr<Module> {
		if (!thing->asObject() || !thing->asObject()->logic().aiWorld())
		{
			return makeUnportedModule(thing, data, info.name, info.type, info.interfaceMask);
		}
		auto dozer = std::make_unique<DozerAIUpdate>(thing, data);
		dozer->m_isWorker = true;
		return dozer;
	});
}

DozerAIUpdate::~DozerAIUpdate()
{
	finishBuildingSound(); // RW 0x88D1F6
}

// ---- the dozer interface ------------------------------------------------------------------------------------------------------

bool DozerAIUpdate::isAnyTaskPending() const
{
	// RW 0x88BDEE
	for (int t = 0; t < DOZER_NUM_TASKS; ++t)
	{
		if (isTaskPending(t))
		{
			return true;
		}
	}
	return false;
}

int DozerAIUpdate::getMostRecentCommand() const
{
	// RW 0x88BE47: the pending task with the largest order frame (strictly larger: the first of equal frames wins); -1 when none
	int best = DOZER_TASK_INVALID;
	UnsignedInt bestFrame = 0;
	for (int t = 0; t < DOZER_NUM_TASKS; ++t)
	{
		if (isTaskPending(t) && m_task[t].orderFrame > bestFrame)
		{
			best = t;
			bestFrame = m_task[t].orderFrame;
		}
	}
	return best;
}

const Coord3D *DozerAIUpdate::getDockPoint(int task, int point) const
{
	// RW 0x88BE84
	if (task < 0 || task >= DOZER_NUM_TASKS || point < 0 || point >= DOZER_NUM_DOCK_POINTS || !m_dockPoint[task][point].valid)
	{
		return nullptr;
	}
	return &m_dockPoint[task][point].location;
}

ObjectID DozerAIUpdate::taskTarget() const
{
	if (m_currentTask != DOZER_TASK_INVALID)
	{
		return getTaskTarget(m_currentTask);
	}
	const int recent = getMostRecentCommand();
	return recent == DOZER_TASK_INVALID ? (ObjectID)INVALID_ID : getTaskTarget(recent);
}

bool DozerAIUpdate::isBuilding(const Object &structure) const
{
	return m_working && m_currentTask != DOZER_TASK_INVALID && getTaskTarget(m_currentTask) == structure.getID();
}

void DozerAIUpdate::setWorking(bool on)
{
	// the dozer's model condition ACTIVELY_CONSTRUCTING (RW + 0x114 bit 9: set by the work, sub task 3, cleared by slot 0x40 RW 0x88C93C and aiDoCommand)
	if (m_working != on)
	{
		m_working = on;
		getObject()->setModelConditionState(modelConditionBit("ACTIVELY_CONSTRUCTING"), on);
	}
}

bool DozerAIUpdate::findGoodBuildOrRepairPosition(const Object &dozer, const Object &target, Coord3D &out)
{
	// RW 0x88C2FE
	const Coord3D &tp = *target.getPosition();
	const Coord3D &dp = *dozer.getPosition();
	float vx = SimMath::sseSub(dp.x, tp.x), vy = SimMath::sseSub(dp.y, tp.y), vz = SimMath::sseSub(dp.z, tp.z);
	const float len2 = SimMath::sseAdd(SimMath::sseAdd(SimMath::sseMul(vz, vz), SimMath::sseMul(vy, vy)), SimMath::sseMul(vx, vx));
	if (len2 != 0.0f)
	{
		const float inv = BFME2_Inverse_Sqrt(len2); // RW 0x441C56, the products x87 PC24
		vx = BFME2_Mul24(vx, inv);
		vy = BFME2_Mul24(vy, inv);
		vz = BFME2_Mul24(inv, vz);
	}
	const float d = SimMath::sseMul(CombatQueries::boundingCircleRadius(target), 0.5f); // target + 0xB8 * 0.5 (RW 0xBD869C)
	Coord3D start;
	start.x = SimMath::sseAdd(SimMath::sseMul(d, vx), tp.x);
	start.y = SimMath::sseAdd(SimMath::sseMul(d, vy), tp.y);
	start.z = SimMath::sseAdd(SimMath::sseMul(d, vz), tp.z);
	out = tp;
	if (const AIUpdateInterface *ai = dozer.getAIUpdateInterface())
	{
		if (ai->findNearestLabeledContactPointOnTarget(target, out, start, true)) // RW 0x667C76
		{
			return true;
		}
	}
	FindPositionOptions options;
	options.minRadius = 0.0f;
	options.maxRadius = kFindMaxRadius;
	options.startAngle = RANDOM_START_ANGLE;
	const bool air = usesAirborneLocomotor(dozer);
	options.maxZDelta = air ? 1e10f : kGroundMaxZDelta;
	options.ignoreObject = air ? &target : nullptr;
	options.sourceToPathToDest = &dozer;
	if (FindPosition::findPositionAround(dozer.logic(), start, options, out)) // the result is written over the target's position (RW - 0x28)
	{
		return true;
	}
	out = start; // RW 0x88C494: nothing found: the start position
	return false;
}

void DozerAIUpdate::newTask(int task, Object &target)
{
	// RW 0x88CCFC
	if (task != DOZER_TASK_BUILD && task != DOZER_TASK_REPAIR)
	{
		return; // only BUILD and REPAIR set a task here (RW 0x88CD1F: the other tasks skip to the frame bookkeeping, which needs a target id they do not have)
	}
	if (isTaskPending(task))
	{
		cancelTaskInternal(task);
	}
	Object *self = getObject();
	if (target.isKindOfName("BRIDGE"))
	{
		self->logic().noteStop("[S-1282] dozer: a BRIDGE target picks its nearest bridge tower (RW 0x88CC08 -> 0x8595BA), not ported: the bridge's own position is used");
	}
	Coord3D pos;
	findGoodBuildOrRepairPosition(*self, target, pos);
	if (task == DOZER_TASK_BUILD)
	{
		target.setBuilder(self); // RW 0x88CEC2
	}
	DockPoint &start = m_dockPoint[task][DOZER_DOCK_POINT_START];
	DockPoint &action = m_dockPoint[task][DOZER_DOCK_POINT_ACTION];
	DockPoint &end = m_dockPoint[task][DOZER_DOCK_POINT_END];
	start.location = pos;
	start.valid = true;
	action.location = pos;
	action.valid = true;
	// RW 0x88CEF2: (pos - target) in x / y, normalised (Coord3D::normalize RW 0x403175: 1 / length, SSE), END = pos + v * 50
	float vx = SimMath::sseSub(pos.x, target.getPosition()->x), vy = SimMath::sseSub(pos.y, target.getPosition()->y), vz = 0.0f;
	const float len = (float)SimMath::length3d(vx, vy, vz);
	if (len != 0.0f)
	{
		const float inv = SimMath::sseDiv(1.0f, len);
		vx = SimMath::sseMul(vx, inv);
		vy = SimMath::sseMul(vy, inv);
		vz = SimMath::sseMul(vz, inv);
	}
	end.location.x = SimMath::sseAdd(SimMath::sseMul(vx, kEndDockDistance), pos.x);
	end.location.y = SimMath::sseAdd(SimMath::sseMul(vy, kEndDockDistance), pos.y);
	end.location.z = SimMath::sseAdd(SimMath::sseMul(vz, kEndDockDistance), pos.z);
	end.valid = true;
	m_task[task].target = target.getID();
	m_task[task].orderFrame = self->logic().getFrame();
	m_taskWorkFrames = 0; // review r1: a new task starts its work count from zero (two 75-frame builds reported 75 then 150)
	primaryReset();
}

void DozerAIUpdate::internalTaskComplete(int task)
{
	// RW 0x88BE18: slot 0x40 (RW 0x88C93C: the BUILD / REPAIR tasks clear ACTIVELY_CONSTRUCTING), the task and its dock points cleared
	if (task == DOZER_TASK_BUILD || task == DOZER_TASK_REPAIR)
	{
		setWorking(false);
	}
	if (task < 0 || task >= DOZER_NUM_TASKS)
	{
		return;
	}
	m_task[task] = TaskInfo();
	for (DockPoint &d : m_dockPoint[task])
	{
		d.valid = false;
	}
}

void DozerAIUpdate::internalCancelTask(int task)
{
	// RW 0x88E40A: as internalTaskComplete, then aiIdle(CMD_FROM_AI)
	const bool had = isTaskPending(task);
	internalTaskComplete(task);
	aiIdle(CMD_FROM_AI);
	if (had)
	{
		notifyAIPlayerDozerFree(); // S-890: RW notifies only on completion; the port also frees an AI dozer whose task is cancelled
	}
}

void DozerAIUpdate::cancelTaskInternal(int task)
{
	// RW 0x88E3CE: internalCancelTask, the primary machine reset, the unplaced structure removed (slot 0x6C), the dozer shown again (RW 0x88D6AE(0))
	internalCancelTask(task);
	primaryReset();
	if (m_unplacedStructure != INVALID_ID)
	{
		removeUnplacedStructure();
	}
	showFromStructure(nullptr); // RW 0x88E3FF: 0x88D6AE(0)
}

void DozerAIUpdate::cancelTask()
{
	if (m_currentTask != DOZER_TASK_INVALID)
	{
		cancelTaskInternal(m_currentTask);
	}
	else if (isAnyTaskPending())
	{
		cancelTaskInternal(getMostRecentCommand());
	}
}

void DozerAIUpdate::removeUnplacedStructure()
{
	// RW 0x88CFFE: the price paid back (ftol, deposit), the score's money spent taken back, the object destroyed
	const ObjectID id = m_unplacedStructure;
	m_unplacedStructure = INVALID_ID;
	Object *self = getObject();
	GameLogic &logic = self->logic();
	Object *obj = logic.findObjectByID(id);
	if (!obj || obj->isDestroyed())
	{
		return;
	}
	if (Player *owner = obj->getControllingPlayer())
	{
		const int paid = SimMath::truncToInt32(obj->getBuildCostPaid());
		if (paid > 0)
		{
			owner->depositMoney((std::uint32_t)paid, true);
		}
		owner->getScoreKeeper().addMoneySpentByKind(logic, static_cast<const ThingTemplate *>(obj->getTemplate()), -paid); // RW 0x79DFC2(template, -cost)
	}
	obj->removeFromPlayerCommandPoints();
	logic.destroyObject(obj);
}

bool DozerAIUpdate::placeUnplacedStructure()
{
	// RW 0x88D44F. The port's structure exists since the order (S-304), so the site's legality check (flags 0x15) is not repeated: the structure would overlap
	// itself; what the placement changes on the object is ported: the health set to 1 (RW 0x88D53A: body vslot 0x84 with 1.0 - health)
	const ObjectID id = m_unplacedStructure;
	m_unplacedStructure = INVALID_ID;
	Object *obj = getObject()->logic().findObjectByID(id);
	if (!obj || obj->isDestroyed())
	{
		return true;
	}
	if (ActiveBody *body = dynamic_cast<ActiveBody *>(obj->getBodyModule()))
	{
		body->internalChangeHealth(SimMath::fstpDword(SimMath::pc24SubW(1.0, (double)body->getHealth())));
	}
	return true;
}

void DozerAIUpdate::notifyAIPlayerDozerFree()
{
	// slot 0x7C, RW 0x88BF4E -> the owner's AIPlayer RW 0x8F0660
	getObject()->logic().skirmishAI().dozerIdle(*getObject());
}

namespace
{
const char *const kHiddenStatuses[] = { "UNSELECTABLE", "UNATTACKABLE", "DO_NOT_PICK_ME", "WONT_RIDE_WITH_YOU" }; // RW 0x7485E3(3, 0x3C, 0x4F, 0x63)
}

void DozerAIUpdate::hideInStructure()
{
	// RW 0x88C51B: (TheInGameUI deselects the drawable: the client's) a dozer without a supply interface gets the statuses and leaves the world when it is in it
	if (m_isWorker)
	{
		return;
	}
	Object *self = getObject();
	for (const char *name : kHiddenStatuses)
	{
		self->setStatus((unsigned)ObjectTemplateInfoBuilder::objectStatusIndex(name), true); // RW 0x68D440(mask, 1)
	}
	if (self->isInWorld())
	{
		self->logic().friend_containLeaveWorld(*self); // RW 0x68C18F
	}
}

void DozerAIUpdate::showFromStructure(const Object *built)
{
	// RW 0x88D6AE
	if (m_isWorker)
	{
		return;
	}
	Object *self = getObject();
	GameLogic &logic = self->logic();
	if (built)
	{
		if (!logic.settings().builderMoveLoaded)
		{
			logic.noteStop("[S-1282] dozer: GameData BuilderMoveFromNewStructureDistance was not loaded (RW GlobalData + 0x11E4's default not read): the porter does not walk away");
		}
		const float k = logic.settings().builderMoveFromNewStructureDistance;
		if (k > 0.0f)
		{
			// (dozer - structure) normalised (Coord3D::normalize RW 0x403175), the dozer's position + v * k (SSE), a PLAYER-sourced move (RW 0x88D762 pushes 0)
			const Coord3D &p = *self->getPosition();
			float vx = SimMath::sseSub(p.x, built->getPosition()->x), vy = SimMath::sseSub(p.y, built->getPosition()->y), vz = SimMath::sseSub(p.z, built->getPosition()->z);
			const float len = (float)SimMath::length3d(vx, vy, vz);
			if (len != 0.0f)
			{
				const float inv = SimMath::sseDiv(1.0f, len);
				vx = SimMath::sseMul(vx, inv);
				vy = SimMath::sseMul(vy, inv);
				vz = SimMath::sseMul(vz, inv);
			}
			Coord3D dest;
			dest.x = SimMath::sseAdd(p.x, SimMath::sseMul(k, vx));
			dest.y = SimMath::sseAdd(p.y, SimMath::sseMul(vy, k));
			dest.z = SimMath::sseAdd(p.z, SimMath::sseMul(vz, k));
			aiMoveToPosition(dest, CMD_FROM_PLAYER);
		}
	}
	if (!self->isInWorld())
	{
		logic.friend_containEnterWorld(*self); // RW 0x68E31F
	}
	// (the drawable fades in over BuilderFadeInTime: the client's)
	for (const char *name : kHiddenStatuses)
	{
		self->setStatus((unsigned)ObjectTemplateInfoBuilder::objectStatusIndex(name), false); // RW 0x68D440(mask, 0)
	}
}

// ---- the orders -----------------------------------------------------------------------------------------------------------------

bool DozerAIUpdate::siteIsLegal(const ThingTemplate &what, const Coord3D &pos, float angle, Player &owner)
{
	return BuildPlacement::isLocationLegalToBuild(getObject()->logic(), pos, *what.getFinalOverride(), angle, kConstructLegalFlags, getObject(), &owner) == LBC_OK;
}

Object *DozerAIUpdate::construct(const ThingTemplate &what, const Coord3D &pos, float angle, Player &owner)
{
	// RW 0x88C217: the machine made, nothing is created, paid or changed when the site is not legal
	if (m_primaryState < 0)
	{
		primarySetState(PRIMARY_IDLE);
	}
	if (!siteIsLegal(what, pos, angle, owner))
	{
		return nullptr;
	}
	if (isTaskPending(DOZER_TASK_BUILD))
	{
		cancelTaskInternal(DOZER_TASK_BUILD); // RW 0x88C2C3
	}
	// slot 0x64 (RW 0x88D272): ZH DozerAIUpdate::construct: the foundation made with the builder as producer, the owner pays the price up front (S-304: made at once)
	Object *structure = Construction::constructByDozer(*getObject(), what, pos, angle, owner);
	if (!structure)
	{
		return nullptr;
	}
	m_unplacedStructure = structure->getID(); // + 0xC0
	newTask(DOZER_TASK_BUILD, *structure);    // RW 0x88C2E9
	return structure;
}

bool DozerAIUpdate::resumeConstruction(Object &structure)
{
	// RW 0x8ACEA9 (the ActionManager's canResumeConstructionOf RW 0x82C84C, not ported: S-1282, the owner's unfinished structure stands for it); the command
	// path RW 0x88E527 idles a dozer without a current task first
	if (!sameOwnerUnfinished(*getObject(), structure))
	{
		return false;
	}
	if (m_primaryState < 0)
	{
		primarySetState(PRIMARY_IDLE);
	}
	if (m_currentTask == DOZER_TASK_INVALID)
	{
		aiIdle(CMD_FROM_AI);
	}
	newTask(DOZER_TASK_BUILD, structure);
	return true;
}

bool DozerAIUpdate::repair(Object &structure)
{
	// RW 0x88BD8D: the dozer can repair (slot 0x50) and the ActionManager allows it (RW 0x82D7F2, S-1282), the structure has no other sole healer (RW 0x68B8D3);
	// the command path RW 0x88E552 idles a dozer without a current task first
	Object *self = getObject();
	if (!canRepair(*self, &structure))
	{
		return false;
	}
	const ObjectID healer = self->logic().getFrame() > structure.soleHealingBenefactorExpiry() ? (ObjectID)INVALID_ID : structure.soleHealingBenefactor();
	if (healer != INVALID_ID && healer != self->getID())
	{
		return false;
	}
	if (m_primaryState < 0)
	{
		primarySetState(PRIMARY_IDLE);
	}
	if (m_currentTask == DOZER_TASK_INVALID)
	{
		aiIdle(CMD_FROM_AI);
	}
	newTask(DOZER_TASK_REPAIR, structure);
	return true;
}

void DozerAIUpdate::commandAccepted(CommandSourceType source, int command, Object *target)
{
	(void)target;
	// RW 0x88E44F: ACTIVELY_CONSTRUCTING cleared, the machine made; the default case cancels the current task of a PLAYER command (slot 0x34) and, after the base
	// aiDoCommand (RW 0x66A7EC), resets the primary machine; repair (0x13) / resume (0x14) / move away (0x34) have their own cases (DozerAIUpdate::repair /
	// resumeConstruction; the move away runs as the base command)
	setWorking(false);
	if (m_primaryState < 0)
	{
		primarySetState(PRIMARY_IDLE);
	}
	if (source != CMD_FROM_PLAYER || command == 0x13 || command == 0x14 || command == 0x34)
	{
		return;
	}
	if (m_currentTask != DOZER_TASK_INVALID)
	{
		cancelTaskInternal(m_currentTask);
	}
	primaryReset(); // INFERENCE (S-1282): before the command runs (RW: after it)
}

// ---- the building sound ---------------------------------------------------------------------------------------------------------

void DozerAIUpdate::startBuildingSound(Object &structure, const char *soundField)
{
	// RW startBuildingSound (dozer interface slot 0x5C, RW 0x88C4A3): the structure's UnitSpecificSounds entry (RW 0x674FFE) replaces the sound this dozer kept
	const std::string sound = UnitSpecificSound::rawValue(*structure.getTemplate(), soundField);
	if (!sound.empty())
	{
		AudioApi::postHeldSound(AudioApi::HELD_BUILDING_LOOP, sound, getObject()->getID(), structure.getID());
		m_buildingSoundHolder = getObject()->getID();
	}
}

// RW 0x88BFA9: TheAudio->removeAudioEvent(handle at + 0x24), the handle reset to 1
void DozerAIUpdate::finishBuildingSound()
{
	if (m_buildingSoundHolder != INVALID_ID)
	{
		AudioApi::stopHeldSound(AudioApi::HELD_BUILDING_LOOP, m_buildingSoundHolder);
		m_buildingSoundHolder = INVALID_ID;
	}
}

// ---- the primary machine (RW 0x88C06B; ZH StateMachine semantics) ------------------------------------------------------------------

int DozerAIUpdate::primaryEnter(int id)
{
	if (id == PRIMARY_IDLE)
	{
		// RW 0x88BB0D
		m_idleTimestamp = getObject()->logic().getFrame();
		m_markedIdle = false;
		return STATE_CONTINUE;
	}
	// RW 0x88BBCE: the current task, the action machine reset (its result is not used)
	const int task = id - PRIMARY_BUILD;
	m_currentTask = task;
	actionReset(task);
	return STATE_CONTINUE;
}

void DozerAIUpdate::primaryExit(int id)
{
	if (id == PRIMARY_IDLE)
	{
		// RW 0x88BB1F: TheInGameUI removeIdleWorker when marked
		if (m_markedIdle)
		{
			m_idlePlayer = -1;
			m_markedIdle = false;
		}
		return;
	}
	// RW 0x88BC04: no current task, the building sound finished
	m_currentTask = DOZER_TASK_INVALID;
	finishBuildingSound();
}

int DozerAIUpdate::primarySetState(int id)
{
	// ZH StateMachine::internalSetState (every id of this machine is a state: success / failure lead to IDLE, RW 999999 for IDLE itself is never asked)
	if (m_primaryState >= 0)
	{
		primaryExit(m_primaryState);
	}
	m_primaryState = id;
	const int status = primaryEnter(id);
	if (m_primaryState != id)
	{
		return primaryCheckTransitions(STATE_CONTINUE);
	}
	return primaryCheckTransitions(status);
}

int DozerAIUpdate::primaryCheckTransitions(int status)
{
	// ZH State::friend_checkForTransitions (with its guard of 20 nested checks)
	if (m_transitionDepth >= 20)
	{
		return STATE_FAILURE;
	}
	struct Guard
	{
		int &d;
		explicit Guard(int &v) : d(v) { ++d; }
		~Guard() { --d; }
	} guard(m_transitionDepth);
	if (status == STATE_SUCCESS || status == STATE_FAILURE)
	{
		return primarySetState(PRIMARY_IDLE);
	}
	if (status == STATE_CONTINUE && m_primaryState == PRIMARY_IDLE)
	{
		// RW 0xC61F70: RW 0x88BC61 / 0x88BCAC / 0x88BCF8: the AI is idle and the most recent pending task is BUILD / REPAIR / FORTIFY
		for (int task = DOZER_TASK_BUILD; task < DOZER_NUM_TASKS; ++task)
		{
			if (isIdle() && getMostRecentCommand() == task)
			{
				return primarySetState(PRIMARY_BUILD + task);
			}
		}
	}
	return status;
}

void DozerAIUpdate::primaryReset()
{
	// ZH StateMachine::resetToDefaultState: the current state exits (EXIT_RESET), the machine's goals cleared, the default state entered
	if (m_primaryState >= 0)
	{
		primaryExit(m_primaryState);
	}
	m_primaryState = -1;
	primarySetState(PRIMARY_IDLE);
}

int DozerAIUpdate::primaryUpdateIdle()
{
	// RW 0x88E582
	Object *self = getObject();
	GameLogic &logic = self->logic();
	if (isIdle() && !m_markedIdle && !self->isEffectivelyDead())
	{
		m_idlePlayer = self->getControllingPlayer() ? self->getControllingPlayer()->getPlayerIndex() : -1; // RW 0x88E5D9: the player's index (+ 0x54)
		m_markedIdle = true;                                                                                  // TheInGameUI vslot 0x1A4 addIdleWorker
	}
	if (m_markedIdle && (!isIdle() || self->isEffectivelyDead()))
	{
		m_idlePlayer = -1; // vslot 0x1A8 removeIdleWorker
		m_markedIdle = false;
	}
	if (!isIdle())
	{
		m_idleTimestamp = logic.getFrame();
	}
	// RW 0x88E64C: (frame - timestamp) as a float (fild, + 2^32 when negative) > BoredTime, no task pending, no status 0x3A: the bored wander (RW 0x88E17F, an AI
	// player's dozer looking for something to repair within BoredRange) is not ported (S-1282); the clock restarts as retail's does before the call
	const float idleFor = SimMath::fstpDword(SimMath::fildU32(logic.getFrame() - m_idleTimestamp));
	if (idleFor > m_boredTime && !isAnyTaskPending() && !self->testStatus(0x3A))
	{
		m_idleTimestamp = logic.getFrame();
		if (self->getControllingPlayer() && self->getControllingPlayer()->getPlayerType() == PLAYER_COMPUTER)
		{
			logic.noteStop("[S-1282] dozer: the bored wander of an AI player's idle dozer (RW 0x88E17F) is not ported");
		}
	}
	return STATE_CONTINUE;
}

// ---- the action machine (RW 0x88B9BE) ----------------------------------------------------------------------------------------------

void DozerAIUpdate::actionReset(int t)
{
	// resetToDefaultState: the current state exits (DO_ACTION's exit finishes the building sound, RW 0x88B998), the goals cleared (ZH internalClear), PICK entered
	if (m_action[t].state == ACTION_DO_ACTION)
	{
		finishBuildingSound();
	}
	m_action[t].state = ACTION_NONE;
	m_action[t].goalObject = INVALID_ID;
	m_action[t].goalPosition = Coord3D{ 0.0f, 0.0f, 0.0f };
	actionSetState(t, ACTION_PICK_ACTION_POS);
}

int DozerAIUpdate::actionSetState(int t, int id)
{
	if (m_action[t].state == ACTION_DO_ACTION)
	{
		finishBuildingSound(); // RW 0x88B998
	}
	if (id == kExitWithSuccess || id == kExitWithFailure)
	{
		m_action[t].state = ACTION_NONE; // MACHINE_DONE_STATE_ID
		return id == kExitWithSuccess ? STATE_SUCCESS : STATE_FAILURE;
	}
	m_action[t].state = id;
	int status = STATE_CONTINUE;
	if (id == ACTION_DO_ACTION)
	{
		// RW 0x88B953: the frame recorded; BUILD / REPAIR start at sub task 0
		m_action[t].doActionEnterFrame = getObject()->logic().getFrame();
		if (t == DOZER_TASK_BUILD || t == DOZER_TASK_REPAIR)
		{
			m_buildSubTask = DOZER_SELECT_BUILD_DOCK_LOCATION;
		}
	}
	return actionCheckTransitions(t, status);
}

int DozerAIUpdate::actionCheckTransitions(int t, int status)
{
	if (status == STATE_SUCCESS)
	{
		static const int next[3] = { ACTION_MOVE_TO_ACTION_POS, ACTION_DO_ACTION, kExitWithSuccess };
		return actionSetState(t, next[m_action[t].state]);
	}
	if (status == STATE_FAILURE)
	{
		static const int next[3] = { kExitWithFailure, ACTION_PICK_ACTION_POS, kExitWithFailure };
		return actionSetState(t, next[m_action[t].state]);
	}
	return status;
}

int DozerAIUpdate::actionUpdate(int t)
{
	const int before = m_action[t].state;
	if (before == ACTION_NONE)
	{
		return STATE_FAILURE;
	}
	int status = STATE_CONTINUE;
	if (before == ACTION_PICK_ACTION_POS)
	{
		status = updatePickActionPos(t);
	}
	else if (before == ACTION_MOVE_TO_ACTION_POS)
	{
		status = updateMoveToActionPos(t);
	}
	else
	{
		status = updateDoAction(t);
	}
	if (m_action[t].state == ACTION_NONE)
	{
		return STATE_FAILURE;
	}
	if (m_action[t].state != before)
	{
		status = STATE_CONTINUE;
	}
	return actionCheckTransitions(t, status);
}

int DozerAIUpdate::updatePickActionPos(int t)
{
	// RW 0x88D7D2
	Object *self = getObject();
	GameLogic &logic = self->logic();
	const int task = t;
	Object *target = logic.findObjectByID(getTaskTarget(task));
	if (!target)
	{
		target = logic.findObjectByID(m_unplacedStructure); // slot 0x74
	}
	if (!target)
	{
		m_action[t].goalObject = INVALID_ID;
		cancelTaskInternal(task);
		return STATE_FAILURE;
	}
	Coord3D pos;
	if (const Coord3D *dock = getDockPoint(task, DOZER_DOCK_POINT_START))
	{
		pos = *dock;
	}
	else
	{
		// RW 0x88D877: around the target at its bounding sphere radius, from the angle target -> dozer
		FindPositionOptions options;
		const float r = CombatQueries::boundingSphereRadius(*target);
		options.minRadius = r;
		options.maxRadius = r;
		options.startAngle = toAngle(SimMath::sseSub(self->getPosition()->x, target->getPosition()->x), SimMath::sseSub(self->getPosition()->y, target->getPosition()->y));
		if (!FindPosition::findPositionAround(logic, *target->getPosition(), options, pos))
		{
			pos = *target->getPosition();
		}
		mover().ignoreObstacle(target->getID());
	}
	m_action[t].goalObject = target->getID();
	m_action[t].goalPosition = pos; // RW 0x8DB810(pos, FLT_MAX)
	mover().ignoreObstacle(target->getID()); // RW 0x66831A
	aiMoveToPosition(pos, CMD_FROM_AI);       // RW 0x66C4CA
	return STATE_SUCCESS;
}

int DozerAIUpdate::updateMoveToActionPos(int t)
{
	// RW 0x88C5C7
	Object *self = getObject();
	GameLogic &logic = self->logic();
	const int task = t;
	Object *goal = logic.findObjectByID(m_action[t].goalObject);
	if (!goal)
	{
		goal = logic.findObjectByID(m_unplacedStructure); // slot 0x74
		if (!goal)
		{
			return STATE_FAILURE;
		}
	}
	static const int swarmDozer = KindOfTokens::indexOf("SWARM_DOZER");
	if (task == DOZER_TASK_REPAIR && !self->isKindOf((unsigned)swarmDozer))
	{
		// RW 0x88C62E: another sole healer: the task is over
		const ObjectID healer = logic.getFrame() > goal->soleHealingBenefactorExpiry() ? (ObjectID)INVALID_ID : goal->soleHealingBenefactor();
		if (healer != INVALID_ID && healer != self->getID())
		{
			internalTaskComplete(task);
			m_action[t].goalObject = INVALID_ID;
			return STATE_FAILURE;
		}
	}
	const float d2 = edgeDistanceSquaredTo(*self, m_action[t].goalPosition);
	const float r = SimMath::sseAdd(CombatQueries::boundingSphereRadius(*self), kActionSlop);
	const float reach = kMinActionTolerance > r ? kMinActionTolerance : r;
	if (SimMath::sseMul(reach, reach) >= d2)
	{
		if (task == DOZER_TASK_BUILD)
		{
			if (m_unplacedStructure != INVALID_ID)
			{
				// RW 0x88C6E6 .. 0x88C704: the porter goes in (RW 0x88C51B), the structure placed (slot 0x68); RW 0x68B21D and the script engine's note (RW
				// 0x756C87(3, dozer)) are not ported (S-1282)
				hideInStructure();
				placeUnplacedStructure();
			}
			// RW 0x88C709: AWAITING_CONSTRUCTION -> PARTIALLY_CONSTRUCTED + ACTIVELY_BEING_CONSTRUCTED
			Construction::setModelConditions(*goal, { "AWAITING_CONSTRUCTION" }, { "PARTIALLY_CONSTRUCTED", "ACTIVELY_BEING_CONSTRUCTED" });
		}
		return STATE_SUCCESS;
	}
	if (isIdle())
	{
		return STATE_FAILURE;
	}
	return STATE_CONTINUE;
}

int DozerAIUpdate::updateDoAction(int t)
{
	// RW 0x88D993
	Object *self = getObject();
	GameLogic &logic = self->logic();
	Object *goal = logic.findObjectByID(m_action[t].goalObject);
	if (!goal || (self->getDisabledMask() & 0x20u)) // + 0x1C8 bit 5 (ZH DISABLED_UNMANNED)
	{
		return STATE_FAILURE;
	}
	const int task = t;
	if (task == DOZER_TASK_FORTIFY)
	{
		return STATE_CONTINUE;
	}
	if (task == DOZER_TASK_BUILD && self->getControllingPlayer() != goal->getControllingPlayer())
	{
		return STATE_FAILURE; // RW 0x88DC93
	}
	if (m_buildSubTask == DOZER_SELECT_BUILD_DOCK_LOCATION)
	{
		if (const Coord3D *dock = getDockPoint(task, DOZER_DOCK_POINT_ACTION))
		{
			aiMoveToPosition(*dock, CMD_FROM_AI);
		}
		m_buildSubTask = DOZER_MOVING_TO_BUILD_DOCK_LOCATION;
	}
	if (m_buildSubTask == DOZER_MOVING_TO_BUILD_DOCK_LOCATION && isIdle())
	{
		m_buildSubTask = DOZER_FACING_BUILD;
		// RW 0x88DD3E: AI command 0x26 (face the goal, AI state 36) is not ported (S-1282): the dozer does not turn
		logic.noteStop("[S-1282] dozer: the face command 0x26 before the work (RW 0x7C81B9 -> AI vslot 0x114, state 36) is not ported");
	}
	if (m_buildSubTask == DOZER_FACING_BUILD && isIdle())
	{
		m_buildSubTask = DOZER_DO_BUILD_AT_DOCK;
		if (task == DOZER_TASK_BUILD)
		{
			startBuildingSound(*goal, "UnderConstruction"); // RW 0x88DD74 (lane AUDIO-3)
		}
		else
		{
			const BodyModuleInterface *body = goal->getBodyModule();
			startBuildingSound(*goal, body && body->getDamageState() == BODY_RUBBLE ? "UnderRepairFromRubble" : "UnderRepairFromDamage"); // RW 0x88DAC3 / 0x88DB0C
		}
	}
	if (m_buildSubTask != DOZER_DO_BUILD_AT_DOCK)
	{
		return STATE_CONTINUE;
	}
	if (task == DOZER_TASK_BUILD)
	{
		setWorking(true); // RW 0x88DDF9
		if (goal->isEffectivelyDead())
		{
			cancelTaskInternal(DOZER_TASK_BUILD); // RW 0x88DE1C
			aiIdle(CMD_FROM_AI);
			return STATE_CONTINUE;
		}
		++m_taskWorkFrames;
		return workOnBuild(t, *goal);
	}
	return workOnRepair(t, *goal);
}

int DozerAIUpdate::taskSucceeded(int t)
{
	// RW 0x88DBB3: internalTaskComplete, the goal cleared, the AI player told (slot 0x7C), success
	internalTaskComplete(t);
	m_action[t].goalObject = INVALID_ID;
	notifyAIPlayerDozerFree();
	return STATE_SUCCESS;
}

// RW 0x88DE43 ff (the build task in sub task 3)
int DozerAIUpdate::workOnBuild(int t, Object &structure)
{
	Object *self = getObject();
	GameLogic &logic = self->logic();
	if (structure.getConstructionPercent() == -1.0f)
	{
		showFromStructure(&structure); // RW 0x88DE56 -> 0x88E156: already complete
		return taskSucceeded(t);
	}
	const int frames = BuildAssistant::calcTimeToBuild(*structure.getTemplate(), self->getControllingPlayer(), self, -1, logic.productionSettings(), logic);
	const float framesF = SimMath::sseFromInt32(frames);
	structure.setConstructionPercent(SimMath::addf32(structure.getConstructionPercent(), SimMath::divf32(100.0f, framesF)));
	if (ActiveBody *body = dynamic_cast<ActiveBody *>(structure.getBodyModule()))
	{
		body->internalChangeHealth(SimMath::fstpDword(SimMath::pc24DivW((double)body->getMaxHealth(), (double)framesF)));
	}
	if (structure.getProducerID() == INVALID_ID)
	{
		structure.setProducer(self);
	}
	if (structure.getConstructionPercent() < 100.0f)
	{
		if (structure.getBuilderID() == INVALID_ID)
		{
			structure.setBuilder(self);
		}
		return STATE_CONTINUE;
	}
	// RW 0x88DEE0 ff: the statuses, the model conditions, percent -1, Player::onStructureConstructionComplete + onBuildComplete (RW 0x6AA72B / 0x68D252), the footprint registered
	// again (RW 0x6E85FB / 0x6E85E9); the EVA / radar message is the client's
	finishBuildingSound(); // RW 0x88DEFA (lane AUDIO-3): dozer interface slot 0x60 at the build's completion
	Construction::completeConstruction(structure, self);
	// lane AI-2: the structure's construction-complete call to its player's AI (RW 0x86C1ED -> RW 0x6C77E4 -> AIPlayer RW 0x8F06FF(builder, structure, 1))
	logic.skirmishAI().structureBuilt(*self, structure);
	structure.setStatus(21, false);
	if (AIWorld *ai = logic.aiWorld())
	{
		ai->removeObjectFromPathfindMap(structure);
		ai->addObjectToPathfindMap(structure);
	}
	showFromStructure(&structure); // RW 0x88E156: the porter comes out (RW 0x88D6AE(1)), then the tail
	return taskSucceeded(t);
}

// RW 0x88DB61 ff (the repair task in sub task 3)
int DozerAIUpdate::workOnRepair(int t, Object &structure)
{
	Object *self = getObject();
	BodyModuleInterface *body = structure.getBodyModule();
	if (!body || body->getHealth() == body->getMaxHealth())
	{
		// RW 0x88DB8F: DOZER:RepairComplete (TheInGameUI, client), the building sound finished, the tail
		finishBuildingSound();
		return taskSucceeded(t);
	}
	setWorking(true); // RW 0x88DBED
	++m_taskWorkFrames;
	// RW 0x88DC46: rate (slot 4) * max health, / LOGICFRAMES_PER_SECOND (fidiv), x87 PC24, stored as the float argument
	const float amount = SimMath::fstpDword(SimMath::pc24DivW(SimMath::pc24MulW((double)body->getMaxHealth(), (double)m_repairPercentPerSecond), 5.0));
	if (!structure.attemptHealingFromSoleBenefactor(amount, self, 2))
	{
		internalTaskComplete(t); // RW 0x88DC77
		m_action[t].goalObject = INVALID_ID;
		return STATE_FAILURE;
	}
	return STATE_CONTINUE;
}

// ---- the update -----------------------------------------------------------------------------------------------------------------

UpdateSleepTime DozerAIUpdate::update()
{
	// RW 0x88CAF5
	if (m_primaryState < 0)
	{
		primarySetState(PRIMARY_IDLE); // RW 0x88C1C1: the machine made and its default state entered
	}
	AIUpdateInterface::update(); // RW 0x66E58F
	Object *self = getObject();
	if (!self->isEffectivelyDead())
	{
		if (m_currentTask == DOZER_TASK_REPAIR && !canRepair(*self, self->logic().findObjectByID(getTaskTarget(DOZER_TASK_REPAIR))))
		{
			cancelTaskInternal(DOZER_TASK_REPAIR); // RW 0x88CB6D (the ActionManager's test RW 0x82D7F2: S-1282)
		}
		// the primary machine (ZH StateMachine::updateStateMachine)
		const int before = m_primaryState;
		int status = before == PRIMARY_IDLE ? primaryUpdateIdle() : actionUpdate(before - PRIMARY_BUILD);
		if (m_primaryState != before)
		{
			status = STATE_CONTINUE;
		}
		primaryCheckTransitions(status);
	}
	return UPDATE_SLEEP_NONE;
}

void DozerAIUpdate::crc(StateHasher &h) const
{
	AIUpdateInterface::crc(h);
	for (int t = 0; t < DOZER_NUM_TASKS; ++t)
	{
		h.addU32(m_task[t].target);
		h.addU32(m_task[t].orderFrame);
		for (const DockPoint &d : m_dockPoint[t])
		{
			h.addBool(d.valid);
			h.addFloat(d.location.x);
			h.addFloat(d.location.y);
			h.addFloat(d.location.z);
		}
	}
	h.addI32(m_currentTask);
	h.addI32(m_buildSubTask);
	h.addU32(m_unplacedStructure);
	h.addI32(m_primaryState);
	h.addU32(m_idleTimestamp);
	h.addI32(m_idlePlayer);
	h.addBool(m_markedIdle);
	for (const ActionMachine &a : m_action)
	{
		h.addI32(a.state);
		h.addU32(a.goalObject);
		h.addFloat(a.goalPosition.x);
		h.addFloat(a.goalPosition.y);
		h.addFloat(a.goalPosition.z);
		h.addU32(a.doActionEnterFrame);
	}
	h.addU32(m_taskWorkFrames);
	h.addBool(m_working);
}
