// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// The AI update module family's state in the obfme:AIWorld block (lane SAVE-4c, docs/SAVES.md section 8). OpenBFME native layout (stop S-4220). Retail:
// AIUpdateInterface::xfer RW 0x667416 (window (1, 6), tier B) saves the module inside Object::xfer's module list (RW 0x697F28) after UpdateModule::xfer, with
// the locomotor set (LocomotorSet::xferSelfAndCurLocoPtr RW 0x5E68A4), the turret (TurretAI::xfer RW 0x8DC2C3) and the state machine
// (AIStateMachine::xfer RW 0x755174); HordeAIUpdate / DozerAIUpdate RW 0x88C966 ((1, 2)) add their own after it (docs/SAVES-RETAIL.md section 6). This
// port's module is built differently (PATH-1's AIMover holds the path and movement fields, the locomotor set is the template's slot), so its fields are
// saved in crc's order with the parts' own versions. The update module's own state (its next call frame and phase) is the scheduler's (obfme:UpdateScheduler,
// SAVE-4b); the module itself is made with its object (obfme:Objects, SAVE-4b): this block fills it.
// Not saved: the module data's settings (the combat settings, the turret data, the movement info: from the template), the audio loop flag
// (m_moveLoopPosted: the audio side's bookkeeping, which no logic reads), the destructor flag. The pending position's frame stamp IS saved (review r2: the
// crush logic reads it through pendingPositionOfFrame).

#include "GameLogic/Module/AIUpdate.h"

#include "Common/Xfer.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/AI/AIXfer.h"
#include "Common/PlayerList.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/AI/TurretAI.h"
#include "GameLogic/Module/DozerAIUpdate.h"
#include "GameLogic/Module/GiantBirdAIUpdate.h"
#include "GameLogic/Module/HordeAIUpdate.h"
#include "GameLogic/Object/Object.h"

#include <limits>

// lane SAVE-4e: the AI update family in Object::xfer's module loop (obfme:Objects): each class its own version (SAVES.md section 8), then the base's; the
// update module's next call frame. The AI state is xferAIState's (obfme:AIWorld, after obfme:Objects in the list)
void AIUpdateInterface::xfer(Xfer &x)
{
	XferVersion v{ 1, 1 };
	x.xferVersion(v);
	xferUpdateModule(x);
}

void HordeAIUpdate::xfer(Xfer &x)
{
	XferVersion v{ 1, 1 };
	x.xferVersion(v);
	AIUpdateInterface::xfer(x);
}

void DozerAIUpdate::xfer(Xfer &x)
{
	XferVersion v{ 1, 1 };
	x.xferVersion(v);
	AIUpdateInterface::xfer(x);
}

void GiantBirdAIUpdate::xfer(Xfer &x)
{
	XferVersion v{ 1, 1 };
	x.xferVersion(v);
	AIUpdateInterface::xfer(x);
}

void AIUpdateInterface::xferAIState(Xfer &x)
{
	AIXfer::IssuedIdScope ids(getObject()->logic().getNextObjectID()); // the object ids the module names: ids the game has issued
	AIXfer::transaction(x, [this](Xfer &t) { xferAIStateBody(t); });
}

