// OpenBFME. GPL-3.0.
// See GameLogic/BuildPlacement.h for the sources of every rule.

#include "GameLogic/BuildPlacement.h"

#include "Common/Player.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Map/TerrainLogic.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SimMath.h"

namespace
{
const float kCell = 10.0f; // MAP_XY_FACTOR (ZH: the terrain's heightmap cell, the sample resolution of checkSampleBuildLocation; RotWK RW 0xBD83D8)
const float kMaxWaterLandSampleRatio = 6.0f; // lane BUILD-5: RW 0xDA59A0 (a PORT's samples, either way)
const float kPortWalkStep = 5.0f;            // lane BUILD-5: RW 0xBDAE58 (getPortTransform's walk from the land centre to the water)
const float kPortAngleOffset = 1.5707963705062866f; // lane BUILD-5: RW 0xDA599C (pi / 2, added to the shore direction)

bool templateKindOf(GameLogic &logic, const ThingTemplate &tt, const char *name)
{
	const int bit = ObjectTemplateInfoBuilder::kindOfIndex(name);
	if (bit < 0)
	{
		return false;
	}
	return MaskTest(logic.templateInfo(tt.getFinalOverride()).kindOf, (unsigned)bit);
}

// ZH BuildAssistant::iterateFootprint as BuildPlacement samples it: every active shape's footprint (lane PATH-2, S-341) on a regular grid of `res` that always
// includes the centre line (the grid starts at the negative extent); a template without geometry samples its position. `visit` answers false to stop.
template <class F>
void sampleFootprints(std::vector<BuildPlacement::Footprint> &fps, const Coord3D &pos, float angle, float res, F &&visit)
{
	if (fps.empty())
	{
		BuildPlacement::Footprint fp;
		fp.type = 0;
		fp.major = fp.minor = 0.0f;
		fp.x = pos.x;
		fp.y = pos.y;
		fp.angle = angle;
		fps.assign(1, fp);
	}
	for (const BuildPlacement::Footprint &fp : fps)
	{
		const float c = SimMath::cosDet(fp.angle), s = SimMath::sinDet(fp.angle);
		for (float u = SimMath::subf32(0.0f, fp.major);; u = SimMath::addf32(u, res))
		{
			const float uu = u > fp.major ? fp.major : u;
			for (float v = SimMath::subf32(0.0f, fp.minor);; v = SimMath::addf32(v, res))
			{
				const float vv = v > fp.minor ? fp.minor : v;
				if (fp.type == 0 || SimMath::sumSquares2(uu, vv) <= SimMath::mulf32(fp.major, fp.major))
				{
					const float x = SimMath::addf32(fp.x, SimMath::subf32(SimMath::mulf32(uu, c), SimMath::mulf32(vv, s)));
					const float y = SimMath::addf32(fp.y, SimMath::addf32(SimMath::mulf32(uu, s), SimMath::mulf32(vv, c)));
					if (!visit(x, y))
					{
						return;
					}
				}
				if (v >= fp.minor)
				{
					break;
				}
			}
			if (u >= fp.major)
			{
				break;
			}
		}
	}
}

template <class F>
void sampleFootprint(GameLogic &logic, const ThingTemplate &build, const Coord3D &pos, float angle, float res, F &&visit)
{
	std::vector<BuildPlacement::Footprint> fps;
	if (!BuildPlacement::footprintsOf(logic, build, pos, angle, fps))
	{
		fps.clear();
	}
	sampleFootprints(fps, pos, angle, res, visit);
}

// the footprints of geometry shapes at a place (footprintsOf's body)
void footprintsOfShapes(const std::vector<PathfindShape> &shapes, const Coord3D &pos, float angle, std::vector<BuildPlacement::Footprint> &out)
{
	out.clear();
	for (const PathfindShape &s : shapes)
	{
		if (!s.active)
		{
			continue;
		}
		Coord3D at = pos;
		PathfindGeometry::applyShapeOffset(s, angle, at);
		BuildPlacement::Footprint f;
		// Footprint numbers its kinds 0 box, 1 sphere, 2 cylinder; PathfindGeometryType is SPHERE 0, CYLINDER 1, BOX 2 (lane PATH-2 fix: the type was copied
		// unconverted, so a box collided as a disc and a sphere as a box)
		f.type = s.type == PATHFIND_GEOMETRY_BOX ? 0 : (s.type == PATHFIND_GEOMETRY_SPHERE ? 1 : 2);
		f.major = s.majorRadius;
		f.minor = s.type == PATHFIND_GEOMETRY_BOX ? s.minorRadius : s.majorRadius;
		f.x = at.x;
		f.y = at.y;
		f.angle = angle;
		out.push_back(f);
	}
}

// the corners of an oriented rectangle (half sizes a along the angle, b across), counter-clockwise
void corners(const BuildPlacement::Footprint &f, float cx[4], float cy[4])
{
	const float c = SimMath::cosDet(f.angle), s = SimMath::sinDet(f.angle);
	const float ax = SimMath::mulf32(c, f.major), ay = SimMath::mulf32(s, f.major);
	const float bx = SimMath::mulf32(SimMath::subf32(0.0f, s), f.minor), by = SimMath::mulf32(c, f.minor);
	cx[0] = SimMath::addf32(SimMath::addf32(f.x, ax), bx);
	cy[0] = SimMath::addf32(SimMath::addf32(f.y, ay), by);
	cx[1] = SimMath::subf32(SimMath::addf32(f.x, ax), bx);
	cy[1] = SimMath::subf32(SimMath::addf32(f.y, ay), by);
	cx[2] = SimMath::subf32(SimMath::subf32(f.x, ax), bx);
	cy[2] = SimMath::subf32(SimMath::subf32(f.y, ay), by);
	cx[3] = SimMath::addf32(SimMath::subf32(f.x, ax), bx);
	cy[3] = SimMath::addf32(SimMath::subf32(f.y, ay), by);
}

// the projection interval of a footprint's corner set on the axis (ux, uy)
void project(const float cx[4], const float cy[4], float ux, float uy, float &lo, float &hi)
{
	for (int i = 0; i < 4; ++i)
	{
		const float d = SimMath::addf32(SimMath::mulf32(cx[i], ux), SimMath::mulf32(cy[i], uy));
		if (i == 0 || d < lo)
		{
			lo = d;
		}
		if (i == 0 || d > hi)
		{
			hi = d;
		}
	}
}

// the point of the rectangle `r` nearest to (px, py), in the rectangle's own frame: the distance test of a disc against an oriented rectangle
bool discHitsRect(const BuildPlacement::Footprint &rect, float px, float py, float radius)
{
	const float c = SimMath::cosDet(rect.angle), s = SimMath::sinDet(rect.angle);
	const float dx = SimMath::subf32(px, rect.x), dy = SimMath::subf32(py, rect.y);
	const float lx = SimMath::addf32(SimMath::mulf32(dx, c), SimMath::mulf32(dy, s));
	const float ly = SimMath::subf32(SimMath::mulf32(dy, c), SimMath::mulf32(dx, s));
	const float qx = lx > rect.major ? rect.major : (lx < SimMath::subf32(0.0f, rect.major) ? SimMath::subf32(0.0f, rect.major) : lx);
	const float qy = ly > rect.minor ? rect.minor : (ly < SimMath::subf32(0.0f, rect.minor) ? SimMath::subf32(0.0f, rect.minor) : ly);
	const float ex = SimMath::subf32(lx, qx), ey = SimMath::subf32(ly, qy);
	return SimMath::sumSquares2(ex, ey) <= SimMath::mulf32(radius, radius);
}

bool isBox(const BuildPlacement::Footprint &f) { return f.type == 0; }
} // namespace

