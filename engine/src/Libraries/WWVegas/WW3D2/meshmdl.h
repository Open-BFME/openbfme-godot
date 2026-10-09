// OpenBFME: faithful rebuild of The Battle for Middle-earth II: Rise of the Witch-king 2.01.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// Data side of ZH WW3D2 MeshModelClass (meshmdl.h) as filled by meshmdlio.cpp. ZH builds
// renderer-ready material/pass objects while loading; this port keeps the raw per-chunk
// arrays so the device layer can build Godot meshes and tests can compare counts with the
// file. Pass/stage data is kept for every pass so later work can reproduce WW3D's multipass
// shading exactly.

#pragma once

#include "Libraries/WWVegas/WW3D2/w3d_file.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

class ChunkLoadClass;

struct MeshTextureStageData
{
	std::vector<uint32> TextureIds;             // one (applies to all tris) or one per tri
	std::vector<W3dTexCoordStruct> TexCoords;   // per vertex
};

struct MeshMaterialPassData
{
	std::vector<uint32> VertexMaterialIds;      // one or per vertex
	std::vector<uint32> ShaderIds;              // one or per tri
	std::vector<W3dRGBAStruct> DCG;             // per-vertex diffuse colour
	std::vector<MeshTextureStageData> Stages;
	std::vector<uint32> ShaderMaterialIds;      // BFME2 W3D_CHUNK_SHADER_MATERIAL_ID: one, or one per triangle
	std::vector<W3dTexCoordStruct> TexCoords;   // BFME2: W3D_CHUNK_STAGE_TEXCOORDS directly inside the pass
};

// One BFME2 FX shader material property (W3D_CHUNK_SHADER_MATERIAL_PROPERTY): u32 type, u32 name length
// (including the NUL), name, then a value by type (W3DSHADERMATERIAL_PROPERTY_*).
struct MeshShaderMaterialProperty
{
	uint32 Type = 0;
	std::string Name;
	std::string StringValue; // texture properties
	float Float[4] = { 0, 0, 0, 0 }; // float / vec2 / vec3 / vec4
	sint32 Int = 0;
	bool Bool = false;
};

struct MeshShaderMaterialDef
{
	uint8 Number = 0;
	std::string TypeName; // "normalmapped.fx", ...
	std::vector<MeshShaderMaterialProperty> Properties;
};

// Triangles of one draw surface: stage 0 texture id and shader id of pass 0 (-1 = none).
struct MeshSurfaceKey
{
	std::int64_t Texture = -1;
	std::int64_t Shader = -1;
	bool operator<(const MeshSurfaceKey &o) const { return Texture != o.Texture ? Texture < o.Texture : Shader < o.Shader; }
};

struct MeshTextureDef
{
	std::string Name;
	bool HasInfo = false;
	W3dTextureInfoStruct Info = {};
};

struct MeshVertexMaterialDef
{
	std::string Name;
	W3dVertexMaterialStruct Info = {};
	std::string MapperArgs0;
	std::string MapperArgs1;
};

class MeshModelClass
{
public:
	// Called with the W3D_CHUNK_MESH chunk open (ZH MeshModelClass::Load_W3D).
	bool Load_W3D(ChunkLoadClass &cload, std::string *error);

	// "CONTAINER.MESH" like ZH's render-object name, or just MESH without a container.
	std::string Get_Name() const;
	bool Is_Skin() const { return (Header.Attributes & W3D_MESH_FLAG_GEOMETRY_TYPE_MASK) == W3D_MESH_FLAG_GEOMETRY_TYPE_SKIN; }
	bool Is_Hidden() const { return (Header.Attributes & W3D_MESH_FLAG_HIDDEN) != 0; }
	bool Is_Two_Sided() const { return (Header.Attributes & W3D_MESH_FLAG_TWO_SIDED) != 0; }

	W3dMeshHeader3Struct Header = {};
	std::string MeshName;
	std::string ContainerName;
	std::string UserText;
	std::vector<W3dVectorStruct> Vertices;
	std::vector<W3dVectorStruct> Normals;
	std::vector<W3dTriStruct> Triangles;
	std::vector<W3dVertInfStruct> Influences;
	std::vector<uint32> ShadeIndices;
	W3dMaterialInfoStruct MaterialInfo = {};
	std::vector<W3dShaderStruct> Shaders;
	std::vector<MeshVertexMaterialDef> VertexMaterials;
	std::vector<MeshTextureDef> Textures;
	std::vector<MeshMaterialPassData> Passes;

	// BFME streams (not in ZH). Vertices2 / Normals2 are the dual-bone skin's second-bone space streams.
	std::vector<W3dVectorStruct> Vertices2;
	std::vector<W3dVectorStruct> Normals2;
	std::vector<W3dVectorStruct> Tangents;
	std::vector<W3dVectorStruct> Bitangents;
	std::vector<MeshShaderMaterialDef> ShaderMaterials;

	// Collision / picking tree (ZH W3D_CHUNK_AABTREE); not used for drawing.
	bool HasAABTree = false;
	W3dMeshAABTreeHeader AABTreeHeader = {};
	std::vector<uint32> AABTreePolyIndices;
	std::vector<W3dMeshAABTreeNode> AABTreeNodes;

	std::vector<uint32> UnhandledChunks; // ids read past without interpretation (reported, not hidden)

	// ZH meshmdlio.cpp:1182 assigns shaders per polygon. Groups the triangles of pass 0 by (stage 0 texture id,
	// shader id): a pass carries one id for every triangle or one per triangle; any other count, or an id
	// outside its table, is an error. No pass: one group with texture -1 and shader -1.
	bool Group_Triangles(std::map<MeshSurfaceKey, std::vector<uint32>> &out, std::string *error) const;

private:
	bool read_chunks(ChunkLoadClass &cload, std::string *error);
	bool read_vertex_materials(ChunkLoadClass &cload, std::string *error);
	bool read_textures(ChunkLoadClass &cload, std::string *error);
	bool read_material_pass(ChunkLoadClass &cload, std::string *error);
	bool read_texture_stage(ChunkLoadClass &cload, MeshTextureStageData &stage, std::string *error);
	bool read_shader_materials(ChunkLoadClass &cload, std::string *error);
	bool read_aabtree(ChunkLoadClass &cload, std::string *error);
};
