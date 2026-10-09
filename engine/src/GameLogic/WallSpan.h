// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0 (BuildAssistant is the model; RotWK's wall spans are not in ZH).
//
// Walls (lane BUILD-2, stop S-307): WallHubBehavior and the wall span a hub builds (MSG_WALL_HUB_CONSTRUCT_SPAN).
//
// TARGET FACTS (RotWK game.dat, S-001 caveat; static disassembly):
//   WallHubBehavior (registry RW 0x657A16: create RW 0x64A601, data RW 0x64A639; module size 0x28, ctor RW 0x855EC7; data size 0x3C, ctor RW 0x8560C0, field table
//   RW 0xC56700): SegmentTemplateName (string list +8, appended), HubCapTemplateName (+0x14), DefaultSegmentTemplateName (+0x18), CliffCapTemplateName (+0x1C),
//   ShoreCapTemplateName (+0x20), BorderCapTemplateName (+0x24), ElevatedSegmentTemplateName (+0x28), BuilderRadius (real +0x2C, default 9.876, RW 0xC565A0),
//   MaxBuildoutDistance (real +0x30, default 54321.0 = unset, RW 0xC565A4), Options (CommandButton option bits +0x34, names RW 0xDAE268), StaggeredBuildFactor
//   (int +0x38, default 5). The module does nothing per frame (its update RW 0x88B211 returns UPDATE_SLEEP_FOREVER); it answers through its interface (module + 0x20,
//   vtable RW 0xC565A8): slot 1 the pattern template of tile i (SegmentTemplateName[i % count], RW 0x855FF1; none when the list is empty), slot 2 / 3 / 4 the hub cap /
//   default segment / cliff cap templates, slot 11 the builder radius (module + 0x24: BuilderRadius, or the object's bounding circle radius when it is the default,
//   ctor RW 0x855EC7), slot 12 MaxBuildoutDistance (true when it was set, RW 0x855EA0), slot 14 StaggeredBuildFactor. Object RW 0x693B55(options) finds the hub module
//   whose Options match (the command button's).
//   MSG_WALL_HUB_CONSTRUCT_SPAN (1123, dispatcher case RW 0x77C4C3): arguments 0 the hub template id (word), 1 / 2 the span's start and end, 3 the options, 4 the
//   hub object; the hub's owner must pass Player::canBuild (RW 0x6AAA2E) and have RW + 0x770 == 0; then BuildAssistant slot 0x3C (RW 0x795221) builds the span.
//   planWallSpan (BuildAssistant slot 0x54, RW 0x79586E) and buildWallSpan (slot 0x3C, RW 0x795221) are ported below in their order; the numbers (the tile length
//   2 * (geometry half extent in y - 1), the cap half extent, the 2/3 snap to an end hub, the 20.0 search radius, MaxLineBuildObjects) are read there.
// Not ported (stop S-657): the RotWK legal-build codes the plan acts on (5 / 6: a cliff cap ends the span; 8: blocked mid-way; 10: an equivalent dead segment is
// rebuilt) belong to RW 0x797A96, which is not read (S-301): the port's ZH codes only refuse a tile; the end-hub link path (RW 0x793DB9 then slot 0x58) and the
// rebuild of dead segments; the HUD's wall mode (the client's drag that makes the message) and the hub's CANCEL_NEIGHBORHOOD.
//
// Simulation maths goes through SimMath.

#pragma once

#include "Common/INI.h"
#include "Common/INIDataTypes.h"
#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/Module/UpdateModule.h"
#include "GameLogic/ObjectTypes.h"

#include <string>
#include <vector>

class GameLogic;
class ModuleFactory;
class Object;
class Player;
class ThingTemplate;

