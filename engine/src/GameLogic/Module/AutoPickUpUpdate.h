// OpenBFME. GPL-3.0.
// Ported from the RotWK 2.01 binary (caveat S-001); BFME2 decomp cross-check (decomp-progress/bfme2 Code/GameEngine/Source/GameLogic/Object/Update/
// AutoPickUpUpdate*.cpp: the same fields and steps, the object fields 8 bytes lower there). ZH has no such class.
//
// AutoPickUpUpdate (lane MODCOMPAT-MOD1, census rank 8): a monster that grabs or eats what is near it through its SPECIAL_GRAB_PASSENGER power (the
// Watcher's head eats infantry, the Mountain / Cave Troll's buttons grab a tree, an orc or a goblin, a cinematic troll picks up a rock).
//
// TARGET FACTS (RotWK game.dat; registry golden engine/data/rotwk-201/module-registry.json: registration site RW 0x6593B2, table RW 0xC65B60):
//   * data: createData RW 0x64DD72 (0x2C bytes), constructor RW 0x899A96 (vtable RW 0xC657B0): ScanDelayTime + 0x08 = LOGICFRAMES_PER_SECOND (RW 0xD9F608),
//     PickUpFilter + 0x0C (an ObjectFilter, RW 0x76406F, then RW 0x763D11's NONE with empty masks: it allows nothing), ScanDistance + 0x10 = 300.0 (RW 0xBD9E90), EatObjectEntry + 0x14 (a vector of 12-byte
//     entries), AutoThrowObject + 0x20, RunFromButton + 0x21, RunFromButtonNumber + 0x24 = 0, CanScanWhileAttackingOrMoving + 0x28 (all false). Table RW
//     0xC65B60: ScanDelayTime (parseDurationUnsignedInt RW 0x73A429), PickUpFilter (ParseObjectFilter RW 0x76392F), ScanDistance (parseReal RW 0x42ED00),
//     EatObjectEntry (RW 0x899C7D), AutoThrowObject / RunFromButton (parseBool RW 0x42E558), RunFromButtonNumber (parseInt RW 0x42EC5E),
//     CanScanWhileAttackingOrMoving (parseBool).
//   * EatObjectEntry RW 0x899C7D: `MyHealth:<percent> TargetHealth:<percent> Filter:<object filter>`, each label matched case-insensitively, then exactly
//     (two different INIException 3 messages), MyHealth default 0 and TargetHealth 1.0 (RW 0xBD1908), parsePercentToReal RW 0x42EEFA; pushed by RW 0x899C49.
//   * module: create RW 0x64DD3A (0x2C bytes), constructor RW 0x8994BF (UpdateModule RW 0x653114; vtables RW 0xC65714 / 0xC65658 / 0xC65648, the
//     AutoPickUpUpdateInterface at + 0x20 RW 0xC65638): the scan countdown + 0x24 = GameLogicRandomValue(0, ScanDelayTime) (RW 0x6D328E, line 0x83), the
//     button flag + 0x28 = false, + 0x29 = false; no wake call.
//   * the interface (+ 0x20, behaviour slot 0x6C): slot 0 RW 0x899554 RunFromButtonNumber, slot 1 RW 0x89955B RunFromButton, slot 2 RW 0x8993FC: + 0x28 =
//     + 0x29 = true (the WAKE_AUTO_PICKUP button: Object RW 0x68C8DE wakes each module whose number is the button's: OPTION_ONE 1, TWO 2, THREE 3, else 0,
//     RW 0x697397 .. 0x6973C1).
//   * the gate RW 0x899562: RunFromButton -> the flag + 0x28; else the countdown 0 -> it is reloaded with ScanDelayTime and the scan runs, else it counts down.
//   * update RW 0x89958F: an effectively dead object (+ 0x458 bit 0) -> 1. "Allowed" = CanScanWhileAttackingOrMoving or neither model condition ATTACKING
//     (0x25) nor MOVING (0x3D) (RW 0x46E918); the gate runs first (it counts down even when not allowed), then not allowed -> 1; + 0x28 = false; then:
//       1. AutoThrowObject, contained (Object + 0x27C) and the own contain's count (contain + 0x258 slot 0x114) 1: the closest enemy within the vision range
//          (RW 0x68E43B; filters RW 0xC0F19C, 0xC1D66C, 0xC10E20, 0xC1676C mask 4) is fired at (RW 0x69200A) -> 1;
//       2. contained with an empty contain: the closest ORC (KindOf 0x6C, RW 0x797E8D -> 0x660DC6) within ScanDistance that PickUpFilter allows for the
//          controlling player (RW 0xBE4CC8, flag 1) -> SPECIAL_GRAB_PASSENGER (0x27, RW 0x691526) doSpecialPowerAtObject(it, 0x2000) -> 1;
//       3. no contain -> UPDATE_SLEEP_FOREVER;
//       4. an empty contain: each EatObjectEntry in order whose MyHealth >= the object's health ratio (body slot 0x14, x87): the closest living object
//          (RW 0xC10E20) within ScanDistance that the entry's filter allows (RW 0xBE4CC8) and whose health ratio <= TargetHealth -> the power
//          doSpecialPowerAtObject(it, 0x4000) -> 1;
//       5. an empty contain: PickUpFilter allows the KindOf mask {TREE} (0x5E, RW 0x76379C with no players): the terrain tree point within ScanDistance
//          (RW 0x67F52B(pos, ScanDistance, 1, 1)) -> the power doSpecialPowerAtLocation(point, 0x2000) -> 1; then the closest object within ScanDistance
//          that PickUpFilter allows (no alive filter) -> doSpecialPowerAtObject(it, 0x2000);
//     returns 1 (every frame).
//   * xfer RW 0x899405: version (1, 2), UpdateModule (RW 0x850C60), the countdown (+ 0x24, unsigned), + 0x28 (bool), + 0x29 (bool, version 2).
// WHAT IS INFERENCE (OpenBFME):
//   * the button number when no OPTION_ONE / TWO / THREE is set is 0 (EBX is zeroed at RW 0x696FE0 and not written on the way to RW 0x6973BE);
//   * the snapshot is OpenBFME native (S-4190): RW's fields in RW's order.
// The power the update uses is the object's GrabPassengerSpecialPower (every retail user's SPECIAL_GRAB_PASSENGER, lane MODCOMPAT-MOD1): the Watcher's
// head grabs and eats a soldier, the trolls grab an orc or a goblin.
// NOT PORTED (stop S-7160, noted when reached): step 1 (AutoThrowObject: no 2.01 template sets it) and step 5's terrain tree point (RW 0x67F52B,
// unported as in FlammableUpdate; step 5 then goes on as without a tree point); an unported special power module met before the power is reported.