bool BuildPlacement::footprintsCollide(const Footprint &a, const Footprint &b)
{
	if (isBox(a) && isBox(b))
	{
		// separating axis test on the four edge normals
		float ax[4], ay[4], bx[4], by[4];
		corners(a, ax, ay);
		corners(b, bx, by);
		const float axes[4][2] = { { SimMath::cosDet(a.angle), SimMath::sinDet(a.angle) },
			{ SimMath::subf32(0.0f, SimMath::sinDet(a.angle)), SimMath::cosDet(a.angle) },
			{ SimMath::cosDet(b.angle), SimMath::sinDet(b.angle) },
			{ SimMath::subf32(0.0f, SimMath::sinDet(b.angle)), SimMath::cosDet(b.angle) } };
		for (const auto &axis : axes)
		{
			float alo = 0, ahi = 0, blo = 0, bhi = 0;
			project(ax, ay, axis[0], axis[1], alo, ahi);
			project(bx, by, axis[0], axis[1], blo, bhi);
			if (ahi < blo || bhi < alo)
			{
				return false;
			}
		}
		return true;
	}
	if (!isBox(a) && !isBox(b))
	{
		const float dx = SimMath::subf32(a.x, b.x), dy = SimMath::subf32(a.y, b.y);
		const float r = SimMath::addf32(a.major, b.major);
		return SimMath::sumSquares2(dx, dy) <= SimMath::mulf32(r, r);
	}
	if (isBox(a))
	{
		return discHitsRect(a, b.x, b.y, b.major);
	}
	return discHitsRect(b, a.x, a.y, a.major);
}

