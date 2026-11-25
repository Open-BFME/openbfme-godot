// OpenBFME: faithful rebuild of The Battle for Middle-earth II: Rise of the Witch-king 2.01.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// Lane LOCAL-38 (stop S-687): LightEnvironmentClass, the per render object light set, and the light part of
// RTS3DScene::renderOneObject that fills it. ZH ww3d2/lightenvironment.cpp; DONOR BFME2 lightenvironment.cpp (0x00140770 Add_Light).
// No Godot dependency (openbfme_core): the device asks it once per drawn render object.
//
// TARGET FACTS (RotWK game.dat, read with tools/mod/rw.py dis):
//   * Layout (0x228 bytes): +0 state byte, +4 LightCount, +8 ObjectCenter, +0x14 InputLights[4] of 0x54 bytes, +0x164 OutputAmbient,
//     +0x224 FillIntensity. One input light: +0 Direction, +0xC Ambient, +0x18 Diffuse, +0x24 DiffuseRejected, +0x25 point flag,
//     +0x28 centre, +0x34 inner radius, +0x38 outer radius, +0x3C ambient * intensity, +0x48 diffuse * intensity (the unattenuated copies).
//   * ctor RW 0x53EEF0: everything 0, FillIntensity (+0x224) 0 (RW 0x53F0F1); Reset RW 0x53F100: count 0, centre, OutputAmbient = ambient.
//   * Add_Light RW 0x540250: a light whose Diffuse (LightClass +0xE0, before the intensity) is below 0.05 in all three channels is
//     dropped (RW 0x540261, 0.05f at 0xBDD760); POINT (type 0) and SPOT (2) go through Init_From_Point_Or_Spot RW 0x53F640, DIRECTIONAL (1)
//     through RW 0x53ED30; with FillIntensity != 0 the diffuse is scaled by the intensity once more (RW 0x540358; never in RotWK: the
//     environment of renderOneObject keeps the ctor's 0); the ambient is added to OutputAmbient for EVERY light that passed the filter
//     (RW 0x540394), before the ranking; a rejected non-point light stops there (RW 0x5403D6); the others are inserted by
//     Contribution = Diffuse.Length2 (RW 0x5403ED), strictly greater moves the weaker ones back, at most 4 are kept (RW 0x54044B / 0x5404DB).
//   * Init_From_Point_Or_Spot RW 0x53F640: Direction = position - centre, normalized when the distance is > 0; with FAR_ATTENUATION
//     (LightClass +0xC8 bit 0) the attenuation is 1 - (dist - start) / (end - start) clamped to [0, 1] (RW 0x53F757 .. 0x53F790), a step
//     (0 past the start) when |end - start| < 1e-4 (0xBD19E0); without the flag it is 1. Ambient and Diffuse are the light's times its
//     intensity (+0xD0, RW 0x53F90C); the copies +0x3C / +0x48 keep them unattenuated (RW 0x53F9A3); DiffuseRejected = Diffuse.Length2 is
//     not above the int global RW 0xDD8E4C (RW 0x53F9E8: `(float)int`, 0 in the image and no caller of its setter RW 0x53F200), so only
//     a zero diffuse is rejected; then Ambient and Diffuse are multiplied by the attenuation (a rejected light folds its diffuse into
//     the ambient and zeroes it, RW 0x53FA5F).
//   * Init_From_Directional RW 0x53ED30: Direction = -transform Z, Ambient and Diffuse as stored (no intensity), not rejected.
//   * Pre_Render_Update RW 0x540500: the fill light (RW 0x53FAF0) only when FillIntensity != 0 (never here); OutputAmbient clamped to [0, 1].
//   * The SAS binder (RW 0x54F406) splits the four slots for the effects: DirectionalLight[i] = the i-th non-point slot (count RW 0x53F140,
//     index RW 0x53F170: byte +0x39 = the point flag), PointLight[i] = the i-th point slot (count RW 0x53F1A0, index RW 0x53F1D0) with
//     Position = +0x3C (centre, RW 0x54D1E8), Color = +0x5C (diffuse * intensity, unattenuated, RW 0x54D135) and Range = +0x4C (outer radius,
//     RW 0x54D26F); AmbientLight = OutputAmbient (RW 0x54CF11). Every colour is doubled while RW 0xD9B03C is set (W3DObjectLighting).
//     The retail effects declare PointLight[1] (normalmapped / defaultw3d / vegetation .fxo): only the FIRST point slot is drawn.
//   * RTS3DScene::renderOneObject RW 0x46F48E: Reset with the render object's bounding sphere centre (vslot +0x104, RW 0x46F4D8) and the
//     scene (or infantry) ambient (RW 0x46FB1F); the global lights (+0x130, or +0x150 for infantry, count +0x160), the scene's light list
//     (+0x94) and the enabled (+0x144) dynamic lights (+0x114) are added in that order (RW 0x46FBB7 / 0x46FC7E / 0x46FD19); a POINT light
//     (+0xC4 == 0) is added only when its bounding sphere (centre, FarAttenEnd: RW 0x530A25) intersects the object's: (r1 + r2)^2 > d^2
//     (Spheres_Intersect RW 0x46E93D); Pre_Render_Update follows (RW 0x46FD7C).
// INFERENCE: x87 parts of the attenuation (RW 0x53F71E: double loads) are computed in float here (presentation only, no hash).