void AIUpdateInterface::xferAIStateBody(Xfer &x)
{
	// version 2 (lane TARGET-1 + lane ATTACKPATH-1 + lane FLANK-1, merged in q23 before any shipped): the mood scan's random-offset flag (AI + 0x3BC) and
	// ordered melee target (AI + 0x1A4); lane BACKLOG-8's formation slot and march fields (hashed but not saved before, found by save2 retail T1), last
	XferVersion v{ 1, 2 };
	x.xferVersion(v);
	const bool loading = x.isLoading();
	for (std::uint32_t &w : m_luaConditionSnapshot)
	{
		x.xferUnsignedInt(&w);
	}
	AIXfer::enumIn(x, m_lastCommandSource, CMD_FROM_PLAYER, CMD_FROM_AI, "ai: the last command's source");
	if (loading)
	{
		AIXfer::inRange(m_lastCommandSource, CMD_FROM_PLAYER, CMD_FROM_AI, "ai: the last command's source");
	}
	AIXfer::objectId(x, m_currentVictim, "ai: the object id currentVictim");
	AIXfer::objectId(x, m_targetHorde, "ai: the object id targetHorde");
	AIXfer::objectId(x, m_moveAwayRequesters[0], "ai: the object id moveAwayRequesters[0]");
	AIXfer::objectId(x, m_moveAwayRequesters[1], "ai: the object id moveAwayRequesters[1]");
	x.xferBool(&m_canPathThroughUnits);
	x.xferBool(&m_attackMoveActive);
	x.xferInt(&m_attitude);
	if (loading)
	{
		AIXfer::inRange(m_attitude, -3, 2, "ai: the attitude"); // RW AI + 0x218: -3 .. 2 (setAttitude)
	}
	x.xferBool(&m_preventPlayerCommands);
	x.xferBool(&m_fastCower);
	AIXfer::uns(x, m_minCowerTime);
	AIXfer::uns(x, m_maxCowerTime);
	AIXfer::coord(x, m_attackMoveGoal);
	AIXfer::uns(x, m_nextMoodCheck);
	if (v.current >= 2)
	{
		x.xferBool(&m_randomlyOffsetMoodCheck);
		AIXfer::objectId(x, m_orderedMeleeTarget, "ai: the object id orderedMeleeTarget");
	}
	else if (loading)
	{
		m_randomlyOffsetMoodCheck = false; // a version 1 snapshot predates the fields: the constructor's values
		m_orderedMeleeTarget = INVALID_ID;
	}
	x.xferBool(&m_isInUpdate);
	x.xferInt(&m_guardMode); // lane PLAY-4's guard (RW AI + 0x4C / + 0x54 / + 0x58 / + 0x1A0)
	x.xferInt(&m_guardTargetType);
	AIXfer::coord(x, m_guardLocation);
	x.xferReal(&m_guardAngle);
	if (loading)
	{
		AIXfer::inRange(m_guardMode, 0, 2, "ai: the guard mode");          // GuardMode
		AIXfer::inRange(m_guardTargetType, 0, 3, "ai: the guard target type"); // GuardTargetType
	}
	x.xferBool(&m_pendingValid);
	AIXfer::coord(x, m_pendingPosition);
	// the frame stamp of the pending position: the crush test reads it through pendingPositionOfFrame (review r2: not client-only), so it is state
	x.xferUnsignedInt(&m_pendingFrame);
	// the turret: made by the constructor from the module data (RW 0x66ECFE); the snapshot's must be there
	bool turret = m_turret != nullptr;
	x.xferBool(&turret);
	if (turret != (m_turret != nullptr))
	{
		throw XferException(std::string("ai: the snapshot's AI ") + (turret ? "has" : "has no") + " turret, this one " + (m_turret ? "has one" : "has none"));
	}
	if (m_turret)
	{
		m_turret->xfer(x);
	}
	m_mover->xfer(x);
	PathfindLayerEnum layer = m_adapter->getLayer();
	AIXfer::enumIn(x, layer, LAYER_INVALID, LAYER_LAST, "ai: the pathfinder layer");
	if (loading)
	{
		AIXfer::inRange(layer, LAYER_INVALID, LAYER_LAST, "ai: the pathfinder layer");
	}
	m_adapter->setLayer(layer);
	// the locomotor set (chooseLocomotorSet's instances made again from the template's slot, without the choice of the current one: it is saved)
	std::int32_t set = loading ? 0 : m_curLocomotorSet;
	x.xferInt(&set);
	std::uint32_t count = (std::uint32_t)m_locomotors.size();
	x.xferCount(&count, 2, sizeof(Locomotor)); // a locomotor is at least its version and an empty name
	std::int32_t current = -1;
	for (size_t i = 0; i < m_locomotors.size(); ++i)
	{
		current = m_locomotors[i].get() == m_curLocomotor ? (std::int32_t)i : current;
	}
	x.xferInt(&current);
	if (loading)
	{
		// the set's instances made complete (from the template's slot) and loaded before they replace the current ones
		std::vector<std::unique_ptr<Locomotor>> made;
		if (set != -1)
		{
			const LocomotorSetTemplate::Slot *slot = m_info->locomotorSets.find(set);
			if (!slot)
			{
				throw XferException("ai: the snapshot's locomotor set " + std::to_string(set) + " is not in the template");
			}
			for (const LocomotorTemplate *lt : slot->locomotors)
			{
				if (lt)
				{
					made.push_back(std::make_unique<Locomotor>(lt));
				}
			}
		}
		if (made.size() != count || current < -1 || current >= (std::int32_t)count)
		{
			throw XferException("ai: the snapshot's locomotor set " + std::to_string(set) + " has " + std::to_string(count) + " locomotors (current " +
				std::to_string(current) + "), the template's " + std::to_string(made.size()));
		}
		for (std::unique_ptr<Locomotor> &l : made)
		{
			l->xfer(x);
		}
		m_locomotors = std::move(made);
		m_curLocomotorSet = set;
		m_curLocomotor = current < 0 ? nullptr : m_locomotors[(size_t)current].get();
	}
	else
	{
		for (std::unique_ptr<Locomotor> &l : m_locomotors)
		{
			l->xfer(x);
		}
	}
	x.xferReal(&m_locomotorSetSpeed);
	x.xferBool(&m_upgradedLocomotors);
	// the state machine (made at onObjectCreated, B1 AIUpdate.cpp:2006; a load into a module without one makes it, without entering the default state)
	bool machine = m_machine != nullptr;
	x.xferBool(&machine);
	x.xferBool(&m_stateMachineMade);
	if (loading)
	{
		AIStateMachine::LoadGuard guard;
		if (machine && !m_machine)
		{
			m_machine = makeStateMachine();
		}
		else if (!machine)
		{
			m_machine.reset();
		}
	}
	if (m_machine)
	{
		AIStateMachine::LoadGuard guard; // states the load replaces (an attack's sub machine, a move) go without their exits
		m_machine->xfer(x);
	}
	// the machine kept across a temporary defection (RW AI + 0x38, lane BACKLOG4: beginDefectedStateMachine; hashed): saved whole like the live one, made
	// and loaded complete before it replaces the kept one (review r7: the merge of the archive 593e83cd added it)
	bool defected = m_defectedMachine != nullptr;
	x.xferBool(&defected);
	if (loading)
	{
		AIStateMachine::LoadGuard guard;
		std::unique_ptr<AIStateMachine> kept;
		if (defected)
		{
			kept = makeStateMachine();
			kept->xfer(x);
		}
		m_defectedMachine = std::move(kept); // the replaced one drops its states without their exits (the guard)
	}
	else if (m_defectedMachine)
	{
		m_defectedMachine->xfer(x);
	}
	// the report's bookkeeping
	AIXfer::map(x, m_unportedCommands, 5, [&](std::string &k, unsigned &n) {
		AIXfer::reportString(x, k);
		AIXfer::uns(x, n);
	});
	AIXfer::reportStrings(x, m_locomotorNotes);
	AIXfer::u64(x, m_memberUpdates);
	// lane BACKLOG-8's formation id and offset (RW Object + 0x42C / + 0x430 / + 0x434) and the march's angle and path index (AI + 0x1A0 / + 0x194)
	if (v.current >= 2)
	{
		x.xferInt(&m_formationId);
		x.xferReal(&m_formationOffset.x);
		x.xferReal(&m_formationOffset.y);
		x.xferReal(&m_teamPathAngle);
		x.xferInt(&m_currentGoalPathIndex);
		if (loading)
		{
			AIXfer::inRange(m_formationId, 0, std::numeric_limits<int>::max(), "ai: the formation id m_formationId");       // 0: none (AIGroup::createFormation's ids start at 1)
			AIXfer::inRange(m_currentGoalPathIndex, -1, std::numeric_limits<int>::max(), "ai: the march's path index"); // -1: none
		}
	}
	else if (loading)
	{
		m_formationId = 0;
		m_formationOffset = Coord2D{ 0.0f, 0.0f };
		m_teamPathAngle = 0.0f;
		m_currentGoalPathIndex = -1;
	}
}

