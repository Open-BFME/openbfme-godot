// OpenBFME: faithful rebuild of The Battle for Middle-earth II: Rise of the Witch-king 2.01.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// Port of the chunk-reading half of ZH Libraries/Source/WWVegas/WW3D2/meshmdlio.cpp
// (MeshModelClass::Load_W3D, read_chunks and the read_* helpers). Chunk ids and structures
// are the ZH ones; data is kept raw (see meshmdl.h).

#include "Libraries/WWVegas/WW3D2/meshmdl.h"
#include "Libraries/WWVegas/WWLib/chunkio.h"

#include <cstring>

namespace
{

std::string fixedName(const char *src, size_t len)
{
	size_t n = 0;
	while (n < len && src[n] != 0)
	{
		++n;
	}
	return std::string(src, n);
}

std::string readString(ChunkLoadClass &cload)
{
	std::string s(cload.Cur_Chunk_Length(), '\0');
	if (!s.empty())
	{
		cload.Read(&s[0], (std::uint32_t)s.size());
	}
	size_t nul = s.find('\0');
	if (nul != std::string::npos)
	{
		s.resize(nul);
	}
	return s;
}

template <typename T>
bool readArray(ChunkLoadClass &cload, std::vector<T> &out, const char *what, std::string *error)
{
	std::uint32_t len = cload.Cur_Chunk_Length();
	if (len % sizeof(T) != 0)
	{
		if (error)
		{
			*error = std::string("chunk ") + what + " length " + std::to_string(len) + " is not a multiple of " + std::to_string(sizeof(T));
		}
		return false;
	}
	out.resize(len / sizeof(T));
	if (len > 0 && cload.Read(out.data(), len) != len)
	{
		if (error)
		{
			*error = std::string("short read of chunk ") + what;
		}
		return false;
	}
	return true;
}

} // namespace

std::string MeshModelClass::Get_Name() const
{
	if (ContainerName.empty())
	{
		return MeshName;
	}
	return ContainerName + "." + MeshName;
}

bool MeshModelClass::Load_W3D(ChunkLoadClass &cload, std::string *error)
{
	// ZH: the first chunk must be the mesh header.
	if (!cload.Open_Chunk())
	{
		if (error)
		{
			*error = "empty mesh chunk";
		}
		return false;
	}
	if (cload.Cur_Chunk_ID() != W3D_CHUNK_MESH_HEADER3)
	{
		if (error)
		{
			*error = "mesh does not start with W3D_CHUNK_MESH_HEADER3 (old mesh formats are not ported)";
		}
		return false;
	}
	if (cload.Read(&Header, sizeof(Header)) != sizeof(Header))
	{
		if (error)
		{
			*error = "short mesh header";
		}
		return false;
	}
	cload.Close_Chunk();

	MeshName = fixedName(Header.MeshName, W3D_NAME_LEN);
	ContainerName = fixedName(Header.ContainerName, W3D_NAME_LEN);

	if (!read_chunks(cload, error))
	{
		if (error)
		{
			*error = "mesh " + Get_Name() + ": " + *error;
		}
		return false;
	}

	// The header is the inventory of what follows; the file must agree with it.
	if (Vertices.size() != Header.NumVertices || Triangles.size() != Header.NumTris)
	{
		if (error)
		{
			*error = "mesh " + Get_Name() + ": header says " + std::to_string(Header.NumVertices) + " vertices / " +
				std::to_string(Header.NumTris) + " triangles, chunks hold " + std::to_string(Vertices.size()) + " / " +
				std::to_string(Triangles.size());
		}
		return false;
	}
	// Per-vertex streams must be as long as the vertex array (spec 5.4 item 4); a mismatch is a malformed
	// file, not something to truncate or pad.
	auto streamMatches = [&](size_t count, const char *what) {
		if (count != 0 && count != Header.NumVertices)
		{
			if (error)
			{
				*error = "mesh " + Get_Name() + ": " + what + " holds " + std::to_string(count) + " entries for " +
					std::to_string(Header.NumVertices) + " vertices";
			}
			return false;
		}
		return true;
	};
	if (!streamMatches(Influences.size(), "W3D_CHUNK_VERTEX_INFLUENCES") || !streamMatches(Normals.size(), "W3D_CHUNK_VERTEX_NORMALS") ||
		!streamMatches(Vertices2.size(), "W3D_CHUNK_VERTICES_2") || !streamMatches(Normals2.size(), "W3D_CHUNK_VERTEX_NORMALS_2") ||
		!streamMatches(Tangents.size(), "W3D_CHUNK_TANGENTS") || !streamMatches(Bitangents.size(), "W3D_CHUNK_BITANGENTS"))
	{
		return false;
	}
	if (Vertices2.size() != Normals2.size())
	{
		if (error)
		{
			*error = "mesh " + Get_Name() + ": W3D_CHUNK_VERTICES_2 and W3D_CHUNK_VERTEX_NORMALS_2 differ in length";
		}
		return false;
	}
	if (Tangents.size() != Bitangents.size())
	{
		if (error)
		{
			*error = "mesh " + Get_Name() + ": W3D_CHUNK_TANGENTS and W3D_CHUNK_BITANGENTS differ in length";
		}
		return false;
	}
	for (const W3dTriStruct &tri : Triangles)
	{
		for (int k = 0; k < 3; ++k)
		{
			if (tri.Vindex[k] >= Header.NumVertices)
			{
				if (error)
				{
					*error = "mesh " + Get_Name() + ": triangle vertex index out of range";
				}
				return false;
			}
		}
	}
	return true;
}

