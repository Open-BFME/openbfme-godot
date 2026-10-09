// OpenBFME: faithful rebuild of The Battle for Middle-earth II: Rise of the Witch-king 2.01.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// Port of ZH Libraries/Source/WWVegas/WWLib/chunkio.h/.cpp (ChunkLoadClass). The ZH class
// reads from a FileClass; this one reads from a memory buffer. Chunk header: uint32 type,
// uint32 size whose MSB flags "contains sub-chunks". Micro chunks are not ported yet.
//
// Addition: Had_Error() reports a chunk whose declared size runs past its parent or the
// buffer, which ZH would only notice as short reads later.

#pragma once

#include <cstddef>
#include <cstdint>

struct ChunkHeader
{
	std::uint32_t ChunkType = 0;
	std::uint32_t ChunkSize = 0; // MSB = contains sub-chunks

	std::uint32_t Get_Type() const { return ChunkType; }
	std::uint32_t Get_Size() const { return ChunkSize & 0x7FFFFFFF; }
	int Get_Sub_Chunk_Flag() const { return (ChunkSize & 0x80000000) ? 1 : 0; }
};

class ChunkLoadClass
{
public:
	ChunkLoadClass(const std::uint8_t *data, size_t size);

	bool Open_Chunk();
	bool Close_Chunk();
	std::uint32_t Cur_Chunk_ID() const;
	std::uint32_t Cur_Chunk_Length() const;
	int Cur_Chunk_Depth() const { return StackIndex; }
	int Contains_Chunks() const;

	// Reads within the current chunk; returns nbytes or 0 if it would cross the chunk end.
	std::uint32_t Read(void *buf, std::uint32_t nbytes);
	std::uint32_t Seek(std::uint32_t nbytes);

	bool Had_Error() const { return Error; }

private:
	enum { MAX_STACK_DEPTH = 256 };

	const std::uint8_t *Data;
	size_t Size;
	size_t Offset;

	int StackIndex;
	std::uint32_t PositionStack[MAX_STACK_DEPTH];
	ChunkHeader HeaderStack[MAX_STACK_DEPTH];
	bool Error;
};
