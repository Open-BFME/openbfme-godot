// OpenBFME unit tests: retail audio file decoders (lane AUDIO-1). GPL-3.0.
//
// Synthetic part: WAV containers, PCM / float conversions, IMA ADPCM (hand-computed vectors from the standard
// step / index tables plus an in-test reference encoder), MS ADPCM, defect warnings, malformed input.
// MP3 part: small lame-encoded synthetic fixtures (tests/data/audio, generated at build time by tools/audio/gen_mp3_fixtures.py into <build>/audio_fixtures, never committed)
// compared with golden numbers taken from mpg123's float decode rounded to 16 bit; no external tool is needed at
// test time. Retail part: runs only with ROTWK_INSTALL / BFME2_INSTALL (SKIP line otherwise): probes and decodes
// every .wav and .mp3 of the mounted archives and asserts the exact defect list against tests/data/audio/
// retail_defects.tsv. The independent oracle (ffmpeg / mpg123) lives in tools/audio/oracle_check.py.

#include "doctest.h"

#include "Common/Audio/AudioDecode.h"
#include "Common/Audio/Mp3Decoder.h"
#include "Common/Audio/Mp3Tables.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"
#include "RetailTestMount.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using namespace AudioDecode;

namespace
{

// ---- WAV building blocks -----------------------------------------------------------------------------------------

typedef std::vector<uint8_t> Bytes;

void put16(Bytes &b, unsigned v)
{
	b.push_back(uint8_t(v & 255));
	b.push_back(uint8_t((v >> 8) & 255));
}
void put32(Bytes &b, uint32_t v)
{
	put16(b, v & 0xFFFF);
	put16(b, v >> 16);
}

struct Chunk
{
	std::string id;
	Bytes body;
	int declaredSize = -1; // override the size field
};

Bytes riff(const std::vector<Chunk> &chunks, bool exactRiffSize = true)
{
	Bytes out;
	out.insert(out.end(), {'R', 'I', 'F', 'F'});
	put32(out, 0);
	out.insert(out.end(), {'W', 'A', 'V', 'E'});
	for (const Chunk &c : chunks)
	{
		out.insert(out.end(), c.id.begin(), c.id.end());
		put32(out, c.declaredSize >= 0 ? uint32_t(c.declaredSize) : uint32_t(c.body.size()));
		out.insert(out.end(), c.body.begin(), c.body.end());
		if (c.body.size() & 1) out.push_back(0);
	}
	uint32_t riffSize = exactRiffSize ? uint32_t(out.size() - 8) : 0x12345678u;
	out[4] = uint8_t(riffSize);
	out[5] = uint8_t(riffSize >> 8);
	out[6] = uint8_t(riffSize >> 16);
	out[7] = uint8_t(riffSize >> 24);
	return out;
}

Bytes fmtBody(unsigned tag, unsigned channels, unsigned rate, unsigned blockAlign, unsigned bits, const Bytes &extra = Bytes())
{
	Bytes b;
	put16(b, tag);
	put16(b, channels);
	put32(b, rate);
	put32(b, rate * blockAlign);
	put16(b, blockAlign);
	put16(b, bits);
	b.insert(b.end(), extra.begin(), extra.end());
	return b;
}

Bytes imaFmt(unsigned channels, unsigned rate, unsigned blockAlign)
{
	Bytes extra;
	put16(extra, 2);
	put16(extra, 1 + (blockAlign - 4 * channels) * 2 / channels);
	return fmtBody(0x11, channels, rate, blockAlign, 4, extra);
}

Bytes factBody(uint32_t frames)
{
	Bytes b;
	put32(b, frames);
	return b;
}

Bytes samples16(const std::vector<int> &v)
{
	Bytes b;
	for (int s : v) put16(b, unsigned(s) & 0xFFFF);
	return b;
}

// ---- reference IMA encoder (independent of the decoder: spells the standard algorithm out) -----------------------

const int kStepTable[89] = {7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97,
	107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166,
	1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493,
	10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};

struct ImaEnc
{
	int pred = 0, index = 0;
	bool multiply = false;

	// encodes one sample, returns the nibble and updates the reconstruction the way a decoder will
	int encode(int sample)
	{
		int step = kStepTable[index];
		int diff = sample - pred;
		int nibble = 0;
		if (diff < 0)
		{
			nibble = 8;
			diff = -diff;
		}
		int t = step;
		if (diff >= t) { nibble |= 4; diff -= t; }
		t >>= 1;
		if (diff >= t) { nibble |= 2; diff -= t; }
		t >>= 1;
		if (diff >= t) { nibble |= 1; }
		int mag = nibble & 7;
		int vp;
		if (multiply)
		{
			vp = ((2 * mag + 1) * step) >> 3;
		}
		else
		{
			vp = step >> 3;
			if (mag & 4) vp += step;
			if (mag & 2) vp += step >> 1;
			if (mag & 1) vp += step >> 2;
		}
		pred += (nibble & 8) ? -vp : vp;
		pred = std::max(-32768, std::min(32767, pred));
		static const int adj[8] = {-1, -1, -1, -1, 2, 4, 6, 8};
		index = std::max(0, std::min(88, index + adj[mag]));
		return nibble;
	}
};

// Encodes per-channel sample vectors into IMA blocks of blockAlign bytes, `blocks` whole blocks plus, when
// `extraGroups` > 0, a final block holding only that many 8-sample groups per channel (a partial block).
// Returns the reconstruction the decoder must reproduce, interleaved. The input must hold exactly
// blocks * spb + (extraGroups ? 1 + 8 * extraGroups : 0) samples per channel.
Bytes encodeIma(const std::vector<std::vector<int>> &ch, unsigned blockAlign, unsigned blocks, unsigned extraGroups, bool multiply,
	std::vector<int> &reconInterleaved)
{
	unsigned nch = unsigned(ch.size());
	unsigned spb = 1 + (blockAlign - 4 * nch) * 2 / nch;
	Bytes data;
	std::vector<std::vector<int>> recon(nch);
	size_t pos = 0;
	auto block = [&](unsigned groups) {
		std::vector<ImaEnc> enc(nch);
		for (unsigned c = 0; c < nch; ++c)
		{
			enc[c].multiply = multiply;
			enc[c].pred = ch[c][pos];
			enc[c].index = int((pos * 7 + c * 13) % 40); // an arbitrary starting step index, as real encoders carry it over
			put16(data, unsigned(enc[c].pred) & 0xFFFF);
			data.push_back(uint8_t(enc[c].index));
			data.push_back(0);
			recon[c].push_back(enc[c].pred);
		}
		for (unsigned g = 0; g < groups; ++g)
		{
			for (unsigned c = 0; c < nch; ++c)
			{
				for (int b = 0; b < 4; ++b)
				{
					int lo = enc[c].encode(ch[c][pos + 1 + g * 8 + 2 * b]);
					recon[c].push_back(enc[c].pred);
					int hi = enc[c].encode(ch[c][pos + 1 + g * 8 + 2 * b + 1]);
					recon[c].push_back(enc[c].pred);
					data.push_back(uint8_t(lo | (hi << 4)));
				}
			}
		}
		pos += 1 + groups * 8;
	};
	for (unsigned b = 0; b < blocks; ++b) block((spb - 1) / 8);
	if (extraGroups) block(extraGroups);
	reconInterleaved.clear();
	for (size_t i = 0; i < recon[0].size(); ++i)
		for (unsigned c = 0; c < nch; ++c) reconInterleaved.push_back(recon[c][i]);
	return data;
}

std::vector<int> testSignal(size_t n, unsigned seed, double amp)
{
	std::vector<int> v(n);
	uint32_t s = seed;
	for (size_t i = 0; i < n; ++i)
	{
		s = s * 1664525u + 1013904223u;
		double noise = ((s >> 8) & 0xFFFF) / 65536.0 - 0.5;
		v[i] = int(amp * (std::sin(double(i) * 0.05 + seed) + 0.3 * std::sin(double(i) * 0.31) + 0.2 * noise));
	}
	return v;
}

DecodedAudio dec(const Bytes &b, std::vector<std::string> *w = nullptr, const DecodeOptions *o = nullptr)
{
	return decode(b.data(), b.size(), w, o);
}

template <typename Fn>
std::string throwsWith(Fn fn)
{
	try
	{
		fn();
	}
	catch (const AudioDecodeError &e)
	{
		return e.what();
	}
	return "";
}

bool contains(const std::string &s, const char *needle)
{
	return s.find(needle) != std::string::npos;
}

} // namespace

