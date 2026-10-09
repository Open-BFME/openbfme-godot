// OpenBFME: faithful rebuild of The Battle for Middle-earth II: Rise of the Witch-king 2.01.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// Port of ZH Libraries/Source/WWVegas/WW3D2/motchan.cpp (see motchan.h for the deliberate
// differences) plus the BFME2 motion channel (Open-BFME-2 BFME2MotionChannelFactory.cpp, and the
// FindIndex / Evaluate attempts in reverse/attempts/0x001b2efc.cpp, 0x001b2fef.cpp, 0x001b3313.cpp).

#include "Libraries/WWVegas/WW3D2/motchan.h"
#include "GameLogic/Object/PristinePose.h"
#include "Libraries/WWVegas/WW3D2/w3d_file.h"
#include "Libraries/WWVegas/WWLib/chunkio.h"

#include <cmath>
#include <cstring>

namespace
{

// ---- adaptive delta filter table (ZH motchan.cpp:56-76 and the AdaptiveDeltaMotionChannelClass constructor, which fills entries
// 16..255 with 1 - sin(90 degrees * i / 240)): RENDER-2 moved it to GameLogic/Object/PristinePose.cpp (numeric facade, canonical
// environment); the decoders below read it through AdaptiveDelta_Filter.
bool setError(std::string *error, const std::string &text)
{
	if (error)
	{
		*error = text;
	}
	return false;
}

// Channel types the retail loaders store: X Y Z (scalar), XR YR ZR (scalar, unused by the pose code),
// Q (4 floats) and the BFME fade (scalar). Anything else is refused rather than dropped.
bool channelShapeOk(unsigned int type, unsigned int vectorLen)
{
	if (type == ANIM_CHANNEL_Q)
	{
		return vectorLen == 4;
	}
	if (type <= ANIM_CHANNEL_ZR || type == ANIM_CHANNEL_FADE)
	{
		return vectorLen == 1;
	}
	return false;
}

float lerpf(float a, float b, float ratio)
{
	return a + (b - a) * ratio;
}

} // namespace

float AdaptiveDelta_Filter(unsigned int index)
{
	return PristinePose::adaptiveDeltaFilter(index);
}

// ---- MotionChannelClass -------------------------------------------------------------------------

// ZH motchan.cpp MotionChannelClass::Load_W3D (no data compression: Do_Data_Compression returns at once).
bool MotionChannelClass::Load_W3D(ChunkLoadClass &cload, std::string *error)
{
	const std::uint32_t headerSize = (std::uint32_t)offsetof(W3dAnimChannelStruct, Data);
	std::uint32_t size = cload.Cur_Chunk_Length();
	W3dAnimChannelStruct chan = {};
	if (size < headerSize || cload.Read(&chan, headerSize) != headerSize)
	{
		return setError(error, "short W3D_CHUNK_ANIMATION_CHANNEL header");
	}
	if (chan.LastFrame < chan.FirstFrame)
	{
		return setError(error, "animation channel LastFrame < FirstFrame");
	}
	if (!channelShapeOk(chan.Flags, chan.VectorLen))
	{
		return setError(error, "animation channel type " + std::to_string(chan.Flags) + " with vector length " +
			std::to_string(chan.VectorLen) + " is not a retail channel shape");
	}

	FirstFrame = chan.FirstFrame;
	LastFrame = chan.LastFrame;
	VectorLen = chan.VectorLen;
	Type = chan.Flags;
	PivotIdx = chan.Pivot;

	std::uint32_t num_floats = (std::uint32_t)(LastFrame - FirstFrame + 1) * (std::uint32_t)VectorLen;
	std::uint32_t datasize = num_floats * (std::uint32_t)sizeof(float);
	if (size - headerSize < datasize)
	{
		return setError(error, "animation channel holds " + std::to_string((size - headerSize) / 4) + " floats, needs " +
			std::to_string(num_floats));
	}
	Data.resize(num_floats);
	if (cload.Read(Data.data(), datasize) != datasize)
	{
		return setError(error, "short animation channel data");
	}
	// ZH: "There was a bug in the exporter which saved too much data" - skip it.
	ExtraBytes = size - headerSize - datasize;
	if (ExtraBytes > 0)
	{
		cload.Seek(ExtraBytes);
	}
	return true;
}

