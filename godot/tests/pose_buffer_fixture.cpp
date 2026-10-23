// OpenBFME. GPL-3.0. Generate synthetic fixtures at runtime; never commit W3D bytes.
// c++ -std=c++17 -Iengine/tests -Iengine/src godot/tests/pose_buffer_fixture.cpp -o <scratch>/fixture
#include "W3dSynth.h"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <stdexcept>

using namespace w3dsynth;

static void write(const std::filesystem::path &root, const std::string &name, const std::vector<std::uint8_t> &bytes)
{
	const auto path = root / "art" / "w3d" / name.substr(0, 2) / (name + ".w3d");
	std::filesystem::create_directories(path.parent_path());
	std::ofstream file(path, std::ios::binary);
	file.write(reinterpret_cast<const char *>(bytes.data()), (std::streamsize)bytes.size());
	if (!file) throw std::runtime_error("cannot write fixture");
}

static SynthMesh mesh(const std::string &container, const std::string &name, bool skin, bool sorted)
{
	SynthMesh m;
	m.Container = container;
	m.Name = name;
	m.Vertices = { { 0, 0, 0 }, { 1, 0, 0 }, { 0, 0, 1 } };
	m.Triangles = { 0, 1, 2 };
	m.Shaders = { opaqueShader() };
	if (skin)
	{
		m.Attributes = W3D_MESH_FLAG_GEOMETRY_TYPE_SKIN;
		m.Influences.assign(3, W3dVertInfStruct{ 1, 0, 100, 0 });
	}
	if (sorted)
	{
		m.SortLevel = 1;
		m.Shaders[0].SrcBlend = 2;
		m.Shaders[0].DestBlend = 5;
		m.Shaders[0].DepthMask = 0;
	}
	m.VertexMaterials = { SynthVMat() };
	SynthPass pass;
	pass.ShaderIds = { 0 };
	pass.VertexMaterialIds = { 0 };
	m.Passes = { pass };
	return m;
}

int main(int argc, char **argv)
{
	if (argc != 2) return 2;
	const std::filesystem::path root(argv[1]);
	write(root, "pb_skl", hierarchyChunk("PB_SKL", {
		pivot("ROOT", 0xFFFFFFFFu, 0, 0, 0), pivot("RIGID", 0, 0, 0, 0), pivot("SKIN", 0, 0, 0, 0) }));
	for (const std::string name : { "PB_MIX", "PB_OPAQUE" })
	{
		std::vector<std::uint8_t> bytes;
		append(bytes, meshChunk(mesh(name, "RIGID", false, false)));
		append(bytes, meshChunk(mesh(name, "SKIN", true, false)));
		std::vector<SynthSub> subs = { { name + ".RIGID", 1 }, { name + ".SKIN", 2 } };
		if (name == "PB_MIX")
		{
			append(bytes, meshChunk(mesh(name, "SORTED", false, true)));
			subs.push_back({ name + ".SORTED", 1 });
		}
		append(bytes, hlodChunk(name.c_str(), "PB_SKL", subs));
		std::string lower = name;
		for (char &c : lower) c = (char)std::tolower((unsigned char)c);
		write(root, lower, bytes);
	}
	write(root, "pb_flat", meshChunk(mesh("PB_FLAT", "M", false, false)));
	write(root, "pb_move", rawAnimChunk("PB_MOVE", "PB_SKL", 4, 1,
		{ { ANIM_CHANNEL_Y, 1, 0, 1, { 0, 20, -20, 0 } } }));
	write(root, "pb_fade", rawAnimChunk("PB_FADE", "PB_SKL", 4, 1,
		{ { ANIM_CHANNEL_FADE, 1, 0, 1, { 1, 0.5f, 0, 1 } } }));
	// Raw visibility: visible, hidden, hidden, visible on pivot 2. Other pivots keep their default visibility.
	ChunkWriter anim;
	W3dAnimHeaderStruct ah = {};
	ah.Version = W3D_MAKE_VERSION(4, 1);
	setName(ah.Name, sizeof(ah.Name), "PB_VIS");
	setName(ah.HierarchyName, sizeof(ah.HierarchyName), "PB_SKL");
	ah.NumFrames = 4;
	ah.FrameRate = 1;
	anim.chunk(W3D_CHUNK_ANIMATION_HEADER, ChunkWriter::of(ah));
	Bytes vis;
	vis.u16(0).u16(3).u16(BIT_CHANNEL_VIS).u16(2).u8(1).u8(0x09);
	anim.chunk(W3D_CHUNK_BIT_CHANNEL, vis.v);
	ChunkWriter file;
	file.wrapper(W3D_CHUNK_ANIMATION, anim);
	write(root, "pb_vis", file.bytes);
}
