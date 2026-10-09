// OpenBFME unit tests: W3D chunk walking and loaders on synthetic bytes. GPL-3.0.

#include "doctest.h"
#include "W3dTestUtil.h"

#include "Libraries/WWVegas/WW3D2/assetmgr.h"
#include "Libraries/WWVegas/WW3D2/w3d_file.h"
#include "Libraries/WWVegas/WWLib/chunkio.h"

#include <cmath>
#include <cstring>

using namespace w3dtest;

TEST_CASE("ChunkLoadClass walks nested chunks, bounds reads and skips unread data")
{
	ChunkWriter inner;
	inner.chunk(0x11, { 1, 2, 3, 4 });
	inner.chunk(0x12, { 5, 6, 7, 8, 9, 10, 11, 12 });
	ChunkWriter file;
	file.wrapper(0x10, inner);
	file.chunk(0x20, { 0xAA });

	ChunkLoadClass cload(file.bytes.data(), file.bytes.size());
	REQUIRE(cload.Open_Chunk());
	CHECK(cload.Cur_Chunk_ID() == 0x10);
	CHECK(cload.Contains_Chunks());
	CHECK(cload.Cur_Chunk_Length() == 12 + 16);

	REQUIRE(cload.Open_Chunk());
	CHECK(cload.Cur_Chunk_ID() == 0x11);
	CHECK_FALSE(cload.Contains_Chunks());
	std::uint8_t buf[8] = {};
	CHECK(cload.Read(buf, 8) == 0); // would cross the chunk end
	CHECK(cload.Read(buf, 2) == 2);
	CHECK(buf[1] == 2);
	REQUIRE(cload.Close_Chunk()); // skips the two unread bytes

	REQUIRE(cload.Open_Chunk());
	CHECK(cload.Cur_Chunk_ID() == 0x12);
	CHECK(cload.Read(buf, 8) == 8);
	CHECK(buf[7] == 12);
	REQUIRE(cload.Close_Chunk());
	CHECK_FALSE(cload.Open_Chunk()); // parent fully eaten
	REQUIRE(cload.Close_Chunk());

	REQUIRE(cload.Open_Chunk());
	CHECK(cload.Cur_Chunk_ID() == 0x20);
	REQUIRE(cload.Close_Chunk());
	CHECK_FALSE(cload.Open_Chunk());
	CHECK_FALSE(cload.Had_Error());
}

TEST_CASE("a child chunk larger than its parent is an error, not a silent over-read")
{
	ChunkWriter inner;
	inner.u32(0x11);
	inner.u32(100); // claims 100 bytes
	inner.u32(0);
	ChunkWriter file;
	file.wrapper(0x10, inner);
	file.bytes.resize(file.bytes.size() + 200, 0); // plenty of buffer after the parent

	ChunkLoadClass cload(file.bytes.data(), file.bytes.size());
	REQUIRE(cload.Open_Chunk());
	CHECK_FALSE(cload.Open_Chunk());
	CHECK(cload.Had_Error());
}