// ---- PCM ---------------------------------------------------------------------------------------------------------

TEST_CASE("audio decode: PCM 8 / 16 / 24 / 32 bit and float WAV convert to s16")
{
	// 16-bit stereo round trip, 3 frames
	{
		Bytes w = riff({{"fmt ", fmtBody(1, 2, 22050, 4, 16)}, {"data", samples16({0, -1, 32767, -32768, 1234, -4321})}});
		std::vector<std::string> warn;
		DecodedAudio a = dec(w, &warn);
		CHECK(warn.empty());
		CHECK(a.info.format == Format::Pcm16);
		CHECK(a.info.channels == 2);
		CHECK(a.info.sampleRate == 22050);
		CHECK(a.info.frames == 3);
		CHECK(a.info.bitsPerSample == 16);
		CHECK(a.info.blockAlign == 4);
		CHECK(a.pcm == std::vector<int16_t>({0, -1, 32767, -32768, 1234, -4321}));
		CHECK(a.info.durationMs() == doctest::Approx(3 * 1000.0 / 22050));
		AudioInfo p = probe(w.data(), w.size());
		CHECK(p.frames == 3);
		CHECK(p.format == Format::Pcm16);
	}
	// 8-bit unsigned
	{
		Bytes w = riff({{"fmt ", fmtBody(1, 1, 8000, 1, 8)}, {"data", Bytes({0, 128, 255, 129})}});
		DecodedAudio a = dec(w);
		CHECK(a.info.format == Format::Pcm8);
		CHECK(a.pcm == std::vector<int16_t>({-32768, 0, 32512, 256}));
	}
	// 24-bit: the top 16 bits (truncation toward minus infinity)
	{
		Bytes d = {0x56, 0x34, 0x12, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x80, 0xFF, 0xFF, 0x7F};
		Bytes w = riff({{"fmt ", fmtBody(1, 1, 44100, 3, 24)}, {"data", d}});
		DecodedAudio a = dec(w);
		CHECK(a.info.format == Format::Pcm24);
		CHECK(a.pcm == std::vector<int16_t>({0x1234, -1, -32768, 0x7FFF}));
	}
	// 32-bit
	{
		Bytes d = {0x78, 0x56, 0x34, 0x12, 0x00, 0x00, 0x00, 0x80};
		Bytes w = riff({{"fmt ", fmtBody(1, 1, 44100, 4, 32)}, {"data", d}});
		DecodedAudio a = dec(w);
		CHECK(a.info.format == Format::Pcm32);
		CHECK(a.pcm == std::vector<int16_t>({0x1234, -32768}));
	}
	// IEEE float: 0.5 -> 16384, -1.0 -> -32768, +1.0 clips to 32767, ties round to even
	{
		float f[5] = {0.5f, -1.0f, 1.0f, 0.5f / 32768.0f, 1.5f / 32768.0f};
		Bytes d(sizeof(f));
		std::memcpy(d.data(), f, sizeof(f));
		Bytes w = riff({{"fmt ", fmtBody(3, 1, 44100, 4, 32)}, {"data", d}});
		DecodedAudio a = dec(w);
		CHECK(a.info.format == Format::Float32);
		CHECK(a.pcm == std::vector<int16_t>({16384, -32768, 32767, 0, 2}));
		float nan = std::nanf("");
		Bytes dn(4);
		std::memcpy(dn.data(), &nan, 4);
		Bytes wn = riff({{"fmt ", fmtBody(3, 1, 44100, 4, 32)}, {"data", dn}});
		CHECK(contains(throwsWith([&] { dec(wn); }), "NaN"));
	}
	// WAVE_FORMAT_EXTENSIBLE carrying PCM16
	{
		Bytes extra;
		put16(extra, 22);
		put16(extra, 16);
		put32(extra, 3);
		put16(extra, 1); // sub-format tag
		const uint8_t guidTail[14] = {0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71};
		extra.insert(extra.end(), guidTail, guidTail + 14);
		Bytes w = riff({{"fmt ", fmtBody(0xFFFE, 1, 44100, 2, 16, extra)}, {"data", samples16({5, -5})}});
		DecodedAudio a = dec(w);
		CHECK(a.info.format == Format::Pcm16);
		CHECK(a.pcm == std::vector<int16_t>({5, -5}));
	}
	// trailing chunks after the data (LIST, cue, PAD) are not defects; chunks before it are skipped
	{
		Bytes junk(7, 0x55);
		Bytes w = riff({{"LIST", junk}, {"fmt ", fmtBody(1, 1, 44100, 2, 16)}, {"data", samples16({1, 2, 3})}, {"cue ", junk}, {"PAD ", Bytes(10)}});
		std::vector<std::string> warn;
		DecodedAudio a = dec(w, &warn);
		CHECK(a.pcm == std::vector<int16_t>({1, 2, 3}));
		CHECK(warn.empty());
	}
}

// ---- IMA ADPCM ---------------------------------------------------------------------------------------------------