void HordeAIUpdate::xferAIStateBody(Xfer &x)
{
	AIUpdateInterface::xferAIStateBody(x);
	XferVersion v{ 1, 1 };
	x.xferVersion(v);
	AIXfer::objectId(x, m_engagedTarget, "horde ai: the object id engagedTarget");
	AIXfer::objectId(x, m_cacheTarget, "horde ai: the object id cacheTarget");
	AIXfer::uns(x, m_cacheExpiry);
	x.xferVectorSize(m_records, 59);
	for (MemberRecord &r : m_records)
	{
		AIXfer::objectId(x, r.id, "horde ai: the object id r.id");
		AIXfer::coord(x, r.dest);
		x.xferBool(&r.hasDest);
		x.xferInt(&r.historyCount);
		for (int i = 0; i < 4; ++i)
		{
			x.xferInt(&r.history[i][0]);
			x.xferInt(&r.history[i][1]);
		}
		AIXfer::uns(x, r.timer);
		x.xferBool(&r.idle);
		x.xferBool(&r.dirty);
		if (x.isLoading())
		{
			AIXfer::inRange(r.historyCount, 0, 4, "horde ai: a member record's history count");
		}
	}
	AIXfer::u64(x, m_stats.orders);
	AIXfer::u64(x, m_stats.steps);
	AIXfer::u64(x, m_stats.idleCycles);
	AIXfer::u64(x, m_contactRefreshes);
	x.xferBool(&m_newTarget);
	AIXfer::u64(x, m_swarmTicks);
}

