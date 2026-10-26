// Synthetic MPEG Layer III frames and no-game decoder coverage. GPL-3.0.
#include "doctest.h"
#include "Common/Audio/Mp3Decoder.h"
#include "Common/Audio/Mp3Tables.h"

#include <stdexcept>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

using namespace AudioDecode;

namespace
{
using Bytes = std::vector<uint8_t>;

// ISO 11172-3 side information is MSB-first. The frame's main data is zero-filled
// unless the caller supplies an explicitly coded Huffman prefix.
struct BitWriter
{
	Bytes bytes;
	size_t bits = 0;
	void put(unsigned value, unsigned width)
	{
		for (unsigned i = width; i != 0; --i)
		{
			if (bits / 8 == bytes.size()) bytes.push_back(0);
			bytes[bits / 8] |= uint8_t(((value >> (i - 1)) & 1u) << (7 - bits % 8));
			++bits;
		}
	}
};

struct Granule
{
	unsigned length = 0, big = 0, gain = 210, scalefac = 0;
	unsigned block = 0, mixed = 0, table = 0, table1 = 0, table2 = 0;
	unsigned region0 = 0, region1 = 0, pre = 0, scale = 0, count1 = 0;
};

struct Frame
{
	int version = 1; // 1, 2, or 25
	unsigned bitrateIndex = 9, rateIndex = 0, mode = 3, extension = 0;
	bool crc = false, padding = false;
	unsigned begin = 0, scfsi = 0;
	Granule gr[2][2];
	Bytes main;
};

Bytes makeFrame(const Frame &f)
{
	unsigned ver = f.version == 1 ? 3u : f.version == 2 ? 2u : 0u;
	Bytes b = {0xFF, uint8_t(0xE0 | (ver << 3) | 2 | (!f.crc)),
		uint8_t((f.bitrateIndex << 4) | (f.rateIndex << 2) | (unsigned(f.padding) << 1)),
		uint8_t((f.mode << 6) | (f.extension << 4))};
	Mp3FrameHeader h;
	if (!parseMp3Header(b.data(), h)) throw std::logic_error("bad synthetic frame header");
	if (f.crc) b.insert(b.end(), {0, 0}); // CRC is intentionally not checked by this decoder
	BitWriter si;
	si.put(f.begin, f.version == 1 ? 9 : 8);
	si.put(0, f.version == 1 ? (h.channels == 1 ? 5 : 3) : (h.channels == 1 ? 1 : 2));
	if (f.version == 1)
		for (unsigned ch = 0; ch < h.channels; ++ch) si.put(f.scfsi, 4);
	for (unsigned gr = 0; gr < (f.version == 1 ? 2u : 1u); ++gr)
		for (unsigned ch = 0; ch < h.channels; ++ch)
		{
			const Granule &g = f.gr[gr][ch];
			si.put(g.length, 12);
			si.put(g.big, 9);
			si.put(g.gain, 8);
			si.put(g.scalefac, f.version == 1 ? 4 : 9);
			si.put(g.block != 0, 1);
			if (g.block)
			{
				si.put(g.block - 1, 2); // block=1..4 means encoded type=0..3
				si.put(g.mixed, 1);
				si.put(g.table, 5);
				si.put(g.table1, 5);
				for (int w = 0; w < 3; ++w) si.put(0, 3);
			}
			else
			{
				si.put(g.table, 5);
				si.put(g.table1, 5);
				si.put(g.table2, 5);
				si.put(g.region0, 4);
				si.put(g.region1, 3);
			}
			if (f.version == 1) si.put(g.pre, 1);
			si.put(g.scale, 1);
			si.put(g.count1, 1);
		}
	if (si.bits != h.sideInfoBytes * 8) throw std::logic_error("synthetic side information size");
	b.insert(b.end(), si.bytes.begin(), si.bytes.end());
	b.insert(b.end(), f.main.begin(), f.main.end());
	b.resize(h.frameBytes, 0);
	return b;
}

Bytes join(const Bytes &a, const Bytes &b)
{
	Bytes out = a;
	out.insert(out.end(), b.begin(), b.end());
	return out;
}

std::string errorFrame(const Frame &f, uint64_t offset = 17)
{
	Bytes b = makeFrame(f);
	Mp3FrameHeader h;
	if (!parseMp3Header(b.data(), h)) throw std::logic_error("bad synthetic frame header");
	Mp3Decoder d;
	std::vector<int16_t> pcm(h.samplesPerFrame * h.channels);
	try { d.decodeFrame(h, b.data(), pcm.data(), offset); }
	catch (const AudioDecodeError &e) { return e.what(); }
	return "";
}

std::vector<int16_t> decodeFrame(const Frame &f)
{
	Bytes b = makeFrame(f);
	Mp3FrameHeader h;
	if (!parseMp3Header(b.data(), h)) throw std::logic_error("bad synthetic frame header");
	std::vector<int16_t> pcm(h.samplesPerFrame * h.channels, -7);
	Mp3Decoder d;
	if (d.decodeFrame(h, b.data(), pcm.data()) != h.samplesPerFrame) throw std::logic_error("wrong sample count");
	return pcm;
}

bool silent(const std::vector<int16_t> &pcm)
{
	return std::all_of(pcm.begin(), pcm.end(), [](int16_t x) { return x == 0; });
}
} // namespace

