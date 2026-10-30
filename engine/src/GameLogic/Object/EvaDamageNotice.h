// OpenBFME. GPL-3.0.
//
// Lane EVADAMAGE-1 (S-709's damage half, stop S-7090): the Eva half of RotWK's drawable damage notice, RW 0x67B4B7 (Drawable::notifyDamaged; BFME2 decomp
// GameClient/DrawableRva0027B18C.cpp:192, tier B same-shape; the template field names from the decomp's ThingTemplate, RotWK's table RW 0xDA4298).
//
// TARGET (RW 0x67B4B7, read in Ghidra): after the gates Object::doAttemptDamage already applies for the radar half (not UNATTACKABLE + MOVE_ONLY, dealt above
// 0.0f, not PENALTY (10) / HEALING (7), not death type FADED (0x16), not sub type 2, the owner's bit not in the source mask, the owner the local player
// (RW 0x6AA7B2)) and the hit's ShouldPlayUnderAttackEva (+0x25), a drawable with a template picks ONE event of its template:
//   1. EvaEventAmbushed (+0x5A8) when it is set, the victim has none of the model conditions ENGAGED (0x9D, itself: RW 0x46E918), EMOTION_ALERT (0x3F) and
//      ATTACKING (0x25) (itself or its horde: RW 0x694154), the attacker (TheGameLogic->findObjectByID(source), RW 0x449681) exists and is NOT visible as an
//      enemy to the owner (RW 0x694CCC(owner, 1), Object::isVisibleTo below); the second event is SecondDamageFarFromFirst (+0x59C);
//   2. else for a LOGICAL_FIRE hit (type 0x17): EvaEventDamagedByFireOwner (+0x598) when set, with no second event;
//   3. else (not 0x17) EvaEventDamagedFromShroudedSourceOwner (+0x594) when set and the attacker's drawable is fully obscured by the shroud (Drawable +0x440,
//      RW 0x678F54: the local player's shroud status at least FOGGED); second event SecondDamageFarFromFirst;
//   4. else EvaEventDamagedOwner (+0x590), second event SecondDamageFarFromFirst.
// With an event (not -1): when the drawable's far frame (+0x388) is not ahead of the logic frame and TheEva says the event is blocked by its timeout (RW 0x5DCC88)
// or about to play (RW 0x5DCCEB), the second event (when set) is reported instead; the report is TheEva RW 0x5DD9EE(event, object position +0x38, null). Then
// RW 0x671DFD (Drawable_extendEvaDamageFarFrame: +0x388 = max(+0x388, frame + EvaEventSecondDamageFarFromFirstTimeoutMS (+0x5A4, frames))) runs on the
// victim's drawable when EvaEventSecondDamageFarFromFirstScanRange (+0x5A0) is not above 0 or DamagedOwner is -1; otherwise on the drawable of every object
// ThePartitionManager finds within the scan range of the victim's drawable position (RW 0x676711; distance type 2, FROM_BOUNDINGSPHERE_2D) that the filters
// allow: the object's template has the same EvaEventDamagedOwner (RW 0x671E36) and the owner controls it (RW 0x660AFE). The template ctor (RW 0x73FB7A) sets the
// events to -1, the scan range to 200.0 (RW 0xBE4170) and the timeout to LOGICFRAMES_PER_SECOND x 30 (RW 0xD9F608 x 0x1E).
//
// Split (SMOOTH-1: the simulation never touches a drawable): the logic half below, called from the hook in Object::doAttemptDamage, reads only logic state and
// does not modify simulation state (the partition query uses its own caches and scratch fields, never hashed or saved): the attacker, the ambush test, the hit's fire type and the scan's objects go into an EvaDamageNotice that the client hook records
// (ClientEvent::EVA_DAMAGED). The client half (GameClient/EvaDamage.h) picks the event from the drawable's template and the attacker drawable's shroud look,
// keeps the far frame on the drawable, and asks the Eva (AudioApi::reportEvaDamage).
// INFERENCE (stop S-7090): the scan's centre is the object's position (retail: the drawable's interpolated position, RW 0x676711), the scan filter and the
// event choice read the object's template (retail: the drawable's, which differs only for a shown disguise, RW 0x776F03); RW 0x694CCC's contain branch takes the
// container itself as the apparent controlling player (contain vslot 0x4C(player) is not ported, as in S-326) and reads the HORDE branch's contain vslot 0x78
// as the horde interface (slot 0x7C).

#pragma once

#include "Common/GameCommon.h"
#include "GameLogic/ObjectTypes.h"

#include <string>
#include <vector>

class GameLogic;
class Object;
class Player;
class ThingTemplate;
struct DamageInfo;

struct EvaDamageNotice
{
	ObjectID attacker = INVALID_ID; ///< the hit's source when it exists (RW 0x449681), else INVALID_ID
	bool ambushable = false;        ///< row 1's tests except the template's EvaEventAmbushed: the victim's conditions and RW 0x694CCC(owner, 1) false
	bool byFire = false;            ///< the hit's damage type is LOGICAL_FIRE (0x17)
	bool scanned = false;           ///< RW 0x671DFD runs on `scan` (false: on the victim's drawable alone)
	std::vector<ObjectID> scan;     ///< the scan's objects, in the partition's order
};

namespace EvaDamage
{
// the template's Eva event name of `field` (RW 0x5DE588: "None" and an unset field are -1, returned as "")
std::string templateEvent(const ThingTemplate &tt, const char *field);
// EvaEventSecondDamageFarFromFirstScanRange (+0x5A0) and ...TimeoutMS (+0x5A4, frames), the ctor's values when unset
float scanRange(const ThingTemplate &tt);
UnsignedInt timeoutFrames(const ThingTemplate &tt);

// RW 0x694CCC Object::isVisibleTo(player, enemyOnly): the object is visible to `player` (with `enemyOnly`: and an enemy of it; see the .cpp)
bool isVisibleTo(const GameLogic &logic, const Object &obj, const Player *player, bool enemyOnly);

// the logic half of RW 0x67B4B7's Eva part for a hit that passed the gates on `victim` of the local `owner` (reads only)
EvaDamageNotice makeNotice(const GameLogic &logic, const Object &victim, const DamageInfo &info, const Player &owner);
} // namespace EvaDamage
