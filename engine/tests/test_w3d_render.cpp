// OpenBFME unit tests: mesh render data (passes, surfaces, skin weights), HLOD prototypes through the asset manager, texture
// path resolution and the housecolor.ini table, all on synthetic W3D files. GPL-3.0.
// Expected values are written down from the W3D format, the ZH / BFME2 source rules cited in the headers, or worked out by
// hand; none is read back from the code under test.

#include "doctest.h"
#include "W3dSynth.h"

#include "GameEngineDevice/W3DDevice/GameClient/HouseColor.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"
#include "Libraries/WWVegas/WW3D2/hanim.h"
#include "Libraries/WWVegas/WW3D2/htree.h"
#include "Libraries/WWVegas/WW3D2/mapper.h"
#include "Libraries/WWVegas/WW3D2/meshrender.h"
#include "Libraries/WWVegas/WW3D2/textureloader.h"
#include "Libraries/WWVegas/WWLib/chunkio.h"

#include <set>

using namespace w3dsynth;

namespace
{

MeshModelClass loadMeshFromChunk(const std::vector<std::uint8_t> &bytes)
{
	ChunkLoadClass cload(bytes.data(), bytes.size());
	REQUIRE(cload.Open_Chunk());
	MeshModelClass mesh;
	std::string error;
	REQUIRE_MESSAGE(mesh.Load_W3D(cload, &error), error);
	return mesh;
}

bool buildFrom(const SynthMesh &s, MeshRenderData &out, std::string &error)
{
	MeshModelClass mesh = loadMeshFromChunk(meshChunk(s));
	return Build_Mesh_Render_Data(mesh, out, &error);
}

// Four vertices of a quad, two triangles.
SynthMesh quad()
{
	SynthMesh m;
	m.Vertices = { { 0, 0, 0 }, { 1, 0, 0 }, { 1, 1, 0 }, { 0, 1, 0 } };
	m.Triangles = { 0, 1, 2, 0, 2, 3 };
	return m;
}

W3dTexCoordStruct tc(float u, float v) { return W3dTexCoordStruct{ u, v }; }

HTreePose poseOf(const HTreeClass &tree)
{
	HTreePose p;
	tree.Base_Pose(Matrix3D(), p);
	return p;
}

} // namespace

TEST_CASE("render data: header flags, camera modes, sort rule and bounds come from the mesh header")
{
	SynthMesh s = quad();
	s.Attributes = W3D_MESH_FLAG_TWO_SIDED | W3D_MESH_FLAG_HIDDEN | W3D_MESH_FLAG_CAST_SHADOW | W3D_MESH_FLAG_GEOMETRY_TYPE_CAMERA_ALIGNED;
	MeshRenderData r;
	std::string error;
	REQUIRE_MESSAGE(buildFrom(s, r, error), error);
	CHECK(r.TwoSided);
	CHECK(r.Hidden);
	CHECK(r.CastShadow);
	CHECK(r.Camera == MESH_CAMERA_ALIGNED);
	CHECK_FALSE(r.Skin);
	CHECK(r.NumVertices == 4);
	CHECK(r.NumTriangles == 2);
	CHECK(r.Surfaces.empty()); // no material pass: nothing to draw

	s.Attributes = W3D_MESH_FLAG_GEOMETRY_TYPE_CAMERA_ORIENTED;
	REQUIRE_MESSAGE(buildFrom(s, r, error), error);
	CHECK(r.Camera == MESH_CAMERA_ORIENTED);
}

