// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0 (FlammableUpdate.h, FireSpreadUpdate.h are the donors of
// the shape: the flame damage limit, ignition, the aflame damage ticks, the burned state, the spread; every field and step below is RotWK's).
//
// Lane MODULES-1: burning.
//
// TARGET FACTS (RotWK game.dat, caveat S-001; registry golden engine/data/rotwk-201/module-registry.json, field-tables.json):
//   * FlammableUpdate (create RW 0x64CD09, constructor RW 0x88FCD8, interfaces UPDATE | DAMAGE; update vtable RW 0xC62BF8, damage vtable RW 0xC62BEC (module
//     + 0x20)). Data RW 0x890BBB, table RW 0xC62E30: BurnedDelay (+8), AflameDuration (+0xC), AflameDamageDelay (+0x10) (durations), AflameDamageAmount
//     (+0x14, int), BurningSoundName (+0x18), FlameDamageLimit (+0x1C, default 20.0), FlameDamageExpiration (+0x20, default 5 * LOGICFRAMES_PER_SECOND),
//     FireFXList (+0x28, a list), SetBurnedStatus (+0x34, default TRUE), SwapModelWhenAflame (+0x35), SwapModelWhenQuenched (+0x36), SwapTextureWhenAflame
//     (+0x37), SwapTextureWhenQuenhed [sic] (+0x38), BurnContained (+0x39), RunToWater (+0x3A), RunToWaterDepth (+0x3C), RunToWaterSearchRadius (+0x40,
//     default 200), RunToWaterSearchIncrement (+0x44, default 60), PanicLocomotorWhileAflame (+0x48), CustomAnimAndDuration (+0x4C, condition -1 / +0x50
//     time), DamageType (+0x24, default 6 = FLAME; 0 = any).
//     Module state: +0x24 status (0 NORMAL, 1 AFLAME, 2 BURNED), +0x28 aflame end frame, +0x2C burned frame, +0x30 next aflame damage frame, +0x34 the
//     burning sound handle (client), +0x38 the remaining flame damage (starts at FlameDamageLimit), +0x3C the frame of the last flame damage, +0x40 running
//     to water, +0x44 the custom anim end frame, +0x48 the last flame damager, +0x4C the panic locomotor flag. The update sleeps forever from the constructor.
//   * onDamage (damage slot 0, RW 0x8904B8): a hit whose clipped damage (D+0x74) is above 0 makes its source (D+8) the last flame damager. Then, unless the
//     object is under water (not for KindOf SHIP, template + 0x11F bit 7 = KindOf bit 191: TheTerrainLogic slot 0x4C at the object's x, y), and when
//     DamageType is 0 or the hit's type (D+0x10): a gap of more than FlameDamageExpiration frames since the last flame damage refills the remaining damage to
//     FlameDamageLimit; the frame is noted; an object neither AFLAME (status 10) nor BURNED (11) loses the hit's dealt amount (D+0x70) from the remaining
//     damage and, at or below 0, sets it to 0 and tries to ignite (RW 0x8901AB). With BurnContained and a contain module the passengers each take
//     max(AflameDamageAmount / 2, 1) FLAME damage (death BURNED) from the object; otherwise a contain module gets slot 0x88(2).
//   * onHealing (damage slot 1, RW 0x89015F): an AFLAME object whose remaining damage is below FlameDamageLimit adds the healing amount (D+0x20); reaching
//     the limit stops the burning (RW 0x88FF6F) and refills it.
//   * tryToIgnite (RW 0x8901AB): only from NORMAL: status AFLAME (10) set; the body's slot 0x2C; model condition AFLAME (bit 78, Object + 0x114 bit 14); the
//     burning sound starts (RW 0x88FE48, client); the object's FireSpreadUpdate starts spreading (RW 0x88EFA9) and its EntEnragedUpdate is told; status
//     AFLAME; the script event 10 (RW 0x7379CB); aflame end = now + AflameDuration (FOREVER without one), burned frame = now + BurnedDelay (0 without),
//     next damage = now + AflameDamageDelay (0 without); the update sleeps calcSleepDelay; SwapModelWhenAflame / SwapTextureWhenAflame set model condition
//     bits 190 / 191; the FireFXList plays (client); with a custom anim time the special model condition state, the DISABLED type 4 until now + time - 1,
//     status 0x52 and the anim end = now + time, else the anim end = now; finally the update wakes next frame.
//   * update (RW 0x890738): with an anim end set: before it, return 1; at it, clear it and (RunToWater) search water around the object (return 1). Without:
//     an object under water (not SHIP) with an AI that is not (RW 0x664485) shortens the aflame end to now + 3 * LOGICFRAMES_PER_SECOND and stops running to
//     water; the next aflame damage frame reached: the next is now + AflameDamageDelay and doAflameDamage (RW 0x88FDFA: AflameDamageAmount FLAME damage, death
//     BURNED, sub type 2, from the last flame damager); the burned frame reached with SetBurnedStatus: status BURNED and model condition BURNED (bit 80); the
//     aflame end reached: stopBurning; return calcSleepDelay.
//   * calcSleepDelay (RW 0x88FB79): 1 while running to water; for an AFLAME object with an aflame end after now, the earliest of the aflame end and of the
//     burned / damage frames that are set, after now and before it, minus now; else FOREVER.
//   * stopBurning (RW 0x88FF6F): status BURNED when the object has status BURNED, else NORMAL; aflame end = 1; the update wakes next frame; statuses 3 and 5
//     cleared; SetBurnedStatus sets status BURNED and model condition BURNED; RW 0x68F2F1(0); EntEnragedUpdate told; the quench swaps clear bits 190 / 191;
//     the script event 11; the sound stops (client); status AFLAME and model condition AFLAME cleared; the panic locomotor ends; the body's slot 0x2C.
//   * FireSpreadUpdate (create RW 0x64CB6E, constructor RW 0x88EDDA, update vtable RW 0xC627AC; table RW 0xC62828: OCLEmbers (+8), MinSpreadDelay (+0xC),
//     MaxSpreadDelay (+0x10), SpreadTryRange (+0x14)). Sleeps forever from the constructor; startFireSpreading (RW 0x88EFA9, called by the ignition): when
//     the object is AFLAME the update sleeps calcNextSpreadDelay (RW 0x88EE4A: GameLogicRandomValue(Min, Max) FireSpreadUpdate.cpp line 0x98, at least 1).
//     update RW 0x88EE6E: not AFLAME: sleep forever. The OCLEmbers list runs (RW 0x5F0126, the object as primary); with a SpreadTryRange: the closest object
//     within it (ThePartitionManager->getClosestObject RW 0xA39090, from the object's position, distance type 2 = bounding sphere 2D) through the filter
//     RW 0xC627A0 (RW 0x88ED46: the object has a FlammableUpdate whose status is NORMAL) is ignited (its FlammableUpdate's tryToIgnite); when none, with
//     TheGlobalData + 0x18 -> + 0xBC set, a burnable terrain object (RW 0x67F52B / 0x68449B) is tried; then sleep calcNextSpreadDelay.
// WHAT IS INFERENCE / NOT PORTED (stop S-983, reported per use):
//   * the client parts (burning sound, FireFXList, the drawable flushes) and the body's slot 0x2C, RW 0x68F2F1(0), the script events 10 / 11 and the
//     EntEnragedUpdate notification are not run;
//   * BurnContained's passenger damage and the contain's slot 0x88(2), CustomAnimAndDuration's disable / status 0x52, RunToWater's search and move, the panic
//     locomotor: reported when the data asks for them (no retail object does);
//   * the under-water test reads the pathfinder's terrain view (TerrainPathfindSource::isUnderwater); a game without one (no map) cannot answer and says so;
//     an object under water with an AI is not tested with RW 0x664485 (reported, the aflame end is not shortened);
//   * FireSpreadUpdate's closest-object query runs on ThePartitionManager (lane MODULES-2, S-1020); the terrain-object branch (TheGlobalData flag) is reported;
//   * the OCLEmbers list runs the position variant of OCL::create (S-980).

