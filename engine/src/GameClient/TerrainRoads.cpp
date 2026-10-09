// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
// See GameClient/TerrainRoads.h.

#include "GameClient/TerrainRoads.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>

namespace
{

std::string lower(std::string s)
{
	for (char &c : s)
	{
		c = (char)std::tolower((unsigned char)c);
	}
	return s;
}

std::string trim(const std::string &s)
{
	size_t a = 0, b = s.size();
	while (a < b && std::isspace((unsigned char)s[a])) ++a;
	while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
	return s.substr(a, b - a);
}

struct V2
{
	float x, y;
};

} // namespace

const RoadType *RoadTypeIndex::find(const std::string &name) const
{
	auto it = byName.find(lower(name));
	return it == byName.end() ? nullptr : &it->second;
}

void TerrainRoads::scanText(const std::string &text, RoadTypeIndex &out)
{
	size_t pos = 0;
	bool inBlock = false;
	RoadType cur;
	while (pos <= text.size())
	{
		size_t nl = text.find('\n', pos);
		if (nl == std::string::npos)
		{
			nl = text.size();
		}
		std::string line = text.substr(pos, nl - pos);
		pos = nl + 1;
		size_t semi = line.find(';');
		if (semi != std::string::npos)
		{
			line.erase(semi);
		}
		line = trim(line);
		if (line.empty())
		{
			continue;
		}
		if (!inBlock)
		{
			const bool road = line.size() > 5 && lower(line.substr(0, 4)) == "road" && std::isspace((unsigned char)line[4]);
			const bool bridge = line.size() > 7 && lower(line.substr(0, 6)) == "bridge" && std::isspace((unsigned char)line[6]);
			if (road || bridge)
			{
				cur = RoadType();
				cur.isBridge = bridge;
				cur.name = trim(line.substr(road ? 4 : 6));
				size_t sp = cur.name.find_first_of(" \t");
				if (sp != std::string::npos)
				{
					cur.name.erase(sp);
				}
				inBlock = true;
			}
			continue;
		}
		if (lower(line) == "end")
		{
			out.byName[lower(cur.name)] = cur;
			inBlock = false;
			continue;
		}
		size_t eq = line.find('=');
		if (eq == std::string::npos)
		{
			continue;
		}
		std::string key = lower(trim(line.substr(0, eq)));
		std::string val = trim(line.substr(eq + 1));
		if (key == "texture")
		{
			cur.texture = val;
		}
		else if (key == "roadwidth")
		{
			cur.roadWidth = (float)std::atof(val.c_str());
		}
		else if (key == "roadwidthintexture")
		{
			cur.roadWidthInTexture = (float)std::atof(val.c_str());
		}
	}
}

bool TerrainRoads::load(ArchiveFileSystem &fs, RoadTypeIndex &out, std::string *error)
{
	out = RoadTypeIndex();
	std::vector<std::uint8_t> bytes;
	if (!fs.readFile("data\\ini\\roads.ini", bytes, error))
	{
		return false;
	}
	scanText(std::string(bytes.begin(), bytes.end()), out);
	if (out.byName.empty())
	{
		if (error)
		{
			*error = "data\\ini\\roads.ini holds no Road blocks";
		}
		return false;
	}
	return true;
}

float TerrainRoads::getMaxCellHeight(const WorldHeightMap &map, float x, float y)
{
	int iX = (int)(x / MAP_XY_FACTOR) + map.m_borderSize;
	int iY = (int)(y / MAP_XY_FACTOR) + map.m_borderSize;
	if (iX < 0) iX = 0;
	if (iY < 0) iY = 0;
	if (iX >= map.m_width - 1) iX = map.m_width - 2;
	if (iY >= map.m_height - 1) iY = map.m_height - 2;
	int h0 = map.getHeight(iX, iY), h1 = map.getHeight(iX + 1, iY), h2 = map.getHeight(iX + 1, iY + 1), h3 = map.getHeight(iX, iY + 1);
	return (float)std::max(std::max(h0, h1), std::max(h2, h3)) * MAP_HEIGHT_SCALE;
}