bool BuildPlacement::footprintsOf(GameLogic &logic, const ThingTemplate &tt, const Coord3D &pos, float angle, std::vector<Footprint> &out)
{
	out.clear();
	AIWorld *ai = logic.aiWorld();
	if (!ai)
	{
		return false;
	}
	const ObjectMovementInfo &info = ai->movementInfo(*tt.getFinalOverride());
	if (!info.hasGeometry)
	{
		return false;
	}
	std::vector<PathfindShape> shapes;
	for (size_t i = 0; i < info.geometry.shapeCount(); ++i)
	{
		shapes.push_back(info.geometry.shape(i));
	}
	footprintsOfShapes(shapes, pos, angle, out);
	return true;
}

bool BuildPlacement::isLocationClearOfObjects(GameLogic &logic, const Coord3D &pos, const ThingTemplate &build, float angle, Object *builder, unsigned options)
{
	std::vector<Footprint> mine, other;
	if (!footprintsOf(logic, build, pos, angle, mine))
	{
		return true; // no geometry known: nothing to collide with (stop S-301 counts it)
	}
	const bool onlyCheckEnemies = options == LLF_NO_ENEMY_OBJECT_OVERLAP;
	static const int immobile = ObjectTemplateInfoBuilder::kindOfIndex("IMMOBILE");
	static const int mine_ = ObjectTemplateInfoBuilder::kindOfIndex("MINE");
	static const int inert = ObjectTemplateInfoBuilder::kindOfIndex("INERT");
	static const int clearedByBuild = ObjectTemplateInfoBuilder::kindOfIndex("CLEARED_BY_BUILD");
	for (Object *them = logic.getFirstObject(); them; them = them->getNextObject())
	{
		if (them == builder || them->isDestroyed() || !them->getTemplate() || !them->isInWorld())
		{
			continue; // lane GARRISON-1: a contain's rider out of the world (RW 0x68C18F) is not in the partition
		}
		if ((clearedByBuild >= 0 && them->isKindOf((unsigned)clearedByBuild)) || (mine_ >= 0 && them->isKindOf((unsigned)mine_)) ||
			(inert >= 0 && them->isKindOf((unsigned)inert)))
		{
			continue;
		}
		if (!footprintsOf(logic, *them->getTemplate(), *them->getPosition(), them->getOrientation(), other))
		{
			continue; // an object without a geometry (props, markers) takes no ground
		}
		bool collide = false;
		for (size_t i = 0; i < mine.size() && !collide; ++i)
		{
			for (size_t j = 0; j < other.size() && !collide; ++j)
			{
				collide = footprintsCollide(mine[i], other[j]);
			}
		}
		if (!collide)
		{
			continue;
		}
		const Relationship rel = builder && builder->getControllingPlayer() && them->getTeam() ? builder->getControllingPlayer()->getRelationship(them->getTeam()) : NEUTRAL;
		if (immobile >= 0 && them->isKindOf((unsigned)immobile))
		{
			if (onlyCheckEnemies && builder && rel != ENEMIES)
			{
				continue;
			}
			return false;
		}
		if (builder && rel == ENEMIES)
		{
			return false;
		}
	}
	return true;
}

