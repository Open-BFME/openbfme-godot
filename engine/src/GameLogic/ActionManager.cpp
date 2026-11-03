// OpenBFME. GPL-3.0.
// See GameLogic/ActionManager.h for the sources and what is inference.

#include "GameLogic/ActionManager.h"

#include "Common/Player.h"
#include "Common/SpecialPower.h"
#include "GameClient/ControlBarCommands.h"
#include "GameLogic/AI/AIPathfind.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameLogic/Object/Contain/OpenContainRuntime.h"
#include "GameLogic/Module/CastleModules.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "Common/Upgrade.h"
#include "GameLogic/Module/PhysicsBehavior.h"
#include "GameLogic/Module/SpecialAbilityModules.h"
#include "GameLogic/Module/SpecialPowerModules.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/PartitionManager.h"
#include "GameLogic/ObjectFilter.h"
#include "GameLogic/ObjectFilterMatch.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/System/ShroudManager.h"

#include <memory>
#include <stdexcept>

namespace
{
const char *const kStopUpgrade =
	"[S-1871] ActionManager::canDoSpecialPowerAtLocation (RW 0x75CF21): a FIRE_WEAPON button (RW 0x75CC3E: Object + 0x478, unidentified) is not ported: "
	"it answers no (lane MODCOMPAT-AI ported the upgrade buttons: the production queue test RW 0x68C327 and RW 0x694914 / 0x691421)";
const char *const kStopLayer =
	"[S-1872] ActionManager RW 0x82C366 (an AI moving along its path, AIUpdate + 0x1FC in {1, 4} with a path whose node + 0x20 is set, on a pathfinder cell "
	"whose bits 4 .. 9 exceed 0x10 refuses every power) is ported (lane WALL-2: walls, portals); the path's waypoint test (RW 0x765522) is not cached at + 0x24";
const char *const kStopRequirements =
	"[S-1873] the location requirements RW 0x897E01: RW 0x896DB4 (power type 0x20: the object's special ability state, RW 0x68C89B, and its AI's last "
	"command frame) is not ported (answers no for that type); only SpecialAbilityUpdate answers the special power update interface (vslot 0x64)";
const char *const kStopTypeTest =
	"[S-1874] ActionManager RW 0x82DFB7: the per-power-type tests of the building powers (0x52, 0x61, 0x67, 0x6A: RW 0x82E2DE), 0x27 (RW 0x67F52B), 0x38 "
	"(RW 0x76A247), 0x41 (RW 0x82E252), 0x49 (RW 0x680A75 layer) and 0x57 (the rebuild scan RW 0x82E93C) are not ported, and the summons' spot test (0x55, 0x56, "
	"0x5B, 0x5D .. 0x5F, 0x73, 0x76, 0x78, 0x7B, 0x7C, 0x7E, 0x96, 0x9A: RW 0x82E586) is ported (lane SUMMONLOC-1: the partition's geometry overlap, distance "
	"type 4, on the template's shapes, S-1020): the unported types answer no (counted per type)";

const char *const kStopObjectTypeTest =
	"[S-7140] ActionManager::canDoSpecialPowerAtObject (RW 0x82D925, lane MODCOMPAT-MOD1): the per-power-type object tests of the capture types (0x1D "
	"and the capture branch of 0x1A / 0x1D: RW 0x82D0A1), 0x33 (RW 0x82D780), 0x8A (a STRUCTURE's canRepairObject RW 0x82D7F2) and the grab types' fake "
	"tree target (an object id 0x5F5E0FC .. 0x5F5E0FF: the GrabPassengerSpecialPower's AllowTree and the template's byte + 0x5FB, RW 0x82DB01 .. 0x82DB5D) are "
	"not ported: they answer no (counted per type); the grab types' object test (RW 0x82DB6A .. 0x82DC80) is ported";

int kindBit(const char *name)
{
	return ObjectTemplateInfoBuilder::kindOfIndex(name);
}

const SpecialPowerTemplate *powerOf(const CommandButton &b)
{
	return TheSpecialPowerStore && !b.m_specialPowerName.empty() ? TheSpecialPowerStore->findSpecialPowerTemplate(b.m_specialPowerName) : nullptr;
}

// the UnitCost test (RW 0x82E000 / 0x82D605): with a cost, the object's horde interface (Object + 0x258 slot 0x7C) member count (slot 0x188) must reach it.
// INFERENCE: slot 0x188 is the member count (the script condition RW 0x7E6195 reads it so); no contain or no horde interface: no test
bool unitCostMet(const Object &obj, const SpecialPowerTemplate &t)
{
	if (t.m_unitCost == 0)
	{
		return true;
	}
	ContainModuleInterface *c = obj.getContain();
	const HordeContainInterface *h = c ? c->getHordeContainInterface() : nullptr;
	return !h || c->getContainCount() >= (unsigned)t.m_unitCost;
}

// RW 0x82C366 (lane WALL-2): the pathfinder exists; the object's AI follows a path (RW 0x6628CD: AIUpdate + 0x1FC 1 or 4, a path, and a node of it carries a
// waypoint id, RW 0x765522); the ground grid's layer at the object's position (RW 0x6EACB9, the point raised by RW 0xBDD97C: the cell is read at x, y) is a
// wall top (0x11 and up): every power is refused
bool movingOnUpperLayer(Object &obj)
{
	AIWorld *world = obj.logic().aiWorld();
	const AIUpdateInterface *ai = obj.getAIUpdateInterface();
	if (!world || !world->mapReady() || !ai)
	{
		return false;
	}
	const AIMover &m = ai->mover();
	const Path *path = m.path();
	if (!(m.goalType() == AIGOAL_ON_PATH || m.goalType() == AIGOAL_EXPLICIT_WITH_PATH) || !path || !path->hasWaypointNodes())
	{
		return false;
	}
	const Coord3D &p = *obj.getPosition();
	const PathfindCell *c = world->pathfinder().cellAt(SimMath::floorToInt(SimMath::mulf32(p.x, 0.1f)), SimMath::floorToInt(SimMath::mulf32(p.y, 0.1f)));
	const bool upper = c && (int)c->getLayer() >= (int)LAYER_WALL_FIRST;
	if (upper)
	{
		++ActionManager::stats().movingOnLayer;
		obj.logic().noteStop(kStopLayer);
	}
	return upper;
}

const PathfindTerrain *terrainOf(GameLogic &logic)
{
	AIWorld *ai = logic.aiWorld();
	return ai ? ai->pathfinder().terrainView() : nullptr;
}

// RW 0x6EACDF(pos, layer 1): the ground layer's cell under the position is WATER (1) or DEEP_WATER (7); no cell: no
bool waterCellAt(GameLogic &logic, const Coord3D &pos)
{
	AIWorld *ai = logic.aiWorld();
	if (!ai)
	{
		throw std::logic_error("ActionManager RW 0x6EACDF: the game has no pathfinder");
	}
	const PathfindCell *cell = ai->pathfinder().getCell(LAYER_GROUND, &pos);
	return cell && (cell->getType() == PathfindCell::CELL_WATER || cell->getType() == PathfindCell::CELL_DEEP_WATER);
}

// RW 0x82D415 (WATER_OK clear): no water under the location or, with a cursor radius r >= 1 (cvttss2si), under any point of the step-2 integer grid strictly
// inside the circle (SSE on the cvtsi2ss values)
bool waterTestPasses(GameLogic &logic, const Coord3D &loc, const SpecialPowerTemplate &t)
{
	if (t.m_flags & SPF_WATER_OK)
	{
		return true;
	}
	const int r = SimMath::truncToInt32(t.m_radiusCursorRadius);
	if (r <= 0)
	{
		return !waterCellAt(logic, loc);
	}
	const float rf = (float)r;
	const int x0 = SimMath::truncToInt32(SimMath::subf32(loc.x, rf)), x1 = SimMath::truncToInt32(SimMath::addf32(rf, loc.x));
	const int y0 = SimMath::truncToInt32(SimMath::subf32(loc.y, rf)), y1 = SimMath::truncToInt32(SimMath::addf32(rf, loc.y));
	const float r2 = (float)(r * r);
	for (int y = y0; y <= y1; y += 2)
	{
		const float fy = (float)y;
		const float dy = SimMath::subf32(fy, loc.y);
		const float dy2 = SimMath::mulf32(dy, dy);
		for (int x = x0; x <= x1; x += 2)
		{
			const float fx = (float)x;
			const float dx = SimMath::subf32(fx, loc.x);
			if (r2 > SimMath::addf32(SimMath::mulf32(dx, dx), dy2))
			{
				const Coord3D p{ fx, fy, loc.z };
				if (waterCellAt(logic, p))
				{
					return false;
				}
			}
		}
	}
	return true;
}

// the default ObjectFilter of a SpecialPowerTemplate (RW 0x7B1F5B -> RW 0x763D11 with the empty KindOf mask RW 0xDE49E4: rule NONE, empty masks, as
// CostModifierUpgrade's constructor builds it, RW 0x8B9E08)
const ObjectFilter &filterOrDefault(const std::shared_ptr<const ObjectFilter> &f)
{
	static const ObjectFilter kDefault = ObjectFilter::none(KindOfMaskType{}, KindOfMaskType{});
	return f ? *f : kDefault;
}

// the shroud status of the location for the controlling player (ShroudManager RW 0xB4D9A0 -> 0xB4FB20 with the player index, Player + 0x54)
int shroudAt(Object &obj, const Coord3D &loc)
{
	GameLogic &logic = obj.logic();
	const Player *p = obj.getControllingPlayer();
	if (!logic.shroud() || !p)
	{
		throw std::logic_error("ActionManager RW 0xB4D9A0: no shroud manager or no controlling player");
	}
	return (int)logic.shroud()->getStatusAt(p->getPlayerIndex(), loc.x, loc.y);
}

// RW 0x897E01 (slot 0x48): the module's requirements (RW 0x8969E5), UseDistanceFromCommandCenter (only with a location: the object form RW 0x82DAAB passes
// none), RW 0x896DB4, the special power update modules
bool moduleAllows(Object &obj, SpecialPowerModuleInterface &m, const SpecialPowerTemplate &t, const Coord3D *locOrNull)
{
	GameLogic &logic = obj.logic();
	if (!m.requirementsMet())
	{
		return false;
	}
	const SpecialPowerModule *spm = dynamic_cast<const SpecialPowerModule *>(&m);
	if (locOrNull && spm && spm->spData()->m_useDistanceFromCommandCenter)
	{
		const Coord3D &loc = *locOrNull;
		// RW 0x897D2D: no controlling player -> passes; else the player's COMMANDCENTER objects (RW 0x6B00F3: template + 0x10A bit 1) in the player's object
		// walk; one whose position is within DistanceFromCommandCenter (Coord3D::length RW 0x403111 of loc - position) -> passes
		const Player *player = obj.getControllingPlayer();
		if (player)
		{
			static const int kCommandCenter = kindBit("COMMANDCENTER");
			bool near = false;
			for (Object *o = logic.getFirstObject(); o && !near; o = o->getNextObject())
			{
				if (o->getControllingPlayer() != player || kCommandCenter < 0 || !o->isKindOf((unsigned)kCommandCenter))
				{
					continue;
				}
				const Coord3D *p = o->getPosition();
				const float len = (float)SimMath::length3d(SimMath::subf32(loc.x, p->x), SimMath::subf32(loc.y, p->y), SimMath::subf32(loc.z, p->z));
				near = len <= spm->spData()->m_distanceFromCommandCenter;
			}
			if (!near)
			{
				return false;
			}
		}
	}
	if (t.getSpecialPowerType() == 0x20)
	{
		logic.noteStop(kStopRequirements); // RW 0x896DB4
		return false;
	}
	for (const std::unique_ptr<BehaviorModule> &b : obj.modules())
	{
		const SpecialPowerUpdateInterface *u = dynamic_cast<const SpecialPowerUpdateInterface *>(b.get());
		if (u && u->drivesSpecialPower(&t)) // slot 0x1C with the module's power (module data + 0x8)
		{
			return u->allowsSpecialPowerUse(); // slot 0x20
		}
	}
	return true;
}

// TheTerrainLogic vslots 0x50 (isCliffCell) / 0x58 (ImpassabilityToPlayers plane, MAP-1's planeA) at the location (fld / fstp of the floats: exact)
bool cliffOrImpassable(GameLogic &logic, const Coord3D &loc)
{
	const PathfindTerrain *terrain = terrainOf(logic);
	if (!terrain)
	{
		throw std::logic_error("ActionManager RW 0x82E184: the game has no terrain");
	}
	return terrain->isCliffCell(loc.x, loc.y) || terrain->impassableToPlayers(loc.x, loc.y);
}

// RW 0x82E913's table (types 10 .. 0x9E)
enum FinalTest
{
	FINAL_NO,
	FINAL_CLEAR,       // RW 0x82E91A: the shroud status is CLEAR (0)
	FINAL_YES,         // RW 0x82ED8E
	FINAL_NOT_SHROUD,  // RW 0x82ED95: the status is not SHROUDED (2)
	FINAL_GRID_CLIFF,  // RW 0x82EC27 (0x48 .. 0x4A)
	FINAL_RANGE,       // RW 0x82ED67 (0x92)
	FINAL_UNPORTED,    // 0x27, 0x57
	FINAL_GRID_PLANE,  // RW 0x82EA58 (0x59, 0x68)
	FINAL_ARMY         // RW 0x82EB27 (0x63)
};

FinalTest finalTestOf(int type)
{
	switch (type)
	{
	case 0x10: case 0x1C: case 0x23: case 0x2A: case 0x2F: case 0x32: case 0x3F: case 0x41: case 0x43: case 0x50: case 0x54: case 0x6B: case 0x70: case 0x71:
	case 0x8C: case 0x8E: case 0x93: case 0x9B: case 0x9C: case 0x9D: case 0x9E:
		return FINAL_CLEAR;
	case 0x34: case 0x35: case 0x36: case 0x3C: case 0x3D: case 0x3E: case 0x42: case 0x46: case 0x4B: case 0x4C: case 0x4E: case 0x51: case 0x53: case 0x58:
	case 0x5A: case 0x5C: case 0x62: case 0x64: case 0x65: case 0x66: case 0x69: case 0x6C: case 0x6D: case 0x6E: case 0x6F: case 0x72: case 0x75: case 0x77:
	case 0x79: case 0x7A: case 0x7D: case 0x7F: case 0x80: case 0x85: case 0x8F: case 0x98:
		return FINAL_YES;
	case 0x38: case 0x4D: case 0x60: case 0x86: case 0x91: case 0x94: case 0x95: case 0x99:
		return FINAL_NOT_SHROUD;
	case 0x48: case 0x49: case 0x4A:
		return FINAL_GRID_CLIFF;
	case 0x92:
		return FINAL_RANGE;
	case 0x27: case 0x57:
		return FINAL_UNPORTED;
	case 0x59: case 0x68:
		return FINAL_GRID_PLANE;
	case 0x63:
		return FINAL_ARMY;
	default:
		return FINAL_NO;
	}
}

// the integer grid walks of RW 0x82EC27 / 0x82EA58 / 0x82EB77 / 0x82E753: x and y from cvttss2si(l - h) to cvttss2si(l + h) (SSE) in steps; each point
// (cvtsi2ss x, cvtsi2ss y) is refused by TheTerrainLogic vslot 0x54 (the third plane) or, with `cliffToo`, vslot 0x50 (cliff). A zero step would loop for
// ever in retail (a radius below 5): reported as an error instead
bool gridClear(GameLogic &logic, const Coord3D &loc, float half, int step, bool cliffToo)
{
	const PathfindTerrain *terrain = terrainOf(logic);
	if (!terrain)
	{
		throw std::logic_error("ActionManager: the game has no terrain");
	}
	if (step <= 0)
	{
		throw std::logic_error("ActionManager RW 0x82EC27 / 0x82EA58: a grid step of 0 (RadiusCursorRadius below 5) never ends in retail");
	}
	const int x0 = SimMath::truncToInt32(SimMath::subf32(loc.x, half)), x1 = SimMath::truncToInt32(SimMath::addf32(half, loc.x));
	const int y0 = SimMath::truncToInt32(SimMath::subf32(loc.y, half)), y1 = SimMath::truncToInt32(SimMath::addf32(half, loc.y));
	for (int y = y0; y <= y1; y += step)
	{
		for (int x = x0; x <= x1; x += step)
		{
			if (terrain->thirdPlane((float)x, (float)y) || (cliffToo && terrain->isCliffCell((float)x, (float)y)))
			{
				return false;
			}
		}
	}
	return true;
}

// RW 0x444D39(0, bit) + 0x445139(mask, none): KindOf `bit` set (the partition KindOf filter RW 0xBD88B0)
bool hasKindBit(const Object &o, int bit)
{
	return bit >= 0 && o.isKindOf((unsigned)bit);
}

// RW 0x82E586 (the summons): RadiusCursorRadius r; the objects overlapping the circle r (distance type 4, RW 0xA39340) that have none of INERT,
// NO_COLLIDE, IGNORED_IN_GUI, ROCK_VENDOR, TAINTEFFECT (RW 0x6615E5(0, 0x59, 0x1E, 0x2F, 0x88, 0xAF) as the excluded mask of RW 0x445139) and are alive:
// one that is IMMOBILE or STRUCTURE (RW 0x46E72F(0, 2, 7), any) but neither BASE_SITE nor CRATE, or whose template ShockwaveResistance (+ 0x620) is
// above 100 (RW 0xBD88D8, SSE) -> no; the BASE_SITE objects within r + 500 (RW 0xBDD97C, distance type 0) with a CastleBehavior (RW 0x797AC1 key): the
// location's 3D distance (Coord3D::length RW 0x403111) below MaxCastleRadius (+ 0x2C, RW 0x797ED7) + r (x87 fadd) -> no; the grid from loc - r to loc + r
// with the step cvttss2si(r * 0.2 + (float)v) (SSE: mulss 0.2 RW 0xBDAD78, addss) refused by vslot 0x54 or 0x50 -> no; then the shroud status is CLEAR
bool summonSpotClear(Object &obj, const Coord3D &loc, const SpecialPowerTemplate &t)
{
	GameLogic &logic = obj.logic();
	const float r = t.m_radiusCursorRadius;
	static const int kInert = kindBit("INERT"), kNoCollide = kindBit("NO_COLLIDE"), kIgnoredInGui = kindBit("IGNORED_IN_GUI"), kRockVendor = kindBit("ROCK_VENDOR"),
					 kTaintEffect = kindBit("TAINTEFFECT"), kImmobile = kindBit("IMMOBILE"), kStructure = kindBit("STRUCTURE"), kBaseSite = kindBit("BASE_SITE"),
					 kCrate = kindBit("CRATE");
	PartitionFilterFn excluded([](Object &o) {
		return !(hasKindBit(o, kInert) || hasKindBit(o, kNoCollide) || hasKindBit(o, kIgnoredInGui) || hasKindBit(o, kRockVendor) || hasKindBit(o, kTaintEffect));
	});
	PartitionFilterFn alive([](Object &o) { return !o.isEffectivelyDead(); });
	for (const PartitionHit &h : logic.partition().iterateObjectsInRange(loc, r, FROM_GEOMETRY_OVERLAP, { &excluded, &alive }, ITER_FASTEST))
	{
		const Object &c = *h.object;
		if ((hasKindBit(c, kImmobile) || hasKindBit(c, kStructure)) && !hasKindBit(c, kBaseSite) && !hasKindBit(c, kCrate))
		{
			return false;
		}
		if (ObjectKnockback::shockwaveResistance(c) > 100.0f)
		{
			return false;
		}
	}
	PartitionFilterFn baseSite([](Object &o) { return hasKindBit(o, kBaseSite); });
	for (const PartitionHit &h : logic.partition().iterateObjectsInRange(loc, SimMath::addf32(r, 500.0f), FROM_CENTER_2D, { &baseSite }, ITER_FASTEST))
	{
		const CastleBehavior *castle = nullptr; // RW 0x68BDA5 with the CastleBehavior key (RW 0x797AC1)
		for (const std::unique_ptr<BehaviorModule> &m : h.object->modules())
		{
			if ((castle = dynamic_cast<const CastleBehavior *>(m.get())) != nullptr)
			{
				break;
			}
		}
		if (!castle)
		{
			continue;
		}
		const float castleRadius = castle->data()->m_maxCastleRadius; // fstp dword
		const Coord3D *p = h.object->getPosition();
		// RW 0x82E72E: RW 0x403111's root under PC24 stays in ST0 for the compare (no fstp): the PC24 root of the float32 sum of squares
		const double len = SimMath::sqrtPC24((double)SimMath::sumSquares3(SimMath::subf32(loc.x, p->x), SimMath::subf32(loc.y, p->y), SimMath::subf32(loc.z, p->z)));
		if (SimMath::pc24AddW((double)castleRadius, (double)r) > len)
		{
			return false;
		}
	}
	const PathfindTerrain *terrain = terrainOf(logic);
	if (!terrain)
	{
		throw std::logic_error("ActionManager RW 0x82E753: the game has no terrain");
	}
	const float stepScale = SimMath::mulf32(r, 0.2f);
	const int x1 = SimMath::truncToInt32(SimMath::addf32(loc.x, r)), y0 = SimMath::truncToInt32(SimMath::subf32(loc.y, r));
	const int y1 = SimMath::truncToInt32(SimMath::addf32(loc.y, r)), x0 = SimMath::truncToInt32(SimMath::subf32(loc.x, r));
	for (int y = y0; y <= y1;)
	{
		for (int x = x0; x <= x1;)
		{
			const float fx = (float)x, fy = (float)y;
			if (terrain->thirdPlane(fx, fy) || terrain->isCliffCell(fx, fy))
			{
				return false;
			}
			const int nx = SimMath::truncToInt32(SimMath::addf32(stepScale, fx));
			if (nx <= x)
			{
				throw std::logic_error("ActionManager RW 0x82E7F1: a summon grid step of 0 never ends in retail");
			}
			x = nx;
		}
		const int ny = SimMath::truncToInt32(SimMath::addf32(stepScale, (float)y));
		if (ny <= y)
		{
			throw std::logic_error("ActionManager RW 0x82E80F: a summon grid step of 0 never ends in retail");
		}
		y = ny;
	}
	return shroudAt(obj, loc) == CELLSHROUD_CLEAR;
}

bool unportedTypeTest(GameLogic &logic, int type)
{
	ActionManager::Stats &s = ActionManager::stats();
	++s.typeTests;
	if (type >= 0 && type < (int)s.unportedTypeTests.size())
	{
		++s.unportedTypeTests[(size_t)type];
	}
	logic.noteStop(kStopTypeTest);
	return false;
}
// RW 0x82D925's unported per-type tests (S-7140): no, counted
bool unportedObjectTypeTest(GameLogic &logic, int type, bool *unported)
{
	ActionManager::Stats &s = ActionManager::stats();
	++s.objectTypeTests;
	if (type >= 0 && type < (int)s.unportedObjectTypeTests.size())
	{
		++s.unportedObjectTypeTests[(size_t)type];
	}
	if (unported)
	{
		*unported = true;
	}
	logic.noteStop(kStopObjectTypeTest);
	return false;
}

// RW 0x68DF46 (Object + 0x3C0, RW 0x68CEFE: bit `type` of the object's special power bits). INFERENCE (ZH Object::Object fills m_specialPowerBits
// from each module's getSpecialPower()->getSpecialPowerTemplate()->getSpecialPowerType()): some module of the object has a special power of that type
// lane MODCOMPAT-MOD1: RW 0x82DAF0 .. 0x82DC80, the grab types' object test (SPECIAL_GRAB_PASSENGER 0x27 / SPECIAL_GRAB_CHUNK 0x28). A fake tree id (0x5F5E0FC ..
// 0x5F5E0FF) is the tree branch (S-7140, counted). Else: a WONT_RIDE_WITH_YOU target (status 0x63) is refused; with options bits 0 and 1 both set and the
// object not a HORDE_MEMBER (status 0x26) the object's contain must allow it by its ManualPickUpFilter (contain slot 0x170, RW 0x865A5C); the object needs a
// contain; a target inside a container needs that container's horde interface (contain slot 0x7C); a HORDE target gives its first member (horde interface
// slot 0x48(0, 0, 0.0, 0, 0)), none refuses; another target must not be TAGGED (status 0x3F). A CHUNK_VENDOR (template + 0x118 bit 4) answers only 0x28, a
// ROCK_VENDOR (+ 0x119 bit 0) only 0x27; then the contain's isValidContainerFor(target, 1, 0) (slot 0x98) decides
bool grabObjectTest(Object &obj, Object &target, int type, unsigned options, bool *unported)
{
	GameLogic &logic = obj.logic();
	const ObjectID id = target.getID();
	if ((int)id >= 0x5F5E0FC && (int)id <= 0x5F5E0FF) // RW 0x82DAF3 / 0x82DAFA
	{
		return unportedObjectTypeTest(logic, type, unported);
	}
	static const int kWontRide = ObjectTemplateInfoBuilder::objectStatusIndex("WONT_RIDE_WITH_YOU"); // 0x63
	static const int kHordeMember = ObjectTemplateInfoBuilder::objectStatusIndex("HORDE_MEMBER");    // 0x26
	static const int kTagged = ObjectTemplateInfoBuilder::objectStatusIndex("TAGGED");               // 0x3F
	if (kWontRide < 0 || kHordeMember < 0 || kTagged < 0)
	{
		throw std::logic_error("ActionManager RW 0x82DB6A: an object status of the grab test is unknown");
	}
	if (target.testStatus((unsigned)kWontRide))
	{
		return false;
	}
	ContainModuleInterface *contain = obj.getContain(); // Object + 0x258
	if ((options & 2u) && (options & 1u) && !obj.testStatus((unsigned)kHordeMember))
	{
		const OpenContain *open = dynamic_cast<const OpenContain *>(contain); // slot 0x170 (RW 0x865A5C, OpenContain's)
		if (!open || !open->manualPickUpAllows(target))
		{
			return false; // RW calls slot 0x170 on the contain unchecked; no OpenContain answers no here
		}
	}
	if (!contain)
	{
		return false;
	}
	if (Object *holder = target.getContainedBy()) // Object + 0x27C
	{
		ContainModuleInterface *hc = holder->getContain();
		if (!hc || !hc->getHordeContainInterface())
		{
			return false;
		}
	}
	Object *grabbed = &target;
	static const int kHorde = kindBit("HORDE"); // template + 0x115 bit 5
	if (kHorde >= 0 && target.isKindOf((unsigned)kHorde))
	{
		ContainModuleInterface *tc = target.getContain();
		HordeContain *horde = tc && tc->getHordeContainInterface() ? dynamic_cast<HordeContain *>(tc) : nullptr;
		grabbed = horde ? horde->pickMemberNoPosition(false) : nullptr; // slot 0x48(0, 0, 0.0, 0, 0)
		if (!grabbed)
		{
			return false;
		}
	}
	else if (target.testStatus((unsigned)kTagged))
	{
		return false;
	}
	static const int kChunkVendor = kindBit("CHUNK_VENDOR"); // template + 0x118 bit 4
	static const int kRockVendor = kindBit("ROCK_VENDOR");   // template + 0x119 bit 0
	if ((kChunkVendor >= 0 && grabbed->isKindOf((unsigned)kChunkVendor) && type != 0x28) || (kRockVendor >= 0 && grabbed->isKindOf((unsigned)kRockVendor) && type != 0x27))
	{
		return false;
	}
	return contain->isValidContainerFor(*grabbed, true, false); // slot 0x98(target, 1, 0)
}

bool hasSpecialPowerOfType(const Object &obj, int type)
{
	for (const std::unique_ptr<BehaviorModule> &m : obj.modules())
	{
		const SpecialPowerModuleInterface *sp = m->getSpecialPower();
		const SpecialPowerTemplate *t = sp ? sp->getSpecialPowerTemplate() : nullptr;
		if (t && t->getSpecialPowerType() == type)
		{
			return true;
		}
	}
	return false;
}

// lane SPELL-4: an unported per-type test answers no as before and, for a caller that asks, says so
bool unportedAnswer(bool *unported, GameLogic &logic, int type)
{
	if (unported)
	{
		*unported = true;
	}
	return unportedTypeTest(logic, type);
}
} // namespace

