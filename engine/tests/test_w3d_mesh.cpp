// OpenBFME unit tests: mesh chunks (ZH + BFME additions) and per-triangle surface grouping. GPL-3.0.
// Expected values are written down from the format (w3d_file.h layouts) and chosen by hand.

#include "doctest.h"
#include "W3dTestUtil.h"

#include "Libraries/WWVegas/WW3D2/meshmdl.h"
#include "Libraries/WWVegas/WW3D2/w3d_file.h"
#include "Libraries/WWVegas/WWLib/chunkio.h"

using namespace w3dtest;

namespace
{

struct MeshSpec
{
	std::uint32_t Verts = 4;
	std::uint32_t Tris = 4;
	std::uint32_t Attributes = 0;
	std::uint32_t VertexChannels = 3;
	std::vector<W3dShaderStruct> Shaders;
	std::vector<std::string> Textures;
	std::vector<std::uint32_t> ShaderIds;  // pass 0
	std::vector<std::uint32_t> TextureIds; // pass 0 stage 0
	bool Pass = true;
	std::vector<W3dVertInfStruct> Influences;
	std::vector<W3dVectorStruct> Vertices2, Normals2, Tangents, Bitangents;
	std::vector<std::uint32_t> ShaderMaterialIds;
	ChunkWriter ExtraChildren; // appended last
};

std::vector<std::uint8_t> meshBytes(const MeshSpec &m)
{
	ChunkWriter mesh;
	W3dMeshHeader3Struct mh = {};
	mh.Version = W3D_MAKE_VERSION(5, 0);
	mh.Attributes = m.Attributes;
	setName(mh.MeshName, sizeof(mh.MeshName), "M");
	setName(mh.ContainerName, sizeof(mh.ContainerName), "C");
	mh.NumTris = m.Tris;
	mh.NumVertices = m.Verts;
	mh.VertexChannels = m.VertexChannels;
	mesh.chunk(W3D_CHUNK_MESH_HEADER3, ChunkWriter::of(mh));
	std::vector<W3dVectorStruct> verts(m.Verts, W3dVectorStruct{ 0, 0, 0 });
	mesh.chunk(W3D_CHUNK_VERTICES, ChunkWriter::ofArray(verts));
	mesh.chunk(W3D_CHUNK_VERTEX_NORMALS, ChunkWriter::ofArray(std::vector<W3dVectorStruct>(m.Verts, W3dVectorStruct{ 0, 0, 1 })));
	std::vector<W3dTriStruct> tris(m.Tris);
	for (std::uint32_t t = 0; t < m.Tris; ++t)
	{
		tris[t] = {};
		tris[t].Vindex[0] = t % m.Verts;
		tris[t].Vindex[1] = (t + 1) % m.Verts;
		tris[t].Vindex[2] = (t + 2) % m.Verts;
	}
	mesh.chunk(W3D_CHUNK_TRIANGLES, ChunkWriter::ofArray(tris));
	if (!m.Influences.empty())
	{
		mesh.chunk(W3D_CHUNK_VERTEX_INFLUENCES, ChunkWriter::ofArray(m.Influences));
	}
	if (!m.Vertices2.empty())
	{
		mesh.chunk(W3D_CHUNK_VERTICES_2, ChunkWriter::ofArray(m.Vertices2));
	}
	if (!m.Normals2.empty())
	{
		mesh.chunk(W3D_CHUNK_VERTEX_NORMALS_2, ChunkWriter::ofArray(m.Normals2));
	}
	if (!m.Tangents.empty())
	{
		mesh.chunk(W3D_CHUNK_TANGENTS, ChunkWriter::ofArray(m.Tangents));
	}
	if (!m.Bitangents.empty())
	{
		mesh.chunk(W3D_CHUNK_BITANGENTS, ChunkWriter::ofArray(m.Bitangents));
	}
	if (!m.Shaders.empty())
	{
		mesh.chunk(W3D_CHUNK_SHADERS, ChunkWriter::ofArray(m.Shaders));
	}
	if (!m.Textures.empty())
	{
		ChunkWriter textures;
		for (const std::string &name : m.Textures)
		{
			ChunkWriter nameChunk;
			std::vector<std::uint8_t> body(name.begin(), name.end());
			body.push_back(0);
			nameChunk.chunk(W3D_CHUNK_TEXTURE_NAME, body);
			textures.wrapper(W3D_CHUNK_TEXTURE, nameChunk);
		}
		mesh.wrapper(W3D_CHUNK_TEXTURES, textures);
	}
	if (m.Pass)
	{
		ChunkWriter pass;
		if (!m.ShaderIds.empty())
		{
			pass.chunk(W3D_CHUNK_SHADER_IDS, ChunkWriter::ofArray(m.ShaderIds));
		}
		if (!m.ShaderMaterialIds.empty())
		{
			pass.chunk(W3D_CHUNK_SHADER_MATERIAL_ID, ChunkWriter::ofArray(m.ShaderMaterialIds));
		}
		if (!m.TextureIds.empty())
		{
			ChunkWriter stage;
			stage.chunk(W3D_CHUNK_TEXTURE_IDS, ChunkWriter::ofArray(m.TextureIds));
			pass.wrapper(W3D_CHUNK_TEXTURE_STAGE, stage);
		}
		mesh.wrapper(W3D_CHUNK_MATERIAL_PASS, pass);
	}
	mesh.bytes.insert(mesh.bytes.end(), m.ExtraChildren.bytes.begin(), m.ExtraChildren.bytes.end());
	ChunkWriter file;
	file.wrapper(W3D_CHUNK_MESH, mesh);
	return file.bytes;
}

bool loadMesh(MeshModelClass &mesh, const std::vector<std::uint8_t> &bytes, std::string &error)
{
	ChunkLoadClass cload(bytes.data(), bytes.size());
	REQUIRE(cload.Open_Chunk());
	return mesh.Load_W3D(cload, &error);
}

W3dShaderStruct shaderWithDest(std::uint8_t dest)
{
	W3dShaderStruct s = {};
	s.DestBlend = dest;
	s.SrcBlend = 1;
	return s;
}

typedef std::map<MeshSurfaceKey, std::vector<std::uint32_t>> Groups;

MeshSurfaceKey key(std::int64_t tex, std::int64_t shader)
{
	MeshSurfaceKey k;
	k.Texture = tex;
	k.Shader = shader;
	return k;
}

} // namespace

