// OpenBFME unit tests: particle emitter and ShdMesh chunks on synthetic bytes. GPL-3.0.
// Layouts: ZH w3d_file.h (emitter 1755-1960, ShdMesh 2170-2237) and part_ldr.cpp Read_* helpers.

#include "doctest.h"
#include "W3dTestUtil.h"

#include "Libraries/WWVegas/WW3D2/assetmgr.h"
#include "Libraries/WWVegas/WW3D2/part_ldr.h"
#include "Libraries/WWVegas/WW3D2/shdmesh.h"
#include "Libraries/WWVegas/WW3D2/w3d_file.h"
#include "Libraries/WWVegas/WWLib/chunkio.h"

using namespace w3dtest;
using doctest::Approx;

namespace
{

struct EmitterSpec
{
	std::uint32_t Version = 0x20000;
	std::uint32_t UserStringBytes = 0;
	std::uint32_t ColorKeys = 2, OpacityKeys = 1, SizeKeys = 2;
	bool Line = false, Rotation = false, Frames = false, Blur = false, Extra = false;
	std::uint32_t RotationKeys = 1;
	std::uint32_t PropsExtraBytes = 0;
	bool DropInfoV2 = false;
	std::uint32_t UnknownChunk = 0;
};

std::vector<std::uint8_t> emitterBytes(const EmitterSpec &e)
{
	ChunkWriter em;
	W3dEmitterHeaderStruct h = {};
	h.Version = e.Version;
	setName(h.Name, sizeof(h.Name), "E_TEST");
	em.chunk(W3D_CHUNK_EMITTER_HEADER, ChunkWriter::of(h));

	Bytes user;
	user.u32(7).u32(e.UserStringBytes).u8(0).u8(0).u8(0).u8(0); // type, size, StringParam[1] + padding = 12 bytes
	for (std::uint32_t i = 0; i < e.UserStringBytes; ++i)
	{
		user.u8(i + 1 == e.UserStringBytes ? 0 : (std::uint8_t)('a' + i));
	}
	em.chunk(W3D_CHUNK_EMITTER_USER_DATA, user.v);

	W3dEmitterInfoStruct info = {};
	setName(info.TextureFilename, sizeof(info.TextureFilename), "spark.tga");
	info.StartSize = 1.5f;
	info.EndSize = 3.0f;
	info.Lifetime = 2.0f;
	info.EmissionRate = 10.0f;
	info.Velocity = { 0, 0, 5 };
	info.StartColor = { 1, 2, 3, 4 };
	info.EndColor = { 9, 8, 7, 6 };
	em.chunk(W3D_CHUNK_EMITTER_INFO, ChunkWriter::of(info));

	if (!e.DropInfoV2)
	{
		W3dEmitterInfoStructV2 v2 = {};
		v2.BurstSize = 4;
		v2.CreationVolume.ClassID = 1;
		v2.CreationVolume.Value1 = 0.25f;
		v2.OutwardVel = 2.5f;
		v2.Shader.SrcBlend = W3DSHADER_SRCBLENDFUNC_ONE;
		v2.Shader.DestBlend = W3DSHADER_DESTBLENDFUNC_ONE;
		v2.RenderMode = W3D_EMITTER_RENDER_MODE_QUAD_PARTICLES;
		v2.FrameMode = 3;
		em.chunk(W3D_CHUNK_EMITTER_INFOV2, ChunkWriter::of(v2));
	}

	W3dEmitterPropertyStruct props = {};
	props.ColorKeyframes = e.ColorKeys;
	props.OpacityKeyframes = e.OpacityKeys;
	props.SizeKeyframes = e.SizeKeys;
	props.OpacityRandom = 0.5f;
	Bytes propBody;
	propBody.v = ChunkWriter::of(props);
	for (std::uint32_t i = 0; i < e.ColorKeys; ++i)
	{
		propBody.f32((float)i * 0.5f).u8((std::uint8_t)(10 + i)).u8(20).u8(30).u8(40);
	}
	for (std::uint32_t i = 0; i < e.OpacityKeys; ++i)
	{
		propBody.f32(0.25f).f32(0.75f);
	}
	for (std::uint32_t i = 0; i < e.SizeKeys; ++i)
	{
		propBody.f32((float)i).f32(2.0f * (float)i + 1.0f);
	}
	for (std::uint32_t i = 0; i < e.PropsExtraBytes; ++i)
	{
		propBody.u8(0);
	}
	em.chunk(W3D_CHUNK_EMITTER_PROPS, propBody.v);

	if (e.Line)
	{
		W3dEmitterLinePropertiesStruct line = {};
		line.Flags = W3D_ELINE_MERGE_INTERSECTIONS | W3D_ELINE_END_CAPS;
		line.SubdivisionLevel = 3;
		line.UPerSec = 0.5f;
		em.chunk(W3D_CHUNK_EMITTER_LINE_PROPERTIES, ChunkWriter::of(line));
	}
	if (e.Rotation)
	{
		W3dEmitterRotationHeaderStruct rh = {};
		rh.KeyframeCount = e.RotationKeys;
		rh.Random = 0.125f;
		rh.OrientationRandom = 0.5f;
		Bytes b;
		b.v = ChunkWriter::of(rh);
		for (std::uint32_t i = 0; i <= e.RotationKeys; ++i) // start key + KeyframeCount keys
		{
			b.f32((float)i).f32(10.0f * (float)(i + 1));
		}
		em.chunk(W3D_CHUNK_EMITTER_ROTATION_KEYFRAMES, b.v);
	}
	if (e.Frames)
	{
		W3dEmitterFrameHeaderStruct fh = {};
		fh.KeyframeCount = 0;
		fh.Random = 2.0f;
		Bytes b;
		b.v = ChunkWriter::of(fh);
		b.f32(0.0f).f32(7.0f); // only the start key
		em.chunk(W3D_CHUNK_EMITTER_FRAME_KEYFRAMES, b.v);
	}
	if (e.Blur)
	{
		W3dEmitterBlurTimeHeaderStruct bh = {};
		bh.KeyframeCount = 1;
		Bytes b;
		b.v = ChunkWriter::of(bh);
		b.f32(0.0f).f32(0.1f).f32(1.0f).f32(0.2f);
		em.chunk(W3D_CHUNK_EMITTER_BLUR_TIME_KEYFRAMES, b.v);
	}
	if (e.Extra)
	{
		W3dEmitterExtraInfoStruct x = {};
		x.FutureStartTime = 1.5f;
		em.chunk(W3D_CHUNK_EMITTER_EXTRA_INFO, ChunkWriter::of(x));
	}
	if (e.UnknownChunk)
	{
		em.chunk(e.UnknownChunk, { 1, 2, 3, 4 });
	}
	ChunkWriter file;
	file.wrapper(W3D_CHUNK_EMITTER, em);
	return file.bytes;
}

bool loadEmitter(ParticleEmitterDefClass &def, const std::vector<std::uint8_t> &bytes, std::string &error)
{
	ChunkLoadClass cload(bytes.data(), bytes.size());
	REQUIRE(cload.Open_Chunk());
	return def.Load_W3D(cload, &error);
}

} // namespace

