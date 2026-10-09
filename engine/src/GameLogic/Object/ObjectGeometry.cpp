// OpenBFME. GPL-3.0.
// See GameLogic/Object/ObjectGeometry.h.

#include "GameLogic/Object/ObjectGeometry.h"

#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SimMath.h"
#include "Common/INI/HostRealText.h"

#include <cctype>
#include <cstdlib>
#include <stdexcept>

namespace
{
bool sameNoCase(const std::string &a, const char *b)
{
	size_t i = 0;
	for (; i < a.size() && b[i]; ++i)
	{
		if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i]))
		{
			return false;
		}
	}
	return i == a.size() && b[i] == 0;
}

int typeOf(const ThingTemplate &tt, const std::vector<std::string> &tokens, const char *row)
{
	if (tokens.empty())
	{
		throw std::logic_error(tt.getName() + ": " + row + " has no geometry type");
	}
	if (sameNoCase(tokens[0], "SPHERE")) return ObjectGeometry::SHAPE_SPHERE;
	if (sameNoCase(tokens[0], "CYLINDER")) return ObjectGeometry::SHAPE_CYLINDER;
	if (sameNoCase(tokens[0], "BOX")) return ObjectGeometry::SHAPE_BOX;
	throw std::logic_error(tt.getName() + ": " + row + " '" + tokens[0] + "' is not SPHERE, CYLINDER or BOX");
}

float realOf(const ThingTemplate &tt, const std::vector<std::string> &tokens, const char *row)
{
	if (tokens.empty())
	{
		throw std::logic_error(tt.getName() + ": " + row + " has no value");
	}
	char *end = nullptr;
	const float v = strtofPortable(tokens[0].c_str(), &end);
	if (end == tokens[0].c_str())
	{
		throw std::logic_error(tt.getName() + ": " + row + " '" + tokens[0] + "' is not a number");
	}
	return v;
}

// GeometryOffset (RW 0xAD1770): a Coord3D "X:<x> Y:<y> Z:<z>". DONOR ZH INI::parseCoord3D: getNextSubToken("X") / ("Y") / ("Z") read the tokens with the
// colon separators (getSepsColon: whitespace, '=' and ':'), so a label and its value may be one token ("Y:98") or two ("Y: 98", retail gondorbuildings.ini
// GondorCastleElevator); the label must match (case-insensitive) or the row fails (lane PATH-2 fix: the first port wanted "<axis>:<value>" as one token).
void coordOf(const ThingTemplate &tt, const std::vector<std::string> &tokens, float out[3])
{
	std::vector<std::string> sub;
	for (const std::string &t : tokens)
	{
		size_t start = 0;
		for (size_t i = 0; i <= t.size(); ++i)
		{
			if (i == t.size() || t[i] == ':')
			{
				if (i > start)
				{
					sub.push_back(t.substr(start, i - start));
				}
				start = i + 1;
			}
		}
	}
	const char labels[3] = { 'X', 'Y', 'Z' };
	for (int k = 0; k < 3; ++k)
	{
		const size_t at = (size_t)k * 2;
		if (sub.size() < at + 2 || sub[at].size() != 1 || std::toupper((unsigned char)sub[at][0]) != labels[k])
		{
			throw std::logic_error(tt.getName() + ": GeometryOffset has no " + std::string(1, labels[k]) + ": component");
		}
		char *end = nullptr;
		out[k] = strtofPortable(sub[at + 1].c_str(), &end);
		if (end == sub[at + 1].c_str())
		{
			throw std::logic_error(tt.getName() + ": GeometryOffset " + std::string(1, labels[k]) + " '" + sub[at + 1] + "' is not a number");
		}
	}
}
} // namespace

