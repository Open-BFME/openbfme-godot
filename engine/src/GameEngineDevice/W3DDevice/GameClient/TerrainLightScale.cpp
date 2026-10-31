// OpenBFME. GPL-3.0. See TerrainLightScale.h (sources: RW 0x4ACA92 / 0x4ACAD1 / 0x4E0E40 / 0x4E0F5D / 0x5521C0 / 0x4E2B3A / 0x4690C1 /
// 0x46E009 / 0x4670E1, BFME2 decomp FlatHeightMap.cpp:773, BaseHeightMapDoTheLight.cpp:309).

#include "GameEngineDevice/W3DDevice/GameClient/TerrainLightScale.h"

#include "GameClient/MapChunks.h"

#include <algorithm>

namespace TerrainLightScale
{

int terrainRenderingMode(const Lod &lod, std::string *error)
{
	if (lod.terrainLOD < 0)
	{
		if (error)
		{
			*error = "TerrainLOD " + std::to_string(lod.terrainLOD) + " is negative: retail indexes its request table (RW 0xD9A608) out of bounds";
		}
		return -1;
	}
	// RW 0x4E0E5A .. 0x4E0E80: UseTerrainNormalMap -> 2, else min(TerrainLOD, 1); the table RW 0xD9A608 maps 0 / 1 / 2 to themselves, and
	// terrain.fxo has the techniques Default (2), Default_M (1) and Default_L (0), so the level found is the one requested.
	return lod.useTerrainNormalMap ? 2 : std::min(lod.terrainLOD, 1);
}

void rawScale(const GlobalLightingData &lighting, float out[3])
{
	for (int c = 0; c < 3; ++c)
	{
		out[c] = lighting.hasTerrainLightScale ? lighting.terrainLightScale[c] : 1.0f;
	}
}

bool resolve(const GlobalLightingData &lighting, const Lod &lod, Scales &out, std::string *error)
{
	out = Scales();
	if (lod.shaderLOD < 0)
	{
		if (error)
		{
			*error = "ShaderLOD " + std::to_string(lod.shaderLOD) + " is negative";
		}
		return false;
	}
	out.terrainMode = terrainRenderingMode(lod, error);
	if (out.terrainMode < 0)
	{
		return false;
	}
	rawScale(lighting, out.raw);
	out.terrainScaled = out.terrainMode < 1;  // `call [vtable + 0x24C]; cmp eax, 1; jge skip` at RW 0x4E2B2A, 0x4690B1, 0x4E46BA
	out.objectsScaled = lod.shaderLOD < 2;    // `cmp [0xD9B058], 2; jge skip` at RW 0x46DFC0, 0x46F890, 0x46FB26 (RW 0x47141C: <= 1)
	for (int c = 0; c < 3; ++c)
	{
		out.terrain[c] = out.terrainScaled ? out.raw[c] : 1.0f;
		out.objects[c] = out.objectsScaled ? out.raw[c] : 1.0f;
		out.scorch[c] = out.raw[c];
	}
	return true;
}

std::vector<std::string> stopLines()
{
	return {
		"[S-033] terrain light scale (GlobalLighting v8): the storage, the eight readers, their guards and the terrain rendering mode "
		"(FlatHeightMap vtable slot 147 = the terrain.fx technique level) are target facts; applied here to the per-pixel sun, the terrain vertex "
		"lights and the roads (rendering mode 0 only), and to the object light sets (shader LOD below 2). Not applied because not drawn: the "
		"scorch marks (always scaled in retail), the tank tracks, the per-instance prop / tree vertex light (RW 0x4E44FB, its role is inferred), "
		"the per-object light of renderOneObject and the tree light environment of Flush (RW 0x46F89E, 0x46FB58, 0x471429). The map renderers draw "
		"the High tier (normal-mapped terrain, shader LOD 2 or above), where the scale is 1: only the viewer options use_terrain_normal_map / "
		"terrain_lod / shader_lod of MapTerrainBuilder (terrain, roads) and MapObjectBuilder (object lights) select a lower tier, GameWorld lights "
		"its objects at the High tier, and MapTerrainBuilder reports the object factor as object_light_scale_retail without applying it",
	};
}

} // namespace TerrainLightScale
