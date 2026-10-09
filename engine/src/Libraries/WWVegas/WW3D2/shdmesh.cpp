// OpenBFME: faithful rebuild of The Battle for Middle-earth II: Rise of the Witch-king 2.01.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// See shdmesh.h. Layouts: ZH w3d_file.h:2170-2237 (chunk comments) checked against the three retail files.

#include "Libraries/WWVegas/WW3D2/shdmesh.h"
#include "Libraries/WWVegas/WWLib/chunkio.h"

namespace
{

bool fail(std::string *error, const std::string &text)
{
	if (error)
	{
		*error = text;
	}
	return false;
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

// The chunk is `count` elements of T, no more and no less.
template <typename T>
bool readCounted(ChunkLoadClass &cload, std::vector<T> &out, std::uint64_t count, const char *what, std::string *error)
{
	std::uint64_t bytes = count * sizeof(T);
	if (cload.Cur_Chunk_Length() != bytes)
	{
		return fail(error, std::string(what) + " is " + std::to_string(cload.Cur_Chunk_Length()) + " bytes, its header implies " + std::to_string(bytes));
	}
	out.resize((size_t)count);
	if (bytes > 0 && cload.Read(out.data(), (std::uint32_t)bytes) != (std::uint32_t)bytes)
	{
		return fail(error, std::string("short ") + what);
	}
	return true;
}

bool loadSubMesh(ChunkLoadClass &cload, ShdSubMeshDef &sub, std::string *error)
{
	bool haveHeader = false;
	while (cload.Open_Chunk())
	{
		bool ok = true;
		const std::uint32_t id = cload.Cur_Chunk_ID();
		if (id != W3D_CHUNK_SHDSUBMESH_HEADER && !haveHeader)
		{
			return fail(error, "ShdSubMesh chunk " + std::to_string(id) + " before its header");
		}
		const std::uint64_t nv = sub.Header.NumVertices;
		const std::uint64_t nt = sub.Header.NumTris;
		switch (id)
		{
		case W3D_CHUNK_SHDSUBMESH_HEADER:
			if (cload.Cur_Chunk_Length() != sizeof(sub.Header) || cload.Read(&sub.Header, sizeof(sub.Header)) != sizeof(sub.Header))
			{
				return fail(error, "W3D_CHUNK_SHDSUBMESH_HEADER is not " + std::to_string(sizeof(sub.Header)) + " bytes");
			}
			haveHeader = true;
			break;
		case W3D_CHUNK_SHDSUBMESH_SHADER:
			while (cload.Open_Chunk())
			{
				if (cload.Cur_Chunk_ID() == W3D_CHUNK_SHDSUBMESH_SHADER_CLASSID)
				{
					if (cload.Cur_Chunk_Length() != 4 || cload.Read(&sub.ShaderClassId, 4) != 4)
					{
						return fail(error, "W3D_CHUNK_SHDSUBMESH_SHADER_CLASSID is not 4 bytes");
					}
				}
				else if (cload.Cur_Chunk_ID() == W3D_CHUNK_SHDSUBMESH_SHADER_DEF)
				{
					sub.ShaderDef.resize(cload.Cur_Chunk_Length());
					if (!sub.ShaderDef.empty() && cload.Read(sub.ShaderDef.data(), (std::uint32_t)sub.ShaderDef.size()) != sub.ShaderDef.size())
					{
						return fail(error, "short W3D_CHUNK_SHDSUBMESH_SHADER_DEF");
					}
				}
				else
				{
					return fail(error, "unexpected chunk " + std::to_string(cload.Cur_Chunk_ID()) + " in W3D_CHUNK_SHDSUBMESH_SHADER");
				}
				cload.Close_Chunk();
			}
			break;
		case W3D_CHUNK_SHDSUBMESH_VERTICES: ok = readCounted(cload, sub.Vertices, nv, "W3D_CHUNK_SHDSUBMESH_VERTICES", error); break;
		case W3D_CHUNK_SHDSUBMESH_VERTEX_NORMALS: ok = readCounted(cload, sub.Normals, nv, "W3D_CHUNK_SHDSUBMESH_VERTEX_NORMALS", error); break;
		case W3D_CHUNK_SHDSUBMESH_TRIANGLES: ok = readCounted(cload, sub.Triangles, nt * 3, "W3D_CHUNK_SHDSUBMESH_TRIANGLES", error); break;
		case W3D_CHUNK_SHDSUBMESH_VERTEX_SHADE_INDICES: ok = readCounted(cload, sub.ShadeIndices, nv, "W3D_CHUNK_SHDSUBMESH_VERTEX_SHADE_INDICES", error); break;
		case W3D_CHUNK_SHDSUBMESH_UV0: ok = readCounted(cload, sub.UV0, nv, "W3D_CHUNK_SHDSUBMESH_UV0", error); break;
		case W3D_CHUNK_SHDSUBMESH_UV1: ok = readCounted(cload, sub.UV1, nv, "W3D_CHUNK_SHDSUBMESH_UV1", error); break;
		case W3D_CHUNK_SHDSUBMESH_TANGENT_BASIS_S: ok = readCounted(cload, sub.TangentBasisS, nv, "W3D_CHUNK_SHDSUBMESH_TANGENT_BASIS_S", error); break;
		case W3D_CHUNK_SHDSUBMESH_TANGENT_BASIS_T: ok = readCounted(cload, sub.TangentBasisT, nv, "W3D_CHUNK_SHDSUBMESH_TANGENT_BASIS_T", error); break;
		case W3D_CHUNK_SHDSUBMESH_TANGENT_BASIS_SxT: ok = readCounted(cload, sub.TangentBasisSxT, nv, "W3D_CHUNK_SHDSUBMESH_TANGENT_BASIS_SxT", error); break;
		case W3D_CHUNK_SHDSUBMESH_VERTEX_COLOR: ok = readCounted(cload, sub.VertexColors, nv, "W3D_CHUNK_SHDSUBMESH_VERTEX_COLOR", error); break;
		case W3D_CHUNK_SHDSUBMESH_VERTEX_INFLUENCES: ok = readCounted(cload, sub.VertexInfluences, nv, "W3D_CHUNK_SHDSUBMESH_VERTEX_INFLUENCES", error); break;
		default:
			return fail(error, "unexpected chunk " + std::to_string(id) + " in W3D_CHUNK_SHDSUBMESH");
		}
		if (!ok)
		{
			return false;
		}
		cload.Close_Chunk();
	}
	if (!haveHeader)
	{
		return fail(error, "ShdSubMesh without a header");
	}
	return true;
}

} // namespace

bool ShdMeshDefClass::Load_W3D(ChunkLoadClass &cload, std::string *error)
{
	*this = ShdMeshDefClass();
	bool haveHeader = false;
	while (cload.Open_Chunk())
	{
		switch (cload.Cur_Chunk_ID())
		{
		case W3D_CHUNK_SHDMESH_NAME:
			Name = readString(cload);
			break;
		case W3D_CHUNK_SHDMESH_HEADER:
			if (cload.Cur_Chunk_Length() != sizeof(Header) || cload.Read(&Header, sizeof(Header)) != sizeof(Header))
			{
				return fail(error, "W3D_CHUNK_SHDMESH_HEADER is not " + std::to_string(sizeof(Header)) + " bytes");
			}
			haveHeader = true;
			break;
		case W3D_CHUNK_SHDMESH_USER_TEXT:
			UserText = readString(cload);
			break;
		case W3D_CHUNK_SHDSUBMESH:
		{
			ShdSubMeshDef sub;
			if (!loadSubMesh(cload, sub, error))
			{
				return false;
			}
			SubMeshes.push_back(std::move(sub));
			break;
		}
		default:
			return fail(error, "unexpected chunk " + std::to_string(cload.Cur_Chunk_ID()) + " in W3D_CHUNK_SHDMESH");
		}
		cload.Close_Chunk();
	}
	if (cload.Had_Error())
	{
		return fail(error, "malformed chunk in ShdMesh " + Name);
	}
	if (!haveHeader)
	{
		return fail(error, "ShdMesh " + Name + " has no header");
	}
	if (SubMeshes.size() != Header.NumSubMeshes)
	{
		return fail(error, "ShdMesh " + Name + " holds " + std::to_string(SubMeshes.size()) + " sub-meshes, header says " + std::to_string(Header.NumSubMeshes));
	}
	return true;
}
