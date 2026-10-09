// OpenBFME unit tests. GPL-3.0.
// Synthetic W3D files: meshes with passes / vertex materials / textures / skins, hierarchies, HLODs and raw animations,
// built byte by byte from the format (w3d_file.h) so render-side tests never need retail files.

#pragma once

#include "W3dTestUtil.h"

#include "Libraries/WWVegas/WW3D2/assetmgr.h"
#include "Libraries/WWVegas/WW3D2/w3d_file.h"

#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace w3dsynth
{

using namespace w3dtest;

struct SynthVMat
{
	std::string Name = "VM";
	std::uint32_t Attributes = 0;
	std::uint8_t Diffuse[3] = { 255, 255, 255 };
	std::uint8_t Emissive[3] = { 0, 0, 0 };
	float Shininess = 0.0f;
	float Opacity = 1.0f;
	std::string Args0, Args1;
};

struct SynthTexture
{
	std::string Name;
	bool HasInfo = false;
	std::uint16_t InfoAttributes = 0;
};

struct SynthStage
{
	std::vector<std::uint32_t> TextureIds;
	std::vector<W3dTexCoordStruct> TexCoords;
};

struct SynthPass
{
	std::vector<std::uint32_t> VertexMaterialIds;
	std::vector<std::uint32_t> ShaderIds;
	std::vector<W3dRGBAStruct> DCG;
	std::vector<SynthStage> Stages;
	std::vector<std::uint32_t> ShaderMaterialIds;
	std::vector<W3dTexCoordStruct> PassTexCoords;
};

struct SynthMesh
{
	std::string Name = "M";
	std::string Container = "C";
	std::uint32_t Attributes = 0;
	std::int32_t SortLevel = 0;
	std::vector<W3dVectorStruct> Vertices;
	std::vector<W3dVectorStruct> Normals; // default (0,0,1)
	std::vector<std::uint32_t> Triangles; // 3 vertex indices per triangle
	std::vector<W3dVertInfStruct> Influences;
	std::vector<W3dVectorStruct> Vertices2, Normals2;
	std::vector<W3dShaderStruct> Shaders;
	std::vector<SynthVMat> VertexMaterials;
	std::vector<SynthTexture> Textures;
	std::vector<SynthPass> Passes;
};

inline std::vector<std::uint8_t> nameBody(const std::string &s)
{
	std::vector<std::uint8_t> b(s.begin(), s.end());
	b.push_back(0);
	return b;
}

// A top-level W3D_CHUNK_MESH chunk (header + body).
inline std::vector<std::uint8_t> meshChunk(const SynthMesh &m)
{
	ChunkWriter mesh;
	W3dMeshHeader3Struct mh = {};
	mh.Version = W3D_MAKE_VERSION(5, 0);
	mh.Attributes = m.Attributes;
	mh.SortLevel = m.SortLevel;
	setName(mh.MeshName, sizeof(mh.MeshName), m.Name.c_str());
	setName(mh.ContainerName, sizeof(mh.ContainerName), m.Container.c_str());
	mh.NumTris = (std::uint32_t)(m.Triangles.size() / 3);
	mh.NumVertices = (std::uint32_t)m.Vertices.size();
	mh.VertexChannels = 3;
	mesh.chunk(W3D_CHUNK_MESH_HEADER3, ChunkWriter::of(mh));
	mesh.chunk(W3D_CHUNK_VERTICES, ChunkWriter::ofArray(m.Vertices));
	std::vector<W3dVectorStruct> normals = m.Normals;
	if (normals.empty())
	{
		normals.assign(m.Vertices.size(), W3dVectorStruct{ 0, 0, 1 });
	}
	mesh.chunk(W3D_CHUNK_VERTEX_NORMALS, ChunkWriter::ofArray(normals));
	std::vector<W3dTriStruct> tris(m.Triangles.size() / 3);
	for (size_t t = 0; t < tris.size(); ++t)
	{
		tris[t] = {};
		tris[t].Vindex[0] = m.Triangles[t * 3];
		tris[t].Vindex[1] = m.Triangles[t * 3 + 1];
		tris[t].Vindex[2] = m.Triangles[t * 3 + 2];
	}
	mesh.chunk(W3D_CHUNK_TRIANGLES, ChunkWriter::ofArray(tris));
	if (!m.Influences.empty()) mesh.chunk(W3D_CHUNK_VERTEX_INFLUENCES, ChunkWriter::ofArray(m.Influences));
	if (!m.Vertices2.empty()) mesh.chunk(W3D_CHUNK_VERTICES_2, ChunkWriter::ofArray(m.Vertices2));
	if (!m.Normals2.empty()) mesh.chunk(W3D_CHUNK_VERTEX_NORMALS_2, ChunkWriter::ofArray(m.Normals2));

	if (!m.Passes.empty())
	{
		W3dMaterialInfoStruct mi = {};
		mi.PassCount = (std::uint32_t)m.Passes.size();
		mi.VertexMaterialCount = (std::uint32_t)m.VertexMaterials.size();
		mi.ShaderCount = (std::uint32_t)m.Shaders.size();
		mi.TextureCount = (std::uint32_t)m.Textures.size();
		mesh.chunk(W3D_CHUNK_MATERIAL_INFO, ChunkWriter::of(mi));
	}
	if (!m.Shaders.empty()) mesh.chunk(W3D_CHUNK_SHADERS, ChunkWriter::ofArray(m.Shaders));
	if (!m.VertexMaterials.empty())
	{
		ChunkWriter vmats;
		for (const SynthVMat &vm : m.VertexMaterials)
		{
			ChunkWriter one;
			one.chunk(W3D_CHUNK_VERTEX_MATERIAL_NAME, nameBody(vm.Name));
			W3dVertexMaterialStruct info = {};
			info.Attributes = vm.Attributes;
			std::memcpy(&info.Diffuse, vm.Diffuse, 3);
			std::memcpy(&info.Emissive, vm.Emissive, 3);
			info.Shininess = vm.Shininess;
			info.Opacity = vm.Opacity;
			one.chunk(W3D_CHUNK_VERTEX_MATERIAL_INFO, ChunkWriter::of(info));
			if (!vm.Args0.empty()) one.chunk(W3D_CHUNK_VERTEX_MAPPER_ARGS0, nameBody(vm.Args0));
			if (!vm.Args1.empty()) one.chunk(W3D_CHUNK_VERTEX_MAPPER_ARGS1, nameBody(vm.Args1));
			vmats.wrapper(W3D_CHUNK_VERTEX_MATERIAL, one);
		}
		mesh.wrapper(W3D_CHUNK_VERTEX_MATERIALS, vmats);
	}
	if (!m.Textures.empty())
	{
		ChunkWriter textures;
		for (const SynthTexture &t : m.Textures)
		{
			ChunkWriter one;
			one.chunk(W3D_CHUNK_TEXTURE_NAME, nameBody(t.Name));
			if (t.HasInfo)
			{
				W3dTextureInfoStruct ti = {};
				ti.Attributes = t.InfoAttributes;
				one.chunk(W3D_CHUNK_TEXTURE_INFO, ChunkWriter::of(ti));
			}
			textures.wrapper(W3D_CHUNK_TEXTURE, one);
		}
		mesh.wrapper(W3D_CHUNK_TEXTURES, textures);
	}
	for (const SynthPass &p : m.Passes)
	{
		ChunkWriter pass;
		if (!p.VertexMaterialIds.empty()) pass.chunk(W3D_CHUNK_VERTEX_MATERIAL_IDS, ChunkWriter::ofArray(p.VertexMaterialIds));
		if (!p.ShaderIds.empty()) pass.chunk(W3D_CHUNK_SHADER_IDS, ChunkWriter::ofArray(p.ShaderIds));
		if (!p.DCG.empty()) pass.chunk(W3D_CHUNK_DCG, ChunkWriter::ofArray(p.DCG));
		if (!p.ShaderMaterialIds.empty()) pass.chunk(W3D_CHUNK_SHADER_MATERIAL_ID, ChunkWriter::ofArray(p.ShaderMaterialIds));
		if (!p.PassTexCoords.empty()) pass.chunk(W3D_CHUNK_STAGE_TEXCOORDS, ChunkWriter::ofArray(p.PassTexCoords));
		for (const SynthStage &s : p.Stages)
		{
			ChunkWriter stage;
			if (!s.TextureIds.empty()) stage.chunk(W3D_CHUNK_TEXTURE_IDS, ChunkWriter::ofArray(s.TextureIds));
			if (!s.TexCoords.empty()) stage.chunk(W3D_CHUNK_STAGE_TEXCOORDS, ChunkWriter::ofArray(s.TexCoords));
			pass.wrapper(W3D_CHUNK_TEXTURE_STAGE, stage);
		}
		mesh.wrapper(W3D_CHUNK_MATERIAL_PASS, pass);
	}
	ChunkWriter file;
	file.wrapper(W3D_CHUNK_MESH, mesh);
	return file.bytes;
}

inline W3dPivotStruct pivot(const char *name, std::uint32_t parent, float x, float y, float z)
{
	W3dPivotStruct p = {};
	setName(p.Name, W3D_NAME_LEN, name);
	p.ParentIdx = parent;
	p.Translation = { x, y, z };
	p.Rotation.Q[3] = 1.0f;
	return p;
}

inline std::vector<std::uint8_t> hierarchyChunk(const char *name, const std::vector<W3dPivotStruct> &pivots)
{
	ChunkWriter hier;
	W3dHierarchyStruct hh = {};
	hh.Version = W3D_MAKE_VERSION(4, 1);
	setName(hh.Name, sizeof(hh.Name), name);
	hh.NumPivots = (std::uint32_t)pivots.size();
	hier.chunk(W3D_CHUNK_HIERARCHY_HEADER, ChunkWriter::of(hh));
	hier.chunk(W3D_CHUNK_PIVOTS, ChunkWriter::ofArray(pivots));
	ChunkWriter file;
	file.wrapper(W3D_CHUNK_HIERARCHY, hier);
	return file.bytes;
}

struct SynthSub
{
	std::string Name;
	std::uint32_t Bone;
};

inline std::vector<std::uint8_t> hlodChunk(const char *name, const char *hierarchy, const std::vector<SynthSub> &lod,
	const std::vector<SynthSub> &aggregates = {})
{
	auto array = [](std::uint32_t id, const std::vector<SynthSub> &subs) {
		ChunkWriter a;
		W3dHLodArrayHeaderStruct ah = {};
		ah.ModelCount = (std::uint32_t)subs.size();
		ah.MaxScreenSize = 0.0f;
		a.chunk(W3D_CHUNK_HLOD_SUB_OBJECT_ARRAY_HEADER, ChunkWriter::of(ah));
		for (const SynthSub &s : subs)
		{
			W3dHLodSubObjectStruct so = {};
			so.BoneIndex = s.Bone;
			setName(so.Name, sizeof(so.Name), s.Name.c_str());
			a.chunk(W3D_CHUNK_HLOD_SUB_OBJECT, ChunkWriter::of(so));
		}
		ChunkWriter w;
		w.wrapper(id, a);
		return w.bytes;
	};
	ChunkWriter h;
	W3dHLodHeaderStruct hh = {};
	hh.Version = W3D_MAKE_VERSION(1, 0);
	hh.LodCount = 1;
	setName(hh.Name, sizeof(hh.Name), name);
	setName(hh.HierarchyName, sizeof(hh.HierarchyName), hierarchy);
	h.chunk(W3D_CHUNK_HLOD_HEADER, ChunkWriter::of(hh));
	std::vector<std::uint8_t> l = array(W3D_CHUNK_HLOD_LOD_ARRAY, lod);
	h.bytes.insert(h.bytes.end(), l.begin(), l.end());
	if (!aggregates.empty())
	{
		std::vector<std::uint8_t> a = array(W3D_CHUNK_HLOD_AGGREGATE_ARRAY, aggregates);
		h.bytes.insert(h.bytes.end(), a.begin(), a.end());
	}
	ChunkWriter file;
	file.wrapper(W3D_CHUNK_HLOD, h);
	return file.bytes;
}

// One raw channel: type (ANIM_CHANNEL_*), pivot, values for frames first..first+n-1 (vector length 1 for X/Y/Z, 4 for Q).
struct SynthChannel
{
	std::uint16_t Type;
	std::uint16_t Pivot;
	std::uint16_t First;
	std::uint16_t VectorLen;
	std::vector<float> Values;
};

inline std::vector<std::uint8_t> rawAnimChunk(const char *name, const char *hierarchy, std::uint32_t frames, std::uint32_t rate,
	const std::vector<SynthChannel> &channels)
{
	ChunkWriter anim;
	W3dAnimHeaderStruct ah = {};
	ah.Version = W3D_MAKE_VERSION(4, 1);
	setName(ah.Name, sizeof(ah.Name), name);
	setName(ah.HierarchyName, sizeof(ah.HierarchyName), hierarchy);
	ah.NumFrames = frames;
	ah.FrameRate = rate;
	anim.chunk(W3D_CHUNK_ANIMATION_HEADER, ChunkWriter::of(ah));
	for (const SynthChannel &c : channels)
	{
		Bytes b;
		const std::uint16_t n = (std::uint16_t)(c.Values.size() / c.VectorLen);
		b.u16(c.First).u16((std::uint16_t)(c.First + n - 1)).u16(c.VectorLen).u16(c.Type).u16(c.Pivot).u16(0);
		for (float f : c.Values) b.f32(f);
		anim.chunk(W3D_CHUNK_ANIMATION_CHANNEL, b.v);
	}
	ChunkWriter file;
	file.wrapper(W3D_CHUNK_ANIMATION, anim);
	return file.bytes;
}

inline void append(std::vector<std::uint8_t> &dst, const std::vector<std::uint8_t> &src)
{
	dst.insert(dst.end(), src.begin(), src.end());
}

// A W3D file system held in memory: lower-cased virtual path -> bytes.
class MemoryFileSource : public W3DFileSource
{
public:
	void Add(const std::string &path, const std::vector<std::uint8_t> &bytes) { Files[path] = bytes; }
	bool Exists(const std::string &path) const override { return Files.count(path) != 0; }
	bool Read(const std::string &path, std::vector<std::uint8_t> &out, std::string *error) override
	{
		auto it = Files.find(path);
		if (it == Files.end())
		{
			if (error) *error = "no such file " + path;
			return false;
		}
		out = it->second;
		return true;
	}
	std::map<std::string, std::vector<std::uint8_t>> Files;
};

inline W3dShaderStruct opaqueShader()
{
	W3dShaderStruct s = {};
	s.DepthCompare = 3;
	s.DepthMask = 1;
	s.ColorMask = 1;
	s.SrcBlend = 1;
	s.DestBlend = 0;
	s.PriGradient = 1;
	s.Texturing = 1;
	return s;
}

} // namespace w3dsynth
