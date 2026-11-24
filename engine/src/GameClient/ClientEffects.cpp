// OpenBFME. GPL-3.0. See ClientEffects.h.

#include "GameClient/ClientEffects.h"

#include <algorithm>
#include <cmath>

// ---- scorches (TARGET call RW 0x5E0045; DONOR BFME2 0x67EA3) -------------------------------------------------------------------------------------------

void ScorchBuffer::add(const Coord3D &pos, float radius, int type)
{
	// DONOR BFME2 0x67EA3: estimate = radius * 0.25; for an entry within that box on every axis, with the same radius and type, the call clears
	// that entry's flag (+0x18) and returns: the entry stays drawn and no new one is added (BFME2 decomp BaseHeightMapAddScorch.cpp; BFME1
	// BaseHeightMap.cpp addScorch's `duplicate:` label). The flag is the overlap mark of RVA 0x69C5F, which this buffer does not keep.
	const float estimate = radius * 0.25f;
	for (auto it = m_entries.begin(); it != m_entries.end(); ++it)
	{
		if (std::fabs(pos.x - it->position.x) < estimate && std::fabs(pos.y - it->position.y) < estimate && std::fabs(radius - it->radius) < estimate
			&& it->type == type)
		{
			++m_duplicates;
			return;
		}
	}
	if ((int)m_entries.size() >= kMaxScorches)
	{
		m_entries.erase(m_entries.begin());
		++m_evictions;
	}
	m_entries.push_back(ScorchFx{ pos, radius, type });
	++m_additions;
	++m_version;
}

void ScorchBuffer::clear()
{
	m_entries.clear();
	++m_version;
}

// ---- dynamic decals (TARGET call RW 0x5E05AB; DONOR BFME2 0x330A37 / 0x10C785) -------------------------------------------------------------------------

bool DecalFx::expired(std::uint32_t frame) const
{
	if (desc.lifetimeFrames < 0)
	{
		return false; // RW's (UnsignedInt)(-1 * 0.03) is not a finite lifetime
	}
	return frame >= startFrame + (std::uint32_t)desc.lifetimeFrames;
}

int DecalFx::opacityAt(std::uint32_t frame) const
{
	const int age = (int)(frame - startFrame);
	const int fadeInStart = desc.startDelayFrames;
	const int fadeInEnd = fadeInStart + desc.fadeOneFrames;
	const int fadeOutStart = fadeInEnd + desc.peakFrames;
	const int fadeOutEnd = fadeOutStart + desc.fadeTwoFrames;
	const int start = (int)desc.opacityStart;
	const int peak = (int)desc.opacityPeak;
	const int end = (int)desc.opacityEnd;
	double v;
	if (age < fadeInStart)
	{
		return 0;
	}
	if (age < fadeInEnd)
	{
		// DONOR BFME2 0x10C785: ceil(from + (age - start) / (end - start) * (peak - from)), the BFME fast_float2long_round of it
		const double from = (double)start;
		v = from + (double)(age - fadeInStart) / (double)(fadeInEnd - fadeInStart) * (double)(peak - start);
	}
	else if (age < fadeOutStart)
	{
		return peak;
	}
	else if (age < fadeOutEnd)
	{
		const double from = (double)peak;
		v = from + (double)(age - fadeOutStart) / (double)(fadeOutEnd - fadeOutStart) * (double)(end - peak);
	}
	else
	{
		return end;
	}
	v = std::ceil(v);
	const int out = (int)std::lround(v);
	return std::max(0, std::min(255, out));
}

// ---- dynamic light pulses (TARGET call RW 0x5DFAA3; DONOR BFME2 0x6DF81 / 0x6DE3D) ---------------------------------------------------------------------

float LightPulseFx::range() const
{
	const float r = m_scale * targetEnd;
	return r < farAttenStart ? farAttenStart : r;
}

RGBColor LightPulseFx::colorNow() const
{
	// the decay scale (On_Frame_Update) times the light's Intensity (lane LIGHTDRAW-1; DONOR BFME2 Init_From_Point_Or_Spot_Light: Diffuse *= Get_Intensity())
	const float k = m_scale * intensity;
	return RGBColor{ targetColor.red * k, targetColor.green * k, targetColor.blue * k };
}