TEST_CASE("coverage1: Layer III header versions, rates, protection and rejected syntax")
{
	for (int version : {1, 2, 25})
		for (unsigned rate = 0; rate < 3; ++rate)
		{
			Frame f;
			f.version = version;
			f.rateIndex = rate;
			f.mode = 1;
			f.extension = 3;
			f.crc = f.padding = true;
			Bytes b = makeFrame(f);
			Mp3FrameHeader h;
			REQUIRE(parseMp3Header(b.data(), h));
			CHECK(h.version == version);
			CHECK(h.crcProtected);
			CHECK(h.padding);
			CHECK(h.modeExtension == 3);
			CHECK(h.headerBytes() == 6);
			CHECK(h.frameBytes == b.size());
			CHECK(h.samplesPerFrame == (version == 1 ? 1152 : 576));
		}
	Frame f;
	Bytes b = makeFrame(f);
	Mp3FrameHeader h;
	const char *why = nullptr;
	struct Bad { unsigned byte, value; const char *why; };
	for (const Bad &bad : {Bad{0, 0, "no frame sync"}, {1, 0xEB, "reserved MPEG version"},
		Bad{1, 0xF8, "reserved layer"}, Bad{1, 0xFD, "Layer II"}, Bad{1, 0xFF, "Layer I"},
		Bad{2, 0, "free-format"}, Bad{2, 0xF0, "invalid bitrate"}, Bad{2, 0x9C, "reserved sample rate"}})
	{
		Bytes corrupted = b;
		corrupted[bad.byte] = uint8_t(bad.value);
		REQUIRE_FALSE(parseMp3Header(corrupted.data(), h, &why));
		CHECK(std::string(why).find(bad.why) != std::string::npos); // tolerance: substring asserts the diagnostic category, not its wording
	}
	f.mode = 0;
	f.extension = 3;
	b = makeFrame(f);
	REQUIRE(parseMp3Header(b.data(), h));
	CHECK(h.modeExtension == 0);
}

TEST_CASE("coverage1: silent MPEG versions, mono stereo, switched windows and scfsi")
{
	for (int version : {1, 2, 25})
		for (unsigned mode : {0u, 1u, 3u})
			for (unsigned block : {0u, 2u, 3u, 4u}) // long, start, short, stop
			{
				Frame f;
				f.version = version;
				f.mode = mode;
				f.extension = mode == 1 ? 3 : 0;
				f.scfsi = block == 0 ? 15 : 0;
				f.crc = true;
				for (auto &gr : f.gr) for (Granule &g : gr) { g.block = block; g.mixed = 0; }
				std::vector<int16_t> pcm = decodeFrame(f);
				CHECK(pcm.size() == size_t(version == 1 ? 1152 : 576) * (mode == 3 ? 1u : 2u));
				CHECK(silent(pcm));
			}
	for (int version : {1, 2, 25})
	{
		Frame f;
		f.version = version;
		f.gr[0][0].block = 3;
		f.gr[0][0].mixed = 1;
		CHECK(silent(decodeFrame(f)));
	}
	// MPEG-1 scalefactors consume real bits, including mixed long/short bands.
	for (unsigned block : {0u, 3u})
		for (unsigned mixed : {0u, 1u})
		{
			Frame f;
			f.scfsi = block == 0 ? 15 : 0; // granule 1 reuses all four groups for a long block
			Granule &g = f.gr[0][0];
			g.block = block;
			g.mixed = mixed;
			g.scalefac = 15; // slen1=4, slen2=3
			g.length = block == 0 ? 74 : mixed ? 122 : 126;
			CHECK(silent(decodeFrame(f)));
		}
	// LSF normal partition rows 0/1/2 and intensity rows 3/4/5 at their boundaries.
	for (unsigned block : {0u, 3u})
		for (unsigned mixed : {0u, 1u})
			for (unsigned compress : {0u, 400u, 500u})
			{
				Frame f;
				f.version = 2;
				f.gr[0][0].block = block;
				f.gr[0][0].mixed = mixed;
				f.gr[0][0].scalefac = compress;
				CHECK(silent(decodeFrame(f)));
				f.mode = 1;
				f.extension = 3;
				f.gr[0][1] = f.gr[0][0];
				f.gr[0][1].scalefac = compress == 0 ? 0 : compress == 400 ? 360 : 488;
				CHECK(silent(decodeFrame(f)));
			}
}

