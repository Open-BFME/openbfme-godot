// OpenBFME. GPL-3.0.
//
// DetachableRiderUpdate and DetachableRiderBody (Local 24, fork issue 179; editions: both).
// Ported from the RotWK 2.01 binary (caveat S-001) and cross-checked with the BFME2 1.06 decompilation
// (Code/GameEngine/Source/GameLogic/Object/Update/DetachableRiderUpdate/*.cpp,
// Code/GameEngine/Source/GameLogic/Object/Body/DetachableRiderBody*.cpp).
//
// TARGET FACTS (RotWK 2.01 game.dat / DONOR BFME2 1.06):
//
// 1. DetachableRiderUpdate:
//   * Registration: RW 0x659FBD (create RW 0x64F603, 0x24 bytes; createData RW 0x64F63B, 0x28 bytes; type Behavior, flags 0x101).
//   * Data constructor: RW 0x8B2B75 / DONOR BFME2 0x4AEAB7 (DetachableRiderUpdateModuleDataCtor.cpp):
//     +0x08 m_deathEntries (vector of 0x54-byte DeathEntry: +0x00 ModelConditionFlags, +0x4C AnimTime = 0, +0x50 RiderOCL = nullptr,
//     element ctor RW 0x8B2B4E), +0x14 m_riderSubObjects (vector of AsciiString), +0x20 m_riderlessWeaponSlot = 0 (PRIMARY_WEAPON),
//     +0x24 m_riderlessHordeFlees = false, +0x25 m_removeRiderlessFromHorde = false.
//   * Field table: buildFieldParse RW 0x8B2FC3 / DONOR BFME2 0x4AEF05, table RW 0xC6C2E8 / DONOR BFME2 0xB46668:
//       - "DeathEntry"               -> RW 0x8B2C42 / DONOR BFME2 0x4AEB84 (parseDeathEntry)
//       - "RiderSubObjects"          -> RW 0x42EED6 (INI::parseAsciiStringVectorAppend, offset +0x14)
//       - "RiderlessWeaponSlot"      -> RW 0x42EA0C (INI::parseLookupList, userData RW 0xC16928 kWeaponSlots, offset +0x20)
//       - "RiderlessHordeFlees"      -> RW 0x42E558 (INI::parseBool, offset +0x24)
//       - "RemoveRiderlessFromHorde" -> RW 0x42E558 (INI::parseBool, offset +0x25)
//   * parseDeathEntry (RW 0x8B2C42 / DONOR BFME2 0x4AEB84):
//       1) getNextTokenOrNull(getSepsColon()) == "AnimState" (stricmp) else INIException(3, "AnimState expected");
//          getNextToken(getSepsColon()) -> INI::scanIndexList(token, TheModelConditionNames RW 0xDCD5B0) -> sets bit in entry.m_animState.
//       2) getNextTokenOrNull(getSepsColon()) == "AnimTime" (stricmp) else INIException(3, "AnimDuration expected");
//          INI::parseDurationUnsignedInt (RW 0x73A429) -> entry.m_animTime.
//       3) getNextTokenOrNull(getSepsColon()) == "RiderOCL" (stricmp) else INIException(3, "RiderOCL expected");
//          INI::parseObjectCreationList (RW 0x73A368) -> entry.m_ocl.
//   * Runtime constructor: RW 0x8B2941 / DONOR BFME2 0x4AE883:
//     m_isRiderDead (+0x20) = false, m_whichDeathEntry (+0x21) = false, setWakeFrame(obj, UPDATE_SLEEP_FOREVER).
//   * getDisabledTypesToProcess: RW 0x8B2A30 / DONOR BFME2 0x4AE972 (vtable RW 0xC6C2D0 slot 1):
//     returns 0x18 ((1 << DISABLED_HELD) | (1 << DISABLED_PARALYZED)) so update() runs while DISABLED_PARALYZED.
//   * killRider: RW 0x8B2A46 / DONOR BFME2 0x4AE988:
//     returns if m_deathEntries.empty(); picks GetGameLogicRandomValue(0, size - 1) (RW 0x8B2A6D -> RW 0x5EE573), stores
//     m_whichDeathEntry = (r != 0) (bool at +0x21, RW 0x8B2A78: test eax, eax; setne al); sets entry.m_animState on the object
//     (RW 0x5E3BA5), calls Object::setDisabledUntil(DISABLED_PARALYZED, FOREVER) (RW 0x8B2A95 -> RW 0x692432(4)), sets
//     m_isRiderDead = true (+0x20), and wakes in entry.m_animTime frames (or UPDATE_SLEEP_NONE when 0, RW 0x8B2AAF -> RW 0x850C32).
//   * restoreRider: RW 0x8B29B8 / DONOR BFME2 0x4AE8FA:
//     m_isRiderDead = false, setWakeFrame(obj, UPDATE_SLEEP_NONE).
//   * update: RW 0x8B2D59 / DONOR BFME2 0x4AEC9B:
//     clears DISABLED_PARALYZED (RW 0x8B2D74 -> RW 0x692443(4)).
//     When m_isRiderDead:
//       - hides every subobject in m_riderSubObjects on the drawable (RW 0x8B2DA8 -> RW 0x672823) and calls
//         Drawable::updateSubObjects (RW 0x8B2DCE -> RW 0x4AE5B0);
//       - if m_whichDeathEntry < m_deathEntries.size(), spawns entry.m_ocl at the object (RW 0x8B2DE6 -> RW 0x5F0126) and clears
//         entry.m_animState (RW 0x8B2DF8 -> RW 0x5E3B79);
//       - calls Object::setWeaponLock(m_riderlessWeaponSlot, LOCKED_PERMANENTLY) (RW 0x8B2E04 -> RW 0x69121A(slot, 2));
//       - sets OBJECT_STATUS_RIDERLESS (bit 39, RW 0x8B2E0F -> RW 0x62684D(0x27, 1)) and MODELCONDITION_RIDERLESS (bit 297,
//         RW 0x8B2E29 -> RW 0x68B53C);
//       - if m_removeRiderlessFromHorde: removes the object from its container (RW 0x8B2E5D -> ContainModuleInterface::removeFromContain),
//         idles its AI with CMD_FROM_SCRIPT (RW 0x8B2E8A -> RW 0x5E821A(2)), and deselects it (RW 0x8B2E9B -> RW 0x625759);
//       - else if m_riderlessHordeFlees: if contained in a HordeContain, checks whether all contained members have
//         OBJECT_STATUS_RIDERLESS and, if so, calls runOffMapOrDie (RW 0x8B2F37 -> RW 0x699282) on every member; if not contained,
//         calls runOffMapOrDie on the object itself (RW 0x8B2F45 -> RW 0x699282).
//     When !m_isRiderDead:
//       - shows every subobject in m_riderSubObjects on the drawable, calls Object::releaseWeaponLock(LOCKED_PERMANENTLY)
//         (RW 0x8B2F92 -> RW 0x68DF11(2)), and clears OBJECT_STATUS_RIDERLESS and MODELCONDITION_RIDERLESS.
//     Returns UPDATE_SLEEP_FOREVER (0x3FFFFFFF).
//   * xfer: RW 0x8B285F / DONOR BFME2 0x4AE7A2: version (1, 1), UpdateModule::xfer, m_isRiderDead, m_whichDeathEntry.
//
// 2. DetachableRiderBody:
//   * Registration: RW 0x65B25E (create RW 0x6514A3, 0x10C bytes; createData RW 0x6514DE, 0x1A0 bytes; type Body, flags 0x02).
//   * Data constructor: RW 0x8C6035 / DONOR BFME2 0x4C1E8B (DetachableRiderBodyModuleDataCtor.cpp):
//     ActiveBodyModuleData (RW 0x8C1DF6), UpgradeMuxData at +0x64 (RW 0x653173; +0x5C in BFME2 because ActiveBodyModuleData grew by 8 bytes
//     in RotWK), +0x194 m_healthPercentageWhenRiderDies = 1.0f, +0x198 m_startsActive = false, +0x19C m_riderlessDeathChance = 0.0f.
//   * Field table: buildFieldParse RW 0x8C5D2B / DONOR BFME2 0x4C1B80, table RW 0xC72A0C / DONOR BFME2 0xB4CEF4 + UpgradeMuxData RW 0xC76AD8:
//       - "HealthPercentageWhenRiderDies" -> RW 0x42EDE1 (INI::parsePercentToReal, offset +0x194)
//       - "StartsActive"                  -> RW 0x42E558 (INI::parseBool, offset +0x198)
//       - "RiderlessDeathChance"          -> RW 0x42EDE1 (INI::parsePercentToReal, offset +0x19C)
//       plus UpgradeMuxData::getFieldParse (RW 0x8D26E0, table RW 0xC76AD8 at +0x64).
//   * Runtime constructor: RW 0x8C5E2C / DONOR BFME2 0x4C1C82:
//     ActiveBody (RW 0x8C3D28) + UpgradeMux at +0x104 (RW 0x8D26E6, vtable RW 0xC729C8); if m_startsActive, calls giveSelfUpgrade (RW 0x855388).
//   * attemptDamage: RW 0x8C5EF3 / DONOR BFME2 0x4C1D49:
//     checks isAlreadyUpgraded (mux slot 0), rolls GetGameLogicRandomValueReal(0.0f, 1.0f) (RW 0x8C5F2E -> RW 0x6D332C) against
//     m_riderlessDeathChance, forces keepRider = true when MODELCONDITION_FREEFALL (bit 72) is set (RW 0x8C5F4F), and when active,
//     !keepRider, !OBJECT_STATUS_RIDERLESS (bit 39, RW 0x8C5F60) and !isDisabledByType(DISABLED_PARALYZED) (RW 0x8C5F7C):
//     computes floor = getMaxHealth() * m_healthPercentageWhenRiderDies (RW 0x8C5FB0), and if info.m_input.m_kill ||
//     health - info.m_input.m_amount <= 0.0f (RW 0x8C5FA8 .. 0x8C5FC6), clamps info.m_input.m_amount = health - floor (RW 0x8C5FD2),
//     clears info.m_input.m_kill = false (RW 0x8C5FD9), finds DetachableRiderUpdate on the object and calls killRider() (RW 0x8C6017);
//     then delegates to ActiveBody::attemptDamage(info) (RW 0x8C601F -> RW 0x8C3FA3).
//   * xfer: RW 0x8C5EB5 / DONOR BFME2 0x4C1D0B: version (1, 2), ActiveBody::xfer, UpgradeMux::upgradeMuxXfer.