bool MeshModelClass::read_chunks(ChunkLoadClass &cload, std::string *error)
{
	while (cload.Open_Chunk())
	{
		bool ok = true;
		switch (cload.Cur_Chunk_ID())
		{
		case W3D_CHUNK_VERTICES:
			ok = readArray(cload, Vertices, "W3D_CHUNK_VERTICES", error);
			break;
		case W3D_CHUNK_VERTEX_NORMALS:
			ok = readArray(cload, Normals, "W3D_CHUNK_VERTEX_NORMALS", error);
			break;
		case W3D_CHUNK_TRIANGLES:
			ok = readArray(cload, Triangles, "W3D_CHUNK_TRIANGLES", error);
			break;
		case W3D_CHUNK_VERTEX_INFLUENCES:
			ok = readArray(cload, Influences, "W3D_CHUNK_VERTEX_INFLUENCES", error);
			break;
		case W3D_CHUNK_VERTEX_SHADE_INDICES:
			ok = readArray(cload, ShadeIndices, "W3D_CHUNK_VERTEX_SHADE_INDICES", error);
			break;
		case W3D_CHUNK_VERTICES_2:
			ok = readArray(cload, Vertices2, "W3D_CHUNK_VERTICES_2", error);
			break;
		case W3D_CHUNK_VERTEX_NORMALS_2:
			ok = readArray(cload, Normals2, "W3D_CHUNK_VERTEX_NORMALS_2", error);
			break;
		case W3D_CHUNK_TANGENTS:
			ok = readArray(cload, Tangents, "W3D_CHUNK_TANGENTS", error);
			break;
		case W3D_CHUNK_BITANGENTS:
			ok = readArray(cload, Bitangents, "W3D_CHUNK_BITANGENTS", error);
			break;
		case W3D_CHUNK_SHADER_MATERIALS:
			ok = read_shader_materials(cload, error);
			break;
		case W3D_CHUNK_AABTREE:
			ok = read_aabtree(cload, error);
			break;
		case W3D_CHUNK_MESH_USER_TEXT:
			UserText = readString(cload);
			break;
		case W3D_CHUNK_MATERIAL_INFO:
			if (cload.Read(&MaterialInfo, sizeof(MaterialInfo)) != sizeof(MaterialInfo))
			{
				ok = false;
				if (error)
				{
					*error = "short W3D_CHUNK_MATERIAL_INFO";
				}
			}
			break;
		case W3D_CHUNK_SHADERS:
			ok = readArray(cload, Shaders, "W3D_CHUNK_SHADERS", error);
			break;
		case W3D_CHUNK_VERTEX_MATERIALS:
			ok = read_vertex_materials(cload, error);
			break;
		case W3D_CHUNK_TEXTURES:
			ok = read_textures(cload, error);
			break;
		case W3D_CHUNK_MATERIAL_PASS:
			ok = read_material_pass(cload, error);
			break;
		default:
			// AABTree, deform, PS2 shaders, prelit wrappers, ...: not needed to draw the bind
			// pose yet. Listed so nobody mistakes them for handled.
			UnhandledChunks.push_back(cload.Cur_Chunk_ID());
			break;
		}
		if (!ok)
		{
			return false;
		}
		cload.Close_Chunk();
	}
	if (cload.Had_Error())
	{
		if (error)
		{
			*error = "malformed chunk";
		}
		return false;
	}
	return true;
}

