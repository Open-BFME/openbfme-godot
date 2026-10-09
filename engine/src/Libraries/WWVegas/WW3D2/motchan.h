// OpenBFME: faithful rebuild of The Battle for Middle-earth II: Rise of the Witch-king 2.01.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// Port of ZH Libraries/Source/WWVegas/WW3D2/motchan.h / motchan.cpp: the animation channel
// decoders.
//   MotionChannelClass                 raw channel (W3D_CHUNK_ANIMATION_CHANNEL 0x202)
//   BitChannelClass                    raw visibility channel (0x203)
//   TimeCodedMotionChannelClass        compressed flavor 0 (0x282)
//   AdaptiveDeltaMotionChannelClass    compressed flavor 1, 4-bit (0x282)
//   TimeCodedBitChannelClass           compressed visibility channel (0x283)
//   BFME2MotionChannel                 BFME2 motion channel, three encodings (0x284); the class name is
//                                      descriptive (Open-BFME-2 BFME2MotionChannelFactory.cpp: original
//                                      names are unrecovered)
//
// Deliberate differences from ZH, all of them failures made explicit:
//  * every Load_W3D validates counts against the chunk length and returns an error string; ZH
//    trusts the file and asserts.
//  * the adaptive delta channels decode all frames at load instead of caching a two frame window;
//    the accumulation order is the same, so the values are identical.
//  * BFME2 motion channels, encoding 0, before the first key: retail extrapolates (it selects index 0 and
//    interpolates with an unclamped factor unless the next key has the step flag; RVA 0x001B30AB-0x001B30F8
//    scalar, 0x001B33CF-0x001B3412 quaternion). This port does the same.
//  * classic timecoded channels before the first key: retail's binary search never terminates there (RVA
//    0x00190DCA-0x00190E1A). No source defines a value, so the evaluators report it (Is_Defined_At, false
//    return) instead of inventing one. PLAN acceptance stop.
//  * every quaternion blend is BFME2's normalised lerp (RVA 0x00717550): called from the classic timecoded
//    evaluator (0x00190EE9), the classic adaptive delta one (0x00190D09), motion encoding 0 (0x001B3412 and
//    others) and encodings 1 and 2 (0x001B2B11, 0x001B2B52, 0x001B2D76, 0x001B2DB7). Never Fast_Slerp.

#pragma once

#include "Libraries/WWVegas/WWMath/quat.h"

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

// Thrown when a channel is asked for a value at a frame no source defines: classic timecoded channels before their first
// key (retail hangs there) and BFME2 motion encodings 1 / 2 at a truncated frame of -1 or below (retail's getter wraps
// frame + 1 to zero and leaves the first blend sample unwritten). PLAN acceptance stop: loud, never a guessed value.
class UndefinedFrameError : public std::runtime_error
{
public:
	using std::runtime_error::runtime_error;
};

class ChunkLoadClass;

// ZH MotionChannelClass. Values outside [FirstFrame, LastFrame] are the identity (0, or 0 0 0 1),
// NOT the edge values: motchan.h Get_Vector / set_identity.
class MotionChannelClass
{
public:
	bool Load_W3D(ChunkLoadClass &cload, std::string *error);

	int Get_Type() const { return Type; }
	int Get_Pivot() const { return PivotIdx; }
	void Set_Pivot(int idx) { PivotIdx = idx; }
	int Get_Vector_Length() const { return VectorLen; }
	int Get_First_Frame() const { return FirstFrame; }
	int Get_Last_Frame() const { return LastFrame; }
	std::uint32_t Get_Extra_Bytes() const { return ExtraBytes; }

	void Get_Vector(int frame, float *setvec) const;
	void Get_Vector_As_Quat(int frame, Quaternion &quat) const;
	// RENDER-2: the stored values as read from the file (no arithmetic), for the deterministic launch-bone pose (GameLogic/Object/PristinePose)
	const std::vector<float> &Get_Data() const { return Data; }

private:
	std::uint32_t PivotIdx = 0;
	std::uint32_t Type = 0;
	int VectorLen = 0;
	int FirstFrame = -1;
	int LastFrame = -1;
	std::uint32_t ExtraBytes = 0; // ZH: "a bug in the exporter saved too much data"; skipped
	std::vector<float> Data;
};

// ZH BitChannelClass.
class BitChannelClass
{
public:
	bool Load_W3D(ChunkLoadClass &cload, std::string *error);

	int Get_Type() const { return Type; }
	int Get_Pivot() const { return PivotIdx; }
	void Set_Pivot(int idx) { PivotIdx = idx; }
	int Get_Bit(int frame) const;

private:
	std::uint32_t PivotIdx = 0;
	std::uint32_t Type = 0;
	int DefaultVal = 0;
	int FirstFrame = -1;
	int LastFrame = -1;
	std::vector<std::uint8_t> Bits;
};

// ZH TimeCodedMotionChannelClass. Data packets are (timecode, VectorLen floats); the MSB of a
// timecode marks a step: if the NEXT key carries it the current value is held, not interpolated.
class TimeCodedMotionChannelClass
{
public:
	bool Load_W3D(ChunkLoadClass &cload, std::string *error);

