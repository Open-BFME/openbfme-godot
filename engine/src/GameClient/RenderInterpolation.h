// OpenBFME. GPL-3.0.
//
// RenderInterpolation (lane SMOOTH-1): the client's pose of a drawable between two logic frames. CLIENT ONLY: it reads a completed frame's snapshot
// record (GameClient/LogicSnapshot.h) and returns the render pose for the client alpha. It never writes logic state and is excluded from the
// simulation audit (tools/sim/sim_policy.json): the render rate cannot reach the state hash.
//
// TARGET FACTS (RotWK game.dat, caveat S-001; review of PHYS-1 / SMOOTH-1 r1):
//   RW 0x6765B9 Drawable::getTransformMatrix (the drawn transform): unless stale (below), RW 0xB27C80 interpolates the gathered recorded transform towards
//     the gathered current transform by the client alpha (GameEngine + 0x3C) - the translation lerped, the rotation as a quaternion slerp (after dividing out
//     the recorded transform's scale) - and then D3DXVec3CatmullRom(P0 previous, P1 recorded, P2 current, P3 pending, alpha) replaces the translation.
//   RW 0x6765F1: when the drawable's last transform change (+0x3A4, written by RW 0x69355D) is before TheGameLogic +0x40 - 2 (unsigned), the object's
//     transform is copied once (+0x3A8) and drawn as it is.
//   RW 0x676711 (the drawn position) is the same Catmull-Rom; RW 0x67679B is a linear reader of P1 -> P2 for its own consumers.
//   RW 0x63256F: alpha = phase / phases-per-frame, clamped to [0, 1].
// THIS PORT: retailPose() is RW 0x6765B9 on a snapshot record. The alpha is the continuous fraction of the logic frame (S-811: retail steps it per 30 Hz
// engine tick). interpolate() (the earlier linear form) remains for its unit tests. No clamp of the Catmull-Rom overshoot (retail has none).

#pragma once

#include "Common/INIDataTypes.h"
#include "GameClient/LogicSnapshot.h"

#include <string>
#include <vector>

namespace RenderInterpolation
{

// alpha clamped to [0, 1] (the clock's alpha is in [0, 1); a caller may pass more after a stall)
float clampAlpha(double alpha);

// from + (to - from) * a, per component
Coord3D lerpPosition(const Coord3D &from, const Coord3D &to, float a);

// from + d * a with d = to - from wrapped into [-pi, pi] (the shortest way round)
float lerpAngle(float from, float to, float a);

// true when the row-major basis is a pure rotation about Z (Thing::setOrientation's form: z row and column are (0, 0, 1))
bool isZRotation(const float basis[9]);

// The rotation between two orthonormal row-major 3x3 bases: quaternion slerp (normalised lerp when they are nearly equal, the shorter arc
// always). a <= 0 copies `from`, a >= 1 copies `to` exactly.
void slerpBasis(const float from[9], const float to[9], float a, float out[9]);

// The drawable's render pose for one object.
struct Pose
{
	Coord3D position;
	float angle = 0.0f;
	float basis[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
	bool zRotation = true; ///< use `angle` (Thing::setOrientation); else `basis` (Thing::setTransform)
};

// recorded: the transform at the start of the logic frame; current: the object's transform now. Without a recorded transform (a new object
// before its first phase 2) the current pose is drawn.
Pose interpolate(bool haveRecorded, const Coord3D &recordedPos, float recordedAngle, const float recordedBasis[9], const Coord3D &currentPos,
	float currentAngle, const float currentBasis[9], double alpha);

// D3DXVec3CatmullRom(p0, p1, p2, p3, s): p1 at s = 0, p2 at s = 1 (the D3DX formula
//   0.5 * (2 p1 + (p2 - p0) s + (2 p0 - 5 p1 + 4 p2 - p3) s^2 + (p3 - 3 p2 + 3 p1 - p0) s^3), Wine's d3dx9 operation order)
Coord3D catmullRom(const Coord3D &p0, const Coord3D &p1, const Coord3D &p2, const Coord3D &p3, float s);

// RW 0x6765B9 Drawable::getTransformMatrix with a gathered snapshot (LogicSnapshot.h, RW 0x674B1F):
//   * stale (RW 0x6765F1: the drawable's last transform change, +0x3A4, is before the current logic frame - 2, unsigned): the object's current
//     transform as it is (+0x3A8 makes the copy once);
//   * else RW 0xB27C80 interpolates the recorded transform towards the current one by the client alpha (the rotation as a quaternion slerp, after
//     dividing out the scale of the recorded transform's third row: 1 here, the instance scale is applied by the device layer), and
//     D3DXVec3CatmullRom(P0, P1, P2, P3, alpha) replaces the translation.
// `snapshotFrame` is the completed frame of the snapshot; retail compares at the next frame's ticks (TheGameLogic +0x40 = snapshotFrame + 1).
// No clamp of the Catmull-Rom overshoot (retail has none; review: compare with retail rather than clamp).
Pose retailPose(const ObjectSnapshot &s, UnsignedInt snapshotFrame, double alpha);

// Lane SMOOTH-2 (owner feedback 2026-10-06: the infantry and horse battalions "jerked around a lot and seemed to 'lag'"; presentation beyond retail,
// S-811): the drawn pose the device layer uses. retailPose's P3 is the movers' pending position, which RW 0x5E5B4E sets to the PRE-move position for a
// mover without a path (every horde member walking to its slot): retail's curve then slows to a standstill at the end of every logic frame (a 5 Hz
// pulse: the drawn speed at a frame boundary jumps by a factor of ~3). Here:
//   * stale (as retail): the current transform;
//   * a segment without motion (P1 == P2): held at P2 (no Catmull-Rom overshoot after a stop);
//   * P3 is the pending position only when it is a look-ahead (set this frame and not the frame's start position, RW 0x5E5A87's path point ahead);
//     otherwise the step is extrapolated (P2 + (P2 - P1));
//   * the curve is the same cubic Hermite form as D3DXVec3CatmullRom (tangents (P2 - P0) / 2 and (P3 - P1) / 2), with each tangent's backward part
//     along the step removed and its length limited to twice the step, so a teleport before the segment, a reversal of the logic or a stop never
//     makes the drawn position run backwards or overshoot;
//   * the rotation as retail (slerp / short-way angle lerp).
// Projectiles keep retailPose (their pending position is their flight path's next point, RW 0x85F6E7).
Pose smoothPose(const ObjectSnapshot &s, UnsignedInt snapshotFrame, double alpha);

// The stops of the client's smooth motion (docs/STOPS.md S-810, S-811), reported with DrawableManager's report next to S-151.
std::vector<std::string> stopLines();

} // namespace RenderInterpolation