bool MeshModelClass::read_vertex_materials(ChunkLoadClass &cload, std::string *error)
{
	while (cload.Open_Chunk())
	{
		if (cload.Cur_Chunk_ID() == W3D_CHUNK_VERTEX_MATERIAL)
		{
			MeshVertexMaterialDef def;
			while (cload.Open_Chunk())
			{
				switch (cload.Cur_Chunk_ID())
				{
				case W3D_CHUNK_VERTEX_MATERIAL_NAME:
					def.Name = readString(cload);
					break;
				case W3D_CHUNK_VERTEX_MATERIAL_INFO:
					if (cload.Read(&def.Info, sizeof(def.Info)) != sizeof(def.Info))
					{
						if (error)
						{
							*error = "short W3D_CHUNK_VERTEX_MATERIAL_INFO";
						}
						return false;
					}
					break;
				case W3D_CHUNK_VERTEX_MAPPER_ARGS0:
					def.MapperArgs0 = readString(cload);
					break;
				case W3D_CHUNK_VERTEX_MAPPER_ARGS1:
					def.MapperArgs1 = readString(cload);
					break;
				default:
					UnhandledChunks.push_back(cload.Cur_Chunk_ID());
					break;
				}
				cload.Close_Chunk();
			}
			VertexMaterials.push_back(def);
		}
		else
		{
			UnhandledChunks.push_back(cload.Cur_Chunk_ID());
		}
		cload.Close_Chunk();
	}
	return true;
}

bool MeshModelClass::read_textures(ChunkLoadClass &cload, std::string *error)
{
	while (cload.Open_Chunk())
	{
		if (cload.Cur_Chunk_ID() == W3D_CHUNK_TEXTURE)
		{
			MeshTextureDef def;
			while (cload.Open_Chunk())
			{
				switch (cload.Cur_Chunk_ID())
				{
				case W3D_CHUNK_TEXTURE_NAME:
					def.Name = readString(cload);
					break;
				case W3D_CHUNK_TEXTURE_INFO:
					if (cload.Read(&def.Info, sizeof(def.Info)) != sizeof(def.Info))
					{
						if (error)
						{
							*error = "short W3D_CHUNK_TEXTURE_INFO";
						}
						return false;
					}
					def.HasInfo = true;
					break;
				default:
					UnhandledChunks.push_back(cload.Cur_Chunk_ID());
					break;
				}
				cload.Close_Chunk();
			}
			Textures.push_back(def);
		}
		else
		{
			UnhandledChunks.push_back(cload.Cur_Chunk_ID());
		}
		cload.Close_Chunk();
	}
	return true;
}

