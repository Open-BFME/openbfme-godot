// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// ConstructionSite (lane BACKLOG2): what retail does to the ground before a structure appears there. ZH BuildAssistant::clearRemovableForConstruction /
// moveObjectsForConstruction and their FoundationAIUpdate twins; the names are ZH's, the code RotWK's.
//
// TARGET FACTS (RotWK game.dat, S-001 caveat; static disassembly; donor: Open-BFME-2's byte-matched BFME2 1.06 source, cited per function):
//   * the geometry filter (ctor RW 0x67C5F3: position, GeometryInfo, angle, flag; vtable RW 0xC112C8, allow RW 0x66139C): the shape overlap test RW 0xAD2CE0 of
//     the filter's geometry at its position / angle with the object's own geometry (Object + 0xA8) at its position (+ 0x38) / orientation (+ 0x44) equals the flag.
//   * BuildAssistant::isRemovableForConstruction RW 0x7940A6 (BFME2 decomp BuildAssistant.cpp, tier B same-shape): never INERT (89) or TAINT (152); SHRUBBERY (6) or
//     CLEARED_BY_BUILD (51): yes; else whether the object is effectively dead (+ 0x458 bit 0). FoundationAIUpdate's RW 0x8582A5 is the same without the TAINT test.
//   * clearRemovableForConstruction RW 0x796576 (tier A, BFME2 decomp BuildAssistant.cpp:clearRemovableForConstruction) and FoundationAIUpdate RW 0x858C41
//     (tier A, BFME2 decomp FoundationAIUpdateSlots.cpp:rva00455BDD): ThePartitionManager within the template's bounding sphere radius (GeometryInfo + 0x14) * 1.1f
//     (SSE, RW 0xBE4398), distance type 3, unsorted, with the geometry filter (the template's geometry at the site, the build angle, flag 1); every hit that is
//     removable (the function's own test) and not ALWAYS_SELECTABLE (58, template + 0x10F bit 2) is destroyed (RW 0x62BBAB).
//   * moveObjectsForConstruction RW 0x797465 (tier B same-shape, BFME2 decomp BuildAssistant.cpp:moveObjectsForConstruction):
//       - the box gi = GeometryInfo(BOX, not small, height 10, major = minor = the template's bounding circle radius, + 0x10) (RW 0x450429, bounds by RW 0xAD2860);
//         a template whose geometry is exactly one BOX (RW 0xAD22C0) is copied instead (RW 0x463DFC); radius = gi's bounding circle * 1.4f (SSE, RW 0xBDBC98);
//       - first every object of the partition's entry list (RW 0xA39420 -> 0xA3C6F0) is offered to the path filter RW 0x7940E8 (vtable RW 0xC307C8, tier B,
//         BFME2 decomp BuildAssistant.cpp:Rva00391D25Filter::allow), which answers no and, as its side effect, destroys the path (AI RW 0x66276B) of a live unit
//         that is none of INERT, MOVE_ONLY, BASE_FOUNDATION, WALK_ON_TOP_OF_WALL, IMMOBILE, SHRUBBERY, CLEARED_BY_BUILD, ALWAYS_SELECTABLE, has an AI with a
//         path (AI + 0x140), is not a horde whose members' paths hold a waypoint node (the horde test below), and whose straight line from its position to the
//         path's last node (Path + 8, node + 0xC) passes within `radius` of the site (closest point: x87 length RW 0x4054F5, normalise RW 0x405553, x87 dot
//         RW 0x4037F5, clamped to the segment; x87 squared length RW 0x405520 against radius * radius);
//       - then ThePartitionManager within gi's bounding sphere * 1.1f, type 3, unsorted, filters alive (RW 0xC10E20), has an AI (RW 0xC2E65C: Object + 0x260),
//         KindOf SELECTABLE and none of INERT / MOVE_ONLY / BASE_FOUNDATION / WALK_ON_TOP_OF_WALL (RW 0x445139), the geometry filter (gi, angle, flag 1); a hit
//         that is IMMOBILE, ALWAYS_SELECTABLE or removable is skipped; an owner the builder's player is not ENEMIES with (Player RW 0x6ADBEB on the hit's team)
//         and not the horde test: its AI draws variedRadius = GetGameLogicRandomValueReal(0.5, 1.5) (GetGameLogicRandomValueReal RW 0x6D332C, line 0xE41; x87 fmul radius, stored) and the
//         angle GetGameLogicRandomValueReal(-pi, pi) (line 0xE44); the goal is the site + (cos * variedRadius - sin * 0, sin * variedRadius + cos * 0) (MSVCR71
//         cos RW 0xA3CF84 / sin RW 0xA3CF90, SSE), z the site's; an AI that is not moving (RW 0x664485) gets aiMoveToPositionEvenIfSleeping(goal, CMD_FROM_AI)
//         (RW 0x79664F, AI command 0x36). An enemy or neutral-less hit marks the site as blocked (the return value, which no caller reads).
//   * the horde test (RW 0x694BF8 -> HordeContainInterface vslot 0x1C8 RW 0x86D830, tier B, BFME2 decomp HordeContainIface11CSlots.cpp:rva0046A6C1): the object's
//     horde (itself, or what contains it, when KindOf HORDE: RW 0x693A1A) has a member whose AI path has a current node (Path + 0x10) and a node with a waypoint id
//     (node + 0x20 != 0x7FFFFFFF; RW 0x765522, tier A, BFME2 decomp Rva003638BAMethod.cpp).
//   * FoundationAIUpdate RW 0x858CFC (tier B same-shape, BFME2 decomp FoundationAIUpdateSlots.cpp:rva00455C98): gi = BOX(height 50, major = the template's
//     + 0x24 * 1.5f, minor = + 0x28 * 1.5f) (SSE); radius = gi's circle * 1.5f; ThePartitionManager within gi's sphere * 1.5f, type 3, the geometry filter only;
//     each hit is replaced by what contains it (Object + 0x27C); INERT, BASE_FOUNDATION, WALK_ON_TOP_OF_WALL, MOVE_ONLY, ALWAYS_SELECTABLE or removable (RW 0x8582A5)
//     hits are skipped; an owner the player is not ENEMIES with and an AI: variedRadius = random(0.5, 1.5) * radius (line 0x2AB); a template that is not
//     WALK_ON_TOP_OF_WALL draws two angles (lines 0x2AF, 0x2B7): dest1 = site + v1 (v1 = (variedRadius, 0) turned by the first), dest2 = site + v1 turned again by
//     the second, z the site's; a WALK_ON_TOP_OF_WALL template: with the foundation BASE_SITE (+ 0x117 bit 0) d = (foundation - site) * 0.5f, else
//     d = the hit's x axis (RW 0x70BA38, normalised) * (variedRadius + radius); dest1 = site + d, dest2 = site - d; the goal is the NEARER of the two: dest1 when the hit is farther
//     from dest2 (squared 2D, SSE: dy^2 + dx^2), else dest2; a hit that is not moving gets aiMoveToPositionEvenIfSleeping(goal, CMD_FROM_AI).
// INFERENCE (stop S-653 / S-950, this lane): the object's own geometry is its template's shapes with the object's active flags (the port keeps no other per-object
// GeometryInfo); the hit's x axis is (cos, sin, 0) of its orientation (the port keeps no tilted transform). The pathfinder classification of the moved units is
// the AI's own.
//
// Simulation maths goes through SimMath / NumericState.