void MotionChannelClass::Get_Vector(int frame, float *setvec) const
{
	if ((frame < FirstFrame) || (frame > LastFrame))
	{
		// set_identity
		if (Type == ANIM_CHANNEL_Q)
		{
			setvec[0] = 0.0f;
			setvec[1] = 0.0f;
			setvec[2] = 0.0f;
			setvec[3] = 1.0f;
		}
		else if (Type == ANIM_CHANNEL_FADE)
		{
			// BFME2 retail, the inlined Get_Vector of FUN_00563a80 (raw Anim_Update): outside [First, Last] a channel of type 0xF
			// yields DAT_00bbb8d8 = 1.0f, every other scalar channel 0.0f. (The Open-BFME-1 comment says the fade goes to zero;
			// the BFME2 binary, which comes first, says it stays opaque.)
			setvec[0] = 1.0f;
		}
		else
		{
			setvec[0] = 0.0f;
		}
	}
	else
	{
		int vframe = frame - FirstFrame;
		for (int i = 0; i < VectorLen; i++)
		{
			setvec[i] = Data[(size_t)vframe * VectorLen + i];
		}
	}
}

void MotionChannelClass::Get_Vector_As_Quat(int frame, Quaternion &quat) const
{
	if ((frame < FirstFrame) || (frame > LastFrame))
	{
		quat.Set(0.0f, 0.0f, 0.0f, 1.0f);
	}
	else
	{
		const float *d = &Data[(size_t)(frame - FirstFrame) * VectorLen];
		quat.Set(d[0], d[1], d[2], d[3]);
	}
}

// ---- BitChannelClass ----------------------------------------------------------------------------

bool BitChannelClass::Load_W3D(ChunkLoadClass &cload, std::string *error)
{
	const std::uint32_t headerSize = (std::uint32_t)offsetof(W3dBitChannelStruct, Data);
	std::uint32_t size = cload.Cur_Chunk_Length();
	W3dBitChannelStruct chan = {};
	if (size < headerSize || cload.Read(&chan, headerSize) != headerSize)
	{
		return setError(error, "short W3D_CHUNK_BIT_CHANNEL header");
	}
	if (chan.LastFrame < chan.FirstFrame)
	{
		return setError(error, "bit channel LastFrame < FirstFrame");
	}
	if (chan.Flags != BIT_CHANNEL_VIS)
	{
		return setError(error, "bit channel type " + std::to_string(chan.Flags) + " is not BIT_CHANNEL_VIS");
	}
	FirstFrame = chan.FirstFrame;
	LastFrame = chan.LastFrame;
	Type = chan.Flags;
	PivotIdx = chan.Pivot;
	DefaultVal = chan.DefaultVal;

	std::uint32_t numbits = (std::uint32_t)(LastFrame - FirstFrame + 1);
	std::uint32_t numbytes = (numbits + 7) / 8;
	if (size - headerSize < numbytes)
	{
		return setError(error, "bit channel holds " + std::to_string(size - headerSize) + " bytes, needs " + std::to_string(numbytes));
	}
	Bits.resize(numbytes);
	if (cload.Read(Bits.data(), numbytes) != numbytes)
	{
		return setError(error, "short bit channel data");
	}
	return true;
}

int BitChannelClass::Get_Bit(int frame) const
{
	if ((frame < FirstFrame) || (frame > LastFrame))
	{
		return DefaultVal;
	}
	int bit = frame - FirstFrame;
	std::uint8_t mask = (std::uint8_t)(1 << (bit % 8));
	return ((Bits[(size_t)bit / 8] & mask) != 0);
}

// ---- TimeCodedMotionChannelClass ------------------------------------------------------------------

bool TimeCodedMotionChannelClass::Load_W3D(ChunkLoadClass &cload, std::string *error)
{
	const std::uint32_t headerSize = (std::uint32_t)offsetof(W3dTimeCodedAnimChannelStruct, Data);
	std::uint32_t size = cload.Cur_Chunk_Length();
	W3dTimeCodedAnimChannelStruct chan = {};
	if (size < headerSize || cload.Read(&chan, headerSize) != headerSize)
	{
		return setError(error, "short time coded channel header");
	}
	if (chan.NumTimeCodes == 0)
	{
		return setError(error, "time coded channel has no keys");
	}
	if (!channelShapeOk(chan.Flags, chan.VectorLen))
	{
		return setError(error, "time coded channel type " + std::to_string(chan.Flags) + " with vector length " +
			std::to_string(chan.VectorLen) + " is not a retail channel shape");
	}
	NumTimeCodes = chan.NumTimeCodes;
	VectorLen = chan.VectorLen;
	Type = chan.Flags;
	PivotIdx = chan.Pivot;
	PacketSize = (std::uint32_t)VectorLen + 1;

	std::uint64_t words = (std::uint64_t)NumTimeCodes * PacketSize;
	std::uint64_t bytes = words * 4;
	if ((std::uint64_t)(size - headerSize) < bytes)
	{
		return setError(error, "time coded channel holds " + std::to_string(size - headerSize) + " data bytes, needs " + std::to_string(bytes));
	}
	Data.resize((size_t)words);
	if (cload.Read(Data.data(), (std::uint32_t)bytes) != (std::uint32_t)bytes)
	{
		return setError(error, "short time coded channel data");
	}
	ExtraBytes = (std::uint32_t)((size - headerSize) - bytes);
	if (ExtraBytes > 0)
	{
		cload.Seek(ExtraBytes);
	}
	// Both the ZH binary search and the retail one assume time codes increase.
	for (std::uint32_t k = 1; k < NumTimeCodes; ++k)
	{
		std::uint32_t t0 = Data[(size_t)(k - 1) * PacketSize] & ~W3D_TIMECODED_BINARY_MOVEMENT_FLAG;
		std::uint32_t t1 = Data[(size_t)k * PacketSize] & ~W3D_TIMECODED_BINARY_MOVEMENT_FLAG;
		if (t1 <= t0)
		{
			return setError(error, "time coded channel key " + std::to_string(k) + " is not after key " + std::to_string(k - 1));
		}
	}
	return true;
}

