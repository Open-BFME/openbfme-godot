// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0 (GameLogic/Object/Object.cpp doCommandButton*).
//
// Lane SCRIPT-4: Object::doCommandButton / doCommandButtonAtObject / doCommandButtonAtPosition as the map scripts run them (NAMED_ / TEAM_USE_
// COMMANDBUTTON_ABILITY[_ON_NAMED / _AT_WAYPOINT], through AIGroup RW 0x76F9FF / 0x76FA59 / 0x76FA2A for a team). Free functions, so Object.h does not
// grow; the AI's own copies (AISpecialPowerUpdate::runButton, AutoAbilityBehavior::doCommand) are left as they are.
// Lane MODCOMPAT-AI: the three functions run Object::doCommandButton* (lane MODS-3, ObjectDoCommandButton.cpp: the fuller port of the same retail
// functions, production, upgrades and castle pack / unpack included); what that port lacks is stop S-5305. The facts below are SCRIPT-4's first reading.
//
// TARGET FACTS (RotWK game.dat, caveat S-001; BFME2 decomp GameLogic/Object/Object.cpp:845 / rva00297000 / :1162, tier B; the command numbers are
// GUICommandType's):
//   * RW 0x696FD2 doCommandButton(button, source, auto): a disabled object (RW 0x9325B4) does nothing unless Object + 0xA0 bit 6 is set (RotWK only; an auto
//     call asks the AutoAbilityBehavior's disabled types instead); a null button nothing. 24 SPECIAL_POWER / 38 SPELL_BOOK: the button's power (+ 0x44)
//     -> Object::doSpecialPower(power, options | 0x40000 (| 0x20000000 auto), force = source is CMD_FROM_SCRIPT) (RW 0x68E5A3); 14 STOP: the AI idles
//     (RW 0x5E821A, source); 45 TOGGLE_WEAPONSET: RW 0x696FD2 case 0x2D (WeaponSetToggle, lane AIPOW-1); 58 SET_STANCE: the StancesBehavior takes the
//     button's first stance (RW 0x8620DD). Cases this port has not got: 1, 3, 6, 7, 8, 16, 17, 23, 27, 33-36, 39, 40, 42-44, 46, 49-53, 59, 60. Any other
//     command has no case: nothing (57 TOGGLE_STANCE among them).
//   * RW 0x697890 doCommandButtonAtObject(button, target, source, auto): the disabled gate without the + 0xA0 exception; 14 STOP; 24 / 38 ->
//     doSpecialPowerAtObject (RW 0x68E67A); 29 -> aiEnterObject(target, source) (RW 0x66C5A4); 23 FIRE_WEAPON not ported here; anything else nothing
//     (UNIT_BUILD included).
//   * RW 0x6979D9 doCommandButtonAtPosition(button, pos, source, auto): a disabled object does nothing (no exception); 14 STOP; 24 / 38 ->
//     doSpecialPowerAtLocation (RW 0x68E754); 10 ATTACK_MOVE, 23 FIRE_WEAPON, 53 DOZER_CONSTRUCT not ported here; anything else nothing.
//
// INFERENCE / NOT PORTED (S-1190): Object + 0xA0 bit 6 is read as clear (as lane AIPOW-1); the commands listed as not ported return false so the caller
// reports them; the horde step of Object::doSpecialPower* (RW 0x68CB85) is SpecialPowerModules' (not ported there).

#pragma once

#include "GameLogic/AI/AICommandSink.h"
#include "GameLogic/ObjectTypes.h"

class CommandButton;
class Object;

namespace ObjectCommandButton
{
// false when the button's command has a retail case this port does not run (the caller reports it); true otherwise (it ran, or retail does nothing)
bool doCommandButton(Object &obj, const CommandButton *b, CommandSourceType source);                                 // RW 0x696FD2
bool doCommandButtonAtObject(Object &obj, const CommandButton *b, Object *target, CommandSourceType source);         // RW 0x697890
bool doCommandButtonAtPosition(Object &obj, const CommandButton *b, const Coord3D &pos, CommandSourceType source);   // RW 0x6979D9
} // namespace ObjectCommandButton
