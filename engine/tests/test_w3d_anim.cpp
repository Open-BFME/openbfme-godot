// OpenBFME unit tests: animation channel decoders on synthetic chunk bytes. GPL-3.0.
//
// Every expected value is computed by hand from the format rules (ZH motchan.cpp / hcanim.cpp /
// hrawanim.cpp, BFME2 BFME2MotionChannelFactory.cpp) in the comment above the case; none is taken from
// the decoder under test.

#include "doctest.h"
#include "W3dTestUtil.h"

#include "Libraries/WWVegas/WW3D2/assetmgr.h"
#include "Libraries/WWVegas/WW3D2/hcanim.h"
#include "Libraries/WWVegas/WW3D2/hrawanim.h"
#include "Libraries/WWVegas/WW3D2/motchan.h"
#include "Libraries/WWVegas/WW3D2/w3d_file.h"
#include "Libraries/WWVegas/WWLib/chunkio.h"

#include <cmath>

using namespace w3dtest;
using doctest::Approx;

namespace
{

// A single data chunk wrapped in a stream, opened and ready for a channel's Load_W3D.
struct OneChunk
{
	std::vector<std::uint8_t> bytes;
	explicit OneChunk(std::uint32_t id, const Bytes &body)
	{
		ChunkWriter w;
		w.chunk(id, body.v);
		bytes = w.bytes;
	}
};

template <typename Channel>
bool loadChannel(Channel &channel, std::uint32_t id, const Bytes &body, std::string &error)
{
	OneChunk c(id, body);
	ChunkLoadClass cload(c.bytes.data(), c.bytes.size());
	REQUIRE(cload.Open_Chunk());
	return channel.Load_W3D(cload, &error);
}

// 0x282 time coded body: u32 numKeys; u16 pivot; u8 vectorLen; u8 type; then (u32 time, floats) per key.
Bytes timeCoded(std::uint16_t pivot, std::uint8_t vlen, std::uint8_t type, const std::vector<std::pair<std::uint32_t, std::vector<float>>> &keys)
{
	Bytes b;
	b.u32((std::uint32_t)keys.size()).u16(pivot).u8(vlen).u8(type);
	for (const auto &k : keys)
	{
		b.u32(k.first);
		for (float f : k.second)
		{
			b.f32(f);
		}
	}
	return b;
}

// 0x282 adaptive delta body: u32 frames; u16 pivot; u8 vectorLen; u8 type; float scale; float initial[vl]; rows.
Bytes adaptiveHeader(std::uint32_t frames, std::uint16_t pivot, std::uint8_t vlen, std::uint8_t type, float scale, const std::vector<float> &initial)
{
	Bytes b;
	b.u32(frames).u16(pivot).u8(vlen).u8(type).f32(scale);
	for (float f : initial)
	{
		b.f32(f);
	}
	return b;
}

// One 9 byte block: filter index then 8 bytes of nibble pairs (even index = low nibble).
void block4(Bytes &b, std::uint8_t filter, const std::vector<int> &nibbles)
{
	b.u8(filter);
	for (int i = 0; i < 16; i += 2)
	{
		int lo = i < (int)nibbles.size() ? nibbles[i] : 0;
		int hi = i + 1 < (int)nibbles.size() ? nibbles[i + 1] : 0;
		b.u8((std::uint8_t)((lo & 0xF) | ((hi & 0xF) << 4)));
	}
}

// 0x284 body. keys / samples for encoding 0; for 1/2 `rest` is everything after the 8 byte header.
Bytes motionHeader(std::uint8_t encoding, std::uint8_t comps, std::uint8_t type, std::uint16_t count, std::uint16_t pivot, std::uint8_t version = 0)
{
	Bytes b;
	b.u8(version).u8(encoding).u8(comps).u8(type).u16(count).u16(pivot);
	return b;
}

} // namespace

TEST_CASE("adaptive delta filter table: entries 0-15 are decades, 16-255 are 1 - sin(90 deg * i / 240)")
{
	CHECK(AdaptiveDelta_Filter(0) == Approx(1e-8f).epsilon(1e-6));
	CHECK(AdaptiveDelta_Filter(8) == Approx(1.0f));
	CHECK(AdaptiveDelta_Filter(15) == Approx(1e7f));
	CHECK(AdaptiveDelta_Filter(16) == Approx(1.0f)); // 1 - sin(0)
	// index 16 + 120: 1 - sin(45 deg) = 1 - 0.70710678
	CHECK(AdaptiveDelta_Filter(16 + 120) == Approx(0.29289322f).epsilon(1e-5));
	// index 255: 1 - sin(90 * 239 / 240 deg) = 1 - cos(0.375 deg) = 2.14183e-5 (series: x^2/2 - x^4/24, x = 0.00654498)
	CHECK(AdaptiveDelta_Filter(255) == Approx(2.14183e-5f).epsilon(1e-3));
}

TEST_CASE("adaptive delta 4-bit channel: nibbles accumulate per 16-frame packet, filter index scales the packet")
{
	// 18 frames = two packet rows. Scale 0.5, initial 10. Row 0 uses filter 8 (1.0) so each nibble is worth
	// 0.5; nibbles for frames 1..16: 1 2 3 -1 -2 -3 7 -8 0 0 0 0 0 0 0 1. Row 1 uses filter 9 (10.0): worth 5;
	// its first nibble (frame 17) is 2.
	Bytes b = adaptiveHeader(18, 4, 1, ANIM_CHANNEL_X, 0.5f, { 10.0f });
	block4(b, 8, { 1, 2, 3, -1, -2, -3, 7, -8, 0, 0, 0, 0, 0, 0, 0, 1 });
	block4(b, 9, { 2 });
	AdaptiveDeltaMotionChannelClass ch;
	std::string error;
	REQUIRE_MESSAGE(loadChannel(ch, W3D_CHUNK_COMPRESSED_ANIMATION_CHANNEL, b, error), error);
	CHECK(ch.Get_Pivot() == 4);
	CHECK(ch.Get_Num_Keys() == 18);
	// 10, +0.5, +1.0, +1.5, -0.5, -1.0, -1.5, +3.5, -4.0, then flat, +0.5, then +10
	const float expected[18] = { 10.0f, 10.5f, 11.5f, 13.0f, 12.5f, 11.5f, 10.0f, 13.5f, 9.5f, 9.5f, 9.5f, 9.5f, 9.5f, 9.5f, 9.5f, 9.5f, 10.0f, 20.0f };
	for (int f = 0; f < 18; ++f)
	{
		float v;
		ch.Get_Key(f, &v);
		CHECK_MESSAGE(v == Approx(expected[f]), "frame " << f);
	}
	float v;
	ch.Get_Vector(1.5f, &v);
	CHECK(v == Approx(11.0f)); // lerp(10.5, 11.5, 0.5)
	ch.Get_Vector(17.0f, &v);
	CHECK(v == Approx(20.0f));
	ch.Get_Vector(40.0f, &v); // clamps to the last frame
	CHECK(v == Approx(20.0f));
}

TEST_CASE("adaptive delta quaternion channel interleaves one block per component and blends")
{
	// 2 frames, one packet row, 4 components in x y z w order. Scale 0.1, filter 8 (1.0): a nibble is worth 0.1.
	// z nibble 6 -> +0.6, w nibble -2 -> -0.2: frame 1 = (0, 0, 0.6, 0.8), a unit quaternion.
	Bytes b = adaptiveHeader(2, 0, 4, ANIM_CHANNEL_Q, 0.1f, { 0.0f, 0.0f, 0.0f, 1.0f });
	block4(b, 8, {});
	block4(b, 8, {});
	block4(b, 8, { 6 });
	block4(b, 8, { -2 });
	AdaptiveDeltaMotionChannelClass ch;
	std::string error;
	REQUIRE_MESSAGE(loadChannel(ch, W3D_CHUNK_COMPRESSED_ANIMATION_CHANNEL, b, error), error);
	float q[4];
	ch.Get_Key(1, q);
	CHECK(q[2] == Approx(0.6f));
	CHECK(q[3] == Approx(0.8f));
	// Half way: (0.5 * (0,0,0,1) + 0.5 * (0,0,0.6,0.8)) = (0, 0, 0.3, 0.9), normalised by sqrt(0.9):
	// (0, 0, sqrt(0.1), sqrt(0.9)).
	Quaternion mid = ch.Get_QuatVector(0.5f);
	CHECK(mid.X == Approx(0.0f).epsilon(1e-5));
	CHECK(mid.Y == Approx(0.0f).epsilon(1e-5));
	CHECK(mid.Z == Approx(0.31622777f).epsilon(1e-5));
	CHECK(mid.W == Approx(0.9486833f).epsilon(1e-5));
}