TEST_CASE("per-triangle shader ids split a mesh into one surface per (texture, shader), never the first shader for all")
{
	// Sol scaffold finding 7: the builder used ShaderIds[0] for every triangle. ZH meshmdlio.cpp:1182 assigns
	// shaders per polygon. Four triangles alternate shaders 0, 1, 0, 1 under one texture.
	MeshSpec spec;
	spec.Shaders = { shaderWithDest(0), shaderWithDest(1) };
	spec.Textures = { "A.tga", "B.tga" };
	spec.ShaderIds = { 0, 1, 0, 1 };
	spec.TextureIds = { 0 };
	MeshModelClass mesh;
	std::string error;
	REQUIRE_MESSAGE(loadMesh(mesh, meshBytes(spec), error), error);
	Groups groups;
	REQUIRE_MESSAGE(mesh.Group_Triangles(groups, &error), error);
	REQUIRE(groups.size() == 2);
	CHECK(groups[key(0, 0)] == std::vector<std::uint32_t>{ 0, 2 });
	CHECK(groups[key(0, 1)] == std::vector<std::uint32_t>{ 1, 3 });
}

TEST_CASE("surface grouping: per-triangle textures with one shader, and both per triangle")
{
	std::string error;
	Groups groups;
	{
		MeshSpec spec;
		spec.Shaders = { shaderWithDest(0), shaderWithDest(1) };
		spec.Textures = { "A.tga", "B.tga" };
		spec.ShaderIds = { 1 };
		spec.TextureIds = { 0, 0, 1, 1 };
		MeshModelClass mesh;
		REQUIRE_MESSAGE(loadMesh(mesh, meshBytes(spec), error), error);
		REQUIRE_MESSAGE(mesh.Group_Triangles(groups, &error), error);
		REQUIRE(groups.size() == 2);
		CHECK(groups[key(0, 1)] == std::vector<std::uint32_t>{ 0, 1 });
		CHECK(groups[key(1, 1)] == std::vector<std::uint32_t>{ 2, 3 });
	}
	{
		MeshSpec spec;
		spec.Shaders = { shaderWithDest(0), shaderWithDest(1) };
		spec.Textures = { "A.tga", "B.tga" };
		spec.ShaderIds = { 0, 0, 1, 1 };
		spec.TextureIds = { 0, 1, 0, 1 };
		MeshModelClass mesh;
		REQUIRE_MESSAGE(loadMesh(mesh, meshBytes(spec), error), error);
		REQUIRE_MESSAGE(mesh.Group_Triangles(groups, &error), error);
		REQUIRE(groups.size() == 4);
		CHECK(groups[key(0, 0)] == std::vector<std::uint32_t>{ 0 });
		CHECK(groups[key(1, 0)] == std::vector<std::uint32_t>{ 1 });
		CHECK(groups[key(0, 1)] == std::vector<std::uint32_t>{ 2 });
		CHECK(groups[key(1, 1)] == std::vector<std::uint32_t>{ 3 });
	}
	{
		// no pass at all: one surface with neither texture nor shader
		MeshSpec spec;
		spec.Pass = false;
		MeshModelClass mesh;
		REQUIRE_MESSAGE(loadMesh(mesh, meshBytes(spec), error), error);
		REQUIRE_MESSAGE(mesh.Group_Triangles(groups, &error), error);
		REQUIRE(groups.size() == 1);
		CHECK(groups[key(-1, -1)] == std::vector<std::uint32_t>{ 0, 1, 2, 3 });
	}
}