bool MeshModelClass::read_material_pass(ChunkLoadClass &cload, std::string *error)
{
	MeshMaterialPassData pass;
	while (cload.Open_Chunk())
	{
		bool ok = true;
		switch (cload.Cur_Chunk_ID())
		{
		case W3D_CHUNK_VERTEX_MATERIAL_IDS:
			ok = readArray(cload, pass.VertexMaterialIds, "W3D_CHUNK_VERTEX_MATERIAL_IDS", error);
			break;
		case W3D_CHUNK_SHADER_IDS:
			ok = readArray(cload, pass.ShaderIds, "W3D_CHUNK_SHADER_IDS", error);
			break;
		case W3D_CHUNK_DCG:
			ok = readArray(cload, pass.DCG, "W3D_CHUNK_DCG", error);
			break;
		case W3D_CHUNK_SHADER_MATERIAL_ID:
			ok = readArray(cload, pass.ShaderMaterialIds, "W3D_CHUNK_SHADER_MATERIAL_ID", error);
			break;
		case W3D_CHUNK_STAGE_TEXCOORDS:
			ok = readArray(cload, pass.TexCoords, "W3D_CHUNK_STAGE_TEXCOORDS", error);
			break;
		case W3D_CHUNK_TEXTURE_STAGE:
		{
			MeshTextureStageData stage;
			ok = read_texture_stage(cload, stage, error);
			pass.Stages.push_back(stage);
			break;
		}
		default:
			UnhandledChunks.push_back(cload.Cur_Chunk_ID());
			break;
		}
		if (!ok)
		{
			return false;
		}
		cload.Close_Chunk();
	}
	Passes.push_back(pass);
	return true;
}

bool MeshModelClass::read_texture_stage(ChunkLoadClass &cload, MeshTextureStageData &stage, std::string *error)
{
	while (cload.Open_Chunk())
	{
		bool ok = true;
		switch (cload.Cur_Chunk_ID())
		{
		case W3D_CHUNK_TEXTURE_IDS:
			ok = readArray(cload, stage.TextureIds, "W3D_CHUNK_TEXTURE_IDS", error);
			break;
		case W3D_CHUNK_STAGE_TEXCOORDS:
			ok = readArray(cload, stage.TexCoords, "W3D_CHUNK_STAGE_TEXCOORDS", error);
			break;
		default:
			UnhandledChunks.push_back(cload.Cur_Chunk_ID());
			break;
		}
		if (!ok)
		{
			return false;
		}
		cload.Close_Chunk();
	}
	return true;
}