// BFME2 truncates the frame and keeps the signed remainder (cvttss2si; classic RVA 0x00190CCB-0x00190D09, motion
// encodings 0x001B2AC4-0x001B2B52 and 0x001B2D29-0x001B2DB7): -0.25 is frame 0 with ratio -0.25, so the blend
// extrapolates backwards. Between (0,0,0,1) and (0,0,0.6,0.8): 1.25 * (0,0,0,1) - 0.25 * (0,0,0.6,0.8) = (0,0,-0.15,1.05),
// length sqrt(0.0225 + 1.1025) = sqrt(1.125), result (0, 0, -0.141421, 0.989949).
TEST_CASE("adaptive delta evaluators keep the sign of a negative fractional frame (extrapolate), they do not clamp to frame 0")
{
	auto check = [](const Quaternion &q) {
		CHECK(q.X == Approx(0.0f).epsilon(1e-5));
		CHECK(q.Z == Approx(-0.141421f).epsilon(1e-4));
		CHECK(q.W == Approx(0.989949f).epsilon(1e-5));
	};
	std::string error;
	{
		Bytes b = adaptiveHeader(2, 0, 4, ANIM_CHANNEL_Q, 0.1f, { 0.0f, 0.0f, 0.0f, 1.0f });
		block4(b, 8, {});
		block4(b, 8, {});
		block4(b, 8, { 6 });
		block4(b, 8, { -2 });
		AdaptiveDeltaMotionChannelClass ch;
		REQUIRE_MESSAGE(loadChannel(ch, W3D_CHUNK_COMPRESSED_ANIMATION_CHANNEL, b, error), error);
		check(ch.Get_QuatVector(-0.25f));
	}
	{
		Bytes b = motionHeader(1, 4, ANIM_CHANNEL_Q, 2, 0);
		b.f32(0.1f).f32(0).f32(0).f32(0).f32(1);
		block4(b, 8, {});
		block4(b, 8, {});
		block4(b, 8, { 6 });
		block4(b, 8, { -2 });
		BFME2MotionChannel ch;
		REQUIRE_MESSAGE(loadChannel(ch, W3D_CHUNK_COMPRESSED_ANIMATION_MOTION_CHANNEL, b, error), error);
		check(ch.Get_QuatVector(-0.25f));
	}
	{
		Bytes b = motionHeader(2, 4, ANIM_CHANNEL_Q, 2, 0);
		b.f32(0.1f).f32(0).f32(0).f32(0).f32(1);
		for (int comp = 0; comp < 4; ++comp)
		{
			b.u8(8);
			for (int i = 0; i < 16; ++i)
			{
				b.u8(i != 0 ? 0x80 : comp == 2 ? 0xE0 : comp == 3 ? 0x60 : 0x80);
			}
		}
		BFME2MotionChannel ch;
		REQUIRE_MESSAGE(loadChannel(ch, W3D_CHUNK_COMPRESSED_ANIMATION_MOTION_CHANNEL, b, error), error);
		check(ch.Get_QuatVector(-0.25f));
	}
	{
		// scalars: frames 10, 10.5 -> frame -0.25 is 10 + (-0.25) * 0.5 = 9.875
		Bytes b = adaptiveHeader(2, 0, 1, ANIM_CHANNEL_X, 0.5f, { 10.0f });
		block4(b, 8, { 1 });
		AdaptiveDeltaMotionChannelClass ch;
		REQUIRE_MESSAGE(loadChannel(ch, W3D_CHUNK_COMPRESSED_ANIMATION_CHANNEL, b, error), error);
		float v;
		ch.Get_Vector(-0.25f, &v);
		CHECK(v == Approx(9.875f));
		// BFME2 encoding 1 scalar: frames 1, 7 (scale 2, nibble 3) -> 1 + (-0.25) * 6 = -0.5
		Bytes m = motionHeader(1, 1, ANIM_CHANNEL_Y, 2, 0);
		m.f32(2.0f).f32(1.0f);
		block4(m, 8, { 3 });
		BFME2MotionChannel mc;
		REQUIRE_MESSAGE(loadChannel(mc, W3D_CHUNK_COMPRESSED_ANIMATION_MOTION_CHANNEL, m, error), error);
		mc.Get_Vector(-0.25f, &v);
		CHECK(v == Approx(-0.5f));
	}
}

// Frames of -1 or below: the classic getter compares the truncated index unsigned and clamps to the last frame
// (BFME2 0x0018FCF5-0x0018FCF9), the decoder repeating the endpoint for the next sample. The motion encodings 1 / 2 getters wrap
// frame + 1 to zero and leave the first blend sample unwritten (0x001B2486, 0x001B24EA-0x001B2501; 0x001B27DF,
// 0x001B283C-0x001B2858): no defined result, so they throw instead.
TEST_CASE("frames of -1 or below: classic adaptive delta clamps to the last frame, motion encodings 1 and 2 are undefined")
{
	std::string error;
	{
		Bytes b = adaptiveHeader(2, 0, 4, ANIM_CHANNEL_Q, 0.1f, { 0.0f, 0.0f, 0.0f, 1.0f });
		block4(b, 8, {});
		block4(b, 8, {});
		block4(b, 8, { 6 });
		block4(b, 8, { -2 });
		AdaptiveDeltaMotionChannelClass ch;
		REQUIRE_MESSAGE(loadChannel(ch, W3D_CHUNK_COMPRESSED_ANIMATION_CHANNEL, b, error), error);
		// both samples are the last frame (0,0,0.6,0.8), a unit quaternion: nlerp of it with itself is itself at any ratio
		Quaternion q = ch.Get_QuatVector(-1.5f);
		CHECK(q.Z == Approx(0.6f).epsilon(1e-5));
		CHECK(q.W == Approx(0.8f).epsilon(1e-5));
	}
	{
		Bytes b = motionHeader(1, 4, ANIM_CHANNEL_Q, 2, 0);
		b.f32(0.1f).f32(0).f32(0).f32(0).f32(1);
		block4(b, 8, {});
		block4(b, 8, {});
		block4(b, 8, { 6 });
		block4(b, 8, { -2 });
		BFME2MotionChannel ch;
		REQUIRE_MESSAGE(loadChannel(ch, W3D_CHUNK_COMPRESSED_ANIMATION_MOTION_CHANNEL, b, error), error);
		CHECK_THROWS_AS(ch.Get_QuatVector(-1.0f), UndefinedFrameError);
		CHECK_THROWS_AS(ch.Get_QuatVector(-2.5f), UndefinedFrameError);
		float v[4];
		CHECK_THROWS_AS(ch.Get_Vector(-1.5f, v), UndefinedFrameError);
		CHECK_NOTHROW(ch.Get_QuatVector(-0.99f)); // truncates to 0: defined
	}
	{
		Bytes b = motionHeader(2, 1, ANIM_CHANNEL_X, 2, 0);
		b.f32(1.0f).f32(0.0f);
		b.u8(8);
		for (int i = 0; i < 16; ++i)
		{
			b.u8(0x80);
		}
		BFME2MotionChannel ch;
		REQUIRE_MESSAGE(loadChannel(ch, W3D_CHUNK_COMPRESSED_ANIMATION_MOTION_CHANNEL, b, error), error);
		float v;
		CHECK_THROWS_AS(ch.Get_Vector(-1.0f, &v), UndefinedFrameError);
		CHECK_NOTHROW(ch.Get_Vector(-0.5f, &v));
	}
}

TEST_CASE("adaptive delta channel with a body shorter than its packet rows is refused")
{
	Bytes b = adaptiveHeader(18, 0, 1, ANIM_CHANNEL_X, 1.0f, { 0.0f });
	block4(b, 8, {}); // second row missing
	AdaptiveDeltaMotionChannelClass ch;
	std::string error;
	CHECK_FALSE(loadChannel(ch, W3D_CHUNK_COMPRESSED_ANIMATION_CHANNEL, b, error));
	CHECK(error.find("needs") != std::string::npos);
}