#pragma once

#include "Libraries/WWVegas/WWMath/matrix3d.h"

#include <vector>

struct RenderObjPrototype;

// What Add_Light reads from a LightClass (W3DDynamicLight / the scene's global lights).
struct LightEnvSourceLight
{
	enum Type
	{
		POINT = 0,
		DIRECTIONAL = 1,
		SPOT = 2
	};
	Type type = POINT;
	Vector3 position;            // POINT / SPOT: the light's world position
	Vector3 direction;           // DIRECTIONAL: Init_From_Directional's Direction (= -transform Z, toward the light)
	Vector3 ambient;             // LightClass +0xD4
	Vector3 diffuse;             // LightClass +0xE0 (the black filter reads it before the intensity)
	float intensity = 1.0f;      // LightClass +0xD0
	bool farAttenuation = false; // LightClass +0xC8 bit 0 (FAR_ATTENUATION)
	float farAttenStart = 0.0f;  // +0x100
	float farAttenEnd = 0.0f;    // +0x104 (also the light's bounding sphere radius, RW 0x530A25)
	bool enabled = true;         // W3DDynamicLight +0x144 (renderOneObject skips a disabled dynamic light)
	// the port's pool keeps ambient / diffuse already times the intensity (intensity 1 here); the black filter then reads the raw +0xE0 from here
	Vector3 filterDiffuse;
	bool useFilterDiffuse = false;
	int id = -1;                 // the caller's tag (not retail: which source a slot came from)
};

class LightEnvironmentClass
{
public:
	static constexpr int MAX_LIGHTS = 4;

	struct InputLightStruct
	{
		Vector3 Direction;
		Vector3 Ambient;
		Vector3 Diffuse;
		bool DiffuseRejected = false;
		bool Point = false;
		Vector3 Center;
		float InnerRadius = 0.0f;
		float OuterRadius = 0.0f;
		Vector3 PointAmbient; // +0x3C: ambient * intensity, unattenuated
		Vector3 PointDiffuse; // +0x48: diffuse * intensity, unattenuated (the SAS PointLight Color)
		int Id = -1;
	};

	void Reset(const Vector3 &objectCenter, const Vector3 &ambient); // RW 0x53F100
	void Add_Light(const LightEnvSourceLight &light);                 // RW 0x540250
	void Pre_Render_Update();                                         // RW 0x540500 (FillIntensity 0: the clamp only)

	int Get_Light_Count() const { return m_count; }
	const InputLightStruct &Get_Input_Light(int i) const { return m_lights[i]; }
	const Vector3 &Get_Equivalent_Ambient() const { return m_outputAmbient; }