LegalBuildCode BuildPlacement::isLocationLegalToBuild(GameLogic &logic, const Coord3D &pos, const ThingTemplate &build, float angle, unsigned options, Object *builder, Player *player)
{
	(void)player;
	AIWorld *ai = logic.aiWorld();
	const PathfindTerrain *terrain = ai ? ai->pathfinder().terrainView() : nullptr;
	// ZH: "You just can't never build off the map, regardless of options"
	if (terrain)
	{
		float loX, loY, hiX, hiY;
		terrain->getMaximumPathfindExtent(loX, loY, hiX, hiY);
		if (pos.x < loX || pos.x > hiX || pos.y < loY || pos.y > hiY)
		{
			return LBC_RESTRICTED_TERRAIN;
		}
	}
	if (options & LLF_SHROUD_REVEALED)
	{
		// no shroud exists yet (VIS-1): every cell is revealed (stop S-301)
	}
	if ((options & LLF_NO_OBJECT_OVERLAP) && !isLocationClearOfObjects(logic, pos, build, angle, builder, LLF_NO_OBJECT_OVERLAP))
	{
		return LBC_OBJECTS_IN_THE_WAY;
	}
	if ((options & LLF_NO_ENEMY_OBJECT_OVERLAP) && !isLocationClearOfObjects(logic, pos, build, angle, builder, LLF_NO_ENEMY_OBJECT_OVERLAP))
	{
		return LBC_OBJECTS_IN_THE_WAY;
	}
	if ((options & LLF_CLEAR_PATH) && builder && builder->isKindOfName("DOZER"))
	{
		// RotWK gates the clear-path query on a DOZER builder (template byte +0x109 bit 0x40, RW 0x796A46..0x796A98; ZH's gate is a non-IMMOBILE builder, BuildAssistant.cpp:997): a
		// wall hub or a plot skips the test (lane BUILD-2); a builder without an AI cannot reach it
		if (!builder->getAIUpdateInterface())
		{
			return LBC_NO_CLEAR_PATH;
		}
		// the path query itself is not ported (stop S-301)
	}
	if (options & LLF_TERRAIN_RESTRICTIONS)
	{
		if (!terrain)
		{
			logic.reportError("BuildPlacement: TERRAIN_RESTRICTIONS asked without a pathfinder terrain (the cell classification lives there) [S-301]");
			return LBC_RESTRICTED_TERRAIN;
		}
		if (!logic.settings().buildRulesLoaded)
		{
			logic.reportError("BuildPlacement: GameData's AllowedHeightVariationForBuilding was not loaded (PLAN rule 10)");
			return LBC_RESTRICTED_TERRAIN;
		}
		// lane BUILD-5 r2: a PORT (KindOf 188, template + 0x11F & 0x10) samples water too: RotWK's checkSampleBuildLocation (RW 0x794A2B, BFME2 decomp
		// BuildAssistant.cpp:1843, tier A) counts a K188 build's samples as water (TerrainLogic::isUnderwater) or land, and only land samples enter the height
		// range; isLocationLegalToBuild (RW 0x796C0B .. 0x796C60, BFME2 decomp BuildAssistant.cpp:2101) then wants both kinds after the full resolution pass and
		// refuses a ratio max / min above 6 (RW 0xDA59A0). A non-port's underwater sample stays restricted (the port's cell classification by the pathfinder's
		// cell types is S-301's inference: cliffs restrict, water restricts unless PORT)
		const bool port = templateKindOf(logic, build, "PORT");
		const float step[2] = { SimMath::mulf32(3.0f, kCell), kCell };
		float lo = 0.0f, hi = 0.0f; // the height range spans both passes (RW: hiZ / loZ are set once; only the water / land counts restart)
		bool first = true;
		for (int pass = 0; pass < 2; ++pass)
		{
			float water = 0.0f, land = 0.0f;
			bool restricted = false;
			sampleFootprint(logic, build, pos, angle, step[pass], [&](float x, float y) {
				float waterZ = 0.0f, terrainZ = 0.0f;
				if (terrain->isCliffCell(x, y))
				{
					restricted = true;
					return false;
				}
				if (terrain->isUnderwater(x, y, &waterZ, &terrainZ))
				{
					if (!port)
					{
						restricted = true;
						return false;
					}
					water = SimMath::addf32(water, 1.0f);
					return true;
				}
				land = SimMath::addf32(land, 1.0f);
				const float z = terrain->getGroundHeight(x, y);
				if (first || z < lo)
				{
					lo = z;
				}
				if (first || z > hi)
				{
					hi = z;
				}
				first = false;
				return true;
			});
			if (restricted)
			{
				return LBC_RESTRICTED_TERRAIN;
			}
			if (SimMath::subf32(hi, lo) > logic.settings().allowedHeightVariationForBuilding)
			{
				return LBC_NOT_FLAT_ENOUGH;
			}
			if (pass == 1 && port)
			{
				if (land == 0.0f || water == 0.0f)
				{
					return LBC_RESTRICTED_TERRAIN;
				}
				const float big = water > land ? water : land, small = land < water ? land : water;
				if (SimMath::divf32(big, small) > kMaxWaterLandSampleRatio)
				{
					return LBC_RESTRICTED_TERRAIN;
				}
			}
		}
	}
	return LBC_OK;
}

