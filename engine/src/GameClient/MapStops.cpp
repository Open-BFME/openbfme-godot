// OpenBFME. GPL-3.0.
// See GameClient/MapStops.h. The text of each gap must match its row in docs/STOPS.md.

#include "GameClient/MapStops.h"

#include <cstring>

namespace
{
const std::vector<MapStop> kStops = {
	{ "S-030", "Terrain UVs", "BFME2/RotWK cliff-info UV unit (256-px units from the class block's left/bottom edge, wrapped per pixel inside the class) and the tile atlas layout (one 2048-wide atlas, 16-bit tiles) are not confirmed against the binary; the renderer uses an isolated, reported assumption" },
	{ "S-031", "Terrain shader inputs", "Taint mask, shadow-receiving variants and the cloud/macro uniform semantics (cloud sign, MapSize, MapBorderWidth) are not retail-verified; the Base / Normal atlases are filtered by the hardware's anisotropic sampler (terrain.fxo: MinFilter ANISOTROPIC, Mag / Mip LINEAR, MaxAnisotropy 8) at the viewport's level (the OpenBFMEAnisotropic option, 16x by default) with the footprint's ratio capped at retail's 8 and its LOD at ZH's 3 mip levels (`atlas_gradients`): below a viewport level of 8 the hardware may take an LOD deeper than the class gutters allow, and a footprint shrunk to the LOD cap keeps its probe count (retail clamps the LOD alone), so the far ground of an oblique view can differ from D3D9's; fog comes only from map.ini; the map's PostEffectsChunk colour grade (LookupTablePostEffect) is applied since lane RENDER-4 (S-1650)" },
	{ "S-032", "Terrain blend states", "Draw states of the blend layers (SRCALPHA/INVSRCALPHA) are inferred, and the spec's 'same triangle flip for all layers' conflicts with ZH, where the extra layer has its own flip; retail bakes the ground on the CPU per 16 x 16 cell tile (FlatHeightMapRenderObjClass; TerrainBake.h): 32 px/cell A1R5G5B5 for a tile nearer than 430 * clamp(width / 1024, 0.5, 2.5) + 25 units, else 16 px/cell (DXT1 colour), normal maps A1R5G5B5, blended with the alpha tiles' pixel ramps by (src * alpha + (255 - alpha) * dst) >> 8 (RW 0x4AC035) and truncated to 5 bits per channel (updateFlat), then lit once; the port composites the layers' filtered albedo and normal texels per pixel in one gamma-space pass instead of blended draws and lights that composite once, reads a far tile (retail's distance rule, judged each render frame from a game's camera; a viewer without one keeps every tile near) at atlas mip 1, and does not reproduce: the blend alpha (corner alphas interpolated, not the alpha tiles' ramps), the blend arithmetic and the 5-bit truncation, the far colour's DXT1 encoding and D3DXFilterTexture's box mips, the cached per-class cliff texture (updateCliff, mode 1: a cliff cell reads its class block of the atlas, wrapped per pixel), DoTesselatedUpdate's tile mesh (the port draws every cell), the tile boxes' vertex set (every vertex) and the tile width (BFME2 donor, not re-read in RotWK)" },
	{ "S-033", "Terrain lighting", "Whether the CPU vertex colour contains the sun and the sun direction sign (-lightPos) are inferred; the overbright flag is a target fact (RENDER-1): the GlobalLighting reader sets RW 0xD9B03C to (terrainLightingMultiplier > 1.0) for chunk version >= 5 (RW 0x4AC97F .. 0x4AC9A5), it selects MODULATE2X (RW 0x537335) and doubles the object light colours (RW 0x54CF3B)" },
	{ "S-034", "Terrain normal maps", "Retail fallback for texture classes without a _nrm texture (534 of 990 in the corpus) is unknown; a flat normal texel (128,128,255), like every retail normal map over flat ground, is used" },
	{ "S-035", "Water", "Standing water preserves authored FX techniques, PCA/PCA_U normals and shoreline Gerstner displacement on a donor high-detail grid alongside the bump/specular path; TIME, LOD selection, scene-light binding and terrain depth are inferred; reflections, shadow filtering, other PCA LODs, river outline cross-section start and minimum water LOD, depth-colour LUT and legacy shoreline texture tracks remain unported" },
	{ "S-036", "Roads", "Straight paths, mitered and curved corners, tee and four-way atlas junctions and authored JOIN end caps are rendered; Y/H junctions, cross-type alpha alignment and BFME2 stacking remain unported. Roads are shaded as retail draws them (lane ROADS-1, RoadShading in GameClient/TerrainRoads.h): terrain.fxo technique Default with RenderingMode 2, PSDefault_Array element 2 (ps_2_0 blob at 0xC160) and the effect's vs_1_1 blob at 0x1C780, vertex normal (0, 0, 1) (RW 0x4D7D32 .. 0x4D7D3C), the road's _nrm texture or the 1x1 (127, 127, 255, 0) fallback (RW 0x4D7132 .. 0x4D7176), and the sun, cloud, macro, fog and shroud inputs of the terrain material. Not ported or unresolved: the shadow-receiving element (NumShadows >= 1: element 7) and the resource-texture elements (IsResourceTextureEnabled: element 12); the road's blend and depth states (the Default pass declares ALPHABLENDENABLE 0, ZWRITEENABLE 0, SRCBLEND / DESTBLEND SRCALPHA / INVSRCALPHA, ZFUNC LESSEQUAL: the port keeps the former alpha-blended, depth-tested draw without depth writes as inference); the dynamic-light road variant (loadLit4PtSection, roads under dynamic lights: its terrain normal and light gradient are not ported); the gate TheGlobalLighting + 0x3E that FlatHeightMap.cpp:811 tests before it draws roads (not read: roads always draw); GlobalData + 0x49, the flag that loads the _nrm companions (its RotWK writer is not read: the companions are loaded when they exist)" },
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