	int Get_Type() const { return Type; }
	int Get_Pivot() const { return PivotIdx; }
	int Get_Vector_Length() const { return VectorLen; }
	// False (and *setvec untouched) when the frame precedes the first key: retail hangs there, see motchan.cpp.
	bool Is_Defined_At(float frame) const;
	bool Get_Vector(float frame, float *setvec) const;
	bool Get_QuatVector(float frame, Quaternion &out) const;

	int Get_Num_Keys() const { return (int)NumTimeCodes; }
	std::uint32_t Get_Key_Time_Code(int key) const { return Data[(size_t)key * PacketSize]; } // raw, MSB = step
	void Get_Key(int key, float *vec) const;
	std::uint32_t Get_Extra_Bytes() const { return ExtraBytes; }

private:
	std::uint32_t index_for(std::uint32_t timecode) const;

	std::uint32_t PivotIdx = 0;
	std::uint32_t Type = 0;
	int VectorLen = 0;
	std::uint32_t PacketSize = 0;
	std::uint32_t NumTimeCodes = 0;
	std::uint32_t ExtraBytes = 0;
	std::vector<std::uint32_t> Data; // NumTimeCodes * PacketSize words
};

// ZH TimeCodedBitChannelClass.
class TimeCodedBitChannelClass
{
public:
	bool Load_W3D(ChunkLoadClass &cload, std::string *error);

	int Get_Type() const { return Type; }
	int Get_Pivot() const { return PivotIdx; }
	int Get_Bit(int frame) const;

	int Get_Num_Keys() const { return (int)Bits.size(); }

private:
	std::uint32_t PivotIdx = 0;
	std::uint32_t Type = 0;
	int DefaultVal = 0; // read from the file but, as in ZH, never consulted by Get_Bit
	std::vector<std::uint32_t> Bits;
};

// ZH AdaptiveDeltaMotionChannelClass (4-bit nybble packets of 9 bytes per component).
class AdaptiveDeltaMotionChannelClass
{
public:
	bool Load_W3D(ChunkLoadClass &cload, std::string *error);

	int Get_Type() const { return Type; }
	int Get_Pivot() const { return PivotIdx; }
	int Get_Vector_Length() const { return VectorLen; }
	void Get_Vector(float frame, float *setvec) const;
	Quaternion Get_QuatVector(float frame) const;

	int Get_Num_Keys() const { return (int)NumFrames; }
	void Get_Key(int frame, float *vec) const;
	std::uint32_t Get_Extra_Bytes() const { return ExtraBytes; }
	// RENDER-2: the packed data as read (VectorLen initial floats, then the 9 byte packets) and the scale, for the deterministic decoder
	const std::vector<std::uint8_t> &Get_Packed() const { return Packed; }
	float Get_Scale() const { return Scale; }

private:
	std::uint32_t PivotIdx = 0;
	std::uint32_t Type = 0;
	int VectorLen = 0;
	std::uint32_t NumFrames = 0;
	float Scale = 0.0f;
	std::uint32_t ExtraBytes = 0;
	std::vector<float> Frames; // NumFrames * VectorLen, decoded
	std::vector<std::uint8_t> Packed;
};

// BFME2 motion channel (chunk 0x284). Retail: Load_BFME2MotionChannel 0x001A46B4 reads the 8 byte
// header and builds BFME2Encoding0MotionChannel (0x001B2E69 Load) or the stream channel (0x001B2168
// Load, encodings 1 and 2).
class BFME2MotionChannel
{
public:
	enum Encoding
	{
		ENCODING_TIMECODED = 0,
		ENCODING_ADAPTIVE_DELTA_4 = 1,
		ENCODING_ADAPTIVE_DELTA_8 = 2,
	};

	bool Load_W3D(ChunkLoadClass &cload, std::string *error);

	int Get_Type() const { return Type; }
	int Get_Pivot() const { return PivotIdx; }
	int Get_Components() const { return Components; }
	int Get_Encoding() const { return EncodingKind; }
	int Get_Count() const { return (int)Count; }

	void Get_Vector(float frame, float *setvec) const;
	Quaternion Get_QuatVector(float frame) const;

	// Keys: for encoding 0 the stored time codes; for 1 / 2 every frame (time code = frame number).
	int Get_Num_Keys() const { return (int)Count; }
	std::uint32_t Get_Key_Time_Code(int key) const; // raw, bit 15 = step (encoding 0 only)
	void Get_Key(int key, float *vec) const;
	// RENDER-2: encodings 1 / 2 as read (the body after the header: scale, initial values, blocks), for the deterministic decoder
	const std::vector<std::uint8_t> &Get_Packed() const { return Packed; }

private:
	int index_for(std::uint32_t time) const;

	int EncodingKind = 0;
	int Type = 0;
	int PivotIdx = 0;
	int Components = 0;
	std::uint32_t Count = 0;
	float Scale = 0.0f;
	std::vector<std::uint16_t> TimeCodes; // encoding 0
	std::vector<float> Samples;           // Count * Components (decoded for encodings 1 / 2)
	std::vector<std::uint8_t> Packed;     // encodings 1 / 2: the body as read
};

// The 256 entry filter table of the adaptive delta decompressor (motchan.cpp filtertable and its
// constructor-time fill of entries 16..255). Exposed for tests. RENDER-2: the table is PristinePose::adaptiveDeltaFilter, computed
// with the numeric facade under the canonical floating-point environment (never the caller's), so the drawn and the launch-bone
// decodes use the same deterministic constants.
float AdaptiveDelta_Filter(unsigned int index);
