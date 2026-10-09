// OpenBFME. GPL-3.0. The map's hardware fog on W3D models and CPU particles (lane RENDER-4): see W3DHardwareFog.h.

#include "GameEngineDevice/W3DDevice/GameClient/W3DHardwareFog.h"

#include <algorithm>

namespace W3DHardwareFog
{

FogMode ModeForBlend(unsigned srcBlend, unsigned dstBlend)
{
	// RW 0x537CE0 (ZH ShaderClass::Enable_Fog), case by source blend
	switch (srcBlend & 3u)
	{
	case 0: // ZERO
		return (dstBlend & 7u) == 2 ? FOG_WHITE : FOG_DISABLE; // ZERO / SRC_COLOR (multiply)
	case 1: // ONE
		if ((dstBlend & 7u) == 0)
		{
			return FOG_ENABLE; // ONE / ZERO (opaque)
		}
		return ((dstBlend & 7u) == 1 || (dstBlend & 7u) == 3) ? FOG_SCALE_FRAGMENT : FOG_DISABLE; // ONE / ONE, ONE / ONE_MINUS_SRC_COLOR
	case 2: // SRC_ALPHA
		return (dstBlend & 7u) == 5 ? FOG_ENABLE : FOG_DISABLE; // SRC_ALPHA / ONE_MINUS_SRC_ALPHA
	default: // ONE_MINUS_SRC_ALPHA
		return (dstBlend & 7u) == 4 ? FOG_ENABLE : FOG_DISABLE; // ONE_MINUS_SRC_ALPHA / SRC_ALPHA
	}
}

FogMode ModeForShaderWord(std::uint32_t word)
{
	return ModeForBlend((word >> 14) & 3u, (word >> 5) & 7u);
}

float Factor(bool enabled, float start, float end, float range)
{
	if (!enabled)
	{
		return 1.0f;
	}
	const float span = std::max(end - start, 0.0001f);
	return 1.0f - std::min(1.0f, std::max(0.0f, (range - start) / span));
}

std::string GlslFor(FogMode mode)
{
	std::string s = "global uniform vec3 w3d_fog;\n"         // x: HardwareFogEnable (0 / 1), y: HardwareFogStart, z: HardwareFogEnd
					"global uniform vec3 w3d_fog_color;\n"   // HardwareFogColor / 255 (gamma space)
					"vec3 w3d_apply_fog(vec3 c, float range) {\n"
					"\tfloat f = 1.0 - w3d_fog.x * clamp((range - w3d_fog.y) / max(w3d_fog.z - w3d_fog.y, 0.0001), 0.0, 1.0);\n";
	switch (mode)
	{
	case FOG_ENABLE: s += "\treturn mix(w3d_fog_color, c, f);\n"; break;
	case FOG_SCALE_FRAGMENT: s += "\treturn c * f;\n"; break;
	case FOG_WHITE: s += "\treturn mix(vec3(1.0), c, f);\n"; break;
	case FOG_DISABLE: s += "\treturn c;\n"; break;
	}
	s += "}\n";
	return s;
}

const char *StopText()
{
	return "S-1651: the map's hardware fog on W3D models and CPU particles follows ShaderClass::Enable_Fog (RW 0x537CE0) and the effects' range fog; "
		   "inferred: the fixed-function range and linear mode, the FX materials' per-blend fog colour, a per-pixel factor instead of per-vertex";
}

} // namespace W3DHardwareFog
