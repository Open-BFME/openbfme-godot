// OpenBFME. GPL-3.0.
//
// W3DObjectLighting: the light environment the retail effects light objects with (lane RENDER-1, stops S-023 / S-390).
// Client / draw only: never read by the logic.
//
// TARGET (RotWK game.dat, read statically):
//   * The SAS binder (RW 0x54F406) maps the effect bindings "AmbientLight" / "DirectionalLight" / "NumDirectionalLights" to
//     callbacks that read the current light environment (global RW 0xDD3454): the ambient colour at +0x164 (RW 0x54CF11), the
//     directional lights as records of 0x54 bytes (colour at +0x2C RW 0x54CFD0, direction at +0x14 RW 0x54D083, count RW 0x53F140).
//     Lights past the count are sent as black (colour 0) with the direction (0, 0, 1).
//   * Both colours are multiplied by 2.0 (RW 0xBD889C) while the byte RW 0xD9B03C is set. The map's GlobalLighting reader sets
//     that byte to (terrainLightingMultiplier > 1.0) for chunk version >= 5 and clears it below (RW 0x4AC97F .. 0x4AC9A5); the
//     retail corpus stores 2.0 (1.0 in 5 of 181 maps), so retail objects almost always receive DOUBLE the map's light colours.
//   * normalmapped.fxo (8,481 of the 8,609 FX surfaces) and defaultw3d.fxo (every classic W3D material) consume them as
//     decoded by tools/render/d3d9_disasm.py (see W3DLitShading below for the formulas).
// DONOR (ZH W3DDisplay::setTimeOfDay, LightEnvironmentClass, RTS3DScene::renderOneObject):
//   * the scene ambient is TerrainObjectsLighting[tod][0].ambient, the global lights carry ambient 0 and the diffuse of
//     TerrainObjectsLighting[tod][0..2]; their transform's Z axis is lightPos and the light direction is -Z (the vector toward
//     the light is -lightPos); LightEnvironmentClass::Add_Light drops a light whose diffuse is below 0.05 in all channels and
//     Pre_Render_Update clamps the summed ambient to [0, 1].
// INFERENCE (stop S-390): RotWK's light environment is filled like ZH's from the map's objects lights; infantry (KINDOF_INFANTRY)
//   drawables use the map's infantry light set the same way (ZH lights infantry with its own light array, BFME2 maps carry a
//   separate infantry set). Neither the environment's fill code in RotWK nor the per-drawable choice were read.

#pragma once

#include <string>
#include <vector>

struct GlobalLightingData;

// One light set as the effects receive it.
struct W3DLightSet
{
	float ambient[3] = { 0, 0, 0 };           // Sas.AmbientLight[0].Color (already scaled)
	float color[3][3] = {};                   // Sas.DirectionalLight[i].Color (already scaled; 0 = no light)
	float toLight[3][3] = { { 0, 0, 1 }, { 0, 0, 1 }, { 0, 0, 1 } }; // Sas.DirectionalLight[i].Direction, SAGE world, toward the light
	int count = 0;                            // accepted lights (NumDirectionalLights)
};

struct W3DObjectLighting
{
	int timeOfDay = 0;        // 1 Morning .. 4 Night
	float colorScale = 1.0f;  // 2.0 when the map's terrainLightingMultiplier > 1 (RW 0x4AC97F), else 1.0
	W3DLightSet objects;      // everything but infantry
	W3DLightSet infantry;     // KINDOF_INFANTRY drawables (inference S-390)
};

namespace W3DObjectLightingUtil
{
// The light sets of the map's current time of day. Fails (with *error) when the time of day is not 1..4.
bool fromMap(const GlobalLightingData &lighting, W3DObjectLighting &out, std::string *error);

// The lines this lighting reports (stop S-390 and the facts above), for the model / world reports.
std::vector<std::string> stopLines();
}
