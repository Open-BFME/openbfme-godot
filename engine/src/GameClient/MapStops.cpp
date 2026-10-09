// OpenBFME. GPL-3.0.
// See GameClient/MapStops.h. The text of each gap must match its row in docs/STOPS.md.

#include "GameClient/MapStops.h"

#include <cstring>

namespace
{
const std::vector<MapStop> kStops = {
	{ "S-030", "Terrain UVs", "BFME2/RotWK cliff-info UV unit (256-px units from the class block's left/bottom edge, wrapped per pixel inside the class) and the tile atlas layout (one 2048-wide atlas, 16-bit tiles) are not confirmed against the binary; the renderer uses an isolated, reported assumption" },
	{ "S-031", "Terrain shader inputs", "Taint mask, shadow-receiving variants, shroud texture and the cloud/macro uniform semantics (cloud sign, MapSize, MapBorderWidth) are not retail-verified; fog comes only from map.ini; the map's PostEffectsChunk colour grade (LookupTablePostEffect) is applied since lane RENDER-4 (S-1650)" },
	{ "S-032", "Terrain blend states", "Draw states of the blend layers (SRCALPHA/INVSRCALPHA) are inferred, and the spec's 'same triangle flip for all layers' conflicts with ZH, where the extra layer has its own flip; the layers are composited per pixel in one gamma-space pass instead of three blended draws" },
	{ "S-033", "Terrain lighting", "Whether the CPU vertex colour contains the sun and the sun direction sign (-lightPos) are inferred; the overbright flag is a target fact (RENDER-1): the GlobalLighting reader sets RW 0xD9B03C to (terrainLightingMultiplier > 1.0) for chunk version >= 5 (RW 0x4AC97F .. 0x4AC9A5), it selects MODULATE2X (RW 0x537335) and doubles the object light colours (RW 0x54CF3B)" },
	{ "S-034", "Terrain normal maps", "Retail fallback for texture classes without a _nrm texture (534 of 990 in the corpus) is unknown; a flat normal texel (128,128,255), like every retail normal map over flat ground, is used" },
	{ "S-035", "Water", "WaterShader.FX materials, the depth-colour LUT use, river mesh/UV generation and standing waves are not ported; simple stand-in shaders" },
	{ "S-036", "Roads", "Only straight road segments; tee, curve, cross and alpha joins and BFME2 stacking are not ported" },
	{ "S-037", "Scripts", "Condition/action types are re-matched by internal name with the rules of the RotWK parsers (lane SCRIPT-1: RW 0x7B776D / 0x7B68F9 against the registries extracted from RW 0x7D01C0 / 0x7D5270, ScriptTemplates.h); the registries come from the community-modified game.dat (S-001), and the old-file heals no retail record needs are untested against data" },
	{ "S-038", "Sides", "validateSides is not ported" },
	{ "S-039", "Loose files", "Loose .map/.scb files in the install folders (not part of retail; the Windows development install had two 0-byte RotWK libraries, lib_end_mission and lib_gollumspawn) are contamination (PLAN rule 7): reported when present, never read; a clean install has none and the map report then omits S-039" },
	{ "S-060", "Terrain height maths", "TerrainLogic::getGroundHeight and its normal run in plain float32 in the donor's operation order, not through the numeric facade (NumericState), and are not compared with the RotWK binary: retail's compiled x87/SSE mix can differ in the last bits (the BFME1 retail body, 0x6CBB50, is x87 with CRT floor calls); the RotWK counterpart of BaseHeightMapRenderObjClass::getHeightMapHeight was not located (the BFME1 byte pattern is absent from game.dat)" },
};
} // namespace

const std::vector<MapStop> &MapStops::all()
{
	return kStops;
}

const MapStop *MapStops::find(const char *id)
{
	for (const MapStop &s : kStops)
	{
		if (std::strcmp(s.id, id) == 0)
		{
			return &s;
		}
	}
	return nullptr;
}

const std::vector<const MapStop *> &MapStops::terrainRender()
{
	static const std::vector<const MapStop *> v = { find("S-030"), find("S-031"), find("S-032"), find("S-033"), find("S-034"), find("S-035"), find("S-036") };
	return v;
}

const std::vector<const MapStop *> &MapStops::terrainLogic()
{
	static const std::vector<const MapStop *> v = { find("S-060") };
	return v;
}

const std::vector<const MapStop *> &MapStops::mapLoad()
{
	static const std::vector<const MapStop *> v = { find("S-037"), find("S-038"), find("S-039") };
	return v;
}
