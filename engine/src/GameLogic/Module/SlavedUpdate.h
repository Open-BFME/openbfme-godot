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
//   * update (RW 0x8A5D00), every SLAVED_UPDATE_RATE = LOGICFRAMES_PER_SECOND / 4 = 1 frame (RW 0xDE95FC, set by RW 0xBC6E11) while not repairing:
//     no slaver: sleep 1; + 0x44 set: sleep forever; DieOnMastersDeath and the slaver gone or dead: the player's RW 0x79F486 (+ 0x3DC, not identified) and
//     kill (RW 0x698EC3(8, 0)), sleep forever; no AI or no locomotor (AI + 0x1F0): sleep 1; a dead guard target clears; the slaver gone or dead: onSlaverDie,
//     sleep 1; the slaver's team not ALLIES with ours (RW 0x7A3DAD): the object takes the slaver's team (RW 0x6996DC(team, 0)), onSlaverDie, sleep forever;
//     StayOnSameLayerAsMaster; the slaver's weapon bonus bit 6 (+ 0x39C) clears; the slaver's victim (RW 0x668303); the slaver's health percent (only with
//     RepairRatePerSecond > 0); repair (health <= RepairWhenBelowHealth%); AttackRange and a victim: endRepair, attack logic (RW 0x8A5319); ScoutRange and the
//     slaver's path: scout (RW 0x8A54DD) when the path's end is more than (GuardMaxRange / 2)^2 away (RW 0x6CA525); health < 100: repair; the guard point =
//     the slaver's transform times GuardPositionOffset (SSE); FadeOutRange: the drawable's fade (client); GuardMaxRange: a guard target moves the object to
//     it (RW 0x8A567E(pos, 0)); else an idle AI more than 225.0 (RW 0xDB0CB0) from the guard point, or within it but farther than GuardMaxRange^2 from the
//     slaver: RW 0x8A567E(guard point, 0); a busy AI with a victim (RW 0x668303) farther than GuardMaxRange^2 from the slaver: RW 0x8A567E(guard point, 1);
//     LeashRange: a busy AI within LeashRange^2 of the slaver stays; else (busy beyond it or idle) a busy AI gets LEASHED_RETURNING (status 30, model
//     condition 342), then RW 0x8A567E(guard point, 0). The distances are RW 0x5E3BE4 (x87 dx^2 + dy^2 + dz^2) against the int squares (fild).
//   * RW 0x8A567E(pos, idleFirst): LEASHED_RETURNING within GuardWanderRange^2 of pos clears (the status and the model condition); GuardWanderRange:
//     pos += (cos a, sin a) * GuardWanderRange, a = GameLogicRandomValueReal(0, 2 pi) line 0x21F, the offset's z the ground height there; the AI:
//     idleFirst: aiIdle(CMD_FROM_AI) then aiMoveToPosition(pos, CMD_FROM_AI) (RW 0x5E821A / 0x66C4CA); else AI command 15 (ZH ATTACKMOVE_TO_POSITION,
//     RW 0x696266(pos, ?, CMD_FROM_AI)).
//   * the attack logic RW 0x8A5319 (ZH doAttackLogic): the victim's position, or the slaver's position plus AttackRange along slaver -> victim when the
//     victim is beyond AttackRange^2 (RW 0x6CA525 from the object); AttackWanderRange: the random offset (line 0x1B5); aiMoveToPosition(CMD_FROM_AI);
//     within DistToTargetToGrantRangeBonus^2 the slaver's weapon bonus bit 6 sets.
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