TEST_CASE("texture id 0xFFFFFFFF means no texture, as a single id and per triangle (ZH read_texture_ids; 2 retail meshes)")
{
	std::string error;
	Groups groups;
	{
		MeshSpec spec;
		spec.Shaders = { shaderWithDest(0) };
		spec.Textures = { "A.tga" };
		spec.ShaderIds = { 0 };
		spec.TextureIds = { 0xFFFFFFFFu };
		MeshModelClass mesh;
		REQUIRE_MESSAGE(loadMesh(mesh, meshBytes(spec), error), error);
		REQUIRE_MESSAGE(mesh.Group_Triangles(groups, &error), error);
		REQUIRE(groups.size() == 1);
		CHECK(groups[key(-1, 0)] == std::vector<std::uint32_t>{ 0, 1, 2, 3 });
	}
	{
		MeshSpec spec;
		spec.Shaders = { shaderWithDest(0) };
		spec.Textures = { "A.tga" };
		spec.ShaderIds = { 0 };
		spec.TextureIds = { 0, 0xFFFFFFFFu, 0, 0xFFFFFFFFu };
		MeshModelClass mesh;
		REQUIRE_MESSAGE(loadMesh(mesh, meshBytes(spec), error), error);
		REQUIRE_MESSAGE(mesh.Group_Triangles(groups, &error), error);
		REQUIRE(groups.size() == 2);
		CHECK(groups[key(0, 0)] == std::vector<std::uint32_t>{ 0, 2 });
		CHECK(groups[key(-1, 0)] == std::vector<std::uint32_t>{ 1, 3 });
	}
}

TEST_CASE("surface grouping refuses ids it cannot resolve instead of guessing")
{
	std::string error;
	Groups groups;
	{
		MeshSpec spec; // 3 shader ids for 4 triangles
		spec.Shaders = { shaderWithDest(0), shaderWithDest(1) };
		spec.ShaderIds = { 0, 1, 0 };
		MeshModelClass mesh;
		REQUIRE_MESSAGE(loadMesh(mesh, meshBytes(spec), error), error);
		CHECK_FALSE(mesh.Group_Triangles(groups, &error));
		CHECK(error.find("3 shader ids for 4 triangles") != std::string::npos);
	}
	{
		MeshSpec spec; // shader id 5 with a table of 2
		spec.Shaders = { shaderWithDest(0), shaderWithDest(1) };
		spec.ShaderIds = { 5 };
		MeshModelClass mesh;
		REQUIRE_MESSAGE(loadMesh(mesh, meshBytes(spec), error), error);
		CHECK_FALSE(mesh.Group_Triangles(groups, &error));
		CHECK(error.find("shader id 5 is outside its table of 2") != std::string::npos);
	}
	{
		MeshSpec spec; // texture id 2 with a table of 1
		spec.Shaders = { shaderWithDest(0) };
		spec.Textures = { "A.tga" };
		spec.ShaderIds = { 0 };
		spec.TextureIds = { 2 };
		MeshModelClass mesh;
		REQUIRE_MESSAGE(loadMesh(mesh, meshBytes(spec), error), error);
		CHECK_FALSE(mesh.Group_Triangles(groups, &error));
		CHECK(error.find("texture id 2 is outside its table of 1") != std::string::npos);
	}
}

