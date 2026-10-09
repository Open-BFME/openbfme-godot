// OpenBFME: faithful rebuild of The Battle for Middle-earth II: Rise of the Witch-king 2.01.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// ZH part_ldr.cpp: Load_W3D (:348), Read_Header (:538), Read_User_Data (:572), Read_Info (:616),
// Read_InfoV2 (:719), Read_Props (:758), Read_Line_Properties (:984), Read_Rotation_Keyframes (:1010),
// Read_Frame_Keyframes, Read_Blur_Time_Keyframes (:1100), Read_Extra_Info (:1143).

#include "Libraries/WWVegas/WW3D2/part_ldr.h"
#include "Libraries/WWVegas/WWLib/chunkio.h"

#include <cstring>

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

std::string fixedName(const char *src, size_t len)
{
	size_t n = 0;
	while (n < len && src[n] != 0)
	{
		++n;
	}
	return std::string(src, n);
}

// Reads exactly sizeof(T) bytes and requires the chunk to end there.
template <typename T>
bool readExact(ChunkLoadClass &cload, T &out, const char *what, std::string *error)
{
	if (cload.Cur_Chunk_Length() != sizeof(T) || cload.Read(&out, sizeof(T)) != sizeof(T))
	{
		return fail(error, std::string(what) + " is " + std::to_string(cload.Cur_Chunk_Length()) + " bytes, expected " + std::to_string(sizeof(T)));
	}
	return true;
}

// header struct, then `count` keys (rotation / frame / blur time chunks carry one extra start key first).
template <typename Header, typename Key>
bool readHeaderAndKeys(ChunkLoadClass &cload, Header &header, std::vector<Key> &keys, const char *what, std::string *error)
{
	std::uint32_t len = cload.Cur_Chunk_Length();
	if (len < sizeof(Header) || cload.Read(&header, sizeof(Header)) != sizeof(Header))
	{
		return fail(error, std::string("short ") + what);
	}
	std::uint64_t total = (std::uint64_t)header.KeyframeCount + 1; // the start key, then KeyframeCount keys
	if ((std::uint64_t)(len - sizeof(Header)) != total * sizeof(Key))
	{
		return fail(error, std::string(what) + " holds " + std::to_string((len - sizeof(Header)) / sizeof(Key)) + " keys, header says " + std::to_string(total));
	}
	keys.resize((size_t)total);
	if (cload.Read(keys.data(), (std::uint32_t)(total * sizeof(Key))) != (std::uint32_t)(total * sizeof(Key)))
	{
		return fail(error, std::string("short ") + what);
	}
	return true;
}

template <typename Key>
bool readKeys(ChunkLoadClass &cload, std::vector<Key> &keys, std::uint32_t count, std::uint32_t &remaining, const char *what, std::string *error)
{
	std::uint64_t bytes = (std::uint64_t)count * sizeof(Key);
	if (bytes > remaining)
	{
		return fail(error, std::string("W3D_CHUNK_EMITTER_PROPS is too short for its ") + what);
	}
	keys.resize(count);
	if (bytes > 0 && cload.Read(keys.data(), (std::uint32_t)bytes) != (std::uint32_t)bytes)
	{
		return fail(error, std::string("short ") + what);
	}
	remaining -= (std::uint32_t)bytes;
	return true;
}

} // namespace