std::vector<ObjectGeometry::Shape> ObjectGeometry::shapesOf(const ThingTemplate &tt)
{
	std::vector<Shape> shapes;
	for (const ThingTemplate::GeometryEvent &e : tt.geometryEvents())
	{
		if (e.row == "Geometry")
		{
			const int type = typeOf(tt, e.tokens, "Geometry");
			if (shapes.empty())
			{
				shapes.push_back(Shape{});
			}
			shapes.resize(1);
			shapes[0].type = type;
		}
		else if (e.row == "AdditionalGeometry")
		{
			Shape s;
			s.type = typeOf(tt, e.tokens, "AdditionalGeometry");
			shapes.push_back(s);
		}
		else if (shapes.empty())
		{
			continue; // retail's rows do nothing without a shape (the list is empty)
		}
		else if (e.row == "GeometryHeight")
		{
			shapes.back().height = realOf(tt, e.tokens, "GeometryHeight");
		}
		else if (e.row == "GeometryMajorRadius")
		{
			shapes.back().majorRadius = realOf(tt, e.tokens, "GeometryMajorRadius");
		}
		else if (e.row == "GeometryMinorRadius")
		{
			shapes.back().minorRadius = realOf(tt, e.tokens, "GeometryMinorRadius");
		}
		else if (e.row == "GeometryOffset")
		{
			float c[3];
			coordOf(tt, e.tokens, c);
			shapes.back().offsetX = c[0];
			shapes.back().offsetY = c[1];
			shapes.back().offsetZ = c[2];
		}
		else if (e.row == "GeometryActive")
		{
			if (e.tokens.empty())
			{
				throw std::logic_error(tt.getName() + ": GeometryActive has no value");
			}
			shapes.back().active = sameNoCase(e.tokens[0], "Yes") || sameNoCase(e.tokens[0], "True") || sameNoCase(e.tokens[0], "1");
		}
	}
	return shapes;
}

float ObjectGeometry::maxHeightAbovePosition(const std::vector<Shape> &shapes)
{
	float best = 0.0f; // RW 0xC1B594
	for (const Shape &s : shapes)
	{
		if (!s.active)
		{
			continue;
		}
		const float extent = s.type == SHAPE_SPHERE ? s.majorRadius : s.height;
		const float v = SimMath::addf32(extent, s.offsetZ);
		if (v > best) // fcomp: the larger one stays
		{
			best = v;
		}
	}
	return best;
}

std::vector<ObjectGeometry::ContactPoint> ObjectGeometry::contactPointsOf(const ThingTemplate &tt)
{
	std::vector<ContactPoint> points;
	for (const ThingTemplate::GeometryEvent &e : tt.geometryEvents())
	{
		if (e.row != "GeometryContactPoint")
		{
			continue;
		}
		// RW 0xAD3D30: getNextSubToken("X") / ("Y") / ("Z") with the colon separators, then getNextTokenOrNull for the label
		float c[3];
		coordOf(tt, e.tokens, c);
		ContactPoint p;
		p.x = c[0];
		p.y = c[1];
		p.z = c[2];
		std::vector<std::string> sub;
		for (const std::string &t : e.tokens)
		{
			size_t start = 0;
			for (size_t i = 0; i <= t.size(); ++i)
			{
				if (i == t.size() || t[i] == ':')
				{
					if (i > start)
					{
						sub.push_back(t.substr(start, i - start));
					}
					start = i + 1;
				}
			}
		}
		if (sub.size() > 6)
		{
			p.label = sub[6];
		}
		points.push_back(p);
	}
	return points;
}

bool ObjectGeometry::getBestContactPoint(const std::vector<Shape> &shapes, const std::vector<ContactPoint> &points, const Coord3D &callerPos, const std::string &label,
                                         bool preferred, Coord3D &out)
{
	if (!preferred)
	{
		throw std::logic_error("GeometryInfo::getBestContactPoint (RW 0xAD30E0): the overlap test of a call without `preferred` is not ported [S-1281]");
	}
	const float height = maxHeightAbovePosition(shapes);
	float best = 3.4028235e38f; // RW 0x7F7FFFFF
	size_t winner = points.size();
	for (size_t i = 0; i < points.size(); ++i)
	{
		const ContactPoint &p = points[i];
		if (p.label != label)
		{
			continue;
		}
		float z = p.z;
		if (height < z) // RW 0xAD33F2: fcomp, the height replaces z when it is below it
		{
			z = height;
		}
		// RW 0xAD3437: x87 PC24, the differences, then (dy^2 + dx^2) + dz^2 kept in the register, compared with the stored best (fcom: strictly below)
		const double dx = SimMath::pc24SubW((double)p.x, (double)callerPos.x);
		const double dy = SimMath::pc24SubW((double)p.y, (double)callerPos.y);
		const double dz = SimMath::pc24SubW((double)z, (double)callerPos.z);
		const double d = SimMath::pc24AddW(SimMath::pc24AddW(SimMath::pc24MulW(dy, dy), SimMath::pc24MulW(dx, dx)), SimMath::pc24MulW(dz, dz));
		if (d < (double)best)
		{
			best = SimMath::fstpDword(d);
			winner = i;
		}
	}
	if (winner == points.size())
	{
		return false;
	}
	out.x = points[winner].x;
	out.y = points[winner].y;
	out.z = points[winner].z;
	if (height < out.z) // RW 0xAD34B6
	{
		out.z = height;
		return true;
	}
	const float floorZ = SimMath::pc24Mul(height, 0.1f); // RW 0xAD34DD: fmul RW 0xBD83D4
	if (floorZ > out.z)
	{
		out.z = floorZ;
	}
	return true;
}