	// the SAS split (RW 0x53F140 / 0x53F170 / 0x53F1A0 / 0x53F1D0): counts, and the slot of the n-th directional / point light (-1: none)
	int Directional_Count() const;
	int Directional_Index(int n) const;
	int Point_Count() const;
	int Point_Index(int n) const;

	// Contribution as Add_Light computes it: the new light's (z*z + y*y) + x*x (RW 0x5403ED), a kept slot's (x*x + z*z) + y*y (RW 0x540410)
	static float New_Contribution(const Vector3 &diffuse);
	static float Slot_Contribution(const Vector3 &diffuse);

private:
	static void Init_Point_Or_Spot(InputLightStruct &out, const LightEnvSourceLight &light, const Vector3 &center); // RW 0x53F640
	static void Init_Directional(InputLightStruct &out, const LightEnvSourceLight &light);                          // RW 0x53ED30

	int m_count = 0;
	Vector3 m_center;
	InputLightStruct m_lights[MAX_LIGHTS];
	Vector3 m_outputAmbient;
};

// What one render object is drawn with: the retail effects' DirectionalLight / PointLight[0] / AmbientLight before the SAS doubling.
struct W3DObjectLightSet
{
	unsigned droppedSuns = 0; // bit i: the i-th global light did not make the four slots (its directional light is black for this object)
	bool hasPoint = false;    // PointLight[0] exists (the first point slot)
	Vector3 pointPosition;    // world (SAGE) position
	Vector3 pointColor;       // diffuse * intensity, unattenuated
	float pointRange = 0.0f;  // the outer radius (FarAttenEnd)
	int pointId = -1;         // the dynamic light's tag
	Vector3 ambient;          // OutputAmbient after Pre_Render_Update (clamped to [0, 1])
	int dynamicAdded = 0;     // dynamic lights that reached Add_Light (the sphere test passed)
	int pointSlots = 0;       // point lights among the four slots (only the first is drawn by the retail effects)
};

// RTS3DScene::renderOneObject's light part (RW 0x46F48E .. 0x46FD7C) for one render object with bounding sphere (centre, radius):
// the global lights (sun i -> bit i of droppedSuns; already scaled by the map's light scale, as RW 0x46FB8D does), then the dynamic lights
// in pool order. The scene's light list (+0x94) is not modelled: no RotWK map or object adds a light to it in the port (INFERENCE).
W3DObjectLightSet W3D_Collect_Object_Lights(const Vector3 &center, float radius, const Vector3 &sceneAmbient, const LightEnvSourceLight *suns, int sunCount,
	const LightEnvSourceLight *dynamicLights, int dynamicCount);

// Spheres_Intersect RW 0x46E93D: (r1 + r2)^2 > |c1 - c2|^2
bool W3D_Spheres_Intersect(const Vector3 &c1, float r1, const Vector3 &c2, float r2);

// The render object's bounding sphere in its own (HLOD) space for the current pose, the centre the light environment is Reset with
// (vslot +0x104, RW 0x46F4D8). DONOR BFME2 HLodUpdateObjSpaceBoundingVolumes.cpp (0x0019DB50) / hlod.cpp (Get_Obj_Space_Bounding_Box /
// _Sphere, 0x0019E... rows): an HLOD whose highest LOD has an OBBox sub object named "BOUNDINGBOX" (after the '.') takes that box, searched from
// the last sub object, moved by its bone's current transform as an axis-aligned box (Transform_Center_Extent_AABox): centre = M * Center,
// extent[i] = sum |M[i][j] * Extent[j]|, radius = |extent|. INFERENCE (not read in RW: the HLOD class's vslot +0x104 was not resolved): the
// box path is the donor's; without a box the meshes' header spheres, moved by their bones, are merged as SphereClass::Add_Sphere (ZH sphere.h).
// False when the prototype has neither.
bool W3D_Render_Object_Bounding_Sphere(const RenderObjPrototype &proto, const std::vector<Matrix3D> &pivots, Vector3 &center, float &radius);