TEST_CASE("coverage1: Huffman pairs, signs, count1 and overlap reset")
{
	Frame f;
	f.gr[0][0].big = 1;
	f.gr[0][0].table = 1;
	f.gr[0][0].gain = 250;
	f.gr[0][0].length = 4;
	// Table 1 (1,0): 001 followed by a positive sign; only one pair is coded.
	f.main = {0x20};
	std::vector<int16_t> positive = decodeFrame(f);
	CHECK_FALSE(silent(positive));
	f.main = {0x30}; // same pair with negative sign
	std::vector<int16_t> negative = decodeFrame(f);
	CHECK_FALSE(positive == negative);
	for (unsigned block : {2u, 3u, 4u})
	{
		f.gr[0][0].block = block;
		f.gr[0][0].mixed = block == 3;
		CHECK_FALSE(silent(decodeFrame(f)));
	}
	f.gr[0][0].block = 3;
	f.gr[0][0].mixed = 0;
	CHECK_FALSE(silent(decodeFrame(f))); // pure short: nonzero 12-point IMDCT
	Frame mid;
	mid.mode = 1;
	mid.extension = 2;
	mid.gr[0][0].big = 1;
	mid.gr[0][0].table = 1;
	mid.gr[0][0].gain = 250;
	mid.gr[0][0].length = 4;
	mid.main = {0x20};
	auto midPcm = decodeFrame(mid);
	CHECK_FALSE(silent(midPcm));
	for (size_t i = 0; i < midPcm.size(); i += 2) CHECK(midPcm[i] == midPcm[i + 1]);
	// A longer spectrum crosses the alias-reduction boundary at line 18.
	BitWriter repeated;
	for (int i = 0; i < 24; ++i) repeated.put(2, 4); // (1,0), positive
	Frame bands;
	bands.gr[0][0].big = 24;
	bands.gr[0][0].table = bands.gr[0][0].table1 = bands.gr[0][0].table2 = 1;
	bands.gr[0][0].length = unsigned(repeated.bits);
	bands.main = repeated.bytes;
	CHECK_FALSE(silent(decodeFrame(bands)));
	// Table 16/24 encode the same escaped value with different linbits lengths.
	// Use the public ISO code tables, not a binary fixture or decoder internals.
	std::vector<int16_t> escaped;
	for (unsigned table : {16u, 24u})
	{
		for (const auto &source : mp3tab::kHuffmanSources)
		{
			if (source.tableNumber != int(table)) continue;
			BitWriter bits;
			unsigned index = 15 * source.n; // x=15, y=0
			bits.put(source.code[index], source.len[index]);
			bits.put(1, table == 16 ? 1 : 4); // escaped x=16
			bits.put(0, 1); // positive sign
			Frame escapedFrame;
			escapedFrame.gr[0][0].table = table;
			escapedFrame.gr[0][0].big = 1;
			escapedFrame.gr[0][0].length = unsigned(bits.bits);
			escapedFrame.main = bits.bytes;
			auto pcm = decodeFrame(escapedFrame);
			CHECK_FALSE(silent(pcm));
			if (table == 16) escaped = pcm;
			else CHECK(pcm == escaped);
		}
	}
	f.gr[0][0].block = 0;
	f.gr[0][0].big = 0;
	f.gr[0][0].table = 0;
	f.gr[0][0].length = 5;
	f.gr[0][0].count1 = 0;
	f.main = {0x70}; // quad table A: v=8 code 0111, positive sign
	CHECK_FALSE(silent(decodeFrame(f)));
	f.gr[0][0].count1 = 1;
	CHECK_FALSE(silent(decodeFrame(f))); // quad table B: v=8 code 0111
	f.gr[0][0].length = 0;
	f.main.clear();
	Bytes silence = makeFrame(f);
	Mp3FrameHeader h;
	REQUIRE(parseMp3Header(silence.data(), h));
	Mp3Decoder decoder;
	std::vector<int16_t> out(h.samplesPerFrame);
	Frame signal;
	signal.gr[0][0].big = 1;
	signal.gr[0][0].table = 1;
	signal.gr[0][0].gain = 250;
	signal.gr[0][0].length = 4;
	signal.gr[1][0] = signal.gr[0][0]; // signal in the last granule leaves overlap for the next frame
	signal.gr[0][0] = Granule();
	signal.main = {0x20};
	Bytes encoded = makeFrame(signal);
	REQUIRE(decoder.decodeFrame(h, encoded.data(), out.data()) == 1152);
	CHECK_FALSE(silent(out));
	decoder.decodeFrame(h, silence.data(), out.data());
	CHECK_FALSE(silent(out)); // synthesis / IMDCT history of the previous frame
	decoder.reset();
	decoder.decodeFrame(h, silence.data(), out.data());
	CHECK(silent(out));
}