bool LightPulseFx::step()
{
	if (!enabled)
	{
		return false;
	}
	if (currentIncrease > 0 && increaseFrames > 0)
	{
		--currentIncrease;
		m_scale = (float)(increaseFrames - currentIncrease) / (float)increaseFrames;
	}
	else if (decreaseFrames == 0)
	{
		m_scale = 1.0f;
	}
	else
	{
		if (currentDecrease > 0)
		{
			--currentDecrease;
		}
		if (currentDecrease == 0)
		{
			enabled = false;
			return false;
		}
		m_scale = (float)currentDecrease / (float)decreaseFrames;
	}
	return true;
}

bool LightPulsePool::add(const Coord3D &pos, const RGBColor &color, float radius, std::uint32_t increaseFrames, std::uint32_t decreaseFrames)
{
	++m_additions;
	if (radius + 1.0f < 21.0f) // DONOR BFME2 0x4423C: W3DDisplay::createLightPulse's floor, `21.0f > inner + width` returns
	{
		++m_refusedRange;
		return false;
	}
	LightPulseFx light;
	light.position = pos;
	light.color = color;
	light.farAttenStart = 1.0f;
	light.farAttenEnd = radius + 1.0f; // DONOR BFME2 0x4423C: FarAttenStart = the inner radius (1), FarAttenEnd = inner + the width
	light.targetEnd = light.farAttenEnd;
	light.targetColor = color;
	light.increaseFrames = increaseFrames;
	light.decreaseFrames = decreaseFrames;
	light.currentIncrease = increaseFrames;
	light.currentDecrease = decreaseFrames;
	light.enabled = true;
	// reuse a disabled pool slot first (DONOR BFME2 0x6F94A: the first light whose in-use byte is clear)
	for (LightPulseFx &l : m_lights)
	{
		if (!l.enabled)
		{
			l = light;
			return true;
		}
	}
	if (m_lights.size() >= kMaxLights)
	{
		++m_overflow;
		return false;
	}
	m_lights.push_back(light);
	return true;
}

void LightPulsePool::step()
{
	for (LightPulseFx &l : m_lights)
	{
		l.step();
	}
}

size_t LightPulsePool::enabledCount() const
{
	return (size_t)std::count_if(m_lights.begin(), m_lights.end(), [](const LightPulseFx &l) { return l.enabled; });
}

void LightPulsePool::clear()
{
	m_lights.clear();
	m_drawLights.clear();
	m_freeDrawLights.clear();
	++m_generation;
}

size_t LightPulsePool::acquireDrawLight()
{
	size_t handle;
	if (!m_freeDrawLights.empty())
	{
		handle = m_freeDrawLights.back();
		m_freeDrawLights.pop_back();
	}
	else
	{
		handle = m_drawLights.size();
		m_drawLights.emplace_back();
	}
	m_drawLights[handle] = LightPulseFx();
	return handle;
}

void LightPulsePool::releaseDrawLight(size_t handle)
{
	if (handle >= m_drawLights.size() || std::find(m_freeDrawLights.begin(), m_freeDrawLights.end(), handle) != m_freeDrawLights.end())
	{
		return;
	}
	m_drawLights[handle].enabled = false; // RW 0x4CF5B6 -> the destructor's setEnabled(false), then Release_Ref
	m_freeDrawLights.push_back(handle);
}

std::vector<const LightPulseFx *> LightPulsePool::select(const Coord3D &eye, size_t max, const std::function<bool(const Coord3D &, float)> &visible) const
{
	struct Candidate
	{
		float d2;
		size_t order;
		const LightPulseFx *light;
	};
	std::vector<Candidate> c;
	size_t order = 0;
	for (const std::vector<LightPulseFx> *list : { &m_lights, &m_drawLights })
	{
		for (const LightPulseFx &l : *list)
		{
			++order;
			if (!l.enabled || (visible && !visible(l.position, l.range())))
			{
				continue;
			}
			const float dx = l.position.x - eye.x, dy = l.position.y - eye.y, dz = l.position.z - eye.z;
			c.push_back(Candidate{ dx * dx + dy * dy + dz * dz, order, &l });
		}
	}
	std::sort(c.begin(), c.end(), [](const Candidate &a, const Candidate &b) { return a.d2 != b.d2 ? a.d2 < b.d2 : a.order < b.order; });
	std::vector<const LightPulseFx *> out;
	for (size_t i = 0; i < c.size() && i < max; ++i)
	{
		out.push_back(c[i].light);
	}
	return out;
}