namespace ActionManager
{
Stats &stats()
{
	static thread_local Stats s;
	return s;
}

// lane MODCOMPAT-AI: RW 0x75CF21's first branch: a button whose Upgrade (+ 0x24, the resolved template: an unknown name leaves it null) is set and whose
// command is not FIRE_WEAPON (+ 0x14 == 0x17) is about buying that upgrade: the object's ProductionUpdate (RW 0x68C327(0); none: no) must hold no upgrade
// entry (its entries by vslots 0x54 / 0x58: type 2 with an upgrade, + 4 / + 0xC: no), then the object must be affected by the upgrade (RW 0x694914) and
// not have it (RW 0x691421)
bool upgradeButtonUsable(Object &obj, const UpgradeTemplate &upgrade)
{
	const ProductionUpdateInterface *pu = obj.getProductionUpdate();
	if (!pu)
	{
		return false;
	}
	for (const ProductionEntry *e = pu->firstProduction(); e; e = pu->nextProduction(e))
	{
		if (e->type == PRODUCTION_UPGRADE && e->upgradeToResearch)
		{
			return false;
		}
	}
	return obj.affectedByUpgrade(&upgrade) && !obj.hasUpgrade(&upgrade);
}

const UpgradeTemplate *buttonUpgrade(const CommandButton &button)
{
	return button.m_upgradeName.empty() ? nullptr : Player::resolveUpgrade(button.m_upgradeName, false);
}

bool canDoSpecialPowerAtLocation(Object &obj, const CommandButton &button, const Coord3D &loc)
{
	// RW 0x75CF21(this = button, obj, target 0, loc, source 0)
	GameLogic &logic = obj.logic();
	if (const UpgradeTemplate *upgrade = buttonUpgrade(button); upgrade && button.m_command != GUI_COMMAND_FIRE_WEAPON)
	{
		return upgradeButtonUsable(obj, *upgrade);
	}
	const std::uint32_t options = button.m_options;
	if (options & 7u)
	{
		return false; // a target object is needed and there is none
	}
	if (options & COMMAND_OPTION_NEED_TARGET_POS)
	{
		return canDoSpecialPowerAtLocation(obj, loc, powerOf(button), options);
	}
	if (button.m_command == GUI_COMMAND_FIRE_WEAPON)
	{
		++stats().fireWeaponButtons;
		logic.noteStop(kStopUpgrade);
		return false;
	}
	return canDoSpecialPower(obj, powerOf(button));
}

bool canUseCommandButton(Object &obj, const CommandButton &button)
{
	// lane MODCOMPAT-AI: RW 0x75CF21(this = button, obj, target 0, loc 0, source): the source only reaches RW 0x82D5DA's third argument, which it does not
	// read; so every source gives this answer
	GameLogic &logic = obj.logic();
	if (const UpgradeTemplate *upgrade = buttonUpgrade(button); upgrade && button.m_command != GUI_COMMAND_FIRE_WEAPON)
	{
		return upgradeButtonUsable(obj, *upgrade);
	}
	const std::uint32_t options = button.m_options;
	if ((options & 7u) || (options & COMMAND_OPTION_NEED_TARGET_POS))
	{
		return false; // a target object or a location is needed and there is none (RW 0x75CF48 .. 0x75CF5A)
	}
	if (button.m_command == GUI_COMMAND_FIRE_WEAPON)
	{
		++stats().fireWeaponButtons;
		logic.noteStop(kStopUpgrade);
		return false;
	}
	return canDoSpecialPower(obj, powerOf(button));
}

int finalTestKind(int type)
{
	return (int)finalTestOf(type);
}

bool canDoSpecialPower(Object &obj, const SpecialPowerTemplate *power)
{
	// RW 0x82D5DA(obj, power, 0, options, 0)
	if (!power)
	{
		return false;
	}
	if (movingOnUpperLayer(obj) || !unitCostMet(obj, *power))
	{
		return false;
	}
	if (!SpecialPowerModules::findModule(obj, power)) // RW 0x68C26D
	{
		return false;
	}
	return targetlessTypeAccepted(power->getFinalOverride()->getSpecialPowerType());
}

bool targetlessTypeAccepted(int type)
{
	switch (type) // RW 0x82D6B1
	{
	case 0x1B: case 0x25: case 0x27: case 0x28: case 0x29: case 0x2B: case 0x2C: case 0x2E: case 0x34: case 0x35: case 0x36: case 0x37: case 0x38: case 0x3A:
	case 0x3C: case 0x3D: case 0x3E: case 0x42: case 0x4C: case 0x4E: case 0x4F: case 0x51: case 0x58: case 0x5C: case 0x62: case 0x66: case 0x69: case 0x6C:
	case 0x6D: case 0x6E: case 0x72: case 0x74: case 0x75: case 0x77: case 0x79: case 0x7A: case 0x7D: case 0x7F: case 0x80: case 0x83: case 0x85: case 0x87:
	case 0x88: case 0x89: case 0x8D: case 0x8F: case 0x98:
		return true;
	default:
		return false;
	}
}

bool canDoSpecialPowerAtLocation(Object &obj, const Coord3D &loc, const SpecialPowerTemplate *power, unsigned options, bool *unported)
{
	// RW 0x82DFB7(obj, loc, 0, power, 0, options, 0)
	(void)options;
	if (unported)
	{
		*unported = false;
	}
	if (!power)
	{
		return false;
	}
	GameLogic &logic = obj.logic();
	const SpecialPowerTemplate &t = *power->getFinalOverride(); // RW 0x688D3C
	if (!unitCostMet(obj, t) || movingOnUpperLayer(obj))
	{
		return false;
	}
	SpecialPowerModuleInterface *m = SpecialPowerModules::findModule(obj, power); // RW 0x68C26D
	if (!m || !moduleAllows(obj, *m, t, &loc))                                 // slot 0x48 (RW 0x897E01)
	{
		return false;
	}
	// RW 0x82D56A: strictly inside the pathfinder's maximum extent (TheTerrainLogic vslot 0x30) shrunk by RadiusCursorRadius (SSE)
	{
		const PathfindTerrain *terrain = terrainOf(logic);
		if (!terrain)
		{
			throw std::logic_error("ActionManager RW 0x82D56A: the game has no terrain");
		}
		float loX = 0.0f, loY = 0.0f, hiX = 0.0f, hiY = 0.0f;
		terrain->getMaximumPathfindExtent(loX, loY, hiX, hiY);
		const float r = t.m_radiusCursorRadius;
		if (!(loc.x > SimMath::addf32(loX, r) && SimMath::subf32(hiX, r) > loc.x && loc.y > SimMath::addf32(loY, r) && SimMath::subf32(hiY, r) > loc.y))
		{
			return false;
		}
	}
	// RW 0x82D3A3 (LIMIT_DISTANCE): MaxCastRange^2 > (dz^2 + dy^2) + dx^2 of the object's position minus the location (SSE)
	if (t.m_flags & SPF_LIMIT_DISTANCE)
	{
		const Coord3D *p = obj.getPosition();
		const float dx = SimMath::subf32(p->x, loc.x), dy = SimMath::subf32(p->y, loc.y), dz = SimMath::subf32(p->z, loc.z);
		const float d2 = SimMath::addf32(SimMath::addf32(SimMath::mulf32(dz, dz), SimMath::mulf32(dy, dy)), SimMath::mulf32(dx, dx));
		if (!(SimMath::mulf32(t.m_maxCastRange, t.m_maxCastRange) > d2))
		{
			return false;
		}
	}
	// RW 0x82D2C9 (PATHABLE_ONLY): RW 0x6F5BB0(obj, its position, loc, 0), the port's quick path test with the object's locomotor (S-1280, as FindPositionAround)
	if (t.m_flags & SPF_PATHABLE_ONLY)
	{
		AIUpdateInterface *ai = obj.getAIUpdateInterface();
		AIWorld *world = logic.aiWorld();
		if (!ai || !world)
		{
			return false; // RW 0x6F5BB0: no AI (+ 0x260) and no locomotor set given -> no (INFERENCE: its earlier status / Object + 0x4AC tests, RW 0x44DDEC / 0x68C053, do not apply)
		}
		const PathfindLocomotorInfo loco = ai->locomotorInfo();
		if (!world->pathfinder().clientSafeQuickDoesPathExist(loco, obj.getPosition(), &loc, &ai->adapter())) // lane WALL-2: with the unit, its portals
		{
			return false;
		}
	}
	if (!waterTestPasses(logic, loc, t)) // RW 0x82D415
	{
		return false;
	}
	const Player *player = obj.getControllingPlayer();
	// RW 0x82DEA6 (NEEDS_OBJECT_FILTER): some live object within RadiusCursorRadius (distance type 0) passes ObjectFilter for the controlling player
	if (t.m_flags & SPF_NEEDS_OBJECT_FILTER)
	{
		const ObjectFilter &f = filterOrDefault(t.m_objectFilter);
		PartitionFilterFn pass([&](Object &o) { return ObjectFilterMatch::allows(logic, f, o, player); });
		PartitionFilterFn alive([](Object &o) { return !o.isEffectivelyDead(); });
		if (logic.partition().iterateObjectsInRange(loc, t.m_radiusCursorRadius, FROM_CENTER_2D, { &pass, &alive }, ITER_FASTEST).empty())
		{
			return false;
		}
	}
	// RW 0x82D2FD (NO_FORBIDDEN_OBJECTS): no live object within ForbiddenObjectRange (distance type 1) passes ForbiddenObjectFilter
	if (t.m_flags & SPF_NO_FORBIDDEN_OBJECTS)
	{
		const ObjectFilter &f = filterOrDefault(t.m_forbiddenObjectFilter);
		PartitionFilterFn pass([&](Object &o) { return ObjectFilterMatch::allows(logic, f, o, player); });
		PartitionFilterFn alive([](Object &o) { return !o.isEffectivelyDead(); });
		if (logic.partition().getClosestObject(loc, t.m_forbiddenObjectRange, FROM_CENTER_3D, { &pass, &alive }))
		{
			return false;
		}
	}
	const int type = t.getSpecialPowerType();
	// RW 0x82E135: 0x38 (the AI's slot 0x184 / RW 0x76A247: not ported), then 0x38, 0x41 and 0x8C: no cliff and no ImpassabilityToPlayers cell at the location
	if (type == 0x38)
	{
		return unportedAnswer(unported, logic, type);
	}
	if ((type == 0x41 || type == 0x8C) && cliffOrImpassable(logic, loc))
	{
		return false;
	}
	// RW 0x82E1C2 .. 0x82E8EF: the per-type tests before the final switch
	switch (type)
	{
	case 0x52: case 0x61: case 0x67: case 0x6A: // RW 0x82E2DE (the building powers)
		return unportedAnswer(unported, logic, type);
	case 0x55: case 0x56: case 0x5B: case 0x5D: case 0x5E: case 0x5F: case 0x73: case 0x76: case 0x78: case 0x7B: case 0x7C: case 0x7E: case 0x96: case 0x9A:
		// RW 0x82E586 (the final switch is not reached): its first scan is the partition's geometry overlap (distance type 4, RW 0xA3C7D0 -> RW 0xAD2CE0)
		return summonSpotClear(obj, loc, t);
	case 0x41:                                                                          // RW 0x82E252
	case 0x49:                                                                          // RW 0x82E21A
		return unportedAnswer(unported, logic, type);
	case 0x8C:
	{
		// RW 0x82E860 (0x8C): no STRUCTURE (KindOf bit 7, RW 0x444D39(0, 7) / 0x445139) within 1.0 of the location (distance type 0, RW 0xA39090); the
		// cell of its layer under it (RW 0x680A75) exists and is not of type 5 (RW 0x6EAD90); with a current locomotor (AI + 0x1F0,
		// RW 0x68B31D) its max speed for the condition (RW 0x5E3F49) is not 0
		static const int kStructure = kindBit("STRUCTURE");
		PartitionFilterFn structure([](Object &o) { return kStructure >= 0 && o.isKindOf((unsigned)kStructure); });
		if (logic.partition().getClosestObject(loc, 1.0f, FROM_CENTER_2D, { &structure }))
		{
			return false;
		}
		AIWorld *world = logic.aiWorld();
		const PathfindCell *cell = world ? world->pathfinder().getCell(world->pathfinder().getLayerAt(loc.x, loc.y, loc.z), &loc) : nullptr; // lane WALL-2: the layer
		if (!cell || (int)cell->getType() == 5)
		{
			return false;
		}
		const AIUpdateInterface *ai = obj.getAIUpdateInterface();
		if (ai && ai->curLocomotor() && ai->curLocomotorSpeed() == 0.0f)
		{
			return false;
		}
		break;
	}
	default:
		break;
	}
	// RW 0x82E913
	switch (finalTestOf(type))
	{
	case FINAL_CLEAR:
		return shroudAt(obj, loc) == CELLSHROUD_CLEAR;
	case FINAL_YES:
		return true;
	case FINAL_NOT_SHROUD:
		return shroudAt(obj, loc) != CELLSHROUD_SHROUDED;
	case FINAL_GRID_CLIFF:
	{
		// RW 0x82EC27 (0x48 .. 0x4A): r = cvttss2si(RadiusCursorRadius); the grid +- (float)r with the step r / 5 (idiv) refused by vslot 0x54 or 0x50; an
		// object of KindOf WALK_ON_TOP_OF_WALL (bit 0x3C, RW 0x444D39 / 0x445139) within (float)r (RW 0xA39090, distance type 0) -> no; then CLEAR
		const int r = SimMath::truncToInt32(t.m_radiusCursorRadius);
		if (!gridClear(logic, loc, (float)r, r / 5, true))
		{
			return false;
		}
		static const int kWallWalker = kindBit("WALK_ON_TOP_OF_WALL");
		PartitionFilterFn wall([](Object &o) { return hasKindBit(o, kWallWalker); });
		if (logic.partition().getClosestObject(loc, (float)r, FROM_CENTER_2D, { &wall }))
		{
			return false;
		}
		return shroudAt(obj, loc) == CELLSHROUD_CLEAR;
	}
	case FINAL_GRID_PLANE:
	{
		// RW 0x82EA58 (0x59, 0x68): the same grid with vslot 0x54 only; then the status is not SHROUDED
		const int r = SimMath::truncToInt32(t.m_radiusCursorRadius);
		return gridClear(logic, loc, (float)r, r / 5, false) && shroudAt(obj, loc) != CELLSHROUD_SHROUDED;
	}
	case FINAL_ARMY:
	{
		// RW 0x82EB27 (0x63): no WALK_ON_TOP_OF_WALL object within 200 (RW 0xBE4170, distance type 0); the grid +- 200 with the step 40 refused by vslot 0x54;
		// then CLEAR
		static const int kWallWalker = kindBit("WALK_ON_TOP_OF_WALL");
		PartitionFilterFn wall([](Object &o) { return hasKindBit(o, kWallWalker); });
		if (logic.partition().getClosestObject(loc, 200.0f, FROM_CENTER_2D, { &wall }))
		{
			return false;
		}
		return gridClear(logic, loc, 200.0f, 40, false) && shroudAt(obj, loc) == CELLSHROUD_CLEAR;
	}
	case FINAL_RANGE:
	{
		// RW 0x82ED67: the object's 2D squared distance to the location (RW 0x66137C, x87 wide) at most ViewObjectRange (+ 0x50) squared (fmul, PC24)
		const Coord3D *p = obj.getPosition();
		const double dx = SimMath::pc24SubW((double)p->x, (double)loc.x), dy = SimMath::pc24SubW((double)p->y, (double)loc.y);
		const double d2 = SimMath::pc24AddW(SimMath::pc24MulW(dy, dy), SimMath::pc24MulW(dx, dx));
		const double r2 = SimMath::pc24MulW((double)t.m_viewObjectRange, (double)t.m_viewObjectRange);
		return !(r2 < d2);
	}
	case FINAL_UNPORTED:
		return unportedAnswer(unported, logic, type);
	case FINAL_NO:
	default:
		return false;
	}
}

bool canDoSpecialPowerAtObject(Object &obj, Object *target, CommandSourceType source, const SpecialPowerTemplate *power, unsigned options,
                               bool checkSourceRequirements, bool *unported)
{
	// RW 0x82D925(obj, target, source, power, options, checkSourceRequirements)
	if (unported)
	{
		*unported = false;
	}
	if (!power)
	{
		return false;
	}
	GameLogic &logic = obj.logic();
	const SpecialPowerTemplate &t = *power->getFinalOverride(); // RW 0x688D3C
	const int type = t.getSpecialPowerType();                   // + 0x1C
	if (checkSourceRequirements && !hasSpecialPowerOfType(obj, type)) // RW 0x68DF46
	{
		return false;
	}
	SpecialPowerModuleInterface *m = SpecialPowerModules::findModule(obj, power); // RW 0x68C26D
	const bool isCapture = type == 0x1D || type == 0x1A;
	const bool filterSet = ObjectFilterMatch::isValid(t.m_objectFilter.get()); // power + 0x60, RW 0x762977
	if (filterSet && target && !ObjectFilterMatch::allows(logic, *t.m_objectFilter, *target, obj.getControllingPlayer())) // RW 0x7640C1(target, player)
	{
		return false;
	}
	bool captureOfDead = false;
	if (isCapture)
	{
		if (filterSet)
		{
			captureOfDead = true;
		}
		else if (target)
		{
			// RW 0x82D0A1 (the capture test) decides whether a dead target may be captured: S-7140
			captureOfDead = unportedObjectTypeTest(logic, type, unported);
		}
	}
	if (!target || (target->isEffectivelyDead() && !captureOfDead)) // Object + 0x458 bit 0
	{
		return false;
	}
	if (!unitCostMet(obj, t) || movingOnUpperLayer(obj)) // power + 0x80 against slot 0x188; RW 0x82C366
	{
		return false;
	}
	const Relationship rel = obj.getRelationship(*target); // RW 0x68D7AB
	if (!m)
	{
		return false;
	}
	if (checkSourceRequirements && (m->getPercentReady() < 1.0f || !moduleAllows(obj, *m, t, nullptr))) // vslot 8 (fld1; fcomip; ja), vslot 0x48(0)
	{
		return false;
	}
	// RW 0x82C167(source): the fake tree ids are exempt; scripts are exempt; a human owner's FOGGED / SHROUDED target is refused (S-565)
	const ObjectID id = target->getID();
	const bool treeId = (int)id >= 0x5F5E0FC && (int)id <= 99999999;
	if (!treeId && source != CMD_FROM_SCRIPT && ShroudManager::isShroudedForAction(obj, *target))
	{
		return false;
	}
	switch (type) // RW 0x82DAE9
	{
	case 0x20: case 0x2A: case 0x2D: case 0x31: case 0x3B: case 0x40: case 0x47: case 0x81: case 0x84: case 0x93: case 0x97:
		return true;
	case 0x39: case 0x44: case 0x45: case 0x90:
	{
		static const int kAirborneTarget = ObjectTemplateInfoBuilder::objectStatusIndex("AIRBORNE_TARGET"); // Object + 0x94 bit 6
		return !(kAirborneTarget >= 0 && target->testStatus((unsigned)kAirborneTarget));
	}
	case 0x82:
	{
		if (rel == ENEMIES)
		{
			return true;
		}
		static const int kDeployedMine = kindBit("DEPLOYED_MINE"); // template + 0x11D bit 4
		return (rel == ALLIES || rel == NEUTRAL) && kDeployedMine >= 0 && target->isKindOf((unsigned)kDeployedMine);
	}
	case 0x27: // RW 0x82DAF0 .. 0x82DC80 (lane MODCOMPAT-MOD1)
	case 0x28:
		return grabObjectTest(obj, *target, type, options, unported);
	case 0x1D: // RW 0x82D0A1
	case 0x33: // RW 0x82D780
		return unportedObjectTypeTest(logic, type, unported);
	case 0x8A:
	{
		static const int kStructure = kindBit("STRUCTURE"); // template + 0x108 bit 7
		if (kStructure < 0 || !target->isKindOf((unsigned)kStructure))
		{
			return false;
		}
		return unportedObjectTypeTest(logic, type, unported); // RW 0x82D7F2 (canRepairObject, S-1282)
	}
	default:
		return false;
	}
}

std::vector<std::string> stopLines()
{
	return { kStopUpgrade, kStopLayer, kStopRequirements, kStopTypeTest, kStopObjectTypeTest };
}
} // namespace ActionManager
