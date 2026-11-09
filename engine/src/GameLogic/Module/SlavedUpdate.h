// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0 (Include/GameLogic/Module/SlavedUpdate.h).
//
// SlavedUpdate (lane MOD-4, QA-1 U2): the creeps a lair spawns (SpawnBehavior) stay near it, guard it and die with it. Ported from the RotWK binary (caveat
// S-001); DONOR: ZH SlavedUpdate.cpp (the same shape; RotWK adds DieOnMastersDeath, LeashRange, MarkUnselectable, the fade and the attack-move guard).
//
// TARGET FACTS (each read from the disassembly):
//   * create RW 0x64E865 (size 0x48), constructor RW 0x8A4F67; vtables: module RW 0xC6890C, behaviour interface RW 0xC68850 (slot 0x68 RW 0x8A18E0: the
//     slaved interface at + 0x20), update interface RW 0xC68844 (update RW 0x8A5D00), slaved interface RW 0xC68820 (slot 1 onEnslave RW 0x8A529F -> 0x8A510B,
//     slot 2 onSlaverDie RW 0x8A52A7 -> 0x8A51B9, slot 3 onSlaverDamage RW 0x8A52FB, slot 4 RW 0x9188EB (false), slot 5 UseSlaverAsControl... + 0x6D, slot 6 /
//     slot 7 set / read + 0x44, slot 8 DieOnMastersDeath + 0x55). Fields (xfer RW 0x8A51F6): + 0x24 the slaver, + 0x28 the guard offset (Coord3D), + 0x34 the
//     frames to wait, + 0x38 the repair state, + 0x3C repairing, + 0x40 a guard target, + 0x44, + 0x45 faded.
//   * data (createData RW 0x6550EE, table RW 0xC08420; the ranges are ints, RW 0x42EC5E): LeashRange + 0x08, GuardMaxRange + 0x0C, GuardWanderRange + 0x10,
//     AttackRange + 0x14, AttackWanderRange + 0x18, ScoutRange + 0x1C, ScoutWanderRange + 0x20, DistToTargetToGrantRangeBonus + 0x24, RepairRange + 0x28,
//     RepairMin / MaxAltitude + 0x2C / + 0x30, RepairRatePerSecond + 0x34, RepairWhenBelowHealth% + 0x38, RepairMin / MaxReadyTime, RepairMin / MaxWeldTime
//     + 0x3C .. + 0x48, RepairWeldingSys / FXBone + 0x4C / + 0x50, StayOnSameLayerAsMaster + 0x54, DieOnMastersDeath + 0x55, GuardPositionOffset + 0x58,
//     FadeOutRange + 0x64, FadeTime + 0x68, MarkUnselectable + 0x6C, UseSlaverAsControlForEvaObjectSightedEvents + 0x6D.
//   * onEnslave (RW 0x8A510B): the slaver kept; the offset = (cos a, sin a) * GuardMaxRange with a = GameLogicRandomValueReal(0, 2 pi) "SlavedUpdate.cpp"
//     line 0x35E (fcos / fsin RW 0x42F4E0 / 0x42F4D0, fimul, fadd, fstp); MarkUnselectable: UNSELECTABLE (status 3); the drawable's slaver link (client).
//   * onSlaverDie (RW 0x8A51B9): unless DieOnMastersDeath, the slaver and the offset clear, UNSELECTABLE and DISABLED_HELD (3) clear.
//   * update (RW 0x8A5D00 .. 0x8A6272, read linearly with tools/mod/rw.py dis), every SLAVED_UPDATE_RATE = LOGICFRAMES_PER_SECOND / 4 = 1 frame
//     (RW 0xDE95FC, set by RW 0xBC6E11) while not repairing. The class chain is registration RW 0x659A30 (name pointer 0xC0B2B8, create 0x64E865) ->
//     constructor 0x8A4F67 -> update-interface vtable 0xC68844 installed at instance + 0x10 (0x8A4F93) -> slot 0 = 0x8A5D00, called by the scheduler at
//     0x62EA97 .. 0x62EA9C. Data field StayOnSameLayerAsMaster: table 0xC08420 row 0xC08550, bool parser 0x42E558, data + 0x54 (zeroed at 0x6550CB).
//     Branches in execution order:
//       - 0x8A5D4F: ebx = the slave Object; 0x8A5D55: edi = module data; 0x8A5D5B: [ebp-4] = slave. 0x8A5D61 no slaver -> 0x8A625B (sleep 1);
//         0x8A5D6E self-task flag + 0x44 -> 0x8A6265 (sleep forever). 0x8A5D7A DieOnMastersDeath with a missing/dead master -> 0x8A5D95: player's
//         RW 0x79F486 (+ 0x3DC, not identified), kill RW 0x698EC3(8, 0), sleep forever. 0x8A5DCD no AI / no locomotor (AI + 0x1F0) -> sleep 1.
//       - 0x8A5DE6 a dead guard target clears + 0x40; 0x8A5DFD resolves the master ID and 0x8A5E0B replaces ebx with the MASTER Object. A missing/dead
//         master calls onSlaverDie (0x8A5E1C) and sleeps 1. The team call is 0x8A5E39 (RW 0x7A3DAD); only result 2 (ALLIES) reaches 0x8A5E66.
//         Otherwise 0x8A5E43 takes the master's team (RW 0x6996DC(team, 0)), calls onSlaverDie and sleeps forever. Thus the layer branch is after the
//         team gate, not in onEnslave and not in the movement helper.
//       - PORT-11: 0x8A5E66 tests data + 0x54; false jumps to 0x8A5E7C. True: 0x8A5E6C mov ecx,ebx (MASTER), 0x8A5E6E call Object::getLayer
//         RW 0x68BBE0; 0x8A5E73 mov ecx,[ebp-4] (SLAVE), push eax, 0x8A5E77 call Object::setLayer RW 0x68BB9D. The receiver swap proves the direction
//         master -> slave. Both paths then clear the MASTER's Object + 0x39C bit 6 unconditionally (0x8A5E82 .. 0x8A5E89), before any repair/attack work.
//       - 0x8A5E91 reads the master's victim (RW 0x668303); 0x8A5EA2 reads health only with RepairRatePerSecond > 0. 0x8A5EC9 health <=
//         RepairWhenBelowHealth% -> repair 0x8A5B75. 0x8A5EEB AttackRange + victim -> endRepair (0x8A5EFC) and doAttackLogic (0x8A5F06).
//         0x8A5F2C ScoutRange + a master path -> scout 0x8A54DD when its end is farther than (GuardMaxRange / 2)^2 (RW 0x6CA525).
//         0x8A5F8B health < 100 -> repair. 0x8A5FA5 builds the guard point from the master's transform and GuardPositionOffset (SSE).
//       - 0x8A5FFB FadeOutRange: drawable fade (client). 0x8A6024 GuardMaxRange: a guard target -> moveTo(target, false) at 0x8A605F; otherwise
//         0x8A607A an idle AI farther than 225.0 (RW 0xDB0CB0) from the guard point, or 0x8A60Bc within it but farther than GuardMaxRange^2 from
//         the master -> moveTo(guard point, false) at 0x8A60EF. 0x8A610A a busy AI with a victim beyond GuardMaxRange^2 -> moveTo(guard point, true).
//         0x8A616B LeashRange: busy within LeashRange^2 stays (0x8A61A0); otherwise 0x8A61B2 sets LEASHED_RETURNING on a busy AI (status 30,
//         model condition 342), then moveTo(guard point, false) at 0x8A624E. The normal tail 0x8A625B returns sleep 1; repairing returns 0 (0x8A6260).
//         Distances use RW 0x5E3BE4 (x87 dx^2 + dy^2 + dz^2) against integer squares (fild).
//   * Object layer accessors (PORT-11, read from RW, not inferred from the nearby code): getLayer 0x68BBE0 tests Object + 0x4AC; nonzero returns
//     ground (1) at 0x68BBE9, otherwise 0x68BBED returns Object + 0x428. That byte's writer is still unidentified / absent in the port (S-4851).
//     setLayer 0x68BB9D: equal layer is a no-op (0x68BBA8); an old layer other than ground calls the terrain removal vslot 0xAC (0x68BBC0);
//     0x68BBCE writes + 0x428; a drawable (+ 0xA4) calls partition refresh 0x8E26B7 (0x68BBD6). Identity is also crossed by AdjustLayer
//     0x68BAEF: it calls 0x68BBE0 at 0x68BB1F and 0x68BB9D at 0x68BB49; the drawable refresh reads the same + 0x428 through 0x68BBE0 at 0x8E27C5.
//     The port's existing Object pathfinding-layer storage is ObjectPathfindAdapter::m_layer (AIWorld::adapterFor works without an AI module);
//     AIUpdateInterface::setLayer writes that same storage, already hashed and xferred. PORT-11 uses those APIs, adds no duplicate layer state, and
//     does not change the existing terrain/partition limitations of the port's setter.
//   * Object + 0x39C bit 6 is DRONE_SPOTTING: RW name-pointer table 0xDA1740, entry 6 at 0xDA1758 points to 0xC18008 ("DRONE_SPOTTING").
//     The update clear is 0x8A5E82 and [ebx] (the master resolved at 0x8A5E0B); the matching set in doAttackLogic is 0x8A54CF on ebx (the master
//     resolved at 0x8A5336, loaded into ebx at 0x8A5379 / 0x8A5401), inside the DistToTargetToGrantRangeBonus^2 test (0x8A54BD .. 0x8A54CD).
//     Both operations affect the MASTER; the slave's mask is untouched. The compared distance is the edge-squared result of RW 0x6CA525 stored at
//     [ebp-0xC] (0x8A5351): the slave's bounding circle radius (+0xB8) is subtracted at 0x6CA536 before squaring, not the centre distance.
//   * RW 0x8A567E(pos, idleFirst): LEASHED_RETURNING within GuardWanderRange^2 of pos clears (the status and the model condition); GuardWanderRange:
//     pos += (cos a, sin a) * GuardWanderRange, a = GameLogicRandomValueReal(0, 2 pi) line 0x21F, the offset's z the ground height there; the AI:
//     idleFirst: aiIdle(CMD_FROM_AI) then aiMoveToPosition(pos, CMD_FROM_AI) (RW 0x5E821A / 0x66C4CA); else AI command 15 (ZH ATTACKMOVE_TO_POSITION,
//     RW 0x696266(pos, ?, CMD_FROM_AI)).
//   * the attack logic RW 0x8A5319 (ZH doAttackLogic): the victim's position, or the slaver's position plus AttackRange along slaver -> victim when the
//     victim is beyond AttackRange^2 (RW 0x6CA525 from the object); AttackWanderRange: the random offset (line 0x1B5); aiMoveToPosition(CMD_FROM_AI);
//     within DistToTargetToGrantRangeBonus^2 of the slave's bounding circle edge (RW 0x6CA525) the slaver's weapon bonus bit 6 sets.
//   * endRepair RW 0x8A52B2: a repair state clears (model condition 0x5E); the AI's vslot 0x238(0) and the locomotor's + 0x44 bits 3 / 6 (not identified).
// INFERENCE / NOT PORTED (stop S-1426, stopLines()): the repair path (RW 0x8A5B75 / 0x8A58FA: no retail object repairs), the scout path's slaver path end
// (the AI's path is not kept as RotWK's + 0x140), AI command 15's second argument (RW 0x68B58C(0) + 0x34) and its state (ported as the attack-move of COMBAT-1,
// S-328), the onSlaverDamage AI command 0x1D (ZH go prone: no RotWK object reacts), the AI's vslot 0x238 and the locomotor bits of endRepair, the player's
// RW 0x79F486, the fade and the drawable links (client), RW 0x6CA525's sphere variant (the bounding circle distance of ApproachMath).