TEST_CASE("emitter: header, user data, info, info v2 and keyframed properties")
{
	EmitterSpec spec;
	spec.UserStringBytes = 4;
	ParticleEmitterDefClass def;
	std::string error;
	REQUIRE_MESSAGE(loadEmitter(def, emitterBytes(spec), error), error);
	CHECK(def.Name == "E_TEST");
	CHECK(def.Version == 0x20000);
	CHECK(def.UserType == 7);
	CHECK(def.UserString == "abc"); // 4 string bytes after the 12 byte struct, NUL terminated
	CHECK(std::string(def.Info.TextureFilename) == "spark.tga");
	CHECK(def.Info.StartSize == 1.5f);
	CHECK(def.Info.EndSize == 3.0f);
	CHECK(def.Info.Velocity.Z == 5.0f);
	CHECK(def.Info.StartColor.B == 3);
	CHECK(def.Info.EndColor.R == 9);
	CHECK(def.InfoV2.BurstSize == 4);
	CHECK(def.InfoV2.CreationVolume.Value1 == 0.25f);
	CHECK(def.InfoV2.OutwardVel == 2.5f);
	CHECK(def.InfoV2.RenderMode == (std::uint32_t)W3D_EMITTER_RENDER_MODE_QUAD_PARTICLES);
	CHECK(def.InfoV2.FrameMode == 3);
	CHECK(def.Props.OpacityRandom == 0.5f);
	REQUIRE(def.ColorKeyframes.size() == 2);
	CHECK(def.ColorKeyframes[1].Time == 0.5f);
	CHECK(def.ColorKeyframes[1].Color.R == 11);
	REQUIRE(def.OpacityKeyframes.size() == 1);
	CHECK(def.OpacityKeyframes[0].Opacity == 0.75f);
	REQUIRE(def.SizeKeyframes.size() == 2);
	CHECK(def.SizeKeyframes[1].Size == 3.0f);
	CHECK_FALSE(def.HasLineProperties);
	CHECK_FALSE(def.HasRotation);
}

