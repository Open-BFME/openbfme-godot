// OpenBFME unit tests: acceptance stops reported at run time (w3dstops.h), and the back-to-front order (w3dsort.h). GPL-3.0.
//
// Review finding P1 (runtime reporting): docs/STOPS.md lists behaviours the render path takes without a recovered source; each must
// show up in the model report when a model uses it. These tests assert the REPORT (ids, counts, message prefixes, which stop fires and
// which does not), never the guessed arithmetic behind the stop. Inputs are synthetic W3D files built from the format; counts are
// the number of vertices / surfaces written into them.

#include "doctest.h"
#include "W3dSynth.h"

#include "GameEngineDevice/W3DDevice/GameClient/HouseColor.h"
#include "Libraries/WWVegas/WW3D2/mapper.h"
#include "Libraries/WWVegas/WW3D2/meshrender.h"
#include "Libraries/WWVegas/WW3D2/w3dsort.h"
#include "Libraries/WWVegas/WW3D2/w3dstops.h"
#include "Libraries/WWVegas/WWLib/chunkio.h"

#include <string>
#include <vector>

using namespace w3dsynth;

namespace
{
MeshModelClass loadMesh(const SynthMesh &s)
{
	const std::vector<std::uint8_t> bytes = meshChunk(s);
	ChunkLoadClass cload(bytes.data(), bytes.size());
	REQUIRE(cload.Open_Chunk());
	MeshModelClass mesh;
	std::string error;
	REQUIRE_MESSAGE(mesh.Load_W3D(cload, &error), error);
	return mesh;
}

struct Built
{
	MeshModelClass Mesh;
	MeshRenderData Data;
};

void build(const SynthMesh &s, Built &b)
{
	b.Mesh = loadMesh(s);
	std::string error;
	REQUIRE_MESSAGE(Build_Mesh_Render_Data(b.Mesh, b.Data, &error), error);
}

SynthMesh quad()
{
	SynthMesh m;
	m.Vertices = { { 0, 0, 0 }, { 1, 0, 0 }, { 1, 1, 0 }, { 0, 1, 0 } };
	m.Triangles = { 0, 1, 2, 0, 2, 3 };
	return m;
}

W3dTexCoordStruct tc(float u, float v) { return W3dTexCoordStruct{ u, v }; }

// A one-pass textured quad with the given W3D shader and stage 0 mapper.
SynthMesh texturedQuad(const std::string &texture, const W3dShaderStruct &shader, std::uint32_t mapperAttributes = 0, const std::string &args = "", bool withUv = true)
{
	SynthMesh s = quad();
	s.Shaders = { shader };
	s.Textures = { { texture } };
	SynthVMat vm;
	vm.Attributes = mapperAttributes;
	vm.Args0 = args;
	s.VertexMaterials = { vm };
	SynthPass p;
	p.ShaderIds = { 0 };
	p.VertexMaterialIds = { 0 };
	SynthStage st;
	st.TextureIds = { 0 };
	if (withUv) st.TexCoords = { tc(0, 0), tc(1, 0), tc(1, 1), tc(0, 1) };
	p.Stages = { st };
	s.Passes = { p };
	return s;
}

W3dShaderStruct blendShader(int src, int dst, int depthCompare = 3, int pri = 1)
{
	W3dShaderStruct sh = opaqueShader();
	sh.SrcBlend = (uint8)src;
	sh.DestBlend = (uint8)dst;
	sh.DepthCompare = (uint8)depthCompare;
	sh.PriGradient = (uint8)pri;
	return sh;
}

std::vector<W3DStopHit> meshStops(const Built &b)
{
	std::vector<W3DStopHit> out;
	W3D_Collect_Mesh_Stops(b.Data, out);
	return out;
}

std::vector<W3DStopHit> surfaceStops(const Built &b, const HouseColorTable *hc = nullptr)
{
	std::vector<W3DStopHit> out;
	for (const MeshDrawSurface &surf : b.Data.Surfaces) W3D_Collect_Surface_Stops(b.Data, surf, b.Mesh, hc, out);
	return out;
}

size_t countId(const std::vector<W3DStopHit> &hits, const std::string &id)
{
	size_t n = 0;
	for (const W3DStopHit &h : hits)
		if (h.Id == id) ++n;
	return n;
}

const W3DStopHit *find(const std::vector<W3DStopHit> &hits, const std::string &id)
{
	for (const W3DStopHit &h : hits)
		if (h.Id == id) return &h;
	return nullptr;
}

bool startsWith(const std::string &s, const std::string &prefix) { return s.compare(0, prefix.size(), prefix) == 0; }
} // namespace

