// OpenBFME. GPL-3.0. See W3DObjectLighting.h (sources: RW 0x54F406 / 0x54CF11 / 0x54CFD0 / 0x54D083 / 0x4AC97F, ZH
// W3DDisplay::setTimeOfDay and LightEnvironmentClass).

#include "GameEngineDevice/W3DDevice/GameClient/W3DObjectLighting.h"

#include "GameClient/MapChunks.h"

#include <algorithm>

namespace
{

// ZH LightEnvironmentClass::Add_Light: "Don't accept lights that are almost black".
bool acceptedLight(const GlobalLight &l)
{
	return !(l.diffuse[0] < 0.05f && l.diffuse[1] < 0.05f && l.diffuse[2] < 0.05f);
}

void fillSet(const GlobalLight (&lights)[3], float scale, const float *lightScale, W3DLightSet &out)
{
	out = W3DLightSet();
	// ZH: the scene ambient is light 0's ambient (the global lights themselves carry none); Pre_Render_Update clamps it.
	for (int c = 0; c < 3; ++c)
	{
		out.ambient[c] = std::clamp(lights[0].ambient[c], 0.0f, 1.0f) * scale;
	}
	for (int i = 0; i < 3; ++i)
	{
		// RTS3DScene::updateFixedLightEnvironments (RW 0x46E009): the light's diffuse is set to diffuse * the map's light scale BEFORE
		// Add_Light, so the 0.05 test sees the scaled colour
		GlobalLight l = lights[i];
		for (int c = 0; c < 3; ++c)
		{
			l.diffuse[c] = lightScale ? lightScale[c] * lights[i].diffuse[c] : lights[i].diffuse[c];
		}
		if (!acceptedLight(l))
		{
			continue;
		}
		W3DLightSet &s = out;
		for (int c = 0; c < 3; ++c)
		{
			s.color[s.count][c] = l.diffuse[c] * scale;
			s.toLight[s.count][c] = -l.lightPos[c]; // ZH Init_From_Directional_Light: Direction = -transform Z = -lightPos
		}
		++s.count;
	}
}

} // namespace

namespace W3DObjectLightingUtil
{

float effectColorScale(const GlobalLightingData &lighting)
{
	return lighting.version >= 5 && lighting.terrainLightingMultiplier > 1.0f ? 2.0f : 1.0f;
}

bool fromMap(const GlobalLightingData &lighting, W3DObjectLighting &out, std::string *error, const float *lightScale)
{
	if (lighting.timeOfDay < 1 || lighting.timeOfDay > 4)
	{
		if (error)
		{
			*error = "GlobalLighting: time of day " + std::to_string(lighting.timeOfDay) + " is not 1..4 (Morning .. Night)";
		}
		return false;
	}
	out = W3DObjectLighting();
	out.timeOfDay = lighting.timeOfDay;
	// RW 0x4AC97F: version >= 5 and multiplier > 1.0 sets the byte RW 0xD9B03C; the SAS callbacks then scale by 2.0 (RW 0xBD889C).
	out.colorScale = effectColorScale(lighting);
	const TimeOfDayLights &tod = lighting.tod[lighting.timeOfDay - 1];
	fillSet(tod.objects, out.colorScale, lightScale, out.objects);
	fillSet(tod.infantry, out.colorScale, lightScale, out.infantry);
	return true;
}

std::vector<std::string> stopLines()
{
	return {
		"[S-390] object lighting: the light environment is filled like ZH's (scene ambient = the map's objects light 0 ambient clamped to [0, 1], "
		"the directional lights = the objects lights 0..2 whose diffuse reaches 0.05, direction -lightPos), infantry drawables use the map's "
		"infantry set; the doubling of both colours (GlobalLighting multiplier > 1, RW 0x4AC97F -> 0x54CF3B) and the effect formulas "
		"(normalmapped.fxo, defaultw3d.fxo) are target facts; the cloud shadow texture, point lights, shadows, shroud and fog of the effects are not applied",
	};
}

} // namespace W3DObjectLightingUtil
