// OpenBFME. GPL-3.0.
// PORT-165: states in the horde attack-on-position machine.
#pragma once

#include "GameLogic/AI/AIStateMachine.h"

// RW 0x740E16, vtable 0xC277E8; both 0x2C0 and 0x2BE use this state.
class AIHordePositionWaitState final : public AIState
{
public:
	explicit AIHordePositionWaitState(AIStateMachine &machine);
	StateReturnType onEnter() override;
	StateReturnType update() override;
	void xfer(class Xfer &x) override { xferNoFields(x); }
};

class AttackFireNotify;

// RW 0x740E67, vtable 0xC278B8: position fire (state 0x2BD).
class AIHordePositionFireState final : public AIState
{
public:
	AIHordePositionFireState(AIStateMachine &machine, AttackFireNotify &notify);
	StateReturnType onEnter() override;
	StateReturnType update() override;
	void onExit(StateExitType status) override;
	void crc(class StateHasher &hasher) const override;
	void xfer(class Xfer &x) override;

private:
	AttackFireNotify *m_notify;
	bool m_deferred = false;
};

// RW 0x744379 / 0xC28CD8: the position-approach state (0x2BC).
class AIHordePositionApproachState final : public AIState, private AIMoveComputePathHook
{
public:
	explicit AIHordePositionApproachState(AIStateMachine &machine);
	StateReturnType onEnter() override;
	StateReturnType update() override;
	void onExit(StateExitType status) override;
	void onTemporaryStateEnded() override;
	void crc(class StateHasher &hasher) const override;
	void crcPersistent(class StateHasher &hasher) const override;
	bool hasPersistentCrc() const override { return true; }
	void xfer(class Xfer &x) override;

private:
	bool computeSpecialPath(AIMoveToState &move) override;
	void ensureMove();
	std::unique_ptr<AIMoveToState> m_move;
	Coord3D m_ownerPosition{0.0f, 0.0f, 0.0f};
	unsigned m_pathFrame = 0;
	unsigned m_waitFrame = 0;
	bool m_stopIfInRange = false;
	bool m_initialApproach = true;
	bool m_waiting = false;
};

// RW 0x74548D: shared kind-7 machine, including both distinct wait rows.
std::unique_ptr<AIStateMachine> makeHordePositionAttackMachine(AIUpdateInterface &ai, AttackFireNotify &notify);