TEST_CASE("stops: skin weights 0/0 raise S-020 with the vertex count, dual-bone skins raise S-021, rigid meshes raise neither")
{
	SynthMesh s;
	s.Attributes = W3D_MESH_FLAG_GEOMETRY_TYPE_SKIN;
	s.Vertices = { { 1, 0, 0 }, { 1, 0, 0 }, { 5, 5, 5 }, { 6, 6, 6 } };
	s.Normals = { { 0, 0, 1 }, { 0, 0, 1 }, { 0, 0, 1 }, { 0, 0, 1 } };
	s.Vertices2 = { { 0, 0, 0 }, { 0, 1, 0 }, { 0, 0, 0 }, { 0, 0, 0 } };
	s.Normals2 = { { 0, 0, 1 }, { 0, 1, 0 }, { 0, 0, 1 }, { 0, 0, 1 } };
	s.Triangles = { 0, 1, 2, 1, 2, 3 };
	s.Influences = { { 1, 0, 100, 0 }, { 1, 2, 60, 40 }, { 1, 0, 0, 0 }, { 1, 0, 0, 0 } }; // two vertices with both weights 0
	Built b;
	build(s, b);
	const std::vector<W3DStopHit> hits = meshStops(b);
	REQUIRE(countId(hits, "S-020") == 1);
	CHECK(find(hits, "S-020")->Count == 2);
	CHECK(startsWith(find(hits, "S-020")->Message, "[S-020] "));
	REQUIRE(countId(hits, "S-021") == 1);
	CHECK(find(hits, "S-021")->Count == 4); // every vertex of a dual-bone mesh
	CHECK(startsWith(find(hits, "S-021")->Message, "[S-021] "));

	// weights that all sum to 100 raise no S-020
	s.Influences = { { 1, 0, 100, 0 }, { 1, 2, 60, 40 }, { 1, 0, 100, 0 }, { 1, 0, 100, 0 } };
	build(s, b);
	CHECK(countId(meshStops(b), "S-020") == 0);
	CHECK(countId(meshStops(b), "S-021") == 1);

	// a rigid mesh raises neither
	Built rigid;
	build(quad(), rigid);
	CHECK(meshStops(rigid).empty());
}

TEST_CASE("stops: a textured stage in a mesh with no texture coordinates raises S-025; one with coordinates does not")
{
	Built none;
	build(texturedQuad("Body.tga", opaqueShader(), 0, "", false), none);
	const std::vector<W3DStopHit> hits = meshStops(none);
	REQUIRE(countId(hits, "S-025") == 1);
	CHECK(startsWith(hits[0].Message, "[S-025] "));
	CHECK(hits[0].Message.find("Body.tga") != std::string::npos);

	Built with;
	build(texturedQuad("Body.tga", opaqueShader()), with);
	CHECK(countId(meshStops(with), "S-025") == 0);
}

TEST_CASE("stops: house colour S-022 is raised for a surface whose texture has a housecolor.ini entry, and only then")
{
	HouseColorTable table;
	std::string error;
	REQUIRE_MESSAGE(table.Parse("HouseColor\n BaseTexture = IUWargSntryB.tga\n HouseTexture = HC_IUWarg.tga\nEnd\n", &error), error);

	Built hit;
	build(texturedQuad("iuwargsntryb.tga", opaqueShader()), hit); // case-insensitive, as the table is
	const std::vector<W3DStopHit> hits = surfaceStops(hit, &table);
	REQUIRE(countId(hits, "S-022") == 1);
	CHECK(startsWith(find(hits, "S-022")->Message, "[S-022] "));
	CHECK(find(hits, "S-022")->Message.find("HC_IUWarg.tga") != std::string::npos);
	CHECK(find(hits, "S-022")->Message.find("NOT applied") != std::string::npos);

	Built other;
	build(texturedQuad("GUManAtArms.tga", opaqueShader()), other);
	CHECK(countId(surfaceStops(other, &table), "S-022") == 0);
	CHECK(countId(surfaceStops(hit, nullptr), "S-022") == 0); // no table loaded: the instancer reports that as a setup error instead
}

TEST_CASE("stops: an FX material raises S-023 per surface and names its type")
{
	Built b;
	b.Mesh = loadMesh(quad());
	MeshShaderMaterialDef def;
	def.Number = 0;
	def.TypeName = "normalmapped.fx";
	MeshShaderMaterialProperty prop;
	prop.Name = "DiffuseTexture";
	prop.StringValue = "BuildingA.tga";
	def.Properties.push_back(prop);
	b.Mesh.ShaderMaterials.push_back(def);
	b.Data.Name = "M";
	MeshDrawSurface surf;
	surf.Kind = MATERIAL_FX;
	surf.ShaderMaterialId = 0;
	b.Data.Surfaces = { surf, surf };

	HouseColorTable table;
	std::string error;
	REQUIRE_MESSAGE(table.Parse("HouseColor\n BaseTexture = BuildingA.tga\n HouseTexture = HC_BuildingA.tga\nEnd\n", &error), error);
	const std::vector<W3DStopHit> hits = surfaceStops(b, &table);
	CHECK(countId(hits, "S-023") == 2);
	CHECK(startsWith(find(hits, "S-023")->Message, "[S-023] "));
	CHECK(find(hits, "S-023")->Message.find("normalmapped.fx") != std::string::npos);
	// the FX diffuse texture counts for the house colour stop too
	CHECK(countId(hits, "S-022") == 2);
}

