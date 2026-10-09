// OpenBFME: faithful rebuild of The Battle for Middle-earth II: Rise of the Witch-king 2.01.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// Port of ZH Libraries/Source/WWVegas/WW3D2/hlod.cpp: HLodDefClass::Load_W3D, read_header,
// read_proxy_array and SubObjectArrayClass::Load_W3D.

#include "Libraries/WWVegas/WW3D2/hlod.h"
#include "Libraries/WWVegas/WW3D2/w3d_file.h"
#include "Libraries/WWVegas/WWLib/chunkio.h"

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
}

bool HLodDefClass::SubObjectArrayClass::Load_W3D(ChunkLoadClass &cload)
{
	if (!cload.Open_Chunk())
	{
		return false;
	}
	if (cload.Cur_Chunk_ID() != W3D_CHUNK_HLOD_SUB_OBJECT_ARRAY_HEADER)
	{
		return false;
	}
	W3dHLodArrayHeaderStruct header;
	if (cload.Read(&header, sizeof(header)) != sizeof(header))
	{
		return false;
	}
	if (!cload.Close_Chunk())
	{
		return false;
	}

	MaxScreenSize = header.MaxScreenSize;
	ModelName.clear();
	BoneIndex.clear();

	for (uint32 imodel = 0; imodel < header.ModelCount; ++imodel)
	{
		if (!cload.Open_Chunk())
		{
			return false;
		}
		if (cload.Cur_Chunk_ID() != W3D_CHUNK_HLOD_SUB_OBJECT)
		{
			return false;
		}
		W3dHLodSubObjectStruct subobjdef;
		if (cload.Read(&subobjdef, sizeof(subobjdef)) != sizeof(subobjdef))
		{
			return false;
		}
		if (!cload.Close_Chunk())
		{
			return false;
		}
		ModelName.push_back(fixedName(subobjdef.Name, sizeof(subobjdef.Name)));
		BoneIndex.push_back((int)subobjdef.BoneIndex);
	}
	return true;
}

bool HLodDefClass::Load_W3D(ChunkLoadClass &cload, std::string *error)
{
	Lod.clear();
	ExtraLod.clear();
	Aggregates = SubObjectArrayClass();
	Proxies = SubObjectArrayClass();

	// read_header
	if (!cload.Open_Chunk() || cload.Cur_Chunk_ID() != W3D_CHUNK_HLOD_HEADER)
	{
		if (error)
		{
			*error = "HLod does not start with W3D_CHUNK_HLOD_HEADER";
		}
		return false;
	}
	W3dHLodHeaderStruct header;
	if (cload.Read(&header, sizeof(header)) != sizeof(header))
	{
		if (error)
		{
			*error = "short HLod header";
		}
		return false;
	}
	cload.Close_Chunk();

	Name = fixedName(header.Name, W3D_NAME_LEN);
	HierarchyName = fixedName(header.HierarchyName, W3D_NAME_LEN);
	Lod.resize(header.LodCount);

	for (uint32 iLOD = 0; iLOD < header.LodCount; iLOD++)
	{
		if (!cload.Open_Chunk() || cload.Cur_Chunk_ID() != W3D_CHUNK_HLOD_LOD_ARRAY)
		{
			if (error)
			{
				*error = "HLod " + Name + ": expected W3D_CHUNK_HLOD_LOD_ARRAY";
			}
			return false;
		}
		if (!Lod[iLOD].Load_W3D(cload))
		{
			if (error)
			{
				*error = "HLod " + Name + ": malformed LOD array " + std::to_string(iLOD);
			}
			return false;
		}
		cload.Close_Chunk();
	}

	while (cload.Open_Chunk())
	{
		switch (cload.Cur_Chunk_ID())
		{
		case W3D_CHUNK_HLOD_LOD_ARRAY:
		{
			SubObjectArrayClass extra;
			if (!extra.Load_W3D(cload))
			{
				if (error)
				{
					*error = "HLod " + Name + ": malformed extra LOD array";
				}
				return false;
			}
			ExtraLod.push_back(std::move(extra));
			break;
		}
		case W3D_CHUNK_HLOD_AGGREGATE_ARRAY:
			if (!Aggregates.Load_W3D(cload))
			{
				if (error)
				{
					*error = "HLod " + Name + ": malformed aggregate array";
				}
				return false;
			}
			break;
		case W3D_CHUNK_HLOD_PROXY_ARRAY:
			if (!Proxies.Load_W3D(cload))
			{
				if (error)
				{
					*error = "HLod " + Name + ": malformed proxy array";
				}
				return false;
			}
			break;
		default:
			if (error)
			{
				*error = "HLod " + Name + ": unexpected chunk " + std::to_string(cload.Cur_Chunk_ID());
			}
			return false;
		}
		cload.Close_Chunk();
	}
	if (cload.Had_Error())
	{
		if (error)
		{
			*error = "HLod " + Name + ": malformed chunk";
		}
		return false;
	}
	return true;
}