#pragma once

#include "Common/INI.h"
#include "GameLogic/Module/DamageModule.h"
#include "GameLogic/Module/UpdateModule.h"

#include <string>
#include <vector>

class FlammableUpdateModuleData : public ModuleData
{
public:
	unsigned m_burnedDelay = 0;               ///< +8
	unsigned m_aflameDuration = 0;            ///< +0xC
	unsigned m_aflameDamageDelay = 0;         ///< +0x10
	int m_aflameDamageAmount = 0;             ///< +0x14
	std::string m_burningSoundName;           ///< +0x18
	float m_flameDamageLimit = 20.0f;         ///< +0x1C (RW 0xBDBC6C)
	unsigned m_flameDamageExpiration = 25;    ///< +0x20: 5 * LOGICFRAMES_PER_SECOND (RW 0x890BF6)
	int m_damageType = 6;                     ///< +0x24 FLAME
	std::vector<std::string> m_fireFXList;    ///< +0x28 (client)
	bool m_setBurnedStatus = true;            ///< +0x34
	bool m_swapModelWhenAflame = false;       ///< +0x35
	bool m_swapModelWhenQuenched = false;     ///< +0x36
	bool m_swapTextureWhenAflame = false;     ///< +0x37
	bool m_swapTextureWhenQuenched = false;   ///< +0x38
	bool m_burnContained = false;             ///< +0x39
	bool m_runToWater = false;                ///< +0x3A
	float m_runToWaterDepth = 0.0f;           ///< +0x3C
	float m_runToWaterSearchRadius = 200.0f;  ///< +0x40 (RW 0xBE4170)
	float m_runToWaterSearchIncrement = 60.0f; ///< +0x44 (RW 0xBDC1F8)
	bool m_panicLocomotorWhileAflame = false; ///< +0x48
	int m_customAnimCondition = -1;           ///< +0x4C
	unsigned m_customAnimFrames = 0;          ///< +0x50
	unsigned m_customTriggerFrames = 0;       ///< +0x54
	static void buildFieldParse(MultiIniFieldParse &p);
};