TEST_CASE("render data: UVs are flipped to (u, 1 - v) and DCG, textures, shader state and mapper types reach the surface")
{
	SynthMesh s = quad();
	s.Shaders = { opaqueShader() };
	s.Textures = { { "Body.tga", true, 0x18 } };
	SynthVMat vm;
	vm.Attributes = (std::uint32_t)W3D_MAPPING_LINEAR_OFFSET << 16; // stage 0 mapping type 4
	vm.Args0 = "UPerSec=0.5; speed\nVPerSec=0";
	s.VertexMaterials = { vm };
	SynthPass p;
	p.ShaderIds = { 0 };
	p.VertexMaterialIds = { 0 };
	p.DCG = { { 10, 20, 30, 255 }, { 11, 21, 31, 255 }, { 12, 22, 32, 255 }, { 13, 23, 33, 255 } };
	SynthStage st;
	st.TextureIds = { 0 };
	st.TexCoords = { tc(0.0f, 0.0f), tc(1.0f, 0.25f), tc(1.0f, 1.0f), tc(0.0f, 0.75f) };
	p.Stages = { st };
	s.Passes = { p };
	MeshRenderData r;
	std::string error;
	REQUIRE_MESSAGE(buildFrom(s, r, error), error);
	REQUIRE(r.Surfaces.size() == 1);
	const MeshDrawSurface &surf = r.Surfaces[0];
	CHECK(surf.Kind == MATERIAL_CLASSIC);
	CHECK(surf.Triangles == std::vector<std::uint32_t>{ 0, 1 });
	CHECK(surf.Indices == std::vector<std::uint32_t>{ 0, 1, 2, 0, 2, 3 });
	REQUIRE(surf.Stage[0].HasTexture);
	CHECK(surf.Stage[0].TextureName == "Body.tga");
	CHECK(surf.Stage[0].HasTextureInfo);
	CHECK(surf.Stage[0].TextureInfo.Attributes == 0x18);
	CHECK_FALSE(surf.Stage[1].HasTexture);
	CHECK(surf.Stage[0].MapperType == W3D_MAPPING_LINEAR_OFFSET);
	CHECK(surf.Stage[1].MapperType == 0);
	CHECK(surf.Stage[0].MapperArgs == "UPerSec=0.5; speed\nVPerSec=0");
	CHECK(surf.HasDCG);
	REQUIRE(surf.StageUVStream[0] >= 0);
	const std::vector<float> &uv = r.PassUV[(size_t)surf.StageUVStream[0]].UV;
	REQUIRE(uv.size() == 8);
	CHECK(uv[0] == 0.0f);
	CHECK(uv[1] == 1.0f);   // 1 - 0
	CHECK(uv[3] == 0.75f);  // 1 - 0.25
	CHECK(uv[5] == 0.0f);   // 1 - 1
	CHECK(uv[7] == 0.25f);  // 1 - 0.75
	REQUIRE(r.PassDCG[0].size() == 16);
	CHECK(r.PassDCG[0][0] == 10);
	CHECK(r.PassDCG[0][4 + 2] == 31);
	CHECK(r.PassDCG[0][12 + 3] == 255);
	CHECK(surf.State.Blend == W3D_GODOT_BLEND_OPAQUE);
	CHECK(surf.State.Texturing);
	CHECK_FALSE(surf.Sorted);
}

TEST_CASE("render data: passes make separate surfaces and per-triangle ids split a pass by (shader, vertex material of the first vertex, textures)")
{
	SynthMesh s = quad();
	W3dShaderStruct blend = opaqueShader();
	blend.SrcBlend = 2;
	blend.DestBlend = 5;
	s.Shaders = { opaqueShader(), blend };
	s.Textures = { { "A.tga" }, { "B.tga" } };
	s.VertexMaterials = { SynthVMat(), SynthVMat() };
	s.VertexMaterials[1].Name = "second";
	SynthPass p0;
	p0.ShaderIds = { 0, 1 };                 // triangle 0 opaque, triangle 1 blended
	p0.VertexMaterialIds = { 0, 0, 1, 1 };   // triangle 0 starts at vertex 0 (material 0), triangle 1 at vertex 0 too
	SynthStage a;
	a.TextureIds = { 0, 1 };
	a.TexCoords = { tc(0, 0), tc(1, 0), tc(1, 1), tc(0, 1) };
	p0.Stages = { a };
	SynthPass p1;
	p1.ShaderIds = { 1 };
	SynthStage b;
	b.TextureIds = { 1 };
	b.TexCoords = a.TexCoords;
	p1.Stages = { b };
	s.Passes = { p0, p1 };
	MeshRenderData r;
	std::string error;
	REQUIRE_MESSAGE(buildFrom(s, r, error), error);
	// pass 0: two surfaces; pass 1: one (all triangles)
	REQUIRE(r.Surfaces.size() == 3);
	int pass0 = 0, pass1 = 0;
	for (const MeshDrawSurface &surf : r.Surfaces)
	{
		(surf.Pass == 0 ? pass0 : pass1)++;
		if (surf.Pass == 0 && surf.Triangles == std::vector<std::uint32_t>{ 0 })
		{
			CHECK(surf.Stage[0].TextureName == "A.tga");
			CHECK(surf.State.Blend == W3D_GODOT_BLEND_OPAQUE);
		}
		if (surf.Pass == 0 && surf.Triangles == std::vector<std::uint32_t>{ 1 })
		{
			CHECK(surf.Stage[0].TextureName == "B.tga");
			CHECK(surf.State.Blend == W3D_GODOT_BLEND_MIX);
			CHECK(surf.Sorted); // blended pass 0, SortLevel 0 (ZH meshmdlio.cpp:1163-1214)
		}
		if (surf.Pass == 1)
		{
			CHECK(surf.Triangles == std::vector<std::uint32_t>{ 0, 1 });
		}
	}
	CHECK(pass0 == 2);
	CHECK(pass1 == 1);
	CHECK(r.TotalSurfaceTriangles() == 4); // 2 triangles drawn in each of the two passes
}