TEST_CASE("emitter: every optional section")
{
	EmitterSpec spec;
	spec.Line = spec.Rotation = spec.Frames = spec.Blur = spec.Extra = true;
	ParticleEmitterDefClass def;
	std::string error;
	REQUIRE_MESSAGE(loadEmitter(def, emitterBytes(spec), error), error);
	CHECK(def.HasLineProperties);
	CHECK(def.LineProperties.Flags == 0x9u);
	CHECK(def.LineProperties.SubdivisionLevel == 3);
	CHECK(def.LineProperties.UPerSec == 0.5f);
	CHECK(def.HasRotation);
	CHECK(def.RotationHeader.Random == 0.125f);
	CHECK(def.RotationHeader.OrientationRandom == 0.5f);
	REQUIRE(def.RotationKeyframes.size() == 2); // the start key and one more
	CHECK(def.RotationKeyframes[0].Rotation == 10.0f);
	CHECK(def.RotationKeyframes[1].Time == 1.0f);
	CHECK(def.RotationKeyframes[1].Rotation == 20.0f);
	CHECK(def.HasFrames);
	REQUIRE(def.FrameKeyframes.size() == 1);
	CHECK(def.FrameKeyframes[0].Frame == 7.0f);
	CHECK(def.FrameHeader.Random == 2.0f);
	CHECK(def.HasBlurTime);
	REQUIRE(def.BlurTimeKeyframes.size() == 2);
	CHECK(def.BlurTimeKeyframes[1].BlurTime == Approx(0.2f));
	CHECK(def.HasExtraInfo);
	CHECK(def.ExtraInfo.FutureStartTime == 1.5f);
}

TEST_CASE("emitter: malformed or foreign chunks are errors, not skipped")
{
	std::string error;
	ParticleEmitterDefClass def;
	{
		EmitterSpec spec;
		spec.Version = 0x10000;
		CHECK_FALSE(loadEmitter(def, emitterBytes(spec), error));
		CHECK(error.find("only version 2 emitters exist in retail") != std::string::npos);
	}
	{
		EmitterSpec spec;
		spec.DropInfoV2 = true;
		CHECK_FALSE(loadEmitter(def, emitterBytes(spec), error));
		CHECK(error.find("no W3D_CHUNK_EMITTER_INFOV2") != std::string::npos);
	}
	{
		EmitterSpec spec;
		spec.PropsExtraBytes = 3;
		CHECK_FALSE(loadEmitter(def, emitterBytes(spec), error));
		CHECK(error.find("3 bytes after its keyframes") != std::string::npos);
	}
	{
		EmitterSpec spec;
		spec.UnknownChunk = 0x5FF;
		CHECK_FALSE(loadEmitter(def, emitterBytes(spec), error));
		CHECK(error.find("unexpected chunk 1535") != std::string::npos);
	}
	{
		EmitterSpec spec;
		spec.Rotation = true;
		spec.RotationKeys = 3;
		std::vector<std::uint8_t> bytes = emitterBytes(spec);
		// claim one key more than the chunk holds: the rotation header's KeyframeCount is the 17th..20th byte of its body
		// (found by searching for the chunk id, then offset 8 into the chunk)
		for (size_t i = 0; i + 12 < bytes.size(); ++i)
		{
			std::uint32_t id;
			std::memcpy(&id, &bytes[i], 4);
			if (id == W3D_CHUNK_EMITTER_ROTATION_KEYFRAMES)
			{
				std::uint32_t wrong = 9;
				std::memcpy(&bytes[i + 8], &wrong, 4);
				break;
			}
		}
		CHECK_FALSE(loadEmitter(def, bytes, error));
		CHECK(error.find("header says 10") != std::string::npos);
	}
}