TEST_CASE("coverage1: invalid granules and bit-reservoir underflow have diagnostics")
{
	Frame f;
	f.gr[0][0].block = 1; // switched block_type=0 is forbidden
	CHECK(errorFrame(f).find("block_type 0") != std::string::npos); // tolerance: diagnostic category
	f = Frame();
	f.begin = 1;
	CHECK(errorFrame(f).find("bit reservoir underflow") != std::string::npos); // tolerance: diagnostic category
	f = Frame();
	f.gr[0][0].big = 289;
	CHECK(errorFrame(f).find("exceeds 288") != std::string::npos); // tolerance: diagnostic category
	f = Frame();
	f.gr[0][0].big = 1;
	f.gr[0][0].table = 4;
	CHECK(errorFrame(f).find("Huffman table 4 is not defined") != std::string::npos); // tolerance: diagnostic category
	f.gr[0][0].table = 14;
	CHECK(errorFrame(f).find("Huffman table 14 is not defined") != std::string::npos); // tolerance: diagnostic category
	f.gr[0][0].table = 1;
	f.gr[0][0].length = 1;
	f.main = {0x20};
	CHECK(errorFrame(f).find("Huffman data of a granule overruns") != std::string::npos); // tolerance: diagnostic category
	f = Frame();
	f.gr[0][0].scalefac = 15;
	f.gr[0][0].length = 1;
	CHECK(errorFrame(f).find("scalefactors overrun") != std::string::npos); // tolerance: diagnostic category
	f = Frame();
	f.gr[0][0].length = 1; // incomplete count1 code is padding, not a spectral value
	CHECK(silent(decodeFrame(f)));
	f = Frame();
	f.bitrateIndex = 1;
	f.gr[0][0].length = 4095;
	CHECK(errorFrame(f).find("frame's main data ends") != std::string::npos); // tolerance: diagnostic category
}

TEST_CASE("coverage1: reservoir back-reference, rollover and reset")
{
	Frame first;
	Bytes b1 = makeFrame(first);
	Mp3FrameHeader h;
	REQUIRE(parseMp3Header(b1.data(), h));
	Frame second = first;
	second.begin = 1;
	Bytes b2 = makeFrame(second);
	Mp3Decoder d;
	std::vector<int16_t> out(h.samplesPerFrame);
	CHECK(d.decodeFrame(h, b1.data(), out.data()) == 1152);
	CHECK(d.decodeFrame(h, b2.data(), out.data()) == 1152);
	CHECK(silent(out));
	d.reset();
	try { d.decodeFrame(h, b2.data(), out.data(), 99); FAIL("reset did not empty the reservoir"); }
	catch (const AudioDecodeError &e) { CHECK(e.offset() == 99); }
	// 320 kbit/s / 32 kHz holds more main data than the 511-byte reservoir.
	Frame large;
	large.bitrateIndex = 14;
	large.rateIndex = 2;
	Bytes big = makeFrame(large);
	Mp3FrameHeader bigHeader;
	REQUIRE(parseMp3Header(big.data(), bigHeader));
	CHECK(d.decodeFrame(bigHeader, big.data(), out.data()) == 1152);
	Frame back = large;
	back.begin = 511;
	Bytes referenced = makeFrame(back);
	CHECK(d.decodeFrame(bigHeader, referenced.data(), out.data()) == 1152);
	CHECK(silent(out));
}