#pragma once

#include "Common/INI.h"
#include "GameLogic/Module/ActiveBody.h"
#include "GameLogic/Module/UpdateModule.h"
#include "GameLogic/Module/UpgradeModule.h"
#include "GameLogic/Object/Object.h"

#include <string>
#include <vector>

class ModuleFactory;

class DetachableRiderUpdateModuleData : public ModuleData
{
public:
	struct DeathEntry
	{
		Object::ModelConditionBits m_animState{}; ///< +0x00 (19 dwords)
		UnsignedInt m_animTime = 0;               ///< +0x4C (logic frames)
		std::string m_ocl;                        ///< +0x50 (ObjectCreationList name; empty: none)
	};

	std::vector<DeathEntry> m_deathEntries;          ///< +0x08
	std::vector<std::string> m_riderSubObjects;      ///< +0x14
	int m_riderlessWeaponSlot = 0;                   ///< +0x20 (PRIMARY_WEAPON)
	bool m_riderlessHordeFlees = false;              ///< +0x24
	bool m_removeRiderlessFromHorde = false;         ///< +0x25

	static void buildFieldParse(MultiIniFieldParse &p);
};

// getModuleName RW 0x008B2859, getModuleNameKey RW 0x008B28F0, dtor RW 0x008B299C
// RW 0x008B2840 (dtor -> RW 0x0064A492)
class DetachableRiderUpdate : public UpdateModule
{
public:
	OBFME_MODULE_XFER(DetachableRiderUpdate)
	DetachableRiderUpdate(Thing *thing, const DetachableRiderUpdateModuleData *data); // RW 0x8B2941 / DONOR BFME2 0x4AE883
	~DetachableRiderUpdate() override = default;                                       // RW 0x008B299C