TEST_CASE("audio decode: IMA ADPCM hand-computed vectors from the standard step and index tables")
{
	// one mono block: header predictor 0, step index 0 (step 7); nibbles 7 9 0 F 4 3 2 1 packed low nibble first.
	//   nibble 7 (+, magnitude 7): reference diff = 7>>3 + 7 + 7>>1 + 7>>2 = 0 + 7 + 3 + 1 = 11 -> 11; index 0 + 8 = 8 (step 16)
	//   nibble 9 (-, magnitude 1): 16>>3 + 16>>2 = 2 + 4 = 6 -> 5; index 8 - 1 = 7 (step 14)
	//   nibble 0 (+, magnitude 0): 14>>3 = 1 -> 6; index 6 (step 13)
	//   nibble F (-, magnitude 7): 13>>3 + 13 + 13>>1 + 13>>2 = 1 + 13 + 6 + 3 = 23 -> -17; index 6 + 8 = 14 (step 28)
	//   nibble 4 (+, magnitude 4): 28>>3 + 28 = 3 + 28 = 31 -> 14; index 14 + 2 = 16 (step 34)
	// ffmpeg's multiplication form ((2m+1)*step)>>3: 13, 7, 8, -16, 15 with the same index walk.
	Bytes block;
	put16(block, 0);
	block.push_back(0);
	block.push_back(0);
	Bytes nibbleBytes = {0x97, 0xF0, 0x34, 0x12, 0, 0, 0, 0}; // 7 9 | 0 F | 4 3 | 2 1 ...
	block.insert(block.end(), nibbleBytes.begin(), nibbleBytes.end());
	Bytes w = riff({{"fmt ", imaFmt(1, 22050, 12)}, {"data", block}});
	DecodedAudio ref = dec(w);
	CHECK(ref.info.format == Format::ImaAdpcm);
	CHECK(ref.info.frames == 1 + 16);
	CHECK(ref.info.bitsPerSample == 4);
	CHECK(ref.info.blockAlign == 12);
	CHECK(std::vector<int16_t>(ref.pcm.begin(), ref.pcm.begin() + 6) == std::vector<int16_t>({0, 11, 5, 6, -17, 14}));
	DecodeOptions mul;
	mul.ima = ImaRounding::FfmpegMultiply;
	DecodedAudio ff = dec(w, nullptr, &mul);
	CHECK(std::vector<int16_t>(ff.pcm.begin(), ff.pcm.begin() + 6) == std::vector<int16_t>({0, 13, 7, 8, -16, 15}));
	// the block decoder directly
	int16_t out[17];
	CHECK(decodeImaAdpcmBlock(block.data(), block.size(), 1, out, 0) == 17);
	CHECK(out[1] == 11);
}

TEST_CASE("audio decode: IMA ADPCM clamps the predictor and the step index")
{
	// predictor 32000 with step index 88 (step 32767) and nibble 7: clamps to 32767; then nibble F clamps down to -32768 region
	Bytes block;
	put16(block, 32000);
	block.push_back(88);
	block.push_back(0);
	block.insert(block.end(), {0x77, 0x77, 0xFF, 0xFF});
	Bytes w = riff({{"fmt ", imaFmt(1, 22050, 8)}, {"data", block}});
	DecodedAudio a = dec(w);
	REQUIRE(a.pcm.size() == 9);
	// nibble 7 at step 32767 adds 32767>>3 + 32767 + 32767>>1 + 32767>>2 = 61436: clamps to 32767 (index stays 88);
	// nibble F subtracts the same: 32767 - 61436 = -28669, then clamps at -32768
	CHECK(a.pcm == std::vector<int16_t>({32000, 32767, 32767, 32767, 32767, -28669, -32768, -32768, -32768}));
	// step index 0 with nibble 0..3 stays at 0
	Bytes b2;
	put16(b2, 0);
	b2.push_back(0);
	b2.push_back(0);
	b2.insert(b2.end(), {0x00, 0x00, 0x00, 0x00});
	DecodedAudio z = dec(riff({{"fmt ", imaFmt(1, 22050, 8)}, {"data", b2}}));
	CHECK(z.pcm == std::vector<int16_t>({0, 0, 0, 0, 0, 0, 0, 0, 0})); // diff = 7 >> 3 = 0 every time
}

TEST_CASE("audio decode: IMA ADPCM reference encoder round trip (mono and stereo, odd counts, block boundaries)")
{
	for (int multiply = 0; multiply < 2; ++multiply)
	{
		DecodeOptions opt;
		opt.ima = multiply ? ImaRounding::FfmpegMultiply : ImaRounding::Reference;
		// mono, blockAlign 256 -> 505 samples per block; 2 whole blocks + a partial block of 3 groups (25 samples)
		for (int channels = 1; channels <= 2; ++channels)
		{
			unsigned blockAlign = channels == 1 ? 256 : 512;
			unsigned spb = 1 + (blockAlign - 4 * channels) * 2 / channels;
			unsigned blocks = 2, extraGroups = 3;
			size_t perChannel = size_t(blocks) * spb + 1 + 8 * extraGroups;
			std::vector<std::vector<int>> sig;
			for (int c = 0; c < channels; ++c) sig.push_back(testSignal(perChannel, 100 + c + multiply, 9000.0 * (c + 1)));
			std::vector<int> recon;
			Bytes data = encodeIma(sig, blockAlign, blocks, extraGroups, multiply != 0, recon);
			// (a) the final partial block as it is: decodes as far as it goes and reports it
			{
				Bytes w = riff({{"fmt ", imaFmt(channels, 44100, blockAlign)}, {"data", data}});
				std::vector<std::string> warn;
				DecodedAudio a = dec(w, &warn, &opt);
				CHECK(a.info.frames == perChannel);
				REQUIRE(a.pcm.size() == recon.size());
				CHECK(a.pcm.size() == perChannel * channels);
				bool same = true;
				for (size_t i = 0; i < recon.size(); ++i) same = same && a.pcm[i] == recon[i];
				CHECK(same);
				REQUIRE(warn.size() == 1);
				CHECK(contains(warn[0], "partial-block"));
				CHECK(probe(w.data(), w.size()).frames == perChannel);
			}
			// (b) the same data with a fact chunk that cuts an odd number of samples off the end of the partial block
			{
				uint32_t fact = uint32_t(perChannel - 5);
				Bytes w = riff({{"fmt ", imaFmt(channels, 44100, blockAlign)}, {"fact", factBody(fact)}, {"data", data}});
				std::vector<std::string> warn;
				DecodedAudio a = dec(w, &warn, &opt);
				CHECK(a.info.frames == fact);
				REQUIRE(a.pcm.size() == size_t(fact) * channels);
				bool same = true;
				for (size_t i = 0; i < a.pcm.size(); ++i) same = same && a.pcm[i] == recon[i];
				CHECK(same);
			}
		}
	}
}

TEST_CASE("audio decode: IMA ADPCM stereo block layout and fact trimming of a padded final block")
{
	// 2 whole stereo blocks (blockAlign 2048 as in the retail stereo files), fact trims 700 samples (less than a block)
	unsigned blockAlign = 2048, channels = 2;
	unsigned spb = 1 + (blockAlign - 8) * 2 / 2; // 2041
	std::vector<std::vector<int>> sig = {testSignal(2 * size_t(spb), 5, 12000), testSignal(2 * size_t(spb), 6, 8000)};
	std::vector<int> recon;
	Bytes data = encodeIma(sig, blockAlign, 2, 0, false, recon);
	CHECK(data.size() == 2u * blockAlign);
	uint32_t fact = 2 * spb - 700;
	Bytes w = riff({{"fmt ", imaFmt(channels, 44100, blockAlign)}, {"fact", factBody(fact)}, {"data", data}, {"LIST", Bytes(20)}});
	std::vector<std::string> warn;
	DecodedAudio a = dec(w, &warn);
	CHECK(warn.empty()); // a fact shorter than the data by less than a block is the normal retail padding
	CHECK(a.info.frames == fact);
	REQUIRE(a.pcm.size() == size_t(fact) * 2);
	bool same = true;
	for (size_t i = 0; i < a.pcm.size(); ++i) same = same && a.pcm[i] == recon[i];
	CHECK(same);
	CHECK(a.info.format == Format::ImaAdpcm);
	CHECK(a.info.channels == 2);
}

