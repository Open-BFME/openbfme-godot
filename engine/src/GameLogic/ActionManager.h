// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// ActionManager (lane AIPOW-1): the "can this object use this power here" tests a computer player's AISpecialPowerUpdate asks before it casts at a
// location (RW 0xA021D8 -> canDoSpecialPowerAtLocation). ZH ActionManager::canDoSpecialPowerAtLocation / canDoSpecialPower are the donors of the names;
// RotWK's bodies differ (the SpecialPower flags, the per-power-type tests), so everything below is read from the RotWK binary (caveat S-001).
//
// TARGET FACTS:
//   * canDoSpecialPowerAtLocation(button, obj, target, loc, source) RW 0x75CF21 (this = the CommandButton): a button with an Upgrade (+ 0x24) whose
//     command is not FIRE_WEAPON (0x17) is about buying that upgrade (lane MODCOMPAT-AI: no upgrade entry in the object's production queue, affected by
//     the upgrade (RW 0x694914) and not having it (RW 0x691421)); else with options
//     NEED_TARGET_* (+ 0x1C & 7) a target object is required (the AI's location runs pass none: no), NEED_TARGET_POS (0x20) a location; NEED_TARGET_*
//     -> RW 0x82D925 / 0x82C3D7 (object tests), NEED_TARGET_POS -> RW 0x82DFB7(obj, loc, source, power + 0x44, 0, options, 0), FIRE_WEAPON -> RW 0x75CC3E,
//     anything else -> RW 0x82D5DA(obj, power, source, options, 0).
//   * RW 0x82D5DA (no target): a power; not RW 0x82C366 (stop S-1872); the UnitCost (power + 0x80) at most the object's horde member count (the horde
//     interface, Object + 0x258 slot 0x7C, slot 0x188); the object's module for the power (RW 0x68C26D); then the power type (+ 0x1C of the final
//     override, RW 0x688D3C) is one of the 47 targetless types of the switch at RW 0x82D6B1.
//   * RW 0x82DFB7 (a location): a power; the unit cost as above; not RW 0x82C366; the module (RW 0x68C26D) and its slot 0x48 with the location
//     (RW 0x897E01: requirementsMet RW 0x8969E5; UseDistanceFromCommandCenter (module data + 0x74) -> the location within DistanceFromCommandCenter
//     (+ 0x78, Coord3D::length) of one of the controlling player's COMMANDCENTER objects (RW 0x897D2D / 0x6B00F3); RW 0x896DB4 (power type 0x20 only:
//     stop S-1873); every special power update module of the object answering the power (slot 0x64 interface, slot 0x1C) decides with its slot 0x20:
//     SpecialAbilityUpdate RW 0x851B6E = its RequiredConditions / RejectedConditions (RW 0x851551), the garrison types 0x27 / 0x28 aside (S-1873));
//     RW 0x82D56A: strictly inside TheTerrainLogic vslot 0x30's extent shrunk by RadiusCursorRadius (+ 0x54, SSE); RW 0x82D3A3 (LIMIT_DISTANCE, flag
//     bit 3): MaxCastRange (+ 0x74) squared above the object's squared 3D distance to the location (SSE: dz^2 + dy^2 + dx^2); RW 0x82D2C9
//     (PATHABLE_ONLY, bit 6): the pathfinder's quick path test RW 0x6F5BB0 from the object; RW 0x82D415 (unless WATER_OK, bit 1): r = cvttss2si
//     (RadiusCursorRadius); r < 1 -> the ground cell under the location is not water (cell types 1 / 7, RW 0x6EACDF); else every point (x, y) of the
//     integer grid with step 2 over [cvttss2si(loc - r), cvttss2si(loc + r)] strictly inside the circle ((x - lx)^2 + (y - ly)^2 < r * r, SSE on
//     cvtsi2ss values) has no water cell; RW 0x82DEA6 (NEEDS_OBJECT_FILTER, bit 2): some live object within RadiusCursorRadius (distance type 0) passes
//     ObjectFilter (+ 0x60) for the controlling player (RW 0xBE4CC8); RW 0x82D2FD (NO_FORBIDDEN_OBJECTS, bit 4): no live object within
//     ForbiddenObjectRange (+ 0x7C, distance type 1) passes ForbiddenObjectFilter (+ 0x78). Then the type tests of RW 0x82E135 .. 0x82E8F3 and the
//     final switch RW 0x82E913 (types 10 .. 0x9E, table RW 0x82EE0D / 0x82EDE1): the shroud status of the location for the controlling player
//     (ShroudManager RW 0xB4D9A0, player index + 0x54) CLEAR / not SHROUDED, always, or the type's own test; the other types answer no.
//   * RW 0x82D925 canDoSpecialPowerAtObject(obj, target, source, power, options, checkSourceRequirements) (lane MODCOMPAT-MOD1, for
//     CommandButtonHuntUpdate's scan RW 0x898C17; BFME2 decomp ActionManager 0x0041CFCC same shape): a power; with checkSourceRequirements the
//     object has a special power of the power's type (RW 0x68DF46: the Object + 0x3C0 bits); the module (RW 0x68C26D); a set ObjectFilter
//     (power + 0x60, RW 0x762977) must allow the target for the object's controlling player (RW 0x7640C1); a capture type (0x1A / 0x1D) may target
//     an effectively dead object (Object + 0x458 bit 0) when the filter is set or the capture test RW 0x82D0A1 passes, any other power may not;
//     no target: no; UnitCost (power + 0x80) at most the horde's member count (slot 0x188); not RW 0x82C366; then the relationship
//     (RW 0x68D7AB) is taken; the module must exist and, with checkSourceRequirements, be fully ready (vslot 8 >= 1.0, x87 fcomip) and allow
//     the use (vslot 0x48 = RW 0x897E01 with no location); not RW 0x82C167 (the shroud test, scripts exempt; the fake tree ids 0x5F5E0FC ..
//     0x5F5E0FF skip it); then the switch on the type (RW 0x82DAE9, types 0x10 .. 0x9D): 0x20, 0x2A, 0x2D, 0x31, 0x3B, 0x40, 0x47, 0x81, 0x84,
//     0x93, 0x97 yes; 0x39, 0x44, 0x45, 0x90 the target is not AIRBORNE_TARGET (Object + 0x94 bit 6); 0x82 an ENEMY, or an ALLY / NEUTRAL that
//     is DEPLOYED_MINE (template + 0x11D bit 4); 0x27 / 0x28 the grab test (RW 0x82DB6A .. 0x82DC80, lane MODCOMPAT-MOD1: see grabObjectTest; its
//     fake tree branch RW 0x82DB01 is S-7140); 0x1D the capture test RW 0x82D0A1, 0x33 RW 0x82D780 with a mobile target that has an AI, 0x8A a
//     STRUCTURE's RW 0x82D7F2 (canRepairObject): not ported (S-7140, answer no, counted per type); any other type no.
// Each type test this file does not port answers no, counted and reported as stop S-1874 (stopLines()); the object form's as S-7140. TheTerrainLogic vslot 0x54 is the third bit plane
// of the map (WorldHeightMap + 0x74, PathfindTerrain::thirdPlane).