bool ParticleEmitterDefClass::Load_W3D(ChunkLoadClass &cload, std::string *error)
{
	*this = ParticleEmitterDefClass();

	// Read_Header
	if (!cload.Open_Chunk() || cload.Cur_Chunk_ID() != W3D_CHUNK_EMITTER_HEADER)
	{
		return fail(error, "emitter does not start with W3D_CHUNK_EMITTER_HEADER");
	}
	W3dEmitterHeaderStruct header;
	if (!readExact(cload, header, "W3D_CHUNK_EMITTER_HEADER", error))
	{
		return false;
	}
	Name = fixedName(header.Name, W3D_NAME_LEN);
	Version = header.Version;
	cload.Close_Chunk();
	if (Version <= 0x00010000)
	{
		return fail(error, "emitter " + Name + " is version " + std::to_string(Version) + "; only version 2 emitters exist in retail");
	}

	// Read_User_Data: type, size, then SizeofStringParam bytes
	if (!cload.Open_Chunk() || cload.Cur_Chunk_ID() != W3D_CHUNK_EMITTER_USER_DATA)
	{
		return fail(error, "emitter " + Name + " has no W3D_CHUNK_EMITTER_USER_DATA");
	}
	{
		// ZH reads sizeof(W3dEmitterUserInfoStruct) (12 bytes: it includes StringParam[1] and padding) and then
		// SizeofStringParam more bytes for the string.
		std::uint32_t len = cload.Cur_Chunk_Length();
		W3dEmitterUserInfoStruct user = {};
		if (len < sizeof(user) || cload.Read(&user, sizeof(user)) != sizeof(user))
		{
			return fail(error, "short W3D_CHUNK_EMITTER_USER_DATA");
		}
		UserType = user.Type;
		if ((std::uint64_t)len != sizeof(user) + (std::uint64_t)user.SizeofStringParam)
		{
			return fail(error, "W3D_CHUNK_EMITTER_USER_DATA declares " + std::to_string(user.SizeofStringParam) + " string bytes in a " + std::to_string(len) + " byte chunk");
		}
		UserString.resize(user.SizeofStringParam);
		if (user.SizeofStringParam > 0 && cload.Read(&UserString[0], user.SizeofStringParam) != user.SizeofStringParam)
		{
			return fail(error, "short W3D_CHUNK_EMITTER_USER_DATA");
		}
		size_t nul = UserString.find('\0');
		if (nul != std::string::npos)
		{
			UserString.resize(nul);
		}
	}
	cload.Close_Chunk();

	// Read_Info, Read_InfoV2
	if (!cload.Open_Chunk() || cload.Cur_Chunk_ID() != W3D_CHUNK_EMITTER_INFO)
	{
		return fail(error, "emitter " + Name + " has no W3D_CHUNK_EMITTER_INFO");
	}
	if (!readExact(cload, Info, "W3D_CHUNK_EMITTER_INFO", error))
	{
		return false;
	}
	cload.Close_Chunk();
	if (!cload.Open_Chunk() || cload.Cur_Chunk_ID() != W3D_CHUNK_EMITTER_INFOV2)
	{
		return fail(error, "emitter " + Name + " has no W3D_CHUNK_EMITTER_INFOV2");
	}
	if (!readExact(cload, InfoV2, "W3D_CHUNK_EMITTER_INFOV2", error))
	{
		return false;
	}
	cload.Close_Chunk();

	// Read_Props
	if (!cload.Open_Chunk() || cload.Cur_Chunk_ID() != W3D_CHUNK_EMITTER_PROPS)
	{
		return fail(error, "emitter " + Name + " has no W3D_CHUNK_EMITTER_PROPS");
	}
	{
		std::uint32_t len = cload.Cur_Chunk_Length();
		if (len < sizeof(Props) || cload.Read(&Props, sizeof(Props)) != sizeof(Props))
		{
			return fail(error, "short W3D_CHUNK_EMITTER_PROPS");
		}
		std::uint32_t remaining = len - (std::uint32_t)sizeof(Props);
		if (!readKeys(cload, ColorKeyframes, Props.ColorKeyframes, remaining, "colour keys", error) ||
			!readKeys(cload, OpacityKeyframes, Props.OpacityKeyframes, remaining, "opacity keys", error) ||
			!readKeys(cload, SizeKeyframes, Props.SizeKeyframes, remaining, "size keys", error))
		{
			return false;
		}
		if (remaining != 0)
		{
			return fail(error, "W3D_CHUNK_EMITTER_PROPS has " + std::to_string(remaining) + " bytes after its keyframes");
		}
	}
	cload.Close_Chunk();

	// the optional sections (ZH "future additions", read in whatever order they come)
	while (cload.Open_Chunk())
	{
		bool ok = true;
		switch (cload.Cur_Chunk_ID())
		{
		case W3D_CHUNK_EMITTER_LINE_PROPERTIES:
			ok = readExact(cload, LineProperties, "W3D_CHUNK_EMITTER_LINE_PROPERTIES", error);
			HasLineProperties = ok;
			break;
		case W3D_CHUNK_EMITTER_ROTATION_KEYFRAMES:
			ok = readHeaderAndKeys(cload, RotationHeader, RotationKeyframes, "W3D_CHUNK_EMITTER_ROTATION_KEYFRAMES", error);
			HasRotation = ok;
			break;
		case W3D_CHUNK_EMITTER_FRAME_KEYFRAMES:
			ok = readHeaderAndKeys(cload, FrameHeader, FrameKeyframes, "W3D_CHUNK_EMITTER_FRAME_KEYFRAMES", error);
			HasFrames = ok;
			break;
		case W3D_CHUNK_EMITTER_BLUR_TIME_KEYFRAMES:
			ok = readHeaderAndKeys(cload, BlurTimeHeader, BlurTimeKeyframes, "W3D_CHUNK_EMITTER_BLUR_TIME_KEYFRAMES", error);
			HasBlurTime = ok;
			break;
		case W3D_CHUNK_EMITTER_EXTRA_INFO:
			ok = readExact(cload, ExtraInfo, "W3D_CHUNK_EMITTER_EXTRA_INFO", error);
			HasExtraInfo = ok;
			break;
		default:
			return fail(error, "emitter " + Name + ": unexpected chunk " + std::to_string(cload.Cur_Chunk_ID()));
		}
		if (!ok)
		{
			return false;
		}
		cload.Close_Chunk();
	}
	if (cload.Had_Error())
	{
		return fail(error, "emitter " + Name + ": malformed chunk");
	}
	return true;
}
