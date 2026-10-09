// OpenBFME: faithful rebuild of The Battle for Middle-earth II: Rise of the Witch-king 2.01.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// See AssetCatalog.h. Retail source: Open-BFME-1 AssetCatalog.cpp (Load_Asset_Catalog, 0x00938150).

#include "GameEngineDevice/W3DDevice/GameClient/AssetCatalog.h"

#include "Common/ArchiveFileSystem.h"
#include "Common/AsciiString.h"
#include "Libraries/WWVegas/WW3D2/w3d_file.h"
#include "Libraries/WWVegas/WW3D2/w3dchunks.h"

#include <algorithm>
#include <cstddef>
#include <cstring>

namespace
{

// The chunk walk reads a byte buffer whose sub-chunks start at arbitrary byte offsets (a chunk's size need not be a multiple of 4), so no struct
// pointer is ever formed over it: names are read at byte offsets (offsetof) and nothing is dereferenced as a typed object.
const char *nameAt(const std::uint8_t *p, size_t offset)
{
	return reinterpret_cast<const char *>(p) + offset;
}

std::string fixedName(const char *src, size_t len)
{
	size_t n = 0;
	while (n < len && src[n] != 0)
	{
		++n;
	}
	return std::string(src, n);
}

std::uint32_t readU32(const std::uint8_t *p)
{
	std::uint32_t v;
	std::memcpy(&v, p, 4);
	return v;
}

std::string leafOf(const std::string &path)
{
	size_t slash = path.find_last_of("\\/");
	return slash == std::string::npos ? path : path.substr(slash + 1);
}

// The first sub-chunk of the prototype chunk at `bodyAt` must have id `headerId` and be at least
// `minSize` bytes; returns a pointer to its body or nullptr.
const std::uint8_t *firstSubChunk(const std::uint8_t *data, size_t size, size_t bodyAt, size_t chunkEnd,
	std::uint32_t headerId, std::uint32_t minSize)
{
	if (bodyAt + 8 > chunkEnd)
	{
		return nullptr;
	}
	if (readU32(data + bodyAt) != headerId)
	{
		return nullptr;
	}
	std::uint32_t sub = readU32(data + bodyAt + 4) & 0x7FFFFFFF;
	if (sub < minSize || bodyAt + 8 + (size_t)sub > chunkEnd || bodyAt + 8 + (size_t)sub > size)
	{
		return nullptr;
	}
	return data + bodyAt + 8;
}

} // namespace

std::string AssetCatalog::Source_Stem(const std::string &key, std::uint32_t type)
{
	switch (type)
	{
	case ASSET_TYPE_MESH:
	{
		size_t dot = key.find('.');
		return dot == std::string::npos ? key : key.substr(0, dot);
	}
	case ASSET_TYPE_BOX:
	{
		// The box loader replaces from the first '.' and leaves an undotted name UNCHANGED, with no ".w3d" appended
		// (Open-BFME-1 Rva00972480Proto_LoadBox.cpp:62-68; BFME2 RVA 0x001808D5-0x001808FC): it opens the literal name,
		// which is no .w3d file, so an undotted box can never be in its loader's file.
		size_t dot = key.find('.');
		return dot == std::string::npos ? std::string() : key.substr(0, dot);
	}
	case ASSET_TYPE_HIER:
		return key.compare(0, 2, "h*") == 0 ? key.substr(2) : std::string();
	case ASSET_TYPE_ANIM:
	{
		size_t dot = key.find('.');
		return (key.compare(0, 2, "a*") == 0 && dot != std::string::npos) ? key.substr(dot + 1) : std::string();
	}
	}
	return key; // HLOD, PART
}

const char *Asset_Type_Name(std::uint32_t type)
{
	switch (type)
	{
	case ASSET_TYPE_TEX: return "TEX";
	case ASSET_TYPE_BOX: return "BOX";
	case ASSET_TYPE_AGGR: return "AGGR";
	case ASSET_TYPE_ANIM: return "ANIM";
	case ASSET_TYPE_HIER: return "HIER";
	case ASSET_TYPE_PART: return "PART";
	case ASSET_TYPE_MESH: return "MESH";
	case ASSET_TYPE_HLOD: return "HLOD";
	}
	return "?";
}

std::string AssetCatalog::Mesh_Key(const std::string &container, const std::string &mesh)
{
	return AsciiStringUtil::lowered(container.empty() ? mesh : container + "." + mesh);
}

std::string AssetCatalog::Hierarchy_Key(const std::string &name)
{
	return "h*" + AsciiStringUtil::lowered(name);
}

// ZH hcanim.cpp:260-262 / hrawanim.cpp: Name = HierarchyName + "." + Name, here under the a* prefix.
std::string AssetCatalog::Animation_Key(const std::string &hierarchyName, const std::string &name)
{
	return "a*" + AsciiStringUtil::lowered(hierarchyName + "." + name);
}