class WallHubBehaviorModuleData : public ModuleData
{
public:
	std::vector<std::string> m_segmentTemplateNames; // +8
	std::string m_hubCapTemplateName;                 // +0x14
	std::string m_defaultSegmentTemplateName;         // +0x18
	std::string m_cliffCapTemplateName;               // +0x1C
	std::string m_shoreCapTemplateName;               // +0x20
	std::string m_borderCapTemplateName;              // +0x24
	std::string m_elevatedSegmentTemplateName;        // +0x28
	float m_builderRadius = 9.876f;                   // +0x2C (RW 0xC565A0)
	float m_maxBuildoutDistance = 54321.0f;           // +0x30 (RW 0xC565A4: unset)
	unsigned m_options = 0;                           // +0x34
	int m_staggeredBuildFactor = 5;                   // +0x38
	static void buildFieldParse(MultiIniFieldParse &p);
};

class WallHubBehavior : public UpdateModule
{
public:
	WallHubBehavior(Thing *thing, const WallHubBehaviorModuleData *data);
	UpdateSleepTime update() override { return UPDATE_SLEEP_FOREVER; } // RW 0x88B211
	void crc(StateHasher &hasher) const override;
	const WallHubBehaviorModuleData *data() const { return m_data; }
	// the interface (RW 0xC565A8); a template name the store lacks answers null (as RW 0x6D1305)
	const ThingTemplate *patternTemplate(unsigned index) const; // slot 1
	const ThingTemplate *hubCapTemplate() const;                // slot 2
	const ThingTemplate *defaultSegmentTemplate() const;        // slot 3
	const ThingTemplate *cliffCapTemplate() const;              // slot 4
	float builderRadius() const;                                 // slot 11
	bool maxBuildoutDistance(float &out) const;                 // slot 12
	int staggeredBuildFactor() const { return m_data->m_staggeredBuildFactor; } // slot 14

	// RW 0x693B55: the hub module of `obj` whose Options are `options` (0: the first one)
	static WallHubBehavior *find(const Object &obj, unsigned options);

private:
	const ThingTemplate *byName(const std::string &name) const;
	const WallHubBehaviorModuleData *m_data;
};

namespace WallSpan
{
struct Tile
{
	Coord3D pos{};
	float angle = 0.0f;
	const ThingTemplate *tmpl = nullptr;
	int code = 0; // the legal-build code of the tile (0 = legal; 7 = the owner cannot pay for the span so far; 9 = beyond MaxBuildoutDistance of every command centre)
};
struct Plan
{
	std::vector<Tile> tiles;
	int cost = 0;               // + 4
	bool endsOnHub = false;     // + 0xC
	bool endCapPlaced = false;  // + 0xD
	int worstCode = 0;          // + 0x10
	ObjectID endHub = INVALID_ID; // + 0x14
};

// the geometry's half extent in y over its active shapes (RW 0xAD2860 / 0xAD2040: template + 0xC8): the bounding box starts at 0 and takes every active shape's
// offset +- its extent (a box: the minor radius in y; a sphere / cylinder: the major radius)
float halfExtentY(const ThingTemplate &tt);

// RW 0x79586E. false when nothing can be planned (no hub module / hub cap, one tile or fewer)
bool plan(GameLogic &logic, Object &hub, const Coord3D &start, const Coord3D &end, unsigned options, Plan &out);
// RW 0x795221: plans, then (every tile legal, the owner can pay) makes every tile through BuildAssistant slot 0x38 with the hub as builder and starts it as a foundation
// at 1.0 health, staggered by StaggeredBuildFactor * index frames (lane BUILD-4: in retail's order - the stagger first, then percent 0, the status bits, the
// construction conditions and the drawable's fade-in over 138 client frames, S-1520), links the tiles, and charges the whole cost. The made objects go to `made`
// (may be null)
bool build(GameLogic &logic, Object &hub, const Coord3D &start, const Coord3D &end, Player &owner, unsigned options, std::vector<ObjectID> *made);

void registerModules(ModuleFactory &modules);
std::vector<std::string> stopLines();
} // namespace WallSpan