TEST_CASE("render data: a stage without its own texture coordinates reads UV array 0 unless its mapper makes coordinates (ZH Configure_Material)")
{
	SynthMesh s = quad();
	s.Shaders = { opaqueShader() };
	s.Textures = { { "A.tga" }, { "Detail.tga" } };
	SynthVMat plain, env;
	plain.Name = "plain";
	env.Name = "env";
	env.Attributes = (std::uint32_t)W3D_MAPPING_ENVIRONMENT << 8; // stage 1 environment mapped
	s.VertexMaterials = { plain, env };
	SynthPass p;
	p.ShaderIds = { 0 };
	p.VertexMaterialIds = { 0 };
	SynthStage st0, st1;
	st0.TextureIds = { 0 };
	st0.TexCoords = { tc(0, 0), tc(1, 0), tc(1, 1), tc(0, 1) };
	st1.TextureIds = { 1 }; // texture, no coordinates of its own
	p.Stages = { st0, st1 };
	s.Passes = { p };
	MeshRenderData r;
	std::string error;
	REQUIRE_MESSAGE(buildFrom(s, r, error), error);
	REQUIRE(r.Surfaces.size() == 1);
	CHECK(r.Surfaces[0].StageUVStream[0] == 0);
	CHECK(r.Surfaces[0].StageUVStream[1] == 0); // plain UV mapping: falls back to array 0
	CHECK(r.Warnings.empty());

	// the same stage 1 with an environment mapper generates its own coordinates
	s.Passes[0].VertexMaterialIds = { 1 };
	REQUIRE_MESSAGE(buildFrom(s, r, error), error);
	CHECK(r.Surfaces[0].StageUVStream[1] == -1);
	CHECK(r.Surfaces[0].Stage[1].MapperType == W3D_MAPPING_ENVIRONMENT);
}

TEST_CASE("render data: a polygon takes its vertex material from its FIRST vertex (ZH dx8renderer.cpp:959)")
{
	SynthMesh s = quad();
	s.Shaders = { opaqueShader() };
	s.Textures = { { "A.tga" } };
	SynthVMat red, green;
	red.Name = "red";
	red.Diffuse[0] = 255;
	red.Diffuse[1] = 0;
	red.Diffuse[2] = 0;
	green.Name = "green";
	green.Diffuse[0] = 0;
	green.Diffuse[1] = 255;
	green.Diffuse[2] = 0;
	s.VertexMaterials = { red, green };
	SynthPass p;
	p.ShaderIds = { 0 };
	p.VertexMaterialIds = { 1, 0, 0, 0 }; // vertex 0 is green; triangles (0,1,2) and (0,2,3) both start there
	SynthStage st;
	st.TextureIds = { 0 };
	st.TexCoords = { tc(0, 0), tc(1, 0), tc(1, 1), tc(0, 1) };
	p.Stages = { st };
	s.Passes = { p };
	MeshRenderData r;
	std::string error;
	REQUIRE_MESSAGE(buildFrom(s, r, error), error);
	REQUIRE(r.Surfaces.size() == 1);
	CHECK(r.Surfaces[0].VertexMaterialId == 1);
	CHECK(r.Surfaces[0].VertexMaterial.Diffuse.G == 255);
	CHECK(r.Surfaces[0].VertexMaterial.Diffuse.R == 0);
}

