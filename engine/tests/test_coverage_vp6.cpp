// Synthetic no-game coverage tests. GPL-3.0.
#include "doctest.h"

#include "GameClient/VP6Decoder.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace
{
// Emit arithmetic intervals at increasing binary precision. This is a tiny
// test-only VP6 bool-stream writer: the low endpoint of each chosen interval
// is an unambiguous code word; trailing zero decisions need no further bits.
class BoolStream
{
public:
	void put(int bit, int probability = 128)
	{
		const int split = 1 + ((m_range - 1) * probability >> 8);
		if (bit)
		{
			int add = split;
			for (int i = (int)m_bits.size() - 1; add; --i)
			{
				const int digit = m_bits[(size_t)i] + (add & 1);
				m_bits[(size_t)i] = (std::uint8_t)(digit & 1);
				add = (add >> 1) + (digit >> 1);
			}
			m_range -= split;
		}
		else
		{
			m_range = split;
		}
		while (m_range < 128)
		{
			m_range <<= 1;
			m_bits.push_back(0);
		}
	}

	std::vector<std::uint8_t> bytes() const
	{
		std::vector<std::uint8_t> result((m_bits.size() + 7) / 8 + 3, 0);
		for (size_t i = 0; i < m_bits.size(); ++i)
		{
			result[i / 8] |= (std::uint8_t)(m_bits[i] << (7 - i % 8));
		}
		return result;
	}

private:
	int m_range = 255;
	std::vector<std::uint8_t> m_bits = std::vector<std::uint8_t>(8, 0);
};

// The frame header is constructed here, not copied from a game movie. A key
// frame has one macroblock, a separate coefficient partition, and the default
// VP6 models (all subsequent range-coded model-update decisions are zero).
std::vector<std::uint8_t> keyFrame(int cols = 1, int rows = 1, int sub = 8, int q = 63, bool huffman = false,
	const std::vector<std::uint8_t> &coefficients = std::vector<std::uint8_t>(96, 0), int filterMode = 0, bool filterHeader = true)
{
	BoolStream header;
	header.put(0); // the key frame's two reserved bits
	header.put(0);
	if (filterHeader)
	{
		header.put(filterMode == 2);
		if (filterMode == 2)
		{
			for (int i = 0; i < 8; ++i)
			{
				header.put(0); // sample variance threshold and max vector length
			}
		}
		else
		{
			header.put(filterMode == 1);
		}
	}
	if (filterHeader && sub == 8)
	{
		for (int i = 0; i < 4; ++i)
		{
			header.put(0); // filter selection
		}
	}
	header.put(huffman ? 1 : 0);
	std::vector<std::uint8_t> frame = { (std::uint8_t)(1 | (q << 1)), (std::uint8_t)((sub << 3) | (filterHeader ? 2 : 0)), 0, 0,
		(std::uint8_t)rows, (std::uint8_t)cols, (std::uint8_t)rows, (std::uint8_t)cols };
	std::vector<std::uint8_t> coded = header.bytes();
	coded.resize(96, 0);
	const size_t offset = frame.size() + coded.size();
	frame[2] = (std::uint8_t)(offset >> 8);
	frame[3] = (std::uint8_t)offset;
	frame.insert(frame.end(), coded.begin(), coded.end());
	frame.insert(frame.end(), coefficients.begin(), coefficients.end());
	return frame;
}

// The previous-picture copy macroblock is selected by the default type model
// when all decisions after the header are zero.
std::vector<std::uint8_t> interFrame(int q = 63, bool golden = false)
{
	BoolStream body;
	body.put(golden ? 1 : 0); // refresh golden reference
	body.put(0); // no deblocking
	body.put(0); // no filter update (VP6 sub-version 8)
	body.put(0); // range-coded coefficients
	std::vector<std::uint8_t> frame = { (std::uint8_t)(0x80 | (q << 1)) };
	const auto coded = body.bytes();
	frame.insert(frame.end(), coded.begin(), coded.end());
	frame.resize(160, 0);
	return frame;
}

// q=0, default DC probability contexts 0/1: b0=+1, b1=+1,
// b2=-1, b3=-1, and zero chroma residuals. Each block ends at AC index 1.
std::vector<std::uint8_t> steppedCoefficients()
{
	BoolStream bits;
	for (int b = 0; b < 6; ++b)
	{
		const int dc = b == 0 || b == 1 ? 1 : b == 2 || b == 3 ? -1 : 0;
		const int ctx = b == 0 || b >= 4 ? 0 : b == 3 ? 2 : 1;
		bits.put(dc != 0, ctx == 0 ? 194 : ctx == 1 ? 118 : 55);
		if (dc)
		{
			bits.put(0, ctx == 0 ? 210 : ctx == 1 ? 156 : 81); // coefficient magnitude one
			bits.put(dc < 0);
		}
		bits.put(0); // no AC coefficient
		bits.put(0); // end block
	}
	auto result = bits.bytes();
	result.resize(96, 0);
	return result;
}

// Pack a sequence of MSB-first Huffman symbols directly as bits. With default
// models, the EOB leaf (symbol 11) has the 2-bit prefix 11.
std::vector<std::uint8_t> huffmanEobs()
{
	return { 0xff, 0xf0 }; // six independent 11 EOB symbols
}

std::vector<std::uint8_t> huffmanNullRuns()
{
	// Default Huffman code 10 is a zero coefficient, 11 is EOB.
	// b0: zero DC, repeat for the following three luma blocks (10 10 01),
	//     then EOB and repeat for those blocks (11 10 01).
	// b4: zero DC and EOB, each repeated for one more chroma block.
	const std::string symbols = "10100111100110011101";
	std::vector<std::uint8_t> bytes((symbols.size() + 7) / 8, 0);
	for (size_t i = 0; i < symbols.size(); ++i)
	{
		bytes[i / 8] |= (std::uint8_t)((symbols[i] - '0') << (7 - i % 8));
	}
	return bytes;
}

void le32(std::vector<std::uint8_t> &out, std::uint32_t n)
{
	for (int i = 0; i != 4; ++i)
	{
		out.push_back((std::uint8_t)(n >> (8 * i)));
	}
}

void chunk(std::vector<std::uint8_t> &out, const char *tag, const std::vector<std::uint8_t> &payload)
{
	out.insert(out.end(), tag, tag + 4);
	le32(out, (std::uint32_t)(payload.size() + 8));
	out.insert(out.end(), payload.begin(), payload.end());
}

std::vector<std::uint8_t> movie(const std::string &codec = "vp60")
{
	std::vector<std::uint8_t> file, header(24, 0);
	std::copy(codec.begin(), codec.end(), header.begin());
	header[4] = 16; // picture width
	header[6] = 16; // picture height
	header[8] = 2; // two frames
	header[12] = 6; // largest payload size (informational)
	header[16] = 25; // 25/1 FPS
	header[20] = 1;
	chunk(file, "MVhd", header);
	chunk(file, "MV0K", { 1, 2, 3, 4, 5, 6 });
	chunk(file, "MV0F", { 7, 8 });
	return file;
}
} // namespace

