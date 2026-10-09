// OpenBFME. GPL-3.0.
//
// HordeContainModuleData: the INI data of HordeContain (and, with the same table, HorseHordeContain), the
// RankInfo / MeleeBehavior / BannerCarrierPosition / SplitHorde / ComboHorde grammars. Lane HORDE-1
// (spec horde-and-movement.md 1.2-1.4, checklist step 2). ZH has no hordes: the structure is the BFME
// one, read from the RotWK binary; the decompile twin is Open-BFME-1 `Object/Contain/HordeContain*.cpp`
// (BFME1: no MeleeBehavior, no flank fields) and Open-BFME-2 `HordeContainModuleDataParse.cpp`.
//
// TARGET FACTS (RotWK game.dat; S-001 caveat):
//   * module data constructor RW 0x878EE5 (after TransportContain's RW 0x86B425), `new(0x284)` from
//     RW 0x64B610; buildFieldParse RW 0x878B63: TransportContain's table chain (which adds OpenContain,
//     the ModuleData base and TransportContain), then the HordeContain table RW 0xC5BB50 (43 rows,
//     golden tests/data/horde1/table_horde_contain.tsv).
//   * grammars (INIException code 3 unless noted): RankInfo RW 0x877385, BannerCarrierPosition RW
//     0x87242F, SplitHorde RW 0x87253E, ComboHorde RW 0x872654, MeleeBehavior RW 0x86C30A (see each
//     proc). Tokens are read with the colon separators " \n\r\t=:" except where noted.
//   * LeaderPosition (Coord3D, RW +0x200) and LeaderRank (Int, RW +0x208) overlap: the Z of a
//     LeaderPosition line IS the LeaderRank. Both are kept in one 12-byte block here (m_leaderPosition),
//     LeaderRank writing the third word, so a mod that sets both gets the retail result.
//   * the module data copies: MeleeBehavior is a shared (immutable after parse) strategy data object.
//
// Fields with no reader found in the binary (spec section 4: Leader <rank> <index>, BackUp*, CowerRadius,
// FrontAngle, FlankedDuration, RanksToJustFreeWhenAttacking, RanksToReleaseWhenAttacking in RotWK) are
// parsed and stored exactly; HordeContainModuleData::unverified() reports them (acceptance stop S-082).

#pragma once

#include "GameLogic/Module/TransportContain.h"

#include <cstdint>
#include <memory>
#include <set>
#include <vector>

// RW 0x877385: one `Position:X:<f> Y:<f> [Leader <rank> <index>]` entry (16 bytes in the binary).
struct HordeRankPosition
{
	float x = 0.0f;
	float y = 0.0f;
	int leaderRank = -1;   // -1: no leader
	int leaderIndex = 0;   // retail leaves this uninitialised when there is no Leader
};

// RW 0x8723E0 (0x38 bytes): rank +0, unit type +4, positions +8, granted +0x14, revoked +0x24, flag +0x34.
struct RankInfo
{
	int rankNumber = 0;
	std::string unitType;
	std::vector<HordeRankPosition> positions;
	WeaponConditionFlags grantedWeaponConditions{};
	WeaponConditionFlags revokedWeaponConditions{};
	bool hasWeaponConditions = false;
};

struct BannerCarrierPositionEntry  // RW 0x86DBE2 (0xC bytes)
{
	std::string unitType;
	float x = 0.0f;
	float y = 0.0f;
};

struct SplitHordeEntry  // RW 0x87253E
{
	std::string splitResult;
	std::string unitType;
	int rankNumber = 0;
};

struct ComboHordeEntry  // RW 0x872654 (0x10 bytes)
{
	std::string target;
	std::string result;
	StoreReference initiateVoice; // `InitiateVoice:`: audio event name, "NoSound" clears
	bool initiateVoiceIsEva = false;
	int evaEventIndex = -1;
};

// The strategy chosen by `MeleeBehavior = <name>` (RW 0x86C30A): one data struct for all four kinds; the
// kinds' own field tables are WaitForLeader RW 0xC870C8 and Amoeba RW 0xC87228 (Swarm and HoldGround
// have none: RW 0x9F3A3C is an empty buildFieldParse).
struct MeleeBehaviorModuleData
{
	enum Kind
	{
		SWARM = 0,
		WAIT_FOR_LEADER = 1,
		HOLD_GROUND = 2,
		AMOEBA = 3
	};

	explicit MeleeBehaviorModuleData(Kind kind);

	Kind m_kind;

	// WaitForLeader (RW ctor 0x98CCA6)
	bool m_followLeader = false;           // +4 FollowLeader
	float m_distanceToActiveLeader = 40.0f; // +8 DistanceToActiveLeader
	float m_distanceToPassiveLeader = 15.0f; // +0xC DistanceToPassiveLeader

	// Amoeba (RW ctor 0x98F780)
	float m_facingBonus = 10.0f;           // +4 FacingBonus
	float m_angleLimitCos = -0.17f;        // +8 AngleLimitCos
	float m_innerRange = 60.0f;            // +0xC InnerRange
	float m_outerRange = 90.0f;            // +0x10 OuterRange
	float m_outerRangeBuildings = 140.0f;  // +0x14 OuterRangeBuildings
	ModelConditionMask m_idleModelConditions{}; // +0x18 IdleModelConditions (default: EMOTION_TAUNTING)
	unsigned m_delayUntilIdle;             // +0x64 DelayUntilIdle (frames; default 2 * LOGICFRAMES_PER_SECOND)
	unsigned m_delayRandomActivateMin;     // +0x68 (default 2 * LOGICFRAMES_PER_SECOND)
	unsigned m_delayRandomActivateMax;     // +0x6C (default 3 * LOGICFRAMES_PER_SECOND)