#pragma once

#include "Common/GameCommon.h"
#include "GameLogic/Object/ObjectGeometry.h"
#include "GameLogic/Object/PartitionManager.h"

#include <vector>

class GameLogic;
class Object;
class Player;
class ThingTemplate;

namespace ConstructionSite
{
// RW 0x67C5F3 / 0x66139C (vtable RW 0xC112C8)
class GeometryFilter : public PartitionFilter
{
public:
	GeometryFilter(const Coord3D &pos, std::vector<ObjectGeometry::Shape> shapes, float angle, bool flag)
		: m_pos(pos)
		, m_shapes(std::move(shapes))
		, m_angle(angle)
		, m_flag(flag)
	{
	}
	bool allow(Object &obj) override;

private:
	Coord3D m_pos;
	std::vector<ObjectGeometry::Shape> m_shapes;
	float m_angle;
	bool m_flag;
};

// the object's own geometry (Object + 0xA8): the template's shapes with the object's active flags (lane HUD-5's Object::geometryActive)
std::vector<ObjectGeometry::Shape> objectShapes(const Object &obj);

// RW 0x7940A6 (BuildAssistant) / RW 0x8582A5 (FoundationAIUpdate: no TAINT test)
bool isRemovableForConstruction(GameLogic &logic, const Object *obj);
bool isRemovableForFoundation(GameLogic &logic, const Object *obj);

// RW 0x796576; returns the number of objects destroyed
int clearRemovableForConstruction(GameLogic &logic, const ThingTemplate &what, const Coord3D &pos, float angle);
// RW 0x797465; false when an object in the way cannot be moved
bool moveObjectsForConstruction(GameLogic &logic, const ThingTemplate &what, const Coord3D &pos, float angle, Player &builderPlayer);
// RW 0x7977C9 .. 0x79780C (BuildAssistant::buildObjectNow, slot 0x38; BFME2 decomp BuildAssistant.cpp:buildObjectNow, tier B): unless the builder is a DOZER or
// the template is NO_COLLIDE (30) or DO_NOT_CLASSIFY (149), clearRemovableForConstruction then moveObjectsForConstruction
void prepareSite(GameLogic &logic, const Object &builder, const ThingTemplate &what, const Coord3D &pos, float angle, Player &owner);

// RW 0x858C41 (FoundationAIUpdate); returns the number destroyed
int clearSiteForFoundation(GameLogic &logic, const ThingTemplate &what, const Coord3D &pos, float angle);
// RW 0x858CFC (FoundationAIUpdate `foundation`); false when an object in the way cannot be moved
bool moveObjectsForFoundation(GameLogic &logic, const Object &foundation, const ThingTemplate &what, const Coord3D &pos, float angle, Player &owner);
} // namespace ConstructionSite