TEST_CASE("coverage1: synthetic MP3 stream scans tags, junk, truncated frames and format changes")
{
	Frame f;
	Bytes frame = makeFrame(f);
	Bytes movie = join(frame, frame);
	CHECK(looksLikeMp3(movie.data(), movie.size()));
	Bytes single = frame;
	CHECK(looksLikeMp3(single.data(), single.size()));
	Bytes stray = join(frame, Bytes{0x10});
	CHECK_FALSE(looksLikeMp3(stray.data(), stray.size()));
	Bytes id3 = {'I', 'D', '3', 4, 0, 0x10, 0, 0, 0, 2, 0x11, 0x22};
	id3.insert(id3.end(), 10, 0); // ID3v2.4 footer
	id3.insert(id3.end(), {0x13, 0x37});
	id3 = join(id3, movie);
	id3.insert(id3.end(), 128, 0);
	std::memcpy(id3.data() + id3.size() - 128, "TAG", 3);
	std::vector<std::string> warnings;
	Mp3Stream s(id3.data(), id3.size(), &warnings);
	CHECK(warnings == std::vector<std::string>{"junk-before-first-frame: 2 byte(s) at offset 22"});
	CHECK(s.info().format == Format::Mp3);
	CHECK(s.info().frames == 2304);
	CHECK(s.audioFrames() == 2);
	CHECK_FALSE(s.hasGaplessTag());
	std::vector<int16_t> pcm(2304, -1);
	CHECK(s.read(pcm.data(), 0) == 0);
	CHECK(s.read(pcm.data(), 7) == 7);
	CHECK(s.position() == 7);
	CHECK(s.read(pcm.data() + 7, 2304) == 2297);
	CHECK(silent(pcm));
	CHECK(s.read(pcm.data(), 1) == 0);
	s.rewind();
	CHECK(s.position() == 0);
	CHECK(s.read(pcm.data(), 2304) == 2304);
	CHECK(silent(pcm));
	Bytes broken = join(movie, Bytes{0x42, 0x43});
	warnings.clear();
	Mp3Stream trailing(broken.data(), broken.size(), &warnings);
	CHECK(warnings == std::vector<std::string>{"trailing-bytes: 2 byte(s) after the last MPEG frame at offset 834"});
	broken = join(frame, Bytes{0x55, 0x56, 0x57});
	broken = join(broken, movie);
	warnings.clear();
	Mp3Stream between(broken.data(), broken.size(), &warnings);
	CHECK(warnings == std::vector<std::string>{"junk-between-frames: 3 byte(s) at offset 417"});
	CHECK(between.info().frames == 3456);
	CHECK(between.read(pcm.data(), 1152) == 1152);
	broken = join(frame, Bytes(frame.begin(), frame.end() - 1));
	warnings.clear();
	Mp3Stream cut(broken.data(), broken.size(), &warnings);
	CHECK(cut.audioFrames() == 1);
	CHECK(warnings == std::vector<std::string>{"truncated-last-frame: MPEG frame at offset 417 (416 of 417 bytes) dropped"});
	Frame other;
	other.rateIndex = 1;
	broken = join(frame, makeFrame(other));
	try { Mp3Stream change(broken.data(), broken.size()); FAIL("accepted rate change"); }
	catch (const AudioDecodeError &e) { CHECK(std::string(e.what()).find("stream parameters change") != std::string::npos); } // tolerance: diagnostic category
}