TEST_CASE("coverage1: VP6 synthetic key frame decodes YUV and BT.601 RGBA without a movie install")
{
	const auto frame = keyFrame();
	VP6Decoder decoder;
	CHECK_FALSE(decoder.hasFrame());
	CHECK(decoder.codedWidth() == 0);
	CHECK(decoder.codedHeight() == 0);
	std::string error = "old error";
	REQUIRE_MESSAGE(decoder.decodeFrame(frame.data(), frame.size(), &error), error);
	CHECK(decoder.hasFrame());
	CHECK(decoder.lastWasKey());
	CHECK(decoder.codedWidth() == 16);
	CHECK(decoder.codedHeight() == 16);
	std::vector<std::uint8_t> y, u, v;
	decoder.copyPlanes(y, u, v);
	REQUIRE(y.size() == 256);
	REQUIRE(u.size() == 64);
	REQUIRE(v.size() == 64);
	CHECK(std::all_of(y.begin(), y.end(), [](std::uint8_t p) { return p == 128; }));
	CHECK(std::all_of(u.begin(), u.end(), [](std::uint8_t p) { return p == 160; }));
	CHECK(u == v);
	CHECK(decoder.lumaAt(0, 0) == 128);
	CHECK(decoder.lumaAt(15, 15) == 128);
	CHECK(decoder.lumaAt(-1, 0) == 0);
	CHECK(decoder.lumaAt(16, 0) == 0);
	CHECK(decoder.lumaAt(0, 16) == 0);
	std::vector<std::uint8_t> rgba(17 * 17 * 4, 0);
	decoder.toRGBA(17, 17, rgba.data()); // crops to the coded edge
	CHECK(std::vector<std::uint8_t>(rgba.begin(), rgba.begin() + 4) == std::vector<std::uint8_t>{ 181, 92, 195, 255 });
	CHECK(std::vector<std::uint8_t>(rgba.end() - 4, rgba.end()) == std::vector<std::uint8_t>{ 181, 92, 195, 255 });
	VP6Decoder alpha;
	REQUIRE_MESSAGE(alpha.decodeFrame(frame.data(), frame.size(), &error), error);
	decoder.toRGBA(17, 17, rgba.data(), &alpha);
	CHECK(rgba[3] == 128);
	CHECK(rgba.back() == 128);
}