void TimeCodedMotionChannelClass::Get_Key(int key, float *vec) const
{
	std::memcpy(vec, &Data[(size_t)key * PacketSize + 1], sizeof(float) * (size_t)VectorLen);
}

// Retail classic timecoded evaluation (BFME2 HCompressedAnimClass::Get_Orientation, RVA 0x00190D1A-0x00190EF3; the
// scalar path is the same search). The frame is truncated to an int and compared UNSIGNED against the key times
// (fistp at 0x00190D62, cmp/jb at 0x00190D80 and 0x00190DC1), so a negative frame is past every key and selects
// the last one. A frame at or after the last key selects it. Otherwise a binary search with bounds 0 and N-2
// (0x00190DCA-0x00190E1A) runs, and that search never terminates for a time before the first key: for such a
// frame retail hangs. No source defines a value there, so this port reports it (Is_Defined_At / a false return)
// instead of inventing one; PLAN acceptance stop. No retail channel is evaluated there (see the corpus test).
static std::uint32_t timecodeOfFrame(float frame)
{
	int t = (int)frame;
	return t < 0 ? 0xFFFFFFFFu : (std::uint32_t)t;
}

bool TimeCodedMotionChannelClass::Is_Defined_At(float frame) const
{
	std::uint32_t tc = timecodeOfFrame(frame);
	return tc >= (Data[0] & ~W3D_TIMECODED_BINARY_MOVEMENT_FLAG);
}

// Packet index of the last key whose time code <= timecode (precondition: timecode >= first key's).
std::uint32_t TimeCodedMotionChannelClass::index_for(std::uint32_t timecode) const
{
	std::uint32_t last = NumTimeCodes - 1;
	if (timecode >= (Data[(size_t)last * PacketSize] & ~W3D_TIMECODED_BINARY_MOVEMENT_FLAG))
	{
		return last;
	}
	std::uint32_t lo = 0;
	std::uint32_t hi = last; // invariant: time(lo) <= timecode < time(hi)
	while (hi - lo > 1)
	{
		std::uint32_t mid = lo + (hi - lo) / 2;
		if (timecode < (Data[(size_t)mid * PacketSize] & ~W3D_TIMECODED_BINARY_MOVEMENT_FLAG))
		{
			hi = mid;
		}
		else
		{
			lo = mid;
		}
	}
	return lo;
}

// ZH motchan.cpp TimeCodedMotionChannelClass::Get_Vector; the retail search is described above.
bool TimeCodedMotionChannelClass::Get_Vector(float frame, float *setvec) const
{
	if (!Is_Defined_At(frame))
	{
		return false;
	}
	std::uint32_t pidx = index_for(timecodeOfFrame(frame));
	const float *frm = reinterpret_cast<const float *>(&Data[(size_t)pidx * PacketSize + 1]);

	if (pidx == NumTimeCodes - 1)
	{
		for (int i = 0; i < VectorLen; i++)
		{
			setvec[i] = frm[i];
		}
		return true;
	}
	std::uint32_t p2idx = pidx + 1;
	std::uint32_t time = Data[(size_t)p2idx * PacketSize];
	if (time & W3D_TIMECODED_BINARY_MOVEMENT_FLAG)
	{
		for (int i = 0; i < VectorLen; i++)
		{
			setvec[i] = frm[i];
		}
		return true;
	}
	float time1 = (float)(Data[(size_t)pidx * PacketSize] & ~W3D_TIMECODED_BINARY_MOVEMENT_FLAG);
	float time2 = (float)(time & ~W3D_TIMECODED_BINARY_MOVEMENT_FLAG);
	float ratio = (frame - time1) / (time2 - time1);
	const float *frame2 = reinterpret_cast<const float *>(&Data[(size_t)p2idx * PacketSize + 1]);
	for (int i = 0; i < VectorLen; i++)
	{
		setvec[i] = lerpf(frm[i], frame2[i], ratio);
	}
	return true;
}