TEST_CASE("time coded channel: lerp between keys, hold across a binary-flagged key, hold at the ends")
{
	const std::uint32_t STEP = W3D_TIMECODED_BINARY_MOVEMENT_FLAG;
	TimeCodedMotionChannelClass ch;
	std::string error;
	REQUIRE_MESSAGE(loadChannel(ch, W3D_CHUNK_COMPRESSED_ANIMATION_CHANNEL,
		timeCoded(3, 1, ANIM_CHANNEL_X, { { 0, { 0.0f } }, { 10, { 10.0f } }, { 20 | STEP, { 50.0f } }, { 30, { 70.0f } } }), error), error);
	CHECK(ch.Get_Pivot() == 3);
	CHECK(ch.Get_Num_Keys() == 4);
	CHECK((ch.Get_Key_Time_Code(2) & STEP) == STEP);
	struct { float frame, expected; } cases[] = {
		{ 0.0f, 0.0f }, { 5.0f, 5.0f },   // lerp 0 -> 10
		{ 10.0f, 10.0f },                 // key 1; the next key is a step so the value is held ...
		{ 15.0f, 10.0f }, { 19.5f, 10.0f }, // ... up to the stepped key
		{ 20.0f, 50.0f },                 // the step lands: key 2, lerp towards key 3 with ratio 0
		{ 25.0f, 60.0f },                 // 50 + (70 - 50) * 0.5
		{ 30.0f, 70.0f }, { 99.0f, 70.0f }, // last key held
	};
	for (const auto &c : cases)
	{
		float v;
		ch.Get_Vector(c.frame, &v);
		CHECK_MESSAGE(v == Approx(c.expected), "frame " << c.frame);
	}
}

TEST_CASE("classic time coded channel: a frame before the first key is reported, not invented")
{
	// Retail (BFME2 RVA 0x00190DCA-0x00190E1A) runs a binary search that never terminates for a time before the first
	// key; no source defines a value, so the evaluators refuse (PLAN acceptance stop). A negative frame compares
	// unsigned in retail (0x00190D80) and so selects the LAST key.
	TimeCodedMotionChannelClass ch;
	std::string error;
	REQUIRE_MESSAGE(loadChannel(ch, W3D_CHUNK_COMPRESSED_ANIMATION_CHANNEL,
		timeCoded(0, 1, ANIM_CHANNEL_Y, { { 4, { 1.0f } }, { 8, { 5.0f } } }), error), error);
	float v = -99.0f;
	CHECK_FALSE(ch.Is_Defined_At(0.0f));
	CHECK_FALSE(ch.Get_Vector(0.0f, &v));
	CHECK_FALSE(ch.Get_Vector(3.9f, &v));
	CHECK(v == -99.0f); // untouched
	CHECK(ch.Is_Defined_At(4.0f));
	REQUIRE(ch.Get_Vector(4.0f, &v));
	CHECK(v == Approx(1.0f));
	REQUIRE(ch.Get_Vector(6.0f, &v));
	CHECK(v == Approx(3.0f)); // lerp(1, 5, 0.5)
	REQUIRE(ch.Get_Vector(-3.0f, &v));
	CHECK(v == Approx(5.0f)); // negative frame: past every key
	Quaternion q;
	TimeCodedMotionChannelClass qc;
	REQUIRE_MESSAGE(loadChannel(qc, W3D_CHUNK_COMPRESSED_ANIMATION_CHANNEL,
		timeCoded(0, 4, ANIM_CHANNEL_Q, { { 4, { 0, 0, 0, 1 } }, { 8, { 0, 0, 0.6f, 0.8f } } }), error), error);
	CHECK_FALSE(qc.Get_QuatVector(1.0f, q));
}

TEST_CASE("time coded channel: binary search over many keys reproduces a linear ramp exactly")
{
	// Values are 2 * time, so lerping any bracket must give 2 * frame at every frame.
	std::vector<std::pair<std::uint32_t, std::vector<float>>> keys;
	for (std::uint32_t t : { 0u, 3u, 7u, 12u, 20u, 31u, 45u, 46u, 80u })
	{
		keys.push_back({ t, { (float)(2 * t) } });
	}
	TimeCodedMotionChannelClass ch;
	std::string error;
	REQUIRE_MESSAGE(loadChannel(ch, W3D_CHUNK_COMPRESSED_ANIMATION_CHANNEL, timeCoded(0, 1, ANIM_CHANNEL_Z, keys), error), error);
	for (float f = 0.0f; f <= 80.0f; f += 0.5f)
	{
		float v;
		ch.Get_Vector(f, &v);
		CHECK_MESSAGE(v == Approx(2.0f * f).epsilon(1e-5), "frame " << f);
	}
}

TEST_CASE("time coded quaternion channel blends with BFME2's nlerp (RVA 0x00190EE9), not slerp")
{
	// 90 degrees about Z: (0, 0, sin45, cos45). At the midpoint the angle is 45 degrees:
	// (0, 0, sin(22.5), cos(22.5)) = (0, 0, 0.38268343, 0.92387953).
	float s = 0.70710678f;
	TimeCodedMotionChannelClass ch;
	std::string error;
	REQUIRE_MESSAGE(loadChannel(ch, W3D_CHUNK_COMPRESSED_ANIMATION_CHANNEL,
		timeCoded(0, 4, ANIM_CHANNEL_Q, { { 0, { 0, 0, 0, 1 } }, { 10, { 0, 0, s, s } } }), error), error);
	Quaternion q;
	REQUIRE(ch.Get_QuatVector(5.0f, q));
	CHECK(q.X == Approx(0.0f).epsilon(1e-5));
	CHECK(q.Z == Approx(0.38268343f).epsilon(1e-4)); // the midpoint of nlerp and slerp coincide for symmetric endpoints
	CHECK(q.W == Approx(0.92387953f).epsilon(1e-4));
	CHECK(q.Length2() == Approx(1.0f).epsilon(1e-4));
}

// nlerp, not slerp, between (0,0,0,1) and (0,0,0.6,0.8) at fraction 0.25. The unnormalised blend is
// 0.75 * (0,0,0,1) + 0.25 * (0,0,0.6,0.8) = (0, 0, 0.15, 0.95); its length is sqrt(0.0225 + 0.9025) = sqrt(0.925), so
// the result is (0, 0, 0.15 / sqrt(0.925), 0.95 / sqrt(0.925)) = (0, 0, 0.155963, 0.987763). Slerp would give
// (0, 0, 0.160182, 0.987087): the two differ at every fraction except the midpoint of symmetric endpoints.
// Retail blends with nlerp in the classic timecoded (0x00190EE9), classic adaptive delta (0x00190D09) and motion
// encoding 1 (0x001B2B11, 0x001B2B52) and 2 (0x001B2D76, 0x001B2DB7) evaluators, and in encoding 0 (0x001B3412).
static void checkQuarterNlerp(const Quaternion &q)
{
	CHECK(q.X == Approx(0.0f).epsilon(1e-5));
	CHECK(q.Y == Approx(0.0f).epsilon(1e-5));
	CHECK(q.Z == Approx(0.155963f).epsilon(1e-5));
	CHECK(q.W == Approx(0.987763f).epsilon(1e-5));
}

