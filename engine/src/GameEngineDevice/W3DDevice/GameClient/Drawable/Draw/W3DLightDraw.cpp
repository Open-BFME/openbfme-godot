// OpenBFME. GPL-3.0. See W3DLightDraw.h (RW 0x4CF515 / 0xBE3638 / 0x4CF121 / 0x4CF2B5 / 0x4CF29D, donor BFME2 0x000CF91D / 0x000CFC0C).

#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DLightDraw.h"

#include "Common/GameCommon.h"
#include "GameClient/DrawableManager.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DDrawModules.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

namespace
{
#define LIGHT_ROW(name, parse, field) { name, parse, nullptr, (int)offsetof(W3DLightDrawModuleData, field) }
// RW 0xBE3638, the 10 rows in the binary's order (the retail table has no terminator: S-074)
const FieldParse kLightFieldParse[] = {
	LIGHT_ROW("Ambient", INI::parseRGBColor, m_ambient),
	LIGHT_ROW("Diffuse", INI::parseRGBColor, m_diffuse),
	LIGHT_ROW("Specular", INI::parseRGBColor, m_specular),
	LIGHT_ROW("Radius", INI::parseReal, m_radius),
	LIGHT_ROW("Intensity", INI::parseReal, m_intensity),
	LIGHT_ROW("FlickerAmplitude", INI::parseReal, m_flickerAmplitude),
	LIGHT_ROW("FlickerFrequency", INI::parseReal, m_flickerFrequency),
	LIGHT_ROW("PulseAmplitude", INI::parseReal, m_pulseAmplitude),
	LIGHT_ROW("PulseFrequency", INI::parseReal, m_pulseFrequency),
	LIGHT_ROW("AttachToBoneInAnotherModule", INI::parseAsciiString, m_attachToBone),
	{ nullptr, nullptr, nullptr, 0 }
};
#undef LIGHT_ROW

const float kTwoPi = 6.2831855f; // RW 0xBDD38C
} // namespace

void W3DLightDrawModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kLightFieldParse); // RW 0x4CF110: tables [0xBE3638]
}

namespace W3DLightDrawTables
{
const FieldParse *light() { return kLightFieldParse; }
}

W3DLightDraw::W3DLightDraw(CrtRand &random)
{
	// RW 0x4CF15D: fld Random_Float, fmul 30.0 (RW 0xBDAE54), fstp +0x10
	m_phase = (float)((double)random.randomFloat() * 30.0);
}

void W3DLightDraw::initLight(const W3DLightDrawModuleData &data, LightPulseFx &light)
{
	light = LightPulseFx();
	light.enabled = true;                                   // RW 0x4CF189: setEnabled(true)
	light.color = data.m_diffuse;                           // RW 0x4CF1CB: Diffuse (+ 0xE0); Ambient (+ 0xD4) is not drawn (S-113)
	light.targetColor = data.m_diffuse;
	light.position = Coord3D{ 0.0f, 0.0f, 0.0f };           // RW 0x4CF1F9: Set_Position (0, 0, 0)
	light.increaseFrames = light.decreaseFrames = 0;        // RW 0x4CF203: setFrameFade(0, 0)
	light.currentIncrease = light.currentDecrease = 0;
	light.farAttenStart = 1.0f;                             // RW 0x4CF21C: FarAttenStart 1.0
	light.farAttenEnd = light.targetEnd = data.m_radius;    // RW 0x4CF216: FarAttenEnd = Radius
	light.intensity = 1.0f;                                 // LightClass's own until the first draw
}

float W3DLightDraw::doDrawModule(const W3DLightDrawModuleData &data, CrtRand &random)
{
	// RW 0x4CF2BB: phase += 1.0 / (float)LOGICFRAMES_PER_SECOND (SSE)
	m_phase += 1.0f / (float)LOGICFRAMES_PER_SECOND;
	float height = data.m_intensity; // RW 0x4CF2F9
	if (data.m_flickerAmplitude > 0.0f) // RW 0x4CF3C4: comiss, jbe
	{
		if (!(0.0f < m_countdown)) // RW 0x4CF3C9: comiss 0, countdown; jb skips (an unordered countdown skips too)
		{
			m_countdown = (float)LOGICFRAMES_PER_SECOND / data.m_flickerFrequency; // RW 0x4CF3CF (SSE)
			const float maximum = data.m_flickerAmplitude;
			const float minimum = -data.m_flickerAmplitude; // RW 0x4CF3E7: fchs, stored as float
			// RW 0x4CF3EF .. 0x4CF40B on the x87: Random_Float * (max - min) + min + Intensity - +0x1C, / countdown, stored as float
			const double r = (double)random.randomFloat();
			const double v = ((double)maximum - (double)minimum) * r + (double)minimum + (double)height - (double)m_height;
			m_step = (float)(v / (double)m_countdown);
		}
		m_countdown -= 1.0f;          // RW 0x4CF413
		height = m_step + m_height;   // RW 0x4CF420
		m_height = height;
	}
	if (data.m_pulseAmplitude > 0.0f) // RW 0x4CF434
	{
		// RW 0x4CF43E: PulseFrequency * phase * 2 pi (SSE), fsin, stored as float; then PulseAmplitude * that + height (SSE)
		const float x = data.m_pulseFrequency * m_phase * kTwoPi;
		const float s = (float)std::sin((double)x);
		height = data.m_pulseAmplitude * s + height;
	}
	return height; // RW 0x4CF479: the light's Intensity (+ 0xD0)
}

void W3DLightDrawSet::reset()
{
	m_modules.clear();
	m_candidates.clear();
	m_scannedSlots = 1;
	m_manager = nullptr;
	m_poolGeneration = ~0ull;
	m_frame.clear();
}