#pragma once

#include "Common/INI.h"
#include "GameLogic/Module/DieModule.h"
#include "GameLogic/Module/UpdateModule.h"
#include "GameLogic/ObjectTypes.h"

#include <string>
#include <vector>

class ModuleFactory;
class Object;
class StateHasher;
struct DamageInfo;

// the slaved interface (RW vtable 0xC68820) a SpawnBehavior finds through the behaviour interface's slot 0x68
class SlavedUpdateInterface
{
public:
	virtual ~SlavedUpdateInterface() = default;
	virtual ObjectID getSlaverID() const = 0;
	virtual void onEnslave(const Object *slaver) = 0;                      // slot 1
	virtual void onSlaverDie(const DieModuleInterface::Event *info) = 0;   // slot 2
	virtual void onSlaverDamage(const DamageInfo &info) = 0;               // slot 3
	virtual bool isSelfTasking() const { return false; }                  // slot 4 (RW 0x9188EB)
	virtual bool dieOnMastersDeath() const = 0;                             // slot 8 (+ 0x55)
	// the slaved interface of `obj`'s first module that has one (the loop of RW 0x862865 / 0x862B07 over the behaviour interface's slot 0x68)
	static SlavedUpdateInterface *of(Object &obj);
};

class SlavedUpdateModuleData : public ModuleData
{
public:
	int m_leashRange = 0;                 ///< + 0x08
	int m_guardMaxRange = 0;              ///< + 0x0C
	int m_guardWanderRange = 0;           ///< + 0x10
	int m_attackRange = 0;                ///< + 0x14
	int m_attackWanderRange = 0;          ///< + 0x18
	int m_scoutRange = 0;                 ///< + 0x1C
	int m_scoutWanderRange = 0;           ///< + 0x20
	int m_distToTargetToGrantRangeBonus = 0; ///< + 0x24
	int m_repairRange = 0;                ///< + 0x28
	float m_repairMinAltitude = 0.0f;     ///< + 0x2C
	float m_repairMaxAltitude = 0.0f;     ///< + 0x30
	float m_repairRatePerSecond = 0.0f;   ///< + 0x34
	int m_repairWhenBelowHealthPercent = 0; ///< + 0x38
	unsigned m_repairMinReadyTime = 0, m_repairMaxReadyTime = 0, m_repairMinWeldTime = 0, m_repairMaxWeldTime = 0; ///< + 0x3C .. + 0x48
	std::string m_repairWeldingSys, m_repairWeldingFXBone; ///< + 0x4C / + 0x50
	bool m_stayOnSameLayerAsMaster = false; ///< + 0x54
	bool m_dieOnMastersDeath = false;     ///< + 0x55
	Coord3D m_guardPositionOffset{};      ///< + 0x58
	int m_fadeOutRange = 0;               ///< + 0x64
	unsigned m_fadeTime = 0;              ///< + 0x68
	bool m_markUnselectable = true;       ///< + 0x6C (RW 0x6550E6: the constructor sets 1)
	bool m_useSlaverAsControlForEvaObjectSightedEvents = false; ///< + 0x6D
	static void buildFieldParse(MultiIniFieldParse &p); // RW 0xC08420
};

