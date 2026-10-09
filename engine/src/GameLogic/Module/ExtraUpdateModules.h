// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0 (DeletionUpdate.h is the donor of the shape; every field and
// step below is RotWK's).
//
// Lane MODULES-1: update modules the base game uses that no lane ported yet.
//
// TARGET FACTS (RotWK game.dat, caveat S-001; registry golden engine/data/rotwk-201/module-registry.json, field-tables.json):
//   * DeletionUpdate (create RW 0x64CA1E, constructor RW 0x88B7B0, update vtable RW 0xC61B18; data table RW 0xC07AD0: MinLifetime (+8), MaxLifetime (+0xC),
//     parseDurationUnsignedInt). Constructor: m_dieFrame (+0x20) = 0, then calcSleepDelay(min, max) (RW 0x88B737: GameLogicRandomValue(min, max) with the
//     source file DeletionUpdate.cpp line 0x37 (RW 0x6D328E), at least 1; m_dieFrame = now + delay) and the update sleeps that long (RW 0x850C32).
//     update RW 0x88B84F: TheGameLogic->destroyObject(object) (RW 0x62BBAB: a silent removal, no death), sleep forever.
//   * MonitorConditionUpdate (create RW 0x64D4EA, constructor RW 0x894CBF, update vtable RW 0xC6423C; table RW 0xC64170: ModelConditionFlags (+8, RW 0x4B8C21,
//     the 19 word model condition set), ModelConditionCommandSet (+0x54), WeaponSetFlags (+0x58, RW 0x6C9951), WeaponToggleCommandSet (+0x68)). The module
//     keeps the command set it displaced (+0x20, empty at first). update RW 0x894DF8 (every frame, returns 1):
//       1. the object's model condition (+0x10C) holds ANY ModelConditionFlags bit (RW 0x6632E9): nothing when the object's command set (RW 0x69156B) is
//          ModelConditionCommandSet; else, when it is not WeaponToggleCommandSet, it is saved; the override becomes ModelConditionCommandSet (RW 0x693B94);
//       2. else the weapon set flags (Object + 0x38C, RW 0x68BE7D) hold ANY WeaponSetFlags bit (RW 0x75CDC4): the same with the two names swapped;
//       3. else, with a saved set that is not the current one, the override becomes the saved set and the saved set is emptied;
//     each change marks the control bar dirty (RW 0xDE7744 + 0x28 = 1, client).
// WHAT IS NOT PORTED (stop S-982): RW 0x693B94's notification of a skirmish AI player (RW 0x6AA5B3 / 0x8E3D1E(object, 2)) when the override changes (as
// CommandSetUpgrade, lane UPGRADE-1); the control bar refresh is the client's.

#pragma once

#include "Common/INI.h"
#include "GameLogic/Module/UpdateModule.h"

#include <array>
#include <cstdint>
#include <string>

class DeletionUpdateModuleData : public ModuleData
{
public:
	unsigned m_minLifetime = 0; ///< +8 (frames)
	unsigned m_maxLifetime = 0; ///< +0xC
	static void buildFieldParse(MultiIniFieldParse &p);
};

class DeletionUpdate : public UpdateModule
{
public:
	DeletionUpdate(Thing *thing, const DeletionUpdateModuleData *data);
	UpdateSleepTime update() override;
	// lane SPELL-2: RW 0x88B830 (the special power's view object, RW 0x896FD9): calcSleepDelay(min, max) again (one more draw) and the sleep
	void setLifetimeRange(unsigned minFrames, unsigned maxFrames);
	unsigned dieFrame() const { return m_dieFrame; }
	void crc(StateHasher &hasher) const override;

private:
	unsigned calcSleepDelay(unsigned minFrames, unsigned maxFrames); ///< RW 0x88B737
	const DeletionUpdateModuleData *m_data;
	unsigned m_dieFrame = 0; ///< +0x20
};

class MonitorConditionUpdateModuleData : public ModuleData
{
public:
	std::array<std::uint32_t, 19> m_modelConditionFlags{}; ///< +8
	std::string m_modelConditionCommandSet;                ///< +0x54
	std::array<std::uint32_t, 4> m_weaponSetFlags{};        ///< +0x58
	std::string m_weaponToggleCommandSet;                  ///< +0x68
	static void buildFieldParse(MultiIniFieldParse &p);
};

class MonitorConditionUpdate : public UpdateModule
{
public:
	MonitorConditionUpdate(Thing *thing, const MonitorConditionUpdateModuleData *data) : UpdateModule(thing, data), m_data(data) {}
	UpdateSleepTime update() override;
	const std::string &savedCommandSet() const { return m_saved; }
	void crc(StateHasher &hasher) const override;

private:
	void swapTo(const std::string &target, const std::string &other);
	const MonitorConditionUpdateModuleData *m_data;
	std::string m_saved; ///< +0x20
};
