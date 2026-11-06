// OpenBFME. GPL-3.0.
// See GameLogic/SkirmishAI/AIPositionSampler.h for the RW facts.

#include "GameLogic/SkirmishAI/AIPositionSampler.h"

#include "Common/StateHash.h"
#include "Common/Xfer.h"
#include "GameLogic/SimMath.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace
{
// RW 0x67EDFA / RW 0x8E4082: the squared 3D distance, SSE float32 in z, y, x order
float squared(const Coord3D &a, const Coord3D &b)
{
	const float dx = SimMath::subf32(a.x, b.x), dy = SimMath::subf32(a.y, b.y), dz = SimMath::subf32(a.z, b.z);
	return SimMath::addf32(SimMath::addf32(SimMath::mulf32(dz, dz), SimMath::mulf32(dy, dy)), SimMath::mulf32(dx, dx));
}
} // namespace

// RW 0x8E4082
void AIPositionSampler::append(Group &g, ObjectID id, const Position &position)
{
	const Coord3D p = position(id);
	if (g.members.empty())
	{
		g.radius = m_padding; // RW 0x8E40A4: the empty group's radius = the padding
		g.center = p;
	}
	else
	{
		// RW 0x67EDFA: outside the sphere (radius - padding)^2
		const float inner = SimMath::subf32(g.radius, m_padding);
		if (!(squared(p, g.center) <= SimMath::mulf32(inner, inner)))
		{
			// RW 0x403111 / 0x403175: the direction to the point, normalized (x87 length, SSE scale)
			Coord3D dir{ SimMath::subf32(g.center.x, p.x), SimMath::subf32(g.center.y, p.y), SimMath::subf32(g.center.z, p.z) };
			const float distance = (float)SimMath::length3d(dir.x, dir.y, dir.z);
			if (distance != 0.0f)
			{
				const float scale = SimMath::divf32(1.0f, distance);
				dir = { SimMath::mulf32(dir.x, scale), SimMath::mulf32(dir.y, scale), SimMath::mulf32(dir.z, scale) };
			}
			// RW 0x8E418E: (old radius + distance + padding) / 2; the edge is the old sphere's point towards the new one
			const float radius = SimMath::mulf32(SimMath::addf32(SimMath::addf32(g.radius, distance), m_padding), 0.5f);
			const Coord3D edge{ SimMath::addf32(g.center.x, SimMath::mulf32(dir.x, g.radius)),
				SimMath::addf32(g.center.y, SimMath::mulf32(dir.y, g.radius)), SimMath::addf32(g.center.z, SimMath::mulf32(dir.z, g.radius)) };
			g.center = { SimMath::addf32(edge.x, SimMath::mulf32(-dir.x, radius)), SimMath::addf32(edge.y, SimMath::mulf32(-dir.y, radius)),
				SimMath::addf32(edge.z, SimMath::mulf32(-dir.z, radius)) };
			g.radius = radius;
		}
	}
	g.members.push_back(id); // RW 0x8E41D9
}

// RW 0x8E41EB
void AIPositionSampler::insert(ObjectID id, const Position &position)
{
	// RW 0x8E42E7: every group starts unselected
	for (Group &g : m_groups)
	{
		g.selected = false;
	}
	size_t first = m_groups.size();
	const float bound = SimMath::mulf32(SimMath::mulf32(m_padding, m_padding), 2.0f); // RW 0x8E421D -> 0x8E3DD6
	for (size_t i = 0; i < m_groups.size(); ++i)
	{
		for (ObjectID candidate : m_groups[i].members)
		{
			if (bound > squared(position(id), position(candidate)))
			{
				m_groups[i].selected = true; // RW 0x8E4237
				if (first == m_groups.size())
				{
					first = i;
					append(m_groups[i], id, position); // RW 0x8E423C
				}
				break;
			}
		}
	}
	if (first == m_groups.size())
	{
		// RW 0x8E4303: a new 0x28-byte group with the padding (its selected byte stays 0)
		m_groups.emplace_back();
		append(m_groups.back(), id, position);
		return;
	}
	// RW 0x8E4376: the later selected groups append their members to the first, in member order, and are destroyed
	for (size_t i = first + 1; i < m_groups.size(); ++i)
	{
		if (!m_groups[i].selected)
		{
			continue;
		}
		for (ObjectID member : m_groups[i].members)
		{
			append(m_groups[first], member, position);
		}
	}
	for (size_t i = m_groups.size(); i-- > first + 1;)
	{
		if (m_groups[i].selected)
		{
			m_groups.erase(m_groups.begin() + (long)i);
		}
	}
}

// RW 0x8E4456
void AIPositionSampler::add(ObjectID id, float padding, const Position &position)
{
	m_padding = padding;
	m_members.push_back(id); // RW 0x5F1B20
	insert(id, position);    // RW 0x8E42C8
}

// RW 0x8E4475: the caller already erased the id from the registration list
void AIPositionSampler::remove(ObjectID id, const Position &position)
{
	auto it = std::find(m_members.begin(), m_members.end(), id);
	if (it == m_members.end())
	{
		return; // RW 0x8E46A5 only calls when its own erase found the object
	}
	m_members.erase(it);
	m_groups.clear();
	for (ObjectID member : m_members)
	{
		insert(member, position);
	}
}

void AIPositionSampler::crc(StateHasher &h) const
{
	h.addFloat(m_padding);
	h.addU32((std::uint32_t)m_members.size());
	for (ObjectID id : m_members)
	{
		h.addU32(id);
	}
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
		{
			h.addU32(id);
		}
	}
}

void AIPositionSampler::xfer(Xfer &x)
{
	x.xferReal(&m_padding);
	if (x.isLoading() && !std::isfinite(m_padding))
	{
		throw XferException("skirmish ai: the position sampler's padding is not finite");
	}
	x.xferVectorSize(m_members, 4);
	for (ObjectID &id : m_members)
	{
		x.xferObjectID(&id);
	}
	x.xferVectorSize(m_groups, 21);
	for (Group &g : m_groups)
	{
		x.xferReal(&g.radius);
		x.xferReal(&g.center.x);
		x.xferReal(&g.center.y);
		x.xferReal(&g.center.z);
		x.xferBool(&g.selected);
		x.xferVectorSize(g.members, 4);
		for (ObjectID &id : g.members)
		{
			x.xferObjectID(&id);
		}
		if (x.isLoading() && (!std::isfinite(g.radius) || !std::isfinite(g.center.x) || !std::isfinite(g.center.y) || !std::isfinite(g.center.z)))
		{
			throw XferException("skirmish ai: a position sampler group is not finite");
		}
	}
	if (x.isLoading())
	{
		// every registration is in exactly one group (the insert / remove invariant)
		std::vector<ObjectID> members = m_members, grouped;
		for (const Group &g : m_groups)
		{
			grouped.insert(grouped.end(), g.members.begin(), g.members.end());
		}
		std::sort(members.begin(), members.end());
		std::sort(grouped.begin(), grouped.end());
		if (members != grouped)
		{
			throw XferException("skirmish ai: the position sampler's groups differ from its registrations");
		}
	}
}