TEST_CASE("render data: malformed passes are errors, never defaults")
{
	std::string error;
	MeshRenderData r;
	{
		SynthMesh s = quad();
		s.Shaders = { opaqueShader() };
		s.Textures = { { "A.tga" } };
		SynthPass p;
		p.ShaderIds = { 0 };
		SynthStage st;
		st.TextureIds = { 0 }; // a texture but no texture coordinates anywhere in the mesh: built, and reported
		p.Stages = { st };
		s.Passes = { p };
		REQUIRE_MESSAGE(buildFrom(s, r, error), error);
		REQUIRE(r.Warnings.size() == 1);
		CHECK(r.Warnings[0].find("no texture coordinates at all") != std::string::npos);
		CHECK(r.Surfaces[0].StageUVStream[0] == -1);
	}
	{
		SynthMesh s = quad();
		s.Shaders = { opaqueShader() };
		SynthPass p;
		p.ShaderIds = { 7 };
		s.Passes = { p };
		CHECK_FALSE(buildFrom(s, r, error));
		CHECK(error.find("outside its table") != std::string::npos);
	}
	{
		SynthMesh s = quad();
		s.Shaders = { opaqueShader() };
		SynthPass p;
		p.ShaderIds = { 0, 0, 0 }; // neither one nor one per triangle
		s.Passes = { p };
		CHECK_FALSE(buildFrom(s, r, error));
		CHECK(error.find("one id, or one per item") != std::string::npos);
	}
	{
		SynthMesh s = quad();
		s.Textures = { { "A.tga" } };
		SynthPass p; // a textured stage with no shader id at all
		SynthStage st;
		st.TextureIds = { 0 };
		st.TexCoords = { tc(0, 0), tc(1, 0), tc(1, 1), tc(0, 1) };
		p.Stages = { st };
		s.Passes = { p };
		CHECK_FALSE(buildFrom(s, r, error));
		CHECK(error.find("no shader id") != std::string::npos);
	}
	{
		SynthMesh s = quad();
		s.Shaders = { opaqueShader() };
		SynthVMat vm;
		vm.Attributes = (std::uint32_t)W3D_MAPPING_SCALE << 16;
		vm.Args0 = "UScale=2\nNotAKey=1";
		s.VertexMaterials = { vm };
		SynthPass p;
		p.ShaderIds = { 0 };
		p.VertexMaterialIds = { 0 };
		s.Passes = { p };
		CHECK_FALSE(buildFrom(s, r, error));
		CHECK(error.find("notakey") != std::string::npos);
	}
}

TEST_CASE("render data: two-bone skin weights and the second-bone streams (hand-computed deformation)")
{
	SynthMesh s;
	s.Attributes = W3D_MESH_FLAG_GEOMETRY_TYPE_SKIN;
	s.Vertices = { { 1, 0, 0 }, { 1, 0, 0 }, { 5, 5, 5 } };
	s.Normals = { { 0, 0, 1 }, { 0, 0, 1 }, { 0, 0, 1 } };
	s.Vertices2 = { { 0, 0, 0 }, { 0, 1, 0 }, { 0, 0, 0 } };
	s.Normals2 = { { 0, 0, 1 }, { 0, 1, 0 }, { 0, 0, 1 } };
	s.Triangles = { 0, 1, 2 };
	W3dVertInfStruct i0 = { 1, 0, 100, 0 }; // all bone 1
	W3dVertInfStruct i1 = { 1, 2, 60, 40 }; // 60% bone 1 (p0), 40% bone 2 (p1)
	W3dVertInfStruct i2 = { 1, 0, 0, 0 };   // both weights 0: counted, treated as bone 1 only (OpenSAGE's reading)
	s.Influences = { i0, i1, i2 };
	MeshRenderData r;
	std::string error;
	REQUIRE_MESSAGE(buildFrom(s, r, error), error);
	REQUIRE(r.Skin);
	REQUIRE(r.DualBone);
	CHECK(r.ZeroWeightVertices == 1);
	CHECK(r.Weight0[0] == 1.0f);
	CHECK(r.Weight0[1] == doctest::Approx(0.6f));
	CHECK(r.Weight1[1] == doctest::Approx(0.4f));
	CHECK(r.Weight0[2] == 1.0f);

	// Hierarchy: root, bone 1 at (10,0,0), bone 2 at (0,20,0), both children of the root.
	HTreePose pose;
	pose.Resize(3);
	pose.Transform[1].Set_Translation(Vector3(10, 0, 0));
	pose.Transform[2].Set_Translation(Vector3(0, 20, 0));

	Vector3 d0 = Deform_Position(r, 0, pose);
	CHECK(d0.X == doctest::Approx(11.0f));
	CHECK(d0.Y == doctest::Approx(0.0f));
	// vertex 1: 0.6 * M1 * (1,0,0) + 0.4 * M2 * (0,1,0) = 0.6 * (11,0,0) + 0.4 * (0,21,0) = (6.6, 8.4, 0)
	Vector3 d1 = Deform_Position(r, 1, pose);
	CHECK(d1.X == doctest::Approx(6.6f));
	CHECK(d1.Y == doctest::Approx(8.4f));
	CHECK(d1.Z == doctest::Approx(0.0f));
	// vertex 2: bone 1 only, (5,5,5) + (10,0,0)
	Vector3 d2 = Deform_Position(r, 2, pose);
	CHECK(d2.X == doctest::Approx(15.0f));
	CHECK(d2.Y == doctest::Approx(5.0f));

	// Normals rotate only: vertex 1 blends (0,0,1) and (0,1,0) 60/40.
	Vector3 n1 = Deform_Normal(r, 1, pose);
	CHECK(n1.X == doctest::Approx(0.0f));
	CHECK(n1.Y == doctest::Approx(0.4f));
	CHECK(n1.Z == doctest::Approx(0.6f));

	// A bone outside the pose is an error.
	HTreePose tiny;
	tiny.Resize(2);
	CHECK_THROWS_AS(Deform_Position(r, 1, tiny), std::out_of_range);
}