TEST_CASE("every compressed quaternion path blends with nlerp: fraction 0.25 between (0,0,0,1) and (0,0,0.6,0.8)")
{
	std::string error;
	{
		TimeCodedMotionChannelClass ch;
		REQUIRE_MESSAGE(loadChannel(ch, W3D_CHUNK_COMPRESSED_ANIMATION_CHANNEL,
			timeCoded(0, 4, ANIM_CHANNEL_Q, { { 0, { 0, 0, 0, 1 } }, { 4, { 0, 0, 0.6f, 0.8f } } }), error), error);
		Quaternion q;
		REQUIRE(ch.Get_QuatVector(1.0f, q));
		checkQuarterNlerp(q);
	}
	{
		// adaptive delta: scale 0.1, filter 8 (1.0): z nibble 6 -> +0.6, w nibble -2 -> -0.2
		Bytes b = adaptiveHeader(2, 0, 4, ANIM_CHANNEL_Q, 0.1f, { 0.0f, 0.0f, 0.0f, 1.0f });
		block4(b, 8, {});
		block4(b, 8, {});
		block4(b, 8, { 6 });
		block4(b, 8, { -2 });
		AdaptiveDeltaMotionChannelClass ch;
		REQUIRE_MESSAGE(loadChannel(ch, W3D_CHUNK_COMPRESSED_ANIMATION_CHANNEL, b, error), error);
		checkQuarterNlerp(ch.Get_QuatVector(0.25f));
	}
	{
		Bytes b = motionHeader(0, 4, ANIM_CHANNEL_Q, 2, 0);
		b.u16(0).u16(4);
		b.f32(0).f32(0).f32(0).f32(1).f32(0).f32(0).f32(0.6f).f32(0.8f);
		BFME2MotionChannel ch;
		REQUIRE_MESSAGE(loadChannel(ch, W3D_CHUNK_COMPRESSED_ANIMATION_MOTION_CHANNEL, b, error), error);
		checkQuarterNlerp(ch.Get_QuatVector(1.0f));
	}
	{
		Bytes b = motionHeader(1, 4, ANIM_CHANNEL_Q, 2, 0);
		b.f32(0.1f).f32(0).f32(0).f32(0).f32(1);
		block4(b, 8, {});
		block4(b, 8, {});
		block4(b, 8, { 6 });
		block4(b, 8, { -2 });
		BFME2MotionChannel ch;
		REQUIRE_MESSAGE(loadChannel(ch, W3D_CHUNK_COMPRESSED_ANIMATION_MOTION_CHANNEL, b, error), error);
		checkQuarterNlerp(ch.Get_QuatVector(0.25f));
	}
	{
		// 8-bit deltas: d / 16 * filter(1.0) * scale(0.1); d = 96 -> +0.6 (stored 96 ^ 0x80 = 0xE0), d = -32 -> -0.2 (stored 0x60)
		Bytes b = motionHeader(2, 4, ANIM_CHANNEL_Q, 2, 0);
		b.f32(0.1f).f32(0).f32(0).f32(0).f32(1);
		for (int comp = 0; comp < 4; ++comp)
		{
			b.u8(8);
			for (int i = 0; i < 16; ++i)
			{
				b.u8(i != 0 ? 0x80 : comp == 2 ? 0xE0 : comp == 3 ? 0x60 : 0x80);
			}
		}
		BFME2MotionChannel ch;
		REQUIRE_MESSAGE(loadChannel(ch, W3D_CHUNK_COMPRESSED_ANIMATION_MOTION_CHANNEL, b, error), error);
		checkQuarterNlerp(ch.Get_QuatVector(0.25f));
	}
}

TEST_CASE("time coded channel with out of order keys or a short body is refused")
{
	TimeCodedMotionChannelClass ch;
	std::string error;
	CHECK_FALSE(loadChannel(ch, W3D_CHUNK_COMPRESSED_ANIMATION_CHANNEL,
		timeCoded(0, 1, ANIM_CHANNEL_X, { { 5, { 0.0f } }, { 5, { 1.0f } } }), error));
	CHECK(error.find("not after") != std::string::npos);

	Bytes shortBody = timeCoded(0, 1, ANIM_CHANNEL_X, { { 0, { 0.0f } } });
	shortBody.v[0] = 3; // claims 3 keys, holds 1
	CHECK_FALSE(loadChannel(ch, W3D_CHUNK_COMPRESSED_ANIMATION_CHANNEL, shortBody, error));
	CHECK(error.find("needs") != std::string::npos);

	// a quaternion channel must have 4 components
	CHECK_FALSE(loadChannel(ch, W3D_CHUNK_COMPRESSED_ANIMATION_CHANNEL, timeCoded(0, 1, ANIM_CHANNEL_Q, { { 0, { 0.0f } } }), error));
	CHECK(error.find("retail channel shape") != std::string::npos);
}

TEST_CASE("raw channel: identity outside [first, last], skipped exporter padding is counted")
{
	// Quaternion channel over frames 2..4; 3 frames * 4 floats, plus 8 junk bytes the exporter appended.
	Bytes b;
	b.u16(2).u16(4).u16(4).u16(ANIM_CHANNEL_Q).u16(9).u16(0);
	for (int i = 0; i < 12; ++i)
	{
		b.f32((float)(i + 1));
	}
	b.u32(0xDEADBEEF).u32(0xCAFEBABE);
	MotionChannelClass ch;
	std::string error;
	REQUIRE_MESSAGE(loadChannel(ch, W3D_CHUNK_ANIMATION_CHANNEL, b, error), error);
	CHECK(ch.Get_Pivot() == 9);
	CHECK(ch.Get_Extra_Bytes() == 8);
	float v[4];
	ch.Get_Vector(3, v); // second frame: floats 5..8
	CHECK(v[0] == 5.0f);
	CHECK(v[3] == 8.0f);
	ch.Get_Vector(1, v); // before First: ZH set_identity, not the edge value
	CHECK(v[0] == 0.0f);
	CHECK(v[1] == 0.0f);
	CHECK(v[2] == 0.0f);
	CHECK(v[3] == 1.0f);
	Quaternion q;
	ch.Get_Vector_As_Quat(5, q); // after Last
	CHECK(q.W == 1.0f);
	CHECK(q.X == 0.0f);
}

TEST_CASE("raw channel: a scalar channel past its range is 0, and a short body is refused")
{
	Bytes b;
	b.u16(0).u16(1).u16(1).u16(ANIM_CHANNEL_X).u16(0).u16(0).f32(7.0f).f32(8.0f);
	MotionChannelClass ch;
	std::string error;
	REQUIRE_MESSAGE(loadChannel(ch, W3D_CHUNK_ANIMATION_CHANNEL, b, error), error);
	float v;
	ch.Get_Vector(1, &v);
	CHECK(v == 8.0f);
	ch.Get_Vector(2, &v);
	CHECK(v == 0.0f);

	Bytes shortBody;
	shortBody.u16(0).u16(3).u16(1).u16(ANIM_CHANNEL_X).u16(0).u16(0).f32(1.0f);
	MotionChannelClass bad;
	CHECK_FALSE(loadChannel(bad, W3D_CHUNK_ANIMATION_CHANNEL, shortBody, error));
	CHECK(error.find("needs") != std::string::npos);
}

TEST_CASE("raw bit channel: bit n of the data is frame First + n, default outside the range")
{
	// frames 2..11, default 1; byte 0 = 0b00000101 (frames 2 and 4), byte 1 = 0b00000010 (frame 11)
	Bytes b;
	b.u16(2).u16(11).u16(BIT_CHANNEL_VIS).u16(3).u8(1).u8(0x05).u8(0x02);
	BitChannelClass ch;
	std::string error;
	REQUIRE_MESSAGE(loadChannel(ch, W3D_CHUNK_BIT_CHANNEL, b, error), error);
	CHECK(ch.Get_Pivot() == 3);
	const int expected[] = { 1, 1, 1, 0, 1, 0, 0, 0, 0, 0, 0, 1, 1 }; // frames 0..12
	for (int f = 0; f <= 12; ++f)
	{
		CHECK_MESSAGE(ch.Get_Bit(f) == expected[f], "frame " << f);
	}
}

TEST_CASE("compressed bit channel: the bit lives in the MSB of each time code")
{
	const std::uint32_t ON = W3D_TIMECODED_BIT_MASK;
	Bytes b;
	b.u32(4).u16(1).u8(BIT_CHANNEL_VIS).u8(1);
	b.u32(0 | ON).u32(5).u32(9 | ON).u32(20);
	TimeCodedBitChannelClass ch;
	std::string error;
	REQUIRE_MESSAGE(loadChannel(ch, W3D_CHUNK_COMPRESSED_BIT_CHANNEL, b, error), error);
	CHECK(ch.Get_Pivot() == 1);
	struct { int frame, bit; } cases[] = { { -1, 1 }, { 0, 1 }, { 4, 1 }, { 5, 0 }, { 8, 0 }, { 9, 1 }, { 19, 1 }, { 20, 0 }, { 100, 0 } };
	for (const auto &c : cases)
	{
		CHECK_MESSAGE(ch.Get_Bit(c.frame) == c.bit, "frame " << c.frame);
	}
}



