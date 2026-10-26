// OpenBFME. GPL-3.0.
// See GameClient/WaterGeometry.h.

#include "GameClient/WaterGeometry.h"

#include <algorithm>
#include <cmath>

namespace
{

float cross(const Point2F &o, const Point2F &a, const Point2F &b)
{
	return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
}

bool pointInTriangle(const Point2F &p, const Point2F &a, const Point2F &b, const Point2F &c)
{
	float d1 = cross(a, b, p), d2 = cross(b, c, p), d3 = cross(c, a, p);
	bool neg = d1 < 0 || d2 < 0 || d3 < 0;
	bool pos = d1 > 0 || d2 > 0 || d3 > 0;
	return !(neg && pos);
}

} // namespace

float WaterGeometry::signedArea(const std::vector<Point2F> &p)
{
	double a = 0;
	for (size_t i = 0, j = p.size() - 1; i < p.size(); j = i++)
	{
		a += (double)p[j].x * p[i].y - (double)p[i].x * p[j].y;
	}
	return (float)(a * 0.5);
}

bool WaterGeometry::triangulate(const std::vector<Point2F> &polygon, std::vector<std::uint32_t> &indices)
{
	indices.clear();
	const size_t n = polygon.size();
	if (n < 3)
	{
		return false;
	}
	float area = signedArea(polygon);
	if (std::fabs(area) < 1e-6f)
	{
		return false;
	}
	std::vector<std::uint32_t> v(n);
	for (size_t i = 0; i < n; ++i)
	{
		v[i] = (std::uint32_t)(area > 0 ? i : n - 1 - i); // counter-clockwise order
	}
	size_t guard = 0;
	while (v.size() > 3 && guard++ < n * n + 8)
	{
		bool clipped = false;
		for (size_t i = 0; i < v.size(); ++i)
		{
			const Point2F &a = polygon[v[(i + v.size() - 1) % v.size()]];
			const Point2F &b = polygon[v[i]];
			const Point2F &c = polygon[v[(i + 1) % v.size()]];
			if (cross(a, b, c) <= 0)
			{
				continue; // reflex or collinear
			}
			bool ear = true;
			for (size_t k = 0; k < v.size() && ear; ++k)
			{
				const std::uint32_t id = v[k];
				if (id == v[(i + v.size() - 1) % v.size()] || id == v[i] || id == v[(i + 1) % v.size()])
				{
					continue;
				}
				const Point2F &p = polygon[id];
				if ((p.x == a.x && p.y == a.y) || (p.x == b.x && p.y == b.y) || (p.x == c.x && p.y == c.y))
				{
					continue; // duplicated vertex positions do not block
				}
				if (pointInTriangle(p, a, b, c))
				{
					ear = false;
				}
			}
			if (!ear)
			{
				continue;
			}
			indices.push_back(v[(i + v.size() - 1) % v.size()]);
			indices.push_back(v[i]);
			indices.push_back(v[(i + 1) % v.size()]);
			v.erase(v.begin() + (long)i);
			clipped = true;
			break;
		}
		if (!clipped)
		{
			// no ear found (self-intersecting or collinear leftovers): drop collinear vertices once, else give up
			bool removed = false;
			for (size_t i = 0; i < v.size(); ++i)
			{
				const Point2F &a = polygon[v[(i + v.size() - 1) % v.size()]];
				const Point2F &b = polygon[v[i]];
				const Point2F &c = polygon[v[(i + 1) % v.size()]];
				if (std::fabs(cross(a, b, c)) < 1e-6f)
				{
					v.erase(v.begin() + (long)i);
					removed = true;
					break;
				}
			}
			if (!removed)
			{
				indices.clear();
				return false;
			}
		}
	}
	if (v.size() == 3)
	{
		indices.push_back(v[0]);
		indices.push_back(v[1]);
		indices.push_back(v[2]);
	}
	return !indices.empty();
}

bool WaterGeometry::buildRiverStrip(const RiverArea &river, RiverStrip &out)
{
	out = RiverStrip();
	const size_t n = river.lines.size();
	if (n < 2)
	{
		return false;
	}
	const std::vector<RiverArea::Line> &l = river.lines;
	auto dist = [](float x0, float y0, float x1, float y1) { return std::sqrt((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0)); };
	// the outline: first[0], second[0..n-1], first[n-1..1] (the closing edge first[1] -> first[0] left out)
	const float end = dist(l[0].x0, l[0].y0, l[0].x1, l[0].y1);
	float total = end + dist(l[n - 1].x1, l[n - 1].y1, l[n - 1].x0, l[n - 1].y0);
	for (size_t i = 1; i < n; ++i)
	{
		total += dist(l[i - 1].x1, l[i - 1].y1, l[i].x1, l[i].y1);
		if (i > 1)
		{
			total += dist(l[i - 1].x0, l[i - 1].y0, l[i].x0, l[i].y0);
		}
	}
	const float vScale = end > 0.0f ? ((total / 2.0f - end) / end) / (float)(n - 1) : 0.0f;
	const float z = (float)river.waterHeight;
	for (size_t i = 0; i < n; ++i)
	{
		const float v = vScale * (float)i;
		out.position.insert(out.position.end(), { l[i].x1, l[i].y1, z, l[i].x0, l[i].y0, z });
		out.uv0.insert(out.uv0.end(), { 0.5f, v, 0.0f, v });
		out.uv1.insert(out.uv1.end(), { 1.0f, v, 0.0f, v });
		if (i > 0)
		{
			const std::uint32_t a = (std::uint32_t)(2 * (i - 1));
			out.index.insert(out.index.end(), { a, a + 1, a + 3, a, a + 3, a + 2 });
		}
	}
	return true;
}

void WaterGeometry::riverVertexColour(const RiverArea &river, const TimeOfDayLights *tod, const float waterDiffuse[4], float out[4])
{
	if (river.r == 255 && river.g == 255 && river.b == 255)
	{
		for (int c = 0; c < 3; ++c)
		{
			float v = tod ? tod->terrain[0].ambient[c] : 0.0f;
			for (int i = 0; tod && i < 3; ++i)
			{
				const float down = -tod->terrain[i].lightPos[2];
				if (down > 0.0f)
				{
					v += down * tod->terrain[i].diffuse[c];
				}
			}
			out[c] = v * waterDiffuse[c];
		}
	}
	else if (river.r == 0 && river.g == 0 && river.b == 0)
	{
		out[0] = out[1] = out[2] = 1.0f;
	}
	else
	{
		out[0] = river.r / 255.0f;
		out[1] = river.g / 255.0f;
		out[2] = river.b / 255.0f;
	}
	// the game packs these into colour bytes unclamped; past 255 a channel would spill into the next, which no retail
	// lighting reaches
	for (int c = 0; c < 3; ++c)
	{
		out[c] = std::min(std::max(out[c], 0.0f), 1.0f);
	}
	out[3] = waterDiffuse[3];
}
