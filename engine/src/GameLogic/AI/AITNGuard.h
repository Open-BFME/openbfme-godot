// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0 (GameLogic/AI/AITNGuard.cpp, AIStates.cpp AITunnelNetworkGuardState).
//
// The tunnel network guard state (state 0x18 / 24, RW 0x754259) and its sub-machine AITNGuardMachine (states 5000..5005, RW 0x94C8D0).
//
// TARGET FACTS (RotWK game.dat, caveat S-001; DONOR Open-BFME-2 AITNGuard.cpp / AITunnelNetworkGuardState*.cpp / AITNGuardReturnStateOnEnter.cpp):
//   * privateGuardTunnelNetwork RW 0x664ABF (AICMD 0x37 -> vslot 0xF8): rejects HORDE_MEMBER (status 0x26), !Object::isMobile (RW 0x690E97), or PROJECTILE;
//     sets m_guardMode (+0x4C), clears machine, records command source (+0x48), and sets parent machine state 0x18 (AI_TUNNEL_NETWORK_GUARD).
//   * AITunnelNetworkGuardState (vtable 0xC27EB0, isIdle false RW 0x9188EB, isActive true RW 0x8BD372):
//     - onEnter RW 0x743AA6: creates AITNGuardMachine, passes guardLocation (+0x1D8 -> +0x3C) and guardMode (+0x1F4 -> +0x4C), calls initDefaultState and
//       unless FAILURE enters 5003 (AI_TN_GUARD_RETURN);
//     - update RW 0x747EAA: requires m_guardMachine and !(isEffectivelyDead && !PROJECTILE), locks parent machine, steps m_guardMachine under KeptSubMachines;
//     - onExit RW 0x743B40: drops m_guardMachine via KeptSubMachines.
//   * AITNGuardMachine (vtable 0xC81090, ctor RW 0x94C8D0):
//     - 5003 AITNGuardReturnState (-> 5001 / 5000, cond hasAttackedMe -> 5005): onEnter RW 0x94CEB4 draws random(0, GuardEnemyReturnScanRate), picks
//       findBestTunnel (RW 0x94C84F) around owner position, sets goalObject, tail-calls AIEnterState::onEnter (RW 0x7515E1); update RW 0x94C517 checks
//       teamTarget and TunnelTracker::getCurNemesis (-> FAILURE / 5000), else runs AIEnterState::update with non-continue collapsed to SUCCESS (-> 5001).
//     - 5001 AITNGuardIdleState (-> 5000 / 5003, cond hasAttackedMe -> 5005, isIdle false RW 0x769CD2): onEnter RW 0x94C579 draws random(0, GuardEnemyScanRate);
//       update RW 0x94CF2A wakes at m_nextEnemyScanTime, calls lookForInnerTarget RW 0x94CA71: when true and contained, finds tunnel closest to nemesis, waits
//       if isExitBusy (RW 0x87CE66), else calls exitObjectInAHurry(&owner) (RW 0x868759) and continues; when true and not contained -> SUCCESS (5000);
//       when false and not contained with a friendly tunnel -> FAILURE (5003); else sleeps until m_nextEnemyScanTime.
//     - 5000 AITNGuardInnerState (-> 5002 / 5002): onEnter RW 0x94CBFC starts non-following AIAttackState with TNGuardExitConditions (GuardChaseUnitsDuration);
//       update RW 0x94CC98 switches goal to teamTarget or getCurNemesis when lost, optionally scans lookForEnemy RW 0x94C6BD when m_scanForEnemy is set.
//     - 5002 AITNGuardOuterState (-> 5004 / 5004): skips on GUARDMODE_GUARD_WITHOUT_PURSUIT; onEnter RW 0x94CD9D runs non-following AIAttackState with
//       TNGuardExitConditions; update RW 0x94CE46 falls back to teamTarget when attackCommonTarget.
//     - 5004 AIPickUpCrateState (-> 5003 / 5003): no AI crates in port, succeeds immediately.
//     - 5005 AITNGuardAttackAggressorState (-> 5003 / 5003): onEnter RW 0x94D02C reads lastDamager, updates TunnelTracker nemesis, starts following AIAttackState;
//       update RW 0x94D11D refreshes TunnelTracker nemesis while in ATTACK_FIRE (0x67); onExit RW 0x94C5C6 clears team target.