TEST_CASE("BFME skin streams: dual-bone influences, second-bone vertices and normals, tangents")
{
	MeshSpec spec;
	spec.Verts = 3;
	spec.Tris = 1;
	spec.Attributes = W3D_MESH_FLAG_GEOMETRY_TYPE_SKIN;
	spec.VertexChannels = 0x63;
	spec.Influences = { { 1, 2, 70, 30 }, { 3, 0, 100, 0 }, { 0, 0, 0, 0 } };
	spec.Vertices2 = { { 1, 2, 3 }, { 4, 5, 6 }, { 7, 8, 9 } };
	spec.Normals2 = { { 0, 0, 1 }, { 0, 1, 0 }, { 1, 0, 0 } };
	spec.Tangents = { { 1, 0, 0 }, { 1, 0, 0 }, { 1, 0, 0 } };
	spec.Bitangents = { { 0, 1, 0 }, { 0, 1, 0 }, { 0, 1, 0 } };
	MeshModelClass mesh;
	std::string error;
	REQUIRE_MESSAGE(loadMesh(mesh, meshBytes(spec), error), error);
	REQUIRE(mesh.Influences.size() == 3);
	CHECK(mesh.Influences[0].BoneIdx == 1);
	CHECK(mesh.Influences[0].Bone1Idx == 2);
	CHECK(mesh.Influences[0].Weight0 == 70);
	CHECK(mesh.Influences[0].Weight1 == 30);
	CHECK(mesh.Influences[1].BoneIdx == 3);
	CHECK(mesh.Influences[2].Weight0 == 0); // the 0/0 vertices of 15 retail meshes are kept as stored
	CHECK(mesh.Influences[2].Weight1 == 0);
	REQUIRE(mesh.Vertices2.size() == 3);
	CHECK(mesh.Vertices2[1].Y == 5.0f);
	CHECK(mesh.Normals2[2].X == 1.0f);
	CHECK(mesh.Tangents[0].X == 1.0f);
	CHECK(mesh.Bitangents[2].Y == 1.0f);
	CHECK(mesh.UnhandledChunks.empty());
	CHECK(mesh.Is_Skin());
}

TEST_CASE("BFME streams whose length disagrees with the vertex count, or come unpaired, are refused")
{
	std::string error;
	{
		MeshSpec spec;
		spec.Verts = 3;
		spec.Tris = 1;
		spec.Influences = { { 0, 0, 100, 0 }, { 0, 0, 100, 0 } }; // 2 influences for 3 vertices
		MeshModelClass mesh;
		CHECK_FALSE(loadMesh(mesh, meshBytes(spec), error));
		CHECK(error.find("W3D_CHUNK_VERTEX_INFLUENCES holds 2 entries for 3 vertices") != std::string::npos);
	}
	{
		MeshSpec spec;
		spec.Verts = 3;
		spec.Tris = 1;
		spec.Vertices2 = { { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 } }; // no second normal stream
		MeshModelClass mesh;
		CHECK_FALSE(loadMesh(mesh, meshBytes(spec), error));
		CHECK(error.find("differ in length") != std::string::npos);
	}
	{
		MeshSpec spec;
		spec.Verts = 3;
		spec.Tris = 1;
		spec.Tangents = { { 1, 0, 0 }, { 1, 0, 0 }, { 1, 0, 0 } }; // no bitangents
		MeshModelClass mesh;
		CHECK_FALSE(loadMesh(mesh, meshBytes(spec), error));
		CHECK(error.find("W3D_CHUNK_TANGENTS and W3D_CHUNK_BITANGENTS differ") != std::string::npos);
	}
}

