// OpenBFME. GPL-3.0.
// See MapPathfindObjects.h.

#include "GameClient/MapPathfindObjects.h"
#include "GameLogic/Object/ObjectGeometry.h"

#include "Common/AsciiString.h"

#include <cstdlib>

namespace
{
const RawTokens *rawField(const ThingTemplate &t, const char *name)
{
	const FieldValue *v = t.findField(name);
	return v ? std::get_if<RawTokens>(v) : nullptr;
}

bool floatField(const ThingTemplate &t, const char *name, float &out, std::string &err)
{
	const FieldValue *v = t.findField(name);
	if (!v || std::holds_alternative<std::monostate>(*v))
	{
		return false;
	}
	if (const float *f = std::get_if<float>(v))
	{
		out = *f;
		return true;
	}
	if (const long long *n = std::get_if<long long>(v))
	{
		out = (float)*n;
		return true;
	}
	if (const RawTokens *raw = std::get_if<RawTokens>(v))
	{
		if (!raw->tokens.empty())
		{
			char *end = nullptr;
			const float f = std::strtof(raw->tokens[0].c_str(), &end);
			if (end != raw->tokens[0].c_str())
			{
				out = f;
				return true;
			}
		}
	}
	err = std::string(name) + " has an unreadable value";
	return false;
}

bool boolField(const ThingTemplate &t, const char *name, bool &out)
{
	const FieldValue *v = t.findField(name);
	if (!v)
	{
		return false;
	}
	if (const bool *b = std::get_if<bool>(v))
	{
		out = *b;
		return true;
	}
	if (const RawTokens *raw = std::get_if<RawTokens>(v))
	{
		if (!raw->tokens.empty())
		{
			out = AsciiStringUtil::compareNoCase(raw->tokens[0], "Yes") == 0 || raw->tokens[0] == "1" || AsciiStringUtil::compareNoCase(raw->tokens[0], "True") == 0;
			return true;
		}
	}
	return false;
}
} // namespace

std::vector<PathfindObject *> MapPathfindObjectSet::pointers() const
{
	std::vector<PathfindObject *> p;
	p.reserve(objects.size());
	for (const auto &o : objects)
	{
		p.push_back(o.get());
	}
	return p;
}

void MapPathfindObjects::kindsOf(const MapTemplateInfo &info, bool (&kinds)[PK_COUNT])
{
	for (bool &k : kinds) k = false;
	struct Map
	{
		PathfindKind kind;
		const char *name;
	};
	static const Map table[] = {
		{ PK_MINE, "MINE" },
		{ PK_PROJECTILE, "PROJECTILE" },
		{ PK_BRIDGE_TOWER, "BRIDGE_TOWER" },
		{ PK_DEFENSIVE_WALL, "DEFENSIVE_WALL" },
		{ PK_BLAST_CRATER, "BLAST_CRATER" },
		{ PK_STRUCTURE, "STRUCTURE" },
		{ PK_CAN_SEE_THROUGH_STRUCTURE, "CAN_SEE_THROUGH_STRUCTURE" },
		{ PK_INFANTRY, "INFANTRY" },
		{ PK_DOZER, "DOZER" },
		{ PK_MACHINE, "MACHINE" },
		{ PK_HORDE, "HORDE" },
		{ PK_LARGE_RECTANGLE_PATHFIND, "LARGE_RECTANGLE_PATHFIND" },
		{ PK_WALK_ON_TOP_OF_WALL, "WALK_ON_TOP_OF_WALL" },
		{ PK_IMMOBILE, "IMMOBILE" },
		{ PK_AIRCRAFT, "AIRCRAFT" },
		{ PK_HARVESTER, "HARVESTER" },
		{ PK_MONSTER, "MONSTER" },
		{ PK_CAVALRY, "CAVALRY" },
		{ PK_HERO, "HERO" },
		{ PK_SHIP, "SHIP" },
		{ PK_PATH_THROUGH_INFANTRY, "PATH_THROUGH_INFANTRY" },
		{ PK_HEAVY_MELEE_HITTER, "HEAVY_MELEE_HITTER" },
		{ PK_DO_NOT_CLASSIFY, "DO_NOT_CLASSIFY" },
		{ PK_SIEGE_LADDER, "SIEGE_LADDER" },
		{ PK_BLOCKING_GATE, "BLOCKING_GATE" },
		{ PK_SCALEABLE_WALL, "SCALEABLE_WALL" },
		{ PK_BASE_SITE, "BASE_SITE" },
		{ PK_WALL_UPGRADE, "WALL_UPGRADE" },
	};
	for (const Map &m : table)
	{
		const int bit = MapObjectCreation::kindOfIndex(m.name);
		kinds[(int)m.kind] = bit >= 0 && info.kindOf_test(bit);
	}
}

