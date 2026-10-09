// OpenBFME: faithful rebuild of The Battle for Middle-earth II: Rise of the Witch-king 2.01.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// OpenBFME addition (no ZH counterpart): the table of every chunk id the engine knows by name
// (w3d_file.h) and a generic walker over a W3D file's chunk tree. The walker is the "ChunkReader
// corpus test" of the port checklist: it counts every chunk by id and by (parent, id) the way a
// reader would meet them, and fails closed per file on a chunk that overruns its parent.

#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

// Name of a chunk id ("W3D_CHUNK_MESH"), or nullptr if the id is not a W3D chunk the engine defines.
const char *W3D_Chunk_Name(std::uint32_t id);
inline bool W3D_Chunk_Is_Known(std::uint32_t id) { return W3D_Chunk_Name(id) != nullptr; }

// All ids the table defines, ascending (for tests).
std::vector<std::uint32_t> W3D_Chunk_All_Ids();

struct W3DChunkStat
{
	std::uint64_t Count = 0;
	std::uint64_t Bytes = 0; // sum of chunk sizes, header excluded
};

struct W3DChunkWalkStats
{
	static const std::uint32_t ROOT = 0xFFFFFFFFu; // "parent" of top-level chunks (not a valid id: MSB set)

	std::map<std::uint32_t, W3DChunkStat> ById;
	std::map<std::pair<std::uint32_t, std::uint32_t>, std::uint64_t> ByParent; // (parent id, id) -> count
	std::vector<std::uint32_t> Unknown; // ids met that are not in the table, in file order

	void Merge(const W3DChunkWalkStats &other);
};

// Walks every chunk of one W3D file (descending into chunks whose size has the sub-chunk bit).
// Chunks seen before a malformed chunk are counted. Returns false and sets *error if any chunk
// overruns its parent or the file, or a partial chunk header trails.
bool Walk_W3D_Chunks(const std::uint8_t *data, size_t size, W3DChunkWalkStats &stats, std::string *error);
