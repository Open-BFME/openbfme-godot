// OpenBFME unit tests: lane CAMP-2, the movies' VP6 picture (GameClient/VP6Decoder.h). The expected values are FFmpeg's: the FNV-1a 64 hash of the
// yuv420p planes (coded size, display orientation) FFmpeg 7.1 decodes from the retail files (`ffmpeg -i <file> -map 0:<stream> -frames:v <n> -f rawvideo
// -pix_fmt yuv420p`), an external decoder of the same bitstream (stop S-2340: the binary's own decoder was not run). Retail files are read from
// ROTWK_INSTALL; without it the retail cases SKIP (a message). GPL-3.0.

#include "doctest.h"

#include "GameClient/VP6Decoder.h"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace
{
namespace stdfs = std::filesystem;

std::string lower(std::string s)
{
	for (char &c : s)
	{
		c = (char)std::tolower((unsigned char)c);
	}
	return s;
}

// <ROTWK_INSTALL>/Data/Movies (any case); "" without the install
std::string moviesDir()
{
	const char *root = std::getenv("ROTWK_INSTALL");
	if (!root)
	{
		return std::string();
	}
	std::error_code ec;
	for (const auto &d : stdfs::directory_iterator(root, ec))
	{
		if (lower(d.path().filename().string()) != "data")
		{
			continue;
		}
		for (const auto &m : stdfs::directory_iterator(d.path(), ec))
		{
			if (lower(m.path().filename().string()) == "movies")
			{
				return m.path().string();
			}
		}
	}
	return std::string();
}

std::string movieFile(const std::string &dir, const std::string &name)
{
	std::error_code ec;
	for (const auto &f : stdfs::directory_iterator(dir, ec))
	{
		if (lower(f.path().filename().string()) == lower(name))
		{
			return f.path().string();
		}
	}
	return std::string();
}

std::vector<std::uint8_t> readAll(const std::string &path)
{
	std::ifstream f(path, std::ios::binary);
	return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

void fnv(std::uint64_t &h, const std::vector<std::uint8_t> &bytes)
{
	for (std::uint8_t b : bytes)
	{
		h ^= b;
		h *= 0x100000001b3ULL;
	}
}

// decodes `frames` frames of the file's video (alpha = false) or alpha stream and hashes the planes as FFmpeg writes them
std::uint64_t decodeHash(const std::string &path, bool alpha, int frames, std::string &error, int &decoded)
{
	const std::vector<std::uint8_t> data = readAll(path);
	EAVP6Movie movie;
	decoded = 0;
	if (!EAVP6Movie::parse(data.data(), data.size(), movie, &error))
	{
		return 0;
	}
	const std::vector<EAVP6Chunk> &list = alpha ? movie.alpha : movie.video;
	VP6Decoder dec;
	std::uint64_t h = 0xcbf29ce484222325ULL;
	std::vector<std::uint8_t> y, u, v;
	for (int i = 0; i < frames && i < (int)list.size(); ++i)
	{
		if (!dec.decodeFrame(data.data() + list[(size_t)i].offset, list[(size_t)i].size, &error))
		{
			return 0;
		}
		dec.copyPlanes(y, u, v);
		fnv(h, y);
		fnv(h, u);
		fnv(h, v);
		decoded += 1;
	}
	return h;
}
} // namespace

TEST_CASE("camp2 VP6 container: EA's chunks are indexed, an unknown chunk and a truncated chunk are errors")
{
	auto chunk = [](const char *tag, std::uint32_t size, std::vector<std::uint8_t> &out) {
		out.insert(out.end(), tag, tag + 4);
		for (int i = 0; i < 4; ++i)
		{
			out.push_back((std::uint8_t)(size >> (8 * i)));
		}
		out.resize(out.size() + size - 8, 0);
	};
	std::vector<std::uint8_t> file;
	chunk("MVhd", 32, file);
	const char codec[] = "vp60";
	std::copy(codec, codec + 4, file.begin() + 8);
	file[12] = 64;  // width 64
	file[14] = 48;  // height 48
	file[16] = 2;   // 2 frames
	file[24] = 30;  // rate 30 / 1
	file[28] = 1;
	chunk("MV0K", 20, file);
	chunk("MV0F", 12, file);
	EAVP6Movie m;
	std::string error;
	REQUIRE_MESSAGE(EAVP6Movie::parse(file.data(), file.size(), m, &error), error);
	CHECK(m.codec == "vp60");
	CHECK(m.width == 64);
	CHECK(m.height == 48);
	CHECK(m.frames == 2);
	REQUIRE(m.video.size() == 2);
	CHECK(m.video[0].key);
	CHECK_FALSE(m.video[1].key);
	CHECK(m.video[0].offset == 32 + 8);
	CHECK(m.video[0].size == 12);
	CHECK(m.video[1].size == 4);
	CHECK(m.durationMs() == doctest::Approx(2000.0 / 30.0));

	std::vector<std::uint8_t> unknown = file;
	chunk("SCHl", 16, unknown);
	CHECK_FALSE(EAVP6Movie::parse(unknown.data(), unknown.size(), m, &error));
	CHECK(error.find("unknown chunk 'SCHl'") != std::string::npos);

	std::vector<std::uint8_t> truncated(file.begin(), file.end() - 2);
	CHECK_FALSE(EAVP6Movie::parse(truncated.data(), truncated.size(), m, &error));
	CHECK(error.find("MV0F") != std::string::npos);

	// a frame that is not VP6 is an error, not a black picture
	VP6Decoder d;
	const std::uint8_t garbage[] = { 0x00, 0xFF, 0x01, 0x00, 0x00, 0x00 }; // a key frame of sub-version 31
	CHECK_FALSE(d.decodeFrame(garbage, sizeof garbage, &error));
	CHECK(error.find("sub-version") != std::string::npos);
	const std::uint8_t inter[] = { 0x80, 0x00, 0x00, 0x00 };
	VP6Decoder fresh;
	CHECK_FALSE(fresh.decodeFrame(inter, sizeof inter, &error));
	CHECK(error.find("before any key frame") != std::string::npos);
}

TEST_CASE("camp2 VP6 container: a picture size the codec cannot have (0, or above 255 macroblocks of 16) is an error before any allocation")
{
	// MVhd with the given width / height, then one key frame chunk
	auto movie = [](std::uint32_t w, std::uint32_t h) {
		std::vector<std::uint8_t> f(32, 0);
		const char head[] = "MVhd";
		std::copy(head, head + 4, f.begin());
		f[4] = 32;
		const char codec[] = "vp60";
		std::copy(codec, codec + 4, f.begin() + 8);
		f[12] = (std::uint8_t)w;
		f[13] = (std::uint8_t)(w >> 8);
		f[14] = (std::uint8_t)h;
		f[15] = (std::uint8_t)(h >> 8);
		f[16] = 1;  // 1 frame
		f[24] = 30; // rate 30 / 1
		f[28] = 1;
		const std::uint8_t key[] = { 'M', 'V', '0', 'K', 12, 0, 0, 0, 0, 0, 0, 0 };
		f.insert(f.end(), key, key + sizeof key);
		return f;
	};
	CHECK(EAVP6Movie::kMaxDimension == 4080);
	struct Case
	{
		std::uint32_t w, h;
		bool ok;
	};
	const Case cases[] = { { 0, 400, false }, { 704, 0, false }, { 0, 0, false }, { 4081, 400, false }, { 704, 4081, false }, { 65535, 65535, false },
		{ 1, 1, true }, { 4080, 4080, true }, { 704, 400, true } };
	for (const Case &c : cases)
	{
		const std::vector<std::uint8_t> f = movie(c.w, c.h);
		EAVP6Movie m;
		std::string error;
		const bool ok = EAVP6Movie::parse(f.data(), f.size(), m, &error);
		INFO(c.w << " x " << c.h << ": " << error);
		CHECK(ok == c.ok);
		if (!c.ok)
		{
			CHECK(error.find("picture size") != std::string::npos);
		}
	}
	// a frame header can never code more than 255 x 255 macroblocks: the decoder's own size is within the same bound
	VP6Decoder d;
	std::string error;
	const std::uint8_t zeroSize[] = { 0x00, 0x06 << 3, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }; // a key frame of 0 x 0 macroblocks
	CHECK_FALSE(d.decodeFrame(zeroSize, sizeof zeroSize, &error));
	CHECK(error.find("size 0") != std::string::npos);
}

TEST_CASE("camp2 VP6 retail: the retail movies decode bit-exact to FFmpeg's pictures (vp60, vp61, VP60 key-only, the alpha stream)")
{
	const std::string dir = moviesDir();
	if (dir.empty())
	{
		MESSAGE("SKIP camp2 VP6 retail: ROTWK_INSTALL is not set (or has no Data/Movies)");
		return;
	}
	struct Case
	{
		const char *file;
		bool alpha;
		int frames;
		std::uint64_t fnv;
	};
	// FFmpeg 7.1 (see the file comment)
	const Case cases[] = {
		{ "LoadingRing.vp6", false, 140, 0xb148a2f4503ed877ULL },          // the loading ring, whole (an AVhd alpha movie)
		{ "LoadingRing.vp6", true, 140, 0xed58255355326c13ULL },           // and its alpha stream
		{ "MovieSkirmishGondor.vp6", false, 320, 0xe1b8f39169ecd76aULL },  // vp61, 110 x 100 (coded 112 x 112), whole
		{ "Mission_Rhudaur_Intro.vp6", false, 60, 0x8132991e1fc16491ULL }, // "VP60": key frames only
		{ "BAM4O.vp6", false, 300, 0x08546c9d229b3856ULL },                // Angmar_Campaign_M4Open (MAP ANG Amon Sul's intro)
		{ "SmallRing.vp6", false, 140, 0xd56ed0644b3849e1ULL },            // a "_with_alpha"-style movie: the colour stream
		{ "SmallRing.vp6", true, 140, 0x1a49176390427e15ULL },             // and its AVhd alpha stream
	};
	for (const Case &c : cases)
	{
		const std::string path = movieFile(dir, c.file);
		REQUIRE_MESSAGE(!path.empty(), (std::string(c.file) + " is not in " + dir));
		std::string error;
		int decoded = 0;
		const std::uint64_t h = decodeHash(path, c.alpha, c.frames, error, decoded);
		INFO(c.file << (c.alpha ? " (alpha)" : "") << ": " << error);
		CHECK(error.empty());
		CHECK(decoded == c.frames);
		CHECK(h == c.fnv);
	}
}

TEST_CASE("camp2 VP6 retail: every movie of Data/Movies is a container this decoder reads, with the header's frame count")
{
	const std::string dir = moviesDir();
	if (dir.empty())
	{
		MESSAGE("SKIP camp2 VP6 retail movies: ROTWK_INSTALL is not set (or has no Data/Movies)");
		return;
	}
	int files = 0;
	std::error_code ec;
	for (const auto &f : stdfs::directory_iterator(dir, ec))
	{
		if (lower(f.path().extension().string()) != ".vp6")
		{
			continue;
		}
		files += 1;
		EAVP6Movie m;
		std::string error;
		const bool ok = EAVP6Movie::parseFile(f.path().string(), m, &error);
		INFO(f.path().filename().string() << ": " << error);
		CHECK(ok);
		CHECK(m.video.size() == m.frames);
		CHECK((m.alpha.empty() || m.alpha.size() == m.video.size()));
		CHECK(m.video.front().key);
		// the first frame decodes (the whole movies are checked against FFmpeg by the case above and the lane's report)
		std::ifstream in(f.path(), std::ios::binary);
		std::vector<std::uint8_t> first(m.video.front().size);
		in.seekg(m.video.front().offset);
		in.read((char *)first.data(), (std::streamsize)first.size());
		VP6Decoder d;
		CHECK(d.decodeFrame(first.data(), first.size(), &error));
		CHECK(d.codedWidth() >= (int)m.width);
		CHECK(d.codedHeight() >= (int)m.height);
	}
	CHECK(files > 0);
	MESSAGE(files << " movies in " << dir << " (a complete RotWK 2.01 install has 81)");
}