TEST_CASE("audio decode: IMA ADPCM block header with an invalid step index throws with the offset")
{
	Bytes block;
	put16(block, 0);
	block.push_back(89);
	block.push_back(0);
	block.insert(block.end(), {0, 0, 0, 0});
	Bytes w = riff({{"fmt ", imaFmt(1, 22050, 8)}, {"data", block}});
	std::string msg = throwsWith([&] { dec(w); });
	CHECK(contains(msg, "step index 89"));
	CHECK(contains(msg, "byte offset"));
}

// ---- MS ADPCM ----------------------------------------------------------------------------------------------------

TEST_CASE("audio decode: MS ADPCM block with the standard coefficient set")
{
	// mono, blockAlign 9: header predictor 0 (coef 256, 0), delta 16, sample1 100, sample2 50, then nibbles 3 D 8 7
	//   out: 50, 100 (sample2 first), then
	//   nibble 3: pred = (100*256 + 50*0)/256 = 100; + 3*16 = 148; delta = 230*16 >> 8 = 14 -> 16
	//   nibble D (-3): pred = 148 - 48 = 100; delta stays 16
	//   nibble 8 (-8): pred = 100 - 128 = -28; delta = 768*16 >> 8 = 48
	//   nibble 7: pred = -28 + 7*48 = 308
	Bytes extra;
	put16(extra, 32);
	put16(extra, 2 + (9 - 7) * 2); // samples per block = 6
	put16(extra, 7);
	const int coefs[7][2] = {{256, 0}, {512, -256}, {0, 0}, {192, 64}, {240, 0}, {460, -208}, {392, -232}};
	for (auto &c : coefs)
	{
		put16(extra, unsigned(c[0]) & 0xFFFF);
		put16(extra, unsigned(c[1]) & 0xFFFF);
	}
	Bytes block;
	block.push_back(0);        // predictor index
	put16(block, 16);          // delta
	put16(block, 100);         // sample1
	put16(block, 50);          // sample2
	block.insert(block.end(), {0x3D, 0x87});
	Bytes w = riff({{"fmt ", fmtBody(2, 1, 22050, 9, 4, extra)}, {"data", block}});
	DecodedAudio a = dec(w);
	CHECK(a.info.format == Format::MsAdpcm);
	CHECK(a.info.frames == 6);
	CHECK(a.pcm == std::vector<int16_t>({50, 100, 148, 100, -28, 308}));
	// stereo: nibble pairs are (left, right) in one byte, high nibble first; header holds both channels' fields
	Bytes block2;
	block2.push_back(0);
	block2.push_back(0);                      // predictor indices
	put16(block2, 16);
	put16(block2, 16);                        // deltas
	put16(block2, 100);
	put16(block2, -100 & 0xFFFF);             // sample1
	put16(block2, 50);
	put16(block2, -50 & 0xFFFF);              // sample2
	block2.push_back(0x3D);                   // left 3 right D
	Bytes extra2 = extra;
	Bytes w2 = riff({{"fmt ", fmtBody(2, 2, 22050, 15, 4, extra2)}, {"data", block2}});
	DecodedAudio b = dec(w2);
	CHECK(b.info.frames == 3);
	// left: 50, 100, 148; right: -50, -100, -100 + (-3 * 16) = -148
	CHECK(b.pcm == std::vector<int16_t>({50, -50, 100, -100, 148, -148}));
}

TEST_CASE("audio decode: MS ADPCM step size that outgrows 32 bits is an error with an offset, not signed overflow")
{
	// mono block: delta 32767, 57 bytes of 0x77 (each nibble 7 multiplies the step by 614/256; UBSan used to report signed overflow at 614 * 6237369)
	Bytes extra;
	put16(extra, 32);
	put16(extra, 2 + (64 - 7) * 2);
	put16(extra, 7);
	const int coefs[7][2] = {{256, 0}, {512, -256}, {0, 0}, {192, 64}, {240, 0}, {460, -208}, {392, -232}};
	for (auto &c : coefs)
	{
		put16(extra, unsigned(c[0]) & 0xFFFF);
		put16(extra, unsigned(c[1]) & 0xFFFF);
	}
	Bytes block;
	block.push_back(0);
	put16(block, 32767);
	put16(block, 0);
	put16(block, 0);
	block.insert(block.end(), 57, 0x77);
	Bytes w = riff({{"fmt ", fmtBody(2, 1, 22050, 64, 4, extra)}, {"data", block}});
	bool threw = false;
	try
	{
		(void)dec(w);
	}
	catch (const AudioDecodeError &e)
	{
		threw = true;
		CHECK(contains(e.what(), "MS ADPCM step size"));
	}
	CHECK(threw);
}

// ---- defects and malformed input ---------------------------------------------------------------------------------

TEST_CASE("audio decode: data and container defects are reported, not hidden")
{
	Bytes fmt = fmtBody(1, 1, 44100, 2, 16);
	{
		// data chunk size larger than the file: the present bytes decode, the overrun is reported
		Chunk data{"data", samples16({1, 2, 3, 4}), 1000};
		Bytes w = riff({{"fmt ", fmt}, data});
		std::vector<std::string> warn;
		DecodedAudio a = dec(w, &warn);
		CHECK(a.pcm == std::vector<int16_t>({1, 2, 3, 4}));
		REQUIRE(warn.size() >= 1);
		CHECK(contains(warn[0], "data-overrun"));
		CHECK(contains(warn[0], "1000"));
	}
	{
		// odd trailing byte inside the data chunk
		Bytes d = samples16({7, 8});
		d.push_back(9);
		Chunk data{"data", d, 5};
		Bytes w = riff({{"fmt ", fmt}, data});
		std::vector<std::string> warn;
		DecodedAudio a = dec(w, &warn);
		CHECK(a.pcm == std::vector<int16_t>({7, 8}));
		REQUIRE(warn.size() == 1);
		CHECK(contains(warn[0], "partial-sample"));
	}
	{
		// RIFF size field wrong
		Bytes w = riff({{"fmt ", fmt}, {"data", samples16({1})}}, false);
		std::vector<std::string> warn;
		dec(w, &warn);
		REQUIRE(warn.size() == 1);
		CHECK(contains(warn[0], "riff-size"));
	}
	{
		// fact larger than the decoded data (the retail case of 14 ambient files), and a fact that drops a whole block
		Bytes block;
		put16(block, 0);
		block.push_back(0);
		block.push_back(0);
		block.insert(block.end(), {0, 0, 0, 0}); // 9 samples
		{
			Bytes w = riff({{"fmt ", imaFmt(1, 22050, 8)}, {"fact", factBody(50)}, {"data", block}});
			std::vector<std::string> warn;
			DecodedAudio a = dec(w, &warn);
			CHECK(a.info.frames == 9);
			REQUIRE(warn.size() == 1);
			CHECK(contains(warn[0], "fact-exceeds-data"));
			CHECK(contains(warn[0], "41 missing"));
		}
		{
			Bytes w = riff({{"fmt ", imaFmt(1, 22050, 8)}, {"fact", factBody(0)}, {"data", block}});
			std::vector<std::string> warn;
			DecodedAudio a = dec(w, &warn);
			CHECK(a.info.frames == 0);
			REQUIRE(warn.size() == 1);
			CHECK(contains(warn[0], "fact-trims-block"));
		}
	}
	{
		// blockAlign inconsistent with the sample layout
		Bytes w = riff({{"fmt ", fmtBody(1, 1, 44100, 4, 16)}, {"data", samples16({1, 2})}});
		std::vector<std::string> warn;
		DecodedAudio a = dec(w, &warn);
		CHECK(a.info.frames == 2);
		REQUIRE(warn.size() == 1);
		CHECK(contains(warn[0], "block-align"));
	}
}