TEST_CASE("coverage1: VP6 synthetic signed DC residuals flip rows and distinguish luma blocks")
{
	const auto frame = keyFrame(1, 1, 8, 0, false, steppedCoefficients());
	VP6Decoder decoder;
	std::string error;
	REQUIRE_MESSAGE(decoder.decodeFrame(frame.data(), frame.size(), &error), error);
	std::vector<std::uint8_t> y, u, v;
	decoder.copyPlanes(y, u, v);
	REQUIRE(y.size() == 256);
	CHECK(decoder.lumaAt(0, 0) == 128); // bitstream bottom-left, displayed at top
	CHECK(decoder.lumaAt(15, 0) == 128);
	CHECK(decoder.lumaAt(0, 15) == 134); // bitstream top-left, predicted DC 1 x 188
	CHECK(decoder.lumaAt(15, 15) == 140); // predicted DC 2 x 188
	CHECK(y[15 * 16] == decoder.lumaAt(0, 15));
	CHECK(std::all_of(u.begin(), u.end(), [](std::uint8_t p) { return p == 255; }));
	CHECK(u == v);
	std::vector<std::uint8_t> rgba(16 * 16 * 4);
	decoder.toRGBA(16, 16, rgba.data());
	CHECK(rgba[3] == 255);
	CHECK(rgba[(15 * 16) * 4 + 3] == 255);
}

TEST_CASE("coverage1: VP6 synthetic inter frames retain previous and golden pictures")
{
	VP6Decoder decoder;
	std::string error;
	const auto key = keyFrame();
	const auto inter = interFrame();
	const auto golden = interFrame(63, true);
	REQUIRE_MESSAGE(decoder.decodeFrame(key.data(), key.size(), &error), error);
	std::vector<std::uint8_t> y, u, v;
	decoder.copyPlanes(y, u, v);
	for (const auto &frame : { inter, golden, inter })
	{
		REQUIRE_MESSAGE(decoder.decodeFrame(frame.data(), frame.size(), &error), error);
		CHECK(decoder.hasFrame());
		CHECK_FALSE(decoder.lastWasKey());
		std::vector<std::uint8_t> yy, uu, vv;
		decoder.copyPlanes(yy, uu, vv);
		CHECK(yy == y);
		CHECK(uu == u);
		CHECK(vv == v);
	}
}