#pragma once

#include "Common/INIDataTypes.h"
#include "GameLogic/AI/AICommandSink.h"
#include "GameLogic/ObjectTypes.h"

#include <array>
#include <string>
#include <vector>

class CommandButton;
class Object;
class SpecialPowerTemplate;

namespace ActionManager
{
// RW 0x75CF21 with no target object and source 0 (the AI's location run, RW 0xA021D8)
bool canDoSpecialPowerAtLocation(Object &obj, const CommandButton &button, const Coord3D &loc);
// lane MODCOMPAT-AI: RW 0x75CF21 with no target object and no location (TEAM_PARTIAL_USE_COMMANDBUTTON, RW 0x7C7D95, source 1)
bool canUseCommandButton(Object &obj, const CommandButton &button);
// RW 0x82DFB7(obj, loc, 0, power, 0, options, 0); lane SPELL-4: `unported` (when given) is set when the answer no came from a per-type test this file does
// not port (S-1874), not from a test that was run
bool canDoSpecialPowerAtLocation(Object &obj, const Coord3D &loc, const SpecialPowerTemplate *power, unsigned options, bool *unported = nullptr);
// RW 0x82D5DA(obj, power, 0, options, 0)
bool canDoSpecialPower(Object &obj, const SpecialPowerTemplate *power);
// RW 0x82D925 (see above); `unported` (when given) is set when the answer no came from a per-type test this file does not port (S-7140)
bool canDoSpecialPowerAtObject(Object &obj, Object *target, CommandSourceType source, const SpecialPowerTemplate *power, unsigned options,
                               bool checkSourceRequirements, bool *unported = nullptr);

// tests: the final test of RW 0x82E913 for a power type (0 no, 1 shroud CLEAR, 2 yes, 3 not SHROUDED, 4 the cliff grid 0x48 .. 0x4A, 5 the range 0x92, 6 the
// unported 0x27 / 0x57, 7 the plane grid 0x59 / 0x68, 8 the army test 0x63) and whether RW 0x82D6B1 accepts a targetless type
int finalTestKind(int type);
bool targetlessTypeAccepted(int type);

// diagnostics (not state): the tests that ran into an unported part (S-1871 .. S-1874), by the power type of RW 0x82DFB7's per-type tests
struct Stats
{
	unsigned long long fireWeaponButtons = 0, movingOnLayer = 0, typeTests = 0;
	std::array<unsigned long long, 0xA0> unportedTypeTests{};
	unsigned long long objectTypeTests = 0;                          ///< RW 0x82D925's unported per-type tests (S-7140)
	std::array<unsigned long long, 0xA0> unportedObjectTypeTests{};  ///< by power type
};
Stats &stats();
std::vector<std::string> stopLines();
} // namespace ActionManager