TEST_CASE("audio decode: malformed and unknown input throws with a precise message")
{
	Bytes fmt = fmtBody(1, 1, 44100, 2, 16);
	Bytes data = samples16({1, 2});
	auto msgOf = [](const Bytes &b) { return throwsWith([&] { decode(b.data(), b.size()); }); };
	CHECK(contains(msgOf(Bytes()), "unrecognised audio content"));
	CHECK(contains(msgOf(Bytes({'R', 'I', 'F', 'F'})), "not a WAVE form"));
	CHECK(contains(msgOf(Bytes({0x00, 0x01, 0x02, 0x03, 0x04})), "unrecognised audio content"));
	{
		Bytes b = riff({{"data", data}});
		CHECK(contains(msgOf(b), "no fmt chunk"));
	}
	{
		Bytes b = riff({{"fmt ", fmt}});
		CHECK(contains(msgOf(b), "no data chunk"));
	}
	{
		Bytes b = riff({{"fmt ", Bytes(10)}, {"data", data}});
		CHECK(contains(msgOf(b), "fmt chunk is 10 bytes"));
	}
	{
		Bytes b = riff({{"fmt ", fmtBody(1, 0, 44100, 2, 16)}, {"data", data}});
		CHECK(contains(msgOf(b), "0 channels"));
	}
	{
		Bytes b = riff({{"fmt ", fmtBody(1, 1, 0, 2, 16)}, {"data", data}});
		CHECK(contains(msgOf(b), "sample rate of 0"));
	}
	{
		Bytes b = riff({{"fmt ", fmtBody(0x55, 1, 44100, 1, 0)}, {"data", data}}); // MPEG Layer 3 in WAV: unsupported
		std::string m = msgOf(b);
		CHECK(contains(m, "unsupported WAVE format tag 0x0055"));
		CHECK(contains(m, "byte offset"));
	}
	{
		Bytes b = riff({{"fmt ", fmtBody(1, 1, 44100, 2, 12)}, {"data", data}});
		CHECK(contains(msgOf(b), "unsupported PCM bit depth 12"));
	}
	{
		Chunk f{"fmt ", fmt, 5000};
		Bytes b = riff({f});
		CHECK(contains(msgOf(b), "runs past the end"));
	}
	{
		// IMA with a blockAlign that is not header + whole 4-byte groups
		Bytes b = riff({{"fmt ", imaFmt(1, 22050, 10)}, {"data", Bytes(10)}});
		CHECK(contains(msgOf(b), "blockAlign 10"));
	}
	{
		Bytes b = riff({{"fmt ", fmtBody(0x11, 1, 22050, 256, 3)}, {"data", Bytes(256)}});
		CHECK(contains(msgOf(b), "3 bits per sample"));
	}
	// truncated everywhere: no prefix of a valid file decodes silently into a wrong result without throwing or warning
	{
		Bytes good = riff({{"fmt ", fmt}, {"data", data}});
		for (size_t cut = 0; cut < good.size(); ++cut)
		{
			std::vector<std::string> warn;
			bool threw = false;
			DecodedAudio a;
			try
			{
				a = decode(good.data(), cut, &warn);
			}
			catch (const AudioDecodeError &)
			{
				threw = true;
			}
			CHECK((threw || !warn.empty() || cut == good.size()));
		}
	}
}

// ---- MP3 ---------------------------------------------------------------------------------------------------------

namespace
{

struct Mp3Golden
{
	std::string name;
	unsigned rate = 0, channels = 0;
	uint64_t frames = 0;
	int peak = 0;
	double rms = 0;
	std::vector<int> points; // every 251st interleaved sample
};

std::vector<Mp3Golden> loadGolden()
{
	std::vector<Mp3Golden> out;
	std::vector<unsigned char> bytes;
	std::string err;
	REQUIRE_MESSAGE(retailtest::readLocalFile(retailtest::dataDir() + "/audio/mp3_golden.tsv", bytes, &err), err);
	std::istringstream in(std::string(bytes.begin(), bytes.end()));
	std::string line;
	while (std::getline(in, line))
	{
		if (line.empty() || line[0] == '#') continue;
		std::vector<std::string> f;
		std::string cur;
		std::istringstream ls(line);
		while (std::getline(ls, cur, '\t')) f.push_back(cur);
		REQUIRE(f.size() == 7);
		Mp3Golden g;
		g.name = f[0];
		g.rate = unsigned(std::stoul(f[1]));
		g.channels = unsigned(std::stoul(f[2]));
		g.frames = std::stoull(f[3]);
		g.peak = std::stoi(f[4]);
		g.rms = std::stod(f[5]);
		std::istringstream ps(f[6]);
		while (std::getline(ps, cur, ',')) g.points.push_back(std::stoi(cur));
		out.push_back(g);
	}
	return out;
}

Bytes loadFixture(const std::string &name)
{
	std::vector<unsigned char> bytes;
	std::string err;
	const char *env = std::getenv("OPENBFME_AUDIO_FIXTURES");
	const std::string dir = env && *env ? env : OPENBFME_AUDIO_FIXTURE_DIR;
	const std::string path = dir + "/" + name + ".mp3.bin";
	// the streams are generated (never committed): a missing one is a missing prerequisite, reported with the command that makes it
	REQUIRE_MESSAGE(retailtest::readLocalFile(path, bytes, &err),
		err << " -- the synthetic MP3 fixtures are generated, not committed: run `python3 tools/audio/gen_mp3_fixtures.py --out " << dir
			<< "` (needs lame and mpg123; the CMake target audio_mp3_fixtures does it at build time)");
	return Bytes(bytes.begin(), bytes.end());
}

} // namespace

