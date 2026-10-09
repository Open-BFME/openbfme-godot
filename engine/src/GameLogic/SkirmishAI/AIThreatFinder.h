// OpenBFME. GPL-3.0.
//
// AIThreatFinder (lane AI-3, QA-1 U22): the per-player army strength around a position the skirmish AI's targets and tactics read (retail class name from the
// manager string RW 0xDC62B8 "TheThreatFinderManager"; the object is 0x5BC bytes, ctor RW 0x7EDDFB).
//
// TARGET FACTS (RotWK game.dat, caveat S-001; static disassembly):
//   * layout: + 0 .. 0x5A0 twenty records of 0x48 bytes (18 floats: + 0 the total, + 4 + 4 * c the ThreatBreakdown category c of 17), + 0x5A0 its name, + 0x5A4
//     the position, + 0x5B0 the radius, + 0x5B4 the frame of the last scan (RW 0x7EDB2B sets 0: rescan), + 0x5B8 "no structures" (the AITarget's finder is made
//     with 1, RW 0x6C6939).
//   * the query RW 0x7EE166(out, player, mode, ...): a rescan (RW 0x7EE057) when (frame - last) * LOGICFRAMES_PER_SECOND >= 3.0 (RW 0xBDD42C; every caller the
//     port has resets the frame first: always a rescan), then out = 0 (RW 0x7ED8A3) plus, for i = 0 .. 19 in order, record i when ThePlayerList's player i
//     (RW 0x6A844E) exists and player->getRelationship(player i) (RW 0x6ACEAF) is ENEMIES (mode 0, RW 0x7EDB4A) or ALLIES (mode 1, RW 0x7EDB5D); the sums
//     are SSE single adds (RW 0x7ED8D9: out[k] = record[k] + out[k]).
//   * the scan RW 0x7EE057: the records cleared (RW 0x7EDB33), then ThePartitionManager->iterateObjectsInRange(position, radius, FROM_CENTER_2D, { alive
//     (RW 0xC10E20 slot 1 = RW 0x660E71: not + 0x458 bit 0) }, ITER_FASTEST); an object whose template KindOf has any of CAN_ATTACK, HERO, SUPPORT (and STRUCTURE
//     when + 0x5B8 is 0) and not STRUCTURE when + 0x5B8 is 1 (RW 0x70C548 -> 0x661359, the any-bit test), adds its threat value to the record of its controlling
//     player's index (RW 0x68B678 -> team + 0x30 -> + 8, Player + 0x54) when the value is > 0 (RW 0x7EDB72: + 0 and its category).
//   * the threat value RW 0x68F0EC: a STRUCTURE (template KindOf + 0x108 bit 7) that CAN_ATTACK: with FS_BASE_DEFENSE (+ 0x110 bit 0) the object's paid cost
//     + 0x340 + + 0x33C (x87, returned unscaled); else the template's ThreatLevel (+ 0x52C) or 150 (RW 0xC041F8) when it is not above 100 (RW 0xBD88D8); a
//     STRUCTURE that cannot attack: 0. Any other object: f = (float)(object + 0x4B8) * 0.2f + 0.8f (RW 0xBDAD78 / 0xBDE8D8, SSE), v = + 0x33C * f + + 0x340
//     (SSE), and when v == 0 ThreatLevel * f. The value is then times the body's slot 0x14 (ActiveBody RW 0x8C1D75: health / max health, 0 without a positive
//     max; x87 at PC24).
//   * object + 0x4B8 is 1 from the constructor (RW 0x699D25, edi = 1 at RW 0x699C17); RW 0x691A2F (called from RW 0x8F98EA) writes it with a rank name at
//     + 0x4B4 and RW 0x695003 hands both to the experience tracker.
//   * the template's ThreatLevel (+ 0x52C) is 1.0 and its ThreatBreakdown category (+ 0x530, the block's AIKindOf, RW 0x73C073 / 0x8ED575) -1 from the constructor
//     (RW 0x73FE51 / 0x73FE59); RW 0x7EDB72 adds the value to + 0 and to + 4 + 4 * category, so an object without a ThreatBreakdown adds it to the total twice.
//   * the counter table RW 0x7EDC2F / 0x7ED963 (used by RW 0x6C6623, the threat of the end's merge RW 0x8F1499, and by the unported best-target gate S-1302).
// INFERENCE / NOT PORTED (stop S-1300, reported by stopLines): object + 0x4B8 is taken as its constructor value 1 (the writer RW 0x691A2F is not ported); an
// InactiveBody's health ratio is taken as 0; the ThreatBreakdown block is read from the template's raw lines (S-071), so an unknown AIKindOf name, a load error in
// retail (RW 0x8ED505 throws), leaves the category -1 here.

#pragma once

#include "Common/INIDataTypes.h"

class GameLogic;
class Object;
class Player;
class ThingTemplate;

namespace AIThreatFinder
{
constexpr int kRecords = 20; // RW 0x7EDB33 / 0x7EE166: 0x14 players
constexpr int kSlots = 18;   // 0x48 bytes: the total and 17 categories

struct ThreatRecord
{
	float v[kSlots] = {}; ///< + 0 the total, + 1 + c the category c (AI_KINDOF)
};

// RW 0x7EDB72's add (SSE): + 0 and + 1 + category (the category -1 adds to + 0 twice)
void accumulate(ThreatRecord &r, int category, float v);
// RW 0x7EDC2F(this = a, b): a's total minus b's categories through the counter table RW 0x7ED963 (consumes a's categories), as the x87 register holds it
double counterDifference(ThreatRecord &a, const ThreatRecord &b, float *aTotal, float *sumOut);
// RW 0x7EE057 + 0x7EE166: the summed record of the players whose relationship from `owner` is ENEMIES (mode 0) or ALLIES (mode 1)
ThreatRecord scanSum(GameLogic &logic, const Coord3D &position, float radius, bool noStructures, const Player &owner, int mode);

// template + 0x530: the ThreatBreakdown's AIKindOf (-1 without one)
int threatCategory(const ThingTemplate &tt);
// RW 0x68F0EC: the threat value of one object, as the x87 register returns it, and stored as a float (RW 0x7EDB83)
double threatValueWide(const Object &obj);
float threatValue(const Object &obj);

// RW 0x7EE057: the per-player records of the objects around `position` (`noStructures` is the finder's + 0x5B8)
void scan(GameLogic &logic, const Coord3D &position, float radius, bool noStructures, ThreatRecord (&records)[kRecords]);

// RW 0x7EE057 + 0x7EE166: the summed total (record + 0) of the players whose relationship from `owner` is ENEMIES (mode 0) or ALLIES (mode 1)
float threatTotal(GameLogic &logic, const Coord3D &position, float radius, bool noStructures, const Player &owner, int mode);

// RW 0x6C650B: allies minus enemies around `position` within 400 (RW 0xBDBCA0) for the target owner `owner` (0 when the allies' total is not above 0)
float relativeThreat(GameLogic &logic, const Coord3D &position, const Player &owner);

// RW 0x6C6623: the allies' total minus the enemies' values through the counter table (RW 0x7EDC2F), within 400; 0 without allies (outputs untouched)
double counteredThreat(GameLogic &logic, const Coord3D &position, const Player &owner, float &alliesOut, float &countersOut);
} // namespace AIThreatFinder