	DisabledMaskType getDisabledTypesToProcess() const override; // RW 0x8B2A30 / DONOR BFME2 0x4AE972
	UpdateSleepTime update() override;                           // RW 0x8B2D59 / DONOR BFME2 0x4AEC9B
	void crc(StateHasher &hasher) const override;

	void killRider();    // RW 0x8B2A46 / DONOR BFME2 0x4AE988
	void restoreRider(); // RW 0x8B29B8 / DONOR BFME2 0x4AE8FA

	bool isRiderDead() const { return m_isRiderDead; }
	bool whichDeathEntry() const { return m_whichDeathEntry; }
	const DetachableRiderUpdateModuleData *riderData() const { return m_data; }

	static void registerClass(ModuleFactory &modules);

private:
	const DetachableRiderUpdateModuleData *m_data;
	bool m_isRiderDead = false;     ///< +0x20
	bool m_whichDeathEntry = false; ///< +0x21 (RW stores bool: index != 0)
};

class DetachableRiderBodyModuleData : public ActiveBodyModuleData
{
public:
	UpgradeModuleData m_upgradeMuxData;               ///< +0x64 (RW 0x8C5D4C; +0x5C in BFME2 0x4C1BA1)
	float m_healthPercentageWhenRiderDies = 1.0f;     ///< +0x194 (RW 0x8C6060; +0x18C in BFME2)
	bool m_startsActive = false;                      ///< +0x198 (RW 0x8C606A; +0x190 in BFME2)
	float m_riderlessDeathChance = 0.0f;              ///< +0x19C (RW 0x8C6071; +0x194 in BFME2)

