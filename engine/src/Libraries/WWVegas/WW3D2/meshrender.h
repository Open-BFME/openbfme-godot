// OpenBFME: faithful rebuild of The Battle for Middle-earth II: Rise of the Witch-king 2.01.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// MeshModelClass -> renderer-ready draw data: the part of ZH's meshmdlio.cpp / dx8renderer.cpp that decides what is
// drawn with which state, without any Godot types so it can be unit tested against the file.
//
// What it does (spec w3d-and-draw.md 3.1, 3.2, 3.4, 3.5):
//  * per-vertex streams: positions and normals (bone space for skins), the BFME second-bone position / normal streams,
//    bone indices and weights, tangents and bitangents, per pass / stage UVs (V flipped on load, ZH meshmdlio.cpp:1513)
//    and the DCG vertex colours;
//  * one draw surface per (pass, shader, vertex material, stage textures, FX material): ZH dx8renderer.cpp:959-973 takes a
//    polygon's vertex material from its FIRST vertex (Polygon_Array[idx][0]) and its shader / textures per polygon;
//  * per surface: the shader's render state (shader.h), texture names and info flags, the vertex material, and the mapper
//    type and argument text of both stages (validated by building a mapper now, so a bad argument is an error here).
// Meshes with BFME2 FX shader materials (chunks 0x50-0x53) are marked MATERIAL_FX; their property sets are carried over
// untouched. The per-pass choice between classic and FX materials is [U] in the spec: a pass with FX material ids is FX.
//
// Skin deformation (Deform_Vertex) is the reference CPU form of what the GPU does; the unit tests pin it.
// Target / donor / unknown: the dual-bone formula p = (w0/100) M[b0] p0 + (w1/100) M[b1] p1 with p1 from W3D_CHUNK_VERTICES_2
// is the spec's reading of the file (OpenSAGE cross-check); the BFME2 binary's blend code was not located (spec 3.1 [U]).
// Vertices whose weights are both 0 (1,653 retail vertices in 15 meshes) are given w0 = 100, which is OpenSAGE's choice,
// not a retail fact: they are counted in ZeroWeightVertices so every consumer can report them.

#pragma once

#include "Libraries/WWVegas/WW3D2/htree.h"
#include "Libraries/WWVegas/WW3D2/meshmdl.h"
#include "Libraries/WWVegas/WW3D2/shader.h"

#include <cstdint>
#include <string>
#include <vector>

enum MeshCameraMode
{
	MESH_CAMERA_NONE = 0,
	MESH_CAMERA_ALIGNED = 1,  // world = Obj_Look_At(pos, pos + cameraZ, 0), WW dx8renderer.cpp:1818-1847
	MESH_CAMERA_ORIENTED = 2, // world = Obj_Look_At(pos, cameraPos, 0)
};

enum MeshMaterialKind
{
	MATERIAL_CLASSIC,
	MATERIAL_FX,
};

struct MeshStageBinding
{
	bool HasTexture = false;
	std::string TextureName;   // as the W3D file spells it
	bool HasTextureInfo = false;
	W3dTextureInfoStruct TextureInfo = {};
	int MapperType = 0;        // W3DMapperType value (Attributes >> 16 or >> 8 & 0xFF)
	std::string MapperArgs;
};

struct MeshDrawSurface
{
	int Pass = 0;
	MeshMaterialKind Kind = MATERIAL_CLASSIC;
	std::vector<std::uint32_t> Triangles; // indices into MeshModelClass::Triangles
	std::vector<std::uint32_t> Indices;   // vertex indices, 3 per triangle in file order (counter-clockwise)

	// classic
	W3dShaderStruct Shader = {};
	W3DRenderState State;
	int VertexMaterialId = -1;            // -1: the pass has none
	W3dVertexMaterialStruct VertexMaterial = {};
	bool HasVertexMaterial = false;
	MeshStageBinding Stage[2];
	bool HasDCG = false;                  // the pass carries per-vertex diffuse colours
	int StageUVStream[2] = { -1, -1 };    // index into MeshRenderData::PassUV, -1: none

	// FX
	int ShaderMaterialId = -1;            // index into MeshModelClass::ShaderMaterials

	int SortLevel = 0;                    // mesh SortLevel; 0 with a blended pass-0 shader = dynamically sorted
	bool Sorted = false;                  // blended (ZH SORT flag or SortLevel > 0)
};

// One stage's texture coordinates of one pass: UV already flipped (u, 1 - v).
struct MeshUVStream
{
	int Pass = 0;
	int Stage = 0;
	std::vector<float> UV; // 2 per vertex
};

struct MeshRenderData
{
	std::string Name;
	std::uint32_t Attributes = 0;
	bool Skin = false;
	bool DualBone = false;   // the second-bone streams are present
	bool TwoSided = false;
	bool Hidden = false;
	bool CastShadow = false;
	MeshCameraMode Camera = MESH_CAMERA_NONE;
	int SortLevel = 0;

	size_t NumVertices = 0;
	size_t NumTriangles = 0;
	float BoundsMin[3] = { 0, 0, 0 };
	float BoundsMax[3] = { 0, 0, 0 };

	std::vector<float> Position;  // 3 per vertex; bone space for skins
	std::vector<float> Normal;
	std::vector<float> Position1; // second-bone space streams (empty unless DualBone)
	std::vector<float> Normal1;
	std::vector<float> Tangent;   // 3 per vertex (empty when the mesh has none)
	std::vector<float> Bitangent;
	// skin only. Weights are fractions (percent / 100); both zero in the file means w0 = 1 (see header).
	std::vector<std::uint16_t> Bone0, Bone1;
	std::vector<float> Weight0, Weight1;
	size_t ZeroWeightVertices = 0;

	std::vector<MeshUVStream> PassUV;
	std::vector<std::vector<std::uint8_t>> PassDCG; // per pass: 4 bytes (R, G, B, A) per vertex, empty when none

	std::vector<MeshDrawSurface> Surfaces;
	std::vector<std::string> Warnings; // faithful but unusual: reported, never hidden
	std::uint32_t TotalSurfaceTriangles() const;
};

// False + *error when the mesh cannot be drawn faithfully: an id outside its table, an array whose length is neither 1 nor
// the triangle / vertex count, a stage texture without texture coordinates, or a mapper whose arguments are rejected.
bool Build_Mesh_Render_Data(const MeshModelClass &mesh, MeshRenderData &out, std::string *error);

// CPU reference of the skinning the vertex shader performs: M[b0] p0 w0 + M[b1] p1 w1 (rigid meshes: M[bone] p).
// pose.Transform holds the world matrices of the hierarchy's pivots.
// Skins use their own bone indices; a rigid mesh uses rigidBone, the pivot of the HLOD sub-object that carries it.
// Throws std::out_of_range for a bone outside the pose.
Vector3 Deform_Position(const MeshRenderData &mesh, size_t vertex, const HTreePose &pose, int rigidBone = -1);
Vector3 Deform_Normal(const MeshRenderData &mesh, size_t vertex, const HTreePose &pose, int rigidBone = -1);