TEST_CASE("BFME2 motion channel encoding 0: 16-bit time codes with a step flag in bit 15, padded to 4 bytes")
{
	// 3 keys: times 0, 10, 20 | 0x8000; odd count so 2 bytes of padding follow; samples 1, 2, 4.
	Bytes b = motionHeader(0, 1, ANIM_CHANNEL_X, 3, 7);
	b.u16(0).u16(10).u16(0x8000 | 20).u16(0);
	b.f32(1.0f).f32(2.0f).f32(4.0f);
	BFME2MotionChannel ch;
	std::string error;
	REQUIRE_MESSAGE(loadChannel(ch, W3D_CHUNK_COMPRESSED_ANIMATION_MOTION_CHANNEL, b, error), error);
	CHECK(ch.Get_Pivot() == 7);
	CHECK(ch.Get_Encoding() == BFME2MotionChannel::ENCODING_TIMECODED);
	struct { float frame, expected; } cases[] = {
		{ 0.0f, 1.0f }, { 5.0f, 1.5f },  // lerp 1 -> 2
		{ 10.0f, 2.0f }, { 15.0f, 2.0f }, // the next key carries the step flag: hold
		{ 20.0f, 4.0f }, { 100.0f, 4.0f }, // last key
	};
	for (const auto &c : cases)
	{
		float v;
		ch.Get_Vector(c.frame, &v);
		CHECK_MESSAGE(v == Approx(c.expected), "frame " << c.frame);
	}
}

TEST_CASE("BFME2 motion channel encoding 0 before the first key: retail extrapolates unless the next key is a step")
{
	// Binary: FindIndex returns 0 for time <= first code, then the scalar path (RVA 0x001B30AB-0x001B30F8) computes
	// factor = (frame - code0) / (code1 - code0) with no clamp and lerps. Keys (6 -> 3), (10 -> 7): at frame 0 the
	// factor is (0 - 6) / 4 = -1.5 and the value 3 + (-1.5) * (7 - 3) = -3.
	std::string error;
	{
		Bytes b = motionHeader(0, 1, ANIM_CHANNEL_Z, 2, 0);
		b.u16(6).u16(10);
		b.f32(3.0f).f32(7.0f);
		BFME2MotionChannel ch;
		REQUIRE_MESSAGE(loadChannel(ch, W3D_CHUNK_COMPRESSED_ANIMATION_MOTION_CHANNEL, b, error), error);
		float v;
		ch.Get_Vector(0.0f, &v);
		CHECK(v == Approx(-3.0f));
		ch.Get_Vector(4.0f, &v);
		CHECK(v == Approx(1.0f)); // factor -0.5: 3 - 0.5 * 4
		ch.Get_Vector(6.0f, &v);
		CHECK(v == Approx(3.0f));
		ch.Get_Vector(8.0f, &v);
		CHECK(v == Approx(5.0f)); // lerp(3, 7, 0.5)
		ch.Get_Vector(-2.0f, &v);
		CHECK(v == Approx(7.0f)); // a negative frame compares unsigned (cmp/jb at 0x001B3059..): past the last key
	}
	{
		// the next key carries the step flag (bit 15): the first sample is held, nothing extrapolates (0x001B30A0)
		Bytes b = motionHeader(0, 1, ANIM_CHANNEL_Z, 2, 0);
		b.u16(6).u16(0x8000 | 10);
		b.f32(3.0f).f32(7.0f);
		BFME2MotionChannel ch;
		REQUIRE_MESSAGE(loadChannel(ch, W3D_CHUNK_COMPRESSED_ANIMATION_MOTION_CHANNEL, b, error), error);
		float v;
		ch.Get_Vector(0.0f, &v);
		CHECK(v == Approx(3.0f));
	}
	{
		// quaternion (0x001B33CF-0x001B3412): keys 4 -> (0,0,0,1), 8 -> (0,0,0.6,0.8), frame 0: factor = -1.
		// Unnormalised blend: 2 * (0,0,0,1) - 1 * (0,0,0.6,0.8) = (0, 0, -0.6, 1.2), length sqrt(0.36 + 1.44) = sqrt(1.8),
		// result (0, 0, -0.6 / sqrt(1.8), 1.2 / sqrt(1.8)) = (0, 0, -0.447214, 0.894427).
		Bytes b = motionHeader(0, 4, ANIM_CHANNEL_Q, 2, 0);
		b.u16(4).u16(8);
		b.f32(0).f32(0).f32(0).f32(1).f32(0).f32(0).f32(0.6f).f32(0.8f);
		BFME2MotionChannel ch;
		REQUIRE_MESSAGE(loadChannel(ch, W3D_CHUNK_COMPRESSED_ANIMATION_MOTION_CHANNEL, b, error), error);
		Quaternion q = ch.Get_QuatVector(0.0f);
		CHECK(q.Z == Approx(-0.447214f).epsilon(1e-5));
		CHECK(q.W == Approx(0.894427f).epsilon(1e-5));
	}
}

TEST_CASE("BFME2 motion channel encoding 0 quaternion: normalised lerp on the shortest path")
{
	// p = (0,0,0,1), q = (0,0,0.6,-0.8): dot = -0.8 < 0 so the blend is beta*p - alpha*q. At 0.5 that is
	// (0, 0, -0.3, 0.9); its length^2 is 0.09 + 0.81 = 0.9, so the result is (0, 0, -0.3, 0.9) / sqrt(0.9)
	// = (0, 0, -0.31622777, 0.9486833).
	Bytes b = motionHeader(0, 4, ANIM_CHANNEL_Q, 2, 2);
	b.u16(0).u16(4);
	b.f32(0).f32(0).f32(0).f32(1);
	b.f32(0).f32(0).f32(0.6f).f32(-0.8f);
	BFME2MotionChannel ch;
	std::string error;
	REQUIRE_MESSAGE(loadChannel(ch, W3D_CHUNK_COMPRESSED_ANIMATION_MOTION_CHANNEL, b, error), error);
	Quaternion q = ch.Get_QuatVector(2.0f);
	CHECK(q.X == Approx(0.0f).epsilon(1e-5));
	CHECK(q.Y == Approx(0.0f).epsilon(1e-5));
	CHECK(q.Z == Approx(-0.31622777f).epsilon(1e-5));
	CHECK(q.W == Approx(0.9486833f).epsilon(1e-5));
	Quaternion last = ch.Get_QuatVector(4.0f);
	CHECK(last.Z == Approx(0.6f));
	CHECK(last.W == Approx(-0.8f));
}

TEST_CASE("BFME2 motion channel encoding 1: 4-bit deltas, scale and initial value precede the blocks")
{
	// 3 frames, one row. Scale 2.0, initial 1.0, filter 8 (1.0): a nibble is worth 2.0.
	// nibbles 3, -1 -> frames 1.0, 7.0, 5.0.
	Bytes b = motionHeader(1, 1, ANIM_CHANNEL_Y, 3, 1);
	b.f32(2.0f).f32(1.0f);
	block4(b, 8, { 3, -1 });
	BFME2MotionChannel ch;
	std::string error;
	REQUIRE_MESSAGE(loadChannel(ch, W3D_CHUNK_COMPRESSED_ANIMATION_MOTION_CHANNEL, b, error), error);
	float v;
	const float expected[3] = { 1.0f, 7.0f, 5.0f };
	for (int f = 0; f < 3; ++f)
	{
		ch.Get_Key(f, &v);
		CHECK_MESSAGE(v == Approx(expected[f]), "frame " << f);
	}
	ch.Get_Vector(0.5f, &v);
	CHECK(v == Approx(4.0f));
	ch.Get_Vector(1.5f, &v);
	CHECK(v == Approx(6.0f));
	ch.Get_Vector(9.0f, &v);
	CHECK(v == Approx(5.0f));
}

TEST_CASE("BFME2 motion channel encoding 2: 8-bit deltas are stored with the top bit flipped and scaled by 1/16")
{
	// Same frame layout, 16 delta bytes per block. Delta d is stored as (d ^ 0x80); the value added is
	// d / 16 * filter * scale. Scale 2, filter 8 (1.0): d = 16 -> +2.0, d = -32 -> -4.0.
	// stored: 16 ^ 0x80 = 0x90; (-32 & 0xFF) ^ 0x80 = 0xE0 ^ 0x80 = 0x60.  Frames: 1.0, 3.0, -1.0.
	Bytes b = motionHeader(2, 1, ANIM_CHANNEL_X, 3, 1);
	b.f32(2.0f).f32(1.0f);
	b.u8(8).u8(0x90).u8(0x60);
	for (int i = 2; i < 16; ++i)
	{
		b.u8(0x80); // zero delta
	}
	BFME2MotionChannel ch;
	std::string error;
	REQUIRE_MESSAGE(loadChannel(ch, W3D_CHUNK_COMPRESSED_ANIMATION_MOTION_CHANNEL, b, error), error);
	CHECK(ch.Get_Encoding() == BFME2MotionChannel::ENCODING_ADAPTIVE_DELTA_8);
	float v;
	const float expected[3] = { 1.0f, 3.0f, -1.0f };
	for (int f = 0; f < 3; ++f)
	{
		ch.Get_Key(f, &v);
		CHECK_MESSAGE(v == Approx(expected[f]), "frame " << f);
	}
}