namespace
{

std::vector<std::uint8_t> shdMeshBytes(std::uint32_t declaredSubMeshes, std::uint32_t triangleBytes)
{
	ChunkWriter shd;
	shd.chunk(W3D_CHUNK_SHDMESH_NAME, { 'W', 'A', 'L', 'L', 0 });
	W3dShdMeshHeaderStruct h = {};
	h.Version = 0x00010000;
	h.NumTris = 1;
	h.NumVertices = 3;
	h.NumSubMeshes = declaredSubMeshes;
	h.SphRadius = 2.0f;
	shd.chunk(W3D_CHUNK_SHDMESH_HEADER, ChunkWriter::of(h));
	shd.chunk(W3D_CHUNK_SHDMESH_USER_TEXT, { 'h', 'i', 0 });

	ChunkWriter sub;
	W3dShdSubMeshHeaderStruct sh = {};
	sh.NumTris = 1;
	sh.NumVertices = 3;
	sub.chunk(W3D_CHUNK_SHDSUBMESH_HEADER, ChunkWriter::of(sh));
	ChunkWriter shader;
	shader.chunk(W3D_CHUNK_SHDSUBMESH_SHADER_CLASSID, Bytes().u32(5).v);
	ChunkWriter def;
	def.chunk(WWSHADE_CHUNK_SHDDEF_VARIABLES, { 9, 9, 9, 9 });
	shader.wrapper(W3D_CHUNK_SHDSUBMESH_SHADER_DEF, def);
	sub.wrapper(W3D_CHUNK_SHDSUBMESH_SHADER, shader);
	sub.chunk(W3D_CHUNK_SHDSUBMESH_VERTICES, ChunkWriter::ofArray(std::vector<W3dVectorStruct>{ { 0, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 } }));
	sub.chunk(W3D_CHUNK_SHDSUBMESH_VERTEX_NORMALS, ChunkWriter::ofArray(std::vector<W3dVectorStruct>(3, W3dVectorStruct{ 0, 0, 1 })));
	Bytes tris;
	for (std::uint32_t i = 0; i < triangleBytes / 2; ++i)
	{
		tris.u16((std::uint16_t)i);
	}
	sub.chunk(W3D_CHUNK_SHDSUBMESH_TRIANGLES, tris.v);
	sub.chunk(W3D_CHUNK_SHDSUBMESH_UV0, ChunkWriter::ofArray(std::vector<W3dTexCoordStruct>{ { 0, 0 }, { 1, 0 }, { 0, 1 } }));
	sub.chunk(W3D_CHUNK_SHDSUBMESH_TANGENT_BASIS_S, ChunkWriter::ofArray(std::vector<W3dVectorStruct>(3, W3dVectorStruct{ 1, 0, 0 })));
	shd.wrapper(W3D_CHUNK_SHDSUBMESH, sub);
	ChunkWriter file;
	file.wrapper(W3D_CHUNK_SHDMESH, shd);
	return file.bytes;
}

} // namespace

TEST_CASE("ShdMesh: header, name, sub-mesh arrays checked against the header counts")
{
	std::vector<std::uint8_t> bytes = shdMeshBytes(1, 6);
	ChunkLoadClass cload(bytes.data(), bytes.size());
	REQUIRE(cload.Open_Chunk());
	ShdMeshDefClass def;
	std::string error;
	REQUIRE_MESSAGE(def.Load_W3D(cload, &error), error);
	CHECK(def.Name == "WALL");
	CHECK(def.UserText == "hi");
	CHECK(def.Header.NumVertices == 3);
	CHECK(def.Header.SphRadius == 2.0f);
	REQUIRE(def.SubMeshes.size() == 1);
	const ShdSubMeshDef &s = def.SubMeshes[0];
	CHECK(s.ShaderClassId == 5);
	CHECK(s.ShaderDef.size() == 12); // one 4 byte WWShade chunk with its 8 byte header
	REQUIRE(s.Vertices.size() == 3);
	CHECK(s.Vertices[1].X == 1.0f);
	REQUIRE(s.Triangles.size() == 3);
	CHECK(s.Triangles[2] == 2);
	CHECK(s.UV0[2].V == 1.0f);
	CHECK(s.TangentBasisS.size() == 3);
	CHECK(s.UV1.empty());

	// it also loads as part of a file
	W3DFileContents contents;
	REQUIRE_MESSAGE(Load_W3D_File(bytes.data(), bytes.size(), contents, &error), error);
	REQUIRE(contents.ShdMeshes.size() == 1);
	CHECK(contents.ShdMeshes[0].Name == "WALL");
	CHECK(contents.OtherChunks.empty());
}

TEST_CASE("ShdMesh: a triangle array of the wrong size, or a missing sub-mesh, is refused")
{
	std::string error;
	{
		std::vector<std::uint8_t> bytes = shdMeshBytes(1, 4); // 2 indices for 1 triangle
		ChunkLoadClass cload(bytes.data(), bytes.size());
		REQUIRE(cload.Open_Chunk());
		ShdMeshDefClass def;
		CHECK_FALSE(def.Load_W3D(cload, &error));
		CHECK(error.find("W3D_CHUNK_SHDSUBMESH_TRIANGLES is 4 bytes, its header implies 6") != std::string::npos);
	}
	{
		std::vector<std::uint8_t> bytes = shdMeshBytes(2, 6); // header promises 2 sub-meshes
		ChunkLoadClass cload(bytes.data(), bytes.size());
		REQUIRE(cload.Open_Chunk());
		ShdMeshDefClass def;
		CHECK_FALSE(def.Load_W3D(cload, &error));
		CHECK(error.find("holds 1 sub-meshes, header says 2") != std::string::npos);
	}
}