bool AssetCatalog::Scan_W3D_File(const std::string &virtualPath, const std::uint8_t *data, size_t size,
	std::vector<AssetCatalogRecord> &out, std::vector<std::uint32_t> *skippedIds, std::string *error)
{
	const std::string path = AsciiStringUtil::lowered(virtualPath);
	const std::string leaf = leafOf(path);
	const std::string fileStem = leaf.size() > 4 && leaf.compare(leaf.size() - 4, 4, ".w3d") == 0 ? leaf.substr(0, leaf.size() - 4) : leaf;

	// Spec 2.3: a file whose chunk tree is malformed anywhere is skipped as a whole. The same walker the corpus
	// count test uses decides.
	{
		W3DChunkWalkStats stats;
		std::string walkError;
		if (!Walk_W3D_Chunks(data, size, stats, &walkError))
		{
			if (error)
			{
				*error = walkError;
			}
			return false;
		}
	}

	size_t off = 0;
	while (off + 8 <= size)
	{
		std::uint32_t id = readU32(data + off);
		std::uint32_t chunkSize = readU32(data + off + 4) & 0x7FFFFFFF;
		size_t body = off + 8;
		size_t end = body + (size_t)chunkSize;
		if (end > size)
		{
			if (error)
			{
				*error = "chunk " + std::to_string(id) + " at offset " + std::to_string(off) + " overruns the file";
			}
			return false;
		}

		std::string key;
		std::uint32_t type = 0;
		std::string problem;
		switch (id)
		{
		case W3D_CHUNK_MESH:
		{
			const std::uint8_t *h = firstSubChunk(data, size, body, end, W3D_CHUNK_MESH_HEADER3, sizeof(W3dMeshHeader3Struct));
			if (h)
			{
				key = Mesh_Key(fixedName(nameAt(h, offsetof(W3dMeshHeader3Struct, ContainerName)), W3D_NAME_LEN),
					fixedName(nameAt(h, offsetof(W3dMeshHeader3Struct, MeshName)), W3D_NAME_LEN));
				type = ASSET_TYPE_MESH;
			}
			else
			{
				problem = "mesh without a readable W3D_CHUNK_MESH_HEADER3";
			}
			break;
		}
		case W3D_CHUNK_HLOD:
		{
			const std::uint8_t *h = firstSubChunk(data, size, body, end, W3D_CHUNK_HLOD_HEADER, sizeof(W3dHLodHeaderStruct));
			if (h)
			{
				key = AsciiStringUtil::lowered(fixedName(nameAt(h, offsetof(W3dHLodHeaderStruct, Name)), W3D_NAME_LEN));
				type = ASSET_TYPE_HLOD;
			}
			else
			{
				problem = "HLod without a readable W3D_CHUNK_HLOD_HEADER";
			}
			break;
		}
		case W3D_CHUNK_HIERARCHY:
		{
			const std::uint8_t *h = firstSubChunk(data, size, body, end, W3D_CHUNK_HIERARCHY_HEADER, sizeof(W3dHierarchyStruct));
			if (h)
			{
				key = Hierarchy_Key(fixedName(nameAt(h, offsetof(W3dHierarchyStruct, Name)), W3D_NAME_LEN));
				type = ASSET_TYPE_HIER;
			}
			else
			{
				problem = "hierarchy without a readable W3D_CHUNK_HIERARCHY_HEADER";
			}
			break;
		}
		case W3D_CHUNK_ANIMATION:
		case W3D_CHUNK_COMPRESSED_ANIMATION:
		{
			// Raw and compressed headers share the first 36 bytes: Version, Name[16], HierarchyName[16].
			std::uint32_t headerId = id == W3D_CHUNK_ANIMATION ? W3D_CHUNK_ANIMATION_HEADER : W3D_CHUNK_COMPRESSED_ANIMATION_HEADER;
			const std::uint8_t *h = firstSubChunk(data, size, body, end, headerId, sizeof(W3dAnimHeaderStruct));
			if (h)
			{
				key = Animation_Key(fixedName(nameAt(h, offsetof(W3dAnimHeaderStruct, HierarchyName)), W3D_NAME_LEN),
					fixedName(nameAt(h, offsetof(W3dAnimHeaderStruct, Name)), W3D_NAME_LEN));
				type = ASSET_TYPE_ANIM;
			}
			else
			{
				problem = "animation without a readable header";
			}
			break;
		}
		case W3D_CHUNK_BOX:
			if (chunkSize >= sizeof(W3dBoxStruct))
			{
				key = AsciiStringUtil::lowered(fixedName(nameAt(data + body, offsetof(W3dBoxStruct, Name)), W3D_NAME_LEN * 2));
				type = ASSET_TYPE_BOX;
			}
			else
			{
				problem = "short W3D_CHUNK_BOX";
			}
			break;
		case W3D_CHUNK_EMITTER:
		{
			const std::uint8_t *h = firstSubChunk(data, size, body, end, W3D_CHUNK_EMITTER_HEADER, sizeof(W3dEmitterHeaderStruct));
			if (h)
			{
				key = AsciiStringUtil::lowered(fixedName(nameAt(h, offsetof(W3dEmitterHeaderStruct, Name)), W3D_NAME_LEN));
				type = ASSET_TYPE_PART;
			}
			else
			{
				problem = "emitter without a readable W3D_CHUNK_EMITTER_HEADER";
			}
			break;
		}
		default:
			if (skippedIds)
			{
				skippedIds->push_back(id);
			}
			break;
		}

		if (!problem.empty())
		{
			if (error)
			{
				*error = problem + " at offset " + std::to_string(off);
			}
			return false;
		}
		if (type != 0)
		{
			AssetCatalogRecord rec;
			rec.Key = key;
			rec.Canonical = AssetCatalog::Source_Stem(key, type) == fileStem;
			rec.Type = type;
			rec.File = leaf;
			rec.Path = path;
			rec.Offset = (std::uint32_t)off;
			rec.Length = chunkSize + 8;
			out.push_back(std::move(rec));
		}
		off = end;
	}
	if (off != size)
	{
		if (error)
		{
			*error = std::to_string(size - off) + " trailing bytes after the last top-level chunk";
		}
		return false;
	}
	return true;
}