#pragma once

#include "GameLogic/AI/AIAttack.h"
#include "GameLogic/AI/AIGuard.h"
#include "GameLogic/AI/AIStateMachine.h"

#include <memory>

class Object;
class Player;

enum
{
	AI_TUNNEL_NETWORK_GUARD = 0x18 // RW 0x664B07 setState(0x18); RW 0x754259 AITunnelNetworkGuardState
};

// RotWK's AITNGuardMachine state ids (RW 0x94C8D0)
enum TNGuardStateId
{
	AI_TN_GUARD_INNER = 5000,
	AI_TN_GUARD_IDLE = 5001,
	AI_TN_GUARD_OUTER = 5002,
	AI_TN_GUARD_RETURN = 5003,
	AI_TN_GUARD_GET_CRATE = 5004,
	AI_TN_GUARD_ATTACK_AGGRESSOR = 5005
};

// RW vtable 0xC8113C: AITNGuardAttackExitCond (shouldExit RW 0x94C43C)
struct TNGuardExitConditions : public AttackExitConditions
{
	unsigned exitFrame = 0; // + 0x04
	bool shouldExit(const AIStateMachine &machine) const override;
};

// RW vtable 0xC81090: AITNGuardMachine
class AITNGuardMachine : public AIStateMachine
{
public:
	explicit AITNGuardMachine(AIUpdateInterface &ai);
	bool lookForInnerTarget(); // RW 0x94CA71
	static Object *findBestTunnel(class GameLogic &logic, const Player *player, const Coord3D &pos); // RW 0x94C84F
	static Object *lookForEnemy(Object &obj); // RW 0x94C6BD

	void setPositionToGuard(const Coord3D &p) { m_positionToGuard = p; }
	const Coord3D &positionToGuard() const { return m_positionToGuard; }
	void setGuardMode(int mode) { m_guardMode = mode; }
	int guardMode() const { return m_guardMode; }
	ObjectID nemesis() const { return m_nemesis; }
	void setNemesis(ObjectID id) { m_nemesis = id; }

	void crcGuard(class StateHasher &h) const;
	void xferGuard(class Xfer &x); // RW 0x94C781 layout in OpenBFME SAVE-4c

private:
	Coord3D m_positionToGuard{ 0.0f, 0.0f, 0.0f }; // + 0x3C
	ObjectID m_nemesis = INVALID_ID;               // + 0x48
	int m_guardMode = GUARDMODE_NORMAL;            // + 0x4C
};

// RW vtable 0xC27EB0: AITunnelNetworkGuardState (state 0x18 in AIUpdateInterfaceMachine)
class AITunnelNetworkGuardState : public AIState
{
public:
	explicit AITunnelNetworkGuardState(AIStateMachine &m) : AIState(m, "AITunnelNetworkGuardState") {}
	~AITunnelNetworkGuardState() override;
	StateReturnType onEnter() override;            // RW 0x743AA6
	StateReturnType update() override;             // RW 0x747EAA
	void onExit(StateExitType status) override;    // RW 0x743B40
	bool isIdle() const override { return false; } // RW 0x9188EB
	bool isActive() const override { return true; } // RW 0x8BD372
	void crc(class StateHasher &hasher) const override;
	void xfer(class Xfer &x) override;             // RW 0x743A14

	AITNGuardMachine *guardMachine() const { return m_guardMachine.get(); }

private:
	std::unique_ptr<AITNGuardMachine> m_guardMachine;       // + 0x20
	KeptSubMachines<AITNGuardMachine> m_keptMachines;
};
