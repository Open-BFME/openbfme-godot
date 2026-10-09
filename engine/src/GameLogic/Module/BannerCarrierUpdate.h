// OpenBFME. GPL-3.0.
//
// BannerCarrierUpdate (lane HORDE-2): the module of a horde's banner carrier. While its horde has been out of combat long enough it brings the horde back to strength, one new
// member every IdleSpawnRate. The banner carrier itself is spawned by its horde (HordeContain, HordeBanner.cpp).
//
// TARGET FACTS (RotWK game.dat, caveat S-001; read with Ghidra):
//   * registry: create RW 0x64DE4C (ctor RW 0x89A258, 0x20 bytes), data table RW 0xC66090: IdleSpawnRate +8, MeleeFreeUnitSpawnTime +0xC, DiedRespawnTime +0x10,
//     MeleeFreeBannerReSpawnTime +0x14 (durations), MorphCondition +0x18 (RW 0x89A90F), ExpLevelDraw +0x24 (RW 0x89AA69), BannerMorphFX / UnitSpawnFX +0x30 / +0x34 (FX lists),
//     ReplenishNearbyHorde +0x38, ReplenishAllNearbyHordes +0x39 (Bool), ScanHordeDistance +0x3C (Real), UpgradeRequired +0x40 (AsciiString);
//   * update RW 0x89ACE4 (sleeps IdleSpawnRate): with UpgradeRequired empty or owned, the carrier not in model condition 69 / 544, not PENDING_CONSTRUCTION (87), not
//     TEMPORARILY_DEFECTED (62), not moving and not waiting for a path, and its horde not busy (RW 0x8B50A5 on object + 0x254): without ReplenishNearbyHorde, when the carrier
//     is not in model condition 37 / 61 and has not fired within MeleeFreeUnitSpawnTime, and its horde has not been in combat within MeleeFreeUnitSpawnTime (the horde's
//     HordeContainInterface slot 0x90 RW 0x86EE52), the horde gets a member (RW 0x89A392: when it has fewer members than slots and RW 0x89A308 allows, HordeContainInterface
//     slot 0x18C RW 0x873AE3 creates the member at the carrier's position; UnitSpawnFX plays); with ReplenishNearbyHorde RW 0x89AB8F (not ported).
// INFERENCE / NOT PORTED (stop S-587): MorphCondition / ExpLevelDraw keep their raw tokens (the grammars RW 0x89A90F / 0x89AA69 are not read; drawing only); the model
// condition / status gates are read by number from the binary's registries; RW 0x8B50A5, the AllowBannerSpawnUpgrade test of RW 0x89A308 and ReplenishNearbyHorde are not
// ported; "in combat" is a member with IS_ATTACKING or a horde melee within the window (RW 0x86EE52 asks a per-member test RW 0x68C933 that is not read); the FX are the client's.

#pragma once

#include "Common/INI.h"
#include "GameLogic/Module/UpdateModule.h"

#include <string>
#include <vector>

class ModuleFactory;

class BannerCarrierUpdateModuleData : public ModuleData
{
public:
	unsigned m_idleSpawnRate = 0;              ///< +8 (frames)
	unsigned m_meleeFreeUnitSpawnTime = 0;     ///< +0xC
	unsigned m_diedRespawnTime = 0;            ///< +0x10
	unsigned m_meleeFreeBannerReSpawnTime = 0; ///< +0x14
	std::vector<std::string> m_morphConditions; ///< +0x18 raw lines
	std::vector<std::string> m_expLevelDraws;   ///< +0x24 raw lines
	std::string m_bannerMorphFX;               ///< +0x30
	std::string m_unitSpawnFX;                 ///< +0x34
	bool m_replenishNearbyHorde = false;       ///< +0x38
	bool m_replenishAllNearbyHordes = false;   ///< +0x39
	float m_scanHordeDistance = 0.0f;          ///< +0x3C
	std::string m_upgradeRequired;             ///< +0x40
	static void buildFieldParse(MultiIniFieldParse &p);
	static void parseRawLine(INI *ini, void *instance, void *store, const void *userData);
};

class BannerCarrierUpdate : public UpdateModule
{
public:
	BannerCarrierUpdate(Thing *thing, const BannerCarrierUpdateModuleData *data);
	static void registerClass(ModuleFactory &modules);
	UpdateSleepTime update() override; // RW 0x89ACE4
	const BannerCarrierUpdateModuleData &data() const { return *m_data; }
	unsigned long long membersSpawned() const { return m_spawned; }
	void crc(StateHasher &hasher) const override;
	static const char *stopLine();

	friend struct Horde2HashAccess; // the HORDE-2 hash mutation tests (test_horde2_hash.cpp)

private:
	const BannerCarrierUpdateModuleData *m_data;
	unsigned long long m_spawned = 0;
};