// The quaternion blend is BFME2's nlerp helper (RVA 0x00717550), called at 0x00190EE9, not ZH's Fast_Slerp.
bool TimeCodedMotionChannelClass::Get_QuatVector(float frame, Quaternion &out) const
{
	if (!Is_Defined_At(frame))
	{
		return false;
	}
	std::uint32_t pidx = index_for(timecodeOfFrame(frame));
	const float *q1 = reinterpret_cast<const float *>(&Data[(size_t)pidx * PacketSize + 1]);
	if (pidx == NumTimeCodes - 1)
	{
		out = Quaternion(q1[0], q1[1], q1[2], q1[3]);
		return true;
	}
	std::uint32_t p2idx = pidx + 1;
	std::uint32_t time = Data[(size_t)p2idx * PacketSize];
	if (time & W3D_TIMECODED_BINARY_MOVEMENT_FLAG)
	{
		out = Quaternion(q1[0], q1[1], q1[2], q1[3]);
		return true;
	}
	float time1 = (float)(Data[(size_t)pidx * PacketSize] & ~W3D_TIMECODED_BINARY_MOVEMENT_FLAG);
	float time2 = (float)(time & ~W3D_TIMECODED_BINARY_MOVEMENT_FLAG);
	float ratio = (frame - time1) / (time2 - time1);
	const float *q2 = reinterpret_cast<const float *>(&Data[(size_t)p2idx * PacketSize + 1]);
	BFME2_Nlerp(out, Quaternion(q1[0], q1[1], q1[2], q1[3]), Quaternion(q2[0], q2[1], q2[2], q2[3]), ratio);
	return true;
}

// ---- TimeCodedBitChannelClass ---------------------------------------------------------------------

bool TimeCodedBitChannelClass::Load_W3D(ChunkLoadClass &cload, std::string *error)
{
	const std::uint32_t headerSize = (std::uint32_t)offsetof(W3dTimeCodedBitChannelStruct, Data);
	std::uint32_t size = cload.Cur_Chunk_Length();
	W3dTimeCodedBitChannelStruct chan = {};
	if (size < headerSize || cload.Read(&chan, headerSize) != headerSize)
	{
		return setError(error, "short compressed bit channel header");
	}
	if (chan.NumTimeCodes == 0)
	{
		return setError(error, "compressed bit channel has no keys");
	}
	if (chan.Flags != BIT_CHANNEL_VIS)
	{
		return setError(error, "compressed bit channel type " + std::to_string(chan.Flags) + " is not BIT_CHANNEL_VIS");
	}
	Type = chan.Flags;
	PivotIdx = chan.Pivot;
	DefaultVal = chan.DefaultVal;
	std::uint64_t bytes = (std::uint64_t)chan.NumTimeCodes * 4;
	if ((std::uint64_t)(size - headerSize) != bytes)
	{
		return setError(error, "compressed bit channel holds " + std::to_string(size - headerSize) + " data bytes, needs " + std::to_string(bytes));
	}
	Bits.resize(chan.NumTimeCodes);
	if (cload.Read(Bits.data(), (std::uint32_t)bytes) != (std::uint32_t)bytes)
	{
		return setError(error, "short compressed bit channel data");
	}
	for (size_t k = 1; k < Bits.size(); ++k)
	{
		if ((Bits[k] & ~W3D_TIMECODED_BIT_MASK) < (Bits[k - 1] & ~W3D_TIMECODED_BIT_MASK))
		{
			return setError(error, "compressed bit channel time codes decrease at key " + std::to_string(k));
		}
	}
	return true;
}

// ZH motchan.cpp TimeCodedBitChannelClass::Get_Bit, without the cached starting index: the entry in
// force at `frame` is the last whose time code is <= frame, or the first entry before any of them.
int TimeCodedBitChannelClass::Get_Bit(int frame) const
{
	int idx = 0;
	for (; idx < (int)Bits.size(); idx++)
	{
		int time = (int)(Bits[idx] & ~W3D_TIMECODED_BIT_MASK);
		if (frame < time)
		{
			break;
		}
	}
	idx--;
	if (idx < 0)
	{
		idx = 0;
	}
	return (((Bits[idx] & W3D_TIMECODED_BIT_MASK) == W3D_TIMECODED_BIT_MASK));
}

