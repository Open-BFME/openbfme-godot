// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// The attack states' part of the obfme:AIWorld block (lane SAVE-4c, docs/SAVES.md section 8). OpenBFME native layouts (stop S-4220). Retail:
// AIAttackState::xfer RW 0x74CD94 ((1, 3): a bool, the original victim position, raw bytes, then the attack machine as a BLOK), AIAttackApproachTargetState
// RW 0x741E38 ((1, 1)), AIAttackAimAtTargetState RW 0x742BE8 ((1, 1): three bools), the fire state's (docs/SAVES-RETAIL.md section 6). This port's states keep
// other fields (crc's), so each gets its own version 1. The attack sub machine is made again by its kind (AttackMachineMade) and then loaded by
// AIStateMachine::xfer (its states' ids must agree).

#include "GameLogic/AI/AIAttack.h"

#include "GameLogic/AI/AIAttackMelee.h"
#include "GameLogic/AI/AIXfer.h"
#include "GameLogic/Module/AIUpdate.h"

void AIAttackState::xfer(Xfer &x)
{
	XferVersion v{ 1, 2 }; // 2: lane ATKSTATE-1a's + 0x49 (m_victimWasEnemyOnEnter); a version-1 save loads it as false
	x.xferVersion(v);
	AIXfer::coord(x, m_originalVictimPos);
	x.xferUnsignedInt(&m_victimTeam);
	x.xferInt(&m_lockedSlotOnEnter);
	x.xferUnsignedInt(&m_shotsFired);
	x.xferBool(&m_meleeMachine);
	if (v.current >= 2)
	{
		x.xferBool(&m_victimWasEnemyOnEnter);
	}
	std::int32_t made = m_attackMachine ? m_machineMade : MADE_NONE;
	x.xferInt(&made);
	if (!x.isLoading())
	{
		if (m_attackMachine)
		{
			m_attackMachine->xfer(x);
		}
		return;
	}
	// the sub machine of the saved kind, made and loaded complete before it replaces the current one
	std::unique_ptr<AIStateMachine> loaded;
	switch (made)
	{
	case MADE_NONE:
		break;
	case MADE_GENERIC:
		loaded = makeGenericAttackMachine();
		break;
	case MADE_MELEE:
		loaded = makeMeleeAttackMachine(ai(), this);
		break;
	case MADE_HORDE_MELEE:
	case MADE_HORDE_RANGED:
		loaded = ai().makeHordeAttackMachine(this, made == MADE_HORDE_MELEE ? 1 : 0);
		if (!loaded)
		{
			throw XferException("ai attack: a horde attack machine saved for an AI that makes none");
		}
		break;
	default:
		throw XferException("ai attack: an attack machine of kind " + std::to_string(made) + " (0 .. 4)");
	}
	if (loaded)
	{
		loaded->xfer(x);
	}
	m_attackMachine = std::move(loaded);
	m_machineMade = made;
}

void AIAttackApproachState::xfer(Xfer &x)
{
	XferVersion v{ 1, 1 };
	x.xferVersion(v);
	xferMove(x, ai().mover(), m_move);
	AIXfer::coord(x, m_prevVictimPos);
	x.xferUnsignedInt(&m_approachFrame);
}

void AIAttackAimState::xfer(Xfer &x)
{
	XferVersion v{ 1, 1 };
	x.xferVersion(v);
	x.xferBool(&m_setLocomotor);
	x.xferBool(&m_canTurnInPlace);
}

void AIAttackFireState::xfer(Xfer &x)
{
	XferVersion v{ 1, 1 };
	x.xferVersion(v);
	x.xferBool(&m_deferred);
}

// the melee machine's states (RW 0x744B71): AIAttackMeleeApproachState::xfer RW 0x742229 is (1, 1): the internal move's, then a position, a cell and a frame
void AIAttackMeleeApproachState::xfer(Xfer &x)
{
	XferVersion v{ 1, 1 };
	x.xferVersion(v);
	xferMove(x, ai().mover(), m_move);
	AIXfer::coord(x, m_victimPos);
	AIXfer::coord(x, m_dest);
	x.xferUnsignedInt(&m_pathFrame);
	x.xferBool(&m_pending);
	x.xferBool(&m_moveEntered);
}

void AIAttackMeleeEngageState::xfer(Xfer &x)
{
	XferVersion v{ 1, 1 };
	x.xferVersion(v);
	xferMove(x, ai().mover(), m_move);
	AIXfer::coord(x, m_dest);
	AIXfer::coord(x, m_victimPos);
	x.xferUnsignedInt(&m_pathFrame);
	x.xferUnsignedInt(&m_waitUntil);
	x.xferBool(&m_wait);
	x.xferBool(&m_noDest);
	x.xferBool(&m_pending);
	x.xferBool(&m_moveEntered);
}

void AIMeleeReAcquireState::xfer(Xfer &x)
{
	XferVersion v{ 1, 1 };
	x.xferVersion(v);
	x.xferUnsignedInt(&m_lastFrame);
	x.xferBool(&m_ran);
}
