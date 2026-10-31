// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0 (Include/GameLogic/Module/CommandButtonHuntUpdate.h,
// Source/GameLogic/Object/Update/CommandButtonHuntUpdate.cpp: the names and the shape; every field and step below is RotWK's).
//
// CommandButtonHuntUpdate (lane MODCOMPAT-MOD1, census rank 7): an update module that makes a unit keep using one command button on the targets it hunts
// for. It sleeps until the map script action TEAM_HUNT_WITH_COMMAND_BUTTON (RW 0x7C46A0, ScriptActionsOrders.cpp) names a button of the unit's command
// set; from then on, while the AI's last command came from the AI itself, a weapon button hunts with that weapon locked and a special power / spell book
// button is cast at the best target its scan finds every ScanRate. The only caller of setCommandButton in RotWK 2.01 is that script action (RW 0x7C4878);
// the skirmish AI does not use it, and no 2.01 map script names the action (engine/tests/data/script-census.json), so in a vanilla game the module only
// exists and sleeps (23 templates declare it: the infantry hordes).
//
// TARGET FACTS (RotWK game.dat, caveat S-001; registry golden engine/data/rotwk-201/module-registry.json: create RW 0x64DC28, data RW 0x64DC60, table
// RW 0xC653F8; BFME2 decomp Code/GameEngine/Source/GameLogic/Object/Update/CommandButtonHuntUpdate*.cpp, the same family 0x20 .. 0x24 layout):
//   * data: RW 0x64DC60 news 0x10 bytes, constructor RW 0x898A2D: + 8 ScanRate = LOGICFRAMES_PER_SECOND ([0xD9F608] = 5 frames), + 0xC ScanRange 9999.0f
//     (RW 0xC18414). Table RW 0xC653F8: ScanRate (+ 8, parseDurationUnsignedInt RW 0x73A429), ScanRange (+ 0xC, parseReal RW 0x42ED00).
//   * module: RW 0x64DC28 news 0x28 bytes; constructor RW 0x898A5D: UpdateModule RW 0x653114, vtables RW 0xC65434 / 0xC67300 / 0xC65428 (update
//     interface), + 0x20 the button name (AsciiString::TheEmptyString), + 0x24 the button (null), then the update sleeps forever (RW 0x850C32(obj,
//     0x3FFFFFFF)).
//   * setCommandButton(name) RW 0x898EE8: + 0x20 = name, + 0x24 = null; the object's command set (RW 0x69156B -> 0x71EFA2): the first of its 33 slots
//     (RW 0x80C837, bound 0x21) whose button has a non-empty name equal to it (StringBase::compare, exact); a button found and an AI (Object + 0x260):
//     aiIdle(CMD_FROM_AI) (RW 0x5E821A(2)), update() once (the update interface's slot 0), then the update wakes next frame (RW 0x850C32(obj, 1)).
//   * update RW 0x89903C: no AI or no button -> sleep forever; the AI's last command source (vslot 0x23C) not CMD_FROM_AI (2) -> + 0x24 = null, the name
//     cleared (releaseBuffer), sleep forever; the button's command (+ 0x14): FIRE_WEAPON 0x17 / SWITCH_WEAPON 0x1B -> huntWeapon RW 0x898F8F,
//     SPECIAL_POWER 0x18 / SPELL_BOOK 0x26 -> huntSpecialPower RW 0x898FCE, anything else sleep forever.
//   * huntWeapon RW 0x898F8F: an idle AI (vslot 0x1B8) hunts (RW 0x6AF150(CMD_FROM_AI)); then Object::setWeaponLock(button + 0x80, LOCKED_TEMPORARILY)
//     (RW 0x69121A); returns 1 (every frame).
//   * huntSpecialPower RW 0x898FCE: an idle AI only (a busy one sleeps ScanRate): with a power (button + 0x44) the object's SpecialAbilityUpdate of its
//     type (RW 0x68C461 with the final override's + 0x1C) must exist (else sleep forever) and not be active (interface + 0x20 vslot 2: else sleep
//     ScanRate); then the scan RW 0x898C17 and, with a victim, Object::doCommandButtonAtObject(button, victim, CMD_FROM_AI, 0) (RW 0x697890); returns
//     ScanRate (data + 8).
//   * scanClosestTarget RW 0x898C17: no power -> null; capture = the power's type 0x1D, explosive = 0x17 or 0x19; ThePartitionManager within ScanRange
//     (RW 0xA39340, distance type 0, sorted near to far) with the filters alive (RW 0xC10E20), the playable-area bit (RW 0xC0F374: Object + 0x458 bit 3,
//     never set here, S-856) and, unless capturing, the relationship filter (RW 0xC11DC0: the hunter's ENEMIES); the AI's attack priority info (AI +
//     0x70); the object's module of the power (RW 0x68C26D, none -> null); per hit in order: capturing skips a target of the hunter's own controlling
//     player or an ALLY of it; ActionManager::canDoSpecialPowerAtObject(me, it, CMD_FROM_AI, power, 0, true) (RW 0x82D925); explosive: a MINE of the
//     hunter's player within the power's ViewObjectRange (+ 0x50) of it (RW 0xA39090, distance type 1, filters KindOf MINE / same player RW 0xC10E14)
//     skips it; dist = (float)sqrt((double)RW 0x66352C(me, it)) (the squared edge distance, CRT sqrt); priority = info ? info->getPriority(me, it, true)
//     (RW 0x7597E9, fstp float) : ScanRange - dist (SSE); exactly 0.0 skips (a NaN goes on); mod = priority - dist / AIData AttackPriorityDistanceModifier
//     (SSE divss, subss), raised to 1.0 when 1.0 > mod (comiss); mod > best takes it; then mod == best (ucomiss, not unordered) and priority > its
//     priority takes it (so a later equal mod with a higher priority wins). The best starts at 0.0 / 0.0 / null.
//   * xfer RW 0x898B7C: UpdateModule (RW 0x850C60); not a light CRC: Version1, the name (Xfer vslot 0x6C); a load re-finds the button: + 0x24 = null, then
//     with a non-empty name the command set's first slot of the 33 whose button's name equals it (no empty-name test, unlike setCommandButton).
//   * BFME2 cross-check (decomp CommandButtonHuntUpdate.cpp): the same steps; BFME2 walks 32 slots (RotWK 0x21 = 33) and its data constructor and
//     update layout are the same. ZH (donor): Int priorities, an enter hunt (hijack / carbomb / sabotage) and ZH's filters (map status, stealth).
// WHAT IS INFERENCE (OpenBFME):
//   * the button's power (+ 0x44, a pointer in RW) is resolved by name through TheSpecialPowerStore when used (the same template);
//   * the snapshot is OpenBFME native: the version, UpdateModule, the name; the button is re-found from the command set on load as RW does.