// Frame selection of the adaptive delta evaluators: classic (BFME2 RVA 0x00190CCB-0x00190D09, getter 0x0018FCC0) and
// motion encodings 1 / 2 (0x001B2AC4-0x001B2B52, 0x001B2D29-0x001B2DB7). The frame is TRUNCATED toward zero
// (cvttss2si) and the fractional remainder kept with its sign, so -0.25 is frame 0 with ratio -0.25 and the blend
// extrapolates backwards. The getter compares the truncated index UNSIGNED against the frame count and clamps to the
// last frame, so a frame of -1 or less selects the last frame (0x0018FCF5-0x0018FCF9, scalar 0x0018FC43-0x0018FC47). The
// second frame is the first plus one; at the last frame the decoder repeats the endpoint for the next sample, so
// clamping it to the last frame is what the classic channel returns. Motion encodings 1 / 2 do NOT share this: see
// motionStreamFrames.
static void streamFrames(float frame, std::uint32_t count, std::uint32_t &f1, std::uint32_t &f2, float &ratio)
{
	int t = (int)frame;
	ratio = frame - (float)t;
	std::uint32_t u = (std::uint32_t)t;
	f1 = u >= count ? count - 1 : u;
	f2 = f1 + 1 >= count ? count - 1 : f1 + 1;
}

// Motion encodings 1 / 2 (getters 0x001B2450 / 0x001B26xx): for a truncated frame of -1 or below the getter wraps
// frame + 1 to zero and leaves the first blend sample unwritten (0x001B2486, 0x001B24EA-0x001B2501 for encoding 1;
// 0x001B27DF, 0x001B283C-0x001B2858 for encoding 2). Retail has no defined result there, so it is reported.
static void motionStreamFrames(float frame, std::uint32_t count, std::uint32_t &f1, std::uint32_t &f2, float &ratio)
{
	if ((int)frame <= -1)
	{
		throw UndefinedFrameError("motion channel: frame " + std::to_string(frame) + " (truncated to -1 or below) has no defined value in BFME2");
	}
	streamFrames(frame, count, f1, f2, ratio);
}

// ---- AdaptiveDeltaMotionChannelClass --------------------------------------------------------------

namespace
{

#define PACKET_SIZE (9)

// Decodes nibble `fi` (0..15) of a 4-bit packet whose delta bytes start at `deltas` (motchan.cpp:1030-1060):
// even nibbles are the low half of byte fi/2, odd nibbles the high half, sign extended.
int nibbleDelta(const std::uint8_t *deltas, int fi)
{
	int factor = deltas[fi >> 1];
	if (fi & 1)
	{
		factor >>= 4;
	}
	factor &= 0xF;
	if (factor & 0x8)
	{
		factor |= 0xFFFFFFF0;
	}
	return factor;
}

} // namespace

bool AdaptiveDeltaMotionChannelClass::Load_W3D(ChunkLoadClass &cload, std::string *error)
{
	const std::uint32_t headerSize = (std::uint32_t)offsetof(W3dAdaptiveDeltaAnimChannelStruct, Data);
	std::uint32_t size = cload.Cur_Chunk_Length();
	W3dAdaptiveDeltaAnimChannelStruct chan = {};
	if (size < headerSize || cload.Read(&chan, headerSize) != headerSize)
	{
		return setError(error, "short adaptive delta channel header");
	}
	if (chan.NumFrames == 0)
	{
		return setError(error, "adaptive delta channel has no frames");
	}
	if (!channelShapeOk(chan.Flags, chan.VectorLen))
	{
		return setError(error, "adaptive delta channel type " + std::to_string(chan.Flags) + " with vector length " +
			std::to_string(chan.VectorLen) + " is not a retail channel shape");
	}
	VectorLen = chan.VectorLen;
	Type = chan.Flags;
	PivotIdx = chan.Pivot;
	NumFrames = chan.NumFrames;
	Scale = chan.Scale;

	std::uint64_t rows = ((std::uint64_t)NumFrames + 15) >> 4;
	std::uint64_t initialBytes = (std::uint64_t)VectorLen * 4;
	std::uint64_t packetBytes = rows * (std::uint64_t)VectorLen * PACKET_SIZE;
	std::uint64_t need = initialBytes + packetBytes;
	if ((std::uint64_t)(size - headerSize) < need)
	{
		return setError(error, "adaptive delta channel holds " + std::to_string(size - headerSize) + " data bytes, needs " + std::to_string(need));
	}
	std::vector<std::uint8_t> raw((size_t)need);
	if (cload.Read(raw.data(), (std::uint32_t)need) != (std::uint32_t)need)
	{
		return setError(error, "short adaptive delta channel data");
	}
	ExtraBytes = (std::uint32_t)((size - headerSize) - need);
	if (ExtraBytes > 0)
	{
		cload.Seek(ExtraBytes);
	}

	// decompress() of ZH, run once for every frame: each component starts from its initial value and
	// adds filter * nibble per frame; packet (row, component) lives at row * 9 * VectorLen + component * 9.
	Packed = raw;
	Frames.assign((size_t)NumFrames * VectorLen, 0.0f);
	for (int vi = 0; vi < VectorLen; vi++)
	{
		float last_value;
		std::memcpy(&last_value, &raw[(size_t)vi * 4], sizeof(float));
		Frames[vi] = last_value;
		for (std::uint32_t frame = 1; frame < NumFrames; frame++)
		{
			std::uint32_t row = (frame - 1) >> 4;
			int fi = (int)((frame - 1) & 15);
			const std::uint8_t *packet = &raw[(size_t)initialBytes + ((size_t)row * VectorLen + vi) * PACKET_SIZE];
			float filter = AdaptiveDelta_Filter(packet[0]) * Scale;
			float ffactor = (float)nibbleDelta(packet + 1, fi);
			float delta = ffactor * filter;
			last_value += delta;
			Frames[(size_t)frame * VectorLen + vi] = last_value;
		}
	}
	return true;
}