namespace
{

Bytes property(std::uint32_t type, const char *name, const std::vector<std::uint8_t> &value)
{
	Bytes b;
	b.u32(type).u32((std::uint32_t)std::strlen(name) + 1);
	for (const char *p = name; *p; ++p)
	{
		b.u8((std::uint8_t)*p);
	}
	b.u8(0);
	b.v.insert(b.v.end(), value.begin(), value.end());
	return b;
}

} // namespace

TEST_CASE("BFME FX shader materials: header and every property type")
{
	// header: u8 number, char[32] type name, u32 reserved  (37 bytes)
	Bytes header;
	header.u8(3);
	header.str("normalmapped.fx", 32);
	header.u32(0);
	REQUIRE(header.v.size() == 37);

	Bytes texValue;
	texValue.u32(6).u8('a').u8('.').u8('d').u8('d').u8('s').u8(0); // length includes the NUL
	Bytes f1, f2, f3, f4, i1, b1;
	f1.f32(0.5f);
	f2.f32(1.0f).f32(2.0f);
	f3.f32(1.0f).f32(2.0f).f32(3.0f);
	f4.f32(0.25f).f32(0.5f).f32(0.75f).f32(1.0f);
	i1.u32((std::uint32_t)-7);
	b1.u8(1); // a bool is one byte: retail 0x53 chunks for bools are name + 12 + 1 bytes

	ChunkWriter material;
	material.chunk(W3D_CHUNK_SHADER_MATERIAL_HEADER, header.v);
	material.chunk(W3D_CHUNK_SHADER_MATERIAL_PROPERTY, property(W3DSHADERMATERIAL_PROPERTY_TEXTURE, "DiffuseTexture", texValue.v).v);
	material.chunk(W3D_CHUNK_SHADER_MATERIAL_PROPERTY, property(W3DSHADERMATERIAL_PROPERTY_FLOAT, "BumpScale", f1.v).v);
	material.chunk(W3D_CHUNK_SHADER_MATERIAL_PROPERTY, property(W3DSHADERMATERIAL_PROPERTY_VECTOR2, "UV", f2.v).v);
	material.chunk(W3D_CHUNK_SHADER_MATERIAL_PROPERTY, property(W3DSHADERMATERIAL_PROPERTY_VECTOR3, "Tint", f3.v).v);
	material.chunk(W3D_CHUNK_SHADER_MATERIAL_PROPERTY, property(W3DSHADERMATERIAL_PROPERTY_VECTOR4, "SpecularColor", f4.v).v);
	material.chunk(W3D_CHUNK_SHADER_MATERIAL_PROPERTY, property(W3DSHADERMATERIAL_PROPERTY_INT, "Count", i1.v).v);
	material.chunk(W3D_CHUNK_SHADER_MATERIAL_PROPERTY, property(W3DSHADERMATERIAL_PROPERTY_BOOL, "AlphaTestEnable", b1.v).v);
	ChunkWriter materials;
	materials.wrapper(W3D_CHUNK_SHADER_MATERIAL, material);
	MeshSpec spec;
	spec.ExtraChildren.wrapper(W3D_CHUNK_SHADER_MATERIALS, materials);
	spec.ShaderMaterialIds = { 0 };

	MeshModelClass mesh;
	std::string error;
	REQUIRE_MESSAGE(loadMesh(mesh, meshBytes(spec), error), error);
	REQUIRE(mesh.ShaderMaterials.size() == 1);
	const MeshShaderMaterialDef &def = mesh.ShaderMaterials[0];
	CHECK(def.Number == 3);
	CHECK(def.TypeName == "normalmapped.fx");
	REQUIRE(def.Properties.size() == 7);
	CHECK(def.Properties[0].Name == "DiffuseTexture");
	CHECK(def.Properties[0].StringValue == "a.dds");
	CHECK(def.Properties[1].Float[0] == 0.5f);
	CHECK(def.Properties[2].Float[1] == 2.0f);
	CHECK(def.Properties[3].Float[2] == 3.0f);
	CHECK(def.Properties[4].Float[0] == 0.25f);
	CHECK(def.Properties[4].Float[3] == 1.0f);
	CHECK(def.Properties[5].Int == -7);
	CHECK(def.Properties[6].Bool);
	REQUIRE(mesh.Passes.size() == 1);
	CHECK(mesh.Passes[0].ShaderMaterialIds == std::vector<std::uint32_t>{ 0 });
	CHECK(mesh.UnhandledChunks.empty());

	// a property with a stray byte, and an unknown type, are refused
	Bytes bad = property(W3DSHADERMATERIAL_PROPERTY_FLOAT, "X", f1.v);
	bad.u8(0);
	ChunkWriter badMaterial;
	badMaterial.chunk(W3D_CHUNK_SHADER_MATERIAL_HEADER, header.v);
	badMaterial.chunk(W3D_CHUNK_SHADER_MATERIAL_PROPERTY, bad.v);
	ChunkWriter badMaterials;
	badMaterials.wrapper(W3D_CHUNK_SHADER_MATERIAL, badMaterial);
	MeshSpec badSpec;
	badSpec.ExtraChildren.wrapper(W3D_CHUNK_SHADER_MATERIALS, badMaterials);
	MeshModelClass badMesh;
	CHECK_FALSE(loadMesh(badMesh, meshBytes(badSpec), error));
	CHECK(error.find("malformed W3D_CHUNK_SHADER_MATERIAL_PROPERTY") != std::string::npos);

	ChunkWriter oddMaterial;
	oddMaterial.chunk(W3D_CHUNK_SHADER_MATERIAL_HEADER, header.v);
	oddMaterial.chunk(W3D_CHUNK_SHADER_MATERIAL_PROPERTY, property(99, "X", f1.v).v);
	ChunkWriter oddMaterials;
	oddMaterials.wrapper(W3D_CHUNK_SHADER_MATERIAL, oddMaterial);
	MeshSpec oddSpec;
	oddSpec.ExtraChildren.wrapper(W3D_CHUNK_SHADER_MATERIALS, oddMaterials);
	MeshModelClass oddMesh;
	CHECK_FALSE(loadMesh(oddMesh, meshBytes(oddSpec), error));
	CHECK(error.find("type 99") != std::string::npos);
}