TEST_CASE("render data: skin weights must sum to 100, a second bone needs its streams, rigid meshes carry no influences")
{
	std::string error;
	MeshRenderData r;
	SynthMesh s;
	s.Attributes = W3D_MESH_FLAG_GEOMETRY_TYPE_SKIN;
	s.Vertices = { { 0, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 } };
	s.Triangles = { 0, 1, 2 };
	s.Influences = { W3dVertInfStruct{ 1, 0, 70, 20 }, W3dVertInfStruct{ 1, 0, 100, 0 }, W3dVertInfStruct{ 1, 0, 100, 0 } };
	CHECK_FALSE(buildFrom(s, r, error));
	CHECK(error.find("weights 70 + 20") != std::string::npos);

	s.Influences = { W3dVertInfStruct{ 1, 2, 50, 50 }, W3dVertInfStruct{ 1, 0, 100, 0 }, W3dVertInfStruct{ 1, 0, 100, 0 } };
	CHECK_FALSE(buildFrom(s, r, error));
	CHECK(error.find("second-bone streams") != std::string::npos);

	SynthMesh rigid = quad();
	rigid.Influences = { W3dVertInfStruct{ 1, 0, 100, 0 }, W3dVertInfStruct{ 1, 0, 100, 0 }, W3dVertInfStruct{ 1, 0, 100, 0 }, W3dVertInfStruct{ 1, 0, 100, 0 } };
	CHECK_FALSE(buildFrom(rigid, r, error));
	CHECK(error.find("not a skin") != std::string::npos);
}

TEST_CASE("render data: a rigid mesh deforms by the pivot of the sub object that carries it")
{
	SynthMesh s = quad();
	MeshRenderData r;
	std::string error;
	REQUIRE_MESSAGE(buildFrom(s, r, error), error);
	HTreePose pose;
	pose.Resize(3);
	pose.Transform[2].Set_Translation(Vector3(0, 0, 7));
	Vector3 p = Deform_Position(r, 2, pose, 2);
	CHECK(p.X == doctest::Approx(1.0f));
	CHECK(p.Y == doctest::Approx(1.0f));
	CHECK(p.Z == doctest::Approx(7.0f));
	CHECK_THROWS_AS(Deform_Position(r, 2, pose, -1), std::out_of_range);
}

// ---------------------------------------------------------------------------------------------------------------------

namespace
{
// A small "game": a soldier HLOD in its own file (mesh + box), its skeleton and one animation in other files, a prop that is
// a bare mesh, and an HLOD whose hierarchy does not exist.
void buildGame(MemoryFileSource &fs)
{
	SynthMesh body = quad();
	body.Container = "ABUNIT_SKN";
	body.Name = "BODY";
	std::vector<std::uint8_t> skn = meshChunk(body);
	// a collision box in the same file
	{
		ChunkWriter box;
		W3dBoxStruct b = {};
		b.Version = W3D_MAKE_VERSION(1, 0);
		setName(b.Name, sizeof(b.Name), "ABUNIT_SKN.BOUNDINGBOX");
		box.chunk(W3D_CHUNK_BOX, ChunkWriter::of(b));
		append(skn, box.bytes);
	}
	append(skn, hlodChunk("ABUNIT_SKN", "ABUNIT_SKL",
		{ { "ABUNIT_SKN.BODY", 1 }, { "ABUNIT_SKN.BOUNDINGBOX", 0 }, { "OTHER.PART", 2 } },
		{ { "E_NOSUCH", 1 }, { "ABUNIT_SKN.BODY", 99 } }));
	fs.Add("art\\w3d\\ab\\abunit_skn.w3d", skn);

	fs.Add("art\\w3d\\ab\\abunit_skl.w3d", hierarchyChunk("ABUNIT_SKL", { pivot("ROOTTRANSFORM", 0xFFFFFFFFu, 0, 0, 0), pivot("HIP", 0, 0, 0, 1), pivot("HAND", 1, 0, 2, 0) }));

	// idle: 4 frames; HIP rises along Z by 0, 1, 2, 3
	fs.Add("art\\w3d\\ab\\abunit_idl.w3d",
		rawAnimChunk("ABUNIT_IDL", "ABUNIT_SKL", 4, 30, { { ANIM_CHANNEL_Z, 1, 0, 1, { 0, 1, 2, 3 } } }));

	SynthMesh part = quad();
	part.Container = "OTHER";
	part.Name = "PART";
	fs.Add("art\\w3d\\ot\\other.w3d", meshChunk(part));

	SynthMesh prop = quad();
	prop.Container = "";
	prop.Name = "PCRATE";
	fs.Add("art\\w3d\\pc\\pcrate.w3d", meshChunk(prop));

	std::vector<std::uint8_t> orphan = meshChunk([] { SynthMesh m = quad(); m.Container = "ORPHAN"; m.Name = "M"; return m; }());
	append(orphan, hlodChunk("ORPHAN", "NO_SUCH_SKL", { { "ORPHAN.M", 0 } }));
	fs.Add("art\\w3d\\or\\orphan.w3d", orphan);
}
} // namespace

