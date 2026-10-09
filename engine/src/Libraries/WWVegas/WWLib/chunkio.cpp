// OpenBFME: faithful rebuild of The Battle for Middle-earth II: Rise of the Witch-king 2.01.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// Port of ZH Libraries/Source/WWVegas/WWLib/chunkio.cpp (ChunkLoadClass).

#include "Libraries/WWVegas/WWLib/chunkio.h"

#include <cstring>

ChunkLoadClass::ChunkLoadClass(const std::uint8_t *data, size_t size)
	: Data(data), Size(size), Offset(0), StackIndex(0), Error(false)
{
	std::memset(PositionStack, 0, sizeof(PositionStack));
}

bool ChunkLoadClass::Open_Chunk()
{
	if (StackIndex >= MAX_STACK_DEPTH - 1)
	{
		Error = true;
		return false;
	}

	// if the parent chunk has been completely eaten, return false
	if ((StackIndex > 0) && (PositionStack[StackIndex - 1] == HeaderStack[StackIndex - 1].Get_Size()))
	{
		return false;
	}

	// read the chunk header
	if (Offset + 8 > Size)
	{
		if (Offset != Size)
		{
			Error = true; // a partial header
		}
		return false;
	}
	ChunkHeader header;
	std::memcpy(&header.ChunkType, Data + Offset, 4);
	std::memcpy(&header.ChunkSize, Data + Offset + 4, 4);

	// Bounds: the chunk must fit in its parent and in the buffer.
	std::uint64_t end = (std::uint64_t)Offset + 8 + header.Get_Size();
	if (end > Size)
	{
		Error = true;
		return false;
	}
	if (StackIndex > 0)
	{
		std::uint64_t parentRemaining = (std::uint64_t)HeaderStack[StackIndex - 1].Get_Size() - PositionStack[StackIndex - 1];
		if (8 + (std::uint64_t)header.Get_Size() > parentRemaining)
		{
			Error = true;
			return false;
		}
	}
	Offset += 8;

	HeaderStack[StackIndex] = header;
	PositionStack[StackIndex] = 0;
	StackIndex++;
	return true;
}

bool ChunkLoadClass::Close_Chunk()
{
	if (StackIndex <= 0)
	{
		Error = true;
		return false;
	}

	std::uint32_t csize = HeaderStack[StackIndex - 1].Get_Size();
	std::uint32_t pos = PositionStack[StackIndex - 1];

	if (pos < csize)
	{
		Offset += csize - pos;
	}

	StackIndex--;
	if (StackIndex > 0)
	{
		PositionStack[StackIndex - 1] += csize + (std::uint32_t)sizeof(ChunkHeader);
	}
	return true;
}

std::uint32_t ChunkLoadClass::Cur_Chunk_ID() const
{
	return StackIndex > 0 ? HeaderStack[StackIndex - 1].Get_Type() : 0;
}

std::uint32_t ChunkLoadClass::Cur_Chunk_Length() const
{
	return StackIndex > 0 ? HeaderStack[StackIndex - 1].Get_Size() : 0;
}

int ChunkLoadClass::Contains_Chunks() const
{
	return StackIndex > 0 ? HeaderStack[StackIndex - 1].Get_Sub_Chunk_Flag() : 0;
}

std::uint32_t ChunkLoadClass::Read(void *buf, std::uint32_t nbytes)
{
	if (StackIndex < 1)
	{
		return 0;
	}
	// Don't read if we would go past the end of the current chunk
	if ((std::uint64_t)PositionStack[StackIndex - 1] + nbytes > HeaderStack[StackIndex - 1].Get_Size())
	{
		return 0;
	}
	if (Offset + nbytes > Size)
	{
		Error = true;
		return 0;
	}
	std::memcpy(buf, Data + Offset, nbytes);
	Offset += nbytes;
	PositionStack[StackIndex - 1] += nbytes;
	return nbytes;
}

std::uint32_t ChunkLoadClass::Seek(std::uint32_t nbytes)
{
	if (StackIndex < 1)
	{
		return 0;
	}
	if ((std::uint64_t)PositionStack[StackIndex - 1] + nbytes > HeaderStack[StackIndex - 1].Get_Size())
	{
		return 0;
	}
	Offset += nbytes;
	PositionStack[StackIndex - 1] += nbytes;
	return nbytes;
}