TEST_CASE("audio decode: Layer III Huffman tables are prefix free and complete where the standard says so")
{
	using namespace AudioDecode::mp3tab;
	for (int t = 0; t < kHuffmanTableCount; ++t)
	{
		const HuffmanSource &s = kHuffmanSources[t];
		std::vector<std::pair<std::string, int>> words;
		double kraft = 0;
		for (int i = 0; i < s.n * s.n; ++i)
		{
			REQUIRE(s.len[i] > 0);
			std::string w;
			for (int b = int(s.len[i]) - 1; b >= 0; --b) w += ((s.code[i] >> b) & 1) ? '1' : '0';
			words.push_back({w, i});
			kraft += std::ldexp(1.0, -int(s.len[i]));
		}
		std::sort(words.begin(), words.end());
		bool prefixFree = true;
		for (size_t i = 1; i < words.size(); ++i) prefixFree = prefixFree && words[i].first.compare(0, words[i - 1].first.size(), words[i - 1].first) != 0;
		CHECK_MESSAGE(prefixFree, "Huffman table " << s.tableNumber);
		CHECK_MESSAGE(kraft <= 1.0 + 1e-12, "Huffman table " << s.tableNumber);
	}
	// the synthesis window is odd-symmetric about its centre in the standard's sense: D[256] is the largest
	CHECK(kSynthWindow257[256] == 75038);
	for (int i = 0; i < 256; ++i) CHECK(std::abs(kSynthWindow257[i]) <= 75038);
}

TEST_CASE("audio decode: MP3 fixtures decode to the mpg123 golden numbers")
{
	std::vector<Mp3Golden> golden = loadGolden();
	REQUIRE(golden.size() >= 15);
	size_t points = 0;
	for (const Mp3Golden &g : golden)
	{
		INFO("fixture " << g.name);
		Bytes mp3 = loadFixture(g.name);
		CHECK(mp3.size() < 12 * 1024);
		std::vector<std::string> warn;
		AudioInfo info = probe(mp3.data(), mp3.size(), &warn);
		CHECK(warn.empty());
		CHECK(info.format == Format::Mp3);
		CHECK(info.sampleRate == g.rate);
		CHECK(info.channels == g.channels);
		CHECK(info.frames == g.frames);
		DecodedAudio a = decode(mp3.data(), mp3.size(), &warn);
		CHECK(warn.empty());
		REQUIRE(a.pcm.size() == g.frames * g.channels);
		CHECK(a.info.frames == g.frames);
		int peak = 0;
		double ss = 0;
		for (int16_t s : a.pcm)
		{
			peak = std::max(peak, std::abs(int(s)));
			ss += double(s) * s;
		}
		CHECK(std::abs(peak - g.peak) <= 2);
		CHECK(std::abs(std::sqrt(ss / double(a.pcm.size())) - g.rms) <= 0.5);
		int worst = 0;
		for (size_t i = 0; i < g.points.size(); ++i)
		{
			worst = std::max(worst, std::abs(int(a.pcm[i * 251]) - g.points[i]));
		}
		CHECK_MESSAGE(worst <= 2, "largest difference from the mpg123 sample points: " << worst);
		points += g.points.size();
		// determinism: a second decode is identical
		DecodedAudio b = decode(mp3.data(), mp3.size());
		CHECK(a.pcm == b.pcm);
	}
	std::printf("  info: %zu MP3 fixtures, %zu golden sample points compared (tolerance 2 LSB)\n", golden.size(), points);
}

TEST_CASE("audio decode: MP3 streaming equals the whole-file decode for any chunk size, rewind restarts")
{
	for (const char *name : {"joint_44k", "mpeg2_22k_mono", "id3_44k", "notag_44k"})
	{
		INFO("fixture " << name);
		Bytes mp3 = loadFixture(name);
		DecodedAudio whole = decode(mp3.data(), mp3.size());
		for (size_t chunk : {size_t(1), size_t(7), size_t(576), size_t(1152), size_t(4000)})
		{
			Mp3Stream s(mp3.data(), mp3.size());
			CHECK(s.info().frames == whole.info.frames);
			std::vector<int16_t> all;
			std::vector<int16_t> buf(chunk * s.info().channels);
			size_t n;
			while ((n = s.read(buf.data(), chunk)) > 0) all.insert(all.end(), buf.begin(), buf.begin() + n * s.info().channels);
			CHECK(all == whole.pcm);
			CHECK(s.position() == whole.info.frames);
			CHECK(s.read(buf.data(), chunk) == 0);
			s.rewind();
			CHECK(s.position() == 0);
			std::vector<int16_t> again(whole.pcm.size());
			CHECK(s.read(again.data(), again.size() / s.info().channels) == whole.info.frames);
			CHECK(again == whole.pcm);
			if (chunk > 576) break; // the large chunks add nothing for the later fixtures
		}
	}
}

TEST_CASE("audio decode: MP3 gapless handling, ID3 tags and frame walking")
{
	// a LAME / Info tag is honoured (delay + 529 skipped, padding - 529 trimmed), a plain stream is not trimmed
	Bytes tagged = loadFixture("joint_44k");
	Mp3Stream a(tagged.data(), tagged.size());
	CHECK(a.hasGaplessTag());
	CHECK(a.encoderDelay() > 0);
	CHECK(a.info().frames == uint64_t(a.audioFrames()) * a.samplesPerFrame() - (a.encoderDelay() + 529) - (a.encoderPadding() > 529 ? a.encoderPadding() - 529 : 0));
	Bytes plain = loadFixture("mono_44k"); // lame wrote no Info frame at this bitrate
	Mp3Stream b(plain.data(), plain.size());
	CHECK_FALSE(b.hasGaplessTag());
	CHECK(b.info().frames == uint64_t(b.audioFrames()) * b.samplesPerFrame());
	CHECK(b.info().frames % 1152 == 0);
	// ID3v2 in front and ID3v1 behind are skipped without a warning
	Bytes id3 = loadFixture("id3_44k");
	CHECK(std::memcmp(id3.data(), "ID3", 3) == 0);
	CHECK(std::memcmp(id3.data() + id3.size() - 128, "TAG", 3) == 0);
	std::vector<std::string> warn;
	Mp3Stream c(id3.data(), id3.size(), &warn);
	CHECK(warn.empty());
	// MPEG-2 22.05 kHz fixture: 576 samples per frame
	Bytes m2 = loadFixture("mpeg2_22k_mono");
	Mp3Stream d(m2.data(), m2.size());
	CHECK(d.samplesPerFrame() == 576);
	CHECK(d.info().sampleRate == 22050);
	CHECK_FALSE(d.hasVbriFrame());
}