class FlammableUpdate : public UpdateModule, public DamageModuleInterface
{
public:
	enum FlammabilityStatus
	{
		FS_NORMAL = 0,
		FS_AFLAME = 1,
		FS_BURNED = 2
	};
	FlammableUpdate(Thing *thing, const FlammableUpdateModuleData *data);
	DamageModuleInterface *getDamage() override { return this; }
	void onDamage(const DamageInfo &info) override;
	void onHealing(const DamageInfo &info) override;
	void onBodyDamageStateChange(BodyDamageType, BodyDamageType) override {} // RW 0x918BA0
	UpdateSleepTime update() override;
	void crc(StateHasher &hasher) const override;

	void tryToIgnite();                                      ///< RW 0x8901AB
	bool wouldIgnite() const { return m_status == FS_NORMAL; } ///< RW 0x88FBE6
	int status() const { return m_status; }
	float remainingFlameDamage() const { return m_flameDamageLimit; }
	unsigned aflameDamageTicks() const { return m_aflameDamageTicks; }

private:
	bool isUnderwater() const;
	void doAflameDamage();           ///< RW 0x88FDFA
	void stopBurning();              ///< RW 0x88FF6F
	UpdateSleepTime calcSleepDelay() const; ///< RW 0x88FB79
	const FlammableUpdateModuleData *m_data;
	int m_status = FS_NORMAL;           ///< +0x24
	unsigned m_aflameEndFrame = 0;      ///< +0x28
	unsigned m_burnedEndFrame = 0;      ///< +0x2C
	unsigned m_damageEndFrame = 0;      ///< +0x30
	float m_flameDamageLimit = 0.0f;    ///< +0x38
	unsigned m_lastFlameDamage = 0;     ///< +0x3C
	bool m_runningToWater = false;      ///< +0x40
	unsigned m_customAnimEndFrame = 0;  ///< +0x44
	ObjectID m_lastFlameDamager = INVALID_ID; ///< +0x48
	unsigned m_aflameDamageTicks = 0;   ///< how many aflame damage hits the module made (tests)
};

class FireSpreadUpdateModuleData : public ModuleData
{
public:
	std::string m_oclEmbers;     ///< +8 (empty: none)
	unsigned m_minSpreadDelay = 0; ///< +0xC
	unsigned m_maxSpreadDelay = 0; ///< +0x10
	float m_spreadTryRange = 0.0f; ///< +0x14
	static void buildFieldParse(MultiIniFieldParse &p);
};

class FireSpreadUpdate : public UpdateModule
{
public:
	FireSpreadUpdate(Thing *thing, const FireSpreadUpdateModuleData *data);
	UpdateSleepTime update() override;
	void startFireSpreading(); ///< RW 0x88EFA9
	unsigned spreads() const { return m_spreads; }
	void crc(StateHasher &hasher) const override;

private:
	UpdateSleepTime calcNextSpreadDelay(); ///< RW 0x88EE4A
	const FireSpreadUpdateModuleData *m_data;
	unsigned m_spreads = 0; ///< how many objects this module set alight (tests)
};
