// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0 (GameLogic/AI/AI.cpp AI::findClosestEnemy and its partition
// filters).
//
// AI::findClosestEnemy (lane TARGET-1): the target search of the idle mood scan, the guard and hunt states and the attack-move (the units' auto-acquire).
// Ported from RotWK 2.01 (caveat S-001); BFME2 decomp GameLogic/AI/AIClosestObjectQueries.cpp (tier A for the wrappers) and ZH AI.cpp are the donors.
//
// TARGET FACTS (RotWK game.dat):
//   * RW 0x701443(me, range, qualifiers, info, optionalFilter, arg) = the worker RW 0x7006D7 at me's position (+ 0x38); RW 0x70146A the same at a position.
//   * The worker: CAN_ATTACK (2) and Object::isAbleToAttack (RW 0x691269) false, or AUTO_ACQUIRE (0x40) and NO_AUTO_ACQUIRE (status 0x17): none. Every filter
//     is made, then linked to the chain (RW 0xA394C0 appends; RW 0xA39450 asks them in order, the first refusal ends) in this order: the ENEMIES head
//     (vtable 0xC1D688, RW 0x6FF7A4), alive (0xC10E20), reject buildings unless ATTACK_BUILDINGS (8) (0xC0F1A8, RW 0x660EC6 / 0x661869), with
//     AUTO_ACQUIRE the KindOf filter (0xC1D6AC RW 0x6FF67C when the player has a skirmish AI, else 0xBD88B0 RW 0x445139), WITHIN_ATTACK_RANGE (0x10)
//     (0xC1D694 -> RW 0x6FF7FA), CAN_SEE (1) the line of sight (0xC1D620, RW 0x6616AC), UNFOGGED (0x20) (0xC1D63C, RW 0x661027), IGNORE_INSIGNIFICANT_BUILDINGS
//     (4) (0xC1D630, RW 0x660F1A), stealthed and undetected (0xC0F19C, RW 0x660D2C / 0x660D71), FLEE_OFF_MAP (0xC1D6A0, RW 0x6FF651), unless NO_VISION_TEST
//     (0x100) canSee within the range (0xC1D66C, RW 0x6612AC), the attack-moving skirmish AI filter (0xC1D678, me's AI in state 0x21), CAN_ATTACK the
//     possible-to-attack filter (0xC0F368, RW 0x660CA4: getAbleToAttackSpecificObject(ATTACK_NEW_TARGET, it, CMD_FROM_AI) possible or after moving).
//   * No attack priority info: ThePartitionManager's closest object (RW 0xA39090) at distance type 1 (3D centres); with CAN_ATTACK it is adjusted for a horde
//     (RW 0x68D30E). The optional filter is not used on this path.
//   * An info: the objects in range (RW 0xA39340, type 1, unsorted; every registered object when the range is 9000 (RW 0xC1D8E0) or more, RW 0xA39420),
//     each scored in that order (see findClosestEnemy in the .cpp: priority, horde / contain maxima, DO_NOT_PICK_ME, open gates, walls, the edge distance,
//     score = (priority + 100) - distance / AttackPriorityDistanceModifier, the computer creeps rule, pathability RW 0x6FB754, the friendly-fire crowd test,
//     the optional filter), then the melee drop and the horde adjustment.
//   * adjustVictim RW 0x68D30E(victim, attacker, attackRange, arg): see the function.
//
// The line of sight filter (RW 0x6616AC: the terrain line, TheTerrainLogic vslot 0x3C, and the pathfinder's view block RW 0x6F4F9D) is ported (lane LOS-1,
// GameLogic/Combat/CombatLineOfSight.h).
// INFERENCE / NOT PORTED (stop S-326, reported): the reject-buildings filter's contain answer (contain vslot 0x4C, the
// apparent controlling player) is not ported (an uncontrolled contained structure is refused); the attack-moving skirmish AI filter needs AI state 0x21
// (AIAttackMoveToState), which the port's attack-move does not use (S-328), so it is never linked; the CONTESTING_BUILDING branch (contain vslot 0xDC)
// has no setter in the port; the friendly-fire crowd query is distance type 4 (RW 0xA3C7D0 -> RW 0xAD2CE0, S-1020): with AIData ArtilleryMaxFriendlyFire
// 1.0 or more and a hit of max health 1 or more it cannot refuse (the hit overlaps its own position, so the non-allied sum holds its max health), otherwise
// it is reported and passes; Object + 0x458 bit 3 is never set in the port.

#pragma once

#include "GameLogic/ObjectTypes.h"

class AttackPriorityInfo;
class Object;
class PartitionFilter;
class Weapon;
struct Coord3D;
struct TriggerArea;