void AdaptiveDeltaMotionChannelClass::Get_Key(int frame, float *vec) const
{
	std::memcpy(vec, &Frames[(size_t)frame * VectorLen], sizeof(float) * (size_t)VectorLen);
}

// ZH motchan.cpp AdaptiveDeltaMotionChannelClass::Get_Vector: lerp between frame and frame + 1, both
// clamped to the last frame (getframe).
void AdaptiveDeltaMotionChannelClass::Get_Vector(float frame, float *setvec) const
{
	std::uint32_t f1, f2;
	float ratio;
	streamFrames(frame, NumFrames, f1, f2, ratio);
	for (int i = 0; i < VectorLen; i++)
	{
		setvec[i] = lerpf(Frames[(size_t)f1 * VectorLen + i], Frames[(size_t)f2 * VectorLen + i], ratio);
	}
}

// Retail blends with the nlerp helper (RVA 0x00190D09, HCompressedAnimClass::Get_Orientation), not Fast_Slerp.
Quaternion AdaptiveDeltaMotionChannelClass::Get_QuatVector(float frame) const
{
	std::uint32_t f1, f2;
	float ratio;
	streamFrames(frame, NumFrames, f1, f2, ratio);
	const float *a = &Frames[(size_t)f1 * 4];
	const float *b = &Frames[(size_t)f2 * 4];
	Quaternion q;
	BFME2_Nlerp(q, Quaternion(a[0], a[1], a[2], a[3]), Quaternion(b[0], b[1], b[2], b[3]), ratio);
	return q;
}

// ---- BFME2MotionChannel ---------------------------------------------------------------------------

