// OpenBFME. GPL-3.0.
//
// DrawableScriptTarget (lane FX-3, QA-1 U9): what a draw script learns about its object's target, captured on the simulation side as values.
//
// TARGET FACTS (RotWK game.dat, S-001 caveat):
//   * Object::preFireCurrentWeapon RW 0x69213E records the target before its weapon test (RW 0x692174 .. 0x6921AB): Object +0x3A8 .. +0x3B0 the victim's
//     position (or the given position), +0x3B4 the victim's id (or 0). The Object constructor zeroes the record (RW 0x699BB6). The port keeps it in
//     ObjectWeapons (drawTargetPosition / drawTargetID).
//   * CurDrawableIsCurrentTargetKindof RW 0x73667D: the script context's drawable -> its object (Drawable +0xFC); without one: nil, then false. Else
//     TheGameLogic->findObjectByID(+0x3B4) (RW 0x449681); no such object, or no argument: false. Else the name through RW 0x6AAD1A (the KindOf names,
//     case insensitive, -1 when absent) and the target's template mask test (Thing RW 0x444FB7 -> RW 0x444FA2 -> RW 0x68CEFE on template +0x108).
//   * CurDrawableGetCurrentTargetBearing RW 0x734A75: the same chain to the object (nil without one); RW 0x4B3D8D(object, +0x3A8): d = target - position
//     in XY; |d| == 0 gives 0; else d / |d| against the object's unit direction (RW 0x70B9E0: cos / sin of the orientation +0x44, INFERENCE from ZH
//     Object::getUnitDirectionVector2D), the dot clamped to [-1, 1] (constants RW 0xBD19DC -1.0f, RW 0xBDFC10 -1.0 double, RW 0xBD1908 1.0f), acos, negated
//     when dir.x * d.y - dir.y * d.x < 0; then RW 0x644FD0 brings the angle into (-pi, pi] by steps of 2 pi (RW 0xBDD388 / 0xBDD38C / 0xBDD390).
// THIS PORT: the drawable lives on the render side and never reads the logic. The values are captured where the logic changes the drawable's model
// conditions (ClientEventRecorder::modelConditionChanged: the call that makes the state script run) and in every published snapshot, so a script
// reads the record of the latest logic state the render side was told about. Retail reads the live object while the script runs (one thread).

#pragma once

#include "Common/GameCommon.h"
#include "Common/INIDataTypes.h"
#include "GameLogic/ObjectTypes.h"

#include <string>

class GameLogic;
class Object;

struct DrawableScriptTarget
{
	bool hasObject = false;        ///< the drawable is bound to an object (false: the scripts get nil / false)
	Coord3D ownPosition;           ///< Object +0x38
	float ownAngle = 0.0f;         ///< Object +0x44
	Coord3D targetPosition;        ///< Object +0x3A8
	bool targetExists = false;     ///< findObjectByID(Object +0x3B4) found an object
	KindOfMaskType targetKindOf{}; ///< its KindOf bits

	// the record of `obj` (an object without weapons keeps the constructor's zero record)
	static DrawableScriptTarget capture(const GameLogic &logic, const Object &obj);
	// RW 0x73667D past the context check: false without a target object or for a name that is not a KindOf
	bool isTargetKindOf(const std::string &kindName) const;
	// RW 0x734A75 past the context check: RW 0x4B3D8D then RW 0x644FD0, radians in (-pi, pi]
	float bearing() const;
};