TEST_CASE("BFME2 motion channel: a non-zero version byte, bad encoding or wrong body size is refused")
{
	BFME2MotionChannel ch;
	std::string error;
	CHECK_FALSE(loadChannel(ch, W3D_CHUNK_COMPRESSED_ANIMATION_MOTION_CHANNEL, motionHeader(0, 1, ANIM_CHANNEL_X, 1, 0, 1), error));
	CHECK(error.find("version") != std::string::npos);
	CHECK_FALSE(loadChannel(ch, W3D_CHUNK_COMPRESSED_ANIMATION_MOTION_CHANNEL, motionHeader(3, 1, ANIM_CHANNEL_X, 1, 0), error));
	CHECK(error.find("encoding") != std::string::npos);

	Bytes wrong = motionHeader(0, 1, ANIM_CHANNEL_X, 2, 0);
	wrong.u16(0).u16(5).f32(1.0f); // second sample missing
	CHECK_FALSE(loadChannel(ch, W3D_CHUNK_COMPRESSED_ANIMATION_MOTION_CHANNEL, wrong, error));
	CHECK(error.find("layout needs") != std::string::npos);

	Bytes decreasing = motionHeader(0, 1, ANIM_CHANNEL_X, 2, 0);
	decreasing.u16(9).u16(3).f32(1.0f).f32(2.0f);
	CHECK_FALSE(loadChannel(ch, W3D_CHUNK_COMPRESSED_ANIMATION_MOTION_CHANNEL, decreasing, error));
	CHECK(error.find("not after") != std::string::npos);
}

// ---- animation level -----------------------------------------------------------------------------

namespace
{

std::vector<std::uint8_t> compressedAnim(std::uint32_t version, std::uint16_t flavor, std::uint32_t frames, const std::vector<std::pair<std::uint32_t, Bytes>> &channels)
{
	ChunkWriter anim;
	W3dCompressedAnimHeaderStruct h = {};
	h.Version = version;
	setName(h.Name, W3D_NAME_LEN, "WALK");
	setName(h.HierarchyName, W3D_NAME_LEN, "UNIT_SKL");
	h.NumFrames = frames;
	h.FrameRate = 30;
	h.Flavor = flavor;
	anim.chunk(W3D_CHUNK_COMPRESSED_ANIMATION_HEADER, ChunkWriter::of(h));
	for (const auto &c : channels)
	{
		anim.chunk(c.first, c.second.v);
	}
	ChunkWriter file;
	file.wrapper(W3D_CHUNK_COMPRESSED_ANIMATION, anim);
	return file.bytes;
}

int loadCompressed(HCompressedAnimClass &anim, const std::vector<std::uint8_t> &bytes, int numNodes, std::string &error)
{
	ChunkLoadClass cload(bytes.data(), bytes.size());
	REQUIRE(cload.Open_Chunk());
	return anim.Load_W3D(cload, numNodes, &error);
}

Bytes bitChannel(std::uint16_t pivot, std::uint32_t t0, bool b0, std::uint32_t t1, bool b1)
{
	Bytes b;
	b.u32(2).u16(pivot).u8(BIT_CHANNEL_VIS).u8(1);
	b.u32(t0 | (b0 ? W3D_TIMECODED_BIT_MASK : 0)).u32(t1 | (b1 ? W3D_TIMECODED_BIT_MASK : 0));
	return b;
}

} // namespace

TEST_CASE("compressed animation, motion channel form: names, per-pivot channels, fade defaults to 1.0")
{
	// Version 1.0 -> 0x284 channels. Pivot 1: X (enc 0: t 0 -> 2.0, t 10 -> 12.0), fade (enc 0: 1.0 -> 0.0), and
	// a visibility channel (visible on frames 0-4, hidden from 5). Pivot 0 has nothing. Pivot 4 is beyond a
	// 3 pivot hierarchy and is dropped.
	Bytes x = motionHeader(0, 1, ANIM_CHANNEL_X, 2, 1);
	x.u16(0).u16(10).f32(2.0f).f32(12.0f);
	Bytes fade = motionHeader(0, 1, ANIM_CHANNEL_FADE, 2, 1);
	fade.u16(0).u16(10).f32(1.0f).f32(0.0f);
	Bytes stray = motionHeader(0, 1, ANIM_CHANNEL_X, 1, 4);
	stray.u16(0).u16(0).f32(9.0f); // odd count: padding u16 follows the key
	HCompressedAnimClass anim;
	std::string error;
	REQUIRE_MESSAGE(loadCompressed(anim,
		compressedAnim(W3D_MAKE_VERSION(1, 0), 0, 20,
			{ { W3D_CHUNK_COMPRESSED_ANIMATION_MOTION_CHANNEL, x }, { W3D_CHUNK_COMPRESSED_ANIMATION_MOTION_CHANNEL, fade },
				{ W3D_CHUNK_COMPRESSED_BIT_CHANNEL, bitChannel(1, 0, true, 5, false) },
				{ W3D_CHUNK_COMPRESSED_ANIMATION_MOTION_CHANNEL, stray } }),
		3, error) == HCompressedAnimClass::OK, error);
	CHECK(anim.Get_Name() == "UNIT_SKL.WALK");
	CHECK(anim.Get_HName() == "UNIT_SKL");
	CHECK(anim.Get_Num_Frames() == 20);
	CHECK(anim.Get_Frame_Rate() == 30.0f);
	CHECK(anim.Get_Total_Time() == Approx(20.0f / 30.0f));
	CHECK(anim.Get_Num_Pivots() == 3);
	CHECK(anim.Get_Dropped_Channels() == 1);
	CHECK(anim.Uses_Motion_Channels());

	Vector3 t;
	anim.Get_Translation(t, 1, 5.0f);
	CHECK(t.X == Approx(7.0f)); // lerp(2, 12, 0.5)
	CHECK(t.Y == 0.0f);
	CHECK(anim.Get_Fade(1, 5.0f) == Approx(0.5f));
	CHECK(anim.Get_Fade(0, 5.0f) == 1.0f); // no fade channel: 1.0
	CHECK(anim.Get_Visibility(1, 4.0f));
	CHECK_FALSE(anim.Get_Visibility(1, 5.0f));
	CHECK(anim.Get_Visibility(0, 7.0f)); // no visibility channel: visible
	CHECK(anim.Is_Node_Motion_Present(1));
	CHECK_FALSE(anim.Is_Node_Motion_Present(0));
	Quaternion q;
	CHECK_FALSE(anim.Get_Orientation(q, 1, 3.0f)); // BFME2 Get_Orientation returns false (RVA 0x00190C92) when the pivot has no rotation channel
	CHECK(q.W == 1.0f); // identity
}

TEST_CASE("compressed animation, classic form: flavor 0 time coded channels; mixing the two forms is an error")
{
	HCompressedAnimClass anim;
	std::string error;
	Bytes x = timeCoded(0, 1, ANIM_CHANNEL_X, { { 0, { 1.0f } }, { 4, { 5.0f } } });
	REQUIRE_MESSAGE(loadCompressed(anim, compressedAnim(1, 0, 10, { { W3D_CHUNK_COMPRESSED_ANIMATION_CHANNEL, x } }), -1, error) == HCompressedAnimClass::OK, error);
	CHECK(anim.Get_Num_Pivots() == 1); // sized by the highest pivot a channel names
	CHECK_FALSE(anim.Uses_Motion_Channels());
	Vector3 t;
	anim.Get_Translation(t, 0, 2.0f);
	CHECK(t.X == Approx(3.0f));

	// 0x284 in a classic form (version 0x1) animation, and 0x282 in a motion channel form one
	Bytes m = motionHeader(0, 1, ANIM_CHANNEL_X, 1, 0);
	m.u16(0).u16(0).f32(1.0f);
	CHECK(loadCompressed(anim, compressedAnim(1, 0, 10, { { W3D_CHUNK_COMPRESSED_ANIMATION_MOTION_CHANNEL, m } }), -1, error) == HCompressedAnimClass::LOAD_ERROR);
	CHECK(error.find("motion channel chunk in a classic form") != std::string::npos);
	CHECK(loadCompressed(anim, compressedAnim(W3D_MAKE_VERSION(1, 0), 0, 10, { { W3D_CHUNK_COMPRESSED_ANIMATION_CHANNEL, x } }), -1, error) == HCompressedAnimClass::LOAD_ERROR);
	CHECK(error.find("classic channel chunk") != std::string::npos);

	// unknown flavor, zero frames, duplicate channel
	CHECK(loadCompressed(anim, compressedAnim(1, 7, 10, {}), -1, error) == HCompressedAnimClass::LOAD_ERROR);
	CHECK(error.find("flavor") != std::string::npos);
	CHECK(loadCompressed(anim, compressedAnim(1, 0, 0, {}), -1, error) == HCompressedAnimClass::LOAD_ERROR);
	CHECK(loadCompressed(anim, compressedAnim(1, 0, 10, { { W3D_CHUNK_COMPRESSED_ANIMATION_CHANNEL, x }, { W3D_CHUNK_COMPRESSED_ANIMATION_CHANNEL, x } }), -1, error) == HCompressedAnimClass::LOAD_ERROR);
	CHECK(error.find("two") != std::string::npos);
}

