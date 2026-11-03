// OpenBFME. GPL-3.0.
// Ported from the RotWK 2.01 binary (caveat S-001); BFME2 decomp cross-check (decomp-progress/bfme2 Code/GameEngine/Source/GameLogic/Object/Update/
// BoredUpdate*.cpp: the same fields and steps). ZH has no such class.
//
// BoredUpdate (lane MODCOMPAT-MOD1, census rank 9): an idle troll now and then clubs a troll friend next to it (the Mountain Troll's and the Cave
// Troll's SpecialAbilityMountainTrollBored, a WeaponFireSpecialAbilityUpdate).
//
// TARGET FACTS (RotWK game.dat; registry golden engine/data/rotwk-201/module-registry.json: registration site RW 0x6593E7, table RW 0xC65CC0):
//   * data: createData RW 0x64DDFB (0x1C bytes), constructor RW 0x899E8C (vtable RW 0xC65C38): ScanDelayTime + 0x08 = LOGICFRAMES_PER_SECOND (RW 0xD9F608),
//     ScanDistance + 0x0C = 300.0 (RW 0xBD9E90), BoredFilter + 0x10 (RW 0x76406F, then RW 0x763D11: NONE with empty masks, not valid), CanScanWhileAttacking
//     OrMoving + 0x14 = false, SpecialPowerTemplate + 0x18 = null. Table RW 0xC65CC0: ScanDelayTime (parseDurationUnsignedInt RW 0x73A429), ScanDistance
//     (parseReal RW 0x42ED00), BoredFilter (ParseObjectFilter RW 0x76392F), CanScanWhileAttackingOrMoving (parseBool RW 0x42E558), SpecialPowerTemplate
//     (RW 0x73B22F: the template by name).
//   * module: create RW 0x64DDC3 (0x24 bytes), constructor RW 0x899F1F (UpdateModule RW 0x653114; vtables RW 0xC65C04 / 0xC67300 / 0xC65BF8): the scan
//     countdown + 0x20 = GameLogicRandomValue(0, ScanDelayTime) (RW 0x6D328E, line 0x46); no wake call.
//   * update RW 0x899F9D: no SpecialPowerTemplate, or an effectively dead object (+ 0x458 bit 0) -> 1. "Allowed" = CanScanWhileAttackingOrMoving or
//     neither model condition ATTACKING (0x25) nor MOVING (0x3D) (RW 0x46E918); the countdown RW 0x8B66B4 runs first (0 -> reloaded with ScanDelayTime,
//     the scan runs; else it counts down); then, allowed: BoredFilter valid (RW 0x762977) -> the closest object (RW 0xA39090, distance type 0) within
//     ScanDistance allied to the object (RW 0xC11DC0(obj, mask 4, 0): the object's relationship to it, RW 0x68D7AB, is ALLIES), not the object itself
//     (RW 0xC1D660 -> 0x661220) and allowed by BoredFilter for the controlling player (RW 0xBE4CC8, flag 1); with one, ActionManager
//     canDoSpecialPowerAtObject (RW 0x82D925(obj, it, CMD_FROM_AI, power, 2, 1)) -> Object::doSpecialPowerAtObject (RW 0x68E67A(power, it, 2, 0)). BoredFilter
//     not valid: canDoSpecialPower (RW 0x82D5DA(obj, power, CMD_FROM_AI, 2, 1)) -> Object::doSpecialPower (RW 0x68E5A3(power, 2, 0)). Returns 1.
//   * xfer RW 0x8B0125 (shared): UpdateModule (RW 0x850C60), then (Xfer slot 0x10 false) version 1 and the countdown (+ 0x20, unsigned).
// WHAT IS INFERENCE (OpenBFME): the snapshot is OpenBFME native (S-4190): RW's field in RW's order.
// NOT PORTED (stop S-7161, noted when reached): the targetless form (BoredFilter not valid: RW 0x82D5DA with source CMD_FROM_AI and checkReady 1, whose
// ready / requirement half the port's canDoSpecialPower does not run; no 2.01 template leaves BoredFilter unset). The at-object test's unported types are
// ActionManager's S-7140 (the lane's at-object test, ported with CommandButtonHuntUpdate).

#pragma once

#include "Common/GameCommon.h"
#include "Common/INI.h"
#include "GameLogic/Module/UpdateModule.h"
#include "GameLogic/ObjectFilter.h"

#include <string>
#include <vector>

class ModuleFactory;
class SpecialPowerTemplate;
class StateHasher;

class BoredUpdateModuleData : public ModuleData
{
public:
	unsigned m_scanDelayTime = LOGICFRAMES_PER_SECOND;           ///< + 0x08 (frames)
	float m_scanDistance = 300.0f;                               ///< + 0x0C
	ObjectFilter m_boredFilter = ObjectFilter::none({}, {});     ///< + 0x10 (RW 0x763D11: not valid)
	bool m_canScanWhileAttackingOrMoving = false;                ///< + 0x14
	const SpecialPowerTemplate *m_specialPowerTemplate = nullptr; ///< + 0x18
	std::string m_specialPowerTemplateName;                      ///< (the name RW 0x73B22F looked up)
	static void buildFieldParse(MultiIniFieldParse &p);
};

class BoredUpdate : public UpdateModule
{
public:
	OBFME_MODULE_XFER(BoredUpdate)
	BoredUpdate(Thing *thing, const BoredUpdateModuleData *data); ///< RW 0x899F1F
	UpdateSleepTime update() override;                            ///< RW 0x899F9D
	void crc(StateHasher &h) const override;

	const BoredUpdateModuleData *data() const { return m_data; }
	unsigned countdown() const { return m_countdown; } ///< + 0x20
	// observation only (tests; not state, not hashed): the scans the countdown let through and the powers used
	unsigned long long scans() const { return m_scans; }
	unsigned long long uses() const { return m_uses; }

	static void registerModule(ModuleFactory &modules);
	static std::vector<std::string> stopLines();

private:
	const BoredUpdateModuleData *m_data;
	unsigned m_countdown = 0; ///< + 0x20
	unsigned long long m_scans = 0;
	unsigned long long m_uses = 0;
};
