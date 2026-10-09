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

bool WaterGeometry::buildRiverStrip(const RiverArea &river, float uvLength, RiverStrip &out)
{
	out = RiverStrip();
	if (river.lines.size() < 2 || uvLength <= 0.0f)
	{
		return false;
	}
	float along = 0.0f;
	float px = 0, py = 0;
	for (size_t i = 0; i < river.lines.size(); ++i)
	{
		const RiverArea::Line &l = river.lines[i];
		const float cx = (l.x0 + l.x1) * 0.5f, cy = (l.y0 + l.y1) * 0.5f;
		if (i > 0)
		{
			along += std::sqrt((cx - px) * (cx - px) + (cy - py) * (cy - py));
		}
		px = cx;
		py = cy;
		const float z = (float)river.waterHeight;
		const float v = along / uvLength;
		out.position.insert(out.position.end(), { l.x0, l.y0, z, l.x1, l.y1, z });
		out.uv0.insert(out.uv0.end(), { 0.0f, v, 1.0f, v });
		out.uv1.insert(out.uv1.end(), { 0.0f, 0.5f, 1.0f, 0.5f });
		if (i > 0)
		{
			const std::uint32_t a = (std::uint32_t)(2 * (i - 1)), b = a + 1, c = a + 2, d = a + 3;
			out.index.insert(out.index.end(), { a, b, d, a, d, c });
		}
	}
	return true;
}