#pragma once

#include "Common/INI.h"
#include "GameLogic/Module/UpdateModule.h"

#include <string>

class AIUpdateInterface;
class CommandButton;
class ModuleFactory;
class Object;
class SpecialPowerTemplate;

class CommandButtonHuntUpdateModuleData : public ModuleData
{
public:
	unsigned m_scanFrames = 5;     ///< + 0x8 ScanRate (LOGICFRAMES_PER_SECOND, [0xD9F608])
	float m_scanRange = 9999.0f;   ///< + 0xC ScanRange (RW 0xC18414)
	static void buildFieldParse(MultiIniFieldParse &p); // table RW 0xC653F8
};

class CommandButtonHuntUpdate : public UpdateModule
{
public:
	OBFME_MODULE_XFER(CommandButtonHuntUpdate)
	CommandButtonHuntUpdate(Thing *thing, const CommandButtonHuntUpdateModuleData *data); // RW 0x898A5D
	UpdateSleepTime update() override;                                                     // RW 0x89903C
	void crc(StateHasher &hasher) const override;

	// RW 0x898EE8 (the script action TEAM_HUNT_WITH_COMMAND_BUTTON's call, RW 0x7C4878)
	void setCommandButton(const std::string &buttonName);
	// RW 0x898C17 (public for tests)
	Object *scanClosestTarget();

	const std::string &commandButtonName() const { return m_commandButtonName; }
	const CommandButton *commandButton() const { return m_commandButton; }
	const CommandButtonHuntUpdateModuleData *data() const { return m_data; }
	// what the hunt did (OpenBFME counters: tests and the hash): casts at a scanned victim, weapon-hunt frames
	unsigned long long casts() const { return m_casts; }
	unsigned long long weaponFrames() const { return m_weaponFrames; }

	static void registerModule(ModuleFactory &modules);

private:
	UpdateSleepTime huntWeapon(AIUpdateInterface &ai);       // RW 0x898F8F
	UpdateSleepTime huntSpecialPower(AIUpdateInterface &ai); // RW 0x898FCE
	const SpecialPowerTemplate *buttonPower() const;         // button + 0x44
	const CommandButton *findInCommandSet(const std::string &name, bool skipEmptyNames) const;
	const CommandButtonHuntUpdateModuleData *m_data;
	std::string m_commandButtonName;                 ///< + 0x20
	const CommandButton *m_commandButton = nullptr;  ///< + 0x24
	unsigned long long m_casts = 0;
	unsigned long long m_weaponFrames = 0;
};