TEST_CASE("coverage1: VP6 synthetic Huffman EOBs decode a complete macroblock")
{
	const auto frame = keyFrame(1, 1, 8, 63, true, huffmanEobs());
	VP6Decoder decoder;
	std::string error;
	REQUIRE_MESSAGE(decoder.decodeFrame(frame.data(), frame.size(), &error), error);
	CHECK(decoder.hasFrame());
	CHECK(decoder.lumaAt(2, 3) == 128);
	std::vector<std::uint8_t> y, u, v;
	decoder.copyPlanes(y, u, v);
	CHECK(u == std::vector<std::uint8_t>(64, 160));
	CHECK(v == u);
}

TEST_CASE("coverage1: VP6 synthetic Huffman zero runs propagate across luma and chroma blocks")
{
	const auto frame = keyFrame(1, 1, 8, 63, true, huffmanNullRuns());
	VP6Decoder decoder;
	std::string error;
	REQUIRE_MESSAGE(decoder.decodeFrame(frame.data(), frame.size(), &error), error);
	std::vector<std::uint8_t> y, u, v;
	decoder.copyPlanes(y, u, v);
	CHECK(y == std::vector<std::uint8_t>(256, 128));
	CHECK(u == std::vector<std::uint8_t>(64, 160));
	CHECK(v == u);
}

TEST_CASE("coverage1: VP6 synthetic filter headers accept all three modes and absent filters")
{
	std::string error;
	VP6Decoder decoder;
	for (int mode : { 0, 1, 2 })
	{
		const auto frame = keyFrame(1, 1, 8, 63, false, std::vector<std::uint8_t>(96, 0), mode);
		REQUIRE_MESSAGE(decoder.decodeFrame(frame.data(), frame.size(), &error), mode << ": " << error);
		CHECK(decoder.codedWidth() == 16);
		CHECK(decoder.lumaAt(0, 0) == 128);
	}
	const auto withoutFilters = keyFrame(1, 1, 6, 63, false, std::vector<std::uint8_t>(96, 0), 0, false);
	REQUIRE_MESSAGE(decoder.decodeFrame(withoutFilters.data(), withoutFilters.size(), &error), error);
	CHECK(decoder.hasFrame());
	CHECK(decoder.lumaAt(15, 15) == 128);
}

TEST_CASE("coverage1: VP6 synthetic new key size and older sub-version replace references")
{
	VP6Decoder decoder;
	std::string error;
	const auto small = keyFrame(1, 1, 8, 63);
	const auto wide = keyFrame(2, 1, 6, 63);
	REQUIRE_MESSAGE(decoder.decodeFrame(small.data(), small.size(), &error), error);
	REQUIRE_MESSAGE(decoder.decodeFrame(wide.data(), wide.size(), &error), error);
	CHECK(decoder.codedWidth() == 32);
	CHECK(decoder.codedHeight() == 16);
	CHECK(decoder.lastWasKey());
	std::vector<std::uint8_t> y, u, v;
	decoder.copyPlanes(y, u, v);
	CHECK(y == std::vector<std::uint8_t>(512, 128));
	CHECK(u == std::vector<std::uint8_t>(128, 160));
	CHECK(decoder.lumaAt(31, 15) == 128);
	REQUIRE_MESSAGE(decoder.decodeFrame(small.data(), small.size(), &error), error);
	CHECK(decoder.codedWidth() == 16);
	CHECK(decoder.codedHeight() == 16);
}