// Open-BFME-2 BFME2MotionChannelFactory.cpp Load_BFME2MotionChannel / BFME2Encoding0MotionChannel::Load /
// BFME2StreamMotionChannel::Load. The 8 byte header is read first; version must be 0.
bool BFME2MotionChannel::Load_W3D(ChunkLoadClass &cload, std::string *error)
{
	const std::uint32_t headerSize = (std::uint32_t)sizeof(W3dMotionChannelHeaderStruct);
	std::uint32_t size = cload.Cur_Chunk_Length();
	W3dMotionChannelHeaderStruct header = {};
	if (size < headerSize || cload.Read(&header, headerSize) != headerSize)
	{
		return setError(error, "short motion channel header");
	}
	if (header.Version != 0)
	{
		return setError(error, "motion channel version " + std::to_string(header.Version) + " (retail refuses anything but 0)");
	}
	if (header.Encoding > ENCODING_ADAPTIVE_DELTA_8)
	{
		return setError(error, "motion channel encoding " + std::to_string(header.Encoding) + " is not 0, 1 or 2");
	}
	if (header.Count == 0)
	{
		return setError(error, "motion channel has no keys");
	}
	if (!channelShapeOk(header.Type, header.Components))
	{
		return setError(error, "motion channel type " + std::to_string(header.Type) + " with " + std::to_string(header.Components) +
			" components is not a retail channel shape");
	}
	EncodingKind = header.Encoding;
	Type = header.Type;
	PivotIdx = header.Pivot;
	Components = header.Components;
	Count = header.Count;

	std::uint32_t body = size - headerSize;
	if (EncodingKind == ENCODING_TIMECODED)
	{
		// u16 TimeCodes[Count]; 2 bytes of padding if Count is odd; float Samples[Components * Count]
		std::uint64_t keyBytes = (std::uint64_t)Count * 2;
		std::uint64_t pad = (Count & 1) ? 2 : 0;
		std::uint64_t sampleBytes = (std::uint64_t)Components * Count * 4;
		if ((std::uint64_t)body != keyBytes + pad + sampleBytes)
		{
			return setError(error, "timecoded motion channel body is " + std::to_string(body) + " bytes, layout needs " +
				std::to_string(keyBytes + pad + sampleBytes));
		}
		TimeCodes.resize(Count);
		if (cload.Read(TimeCodes.data(), (std::uint32_t)keyBytes) != (std::uint32_t)keyBytes)
		{
			return setError(error, "short motion channel time codes");
		}
		if (pad)
		{
			cload.Seek(2);
		}
		Samples.resize((size_t)Components * Count);
		if (cload.Read(Samples.data(), (std::uint32_t)sampleBytes) != (std::uint32_t)sampleBytes)
		{
			return setError(error, "short motion channel samples");
		}
		// FindIndex (0x001B2EFC) masks bit 15 and assumes the masked codes increase.
		for (std::uint32_t k = 1; k < Count; ++k)
		{
			if ((TimeCodes[k] & ~0x8000) <= (TimeCodes[k - 1] & ~0x8000))
			{
				return setError(error, "motion channel time code " + std::to_string(k) + " is not after time code " + std::to_string(k - 1));
			}
		}
		return true;
	}

	// Encodings 1 and 2: float Scale, float Initial[Components], then ceil(Count / 16) rows of
	// Components blocks (u8 filter index + 8 or 16 delta bytes).
	const int deltaBytes = (EncodingKind == ENCODING_ADAPTIVE_DELTA_4) ? 8 : 16;
	const int blockBytes = 1 + deltaBytes;
	std::uint64_t rows = ((std::uint64_t)Count + 15) >> 4;
	std::uint64_t need = 4 + (std::uint64_t)Components * 4 + rows * Components * blockBytes;
	if ((std::uint64_t)body != need)
	{
		return setError(error, "adaptive delta motion channel body is " + std::to_string(body) + " bytes, layout needs " + std::to_string(need));
	}
	std::vector<std::uint8_t> raw(body);
	if (cload.Read(raw.data(), body) != body)
	{
		return setError(error, "short adaptive delta motion channel data");
	}
	std::memcpy(&Scale, raw.data(), 4);
	const size_t initialAt = 4;
	const size_t blocksAt = 4 + (size_t)Components * 4;
	Packed = raw;
	Samples.assign((size_t)Count * Components, 0.0f);
	for (int vi = 0; vi < Components; vi++)
	{
		float last_value;
		std::memcpy(&last_value, &raw[initialAt + (size_t)vi * 4], sizeof(float));
		Samples[vi] = last_value;
		for (std::uint32_t frame = 1; frame < Count; frame++)
		{
			std::uint32_t row = (frame - 1) >> 4;
			int fi = (int)((frame - 1) & 15);
			const std::uint8_t *block = &raw[blocksAt + ((size_t)row * Components + vi) * blockBytes];
			float filter = AdaptiveDelta_Filter(block[0]) * Scale;
			float ffactor;
			if (EncodingKind == ENCODING_ADAPTIVE_DELTA_4)
			{
				ffactor = (float)nibbleDelta(block + 1, fi);
			}
			else
			{
				// 8 bit deltas are stored offset by 0x80 (OpenSAGE W3dAdaptiveDeltaBlock.GetDeltas flips the
				// top bit) and scaled by 1/16 so the 8 bit range matches the 4 bit one (W3dAdaptiveDeltaCodec).
				std::int8_t d = (std::int8_t)(block[1 + fi] ^ 0x80);
				ffactor = (float)d;
				filter *= (1.0f / 16.0f);
			}
			float delta = ffactor * filter;
			last_value += delta;
			Samples[(size_t)frame * Components + vi] = last_value;
		}
	}
	return true;
}

std::uint32_t BFME2MotionChannel::Get_Key_Time_Code(int key) const
{
	return EncodingKind == ENCODING_TIMECODED ? TimeCodes[key] : (std::uint32_t)key;
}

void BFME2MotionChannel::Get_Key(int key, float *vec) const
{
	std::memcpy(vec, &Samples[(size_t)key * Components], sizeof(float) * (size_t)Components);
}