void MapPathfindObjects::build(const MapObjectDrawables &drawables, MapPathfindObjectSet &out)
{
	PathfindObjectID nextId = 1;
	for (const MapObjectDrawable &d : drawables.drawables)
	{
		if ((d.fate != MAPOBJ_OBJECT && d.fate != MAPOBJ_OBJECT_BRIDGE) || !d.info || !d.info->tmpl)
		{
			continue;
		}
		auto o = std::make_unique<MapPathfindObject>();
		MapPathfindObjects::kindsOf(*d.info, o->kinds);
		const ThingTemplate &t = *d.info->tmpl;
		std::string err;
		float fence = 0.0f, fenceX = 0.0f, diameter = -1.0f;
		floatField(t, "FenceWidth", fence, err);
		floatField(t, "FenceXOffset", fenceX, err);
		floatField(t, "PathfindDiameter", diameter, err);
		const bool isFence = fence > 0.0f;
		if (!o->kinds[PK_STRUCTURE] && !isFence)
		{
			continue; // only structures and fences have a footprint
		}
		// geometry
		float major = 0.0f, minor = 0.0f, height = 0.0f;
		std::string gerr;
		floatField(t, "GeometryMajorRadius", major, gerr);
		floatField(t, "GeometryMinorRadius", minor, gerr);
		floatField(t, "GeometryHeight", height, gerr);
		bool isSmall = false;
		boolField(t, "GeometryIsSmall", isSmall);
		PathfindGeometryType type = PATHFIND_GEOMETRY_BOX;
		if (const RawTokens *g = rawField(t, "Geometry"))
		{
			if (!g->tokens.empty())
			{
				const std::string &tok = g->tokens[0];
				if (AsciiStringUtil::compareNoCase(tok, "BOX") == 0) type = PATHFIND_GEOMETRY_BOX;
				else if (AsciiStringUtil::compareNoCase(tok, "CYLINDER") == 0) type = PATHFIND_GEOMETRY_CYLINDER;
				else if (AsciiStringUtil::compareNoCase(tok, "SPHERE") == 0) type = PATHFIND_GEOMETRY_SPHERE;
				else gerr = "Geometry '" + tok + "' is not BOX, CYLINDER or SPHERE";
			}
		}
		else if (!isFence)
		{
			gerr = "no Geometry";
		}
		if (!gerr.empty())
		{
			out.errors.push_back("template " + t.getName() + ": " + gerr);
		}
		o->id = nextId++;
		o->templateName = t.getName();
		o->position = d.position;
		o->orientation = d.angle;
		o->geometry.type = type;
		o->geometry.majorRadius = major;
		o->geometry.minorRadius = minor;
		o->geometry.height = height;
		o->geometry.isSmall = isSmall;
		// lane PATH-2 (S-341): every shape of the Geometry rows (GameLogic/Object/ObjectGeometry.h), shape 0 for the single-shape fields, as AIWorld::movementInfo
		if (rawField(t, "Geometry"))
		{
			try
			{
				ObjectGeometry::fillPathfindGeometry(t, o->geometry);
			}
			catch (const std::exception &e)
			{
				out.errors.push_back("template " + t.getName() + ": " + e.what());
			}
		}
		o->fenceWidth = fence;
		o->fenceXOffset = fenceX;
		o->pathfindDiameter = diameter;
		o->mobile = false;
		if (isFence) ++out.fences; else ++out.structures;
		out.objects.push_back(std::move(o));
	}
}