	static void buildFieldParse(MultiIniFieldParse &p);
};

// getModuleName RW 0x008C5D78, getModuleNameKey RW 0x008C5D8E, dtor RW 0x008C5E99;
// UpgradeMux accessors via base: getUpgradeActivationMasks RW 0x008C5DD3, requiresAllActivationUpgrades RW 0x008C5DFD,
// requiresAllConflictingTriggers RW 0x008C5E0A, performUpgradeFX RW 0x008C5E17 (empty RW 0x009F3A3C).
// RW 0x008C5D55 (dtor -> RW 0x008C3AED), RW 0x008C60BF (ActiveBody dtor thunk -> RW 0x008C35AF)
class DetachableRiderBody : public ActiveBody, public UpgradeMux
{
public:
	OBFME_MODULE_XFER(DetachableRiderBody)
	DetachableRiderBody(Thing *thing, const DetachableRiderBodyModuleData *data); // RW 0x8C5E2C / DONOR BFME2 0x4C1C82
	~DetachableRiderBody() override = default;                                     // RW 0x008C5E99

	UpgradeMux *getUpgrade() override { return this; } // RW 0x8C5D7E (behavior slot 10)
	void attemptDamage(DamageInfo &info) override;     // RW 0x8C5EF3 / DONOR BFME2 0x4C1D49
	void crc(StateHasher &hasher) const override;

	bool startsActive() const { return m_riderData->m_startsActive; }
	const DetachableRiderBodyModuleData *riderBodyData() const { return m_riderData; }

	static void registerClass(ModuleFactory &modules);

protected:
	void upgradeImplementation() override {}     // RW 0x63F3BF (mux slot 10: no-op)
	void processUpgradeRemoval() override {}     // RW 0x63F3BF (mux slot 8: no-op)

private:
	const DetachableRiderBodyModuleData *m_riderData;
};