// Retail BFME2Encoding0MotionChannel::FindIndex, uncached path (0x001B2EFC): index 0 at or before the
// first code, Count-1 at or after the last, else the key with code(i) <= time < code(i+1).
int BFME2MotionChannel::index_for(std::uint32_t time) const
{
	if (time <= (std::uint32_t)(TimeCodes[0] & ~0x8000))
	{
		return 0;
	}
	if (time >= (std::uint32_t)(TimeCodes[Count - 1] & ~0x8000))
	{
		return (int)Count - 1;
	}
	int low = 0;
	int high = (int)Count - 2;
	for (;;)
	{
		int index = (low + high) / 2;
		if (time < (std::uint32_t)(TimeCodes[index] & ~0x8000))
		{
			high = index;
		}
		else if (time >= (std::uint32_t)(TimeCodes[index + 1] & ~0x8000))
		{
			if (low ^ index)
			{
				low = index;
			}
			else
			{
				++low;
			}
		}
		else
		{
			return index;
		}
	}
}

// Retail encoding 0 evaluators (BFME2: scalar RVA 0x001B2FEF, vector 0x001B3159, quaternion 0x001B3313), read from the
// binary: time = (int)frame (call 0x00A29228), index by FindIndex (0x001B2EFC: 0 when time <= first code, Count-1 when
// time >= last code, else the key with code(i) <= time < code(i+1)); then hold the sample when the key is the last or
// the NEXT code has bit 15 set (test byte [..+3], 0x80 at 0x001B30A0 / 0x001B33C4), otherwise interpolate with
// factor = (frame - code(i)) / (code(i+1) - code(i)) and NO clamp (0x001B30AB-0x001B30F8, 0x001B33CF-0x001B3412).
// So a frame before the first code extrapolates backwards from the first segment, and a negative frame is past the
// last code (the comparisons are unsigned).
static std::uint32_t motionTimeOfFrame(float frame)
{
	int t = (int)frame;
	return t < 0 ? 0xFFFFFFFFu : (std::uint32_t)t;
}

void BFME2MotionChannel::Get_Vector(float frame, float *setvec) const
{
	if (EncodingKind != ENCODING_TIMECODED)
	{
		// Stream channels (RVA 0x001B2B5F etc.): frame truncated, ratio = frame - (float)(int)frame, both frames fetched
		// by the getter at 0x001B2450 and clamped to the last frame.
		std::uint32_t f1, f2;
		float ratio;
		motionStreamFrames(frame, Count, f1, f2, ratio);
		for (int i = 0; i < Components; i++)
		{
			setvec[i] = lerpf(Samples[(size_t)f1 * Components + i], Samples[(size_t)f2 * Components + i], ratio);
		}
		return;
	}
	int index = index_for(motionTimeOfFrame(frame));
	const float *a = &Samples[(size_t)index * Components];
	if (index == (int)Count - 1 || (TimeCodes[index + 1] & 0x8000))
	{
		for (int i = 0; i < Components; i++)
		{
			setvec[i] = a[i];
		}
		return;
	}
	float t0 = (float)(TimeCodes[index] & ~0x8000);
	float t1 = (float)(TimeCodes[index + 1] & ~0x8000);
	float factor = (frame - t0) / (t1 - t0);
	const float *b = a + Components;
	for (int i = 0; i < Components; i++)
	{
		setvec[i] = lerpf(a[i], b[i], factor);
	}
}

Quaternion BFME2MotionChannel::Get_QuatVector(float frame) const
{
	if (EncodingKind != ENCODING_TIMECODED)
	{
		// nlerp helper called at RVA 0x001B2B11 / 0x001B2B52 (encoding 1) and 0x001B2D76 / 0x001B2DB7 (encoding 2).
		std::uint32_t f1, f2;
		float ratio;
		motionStreamFrames(frame, Count, f1, f2, ratio);
		const float *a = &Samples[(size_t)f1 * 4];
		const float *b = &Samples[(size_t)f2 * 4];
		Quaternion q;
		BFME2_Nlerp(q, Quaternion(a[0], a[1], a[2], a[3]), Quaternion(b[0], b[1], b[2], b[3]), ratio);
		return q;
	}
	int index = index_for(motionTimeOfFrame(frame));
	const float *a = &Samples[(size_t)index * Components];
	if (index == (int)Count - 1 || (TimeCodes[index + 1] & 0x8000))
	{
		return Quaternion(a[0], a[1], a[2], a[3]);
	}
	float t0 = (float)(TimeCodes[index] & ~0x8000);
	float t1 = (float)(TimeCodes[index + 1] & ~0x8000);
	float factor = (frame - t0) / (t1 - t0);
	const float *b = a + Components;
	Quaternion q;
	BFME2_Nlerp(q, Quaternion(a[0], a[1], a[2], a[3]), Quaternion(b[0], b[1], b[2], b[3]), factor);
	return q;
}