	static void buildFieldParse(Kind kind, MultiIniFieldParse &p);
	static const FieldParse *getWaitForLeaderFieldParse();
	static const FieldParse *getAmoebaFieldParse();
};

struct HordeContainModuleData
{
	HordeContainModuleData(); // RW 0x878EE5

	TransportContainModuleData m_transport;           // RW base chain (first member: address of the whole)

	std::vector<RankInfo> m_rankInfo;                  // 0x18C RankInfo
	std::vector<ComboHordeEntry> m_comboHorde;         // 0x198 ComboHorde (parse only; 2.01 data has none)
	std::vector<SplitHordeEntry> m_splitHorde;         // 0x1A4 SplitHorde
	std::string m_alternateFormation;                  // 0x1B0 AlternateFormation
	std::vector<int> m_ranksThatStopAdvance;           // 0x1B4 RanksThatStopAdvance (INI order)
	std::set<int> m_ranksToReleaseWhenAttacking;       // 0x1B8
	std::set<int> m_ranksToJustFreeWhenAttacking;      // 0x1C4
	Coord2D m_randomOffset;                            // 0x1D0 RandomOffset
	bool m_thisFormationIsTheMainFormation;            // 0x1D8
	unsigned m_backUpMinDelayTime;                     // 0x1DC (frames; default LOGICFRAMES_PER_SECOND / 2)
	unsigned m_backUpMaxDelayTime;                     // 0x1E0 (default 3 * LOGICFRAMES_PER_SECOND)
	float m_backUpMinDistance;                         // 0x1E4
	float m_backUpMaxDistance;                         // 0x1E8
	float m_backupPercentage;                          // 0x1EC
	float m_cowerRadius;                               // 0x1F0
	std::vector<std::string> m_leadersAllowed;         // 0x1F4 LeadersAllowed
	struct LeaderPositionBlock
	{
		float x = 0.0f;
		float y = 0.0f;
		std::uint32_t zOrRank = 0; // LeaderPosition Z (a float's bits) or LeaderRank (an int)
	} m_leaderPosition;                                // 0x200 LeaderPosition / 0x208 LeaderRank
	std::vector<BannerCarrierPositionEntry> m_bannerCarrierPosition; // 0x20C
	std::vector<std::string> m_bannerCarriersAllowed;  // 0x218
	bool m_bannerCarrierDestroyHordeOnDeath;           // 0x224
	std::uint32_t m_bannerCarrierHordeDeathType;       // 0x228 (DeathType mask)
	std::vector<std::string> m_attributeModifiers;     // 0x22C
	bool m_isPorcupineFormation;                       // 0x238
	int m_forcedLocomotorSet;                          // 0x23C (SET_* index, -1)
	bool m_machineAllowed;                             // 0x240
	std::string m_machineType;                         // 0x244
	bool m_useSlowHordeMovement;                       // 0x248
	float m_meleeAttackLeashDistance;                  // 0x24C
	int m_evaEventLastMemberDeath;                     // 0x250 (EVA event index, -1)
	bool m_rankSplit;                                  // 0x254
	int m_splitHordeNumber;                            // 0x258
	bool m_notComboFormation;                          // 0x25C
	bool m_useMarchingAnims;                           // 0x25D
	std::shared_ptr<MeleeBehaviorModuleData> m_meleeBehavior; // 0x260 (null: the runtime creates Swarm)
	float m_frontAngle;                                // 0x264 (degrees, not converted)
	unsigned m_flankedDelay;                           // 0x268 (frames)
	unsigned m_flankedDuration;                        // 0x26C (frames; default 5 * LOGICFRAMES_PER_SECOND)
	unsigned m_minimumHordeSize;                       // 0x270
	float m_visionRearOverride;                        // 0x274
	float m_visionSideOverride;                        // 0x278
	std::uint8_t m_bannerCarrierMinLevel;              // 0x27C (a byte)
	std::string m_livingWorldOverloadTemplate;         // 0x280

	int leaderRank() const { return (int)m_leaderPosition.zOrRank; }

	// ZH style: Transport's chain, then the HordeContain table (RW 0x878B63).
	static void buildFieldParse(MultiIniFieldParse &p, unsigned extraOffset = 0);
	static const FieldParse *getFieldParse();

	// Test / tooling convenience: parse the body of a `Behavior = HordeContain <tag>` block (the lines up to
	// its End) into this data. The caller has already consumed the Behavior header line.
	void parseFromINI(INI *ini);

	// Acceptance stop report: parsed fields whose runtime reader is not located (S-082).
	static std::vector<std::string> unverified();

	static void parseRankInfo(INI *ini, void *instance, void *store, const void *userData);
	static void parseRankSet(INI *ini, void *instance, void *store, const void *userData);
	static void parseRankList(INI *ini, void *instance, void *store, const void *userData);
	static void parseComboHorde(INI *ini, void *instance, void *store, const void *userData);
	static void parseBannerCarrierPosition(INI *ini, void *instance, void *store, const void *userData);
	static void parseSplitHorde(INI *ini, void *instance, void *store, const void *userData);
	static void parseMeleeBehavior(INI *ini, void *instance, void *store, const void *userData);
	static void parseEvaEvent(INI *ini, void *instance, void *store, const void *userData);
	static void parseLeaderRank(INI *ini, void *instance, void *store, const void *userData);
};
