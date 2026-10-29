// OpenBFME. GPL-3.0.
//
// The planar water reflection's maths, without a Godot dependency (stop S-035). RotWK renders ONE reflection texture per frame: the scene seen through
// the camera mirrored in the plane z = ReflectionPlaneZ (the WaterTransparency setting, TheWaterTransparency + 0x4C; water.ini 59.0, maps override it),
// into a 512x512 render target, with everything below the plane clipped by an oblique near plane. The texture is shared by every standing water.
//
// TARGET (RotWK 2.01 game.dat, community-modified image, S-001):
//   RW 0x47EA2B  WaterRenderObjClass reflection camera setup (BFME2 Rva0007F0FB, GameEngineDevice/.../Water/W3DWaterRva0007F0FB.cpp): the source camera's
//                axes and position reflected in the plane (0, 0, 1, ReflectionPlaneZ), the reflected up negated so the matrix stays a rotation, the viewport
//                0..1, then the oblique clip projection and the camera's projection override.
//   RW 0x47F1AC  the pass (BFME2 W3DWater.cpp rva0007F87C): the render target, a black clear (alpha 0), the scene drawn through the mirror camera.
//   RW 0x47E1BD  the texture: Create_Render_Target(0x200, 0x200, format 0) stored in the water object at + 0xF8.
//   RW 0x47F695  IsReflectionVisible (BFME2 WaterRenderObjClass_IsReflectionVisible.cpp): the pass runs only when GlobalData + 0x5E (WaterLOD >= 2) and
//                ReflectionOn hold and some standing water (an FX shader loaded at level >= 2, shader + 0x1C) has its polygon inside the camera frustum.
//   RW 0x449E38  W3DDisplay::draw calls it ("UpdateWaterReflection", after the shadow map, before the main Begin_Render).
//   RW 0x47F952  the effect parameter "ReflectionTexture" callback: the rendered texture only with GlobalData + 0x5E, the water object and ReflectionOn,
//                else the black texture (BFME2 Rva0007EBFBWaterDraw.cpp:419).
//
// The water shaders sample it with the main view's clip coordinates: vs blob 19 oT2 = ((x + w) / 2, (y + w) / 2, w), ps blob 4 divides by w: u = (ndc.x + 1) / 2,
// v = (ndc.y + 1) / 2, no flip. The mirror camera's up is negated, so the texture holds the image upside down and that v lands on the right row.

#pragma once

#include "Libraries/WWVegas/WWMath/matrix3d.h"

#include <vector>

namespace WaterReflection
{
// RW 0x47E1D7 / 0x47E1DC: push 0x200 twice before Create_Render_Target.
constexpr int kTextureSize = 512;

// Part of the water setup the pass reads: TheWaterTransparency + 0x4C (ReflectionPlaneZ) and + 0x50 (ReflectionOn).
struct Setting
{
	bool reflectionOn = false;
	float planeZ = 0.0f;
};

// The mirror camera of RW 0x47EA2B (BFME2 Rva0007F0FB): `source` is the viewing camera's transform (columns right, up, back; translation), `planeZ` the
// plane z = planeZ with normal (0, 0, 1). Axes: v - 2 (v . n) n; position: p - 2 ((p . n) - planeZ) n; the reflected up is negated (Matrix3D(rRight,
// -rUp, rN, rPos)). False, and `out` untouched, when the source's first element is not finite (the function returns at its first test).
bool mirrorCameraTransform(const Matrix3D &source, float planeZ, Matrix3D &out);

// Row-major 4x4 in the Direct3D row-vector convention (v' = v * M), as D3DXMATRIX.
struct Mat4
{
	float m[4][4];
};

// The projection of RW 0x47EA2B's tail (Lengyel's oblique near plane, "Oblique View Frustum Depth Projection and Clipping"): `d3dView` and `d3dProjection`
// are the mirror camera's view (DX8Wrapper's render_state.view, whose transpose the code inverts) and projection (DeviceProjectionMatrix), both in the D3D layout. The plane (0, 0, 1, -planeZ) in view space replaces the near plane: the third column of the
// projection is replaced by the plane scaled so that its point on the far corner maps to clip z = 1. False when the projection's x or y scale is 0 or the
// view is singular.
bool obliqueClipProjection(const Mat4 &d3dView, const Mat4 &d3dProjection, float planeZ, Mat4 &out);
// D3DXMatrixInverse's general inverse (false when singular).
bool invert(const Mat4 &m, Mat4 &out);
// v * M (D3DXVec4Transform).
void transform(const float v[4], const Mat4 &m, float out[4]);

// One frustum plane: a point p is inside when n . p + d >= 0.
struct Plane
{
	float n[3];
	float d;
};
// The polygon clipped by the planes (Sutherland-Hodgman, as SimpleDynVecClass<Vector3>::rva001006f8 does against the camera frustum) keeps at least three
// vertices: RW 0x47F491 (BFME2 Rva0007FD89Entry::rva0007FB85).
bool polygonInFrustum(const std::vector<Vector3> &polygon, const std::vector<Plane> &frustum);

// The reflection texture coordinate a water pixel samples: the main view's normalized device coordinates (x, y) mapped (x + 1) / 2, (y + 1) / 2 (vs blob 19
// oT2, ps blob 4 `rcp r1.w, t2.z; mul r1.xy, r1.w, t2`), plus ReflectionPerturbation * the scaled bump normal's xy.
void reflectionUv(float ndcX, float ndcY, float perturbation, const float scaledNormalXY[2], float out[2]);
} // namespace WaterReflection
