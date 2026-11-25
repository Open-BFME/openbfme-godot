// OpenBFME. GPL-3.0. See ClientEffects.h.

#include "GameClient/ClientEffects.h"

#include "Libraries/WWVegas/WW3D2/lightenvironment.h"

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
	return RGBColor{ targetColor.red * m_scale, targetColor.green * m_scale, targetColor.blue * m_scale };
}

void LightPulseToEnvSource(const LightPulseFx &l, LightEnvSourceLight &out)
{
	out = LightEnvSourceLight();
	out.type = LightEnvSourceLight::POINT; // the pool's W3DDynamicLight is built POINT (LightClass ctor with 0, RW 0x46D59B)
	out.position = Vector3(l.position.x, l.position.y, l.position.z);
	const RGBColor a = l.ambientNow();
	const RGBColor d = l.colorNow();
	const RGBColor f = l.filterNow();
	// the port keeps ambient and diffuse already multiplied by the intensity (W3DLightDraw::lightColor / lightAmbient); the raw diffuse is the
	// filter's input (RW 0x540261 reads +0xE0, not times +0xD0)
	out.ambient = Vector3(a.red, a.green, a.blue);
	out.diffuse = Vector3(d.red, d.green, d.blue);
	out.intensity = 1.0f;
	out.filterDiffuse = Vector3(f.red, f.green, f.blue);
	out.useFilterDiffuse = true;
	out.farAttenuation = l.farAttenuation;
	out.farAttenStart = l.farAttenStart;
	out.farAttenEnd = l.range();
	out.enabled = l.enabled;
}

RGBColor LightPulseFx::ambientNow() const
{
	return RGBColor{ targetAmbient.red * m_scale, targetAmbient.green * m_scale, targetAmbient.blue * m_scale };
}

RGBColor LightPulseFx::filterNow() const
{
	return RGBColor{ targetFilter.red * m_scale, targetFilter.green * m_scale, targetFilter.blue * m_scale };
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
	light.targetAmbient = color; // RW 0x443BAB: createLightPulse writes the colour to the ambient (+0xD4) and the diffuse (+0xE0)
	light.targetFilter = color;
	light.increaseFrames = increaseFrames;
	light.decreaseFrames = decreaseFrames;
	light.currentIncrease = increaseFrames;
	light.currentDecrease = decreaseFrames;
	light.enabled = true;
	light.isPersistent = false;
	// reuse a disabled pool slot first (DONOR BFME2 0x6F94A: the first light whose in-use byte is clear)
	// Persistent light slots are never reused by transient pulses even when disabled (prevents slot aliasing)
	for (LightPulseFx &l : m_lights)
	{
		if (!l.enabled && !l.isPersistent)
		{
			const bool farAttenuation = l.farAttenuation; // lane LOCAL-38: the reused light keeps its +0xC8 flags (createLightPulse sets none, RW 0x443B5D)
			l = light;
			l.farAttenuation = farAttenuation;
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

int LightPulsePool::addPersistent(const Coord3D &pos, const RGBColor &color, float radius, const RGBColor *ambient, const RGBColor *rawDiffuse)
{
	++m_additions;
	LightPulseFx light;
	light.position = pos;
	light.color = color;
	light.farAttenStart = 1.0f;
	light.farAttenEnd = radius;
	light.targetEnd = radius;
	light.targetColor = color;
	light.targetAmbient = ambient ? *ambient : RGBColor{ 0.0f, 0.0f, 0.0f };
	light.targetFilter = rawDiffuse ? *rawDiffuse : color;
	light.farAttenuation = true; // RW 0x4CF227: W3DLightDraw's ctor ORs FAR_ATTENUATION into the light's flags
	light.increaseFrames = 0;
	light.decreaseFrames = 0;
	light.currentIncrease = 0;
	light.currentDecrease = 0;
	light.enabled = true;
	light.isPersistent = true;
	// reuse a disabled pool slot first (DONOR BFME2 0x6F94A: the first light whose in-use byte is clear)
	for (size_t i = 0; i < m_lights.size(); ++i)
	{
		if (!m_lights[i].enabled && !m_lights[i].isPersistent)
		{
			m_lights[i] = light;
			return (int)i;
		}
	}
	if (m_lights.size() >= kMaxLights)
	{
		++m_overflow;
		return -1;
	}
	m_lights.push_back(light);
	return (int)(m_lights.size() - 1);
}

void LightPulsePool::updatePersistent(int handle, const Coord3D &pos, const RGBColor &color, float radius, bool enabled, const RGBColor *ambient,
	const RGBColor *rawDiffuse)
{
	if (handle >= 0 && (size_t)handle < m_lights.size())
	{
		LightPulseFx &l = m_lights[(size_t)handle];
		if (l.isPersistent)
		{
			l.position = pos;
			l.color = color;
			l.targetColor = color;
			l.targetAmbient = ambient ? *ambient : RGBColor{ 0.0f, 0.0f, 0.0f };
			l.targetFilter = rawDiffuse ? *rawDiffuse : color;
			l.farAttenEnd = radius;
			l.targetEnd = radius;
			l.enabled = enabled;
		}
	}
}

void LightPulsePool::removePersistent(int handle)
{
	if (handle >= 0 && (size_t)handle < m_lights.size())
	{
		m_lights[(size_t)handle].enabled = false;
		m_lights[(size_t)handle].isPersistent = false;
	}
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
}