bool BuildPlacement::getPortTransform(GameLogic &logic, const Coord3D &pos, const ThingTemplate &build, float angle, float &outAngle, Coord3D &outPos)
{
	// lane BUILD-5: RotWK BuildAssistant::getPortTransform (RW 0x794C63, tier A with BFME2 0x792802; the decomp's recovered, not yet byte-matching attempt
	// reverse/attempts/0x00392802.cpp): the footprint sampled at MAP_XY_FACTOR (RW 0x794CFB -> RW 0x793A6E with checkSampleBuildLocation RW 0x794A2B); no answer
	// when a sample is restricted or either kind is missing; else the land samples' centre walks towards the water samples' centre in steps of 5 (RW 0xBDAE58)
	// until it is under water, steps back 10 (the direction normalized, times RW 0xBD83D8) and takes the ground height there; the angle is atan2 of that
	// direction (RW 0x441BD2) plus pi / 2 (RW 0xDA599C)
	PortSnapView view;
	return portSnapView(logic, build, view) && getPortTransform(view, pos, angle, outAngle, outPos);
}

bool BuildPlacement::portSnapView(GameLogic &logic, const ThingTemplate &build, PortSnapView &out)
{
	out = PortSnapView();
	AIWorld *ai = logic.aiWorld();
	out.terrain = ai ? ai->pathfinder().terrainView() : nullptr;
	if (!out.terrain)
	{
		return false;
	}
	const ObjectMovementInfo &info = ai->movementInfo(*build.getFinalOverride());
	out.hasGeometry = info.hasGeometry;
	for (size_t i = 0; info.hasGeometry && i < info.geometry.shapeCount(); ++i)
	{
		out.shapes.push_back(info.geometry.shape(i));
	}
	return true;
}

