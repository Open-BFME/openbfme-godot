// OpenBFME. GPL-3.0.
// Ported from the RotWK 2.01 binary (caveat S-001). ZH has no such class.
//
// SpawnUnitBehavior: a structure that offers a unit (UnitName: the build assistant's query, S-207) and, with UnitCommand, presses that command button itself every
// second while its own queue is empty and its player can afford the unit's command points (SpawnOnce: once); a queue of two or more that starts with the unit
// drops that first entry. In 2.01 only the Army of the Dead's egg (GondorArmyofTheDeadEgg) names a UnitCommand, Command_ConstructRohanOathbreakerHorde, whose
// block commandbutton.ini has commented out: its press finds no button; the fortresses' drake and eagle only name the unit. Edain 4.8.5.4 uses it more.
//
// TARGET FACTS (RotWK game.dat 2.01, a clean copy; S-207's offer was first read in a modified image, the addresses below are the clean binary's):
//   * data: defaults RW 0x65573A, table RW 0xC6FEF0 (RW 0x8BC981): UnitName + 0x08, UnitCommand + 0x0C, SpawnOnce + 0x1C;
//   * module: RW 0x6506C0 -> constructor RW 0x8BC924 over UpdateModule RW 0x653114: + 0x24 = the data's SpawnOnce (whether the one press of SpawnOnce is still
//     to come); vtables RW 0xC6FEA4 (module), 0xC6FDE8 (behavior), 0xC6FDDC (update), 0xC6FDD8 (the SpawnUnitBehaviorInterface at + 0x20: slot 0 RW 0x8BC992
//     copies UnitName);
//   * update RW 0x8BC9B0, every return UPDATE_SLEEP(LOGICFRAMES_PER_SECOND) ([RW 0xD9F608] = 5):
//       - nothing without a controlling player (RW 0x68B678), before logic frame 5 (TheGameLogic + 0x40), while the player's Player + 0x755 is clear (the flag
//         of the script action TOGGLE_AUTO_BUILD, 418: RW 0x7BDBAB; 1 from RW 0x6B04E9), or while the object has the model condition AWAITING_CONSTRUCTION (67)
//         or ACTIVELY_BEING_CONSTRUCTED (69) (Object + 0x114 bits 3 and 5);
//       - with a production queue (RW 0x68C327(0)) that holds anything (slot 17): one entry: nothing; two or more: the first (slot 21) is cancelled (slot 10,
//         its production id) when it is a unit whose template name is UnitName (AsciiString::compare RW 0x4065AA, case sensitive), so the pressed unit does not
//         hold up what the player queued behind it;
//       - else: UnitName's template (TheThingFactory RW 0x6D1305), and the player's command points can afford it (Player + 0x60, RW 0x6A7F79); UnitCommand's
//         button (TheControlBar RW 0x71D6EA); with SpawnOnce and the press already made: nothing; a button is pressed (Object::doCommandButton RW 0x696FD2,
//         CMD_FROM_AI, not from an auto ability); + 0x24 is cleared, with a button or without;
//   * xfer RW 0x8BC8E4: version 2; from version 2 the UpdateModule block (RW 0x850C60); the byte + 0x24;
//   * BFME2 1.06: the same update instruction for instruction (0x8B886F), only TOGGLE_AUTO_BUILD's flag is Player + 0x735.
// WHAT IS INFERENCE (OpenBFME): the port has no TOGGLE_AUTO_BUILD, so a player's flag is always set (as retail's until a script clears it); retail reads the
// first entry of a non-empty queue without a null test; the snapshot is OpenBFME native (S-4190): version 2 is the UpdateModule block and + 0x24; version 1, the
// block an OpenBFME build wrote while the update was the unported shell's, still loads.

#pragma once

#include "Common/INI.h"
#include "GameLogic/Module/UpdateModule.h"

#include <string>

class ModuleFactory;

class SpawnUnitBehaviorModuleData : public ModuleData
{
public:
	std::string m_unitName;    ///< RW data +0x08, default empty
	std::string m_unitCommand; ///< RW data +0x0C, default empty
	bool m_spawnOnce = false; ///< RW data +0x1C
	static void buildFieldParse(MultiIniFieldParse &p);
};

class SpawnUnitBehavior : public UpdateModule, public SpawnUnitBehaviorInterface
{
public:
	OBFME_MODULE_XFER(SpawnUnitBehavior)
	SpawnUnitBehavior(Thing *thing, const SpawnUnitBehaviorModuleData *data); ///< RW 0x8BC924
	UpdateSleepTime update() override;                                         ///< RW 0x8BC9B0
	void crc(StateHasher &hasher) const override;

	SpawnUnitBehaviorInterface *getSpawnUnitBehaviorInterface() override { return this; }
	const std::string &getUnitName() const override { return m_data->m_unitName; } ///< RW 0x8BC992

	const SpawnUnitBehaviorModuleData *spawnData() const { return m_data; }
	bool oncePressPending() const { return m_oncePressPending; }

	static void registerModule(ModuleFactory &modules);

private:
	const SpawnUnitBehaviorModuleData *m_data;
	bool m_oncePressPending; ///< + 0x24 (the data's SpawnOnce at first)
};