TEST_CASE("coverage1: VP6 header rejects short, interlaced, non-VP6, and zero-sized pictures")
{
	VP6Decoder decoder;
	std::string error;
	const std::uint8_t shortFrame[] = { 0, 0 };
	CHECK_FALSE(decoder.decodeFrame(shortFrame, 2, &error));
	CHECK(error == "VP6 frame of 2 bytes");
	const auto inter = interFrame();
	CHECK_FALSE(decoder.decodeFrame(inter.data(), inter.size(), &error));
	CHECK(error == "VP6 inter frame before any key frame");
	const std::uint8_t invalidSub[] = { 0, 9 << 3, 0, 0 };
	CHECK_FALSE(decoder.decodeFrame(invalidSub, sizeof invalidSub, &error));
	CHECK(error == "VP6 sub-version 9 is not VP6");
	const std::uint8_t interlaced[] = { 0, (8 << 3) | 3, 0, 0 };
	CHECK_FALSE(decoder.decodeFrame(interlaced, sizeof interlaced, &error));
	CHECK(error == "VP6 interlaced frames are not supported (no retail movie uses them)");
	auto zeroRows = keyFrame();
	zeroRows[4] = 0;
	CHECK_FALSE(decoder.decodeFrame(zeroRows.data(), zeroRows.size(), &error));
	CHECK(error == "VP6 key frame of size 0");
	auto zeroCols = keyFrame();
	zeroCols[5] = 0;
	CHECK_FALSE(decoder.decodeFrame(zeroCols.data(), zeroCols.size(), &error));
	CHECK(error == "VP6 key frame of size 0");
	CHECK_FALSE(decoder.hasFrame());
	CHECK(decoder.codedWidth() == 0);
}

TEST_CASE("coverage1: VP6 bad coefficient partitions are errors, never pictures")
{
	std::string error;
	VP6Decoder decoder;
	const auto valid = keyFrame();
	auto outside = valid;
	outside[2] = 2;
	outside[3] = 0; // declared offset 512, beyond actual packet
	CHECK_FALSE(decoder.decodeFrame(outside.data(), outside.size(), &error));
	CHECK(error == "VP6 coefficient partition outside the frame");
	auto empty = keyFrame(1, 1, 8, 63, false, {});
	CHECK_FALSE(decoder.decodeFrame(empty.data(), empty.size(), &error));
	CHECK(error == "VP6 coefficient partition empty");
	auto huffmanEmpty = keyFrame(1, 1, 8, 63, true, {});
	CHECK_FALSE(decoder.decodeFrame(huffmanEmpty.data(), huffmanEmpty.size(), &error));
	CHECK(error == "VP6 coefficients damaged at macroblock 0, 0");
	CHECK_FALSE(decoder.hasFrame());
}

TEST_CASE("coverage1: VP6 in-memory EA container indexes synthetic video and alpha chunks")
{
	auto file = movie();
	std::vector<std::uint8_t> alphaHeader(24, 0);
	std::copy_n("vp61", 4, alphaHeader.begin());
	chunk(file, "AVhd", alphaHeader);
	chunk(file, "AVP6", {});
	chunk(file, "AV0K", { 9, 10, 11 });
	chunk(file, "AV0F", { 12 });
	EAVP6Movie parsed;
	std::string error;
	REQUIRE_MESSAGE(EAVP6Movie::parse(file.data(), file.size(), parsed, &error), error);
	CHECK(parsed.codec == "vp60");
	CHECK(parsed.width == 16);
	CHECK(parsed.height == 16);
	CHECK(parsed.frames == 2);
	CHECK(parsed.largestChunk == 6);
	CHECK(parsed.durationMs() == 80.0);
	CHECK(parsed.hasAlpha);
	REQUIRE(parsed.video.size() == 2);
	REQUIRE(parsed.alpha.size() == 2);
	CHECK(parsed.video[0].offset == 40);
	CHECK(parsed.video[0].size == 6);
	CHECK(parsed.video[0].key);
	CHECK(parsed.video[1].offset == 54);
	CHECK(parsed.video[1].size == 2);
	CHECK_FALSE(parsed.video[1].key);
	CHECK(parsed.alpha[0].size == 3);
	CHECK(parsed.alpha[0].key);
	CHECK(parsed.alpha[1].size == 1);
	CHECK_FALSE(parsed.alpha[1].key);
}