TEST_CASE("asset manager: an HLOD prototype resolves sub objects by global name, with boxes, aggregates and reported gaps")
{
	MemoryFileSource fs;
	buildGame(fs);
	WW3DAssetManager am(fs);
	std::string error;
	const RenderObjPrototype *proto = am.Create_Render_Obj("AbUnit_Skn", &error); // any case
	REQUIRE_MESSAGE(proto, error);
	CHECK(proto->Type == RenderObjPrototype::PROTO_HLOD);
	CHECK(proto->HierarchyName == "ABUNIT_SKL");
	CHECK_FALSE(proto->HierarchyMissing);
	REQUIRE(proto->Tree);
	CHECK(proto->Tree->Num_Pivots() == 3);
	REQUIRE(proto->SubObjects.size() == 5);

	CHECK(proto->SubObjects[0].Type == RenderSubObject::SUB_MESH);
	CHECK(proto->SubObjects[0].BoneIndex == 1);
	CHECK(proto->SubObjects[0].Mesh->Get_Name() == "ABUNIT_SKN.BODY");
	CHECK(proto->SubObjects[1].Type == RenderSubObject::SUB_BOX);
	CHECK(proto->SubObjects[1].Box != nullptr);
	CHECK(proto->SubObjects[2].Type == RenderSubObject::SUB_MESH); // in OTHER.w3d, found by its container name
	CHECK(proto->SubObjects[2].Mesh->Get_Name() == "OTHER.PART");
	CHECK(proto->SubObjects[3].Aggregate);
	CHECK(proto->SubObjects[3].Type == RenderSubObject::SUB_UNRESOLVED);
	CHECK(proto->SubObjects[3].Reason.find("no prototype") != std::string::npos);
	CHECK(proto->SubObjects[4].Type == RenderSubObject::SUB_UNRESOLVED); // bone 99 is outside the hierarchy
	CHECK(proto->SubObjects[4].Reason.find("outside the hierarchy") != std::string::npos);

	// the same prototype comes back from the cache
	CHECK(am.Create_Render_Obj("abunit_skn", &error) == proto);
}

TEST_CASE("asset manager: a bare mesh is a one-pivot render object and an HLOD without its hierarchy gets the default tree, flagged")
{
	MemoryFileSource fs;
	buildGame(fs);
	WW3DAssetManager am(fs);
	std::string error;
	const RenderObjPrototype *prop = am.Create_Render_Obj("PCrate", &error);
	REQUIRE_MESSAGE(prop, error);
	CHECK(prop->Type == RenderObjPrototype::PROTO_MESH);
	CHECK(prop->Tree->Num_Pivots() == 1);
	REQUIRE(prop->SubObjects.size() == 1);
	CHECK(prop->SubObjects[0].Mesh->Get_Name() == "PCRATE");

	const RenderObjPrototype *orphan = am.Create_Render_Obj("ORPHAN", &error);
	REQUIRE_MESSAGE(orphan, error);
	CHECK(orphan->HierarchyMissing);
	CHECK(orphan->Tree->Num_Pivots() == 1);
	CHECK(orphan->Tree->Get_Pivot(0).Name == "RootTransform");
	CHECK(orphan->SubObjects[0].Type == RenderSubObject::SUB_MESH);

	CHECK(am.Create_Render_Obj("NOSUCH", &error) == nullptr);
	CHECK(error.find("not registered") != std::string::npos);
}