TEST_CASE("audio decode: MP3 container defects are reported and decided")
{
	Bytes base = loadFixture("notag_44k"); // no Xing frame: every frame is audio
	Mp3Stream clean(base.data(), base.size());
	uint64_t cleanFrames = clean.info().frames;
	{
		// trailing junk after the last frame
		Bytes b = base;
		for (int i = 0; i < 50; ++i) b.push_back(uint8_t(0x11 + i));
		std::vector<std::string> warn;
		AudioInfo info = probe(b.data(), b.size(), &warn);
		CHECK(info.frames == cleanFrames);
		REQUIRE(warn.size() == 1);
		CHECK(contains(warn[0], "trailing-bytes: 50 byte(s)"));
	}
	{
		// a truncated final frame is dropped
		Bytes b = base;
		b.resize(b.size() - 17);
		std::vector<std::string> warn;
		AudioInfo info = probe(b.data(), b.size(), &warn);
		CHECK(info.frames == cleanFrames - 1152);
		REQUIRE(warn.size() == 1);
		CHECK(contains(warn[0], "truncated-last-frame"));
		DecodedAudio a = decode(b.data(), b.size());
		CHECK(a.info.frames == cleanFrames - 1152);
	}
	{
		// junk between an ID3v2-less start and the first frame
		Bytes b(33, 0x20);
		b.insert(b.end(), base.begin(), base.end());
		std::vector<std::string> warn;
		AudioInfo info = probe(b.data(), b.size(), &warn);
		CHECK(info.frames == cleanFrames);
		REQUIRE(warn.size() == 1);
		CHECK(contains(warn[0], "junk-before-first-frame: 33 byte(s)"));
	}
	{
		// a sample-rate / channel change in the middle of the stream cannot be played as one stream
		Bytes b = base;
		Bytes other = loadFixture("mono_48k");
		b.insert(b.end(), other.begin(), other.end());
		CHECK(contains(throwsWith([&] { Mp3Stream s(b.data(), b.size()); }), "stream parameters change"));
	}
	{
		// garbage that is neither a frame nor sync-like throws instead of producing silence
		Bytes b(300, 0x00);
		CHECK(contains(throwsWith([&] { Mp3Stream s(b.data(), b.size()); }), "no valid MPEG Layer III frame found"));
		Bytes junk(300, 0xAB);
		CHECK_FALSE(throwsWith([&] { decode(junk.data(), junk.size()); }).empty());
		// Layer II frame header
		Bytes l2 = {0xFF, 0xFD, 0x90, 0x00, 0, 0, 0, 0};
		CHECK(contains(throwsWith([&] { decode(l2.data(), l2.size()); }), "Layer II is not supported"));
	}
	{
		// a frame whose main data begins before any reservoir exists
		Bytes b = base;
		// frame 2 of notag_44k has main_data_begin > 0; dropping frame 1 leaves it without a reservoir
		Mp3FrameHeader h;
		REQUIRE(parseMp3Header(b.data(), h));
		Bytes cut(b.begin() + h.frameBytes, b.end());
		std::string msg = throwsWith([&] { decode(cut.data(), cut.size()); });
		// it either decodes (main_data_begin == 0 by chance) or throws the reservoir error: never garbage
		if (!msg.empty()) CHECK(contains(msg, "bit reservoir underflow"));
	}
}

TEST_CASE("audio decode: corrupted MP3 fixtures decode or throw AudioDecodeError, nothing else")
{
	// deterministic mutations (bit flips, truncation, inserted junk); run under ASan / UBSan in the lane's checks
	uint32_t rng = 12345;
	auto next = [&]() {
		rng = rng * 1664525u + 1013904223u;
		return rng >> 8;
	};
	size_t decoded = 0, threw = 0;
	for (const char *name : {"joint_44k", "mpeg2_22k_joint", "mono_32k", "id3_44k", "vbr_44k"})
	{
		Bytes base = loadFixture(name);
		for (int it = 0; it < 25; ++it)
		{
			Bytes b = base;
			switch (it % 4)
			{
			case 0:
				for (int k = 0; k < 6; ++k) b[next() % b.size()] ^= uint8_t(1u << (next() % 8));
				break;
			case 1:
				b.resize(1 + next() % (b.size() - 1));
				break;
			case 2:
				b.insert(b.begin() + next() % b.size(), 1 + next() % 30, uint8_t(next()));
				break;
			default:
				for (int k = 0; k < 40; ++k) b[next() % b.size()] = 0;
				break;
			}
			try
			{
				std::vector<std::string> warn;
				DecodedAudio a = decode(b.data(), b.size(), &warn);
				CHECK(a.pcm.size() == size_t(a.info.frames) * a.info.channels);
				++decoded;
			}
			catch (const AudioDecodeError &)
			{
				++threw;
			}
		}
	}
	std::printf("  info: %zu corrupted MP3s decoded, %zu rejected with AudioDecodeError\n", decoded, threw);
}

TEST_CASE("audio decode: MPEG header parser")
{
	Mp3FrameHeader h;
	const uint8_t v1[4] = {0xFF, 0xFB, 0x90, 0x64}; // MPEG-1 L3, 128 kbit/s, 44.1 kHz, no padding, joint stereo
	REQUIRE(parseMp3Header(v1, h));
	CHECK(h.version == 1);
	CHECK(h.bitrate == 128000);
	CHECK(h.sampleRate == 44100);
	CHECK(h.frameBytes == 417);
	CHECK(h.channels == 2);
	CHECK(h.channelMode == 1);
	CHECK(h.samplesPerFrame == 1152);
	CHECK(h.sideInfoBytes == 32);
	const uint8_t v2[4] = {0xFF, 0xF3, 0x40, 0xC4}; // MPEG-2 L3, 32 kbit/s, 22.05 kHz, mono
	REQUIRE(parseMp3Header(v2, h));
	CHECK(h.version == 2);
	CHECK(h.sampleRate == 22050);
	CHECK(h.channels == 1);
	CHECK(h.samplesPerFrame == 576);
	CHECK(h.sideInfoBytes == 9);
	const char *why = nullptr;
	const uint8_t bad[4] = {0xFF, 0xFB, 0xF0, 0x00};
	CHECK_FALSE(parseMp3Header(bad, h, &why));
	CHECK(contains(why, "bitrate"));
	const uint8_t free_[4] = {0xFF, 0xFB, 0x00, 0x00};
	CHECK_FALSE(parseMp3Header(free_, h, &why));
	CHECK(contains(why, "free-format"));
	const uint8_t nosync[4] = {0x12, 0x34, 0x56, 0x78};
	CHECK_FALSE(parseMp3Header(nosync, h, &why));
}

// ---- retail corpus -----------------------------------------------------------------------------------------------

namespace
{

struct CorpusEntry
{
	std::string name;
	Format format = Format::Pcm16;
	bool ok = false;
	std::string error;
	std::vector<std::string> warnings;
	double ms = 0;
	size_t bytes = 0;
	size_t pcmBytes = 0;
	AudioInfo info;
};

std::string lowerAscii(std::string s)
{
	for (char &c : s) c = char(std::tolower((unsigned char)c));
	return s;
}

} // namespace