namespace AI
{
// the qualifier bits of RW 0x7006D7 (ZH AI.h names where ZH has the bit)
enum Qualifier : unsigned
{
	CAN_SEE = 0x1,                         ///< the line of sight filter (ZH CAN_SEE)
	CAN_ATTACK = 0x2,                      ///< isAbleToAttack and the possible-to-attack filter (ZH CAN_ATTACK)
	IGNORE_INSIGNIFICANT_BUILDINGS = 0x4,  ///< ZH IGNORE_INSIGNIFICANT_BUILDINGS
	ATTACK_BUILDINGS = 0x8,                ///< no reject-buildings filter (ZH ATTACK_BUILDINGS)
	WITHIN_ATTACK_RANGE = 0x10,            ///< the reach filter RW 0x6FF7FA (ZH WITHIN_ATTACK_RANGE), and no horde adjustment at the end
	UNFOGGED = 0x20,                       ///< ZH UNFOGGED
	AUTO_ACQUIRE = 0x40,                   ///< RotWK: NO_AUTO_ACQUIRE refuses the search, NOT_AUTOACQUIRABLE / ROCK_VENDOR targets are refused
	COMPUTER_CREEPS = 0x80,                ///< RotWK: a creep target's score is 1.0
	NO_VISION_TEST = 0x100                 ///< RotWK: no canSee filter
};

// RW 0x7006D7. `info` null: the partition's closest object; else the scored search. Never throws for a missing piece: unported parts are reported (S-326)
Object *findClosestEnemy(Object &me, const Coord3D &pos, float range, unsigned qualifiers, const AttackPriorityInfo *info, PartitionFilter *optionalFilter, int arg);
// Building filters shared with melee reacquisition (BFME2 RVAs 0x261B7E / 0x261246).
bool allowsBuildingTarget(const Object &seeker, Object &target);
bool isSignificantBuildingTarget(Object &target);
// RW 0x701443
Object *findClosestEnemy(Object &me, float range, unsigned qualifiers, const AttackPriorityInfo *info, PartitionFilter *optionalFilter, int arg);
// lane BFME2CAMP1: RW 0x700F52(area, me, info), the attack-area state's search: isAbleToAttack false: none. The filters, linked in RW's order: the ENEMIES head
// (vtable 0xC1D688), possible-to-attack (0xC0F368), reject buildings (0xC0F1A8: always, there is no ATTACK_BUILDINGS here), stealthed and undetected (0xC0F19C),
// FLEE_OFF_MAP (0xC1D6A0), alive (0xC10E20); the polygon filter (0xC1D654, allow RW 0x66103C: the candidate's position truncated to integers in the area,
// RW 0x6E4DC7) is kept apart. The area's bounds (RW 0x6E4D2C: floor of the low corner, ceil of the high one, as integers) bound the search:
//   * no info: the polygon filter joins the chain; ThePartitionManager's closest object to me within 9999.9 (RW 0xC1D8E4), distance type 1, inside the bounds
//     (RW 0xA390F0 -> RW 0xA3BDB0), adjusted (RW 0x68D30E(me, 1, 0));
//   * an info: the objects inside the bounds (RW 0xA393C0 -> RW 0xA3C4E0, unsorted). Each: p = the priority (RW 0x7597E9(me, it, 1)); 0 skips it; a container
//     takes its contained objects' highest (vslot 0x110 with RW 0x6FF29B); n = _ftol(sqrt(edge distance squared, RW 0x66352C) / AttackPriorityDistanceModifier)
//     (x87 under PC24); score = max(1, cvttss2si(p - n)) as a float. A higher score, or the same score with a higher p, wins when the polygon filter allows
//     it. The winner is adjusted (RW 0x68D30E(me, 1, 0)).
// The CONTESTING_BUILDING branch (status 0x25 and the container's vslot 0xDC, RW 0x701054) has no setter of the status in the port (as in RW 0x7006D7).
Object *findClosestEnemyInArea(const TriggerArea &area, Object &me, const AttackPriorityInfo *info);
// RW 0x68D30E: the victim an attacker aims at. Null attacker: null. The victim's container chain is climbed while the container has a horde interface; a
// result without a horde interface is the victim itself; a horde with no members (slot 0x180(0)) gives null; else its member near the attacker (slot 0x48,
// RW 0x86FA87) within the attacker's attack range (RW 0x6CA935) when `attackRange` and the attacker is not a HORDE and holds a ranged current weapon, 0 (no
// range) when `attackRange` otherwise, 99999 (RW 0xBDF350) without `attackRange`; none: the first member (slot 0x48 without a position)
Object *adjustVictim(Object &victim, Object *attacker, bool attackRange, int arg);
// RW 0x6FF7FA(source, victim): the source can hit the victim from where it stands (any weapon slot within its attack range, or a melee weapon of a source that
// does not stand its ground and sees the victim on its layer)
bool canAttackFromHere(Object &source, Object &victim);
// RW 0x6FF412(obj, flags): with an AI, the vision range (RW 0x68E43B) plus the bounding circle (+ 0xB8); with MOOD (2) and a computer unit's mood: asleep 0,
// alert * AlertRangeModifier, aggressive * AggressiveRangeModifier; 0 without an AI
float adjustedVisionRange(const Object &obj, unsigned flags);
// RW 0x6CB9F7(template, weapon): the Radius of the first damage nugget of the weapon's first projectile warhead whose RadiusDamageAffects holds ALLIES, 0 else
float friendlyFireRadius(const Weapon &weapon, const Object &owner);
} // namespace AI
