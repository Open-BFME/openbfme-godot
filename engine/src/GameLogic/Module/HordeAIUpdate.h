// OpenBFME. GPL-3.0.
//
// HordeAIUpdate (lane MOVE-1): the AI of a horde object (horde spec 2.11). The horde object is the thing that paths; its members are ordinary units the
// HordeContain's member pass steers (GameLogic/Object/Contain/HordeMemberPass.cpp).
//
// TARGET FACTS (RotWK game.dat, caveat S-001): RW 0x89E2D0, the twin of B1 HordeAIUpdate_update.cpp (RVA 0x2C4790): when the horde's AI is not moving and the
// formation was last refreshed more than 5 frames ago (BFME1 hard-codes 5; RotWK reads LOGICFRAMES_PER_SECOND), the contain's updateFormation(1) runs, which
// stamps the frame and sends the member pass round again: members that have been displaced walk back to their slots. The base AIUpdateInterface::update
// runs last.
// NOT PORTED (S-222): the IS_LEAVING_FACTORY reform (production, lane PROD-1), the "ready" notification of idle hordes (iface 0x98) and the per-frame horde tick
// (iface 0xDC: the melee tick of M2).

#pragma once

#include "GameLogic/Module/AIUpdate.h"

#include <vector>

struct MeleeBehaviorModuleData;
class GameLogic;

// COMBAT-1 (horde spec 2.7 / 2.8): the horde object's attack. The horde object fights through the horde attack machine (B1 Rva001812B0AIHordeMachineCtor.cpp, ids 0xC8..0xCC): a
// MELEE_HORDE with a MeleeWeapon approaches until the Amoeba readiness rule holds, then its members fight (the per-member Amoeba behaviour below); any other horde approaches to the
// range of its rangefinder weapon, whose HordeAttackNugget releases the ranks of RanksToReleaseWhenAttacking to shoot (RW 0x241F10).
//
// WHAT IS INFERENCE (stop S-327): see CombatState::stops().
class HordeAIUpdate : public AIUpdateInterface
{
public:
	HordeAIUpdate(Thing *thing, const ModuleData *data)
		: AIUpdateInterface(thing, data)
	{
	}

	AttackMachineKind attackMachineKind() const override { return ATTACK_MACHINE_HORDE; }
	// lane MOVE-2 r2: HordeAIUpdate::aiDoCommand RW 0x89E169: after the gate, a horde command of the hand-off list first ends its members' orders (HordeContain
	// slot 0x14, RW 0x87594C) unless the horde stands in a container; then the base command runs
	void commandAccepted(CommandSourceType source, int command, Object *target) override;
	// the command numbers RW 0x89E169's switch sends to slot 0x14 (0, 1, 3, 4, 6 .. 9, 0xB, 0xC, 0xE, 0xF, 0x17, 0x18, 0x24, 0x32 .. 0x34, 0x36, 0x38, 0x41,
	// 0x42, 0x4E) and the port's unidentified move variants (AIUpdateInterface::kCommandUnidentifiedMove, INFERENCE S-1501)
	static bool commandEndsMemberOrders(int command);
	std::unique_ptr<AIStateMachine> makeHordeAttackMachine(AIAttackState *att) override;
	void crc(StateHasher &hasher) const override;