TEST_CASE("stops: blend pairs, depth functions and gradients Godot cannot express raise S-026 once per surface")
{
	// (ONE, SRCALPHA): spec 3.2's x26 group
	Built blend;
	build(texturedQuad("A.tga", blendShader(1, 4)), blend);
	REQUIRE(countId(surfaceStops(blend), "S-026") == 1);
	CHECK(startsWith(find(surfaceStops(blend), "S-026")->Message, "[S-026] "));

	// depth function LESS (file value 1): the x37 group, drawn as LESSEQUAL
	Built less;
	build(texturedQuad("A.tga", blendShader(1, 0, 1)), less);
	const std::vector<W3DStopHit> lessHits = surfaceStops(less);
	REQUIRE(countId(lessHits, "S-026") == 1);
	CHECK(find(lessHits, "S-026")->Message.find("LESS") != std::string::npos);

	// BUMPENVMAP gradient (file value 3): drawn with the diffuse colour only
	Built bump;
	build(texturedQuad("A.tga", blendShader(1, 0, 3, 3)), bump);
	const std::vector<W3DStopHit> bumpHits = surfaceStops(bump);
	REQUIRE(countId(bumpHits, "S-026") == 1);
	CHECK(find(bumpHits, "S-026")->Message.find("BUMPENVMAP") != std::string::npos);

	// an ordinary opaque and an exact alpha blend raise none
	Built plain, alpha;
	build(texturedQuad("A.tga", opaqueShader()), plain);
	build(texturedQuad("A.tga", blendShader(2, 5, 3)), alpha); // (SRCALPHA, INVSRCALPHA)
	CHECK(countId(surfaceStops(plain), "S-026") == 0);
	CHECK(countId(surfaceStops(alpha), "S-026") == 0);
}

TEST_CASE("stops: animated mappers raise S-027 (shared phase); a RANDOM mapper also names its non-retail generator; UV and static mappers raise none")
{
	// LINEAR_OFFSET (type 4) in stage 0
	Built scroll;
	build(texturedQuad("A.tga", opaqueShader(), (std::uint32_t)W3D_MAPPING_LINEAR_OFFSET << 16, "UPerSec=0.5\nVPerSec=0"), scroll);
	const std::vector<W3DStopHit> scrollHits = surfaceStops(scroll);
	CHECK(countId(scrollHits, "S-027") == 1);
	CHECK(find(scrollHits, "S-027")->Message.find("shared by every instance") != std::string::npos);

	Built random;
	build(texturedQuad("A.tga", opaqueShader(), (std::uint32_t)W3D_MAPPING_RANDOM << 16, "FPS=2"), random);
	const std::vector<W3DStopHit> randomHits = surfaceStops(random);
	CHECK(countId(randomHits, "S-027") == 2); // the shared phase and the generator
	bool sawGenerator = false;
	for (const W3DStopHit &h : randomHits)
		if (h.Message.find("Random4Class") != std::string::npos) sawGenerator = true;
	CHECK(sawGenerator);

	Built plain;
	build(texturedQuad("A.tga", opaqueShader()), plain);
	CHECK(countId(surfaceStops(plain), "S-027") == 0);
	Built env; // a static environment mapper has no phase
	build(texturedQuad("A.tga", opaqueShader(), (std::uint32_t)W3D_MAPPING_ENVIRONMENT << 16), env);
	CHECK(countId(surfaceStops(env), "S-027") == 0);
}

TEST_CASE("stops: S-024, S-028 and S-029 messages carry their id, counts and the names involved")
{
	const W3DStopHit clip = W3D_Bare_Clip_Stop("IDLE", "GUHM_SKL", "GUHM_SKL.IDLE");
	CHECK(clip.Id == "S-024");
	CHECK(startsWith(clip.Message, "[S-024] "));
	CHECK(clip.Message.find("\"IDLE\"") != std::string::npos);
	CHECK(clip.Message.find("GUHM_SKL.IDLE") != std::string::npos);

	const W3DStopHit pose = W3D_Pose_Order_Stop(7);
	CHECK(pose.Id == "S-028");
	CHECK(pose.Count == 7);
	CHECK(startsWith(pose.Message, "[S-028] 7 pose(s)"));

	const W3DStopHit dither = W3D_Fade_Dither_Stop(3);
	CHECK(dither.Id == "S-029");
	CHECK(dither.Count == 3);
	CHECK(startsWith(dither.Message, "[S-029] 3 instance(s)"));
}