// ZH W3DRoadBuffer::addMapObjects + preloadRoadSegment + loadFloat4PtSection (straight SEGMENT only).
void TerrainRoads::buildStrips(const std::vector<MapObject> &objects, const RoadTypeIndex &types, const WorldHeightMap &map,
	float liftWorldUnits, std::vector<RoadStrip> &out, RoadBuildReport &report)
{
	out.clear();
	report = RoadBuildReport();
	std::vector<std::pair<V2, V2>> seen; // for the identical-segment test (per road type name)
	std::vector<std::string> seenName;
	for (size_t i = 0; i < objects.size(); ++i)
	{
		const MapObject &a = objects[i];
		if (!a.getFlag(FLAG_ROAD_POINT1))
		{
			if (a.getFlag(FLAG_ROAD_POINT2))
			{
				++report.orphanPoints; // a POINT2 whose predecessor is not a POINT1 (checked below)
			}
			continue;
		}
		if (i + 1 >= objects.size() || !objects[i + 1].getFlag(FLAG_ROAD_POINT2))
		{
			++report.orphanPoints;
			continue;
		}
		const MapObject &b = objects[i + 1];
		++i; // a pair consumes both (ZH: pMapObj = pMapObj2)
		++report.pairs;

		V2 loc1 = { a.m_location.x, a.m_location.y };
		V2 loc2 = { b.m_location.x, b.m_location.y };
		if (loc1.x == loc2.x && loc1.y == loc2.y)
		{
			loc2.x += 0.25f;
			++report.nudgedZeroLength;
		}
		const RoadType *road = types.find(a.m_objectName);
		if (!road || road->roadWidth <= 0.0f)
		{
			++report.unknownRoadTypes;
			if (std::find(report.unknownNames.begin(), report.unknownNames.end(), a.m_objectName) == report.unknownNames.end())
			{
				report.unknownNames.push_back(a.m_objectName);
			}
			continue;
		}
		bool dup = false;
		for (size_t s = 0; s < seen.size(); ++s)
		{
			if ((seen[s].first.x == loc1.x && seen[s].first.y == loc1.y && seen[s].second.x == loc2.x && seen[s].second.y == loc2.y)
				|| (seen[s].first.x == loc2.x && seen[s].first.y == loc2.y && seen[s].second.x == loc1.x && seen[s].second.y == loc1.y))
			{
				dup = true;
				break;
			}
		}
		if (dup)
		{
			++report.duplicateSegments;
			continue;
		}
		seen.push_back({ loc1, loc2 });

		const float scale = road->roadWidth;
		const float widthInTexture = road->roadWidthInTexture > 0.0f ? road->roadWidthInTexture : 1.0f;
		V2 roadVector = { loc2.x - loc1.x, loc2.y - loc1.y };
		const float len = std::sqrt(roadVector.x * roadVector.x + roadVector.y * roadVector.y);
		V2 roadNormal = { -roadVector.y, roadVector.x };
		const float nl = std::sqrt(roadNormal.x * roadNormal.x + roadNormal.y * roadNormal.y);
		roadNormal.x /= nl;
		roadNormal.y /= nl;
		const float half = scale * widthInTexture / 2.0f;
		const V2 hn = { roadNormal.x * half, roadNormal.y * half };
		const V2 bottom1 = { loc1.x - hn.x, loc1.y - hn.y }, top1 = { loc1.x + hn.x, loc1.y + hn.y };
		const V2 bottom2 = { loc2.x - hn.x, loc2.y - hn.y }, top2 = { loc2.x + hn.x, loc2.y + hn.y };
		const V2 dir = { roadVector.x / len, roadVector.y / len };

		int uCount = (int)(len / MAP_XY_FACTOR) + 1;
		if (uCount < 2) uCount = 2;
		int vCount = (int)(2 * half / MAP_XY_FACTOR) + 1;
		if (vCount < 2) vCount = 2;
		if (vCount > 100) vCount = 100;

		RoadStrip strip;
		strip.roadName = a.m_objectName;
		strip.texture = road->texture;
		const float uOffset = 0.0f, vOffset = 85.0f / 512.0f;
		for (int c = 0; c < uCount; ++c)
		{
			const float f = (float)c / (float)(uCount - 1);
			const V2 pb = { bottom1.x + (bottom2.x - bottom1.x) * f, bottom1.y + (bottom2.y - bottom1.y) * f };
			const V2 pt = { top1.x + (top2.x - top1.x) * f, top1.y + (top2.y - top1.y) * f };
			float maxZ = -1e30f;
			for (int j = 0; j < vCount; ++j)
			{
				const float jf = (float)j / (float)(vCount - 1);
				maxZ = std::max(maxZ, getMaxCellHeight(map, pb.x + (pt.x - pb.x) * jf, pb.y + (pt.y - pb.y) * jf));
			}
			const V2 pts[2] = { pb, pt };
			for (int k = 0; k < 2; ++k)
			{
				const V2 cur = { pts[k].x - loc1.x, pts[k].y - loc1.y };
				const float U = dir.x * cur.x + dir.y * cur.y;
				const float V = roadNormal.x * cur.x + roadNormal.y * cur.y;
				strip.position.push_back(pts[k].x);
				strip.position.push_back(pts[k].y);
				strip.position.push_back(maxZ + liftWorldUnits);
				strip.uv.push_back(uOffset + U / (scale * 4));
				strip.uv.push_back(vOffset - V / (scale * 4));
			}
			if (c > 0)
			{
				const std::uint32_t p0 = (std::uint32_t)(2 * (c - 1)), p1 = p0 + 1, c0 = p0 + 2, c1 = c0 + 1;
				// ZH: (prev[j+1], prev[j], cur[k]) then (prev[j+1], cur[k], cur[k+1]) for the collapsed pair;
				// emitted here with Godot's clockwise front face under (x, z, -y): (p0,p1,c1),(p0,c1,c0)
				strip.index.insert(strip.index.end(), { p0, p1, c1, p0, c1, c0 });
			}
		}
		report.vertices += strip.position.size() / 3;
		report.triangles += strip.index.size() / 3;
		out.push_back(std::move(strip));
	}
	report.strips = out.size();
}