TEST_CASE("AABTree chunks: header counts must match the polygon index and node arrays")
{
	auto tree = [](std::uint32_t nodes, std::uint32_t polys, std::uint32_t nodesPresent, std::uint32_t polysPresent) {
		W3dMeshAABTreeHeader h = {};
		h.NodeCount = nodes;
		h.PolyCount = polys;
		ChunkWriter t;
		t.chunk(W3D_CHUNK_AABTREE_HEADER, ChunkWriter::of(h));
		t.chunk(W3D_CHUNK_AABTREE_POLYINDICES, ChunkWriter::ofArray(std::vector<std::uint32_t>(polysPresent, 7)));
		t.chunk(W3D_CHUNK_AABTREE_NODES, ChunkWriter::ofArray(std::vector<W3dMeshAABTreeNode>(nodesPresent, W3dMeshAABTreeNode{})));
		ChunkWriter w;
		w.wrapper(W3D_CHUNK_AABTREE, t);
		return w;
	};
	std::string error;
	{
		MeshSpec spec;
		spec.ExtraChildren = tree(2, 4, 2, 4);
		MeshModelClass mesh;
		REQUIRE_MESSAGE(loadMesh(mesh, meshBytes(spec), error), error);
		CHECK(mesh.HasAABTree);
		CHECK(mesh.AABTreeNodes.size() == 2);
		CHECK(mesh.AABTreePolyIndices.size() == 4);
		CHECK(mesh.AABTreePolyIndices[3] == 7);
		CHECK(mesh.UnhandledChunks.empty());
	}
	{
		MeshSpec spec;
		spec.ExtraChildren = tree(2, 4, 1, 4); // one node missing
		MeshModelClass mesh;
		CHECK_FALSE(loadMesh(mesh, meshBytes(spec), error));
		CHECK(error.find("AABTree") != std::string::npos);
	}
}