TEST_CASE("sorting: back to front by depth, farthest first, equal depths keep their insertion order")
{
	// two overlapping alpha instances inserted near-first: item 0 near (depth 2), item 1 far (depth 9)
	const std::vector<int> two = W3D_Back_To_Front({ 2.0f, 9.0f });
	REQUIRE(two.size() == 2);
	CHECK(two[0] == 1); // far drawn first
	CHECK(two[1] == 0);

	// depths 1, 5, 3, 5, -2: farthest first -> 1, 3 (tie keeps insertion order), 2, 0, 4
	const std::vector<int> five = W3D_Back_To_Front({ 1.0f, 5.0f, 3.0f, 5.0f, -2.0f });
	CHECK(five == std::vector<int>{ 1, 3, 2, 0, 4 });

	CHECK(W3D_Back_To_Front({}).empty());
}

namespace
{
W3DSortBatch batch(std::vector<float> depths, std::set<int> priorities = { 0 })
{
	W3DSortBatch b;
	b.Depth = std::move(depths);
	b.Priorities = std::move(priorities);
	return b;
}
} // namespace

TEST_CASE("sorting across batches: group A at depths 20 and 2, group B at depth 10 cannot be drawn back to front (S-029)")
{
	// Required order A20, B10, A2: Godot draws each MultiMesh as one instanced draw, so either whole-batch order is wrong.
	const W3DSortBatch a = batch({ 20.0f, 2.0f }), b = batch({ 10.0f });
	CHECK(W3D_Batches_Interleave(a, b));
	CHECK(W3D_Batches_Interleave(b, a));
	CHECK(W3D_Count_Interleaved_Batches({ a, b }) == 1);

	// identical ranges: no whole-batch order is back to front (A20, B20, B2, A2 would be needed)
	CHECK(W3D_Batches_Interleave(batch({ 20.0f, 2.0f }), batch({ 20.0f, 2.0f })));
	CHECK(W3D_Count_Interleaved_Batches({ batch({ 20.0f, 2.0f }), batch({ 20.0f, 2.0f }) }) == 1);
	// touching ranges (A {20, 10}, B {10, 2}): A then B is back to front
	CHECK_FALSE(W3D_Batches_Interleave(batch({ 20.0f, 10.0f }), batch({ 10.0f, 2.0f })));
	// separated ranges
	CHECK_FALSE(W3D_Batches_Interleave(batch({ 20.0f, 15.0f }), batch({ 10.0f, 2.0f })));
	// B wholly in front of A (depths 20 and 15 vs 10) or wholly behind: one batch order is back to front
	CHECK_FALSE(W3D_Batches_Interleave(batch({ 20.0f, 15.0f }), batch({ 10.0f })));
	CHECK_FALSE(W3D_Batches_Interleave(batch({ 20.0f, 15.0f }), batch({ 25.0f })));
	// equal depth is not between
	CHECK_FALSE(W3D_Batches_Interleave(batch({ 20.0f, 2.0f }), batch({ 20.0f })));
	// single-instance batches never interleave
	CHECK_FALSE(W3D_Batches_Interleave(batch({ 5.0f }), batch({ 6.0f })));
	// different render priorities are ordered by the renderer, not by depth
	CHECK_FALSE(W3D_Batches_Interleave(batch({ 20.0f, 2.0f }, { 0 }), batch({ 10.0f }, { -4 })));
	CHECK(W3D_Batches_Interleave(batch({ 20.0f, 2.0f }, { 0, -4 }), batch({ 10.0f }, { -4 })));
	// empty batches
	CHECK_FALSE(W3D_Batches_Interleave(batch({}), batch({ 10.0f })));

	// three batches: A and B interleave, C is behind everything: one pair
	CHECK(W3D_Count_Interleaved_Batches({ a, b, batch({ 30.0f }) }) == 1);
	// A interleaves with both B and C
	CHECK(W3D_Count_Interleaved_Batches({ a, b, batch({ 5.0f }) }) == 2);

	const W3DStopHit hit = W3D_Batch_Order_Stop(2);
	CHECK(hit.Id == "S-029");
	CHECK(hit.Count == 2);
	CHECK(startsWith(hit.Message, "[S-029] "));
	CHECK(hit.Message.find("batch by batch") != std::string::npos);
}