bool AssetCatalog::Add_W3D_File(const std::string &virtualPath, const std::uint8_t *data, size_t size, std::string *error)
{
	std::vector<AssetCatalogRecord> records;
	std::vector<std::uint32_t> skipped;
	std::string scanError;
	const std::string lowered = AsciiStringUtil::lowered(virtualPath);
	FileNames.push_back(lowered);
	FileSizes.push_back((std::uint64_t)size);
	if (!Scan_W3D_File(virtualPath, data, size, records, &skipped, &scanError))
	{
		// Fail closed: nothing of this file is kept or registered.
		AssetCatalogFileFault fault;
		fault.Path = lowered;
		fault.Reason = scanError;
		FaultList.push_back(fault);
		FileRecords.push_back(std::vector<AssetCatalogRecord>());
		if (error)
		{
			*error = scanError;
		}
		return false;
	}
	for (std::uint32_t id : skipped)
	{
		SkippedCounts[id]++;
	}
	const std::string pathRulePath = [&] {
		std::string leaf = leafOf(lowered);
		return "art\\w3d\\" + leaf.substr(0, 2) + "\\" + leaf;
	}();
	for (const AssetCatalogRecord &rec : records)
	{
		if (!rec.Canonical)
		{
			UncataloguedCount++;
			continue;
		}
		auto it = Registry.find(rec.Key);
		if (it == Registry.end())
		{
			Registry[rec.Key] = rec;
		}
		else if (it->second.Path != rec.Path)
		{
			// two canonical files with one name (same leaf in two directories): the 2-letter path of spec 1.4 wins
			CanonicalTieCount++;
			if (rec.Path == pathRulePath && it->second.Path != pathRulePath)
			{
				it->second = rec;
			}
		}
	}
	RecordCount += records.size();
	FileRecords.push_back(std::move(records));
	return true;
}

bool AssetCatalog::Scan_File_System(ArchiveFileSystem &fileSystem, std::string *error)
{
	FilenameList list;
	fileSystem.getFileListInDirectory("", "", "*.w3d", list, true);
	std::vector<std::string> paths;
	paths.reserve(list.size());
	for (const std::string &p : list)
	{
		paths.push_back(p);
	}
	std::sort(paths.begin(), paths.end(), [](const std::string &a, const std::string &b) {
		std::string la = AsciiStringUtil::lowered(leafOf(a));
		std::string lb = AsciiStringUtil::lowered(leafOf(b));
		if (la != lb)
		{
			return la < lb;
		}
		return AsciiStringUtil::lowered(a) < AsciiStringUtil::lowered(b);
	});
	if (paths.empty())
	{
		if (error)
		{
			*error = "no .w3d files in the mounted archives";
		}
		return false;
	}

	std::vector<std::uint8_t> bytes;
	for (const std::string &path : paths)
	{
		std::string readError;
		if (!fileSystem.readFile(path, bytes, &readError))
		{
			if (error)
			{
				*error = "cannot read " + path + ": " + readError;
			}
			return false;
		}
		Add_W3D_File(path, bytes.data(), bytes.size(), nullptr); // a malformed file is recorded in Faults()
	}
	return true;
}

const AssetCatalogRecord *AssetCatalog::Find(const std::string &key) const
{
	auto it = Registry.find(AsciiStringUtil::lowered(key));
	return it == Registry.end() ? nullptr : &it->second;
}