TEST_CASE("asset manager: animations are looked up as HIERARCHY.ANIM; BFME2's prefix[N].name[N] fallback order")
{
	MemoryFileSource fs;
	buildGame(fs);
	WW3DAssetManager am(fs);
	std::string error;
	const HAnimClass *anim = am.Get_HAnim("abunit_skl.abunit_idl", &error);
	REQUIRE_MESSAGE(anim, error);
	CHECK(anim->Get_Num_Frames() == 4);
	CHECK(anim->Get_Frame_Rate() == 30.0f);
	CHECK(am.Get_HAnim("ABUNIT_IDL", &error) == nullptr); // no HIERARCHY. part
	CHECK(am.Get_HAnim("ABUNIT_SKL.ABUNIT_GONE", &error) == nullptr);

	std::string resolved;
	CHECK(am.Resolve_Animation("ABUNIT_SKL", "ABUNIT_IDL", false, 0, &resolved));
	CHECK(resolved == "ABUNIT_SKL.ABUNIT_IDL");
	// numbered: prefix2.name2 does not exist, so it retries prefix.name
	resolved.clear();
	CHECK(am.Resolve_Animation("ABUNIT_SKL", "ABUNIT_IDL", true, 2, &resolved));
	CHECK(resolved == "ABUNIT_SKL.ABUNIT_IDL");
	// no prefix: the bare name, which is not a HIERARCHY.ANIM key
	CHECK_FALSE(am.Resolve_Animation("", "ABUNIT_IDL", false, 0, &resolved));
	CHECK_FALSE(am.Resolve_Animation("ABUNIT_SKL", "NOPE", true, 1, &resolved));
}

TEST_CASE("asset manager: the animation drives the shared hierarchy to hand-computed bone positions")
{
	MemoryFileSource fs;
	buildGame(fs);
	WW3DAssetManager am(fs);
	std::string error;
	const RenderObjPrototype *proto = am.Create_Render_Obj("ABUNIT_SKN", &error);
	REQUIRE_MESSAGE(proto, error);
	const HAnimClass *anim = am.Get_HAnim("ABUNIT_SKL.ABUNIT_IDL", &error);
	REQUIRE_MESSAGE(anim, error);
	HTreePose pose;
	proto->Tree->Anim_Pose(Matrix3D(), anim, 2.0f, pose);
	// HIP base (0,0,1) + animated Z of frame 2 = 2 -> (0,0,3); HAND is (0,2,0) below it.
	CHECK(pose.Transform[1].Get_Translation().Z == doctest::Approx(3.0f));
	CHECK(pose.Transform[2].Get_Translation().Y == doctest::Approx(2.0f));
	CHECK(pose.Transform[2].Get_Translation().Z == doctest::Approx(3.0f));
	proto->Tree->Anim_Pose(Matrix3D(), anim, 0.0f, pose);
	CHECK(pose.Transform[1].Get_Translation().Z == doctest::Approx(1.0f));
}

TEST_CASE("asset manager: a malformed file is reported once in Faults and its names are not registered")
{
	MemoryFileSource fs;
	fs.Add("art\\w3d\\ba\\bad.w3d", std::vector<std::uint8_t>{ 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0x7F });
	WW3DAssetManager am(fs);
	std::string error;
	CHECK(am.Create_Render_Obj("BAD", &error) == nullptr);
	CHECK(am.Create_Render_Obj("BAD", &error) == nullptr);
	CHECK(am.Faults().size() == 1);
}

// ---------------------------------------------------------------------------------------------------------------------