TEST_CASE("mesh, hierarchy and HLod load from a synthetic W3D; skin bind pose follows the pivots")
{
	// Hierarchy: root + one bone translated by (0,0,10) and rotated 90 degrees about Z.
	ChunkWriter hier;
	W3dHierarchyStruct hh = {};
	hh.Version = W3D_MAKE_VERSION(4, 1);
	setName(hh.Name, sizeof(hh.Name), "TEST_SKL");
	hh.NumPivots = 2;
	hier.chunk(W3D_CHUNK_HIERARCHY_HEADER, ChunkWriter::of(hh));
	std::vector<W3dPivotStruct> pivots(2);
	std::memset(pivots.data(), 0, sizeof(W3dPivotStruct) * 2);
	setName(pivots[0].Name, W3D_NAME_LEN, "ROOTTRANSFORM");
	pivots[0].ParentIdx = 0xffffffffu;
	pivots[0].Rotation.Q[3] = 1.0f;
	setName(pivots[1].Name, W3D_NAME_LEN, "BONE");
	pivots[1].ParentIdx = 0;
	pivots[1].Translation = { 0.0f, 0.0f, 10.0f };
	float s = std::sqrt(0.5f);
	pivots[1].Rotation.Q[2] = s; // z
	pivots[1].Rotation.Q[3] = s; // w
	hier.chunk(W3D_CHUNK_PIVOTS, ChunkWriter::ofArray(pivots));

	// Skin mesh: one triangle, vertex 1 bound to BONE.
	ChunkWriter mesh;
	W3dMeshHeader3Struct mh = {};
	mh.Version = W3D_MAKE_VERSION(4, 2);
	mh.Attributes = W3D_MESH_FLAG_GEOMETRY_TYPE_SKIN;
	setName(mh.MeshName, sizeof(mh.MeshName), "BODY");
	setName(mh.ContainerName, sizeof(mh.ContainerName), "TEST_SKN");
	mh.NumTris = 1;
	mh.NumVertices = 3;
	mesh.chunk(W3D_CHUNK_MESH_HEADER3, ChunkWriter::of(mh));
	std::vector<W3dVectorStruct> verts = { { 0, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 } };
	mesh.chunk(W3D_CHUNK_VERTICES, ChunkWriter::ofArray(verts));
	mesh.chunk(W3D_CHUNK_VERTEX_NORMALS, ChunkWriter::ofArray(std::vector<W3dVectorStruct>(3, { 0, 0, 1 })));
	W3dTriStruct tri = {};
	tri.Vindex[0] = 0;
	tri.Vindex[1] = 1;
	tri.Vindex[2] = 2;
	mesh.chunk(W3D_CHUNK_TRIANGLES, ChunkWriter::of(tri));
	std::vector<W3dVertInfStruct> inf(3);
	std::memset(inf.data(), 0, sizeof(W3dVertInfStruct) * 3);
	inf[1].BoneIdx = 1;
	mesh.chunk(W3D_CHUNK_VERTEX_INFLUENCES, ChunkWriter::ofArray(inf));
	ChunkWriter texName;
	texName.chunk(W3D_CHUNK_TEXTURE_NAME, { 'T', 'e', 's', 't', '.', 't', 'g', 'a', 0 });
	ChunkWriter tex;
	tex.wrapper(W3D_CHUNK_TEXTURE, texName);
	mesh.wrapper(W3D_CHUNK_TEXTURES, tex);

	// HLod: one LOD with the mesh on bone 0.
	ChunkWriter hlod;
	W3dHLodHeaderStruct lh = {};
	lh.Version = W3D_MAKE_VERSION(1, 0);
	lh.LodCount = 1;
	setName(lh.Name, sizeof(lh.Name), "TEST_SKN");
	setName(lh.HierarchyName, sizeof(lh.HierarchyName), "TEST_SKL");
	hlod.chunk(W3D_CHUNK_HLOD_HEADER, ChunkWriter::of(lh));
	ChunkWriter lod;
	W3dHLodArrayHeaderStruct ah = { 1, 0.0f };
	lod.chunk(W3D_CHUNK_HLOD_SUB_OBJECT_ARRAY_HEADER, ChunkWriter::of(ah));
	W3dHLodSubObjectStruct so = {};
	so.BoneIndex = 0;
	setName(so.Name, sizeof(so.Name), "TEST_SKN.BODY");
	lod.chunk(W3D_CHUNK_HLOD_SUB_OBJECT, ChunkWriter::of(so));
	hlod.wrapper(W3D_CHUNK_HLOD_LOD_ARRAY, lod);

	ChunkWriter file;
	file.wrapper(W3D_CHUNK_HIERARCHY, hier);
	file.wrapper(W3D_CHUNK_MESH, mesh);
	file.wrapper(W3D_CHUNK_HLOD, hlod);
	file.chunk(W3D_CHUNK_POINTS, { 0, 0, 0, 0 }); // no loader: listed in OtherChunks, not dropped

	W3DFileContents contents;
	std::string error;
	REQUIRE_MESSAGE(Load_W3D_File(file.bytes.data(), file.bytes.size(), contents, &error), error);
	REQUIRE(contents.Meshes.size() == 1);
	REQUIRE(contents.HTrees.size() == 1);
	REQUIRE(contents.HLods.size() == 1);
	CHECK(contents.OtherChunks == std::vector<std::uint32_t>{ W3D_CHUNK_POINTS });

	const MeshModelClass *m = contents.Find_Mesh("test_skn.body");
	REQUIRE(m);
	CHECK(m->Is_Skin());
	CHECK(m->Vertices.size() == m->Header.NumVertices);
	CHECK(m->Triangles.size() == m->Header.NumTris);
	REQUIRE(m->Textures.size() == 1);
	CHECK(m->Textures[0].Name == "Test.tga");

	const HLodDefClass *h = contents.Find_HLod("TEST_SKN");
	REQUIRE(h);
	CHECK(h->HierarchyName == "TEST_SKL");
	REQUIRE(h->Lod.size() == 1);
	CHECK(h->Lod[0].ModelName[0] == "TEST_SKN.BODY");

	HTreeClass tree = *contents.Find_HTree("TEST_SKL");
	tree.Base_Update(Matrix3D());
	Vector3 p = tree.Get_Transform(m->Influences[1].BoneIdx).Transform_Point(Vector3(1, 0, 0));
	CHECK(p.X == doctest::Approx(0.0f).epsilon(1e-5));
	CHECK(p.Y == doctest::Approx(1.0f));
	CHECK(p.Z == doctest::Approx(10.0f));

	CHECK(W3D_Asset_Path("GUMAArms_SKN") == "art\\w3d\\gu\\gumaarms_skn.w3d");
	CHECK(W3D_Compiled_Texture_Path("GUManAtArms.tga") == "art\\compiledtextures\\gu\\gumanatarms.dds");
}

TEST_CASE("a mesh whose chunks disagree with its header is rejected")
{
	ChunkWriter mesh;
	W3dMeshHeader3Struct mh = {};
	mh.Version = W3D_MAKE_VERSION(4, 2);
	setName(mh.MeshName, sizeof(mh.MeshName), "M");
	mh.NumTris = 0;
	mh.NumVertices = 4;
	mesh.chunk(W3D_CHUNK_MESH_HEADER3, ChunkWriter::of(mh));
	mesh.chunk(W3D_CHUNK_VERTICES, ChunkWriter::ofArray(std::vector<W3dVectorStruct>(3, { 0, 0, 0 })));
	ChunkWriter file;
	file.wrapper(W3D_CHUNK_MESH, mesh);

	W3DFileContents contents;
	std::string error;
	CHECK_FALSE(Load_W3D_File(file.bytes.data(), file.bytes.size(), contents, &error));
	CHECK(error.find("header says 4 vertices") != std::string::npos);
}
