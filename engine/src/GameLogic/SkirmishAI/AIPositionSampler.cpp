// OpenBFME. GPL-3.0. See AIPositionSampler.h for source and target evidence.
#include "GameLogic/SkirmishAI/AIPositionSampler.h"
#include "Common/StateHash.h"
#include "GameLogic/SimMath.h"
#include <algorithm>

namespace
{
float squared(const Coord3D &a, const Coord3D &b)
{
	const float dx = SimMath::subf32(a.x, b.x), dy = SimMath::subf32(a.y, b.y), dz = SimMath::subf32(a.z, b.z);
	return SimMath::addf32(SimMath::addf32(SimMath::mulf32(dz, dz), SimMath::mulf32(dy, dy)), SimMath::mulf32(dx, dx));
}
} // namespace
void AIPositionSampler::append(Group &g, ObjectID id, const Position &position)
{
	const Coord3D p = position(id);
	if (g.members.empty())
	{
		g.radius = m_padding;
		g.center = p;
	}
	else
	{
		const float inner = SimMath::subf32(g.radius, m_padding);
		if (!(squared(p, g.center) <= SimMath::mulf32(inner, inner)))
		{
			Coord3D dir{SimMath::subf32(g.center.x, p.x), SimMath::subf32(g.center.y, p.y), SimMath::subf32(g.center.z, p.z)};
			const float distance = (float)SimMath::length3d(dir.x, dir.y, dir.z);
			if (distance != 0.0f)
			{
				const float scale = SimMath::divf32(1.0f, distance);
				dir = {SimMath::mulf32(dir.x, scale), SimMath::mulf32(dir.y, scale), SimMath::mulf32(dir.z, scale)};
			}
			const float radius = SimMath::mulf32(SimMath::addf32(SimMath::addf32(m_padding, g.radius), distance), 0.5f);
			// Keep the source's two vector scales and two vector adds, rather than combining them.
			const Coord3D edge{SimMath::addf32(g.center.x, SimMath::mulf32(dir.x, g.radius)),
							   SimMath::addf32(g.center.y, SimMath::mulf32(dir.y, g.radius)),
							   SimMath::addf32(g.center.z, SimMath::mulf32(dir.z, g.radius))};
			g.center = {SimMath::addf32(edge.x, SimMath::mulf32(-dir.x, radius)), SimMath::addf32(edge.y, SimMath::mulf32(-dir.y, radius)),
						SimMath::addf32(edge.z, SimMath::mulf32(-dir.z, radius))};
			g.radius = radius;
		}
	}
	g.members.push_back(id);
}
void AIPositionSampler::insert(ObjectID id, const Position &position)
{
	// Rva00282B26: membership tests use the individual points, not the group's bounding sphere.
	std::vector<bool> active(m_groups.size(), false);
	size_t first = m_groups.size();
	const float bound = SimMath::mulf32(SimMath::mulf32(m_padding, m_padding), 2.0f);
	for (size_t i = 0; i < m_groups.size(); ++i)
	{
		for (ObjectID candidate : m_groups[i].members)
		{
			if (bound > squared(position(id), position(candidate)))
			{
				active[i] = true;
				if (first == m_groups.size())
				{
					first = i;
					append(m_groups[i], id, position);
				}
				break;
			}
		}
	}
	if (first == m_groups.size())
	{
		m_groups.emplace_back();
		append(m_groups.back(), id, position);
		return;
	}
	// Merge later matching groups into the first in their existing member order.
	for (size_t i = first + 1; i < m_groups.size(); ++i)
		if (active[i])
			for (ObjectID member : m_groups[i].members)
				append(m_groups[first], member, position);
	for (size_t i = m_groups.size(); i-- > first + 1;)
		if (active[i])
			m_groups.erase(m_groups.begin() + i);
}
void AIPositionSampler::add(ObjectID id, float padding, const Position &position)
{
	m_padding = padding;
	m_members.push_back(id);
	insert(id, position);
}
void AIPositionSampler::remove(ObjectID id, const Position &position)
{
	auto it = std::find(m_members.begin(), m_members.end(), id);
	if (it == m_members.end())
		return;
	m_members.erase(it);
	// RW 0x8E4475: removal rebuilds all groups from the surviving registration list.
	m_groups.clear();
	for (ObjectID member : m_members)
		insert(member, position);
}
void AIPositionSampler::crc(StateHasher &h) const
{
	h.addFloat(m_padding);
	h.addU32((std::uint32_t)m_members.size());
	for (ObjectID id : m_members)
		h.addU32(id);
	h.addU32((std::uint32_t)m_groups.size());
	for (const Group &g : m_groups)
	{
		h.addFloat(g.radius);
		h.addFloat(g.center.x);
		h.addFloat(g.center.y);
		h.addFloat(g.center.z);
		h.addBool(g.selected);
		h.addU32((std::uint32_t)g.members.size());
		for (ObjectID id : g.members)
			h.addU32(id);
	}
}