TEST_CASE("coverage1: Xing LAME trimming, exhausted tags and VBRI audio")
{
	Frame f;
	Bytes tag = makeFrame(f);
	Mp3FrameHeader h;
	REQUIRE(parseMp3Header(tag.data(), h));
	const size_t at = h.headerBytes() + h.sideInfoBytes;
	std::memcpy(tag.data() + at, "Xing", 4);
	std::memcpy(tag.data() + at + 8, "LAME", 4);
	// encoder delay=0, padding=600 -> skip 529 at start, trim 71 at end
	tag[at + 8 + 22] = 2;
	tag[at + 8 + 23] = 88;
	Bytes movie = join(tag, join(makeFrame(f), makeFrame(f)));
	std::vector<std::string> warnings;
	Mp3Stream s(movie.data(), movie.size(), &warnings);
	CHECK(warnings.empty());
	CHECK(s.hasGaplessTag());
	CHECK(s.audioFrames() == 2);
	CHECK(s.encoderDelay() == 0);
	CHECK(s.encoderPadding() == 600);
	CHECK(s.info().frames == 1704);
	std::vector<int16_t> pcm(1704, -1);
	CHECK(s.read(pcm.data(), 1) == 1);
	CHECK(s.position() == 1);
	CHECK(s.read(pcm.data() + 1, 1703) == 1703);
	CHECK(silent(pcm));
	CHECK(s.read(pcm.data(), 1) == 0);
	s.rewind();
	CHECK(s.read(pcm.data(), 1704) == 1704);
	// Excessive delay/padding does not silently produce an empty audio stream.
	Bytes excessive = movie;
	excessive[at + 8 + 21] = 255;
	excessive[at + 8 + 22] = 242;
	excessive[at + 8 + 23] = 255;
	warnings.clear();
	Mp3Stream ignored(excessive.data(), excessive.size(), &warnings);
	CHECK_FALSE(ignored.hasGaplessTag());
	CHECK(ignored.info().frames == 2304);
	CHECK(warnings.size() == 1);
	Bytes vbri = makeFrame(f);
	std::memcpy(vbri.data() + 36, "VBRI", 4);
	Mp3Stream v(vbri.data(), vbri.size());
	CHECK(v.hasVbriFrame());
	CHECK(v.audioFrames() == 1);
	std::vector<int16_t> vbriPcm(1152, -1);
	CHECK(v.read(vbriPcm.data(), 1152) == 1152);
	CHECK(silent(vbriPcm)); // VBRI lies in ancillary data, not in a coded granule
	try { Mp3Stream metadataOnly(tag.data(), tag.size()); FAIL("accepted metadata without audio"); }
	catch (const AudioDecodeError &e) { CHECK(std::string(e.what()).find("no audio frame") != std::string::npos); } // tolerance: diagnostic category
	Bytes invalidId3 = {'I', 'D', '3', 4, 0, 0, 0, 0, 1, 0}; // declares 128 absent bytes
	try { Mp3Stream invalid(invalidId3.data(), invalidId3.size()); FAIL("accepted truncated ID3"); }
	catch (const AudioDecodeError &e) { CHECK(std::string(e.what()).find("ID3v2 tag of 138 bytes") != std::string::npos); } // tolerance: diagnostic category
}

TEST_CASE("coverage1: intensity stereo ignores coded zero right-channel quads")
{
	Frame f;
	f.mode = 1;
	f.extension = 1; // intensity, without mid/side
	f.gr[0][0].big = 1;
	f.gr[0][0].table = 1;
	f.gr[0][0].gain = 250;
	f.gr[0][0].length = 4;
	f.main = {0x20}; // left (1,0), right part2_3_length=0
	std::vector<int16_t> baseline = decodeFrame(f);
	CHECK_FALSE(silent(baseline));
	// MPEG-1 is_pos=0 gives (left gain,right gain)=(0,1). Its output must
	// exactly equal ordinary stereo with the same spectral pair coded on the right.
	Frame reference;
	reference.mode = 0;
	reference.gr[0][1] = f.gr[0][0];
	reference.main = {0x20};
	CHECK(baseline == decodeFrame(reference));
	// Append an all-zero count1 quadruple in the right channel. It extends
	// the decoded region by four lines but MUST NOT move the last nonzero right
	// sample or change intensity stereo's boundary.
	f.gr[0][1].length = 1;
	f.main = {0x28}; // 0010 1..., right quad A (0,0,0,0) code 1
	CHECK(decodeFrame(f) == baseline);
}
