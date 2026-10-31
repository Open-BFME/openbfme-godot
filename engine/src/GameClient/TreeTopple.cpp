// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// TreeTopple. See GameClient/TreeTopple.h for the sources. Lane SPELL-3. Client only (not simulation).

#include "GameClient/TreeTopple.h"

#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DTreeDraw.h"

#include <algorithm>
#include <cmath>

namespace
{
const float kVelocityBounceLimit = 0.01f;      // RW 0xBE5600 (ZH VELOCITY_BOUNCE_LIMIT)
const float kVelocityBounceSoundLimit = 0.03f; // RW 0xBDC540 (ZH VELOCITY_BOUNCE_SOUND_LIMIT)
const float kBounceFXHeight = 27.0f;           // RW 0xBE4CC4 (lane TREES1: ZH's 3 x TREE_RADIUS_APPROX of the tree's bounds)

// the sink's updates (RW 0x4EA0D3 .. 0x4EA0F3): SinkTime (logic frames) times the client frames per logic frame (the tree buffer's + 0x5BC, set at RW
// 0x4E5F94 from RW 0xDE4324 + 0x38 = 30 / 5 = 6), here kSinkFrameSeconds / kUpdateSeconds
unsigned sinkUpdates(const W3DTreeDrawModuleData &d)
{
	const float seconds = (float)d.m_sinkFrames * TreeTopple::kSinkFrameSeconds;
	return (unsigned)(seconds / TreeTopple::kUpdateSeconds + 0.5f);
}

void startSink(TreeTopple::Tree &t)
{
	t.sinking = true;
	t.sinkFramesLeft = t.data ? sinkUpdates(*t.data) : 0u;
}
} // namespace

size_t TreeTopple::add(std::uint32_t clientId, const Coord3D &position, const W3DTreeDrawModuleData *data)
{
	Tree t;
	t.clientId = clientId;
	t.position = position;
	t.data = data;
	m_trees.push_back(t);
	return m_trees.size() - 1;
}

// RW 0x4E9E4E (TheTerrainVisual slot 0x70)
bool TreeTopple::topple(std::uint32_t clientId, const Coord3D &direction, float speed)
{
	bool any = false;
	for (size_t i = 0; i < m_trees.size(); ++i)
	{
		Tree &t = m_trees[i];
		if (t.clientId != clientId || t.state != UPRIGHT || !t.data)
		{
			continue;
		}
		const W3DTreeDrawModuleData &d = *t.data;
		// the logic's topple speed (TerrainLogic + 0x1918) when above 0, else MinimumToppleSpeed (at least 0)
		const float v = speed > 0.0f ? speed : std::max(0.0f, d.m_minimumToppleSpeed);
		const float len = std::sqrt(direction.x * direction.x + direction.y * direction.y);
		t.direction = len > 0.0f ? Coord3D{ direction.x / len, direction.y / len, 0.0f } : Coord3D{ 1.0f, 0.0f, 0.0f };
		t.angle = t.previousAngle = 0.0f;
		t.angularVelocity = v * d.m_initialVelocityPercent;
		t.angularAcceleration = v * d.m_initialAccelPercent;
		t.state = FALLING;
		if (!d.m_toppleFX.empty())
		{
			m_fx.push_back(FX{ d.m_toppleFX, t.position });
		}
		m_changed.push_back(i);
		any = true;
	}
	return any;
}

// RW 0x4EA068 (slot 0x7C): the lumber is gone, the tree sinks where it is (standing, falling or down)
void TreeTopple::remove(std::uint32_t clientId)
{
	for (size_t i = 0; i < m_trees.size(); ++i)
	{
		Tree &t = m_trees[i];
		if (t.clientId != clientId || t.removed || t.state == GONE)
		{
			continue;
		}
		t.removed = true;
		startSink(t);
		m_changed.push_back(i);
	}
}

// RW 0x4E9C81 (slot 0x58): the tree goes at once
void TreeTopple::removeNow(std::uint32_t clientId)
{
	for (size_t i = 0; i < m_trees.size(); ++i)
	{
		Tree &t = m_trees[i];
		if (t.clientId != clientId || t.state == GONE)
		{
			continue;
		}
		t.removed = true;
		t.sinking = false;
		t.state = GONE;
		t.previousAngle = t.angle;
		t.previousSink = t.sink;
		m_changed.push_back(i);
	}
}