TEST_CASE("coverage1: VP6 container rejects invalid length, missing header, size, rate, and video")
{
	std::string error;
	EAVP6Movie parsed;
	auto file = movie();
	file[4] = 7; // a chunk smaller than its own header
	CHECK_FALSE(EAVP6Movie::parse(file.data(), file.size(), parsed, &error));
	CHECK(error == "chunk MVhd at 0 has the size 7 (file 56 bytes)");
	file = movie();
	file[4] = 31;
	CHECK_FALSE(EAVP6Movie::parse(file.data(), file.size(), parsed, &error));
	CHECK(error == "MVhd is too short");
	file = movie();
	file[4] = 63; // the header claims bytes beyond EOF
	CHECK_FALSE(EAVP6Movie::parse(file.data(), file.size(), parsed, &error));
	CHECK(error == "chunk MVhd at 0 has the size 63 (file 56 bytes)");
	file = movie();
	std::copy_n("AVP6", 4, file.begin());
	CHECK_FALSE(EAVP6Movie::parse(file.data(), file.size(), parsed, &error));
	CHECK(error == "no MVhd chunk");
	file = movie();
	file[12] = 0;
	CHECK_FALSE(EAVP6Movie::parse(file.data(), file.size(), parsed, &error));
	CHECK(error == "the MVhd picture size 0 x 16 is outside 1 .. 4080");
	file = movie();
	file[24] = 0;
	CHECK_FALSE(EAVP6Movie::parse(file.data(), file.size(), parsed, &error));
	CHECK(error == "the MVhd frame rate is zero");
	file = movie();
	file.resize(32); // only MVhd
	CHECK_FALSE(EAVP6Movie::parse(file.data(), file.size(), parsed, &error));
	CHECK(error == "no video frames");
	file = movie();
	chunk(file, "NOPE", {});
	CHECK_FALSE(EAVP6Movie::parse(file.data(), file.size(), parsed, &error));
	CHECK(error == "unknown chunk 'NOPE' at 56");
}

TEST_CASE("coverage1: VP6 container codec must be a supported VP6 tag")
{
	EAVP6Movie parsed;
	std::string error;
	for (const std::string tag : { "vp60", "vp61", "VP60" })
	{
		const auto file = movie(tag);
		REQUIRE_MESSAGE(EAVP6Movie::parse(file.data(), file.size(), parsed, &error), tag << ": " << error);
		CHECK(parsed.codec == tag);
	}
	for (const std::string tag : { "v264", "vpxx", "VXXX" })
	{
		const auto file = movie(tag);
		CHECK_FALSE(EAVP6Movie::parse(file.data(), file.size(), parsed, &error));
		CHECK(error == "MVhd codec '" + tag + "' is not VP6");
	}
	auto alphaFile = movie();
	std::vector<std::uint8_t> alphaHeader(24, 0);
	std::copy_n("v264", 4, alphaHeader.begin());
	chunk(alphaFile, "AVhd", alphaHeader);
	CHECK_FALSE(EAVP6Movie::parse(alphaFile.data(), alphaFile.size(), parsed, &error));
	CHECK(error == "AVhd codec 'v264' is not VP6");
}

TEST_CASE("coverage1: VP6 container rejects non-chunk trailing bytes")
{
	EAVP6Movie parsed;
	std::string error;
	for (int tail = 1; tail != 8; ++tail)
	{
		auto file = movie();
		file.resize(file.size() + (size_t)tail, 0xaa);
		CHECK_FALSE(EAVP6Movie::parse(file.data(), file.size(), parsed, &error));
		CHECK(error == "trailing " + std::to_string(tail) + " bytes at 56");
	}
}
