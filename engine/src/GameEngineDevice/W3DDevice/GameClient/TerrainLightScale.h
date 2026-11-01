// OpenBFME. GPL-3.0.
//
// TerrainLightScale: the map's "low LOD light scale" (GlobalLighting chunk version 8, three floats after the shadow colour) and the detail
// tiers at which retail applies it (lane TERRAINSCALE-1, stop S-033). Client / draw only: never read by the logic.
//
// TARGET (RotWK game.dat, read statically; caveat S-001: community-modified image):
//   * The GlobalLighting reader stores the floats at TheWritableGlobalData (RW 0xDE4364) + 0x944 / 0x948 / 0x94C for chunk version >= 8
//     (RW 0x4ACA92 .. 0x4ACACF), else 1.0f from RW 0xBD1908 (RW 0x4ACAD1 .. 0x4ACAF8); the chunk writer reads them back (RW 0x4AD095).
//   * Exactly eight code sites read them (movss from [eax + 0x944 / 0x948 / 0x94C]); nothing else in the image does. Each is guarded:
//       RW 0x4E2B3A  FlatHeightMap terrain render: the sun colour of the per-pixel terrain shader        } guarded by the terrain
//       RW 0x4690C1  BaseHeightMap::doTheLight: each terrain light's diffuse (vertex colours, roads)       } rendering mode < 1:
//       RW 0x4E46CA  per-instance terrain vertex light (called from RW 0x4E4D2C)                           } `call [vtable + 0x24C]`
//     and, guarded by the shader LOD [RW 0xD9B058] being below 2 (RW 0x46DFC0, 0x46F890, 0x46FB26) or at most 1 (RW 0x47141C):
//       RW 0x46E009  RTS3DScene::updateFixedLightEnvironments: the objects' and infantry global light diffuse
//       RW 0x46F89E / 0x46FB58  RTS3DScene::renderOneObject: the per-object light colours
//       RW 0x471429  RTS3DScene::Flush: the terrain-side light environment (trees)
//     and unconditionally:
//       RW 0x4670E1  BaseHeightMap::updateScorches: the scorch marks' vertex colour, ambient(+0x8D8) + scale * diffuse(+0x8FC) * 0.5
//                    (0.5 = RW 0xBD869C), times 2.0 (RW 0xBD889C) when the overbright byte RW 0xD9B03C is set, clamped to 1.
//   * The terrain rendering mode (vtable slot 147, +0x24C): FlatHeightMapRenderObjClass is the only class derived from BaseHeightMap (its
//     constructor RW 0x4E3531 is the only caller of the base constructor RW 0x46C1F3; vtable RW 0xBE4760). Its slot 147 is RW 0x4E0F5D:
//     `eax = [this + 0x3938]; return eax ? [eax + 0x1C] : 4` where [this + 0x3938] is the FX rendering method built from terrain.fx
//     technique "Default" (RW 0x4E0E40 -> 0x552578 -> 0x5521C0) and [method + 0x1C] is the technique's detail level: the suffix index of
//     the technique that was found ("_L" 0, "_M" 1, "" 2, "_U" 3; RW 0xBE9BAC), searched downward from min(requested, 2). The request is
//     2 when GlobalData + 0x49 (UseTerrainNormalMap) is set, else min(TerrainLOD, 1) with TerrainLOD = manager + 0x1780 (RW 0x4E0E5A ..
//     0x4E0E80, table RW 0xD9A608 = 0, 1, 2). terrain.fxo has exactly the techniques Default, Default_M, Default_L, so the level found is the
//     requested one. The BFME2 decomp calls the same slot getRenderingMode (FlatHeightMap.cpp:773, BaseHeightMapDoTheLight.cpp:309) without
//     a body; it is NOT the "RenderingMode" shader parameter that setRenderingMode(0..4) feeds.
//   * [RW 0xD9B058] is the ShaderLOD of the active static level (written at RW 0x601D8A from the level record + 0x38); the decomp calls it
//     LODTextureReduction (RTS3DSceneRenderOneObject.cpp:527, RTS3DSceneFlush.cpp:213, W3DSceneUpdateFixedLightEnvironments.cpp:326).
//   Consequence: GameLOD.ini's High and UltraHigh levels (UseTerrainNormalMap = Yes, ShaderLOD High / UltraHigh) never scale the terrain; the
//   scale is a compensation for the VeryLow / Low terrain (mode 0) and for the object lights at the Low / Medium shader LOD.
// DONOR (BFME2 decomp): GlobalLightingDataChunk.cpp:219 (finalValues = m_terrainLightScale, +0x944), FlatHeightMap.cpp:770-777,
//   BaseHeightMapDoTheLight.cpp:309, RTS3DSceneRenderOneObject.cpp:21 / 527 / 547 / 633, RTS3DSceneFlush.cpp:213-219,
//   W3DSceneUpdateFixedLightEnvironments.cpp:326-331, W3DTerrainTracksFlush.cpp:264-269 (the tank-track twin of the scorch colour).
// INFERENCE: the role of RW 0x4E44FB (a per-instance vertex light for terrain props / trees; the caller shape is read, the buffer is not) and
//   RW 0x46F48E (renderOneObject, matched to the decomp by its light-array walk) are inferred; the scorch marks and the tank tracks are not
//   drawn by this port (stop S-033).

#pragma once

#include <string>
#include <vector>

struct GlobalLightingData;

namespace TerrainLightScale
{
// The detail inputs retail's readers look at.
struct Lod
{
	bool useTerrainNormalMap = true; // GlobalData + 0x49
	int terrainLOD = 2;              // manager + 0x1780 (0 / 1 / 2)
	int shaderLOD = 2;               // [RW 0xD9B058] (0 Low, 1 Medium, 2 High, 3 UltraHigh)
};

// What a map's scale does at one detail tier. The factors are exactly 1.0 where retail does not apply the scale.
struct Scales
{
	float raw[3] = { 1, 1, 1 };      // GlobalData + 0x944 / 0x948 / 0x94C
	int terrainMode = 2;             // FlatHeightMap vtable slot 147
	bool terrainScaled = false;      // terrainMode < 1
	bool objectsScaled = false;      // shaderLOD < 2
	float terrain[3] = { 1, 1, 1 };  // RW 0x4E2B3A, 0x4690C1, 0x4E46CA
	float objects[3] = { 1, 1, 1 };  // RW 0x46E009, 0x46F89E, 0x46FB58 (and RW 0x471429 at shaderLOD <= 1, the same set)
	float scorch[3] = { 1, 1, 1 };   // RW 0x4670E1: always the raw scale
};

// RW 0x4E0E40 + 0x4E0F5D: the value of FlatHeightMap vtable slot 147 for these inputs. Fails (-1, *error) for a negative TerrainLOD (retail
// would index its table out of bounds).
int terrainRenderingMode(const Lod &lod, std::string *error);

// The scale as the map stores it: its three floats for chunk version >= 8, else 1.0 (RW 0x4ACA92 / 0x4ACAD1).
void rawScale(const GlobalLightingData &lighting, float out[3]);

// Fails (with *error) when the detail inputs are outside what the static levels produce.
bool resolve(const GlobalLightingData &lighting, const Lod &lod, Scales &out, std::string *error);

// The lines this port reports for the scale (stop S-033).
std::vector<std::string> stopLines();
} // namespace TerrainLightScale