bool ObjectGeometry::worldspaceBestContactPoint(const Object &obj, const Coord3D &callerPos, const std::string &label, bool preferred, Coord3D &out)
{
	using SimMath::addf32;
	using SimMath::mulf32;
	using SimMath::subf32;
	const ThingTemplate *tt = static_cast<const ThingTemplate *>(obj.getTemplate())->getFinalOverride();
	const float *m = obj.getBasis(); // row major: m[r * 3 + c]
	const Coord3D &t = *obj.getPosition();
	// RW 0xB26C00: the orthogonal inverse (the transposed rotation, the translation -(R^T t) in this order)
	const float itx = subf32(0.0f, addf32(addf32(mulf32(m[6], t.z), mulf32(m[0], t.x)), mulf32(t.y, m[3])));
	const float ity = subf32(0.0f, addf32(addf32(mulf32(m[4], t.y), mulf32(m[1], t.x)), mulf32(t.z, m[7])));
	const float itz = subf32(0.0f, addf32(addf32(mulf32(m[5], t.y), mulf32(m[8], t.z)), mulf32(m[2], t.x)));
	// RW 0x690C74 .. 0x690D03: the caller's position in the object's space
	Coord3D local;
	local.x = addf32(addf32(addf32(mulf32(m[6], callerPos.z), mulf32(m[3], callerPos.y)), mulf32(m[0], callerPos.x)), itx);
	local.y = addf32(addf32(addf32(mulf32(m[7], callerPos.z), mulf32(m[4], callerPos.y)), mulf32(m[1], callerPos.x)), ity);
	local.z = addf32(addf32(addf32(mulf32(m[8], callerPos.z), mulf32(m[5], callerPos.y)), mulf32(m[2], callerPos.x)), itz);
	const std::vector<Shape> shapes = shapesOf(*tt);
	Coord3D p;
	if (!getBestContactPoint(shapes, contactPointsOf(*tt), local, label, preferred, p))
	{
		return false;
	}
	// RW 0x690D4E: a body in BODY_RUBBLE (vslot 0x24 == 3) puts the point at the geometry's height
	if (const BodyModuleInterface *body = obj.getBodyModule())
	{
		if (body->getDamageState() == BODY_RUBBLE)
		{
			p.z = maxHeightAbovePosition(shapes);
		}
	}
	// RW 0x690D6E .. 0x690DEC: back to the world
	out.x = addf32(addf32(addf32(mulf32(m[1], p.y), mulf32(m[2], p.z)), mulf32(m[0], p.x)), t.x);
	out.y = addf32(addf32(addf32(mulf32(m[3], p.x), mulf32(m[4], p.y)), mulf32(m[5], p.z)), t.y);
	out.z = addf32(addf32(addf32(mulf32(m[6], p.x), mulf32(m[7], p.y)), mulf32(m[8], p.z)), t.z);
	return true;
}

void ObjectGeometry::fillPathfindGeometry(const ThingTemplate &tt, PathfindGeometry &out)
{
	const std::vector<Shape> shapes = shapesOf(tt);
	if (shapes.empty())
	{
		return;
	}
	out.shapes.clear();
	for (const Shape &g : shapes)
	{
		PathfindShape ps;
		ps.type = g.type == SHAPE_BOX ? PATHFIND_GEOMETRY_BOX : (g.type == SHAPE_CYLINDER ? PATHFIND_GEOMETRY_CYLINDER : PATHFIND_GEOMETRY_SPHERE);
		ps.height = g.height;
		ps.majorRadius = g.majorRadius;
		ps.minorRadius = g.minorRadius;
		ps.offsetX = g.offsetX;
		ps.offsetY = g.offsetY;
		ps.offsetZ = g.offsetZ;
		ps.active = g.active;
		out.shapes.push_back(ps);
	}
	const PathfindShape &first = out.shapes.front();
	out.type = first.type;
	out.majorRadius = first.majorRadius;
	out.minorRadius = first.minorRadius;
	out.height = first.height;
}