TEST_CASE("compressed animation: only header versions 1 and 0x10000 load (BFME2 RVA 0x0018FEE8-0x0018FF01)")
{
	Bytes x = timeCoded(0, 1, ANIM_CHANNEL_X, { { 0, { 1.0f } }, { 4, { 5.0f } } });
	Bytes m = motionHeader(0, 1, ANIM_CHANNEL_X, 1, 0);
	m.u16(0).u16(0).f32(1.0f);
	std::string error;
	HCompressedAnimClass anim;
	CHECK(loadCompressed(anim, compressedAnim(1, 0, 10, { { W3D_CHUNK_COMPRESSED_ANIMATION_CHANNEL, x } }), -1, error) == HCompressedAnimClass::OK);
	CHECK_FALSE(anim.Uses_Motion_Channels());
	CHECK(loadCompressed(anim, compressedAnim(0x10000, 0, 10, { { W3D_CHUNK_COMPRESSED_ANIMATION_MOTION_CHANNEL, m } }), -1, error) == HCompressedAnimClass::OK);
	CHECK(anim.Uses_Motion_Channels());
	for (std::uint32_t version : { 0u, 2u, 0x8000u, 0xFFFFu, 0x10001u, 0x20000u, 0x40001u })
	{
		CHECK_MESSAGE(loadCompressed(anim, compressedAnim(version, 0, 10, { { W3D_CHUNK_COMPRESSED_ANIMATION_CHANNEL, x } }), -1, error) == HCompressedAnimClass::LOAD_ERROR,
			"version " << version);
		CHECK(error.find("retail accepts only 1 and 0x10000") != std::string::npos);
	}
}

TEST_CASE("compressed animation, classic form: a frame before a timecoded channel's first key throws, it is never invented")
{
	// BFME2 hangs there (RVA 0x00190DCA-0x00190E1A); the port reports it loudly.
	Bytes x = timeCoded(0, 1, ANIM_CHANNEL_X, { { 4, { 1.0f } }, { 8, { 5.0f } } });
	HCompressedAnimClass anim;
	std::string error;
	REQUIRE_MESSAGE(loadCompressed(anim, compressedAnim(1, 0, 10, { { W3D_CHUNK_COMPRESSED_ANIMATION_CHANNEL, x } }), -1, error) == HCompressedAnimClass::OK, error);
	Vector3 t;
	CHECK_FALSE(anim.Frame_Is_Defined(0, 2.0f));
	CHECK_THROWS_AS(anim.Get_Translation(t, 0, 2.0f), UndefinedFrameError);
	CHECK(anim.Frame_Is_Defined(0, 4.0f));
	anim.Get_Translation(t, 0, 6.0f);
	CHECK(t.X == Approx(3.0f));
	// the same keys in the motion channel form extrapolate instead (encoding 0, see its own test)
}

TEST_CASE("compressed animation, classic form flavor 1: adaptive delta channels and the fade slot")
{
	Bytes x = adaptiveHeader(18, 0, 1, ANIM_CHANNEL_X, 0.5f, { 10.0f });
	block4(x, 8, { 1, 2 });
	block4(x, 9, {});
	Bytes fade = adaptiveHeader(2, 0, 1, ANIM_CHANNEL_FADE, 1.0f, { 1.0f });
	block4(fade, 8, { -1 });
	HCompressedAnimClass anim;
	std::string error;
	REQUIRE_MESSAGE(loadCompressed(anim, compressedAnim(1, 1, 18, { { W3D_CHUNK_COMPRESSED_ANIMATION_CHANNEL, x }, { W3D_CHUNK_COMPRESSED_ANIMATION_CHANNEL, fade } }), 2, error) == HCompressedAnimClass::OK, error);
	CHECK(anim.Get_Flavor() == ANIM_FLAVOR_ADAPTIVE_DELTA);
	Vector3 t;
	anim.Get_Translation(t, 0, 2.0f);
	CHECK(t.X == Approx(11.5f)); // 10 + 1*0.5 + 2*0.5
	CHECK(anim.Get_Fade(0, 1.0f) == Approx(0.0f)); // 1.0 + (-1 * filter 1.0 * scale 1.0)
	CHECK(anim.Get_Fade(1, 1.0f) == 1.0f);
}

TEST_CASE("raw animation: translation lerps between frames, wraps to frame 0 at the end, orientation nlerps, fade and visibility")
{
	ChunkWriter anim;
	W3dAnimHeaderStruct h = {};
	h.Version = W3D_MAKE_VERSION(4, 1);
	setName(h.Name, W3D_NAME_LEN, "RUN");
	setName(h.HierarchyName, W3D_NAME_LEN, "UNIT_SKL");
	h.NumFrames = 3;
	h.FrameRate = 30;
	anim.chunk(W3D_CHUNK_ANIMATION_HEADER, ChunkWriter::of(h));
	Bytes x; // X over frames 0..2 = 0, 10, 20
	x.u16(0).u16(2).u16(1).u16(ANIM_CHANNEL_X).u16(1).u16(0).f32(0).f32(10).f32(20);
	anim.chunk(W3D_CHUNK_ANIMATION_CHANNEL, x.v);
	Bytes fade; // fade over frames 0..2 = 1, 0.5, 0
	fade.u16(0).u16(2).u16(1).u16(ANIM_CHANNEL_FADE).u16(1).u16(0).f32(1.0f).f32(0.5f).f32(0.0f);
	anim.chunk(W3D_CHUNK_ANIMATION_CHANNEL, fade.v);
	Bytes q; // Q frames 0..1: identity, then (0,0,0.6,-0.8)
	q.u16(0).u16(1).u16(4).u16(ANIM_CHANNEL_Q).u16(1).u16(0).f32(0).f32(0).f32(0).f32(1).f32(0).f32(0).f32(0.6f).f32(-0.8f);
	anim.chunk(W3D_CHUNK_ANIMATION_CHANNEL, q.v);
	Bytes vis; // visible on frame 0 only
	vis.u16(0).u16(2).u16(BIT_CHANNEL_VIS).u16(1).u8(1).u8(0x01);
	anim.chunk(W3D_CHUNK_BIT_CHANNEL, vis.v);
	ChunkWriter file;
	file.wrapper(W3D_CHUNK_ANIMATION, anim);

	HRawAnimClass raw;
	ChunkLoadClass cload(file.bytes.data(), file.bytes.size());
	REQUIRE(cload.Open_Chunk());
	std::string error;
	REQUIRE_MESSAGE(raw.Load_W3D(cload, 2, &error) == HRawAnimClass::OK, error);
	CHECK(raw.Get_Name() == "UNIT_SKL.RUN");
	CHECK(raw.Get_Num_Frames() == 3);
	CHECK(raw.Get_Dropped_Channels() == 0);

	Vector3 t;
	raw.Get_Translation(t, 1, 0.5f);
	CHECK(t.X == Approx(5.0f));
	raw.Get_Translation(t, 1, 1.0f);
	CHECK(t.X == Approx(10.0f));
	raw.Get_Translation(t, 1, 2.5f); // frame1 = 3 >= NumFrames wraps to frame 0: lerp(20, 0, 0.5)
	CHECK(t.X == Approx(10.0f));
	raw.Get_Translation(t, 0, 1.0f); // pivot 0 has no channels
	CHECK(t.X == 0.0f);

	CHECK(raw.Get_Fade(1, 0.5f) == Approx(0.75f));
	CHECK(raw.Get_Fade(0, 0.5f) == 1.0f);

	Quaternion o;
	raw.Get_Orientation(o, 1, 0.5f);
	// nlerp of (0,0,0,1) and (0,0,0.6,-0.8): dot < 0 -> (0, 0, -0.3, 0.9) / sqrt(0.9)
	CHECK(o.Z == Approx(-0.31622777f).epsilon(1e-5));
	CHECK(o.W == Approx(0.9486833f).epsilon(1e-5));

	CHECK(raw.Get_Visibility(1, 0.0f));
	CHECK_FALSE(raw.Get_Visibility(1, 1.0f));
	CHECK(raw.Get_Visibility(0, 1.0f));
}