// BFME2 FX shader materials: W3D_CHUNK_SHADER_MATERIALS { W3D_CHUNK_SHADER_MATERIAL { HEADER, PROPERTY... }... }.
// Layouts: OpenSAGE W3dShaderMaterialHeader.cs / W3dShaderMaterialProperty.cs, checked against all 8,609
// materials and 67,602 properties of the retail corpus (every chunk must be consumed exactly).
bool MeshModelClass::read_shader_materials(ChunkLoadClass &cload, std::string *error)
{
	while (cload.Open_Chunk())
	{
		if (cload.Cur_Chunk_ID() != W3D_CHUNK_SHADER_MATERIAL)
		{
			UnhandledChunks.push_back(cload.Cur_Chunk_ID());
			cload.Close_Chunk();
			continue;
		}
		MeshShaderMaterialDef def;
		bool haveHeader = false;
		while (cload.Open_Chunk())
		{
			switch (cload.Cur_Chunk_ID())
			{
			case W3D_CHUNK_SHADER_MATERIAL_HEADER:
			{
				W3dShaderMaterialHeaderStruct h;
				if (cload.Cur_Chunk_Length() != sizeof(h) || cload.Read(&h, sizeof(h)) != sizeof(h))
				{
					if (error)
					{
						*error = "W3D_CHUNK_SHADER_MATERIAL_HEADER is not " + std::to_string(sizeof(h)) + " bytes";
					}
					return false;
				}
				def.Number = h.Number;
				def.TypeName = fixedName(h.TypeName, sizeof(h.TypeName));
				haveHeader = true;
				break;
			}
			case W3D_CHUNK_SHADER_MATERIAL_PROPERTY:
			{
				std::uint32_t len = cload.Cur_Chunk_Length();
				std::vector<std::uint8_t> raw(len);
				if (len > 0 && cload.Read(raw.data(), len) != len)
				{
					if (error)
					{
						*error = "short W3D_CHUNK_SHADER_MATERIAL_PROPERTY";
					}
					return false;
				}
				MeshShaderMaterialProperty prop;
				size_t at = 0;
				auto take32 = [&](std::uint32_t &out) {
					if (at + 4 > raw.size())
					{
						return false;
					}
					std::memcpy(&out, &raw[at], 4);
					at += 4;
					return true;
				};
				auto takeString = [&](std::string &out) {
					std::uint32_t n;
					if (!take32(n) || at + n > raw.size())
					{
						return false;
					}
					out.assign(reinterpret_cast<const char *>(&raw[at]), n);
					at += n;
					size_t nul = out.find('\0');
					if (nul != std::string::npos)
					{
						out.resize(nul);
					}
					return true;
				};
				bool ok = take32(prop.Type) && takeString(prop.Name);
				int floats = 0;
				if (ok)
				{
					switch (prop.Type)
					{
					case W3DSHADERMATERIAL_PROPERTY_TEXTURE: ok = takeString(prop.StringValue); break;
					case W3DSHADERMATERIAL_PROPERTY_FLOAT: floats = 1; break;
					case W3DSHADERMATERIAL_PROPERTY_VECTOR2: floats = 2; break;
					case W3DSHADERMATERIAL_PROPERTY_VECTOR3: floats = 3; break;
					case W3DSHADERMATERIAL_PROPERTY_VECTOR4: floats = 4; break;
					case W3DSHADERMATERIAL_PROPERTY_INT:
					{
						std::uint32_t v;
						ok = take32(v);
						prop.Int = (sint32)v;
						break;
					}
					case W3DSHADERMATERIAL_PROPERTY_BOOL:
					{
						// One byte, 0 or 1 (retail chunks are 1 byte longer than a name + type + length header
						// + a 32 bit value would make them). OpenSAGE reads it with ReadBooleanChecked.
						if (at + 1 > raw.size() || raw[at] > 1)
						{
							ok = false;
							break;
						}
						prop.Bool = raw[at] != 0;
						at += 1;
						break;
					}
					default:
						ok = false;
						break;
					}
				}
				for (int i = 0; ok && i < floats; ++i)
				{
					std::uint32_t bits;
					ok = take32(bits);
					std::memcpy(&prop.Float[i], &bits, 4);
				}
				if (!ok || at != raw.size())
				{
					if (error)
					{
						*error = "malformed W3D_CHUNK_SHADER_MATERIAL_PROPERTY (type " + std::to_string(prop.Type) + ", " +
							std::to_string(raw.size()) + " bytes, " + std::to_string(at) + " read)";
					}
					return false;
				}
				def.Properties.push_back(prop);
				break;
			}
			default:
				UnhandledChunks.push_back(cload.Cur_Chunk_ID());
				break;
			}
			cload.Close_Chunk();
		}
		if (!haveHeader)
		{
			if (error)
			{
				*error = "W3D_CHUNK_SHADER_MATERIAL without a header";
			}
			return false;
		}
		ShaderMaterials.push_back(def);
		cload.Close_Chunk();
	}
	return true;
}