// RW 0x4ED508's per-tree step: the sink, then the fall (RW 0x4EA107)
void TreeTopple::update(Tree &t)
{
	t.previousAngle = t.angle;
	t.previousSink = t.sink;
	if (t.sinking)
	{
		if (t.sinkFramesLeft == 0)
		{
			t.state = GONE; // RW 0x4ED5AD: the type -2
			t.sinking = false;
			return;
		}
		--t.sinkFramesLeft;
		if (t.data && t.data->m_sinkFrames > 0)
		{
			t.sink += t.data->m_sinkDistance / (float)t.data->m_sinkFrames; // z -= SinkDistance / SinkTime (the logic frames, not the updates)
		}
	}
	if (t.state == FALLING && t.data)
	{
		const W3DTreeDrawModuleData &d = *t.data;
		float step = t.angularVelocity;
		if (t.angle + step > kAngularLimit)
		{
			step = kAngularLimit - t.angle;
		}
		t.angle += step;
		if (t.angle >= kAngularLimit && t.angularVelocity > 0.0f)
		{
			t.angularVelocity *= -d.m_bounceVelocityPercent;
			if (std::fabs(t.angularVelocity) < kVelocityBounceLimit)
			{
				t.angularVelocity = 0.0f;
				t.state = DOWN;
				if (d.m_killWhenToppled)
				{
					startSink(t);
				}
			}
			else if (std::fabs(t.angularVelocity) >= kVelocityBounceSoundLimit && !d.m_bounceFX.empty())
			{
				// the fallen trunk's matrix applied to (0, 0, 27)
				const Coord3D top{ t.position.x + t.direction.x * kBounceFXHeight * std::sin(t.angle), t.position.y + t.direction.y * kBounceFXHeight * std::sin(t.angle),
					t.position.z - t.sink + kBounceFXHeight * std::cos(t.angle) };
				m_fx.push_back(FX{ d.m_bounceFX, top });
			}
		}
		else
		{
			t.angularVelocity += t.angularAcceleration;
		}
	}
}

void TreeTopple::advance(double seconds)
{
	m_accumulator += seconds;
	while (m_accumulator >= kUpdateSeconds)
	{
		m_accumulator -= kUpdateSeconds;
		for (size_t i = 0; i < m_trees.size(); ++i)
		{
			Tree &t = m_trees[i];
			if (t.state == FALLING || t.sinking)
			{
				update(t);
				m_changed.push_back(i);
			}
			else if (t.previousAngle != t.angle || t.previousSink != t.sink)
			{
				t.previousAngle = t.angle; // the last interpolated draw ends on the resting pose
				t.previousSink = t.sink;
				m_changed.push_back(i);
			}
		}
	}
	// the interpolated pose moves every frame while an update is under way
	for (size_t i = 0; i < m_trees.size(); ++i)
	{
		const Tree &t = m_trees[i];
		if (t.previousAngle != t.angle || t.previousSink != t.sink)
		{
			m_changed.push_back(i);
		}
	}
}

std::vector<size_t> TreeTopple::takeChanged()
{
	std::sort(m_changed.begin(), m_changed.end());
	m_changed.erase(std::unique(m_changed.begin(), m_changed.end()), m_changed.end());
	std::vector<size_t> out;
	out.swap(m_changed);
	return out;
}

void TreeTopple::pose(size_t index, float r[9], Coord3D &offset) const
{
	const Tree &t = m_trees[index];
	const float a = (float)(m_accumulator / kUpdateSeconds);
	const float angle = t.previousAngle + (t.angle - t.previousAngle) * a;
	const float sink = t.previousSink + (t.sink - t.previousSink) * a;
	// rotation by `angle` about k = (-dir.y, dir.x, 0) (Rodrigues): +Z tips towards the direction
	const float kx = -t.direction.y, ky = t.direction.x;
	const float c = std::cos(angle), s = std::sin(angle), v = 1.0f - c;
	r[0] = c + kx * kx * v;
	r[1] = kx * ky * v;
	r[2] = ky * s;
	r[3] = kx * ky * v;
	r[4] = c + ky * ky * v;
	r[5] = -kx * s;
	r[6] = -ky * s;
	r[7] = kx * s;
	r[8] = c;
	offset = Coord3D{ 0.0f, 0.0f, -sink };
}

void TreeTopple::placement(size_t index, const float base[9], const Coord3D &basePosition, float out[9], Coord3D &outPosition) const
{
	float r[9];
	Coord3D offset;
	pose(index, r, offset);
	for (int a = 0; a < 3; ++a)
	{
		for (int c = 0; c < 3; ++c)
		{
			out[a * 3 + c] = r[a * 3 + 0] * base[0 * 3 + c] + r[a * 3 + 1] * base[1 * 3 + c] + r[a * 3 + 2] * base[2 * 3 + c];
		}
	}
	outPosition = Coord3D{ basePosition.x + offset.x, basePosition.y + offset.y, basePosition.z + offset.z };
}

bool TreeTopple::within2D(const Coord3D &p, double x, double y, double radius)
{
	const double dx = p.x - x, dy = p.y - y;
	return dx * dx + dy * dy <= radius * radius;
}

std::vector<TreeTopple::FX> TreeTopple::takeFX()
{
	std::vector<FX> out;
	out.swap(m_fx);
	return out;
}

size_t TreeTopple::animating() const
{
	size_t n = 0;
	for (const Tree &t : m_trees)
	{
		n += (t.state == FALLING || t.sinking) ? 1 : 0;
	}
	return n;
}