TEST_CASE("raw animation: a channel for a pivot beyond the hierarchy is dropped and counted; pre-3.0 pivots shift by one")
{
	ChunkWriter anim;
	W3dAnimHeaderStruct h = {};
	h.Version = W3D_MAKE_VERSION(2, 0);
	setName(h.Name, W3D_NAME_LEN, "OLD");
	setName(h.HierarchyName, W3D_NAME_LEN, "OLD_SKL");
	h.NumFrames = 1;
	h.FrameRate = 15;
	anim.chunk(W3D_CHUNK_ANIMATION_HEADER, ChunkWriter::of(h));
	Bytes x;
	x.u16(0).u16(0).u16(1).u16(ANIM_CHANNEL_X).u16(0).u16(0).f32(4.0f); // pivot 0 -> becomes 1
	anim.chunk(W3D_CHUNK_ANIMATION_CHANNEL, x.v);
	Bytes y;
	y.u16(0).u16(0).u16(1).u16(ANIM_CHANNEL_Y).u16(5).u16(0).f32(9.0f); // pivot 5 -> 6, beyond 3 nodes
	anim.chunk(W3D_CHUNK_ANIMATION_CHANNEL, y.v);
	ChunkWriter file;
	file.wrapper(W3D_CHUNK_ANIMATION, anim);

	HRawAnimClass raw;
	ChunkLoadClass cload(file.bytes.data(), file.bytes.size());
	REQUIRE(cload.Open_Chunk());
	std::string error;
	REQUIRE_MESSAGE(raw.Load_W3D(cload, 3, &error) == HRawAnimClass::OK, error);
	CHECK(raw.Get_Dropped_Channels() == 1);
	Vector3 t;
	raw.Get_Translation(t, 1, 0.0f);
	CHECK(t.X == Approx(4.0f));
	raw.Get_Translation(t, 0, 0.0f);
	CHECK(t.X == 0.0f);
}

TEST_CASE("a W3D file's animations load with the file and are found by HIERARCHY.ANIM name")
{
	ChunkWriter file;
	// raw animation UNIT_SKL.WALK with one X channel on pivot 2
	{
		ChunkWriter anim;
		W3dAnimHeaderStruct h = {};
		h.Version = W3D_MAKE_VERSION(4, 1);
		setName(h.Name, W3D_NAME_LEN, "WALK");
		setName(h.HierarchyName, W3D_NAME_LEN, "UNIT_SKL");
		h.NumFrames = 2;
		h.FrameRate = 30;
		anim.chunk(W3D_CHUNK_ANIMATION_HEADER, ChunkWriter::of(h));
		Bytes x;
		x.u16(0).u16(1).u16(1).u16(ANIM_CHANNEL_X).u16(2).u16(0).f32(1.0f).f32(3.0f);
		anim.chunk(W3D_CHUNK_ANIMATION_CHANNEL, x.v);
		file.wrapper(W3D_CHUNK_ANIMATION, anim);
	}
	// compressed animation UNIT_SKL.WALK2... (named RUN) in the motion channel form, Z channel on pivot 1
	{
		Bytes ch = motionHeader(0, 1, ANIM_CHANNEL_Z, 2, 1);
		ch.u16(0).u16(4).f32(0.0f).f32(8.0f);
		std::vector<std::uint8_t> bytes = compressedAnim(W3D_MAKE_VERSION(1, 0), 0, 5, { { W3D_CHUNK_COMPRESSED_ANIMATION_MOTION_CHANNEL, ch } });
		file.bytes.insert(file.bytes.end(), bytes.begin(), bytes.end());
	}
	W3DFileContents contents;
	std::string error;
	REQUIRE_MESSAGE(Load_W3D_File(file.bytes.data(), file.bytes.size(), contents, &error), error);
	CHECK(contents.RawAnims.size() == 1);
	CHECK(contents.CompressedAnims.size() == 1);
	CHECK(contents.OtherChunks.empty());
	const HAnimClass *walk = contents.Find_Animation("unit_skl.walk");
	REQUIRE(walk);
	CHECK(walk->Get_Num_Pivots() == 3); // highest pivot named by a channel + 1
	Vector3 t;
	walk->Get_Translation(t, 2, 0.5f);
	CHECK(t.X == Approx(2.0f));
	// compressedAnim() names its animation UNIT_SKL.WALK as well; both kinds are searched
	CHECK(contents.Find_Animation("UNIT_SKL.WALK") != nullptr);
	CHECK(contents.Find_Animation("unit_skl.missing") == nullptr);
	CHECK(contents.CompressedAnims[0].Get_Num_Frames() == 5);
	contents.CompressedAnims[0].Get_Translation(t, 1, 2.0f);
	CHECK(t.Z == Approx(4.0f)); // lerp(0, 8, 2 / 4)

	// a malformed animation fails the file, with the animation named
	ChunkWriter bad;
	bad.chunk(W3D_CHUNK_ANIMATION, { 1, 2, 3 });
	W3DFileContents none;
	CHECK_FALSE(Load_W3D_File(bad.bytes.data(), bad.bytes.size(), none, &error));
	CHECK(error.find("W3D_CHUNK_ANIMATION") != std::string::npos);
}

TEST_CASE("HLod: LOD arrays beyond LodCount are kept visible, not dropped; unknown HLod chunks are refused")
{
	auto hlodFile = [](bool extraArray, bool junkChunk) {
		ChunkWriter hlod;
		W3dHLodHeaderStruct lh = {};
		lh.Version = W3D_MAKE_VERSION(1, 0);
		lh.LodCount = 1;
		setName(lh.Name, sizeof(lh.Name), "X");
		setName(lh.HierarchyName, sizeof(lh.HierarchyName), "X_SKL");
		hlod.chunk(W3D_CHUNK_HLOD_HEADER, ChunkWriter::of(lh));
		for (int array = 0; array < (extraArray ? 2 : 1); ++array)
		{
			ChunkWriter lod;
			W3dHLodArrayHeaderStruct ah = { 1, array == 0 ? 0.0f : 99.0f };
			lod.chunk(W3D_CHUNK_HLOD_SUB_OBJECT_ARRAY_HEADER, ChunkWriter::of(ah));
			W3dHLodSubObjectStruct so = {};
			so.BoneIndex = (std::uint32_t)array;
			setName(so.Name, sizeof(so.Name), array == 0 ? "X.A" : "X.B");
			lod.chunk(W3D_CHUNK_HLOD_SUB_OBJECT, ChunkWriter::of(so));
			hlod.wrapper(W3D_CHUNK_HLOD_LOD_ARRAY, lod);
		}
		if (junkChunk)
		{
			hlod.chunk(0x7777, { 1, 2, 3, 4 });
		}
		ChunkWriter file;
		file.wrapper(W3D_CHUNK_HLOD, hlod);
		return file.bytes;
	};
	std::string error;
	{
		std::vector<std::uint8_t> bytes = hlodFile(true, false);
		W3DFileContents contents;
		REQUIRE_MESSAGE(Load_W3D_File(bytes.data(), bytes.size(), contents, &error), error);
		REQUIRE(contents.HLods.size() == 1);
		CHECK(contents.HLods[0].Lod.size() == 1);
		REQUIRE(contents.HLods[0].ExtraLod.size() == 1);
		CHECK(contents.HLods[0].ExtraLod[0].ModelName[0] == "X.B");
		CHECK(contents.HLods[0].ExtraLod[0].MaxScreenSize == 99.0f);
	}
	{
		std::vector<std::uint8_t> bytes = hlodFile(false, true);
		W3DFileContents contents;
		CHECK_FALSE(Load_W3D_File(bytes.data(), bytes.size(), contents, &error));
		CHECK(error.find("unexpected chunk 30583") != std::string::npos);
	}
}
