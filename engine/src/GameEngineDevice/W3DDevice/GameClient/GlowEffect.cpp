// OpenBFME. GPL-3.0. The GlowEffect block (lane GLOW-1): see GlowEffect.h.

#include "GameEngineDevice/W3DDevice/GameClient/GlowEffect.h"

#include "Common/INI.h"

#include <cstddef>

static_assert(offsetof(GlowEffect, glowEnabled) == 0x00 && offsetof(GlowEffect, glowDiameter) == 0x04 && offsetof(GlowEffect, glowIntensity) == 0x08 &&
		offsetof(GlowEffect, glowTextureWidth) == 0x0C && offsetof(GlowEffect, radiusScale1) == 0x10 && offsetof(GlowEffect, amplitude1) == 0x14 &&
		offsetof(GlowEffect, radiusScale2) == 0x18 && offsetof(GlowEffect, amplitude2) == 0x1C && offsetof(GlowEffect, terrainGlow) == 0x20 &&
		offsetof(GlowEffect, multipassGlowEnabled) == 0x21,
	"GlowEffect is the retail record (RW 0xDA28E4, field table RW 0xC1E810)");

namespace
{
#define GLOW_OFF(f) (int)offsetof(GlowEffect, f)
// RW 0xC1E810, in the binary's order (RadiusScale2 before Amplitude1)
const FieldParse kGlowEffectFields[] = {
	{ "GlowEnabled", INI::parseBool, nullptr, GLOW_OFF(glowEnabled) },
	{ "GlowDiameter", INI::parseInt, nullptr, GLOW_OFF(glowDiameter) },
	{ "GlowIntensity", INI::parseReal, nullptr, GLOW_OFF(glowIntensity) },
	{ "GlowTextureWidth", INI::parseInt, nullptr, GLOW_OFF(glowTextureWidth) },
	{ "RadiusScale1", INI::parseReal, nullptr, GLOW_OFF(radiusScale1) },
	{ "RadiusScale2", INI::parseReal, nullptr, GLOW_OFF(radiusScale2) },
	{ "Amplitude1", INI::parseReal, nullptr, GLOW_OFF(amplitude1) },
	{ "Amplitude2", INI::parseReal, nullptr, GLOW_OFF(amplitude2) },
	{ "TerrainGlow", INI::parseBool, nullptr, GLOW_OFF(terrainGlow) },
	{ "MultipassGlowEnabled", INI::parseBool, nullptr, GLOW_OFF(multipassGlowEnabled) },
	{ nullptr, nullptr, nullptr, 0 },
};
#undef GLOW_OFF
} // namespace

const FieldParse *GlowEffectStore::fieldTable()
{
	return kGlowEffectFields;
}

void GlowEffectStore::parse(INI *ini)
{
	// RW 0x70B73B .. 0x70B757: saved -> active, the block over it
	m_active = m_saved;
	ini->initFromINI(&m_active, kGlowEffectFields);
	// RW 0x70B75F: a map.ini (2) or a ChildObject (4) load leaves the saved copy alone
	const INILoadType type = ini->getLoadType();
	if (type != INI_LOAD_CREATE_OVERRIDES && type != INI_LOAD_CHILD_OBJECT)
	{
		m_saved = m_active;
	}
	m_dirty = true; // RW 0x70B775
}

void GlowEffectStore::reset()
{
	m_active = m_saved; // RW 0x70B5B6 .. 0x70B5C0
	m_dirty = true;     // RW 0x70B5C3
}

void GlowEffectStore::registerBlock(INIBlockRegistry &registry)
{
	registry.registerBlock("GlowEffect", [this](INI *ini) { parse(ini); });
}