class SlavedUpdate : public UpdateModule, public SlavedUpdateInterface
{
public:
	OBFME_MODULE_XFER(SlavedUpdate) // lane SAVE-4b
	SlavedUpdate(Thing *thing, const SlavedUpdateModuleData *data); // RW 0x8A4F67
	void onObjectCreated() override;                                 // RW 0x8A4FF2
	UpdateSleepTime update() override;                              // RW 0x8A5D00
	void crc(StateHasher &h) const override;

	ObjectID getSlaverID() const override { return m_slaverID; }
	void onEnslave(const Object *slaver) override;                  // RW 0x8A510B
	void onSlaverDie(const DieModuleInterface::Event *info) override; // RW 0x8A51B9
	void onSlaverDamage(const DamageInfo &info) override;           // RW 0x8A52FB
	bool dieOnMastersDeath() const override { return m_data->m_dieOnMastersDeath; }

	const SlavedUpdateModuleData *data() const { return m_data; }
	const Coord3D &guardOffset() const { return m_guardOffset; }
	static void registerClass(ModuleFactory &modules);
	static std::vector<std::string> stopLines();

	struct Stats
	{
		unsigned long long guardMoves = 0, attackMoves = 0, leashReturns = 0, mastersDeathKills = 0, unported = 0;
	};
	static Stats &stats();

private:
	void stopSlavedEffects();                                       // RW 0x8A51B9
	void endRepair();                                               // RW 0x8A52B2
	void doAttackLogic(Object &slaver, Object &target);             // RW 0x8A5319
	void moveTo(Coord3D pos, bool idleFirst);                       // RW 0x8A567E
	void setLeashed(bool on);

	const SlavedUpdateModuleData *m_data;
	ObjectID m_slaverID = INVALID_ID;   // + 0x24
	Coord3D m_guardOffset{};            // + 0x28
	int m_framesToWait = 0;             // + 0x34
	int m_repairState = 0;              // + 0x38
	bool m_repairing = false;           // + 0x3C
	ObjectID m_guardTargetID = INVALID_ID; // + 0x40
	bool m_selfTaskFlag = false;        // + 0x44 (slots 6 / 7)
	bool m_faded = false;               // + 0x45
};