// ZH W3D_CHUNK_AABTREE: header, polygon indices, nodes (aabtree.cpp). Counts must match the chunk lengths.
bool MeshModelClass::read_aabtree(ChunkLoadClass &cload, std::string *error)
{
	bool haveHeader = false;
	while (cload.Open_Chunk())
	{
		bool ok = true;
		switch (cload.Cur_Chunk_ID())
		{
		case W3D_CHUNK_AABTREE_HEADER:
			if (cload.Cur_Chunk_Length() != sizeof(AABTreeHeader) || cload.Read(&AABTreeHeader, sizeof(AABTreeHeader)) != sizeof(AABTreeHeader))
			{
				ok = false;
				if (error)
				{
					*error = "W3D_CHUNK_AABTREE_HEADER is not 32 bytes";
				}
			}
			haveHeader = ok;
			break;
		case W3D_CHUNK_AABTREE_POLYINDICES:
			ok = readArray(cload, AABTreePolyIndices, "W3D_CHUNK_AABTREE_POLYINDICES", error);
			break;
		case W3D_CHUNK_AABTREE_NODES:
			ok = readArray(cload, AABTreeNodes, "W3D_CHUNK_AABTREE_NODES", error);
			break;
		default:
			UnhandledChunks.push_back(cload.Cur_Chunk_ID());
			break;
		}
		if (!ok)
		{
			return false;
		}
		cload.Close_Chunk();
	}
	if (!haveHeader || AABTreePolyIndices.size() != AABTreeHeader.PolyCount || AABTreeNodes.size() != AABTreeHeader.NodeCount)
	{
		if (error)
		{
			*error = "AABTree chunks disagree with their header";
		}
		return false;
	}
	HasAABTree = true;
	return true;
}

bool MeshModelClass::Group_Triangles(std::map<MeshSurfaceKey, std::vector<uint32>> &out, std::string *error) const
{
	out.clear();
	const MeshMaterialPassData *pass = Passes.empty() ? nullptr : &Passes[0];
	const MeshTextureStageData *stage = (pass && !pass->Stages.empty()) ? &pass->Stages[0] : nullptr;
	const size_t tris = Triangles.size();

	// 0xFFFFFFFF in a texture id array means "no texture": ZH meshmdlio.cpp read_texture_ids skips such polygons
	// in the per-polygon form. Two retail meshes (cudeer_m_skn, mmdcrd3waya) carry it as their single id; it is
	// read the same way there, since a table index can never be 0xFFFFFFFF.
	auto perTriangle = [&](const std::vector<uint32> *ids, size_t tableSize, const char *what, bool noneIsAllOnes, std::vector<std::int64_t> &resolved) -> bool {
		resolved.assign(tris, -1);
		if (ids == nullptr || ids->empty())
		{
			return true;
		}
		if (ids->size() != 1 && ids->size() != tris)
		{
			if (error)
			{
				*error = "mesh " + Get_Name() + ": pass 0 has " + std::to_string(ids->size()) + " " + what + " ids for " + std::to_string(tris) +
					" triangles (one id, or one per triangle)";
			}
			return false;
		}
		for (size_t t = 0; t < tris; ++t)
		{
			std::uint32_t id = ids->size() == 1 ? (*ids)[0] : (*ids)[t];
			if (noneIsAllOnes && id == 0xFFFFFFFFu)
			{
				continue; // resolved[t] stays -1
			}
			if (id >= tableSize)
			{
				if (error)
				{
					*error = "mesh " + Get_Name() + ": " + what + " id " + std::to_string(id) + " is outside its table of " + std::to_string(tableSize);
				}
				return false;
			}
			resolved[t] = id;
		}
		return true;
	};

	std::vector<std::int64_t> textures, shaders;
	if (!perTriangle(stage ? &stage->TextureIds : nullptr, Textures.size(), "texture", true, textures) ||
		!perTriangle(pass ? &pass->ShaderIds : nullptr, Shaders.size(), "shader", false, shaders))
	{
		return false;
	}
	for (size_t t = 0; t < tris; ++t)
	{
		MeshSurfaceKey key;
		key.Texture = textures[t];
		key.Shader = shaders[t];
		out[key].push_back((uint32)t);
	}
	return true;
}