TEST_CASE("texture resolution: dds first, then tga, then art\\textures; names are lower-cased; two-letter folder")
{
	std::set<std::string> files = { "art\\compiledtextures\\gu\\gumanatarms.dds", "art\\compiledtextures\\ot\\other.tga",
		"art\\textures\\apt_thing.tga", "art\\compiledtextures\\ex\\exicemunitionsalpha .dds", "art\\compiledtextures\\ma\\mapname.dds" };
	auto exists = [&](const std::string &p) { return files.count(p) != 0; };

	TextureResolution a = Resolve_W3D_Texture("GUManAtArms.tga", exists);
	REQUIRE(a.Found);
	CHECK(a.Path == "art\\compiledtextures\\gu\\gumanatarms.dds");
	CHECK(a.IsDDS);

	TextureResolution b = Resolve_W3D_Texture("Other.tga", exists); // no dds: the tga as written
	REQUIRE(b.Found);
	CHECK(b.Path == "art\\compiledtextures\\ot\\other.tga");
	CHECK_FALSE(b.IsDDS);

	TextureResolution c = Resolve_W3D_Texture("APT_Thing.tga", exists);
	REQUIRE(c.Found);
	CHECK(c.Path == "art\\textures\\apt_thing.tga");

	// a name with a space matches the archive entry that carries the space...
	TextureResolution d = Resolve_W3D_Texture("ExIceMunitionsAlpha .tga", exists);
	REQUIRE(d.Found);
	CHECK(d.Path == "art\\compiledtextures\\ex\\exicemunitionsalpha .dds");
	// ...and a name whose file has none does NOT match with the spaces stripped: BFME2 copies the name as written
	// (0x4785F1-0x478606), so "Map Name.tga" is a missing texture even though "mapname.dds" exists. The candidates tried
	// are the three paths of the name as written, none with the space removed.
	TextureResolution e = Resolve_W3D_Texture("Map Name.tga", exists);
	CHECK_FALSE(e.Found);
	REQUIRE(e.Tried.size() == 3);
	CHECK(e.Tried[0] == "art\\compiledtextures\\ma\\map name.dds");
	CHECK(e.Tried[1] == "art\\compiledtextures\\ma\\map name.tga");
	CHECK(e.Tried[2] == "art\\textures\\map name.tga");

	TextureResolution f = Resolve_W3D_Texture("Missing.tga", exists);
	CHECK_FALSE(f.Found);
	CHECK(f.Tried.size() == 3);
	CHECK(f.TerrainFolderPath.empty());

	// a texture file that exists only under art\terrain (the terrain loader's folder, RW 0x709F77) is not found by a model's name:
	// retail's name builder (RW 0x477D1C) never searches there. The file is reported in TerrainFolderPath, not used.
	std::set<std::string> withTerrain = files;
	withTerrain.insert("art\\terrain\\tclif_mor01_nrm.tga");
	TextureResolution g = Resolve_W3D_Texture("TClif_Mor01_NRM.tga", [&](const std::string &p) { return withTerrain.count(p) != 0; });
	CHECK_FALSE(g.Found);
	CHECK(g.Tried.size() == 3);
	CHECK(g.Tried[0] == "art\\compiledtextures\\tc\\tclif_mor01_nrm.dds");
	CHECK(g.TerrainFolderPath == "art\\terrain\\tclif_mor01_nrm.tga");
	// a found texture does not report one
	CHECK(a.TerrainFolderPath.empty());
}

TEST_CASE("housecolor.ini: blocks map a base texture to its house colour texture; later blocks replace earlier ones")
{
	const char *ini =
		"; header comment\n"
		"HouseColor\n"
		"\tBaseTexture\t= IUWargSntryB.tga\n"
		"\tHouseTexture\t= HC_IUWarg.tga\n"
		"End\n"
		"\n"
		"HouseColor ; trailing\n"
		"\tBaseTexture = GUHbtShfA.tga\n"
		"\tHouseTexture = HC_GUHbtShfA.tga\n"
		"End\n"
		"HouseColor\n"
		"\tBaseTexture = GUHbtShfB.tga\n"
		"\tHouseTexture = HC_GUHbtShfA.tga // shared mask\n"
		"End\n"
		"HouseColor\n"
		"\tBaseTexture = guhbtshfa.tga\n"
		"\tHouseTexture = HC_Replaced.tga\n"
		"End\n";
	HouseColorTable t;
	std::string error;
	REQUIRE_MESSAGE(t.Parse(ini, &error), error);
	CHECK(t.Block_Count() == 4);
	CHECK(t.Base_Count() == 3);
	CHECK(t.House_Texture_Count() == 3); // HC_IUWarg, HC_GUHbtShfA, HC_Replaced
	REQUIRE(t.Find("IUWARGSNTRYB.TGA"));
	CHECK(*t.Find("iuwargsntryb.tga") == "HC_IUWarg.tga");
	CHECK(*t.Find("GUHbtShfA.tga") == "HC_Replaced.tga");
	CHECK(*t.Find("GUHbtShfB.tga") == "HC_GUHbtShfA.tga");
	CHECK(t.Find("nothing.tga") == nullptr);
	REQUIRE(t.Replaced().size() == 1);
	CHECK(t.Replaced()[0] == "guhbtshfa.tga");

	CHECK_FALSE(t.Parse("HouseColor\n BaseTexture = A.tga\nEnd\n", &error)); // no HouseTexture
	CHECK(error.find("both") != std::string::npos);
	CHECK_FALSE(t.Parse("HouseColor\n Bogus = 1\nEnd\n", &error));
	CHECK(error.find("unknown field") != std::string::npos);
	CHECK_FALSE(t.Parse("HouseColor\n BaseTexture = A.tga\n HouseTexture = B.tga\n", &error)); // not closed
	CHECK(error.find("not closed") != std::string::npos);
	CHECK_FALSE(t.Parse("Colour\nEnd\n", &error));
}