bool BuildPlacement::getPortTransform(const PortSnapView &view, const Coord3D &pos, float angle, float &outAngle, Coord3D &outPos)
{
	const PathfindTerrain *terrain = view.terrain;
	if (!terrain)
	{
		return false;
	}
	std::vector<Footprint> fps;
	if (view.hasGeometry)
	{
		footprintsOfShapes(view.shapes, pos, angle, fps);
	}
	float water = 0.0f, land = 0.0f;
	float wx = 0.0f, wy = 0.0f, wz = 0.0f, lx = 0.0f, ly = 0.0f, lz = 0.0f;
	bool restricted = false;
	sampleFootprints(fps, pos, angle, kCell, [&](float x, float y) {
		if (terrain->isCliffCell(x, y))
		{
			restricted = true; // INFERENCE (S-301): the pathfinder's cell types stand for retail's GetCellType classification
		}
		float waterZ = 0.0f, terrainZ = 0.0f;
		const float z = terrain->getGroundHeight(x, y);
		if (terrain->isUnderwater(x, y, &waterZ, &terrainZ))
		{
			water = SimMath::addf32(water, 1.0f);
			wx = SimMath::addf32(wx, x);
			wy = SimMath::addf32(wy, y);
			wz = SimMath::addf32(wz, z);
		}
		else
		{
			land = SimMath::addf32(land, 1.0f);
			lx = SimMath::addf32(lx, x);
			ly = SimMath::addf32(ly, y);
			lz = SimMath::addf32(lz, z);
		}
		return true;
	});
	if (restricted || !(land > 0.0f) || !(water > 0.0f))
	{
		return false;
	}
	const float invL = SimMath::divf32(1.0f, land), invW = SimMath::divf32(1.0f, water);
	lx = SimMath::mulf32(lx, invL);
	ly = SimMath::mulf32(ly, invL);
	lz = SimMath::mulf32(lz, invL);
	wx = SimMath::mulf32(wx, invW);
	wy = SimMath::mulf32(wy, invW);
	wz = SimMath::mulf32(wz, invW);
	float dx = SimMath::subf32(wx, lx), dy = SimMath::subf32(wy, ly), dz = SimMath::subf32(wz, lz);
	const float dist = (float)SimMath::length3d(dx, dy, dz);
	auto normalize = [&](float scale) {
		const float len = (float)SimMath::length3d(dx, dy, dz);
		if (len != 0.0f)
		{
			const float inv = SimMath::divf32(1.0f, len);
			dx = SimMath::mulf32(dx, inv);
			dy = SimMath::mulf32(dy, inv);
			dz = SimMath::mulf32(dz, inv);
		}
		dx = SimMath::mulf32(dx, scale);
		dy = SimMath::mulf32(dy, scale);
		dz = SimMath::mulf32(dz, scale);
	};
	normalize(kPortWalkStep);
	outPos.x = lx;
	outPos.y = ly;
	outPos.z = lz;
	for (float t = 0.0f; t < dist; t = SimMath::addf32(t, kPortWalkStep))
	{
		float waterZ = 0.0f, terrainZ = 0.0f;
		if (terrain->isUnderwater(outPos.x, outPos.y, &waterZ, &terrainZ))
		{
			normalize(kCell);
			outPos.x = SimMath::subf32(outPos.x, dx);
			outPos.y = SimMath::subf32(outPos.y, dy);
			outPos.z = terrain->getGroundHeight(outPos.x, outPos.y);
			break;
		}
		outPos.x = SimMath::addf32(outPos.x, dx);
		outPos.y = SimMath::addf32(outPos.y, dy);
		outPos.z = SimMath::addf32(outPos.z, dz);
	}
	outAngle = SimMath::addf32((float)SimMath::atan2d(dy, dx), kPortAngleOffset);
	return true;
}

const char *BuildPlacement::codeName(LegalBuildCode code)
{
	switch (code)
	{
		case LBC_OK: return "OK";
		case LBC_RESTRICTED_TERRAIN: return "RESTRICTED_TERRAIN";
		case LBC_NOT_FLAT_ENOUGH: return "NOT_FLAT_ENOUGH";
		case LBC_OBJECTS_IN_THE_WAY: return "OBJECTS_IN_THE_WAY";
		case LBC_NO_CLEAR_PATH: return "NO_CLEAR_PATH";
		case LBC_SHROUD: return "SHROUD";
		case LBC_TOO_CLOSE_TO_SUPPLIES: return "TOO_CLOSE_TO_SUPPLIES";
	}
	return "?";
}

std::vector<std::string> BuildPlacement::stopLines()
{
	return {
		"[S-301] building placement: ZH BuildAssistant::isLocationLegalToBuild is followed (RW 0x797A96 itself was not read): the footprint is the template Geometry tested in 2D, objects without a "
		"geometry take no ground, the shroud (VIS-1), the supply source border, the clear-path query of the builder's AI, the factory exit width / extra bib checks and the cleared-by-build "
		"objects are not ported; terrain restrictions sample the footprint grid for cliff cells (the pathfinder's cell classification), water and the ground height spread",
	};
}