	// ---- melee ----
	// RW 0x9E6763 / 0x98FA8E (Amoeba isTargetReady, cached for 3 seconds): the fighting can start / goes on
	bool isMeleeTargetReady(Object &target);
	// RW 0x86D75E: the horde engages `target` (does nothing when it is the engaged one); every member's orders are cancelled, the formation freezes
	void beginMelee(Object &target);
	// RW 0x86C0F9: the fight is over; the survivors walk back to their slots
	void endMelee();
	// lane INTEG-1, RW 0x86C40D (HordeContain slot 0x260): the old MeleeBehavior object is destroyed and a new one made from the selected data; the port keeps the
	// behaviour's runtime here, so this resets it the way the replacement starts: its records are rebuilt for the members (runtime slot +4, Amoeba RW 0x990B5B: one
	// fresh record per member through RW 0x98F5A5: no destination, no history, timer 0, not idle, dirty) and the Amoeba constructor (RW 0x98FE62) sets newTarget
	// (+0x14) to 1. The engaged target and the readiness cache are the contain's and stay; no order is given and no random number drawn
	void resetMeleeRuntime(int kind);
	// RW 0x009902A1 Amoeba per-frame update: the members that are not fighting attack what is in reach or step toward the enemy
	void meleeTick(Object &target);
	// lane MOVE-2 r3 (S-1502): RW 0x870A1B, the melee part of the contain's update (RW 0x872FD8): the engaged target gone -> the melee ends (slot 0x138); else the
	// melee behaviour's update (slot 0x14: Amoeba RW 0x9902A1, HoldGround RW 0x98B7CC) and the horde turns to face the target (or its horde)
	void containMeleeUpdate();
	// lane MOVE-2 r3: the melee behaviour's slot 0x34, read by the contain's slot 7 (RW 0x877D89) while the horde melees: Amoeba RW 0x98F819 hands out the member
	// record's destination when its flag (+ 0x39) is set; HoldGround RW 0x8F8014 none (the member keeps its formation slot)
	bool meleeDestination(ObjectID member, Coord3D &out) const;
	// lane MOVE-2 r3: the move hub's melee turn test (RW 0x874FA6 .. with the behaviour's slots 0x1C / 0x20 / 0x24): the Amoeba's 0x1C and 0x24 answer true
	// (RW 0x9B501B), so a member at its melee destination always turns to the formation's facing and is never put on it; HoldGround's 0x1C is false
	bool meleeAlwaysTurns() const;
	ObjectID engagedTarget() const { return m_engagedTarget; }
	// ---- lane HORDE-2: what HordeMemberCollide (RW 0x8C0518) asks the horde of a member (RW HordeContainInterface slots; the port keeps the melee target id and the readiness
	// expiry here: RW +0x16C / +0x170 are the cache this class keeps as m_cacheTarget / m_cacheExpiry) ----
	// slot 0x16C: the melee target id (0 when none)
	ObjectID meleeTargetId() const { return m_cacheTarget; }
	// slot 0x164 RW 0x86BE9A: `obj` or its container is the melee target -> the readiness holds for 3 * LOGICFRAMES_PER_SECOND more frames
	void refreshMeleeReadiness(Object &obj);
	// slot 0x160 RW 0x870180: the readiness holds and `t` shares the melee target's container or stands within 100 (edge distance) of this horde
	bool isInCurrentMelee(Object *t);
	// slot 0x15C RW 0x86D614: a member is attacking (AI slot 0x1BC) with IS_MELEE_ATTACKING
	bool hasMeleeAttackingMember() const;
	unsigned long long contactRefreshes() const { return m_contactRefreshes; }
	// lane HORDE-2: the stop line of the Squish (crush attack) state, S-583
	static const char *squishStopLine();
	static const char *memberCollideStopLine(); // S-586
	static const char *amoebaStopLine();        // S-588
	static const char *approachStopLine();      // S-590
	// RW 0x241F10: the ranks of RanksToReleaseWhenAttacking attack `target` (a HordeAttackNugget fire)
	void releaseMembersToAttack(Object &target);

	struct MeleeStats
	{
		unsigned long long orders = 0;      ///< attack orders given to members
		unsigned long long steps = 0;       ///< one-cell steps ordered
		unsigned long long idleCycles = 0;  ///< members that went idle
		unsigned long long releases = 0;    ///< HordeAttackNugget releases
	};
	const MeleeStats &meleeStats() const { return m_stats; }

	friend struct Horde2HashAccess; // the HORDE-2 hash mutation tests (test_horde2_hash.cpp)

private:
	struct MemberRecord
	{
		ObjectID id = 0;
		Coord3D dest{ 0.0f, 0.0f, 0.0f };
		bool hasDest = false;
		int historyCount = 0;
		int history[4][2] = {};
		unsigned timer = 0;
		bool idle = false;
		bool dirty = false; ///< RW record + 0xC
	};
	void pushHistory(MemberRecord &rec, int x, int y);    // RW 0x98F3A3
	bool amoebaIdleCycle(MemberRecord &rec, const MeleeBehaviorModuleData &amoeba, GameLogic &logic); // RW 0x990526 .. 0x9905A1
	void recentreOnMembers();                              // RW 0x86F18F
	void holdGroundTick(Object &target);                   // RW 0x98B7CC
	void swarmTick(Object &target);                        // RW 0x98C6A8 (not ported, S-588)
	bool m_newTarget = false;                              ///< RW MeleeBehavior + 0x14
	unsigned long long m_swarmTicks = 0;
	MemberRecord &recordOf(ObjectID id);
	std::vector<Object *> membersOf() const;
	ObjectID m_engagedTarget = 0;
	ObjectID m_cacheTarget = 0;
	unsigned m_cacheExpiry = 0;
	std::vector<MemberRecord> m_records;
	MeleeStats m_stats;
	unsigned long long m_contactRefreshes = 0; ///< HORDE-2: readiness refreshes by member contact

protected:
	void updateBeforeBase() override;
	const char *machineName() const override { return "HordeAIUpdateMachine"; }
};
