// OpenBFME. GPL-3.0.
// See TacticalView.h.

#include "GameClient/TacticalView.h"

#include "GameLogic/GameLogic.h"

#include <cmath>

namespace
{
Coord3D sub(const Coord3D &a, const Coord3D &b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
float dot(const Coord3D &a, const Coord3D &b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Coord3D cross(const Coord3D &a, const Coord3D &b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
Coord3D norm(const Coord3D &a)
{
	const float l = std::sqrt(dot(a, a));
	return l > 0.0f ? Coord3D{ a.x / l, a.y / l, a.z / l } : Coord3D{ 0, 1, 0 };
}
} // namespace

bool TacticalView::screenToTerrain(const ICoord2D &pixel, const GameLogic &logic, Coord3D &out, float maxDistance) const
{
	Coord3D o, d;
	if (!screenToRay(pixel, o, d))
	{
		return false;
	}
	// march along the ray until it passes below the ground, then bisect (ZH W3DView::screenToTerrain intersects the height field the same way)
	const float step = 8.0f;
	auto below = [&](float t) {
		const float x = o.x + d.x * t, y = o.y + d.y * t, z = o.z + d.z * t;
		return z <= logic.getGroundHeight(x, y);
	};
	float lo = 0.0f;
	if (below(0.0f))
	{
		out = o;
		out.z = logic.getGroundHeight(o.x, o.y);
		return true;
	}
	float hi = step;
	while (!below(hi))
	{
		lo = hi;
		hi += step;
		if (hi > maxDistance)
		{
			return false;
		}
	}
	for (int i = 0; i < 24; ++i)
	{
		const float mid = 0.5f * (lo + hi);
		(below(mid) ? hi : lo) = mid;
	}
	const float t = 0.5f * (lo + hi);
	out.x = o.x + d.x * t;
	out.y = o.y + d.y * t;
	out.z = logic.getGroundHeight(out.x, out.y);
	return true;
}

PinholeView::PinholeView(const Coord3D &eye, const Coord3D &target, int width, int height, float verticalFovRadians)
	: m_width(width)
	, m_height(height)
	, m_fov(verticalFovRadians)
{
	set(eye, target);
}

void PinholeView::set(const Coord3D &eye, const Coord3D &target)
{
	m_eye = eye;
	m_target = target;
	rebuild();
}

void PinholeView::rebuild()
{
	m_forward = norm(sub(m_target, m_eye));
	Coord3D worldUp{ 0, 0, 1 };
	Coord3D r = cross(m_forward, worldUp);
	if (dot(r, r) < 1e-12f)
	{
		r = { 1, 0, 0 }; // looking straight down or up
	}
	m_right = norm(r);
	m_up = cross(m_right, m_forward);
}

void PinholeView::lookAt(const Coord3D &world)
{
	const Coord3D off = sub(m_eye, m_target);
	m_target = world;
	m_eye = { world.x + off.x, world.y + off.y, world.z + off.z };
	rebuild();
}

bool PinholeView::worldToScreen(const Coord3D &world, ICoord2D &screen) const
{
	const Coord3D v = sub(world, m_eye);
	const float depth = dot(v, m_forward);
	if (depth <= 1e-3f)
	{
		return false;
	}
	const float th = std::tan(m_fov * 0.5f);
	const float aspect = (float)m_width / (float)m_height;
	const float nx = dot(v, m_right) / depth / (th * aspect);
	const float ny = dot(v, m_up) / depth / th;
	screen.x = (int)std::lround((nx * 0.5f + 0.5f) * (float)m_width);
	screen.y = (int)std::lround((0.5f - ny * 0.5f) * (float)m_height);
	return true;
}

bool PinholeView::screenToRay(const ICoord2D &pixel, Coord3D &origin, Coord3D &direction) const
{
	const float th = std::tan(m_fov * 0.5f);
	const float aspect = (float)m_width / (float)m_height;
	const float nx = ((float)pixel.x / (float)m_width - 0.5f) * 2.0f;
	const float ny = (0.5f - (float)pixel.y / (float)m_height) * 2.0f;
	Coord3D d{ m_forward.x + m_right.x * nx * th * aspect + m_up.x * ny * th, m_forward.y + m_right.y * nx * th * aspect + m_up.y * ny * th,
		m_forward.z + m_right.z * nx * th * aspect + m_up.z * ny * th };
	origin = m_eye;
	direction = norm(d);
	return true;
}