void DozerAIUpdate::xferAIStateBody(Xfer &x)
{
	AIUpdateInterface::xferAIStateBody(x);
	XferVersion v{ 1, 2 }; // lane LUMBER1: version 2 ends with the supply half (WorkerAIUpdate; nothing for a dozer)
	x.xferVersion(v);
	for (int t = 0; t < DOZER_NUM_TASKS; ++t)
	{
		AIXfer::objectId(x, m_task[t].target, "dozer ai: the object id task[t].target");
		x.xferUnsignedInt(&m_task[t].orderFrame);
		for (DockPoint &d : m_dockPoint[t])
		{
			x.xferBool(&d.valid);
			AIXfer::coord(x, d.location);
		}
	}
	x.xferInt(&m_currentTask);
	x.xferInt(&m_buildSubTask);
	AIXfer::objectId(x, m_unplacedStructure, "dozer ai: the object id unplacedStructure");
	x.xferInt(&m_primaryState);
	if (x.isLoading())
	{
		AIXfer::inRange(m_currentTask, DOZER_TASK_INVALID, DOZER_NUM_TASKS - 1, "dozer ai: the current task");
		AIXfer::inRange(m_buildSubTask, DOZER_SELECT_BUILD_DOCK_LOCATION, DOZER_DO_BUILD_AT_DOCK, "dozer ai: the build sub task");
		AIXfer::inRange(m_primaryState, -1, PRIMARY_FORTIFY, "dozer ai: the primary state"); // -1 until the machine is made (RW 0x88C1C1)
	}
	x.xferUnsignedInt(&m_idleTimestamp);
	x.xferInt(&m_idlePlayer);
	x.xferBool(&m_markedIdle);
	x.xferInt(&m_transitionDepth);
	if (x.isLoading()) // lane SAVE-4e (closed sets): the idle list's player (-1: none); the transition guard is 0 between frames (it counts nested calls, < 20)
	{
		AIXfer::inRange(m_idlePlayer, -1, getObject()->logic().players().getPlayerCount() - 1, "dozer ai: the idle list's player");
		AIXfer::inRange(m_transitionDepth, 0, 0, "dozer ai: the transition depth between frames");
	}
	for (ActionMachine &a : m_action)
	{
		x.xferInt(&a.state);
		AIXfer::objectId(x, a.goalObject, "dozer ai: the object id a.goalObject");
		AIXfer::coord(x, a.goalPosition);
		x.xferUnsignedInt(&a.doActionEnterFrame);
		if (x.isLoading())
		{
			AIXfer::inRange(a.state, ACTION_NONE, ACTION_DO_ACTION, "dozer ai: an action machine's state");
		}
	}
	x.xferUnsignedInt(&m_taskWorkFrames);
	x.xferBool(&m_working);
	AIXfer::objectId(x, m_buildingSoundHolder, "dozer ai: the object id buildingSoundHolder");
	if (v.current >= 2)
	{
		xferSupplyBody(x);
	}
}