const W3DLightDraw *W3DLightDrawSet::find(std::uint64_t key) const
{
	const auto it = m_modules.find(key);
	return it == m_modules.end() ? nullptr : &it->second.draw;
}

size_t W3DLightDrawSet::lightHandle(std::uint64_t key) const
{
	const auto it = m_modules.find(key);
	return it == m_modules.end() ? (size_t)-1 : it->second.handle;
}

void W3DLightDrawSet::update(const std::vector<Source> &sources, LightPulsePool &pool)
{
	if (pool.generation() != m_poolGeneration)
	{
		// the pool was cleared (a game ended): its lights are gone, so are the modules holding them
		m_modules.clear();
		m_poolGeneration = pool.generation();
	}
	for (auto &kv : m_modules)
	{
		kv.second.seen = false;
	}
	for (const Source &s : sources)
	{
		if (!s.data)
		{
			continue;
		}
		auto it = m_modules.find(s.key);
		if (it == m_modules.end())
		{
			// RW 0x4CF121: the module is made with its drawable. INFERENCE (S-113): its first transform (reactToTransformChange, RW 0x4CF5EE) is the
			// drawable's position here; the AttachToBoneInAnotherModule bone's own position is never drawn (the next draw replaces it)
			Module m{ W3DLightDraw(m_random), pool.acquireDrawLight(), false };
			W3DLightDraw::initLight(*s.data, pool.drawLight(m.handle));
			pool.drawLight(m.handle).position = s.position;
			it = m_modules.emplace(s.key, m).first;
		}
		Module &m = it->second;
		m.seen = true;
		LightPulseFx &light = pool.drawLight(m.handle);
		light.enabled = !s.hidden; // RW 0x4CF29D: setHidden -> setEnabled(!hidden)
		if (s.hidden || !s.drawn)
		{
			continue; // Drawable::draw does not reach its draw modules (hidden, or fully under the shroud): the light keeps its place and intensity
		}
		light.position = s.position; // RW 0x4CF38E: the drawable's position as the light's translation
		light.intensity = m.draw.doDrawModule(*s.data, m_random);
	}
	for (auto it = m_modules.begin(); it != m_modules.end();)
	{
		if (it->second.seen)
		{
			++it;
			continue;
		}
		pool.releaseDrawLight(it->second.handle); // RW 0x4CF5B6: the destructor disables and releases the light
		it = m_modules.erase(it);
	}
}

void W3DLightDrawSet::update(const DrawableManager &drawables, const LogicSnapshot *presented, bool shroudDrawn, LightPulsePool &pool)
{
	if (m_manager != &drawables || pool.generation() != m_poolGeneration)
	{
		for (const auto &kv : m_modules)
		{
			if (pool.generation() == m_poolGeneration)
			{
				pool.releaseDrawLight(kv.second.handle);
			}
		}
		m_modules.clear();
		m_candidates.clear();
		m_scannedSlots = 1;
		m_manager = &drawables;
		m_poolGeneration = pool.generation();
	}
	// a drawable's entries are made by its constructor only (Drawable::addEntry) and ids are never reused: each new slot is looked at once
	for (; m_scannedSlots < drawables.slotCount(); ++m_scannedSlots)
	{
		const Drawable *d = drawables.find((DrawableID)m_scannedSlots);
		if (!d)
		{
			continue;
		}
		const std::vector<DrawEntry> &entries = d->entries();
		for (size_t k = 0; k < entries.size() && k < 256; ++k)
		{
			if (entries[k].kind == W3D_DRAWKIND_LIGHT)
			{
				m_candidates.push_back(((std::uint64_t)m_scannedSlots << 8) | k);
			}
		}
	}
	m_frame.clear();
	size_t kept = 0;
	for (const std::uint64_t key : m_candidates)
	{
		const Drawable *d = drawables.find((DrawableID)(key >> 8));
		if (!d)
		{
			continue; // destroyed: its module goes with it
		}
		m_candidates[kept++] = key;
		const DrawEntry &e = d->entries()[key & 0xFF];
		Source s;
		s.key = key;
		s.data = dynamic_cast<const W3DLightDrawModuleData *>(e.data);
		s.position = *d->getPosition();
		// ZH Drawable::updateHiddenStatus: hidden or hidden by stealth reaches every draw module's setHidden; a script's module hide likewise
		s.hidden = e.moduleHidden || d->presentedLook(presented, false).hidden;
		// ZH Drawable::draw returns early for a drawable fully under the shroud: its modules are not drawn this frame
		s.drawn = s.hidden || !d->presentedLook(presented, shroudDrawn).hidden;
		m_frame.push_back(s);
	}
	m_candidates.resize(kept);
	update(m_frame, pool);
}

std::string W3DLightDrawStopLine()
{
	return "[S-113] W3DLightDraw (lane LIGHTDRAW-1): ported from RotWK: the module data (RW 0x4CF515, table RW 0xBE3638), the light the constructor takes "
		   "(RW 0x4CF121: Diffuse, FarAttenStart 1, FarAttenEnd Radius, no fade), the draw's flicker and pulse into the light's Intensity and its position "
		   "at the drawable's (RW 0x4CF2B5), setHidden (RW 0x4CF29D); inference: the draw runs on the 30 Hz client step, the module's Random_Float is a "
		   "generator of its own, the light's Ambient is not drawn (the object shaders take a light's diffuse only), the lights reach the four "
		   "dynamic-light slots of the object shaders as one set per frame (the nearest to the camera of those in view; retail picks per object), "
		   "a light whose drawable was never drawn stands at the drawable's position (not at its bone), retail's pool reuse of a hidden draw's disabled "
		   "light by a pulse is not reproduced, and the terrain is not lit by it";
}
