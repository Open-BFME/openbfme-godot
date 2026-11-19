// OpenBFME. GPL-3.0.
// Lane THREATLEVEL-1: the map scripts' threat level, the cached enemy threat around an object or a team that the conditions UNIT_THREAT_LEVEL (115) /
// TEAM_THREAT_LEVEL (116) test and the actions SET_COUNTER_TO_UNIT_THREAT (439) / SET_COUNTER_TO_TEAM_THREAT (440) store in a counter.
//
// TARGET FACTS (RotWK 2.01 rotwk201_game.exe, caveat S-001; read on the bytes):
//   * Object RW 0x697B41 (getThreatInRange): `if (obj + 0x468 < TheGameLogic->getFrame()) RW 0x69609F(radius); return obj + 0x464` (fld float).
//   * RW 0x69609F (refreshThreatInRange): ThePartitionManager->iterateObjectsInRange(obj + 0x38 the position, radius, distance type 0 FROM_CENTER_2D, the
//     filter chain RW 0xC11DC0 (the object's relationship to the hit in the mask 1 = ENEMIES, flag 0) -> RW 0xC10E20 (alive) -> RW 0xC0F374 (the
//     object's + 0x458 bit 3: never set, always passes), order 0 ITER_FASTEST). The sum starts at 0 (xorps / movss) and adds each hit's RW 0x68F0EC
//     (AIThreatFinder::threatValueWide) on the x87 stack: `call 0x68F0EC; fadd float [ebp+8]; fstp float [ebp+8]` (PC24, every partial sum stored
//     as a float). Then + 0x464 = the sum and RW 0x68C7C1(5000): + 0x468 = (5000 / 1000) * LOGICFRAMES_PER_SECOND (RW 0xD9F608 = 5) + frame, so the
//     deadline is the frame + 25, and the next refresh happens on the frame + 26.
//   * Team RW 0x7A6A20: the same test on team + 0x124 / + 0x120 -> RW 0x7A5B03: the team centre (RW 0x7A0ECD, every member), the first member (RW
//     0x66362D; none: return at once, the cache keeps its value and its deadline), then the same query around the centre with the first member as the
//     filters' object, the same sum into + 0x120 and RW 0x79FE73(5000) on + 0x124.
//   * The constructors clear both pairs (Object RW 0x69990F: + 0x464 = 0.0f, + 0x468 = 0; Team RW 0x7A6C91: + 0x120 = 0.0f, + 0x124 = 0). So on frame 0
//     the first question answers 0 without a refresh (0 < 0 is false).
//   * The radius only matters when the cache refreshes: within 5 s every caller gets the first caller's value, whatever radius it passes (kept).
// BFME2 1.06 (game.dat, the BFME2 decomp; xmap tier B "sim, edited"): the same functions, except the per-hit value. BFME2 adds the hit's ThingTemplate
// ThreatLevel (template + 0x51C) with SSE (`total += obj->getTemplate()->m_threatLevel`: addss) instead of calling the threat value: Team BFME2 0x7A2DBB
// (TeamRva003A2DBB.cpp:76-94) and Object BFME2 0x695844 (ObjectRva00295844.cpp:51-61). The cache sits at Object + 0x444 / + 0x448 there.
// The edition is the game's (GameLogic::edition, Common/GameEdition.h); there is no other rule and no fallback between the two.
// Saves: RotWK's Team::xfer (RW 0x7A5C06) writes + 0x120 (xferReal) and + 0x124 (xferUnsignedInt), as BFME2's (TeamXfer.cpp:196-197): Team::xfer
// version 5 carries them. RotWK's Object::xfer (RW 0x697F28) does NOT write + 0x464 / + 0x468 (a retail load starts from the constructor's pair);
// BFME2's does (+ 0x448 / + 0x444, ObjectDoXfer.cpp:715-716). OpenBFME's native object snapshot (version 3) keeps the pair in both editions, because
// the state hash covers it and a load must reproduce the saved hash (OpenBFME, S-4190): for up to 5 s after a load a RotWK game answers the cached
// value where retail would refresh at once.
#pragma once

class GameLogic;
class Object;
class Team;

namespace ScriptThreat
{
// RW 0x697B41 (BFME2 caller of 0x695844): the object's cached enemy threat, refreshed within `radius` when the deadline has passed
float getThreatInRange(GameLogic &logic, Object &obj, float radius);
// RW 0x7A6A20 (BFME2 caller of 0x7A2DBB): the team's cached enemy threat around its centre
float getThreatInRange(GameLogic &logic, Team &team, float radius);
} // namespace ScriptThreat