TEST_CASE("audio decode: every retail WAV and MP3 probes and decodes; the defect list is exact")
{
	retailtest::Mount *mount = retailtest::pureMount();
	if (!mount)
	{
		retailtest::printSkip("audio decode retail corpus");
		return;
	}
	REQUIRE_MESSAGE(mount->fs != nullptr, mount->error);

	FilenameList wavs, mp3s;
	mount->fs->getFileListInDirectory("", "", "*.wav", wavs, true);
	mount->fs->getFileListInDirectory("", "", "*.mp3", mp3s, true);
	std::vector<std::string> names;
	for (const std::string &n : wavs) names.push_back(n);
	for (const std::string &n : mp3s) names.push_back(n);
	REQUIRE(!names.empty());

	// The full decode of all 724 MP3s takes minutes (and far longer under sanitizers). By default every file is probed (container walk, exact
	// counts, the same defect list the decoder reports); WAV files are always decoded; AUDIO_DECODE_ALL=1 decodes every MP3 too.
	const bool decodeAllMp3 = std::getenv("AUDIO_DECODE_ALL") != nullptr;
	std::printf("  info: MP3 files are %s (AUDIO_DECODE_ALL=1 decodes all)\n", decodeAllMp3 ? "fully decoded" : "probed (header walk), not decoded");
	std::vector<CorpusEntry> results(names.size());
	std::atomic<size_t> next{0};
	std::mutex fsMutex;
	unsigned threads = std::max(1u, std::min(8u, std::thread::hardware_concurrency()));
	auto t0 = std::chrono::steady_clock::now();
	auto worker = [&]() {
		for (;;)
		{
			size_t i = next.fetch_add(1);
			if (i >= names.size()) return;
			CorpusEntry &e = results[i];
			e.name = lowerAscii(names[i]);
			std::vector<uint8_t> bytes;
			std::string err;
			{
				std::lock_guard<std::mutex> lock(fsMutex);
				if (!mount->fs->readFile(names[i], bytes, &err))
				{
					e.error = "cannot read: " + err;
					continue;
				}
			}
			e.bytes = bytes.size();
			try
			{
				std::vector<std::string> probeWarnings;
				AudioInfo p = probe(bytes.data(), bytes.size(), &probeWarnings);
				if (p.format == Format::Mp3 && !decodeAllMp3)
				{
					e.info = p;
					e.format = p.format;
					e.warnings = probeWarnings;
					e.ok = true;
					continue;
				}
				auto s0 = std::chrono::steady_clock::now();
				DecodedAudio a = decode(bytes.data(), bytes.size(), &e.warnings);
				auto s1 = std::chrono::steady_clock::now();
				e.ms = std::chrono::duration<double, std::milli>(s1 - s0).count();
				e.info = a.info;
				e.format = a.info.format;
				e.pcmBytes = a.pcm.size() * sizeof(int16_t);
				e.ok = true;
				// probe and decode must agree on the format, rate, channels, length and the defect list
				if (p.format != a.info.format || p.sampleRate != a.info.sampleRate || p.channels != a.info.channels || p.frames != a.info.frames ||
					a.pcm.size() != size_t(a.info.frames) * a.info.channels || probeWarnings != e.warnings)
				{
					e.ok = false;
					e.error = "probe and decode disagree";
				}
			}
			catch (const std::exception &ex)
			{
				e.error = ex.what();
			}
		}
	};
	std::vector<std::thread> pool;
	for (unsigned t = 0; t < threads; ++t) pool.emplace_back(worker);
	for (std::thread &t : pool) t.join();
	double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

	// statistics
	std::map<std::string, size_t> perFormat;
	std::map<std::string, double> msPerFormat;
	size_t peakBytes = 0;
	const CorpusEntry *longest = nullptr;
	std::vector<std::string> failures, defects;
	size_t wavCount = 0, mp3Count = 0;
	for (const CorpusEntry &e : results)
	{
		if (!e.ok)
		{
			failures.push_back(e.name + ": " + e.error);
			continue;
		}
		perFormat[formatName(e.format)]++;
		msPerFormat[formatName(e.format)] += e.ms;
		peakBytes = std::max(peakBytes, e.bytes + e.pcmBytes);
		(e.format == Format::Mp3 ? mp3Count : wavCount)++;
		if (e.format == Format::Mp3 && e.ms > 0 && (!longest || e.ms > longest->ms)) longest = &e;
		for (const std::string &w : e.warnings) defects.push_back(e.name + "\t" + w);
	}
	std::sort(defects.begin(), defects.end());
	std::printf("  info: %zu wav + %zu mp3 files decoded on %u threads in %.1f s wall\n", wavCount, mp3Count, threads, wall);
	for (const auto &kv : perFormat) std::printf("  info: %-10s %6zu files, %9.1f ms decode time (summed over threads)\n", kv.first.c_str(), kv.second, msPerFormat[kv.first]);
	std::printf("  info: largest single file needs %.1f MB (file + decoded s16)\n", peakBytes / 1048576.0);
	if (longest)
	{
		std::printf("  info: longest-decoding MP3 %s: %.1f ms for %.1f s of audio (%llu frames, %u ch, %u Hz)\n", longest->name.c_str(), longest->ms,
			longest->info.durationMs() / 1000.0, (unsigned long long)longest->info.frames, longest->info.channels, longest->info.sampleRate);
	}
	std::printf("  info: %zu defects reported\n", defects.size());

	// the optional dump that regenerates the pinned list
	if (const char *dump = std::getenv("OPENBFME_AUDIO_DUMP_DEFECTS"))
	{
		std::ofstream out(dump, std::ios::binary);
		out << "# retail audio defects: <file>\\t<warning>; counts: wav=" << wavCount << " mp3=" << mp3Count;
		for (const auto &kv : perFormat) out << " " << kv.first << "=" << kv.second;
		out << "\n";
		for (const std::string &d : defects) out << d << "\n";
	}

	for (const std::string &f : failures) FAIL_CHECK("retail file does not decode: " << f);
	CHECK(failures.empty());

	// the pinned defect list
	std::vector<unsigned char> pinnedBytes;
	std::string err;
	REQUIRE_MESSAGE(retailtest::readLocalFile(retailtest::dataDir() + "/audio/retail_defects.tsv", pinnedBytes, &err), err);
	std::istringstream in(std::string(pinnedBytes.begin(), pinnedBytes.end()));
	std::string line, header;
	std::vector<std::string> pinned;
	while (std::getline(in, line))
	{
		if (line.empty()) continue;
		if (line[0] == '#')
		{
			if (header.empty()) header = line;
			continue;
		}
		pinned.push_back(line);
	}
	std::set<std::string> a(defects.begin(), defects.end()), b(pinned.begin(), pinned.end());
	size_t shown = 0;
	for (const std::string &d : defects)
		if (!b.count(d) && shown++ < 10) FAIL_CHECK("new defect not in retail_defects.tsv: " << d);
	shown = 0;
	for (const std::string &d : pinned)
		if (!a.count(d) && shown++ < 10) FAIL_CHECK("pinned defect no longer reported: " << d);
	CHECK(defects.size() == pinned.size());
	std::ostringstream counts;
	counts << "# retail audio defects: <file>\\t<warning>; counts: wav=" << wavCount << " mp3=" << mp3Count;
	for (const auto &kv : perFormat) counts << " " << kv.first << "=" << kv.second;
	CHECK(header == counts.str());
}