#pragma once

#include "Common/GameCommon.h"
#include "Common/INI.h"
#include "GameLogic/Module/UpdateModule.h"
#include "GameLogic/ObjectFilter.h"

#include <string>
#include <vector>

class ModuleFactory;
class Object;
class SpecialPowerModuleInterface;
class StateHasher;

struct AutoPickUpEatObjectEntry
{
	ObjectFilter m_filter;       ///< + 0x00
	float m_myHealth = 0.0f;     ///< + 0x04
	float m_targetHealth = 1.0f; ///< + 0x08 (RW 0xBD1908)
};

class AutoPickUpUpdateModuleData : public ModuleData
{
public:
	unsigned m_scanDelayTime = LOGICFRAMES_PER_SECOND;     ///< + 0x08 (frames)
	ObjectFilter m_pickUpFilter = ObjectFilter::none({}, {}); ///< + 0x0C (the constructor RW 0x899A96 ends with RW 0x763D11: NONE, empty masks)
	float m_scanDistance = 300.0f;                         ///< + 0x10
	std::vector<AutoPickUpEatObjectEntry> m_eatObjectEntries; ///< + 0x14
	bool m_autoThrowObject = false;                        ///< + 0x20
	bool m_runFromButton = false;                          ///< + 0x21
	int m_runFromButtonNumber = 0;                         ///< + 0x24
	bool m_canScanWhileAttackingOrMoving = false;          ///< + 0x28
	static void buildFieldParse(MultiIniFieldParse &p);
};

// BFME1 only: field table VA 0x010BB9D0 and update RVA 0x00281F40 (Open-BFME-1
// GameLogic/Object/Update/AutoPickUpUpdateUpdate.cpp). Its KindOf mask is an ANY
// filter, and boredom is part of this update, before the ordinary pickup scan.
// BFME2/RotWK keep the table RW 0xC65B60 and their separate BoredUpdate.
class Bfme1AutoPickUpUpdateModuleData : public AutoPickUpUpdateModuleData
{
public:
	bool m_bored = false;
	ObjectFilter m_boredFilter = ObjectFilter::none({}, {});
	static void buildFieldParse(MultiIniFieldParse &p);
};

class AutoPickUpUpdate : public UpdateModule
{
public:
	OBFME_MODULE_XFER(AutoPickUpUpdate)
	AutoPickUpUpdate(Thing *thing, const AutoPickUpUpdateModuleData *data); ///< RW 0x8994BF
	UpdateSleepTime update() override;                                      ///< RW 0x89958F
	void crc(StateHasher &h) const override;

	// the AutoPickUpUpdateInterface (behaviour slot 0x6C)
	int getRunFromButtonNumber() const { return m_data->m_runFromButtonNumber; } ///< slot 0, RW 0x899554
	bool isRunFromButton() const { return m_data->m_runFromButton; }             ///< slot 1, RW 0x89955B
	void wakeFromButton() { m_buttonWake = true; m_buttonWoken = true; }          ///< slot 2, RW 0x8993FC

	const AutoPickUpUpdateModuleData *data() const { return m_data; }
	unsigned countdown() const { return m_countdown; }   ///< + 0x24
	bool buttonWake() const { return m_buttonWake; }     ///< + 0x28
	bool buttonWoken() const { return m_buttonWoken; }   ///< + 0x29
	// observation only (tests; not state, not hashed): the scans the gate let through
	unsigned long long scans() const { return m_scans; }

	// RW 0x68C8DE: every AutoPickUpUpdate of `obj` whose RunFromButtonNumber is `number` is woken
	static void wakeAutoPickUp(Object &obj, int number);

	static void registerModule(ModuleFactory &modules);
	static std::vector<std::string> stopLines();

protected:
	// BFME1's integrated boredom step; RotWK's update has no such step.
	virtual bool tryBoredAction() { return false; }

private:
	bool gate();                                     ///< RW 0x899562
	SpecialPowerModuleInterface *grabPower();        ///< RW 0x691526(0x27)

	const AutoPickUpUpdateModuleData *m_data;
	unsigned m_countdown = 0;   ///< + 0x24
	bool m_buttonWake = false;  ///< + 0x28
	bool m_buttonWoken = false; ///< + 0x29
	unsigned long long m_scans = 0;
};
