// OpenBFME. GPL-3.0.
// See GameClient/VideoPlayer.h.

#include "GameClient/VideoPlayer.h"

#include "Common/AsciiString.h"
#include "Common/INI.h"

#include <cstddef>
#include <filesystem>
#include <fstream>

namespace stdfs = std::filesystem;

namespace
{
// RW 0xCFD2E8
const FieldParse kVideoParse[] = {
	{ "Filename", INI::parseAsciiString, nullptr, (int)offsetof(Video, filename) },
	{ "Comment", INI::parseAsciiString, nullptr, (int)offsetof(Video, comment) },
	{ "HasSubtitles", INI::parseBool, nullptr, (int)offsetof(Video, hasSubtitles) },
	{ "Volume", INI::parsePercentToReal, nullptr, (int)offsetof(Video, volume) },
	{ "IsDefault", INI::parseBool, nullptr, (int)offsetof(Video, isDefault) },
	{ nullptr, nullptr, nullptr, 0 }
};

// one path component of `dir` matching `name` without regard to case (the retail file system is Windows'); "" when none
std::string findNoCase(const stdfs::path &dir, const std::string &name)
{
	std::error_code ec;
	if (stdfs::exists(dir / name, ec))
	{
		return name;
	}
	for (stdfs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
	{
		const std::string n = it->path().filename().string();
		if (AsciiStringUtil::compareNoCase(n, name) == 0)
		{
			return n;
		}
	}
	return std::string();
}

// `relative` ('/' or '\' separated) below `root`, each component matched without regard to case; "" when a component is missing
std::string resolveNoCase(const std::string &root, const std::string &relative)
{
	stdfs::path at(root);
	std::string out;
	std::string part;
	auto step = [&](const std::string &comp) {
		if (comp.empty())
		{
			return true;
		}
		const std::string found = findNoCase(at, comp);
		if (found.empty())
		{
			return false;
		}
		at /= found;
		out += (out.empty() ? "" : "/") + found;
		return true;
	};
	for (char c : relative)
	{
		if (c == '/' || c == '\\')
		{
			if (!step(part))
			{
				return std::string();
			}
			part.clear();
		}
		else
		{
			part += c;
		}
	}
	if (!step(part))
	{
		return std::string();
	}
	std::error_code ec;
	return stdfs::is_regular_file(at, ec) ? out : std::string();
}

std::uint32_t le32(const std::uint8_t *p)
{
	return (std::uint32_t)p[0] | ((std::uint32_t)p[1] << 8) | ((std::uint32_t)p[2] << 16) | ((std::uint32_t)p[3] << 24);
}
} // namespace

const std::vector<std::string> &VideoPlayer::loadOrder()
{
	static const std::vector<std::string> files = { "Data\\INI\\Default\\Video.ini", "Data\\INI\\Video.ini" };
	return files;
}

void VideoPlayer::registerBlock(INIBlockRegistry &registry)
{
	registry.registerBlock("Video", [this](INI *ini) { parseVideoDefinition(ini); });
}

void VideoPlayer::parseVideoDefinition(INI *ini)
{
	Video v;
	v.internalName = ini->getNextToken();
	ini->initFromINI(&v, kVideoParse);
	addVideo(v);
}

void VideoPlayer::addVideo(const Video &v)
{
	for (Video &have : m_videos)
	{
		if (have.internalName == v.internalName) // donor RW 0x689FD0: an exact compare
		{
			have = v;
			return;
		}
	}
	m_videos.push_back(v);
}

const Video *VideoPlayer::getVideo(const std::string &title) const
{
	for (const Video &v : m_videos)
	{
		if (AsciiStringUtil::compareNoCase(v.internalName, title) == 0) // donor RW 0x689B10
		{
			return &v;
		}
	}
	return nullptr;
}

VideoStreamInfo VideoPlayer::locate(const std::string &title, const std::string &installRoot, const std::string &modRoot, const std::string &language) const
{
	VideoStreamInfo info;
	info.title = title;
	const Video *v = getVideo(title);
	if (!v)
	{
		info.error = "A movie named '" + title + "' was requested but can't be found (no Video block of that name)"; // RW 0xBDDF3C
		return info;
	}
	// RW 0x490D08's three directories in order, "%s%s.%s" (RW 0xBDDF78)
	struct Dir
	{
		std::string root, relative;
	};
	std::vector<Dir> dirs;
	if (!modRoot.empty())
	{
		dirs.push_back({ modRoot, "Data\\Movies\\" });
	}
	if (!language.empty())
	{
		dirs.push_back({ installRoot, "Lang/" + language + "/Data/Movies/" });
	}
	dirs.push_back({ installRoot, "Data\\Movies\\" });
	std::string tried;
	for (const Dir &d : dirs)
	{
		const std::string rel = d.relative + v->filename + ".vp6";
		const std::string found = resolveNoCase(d.root, rel);
		tried += (tried.empty() ? "" : ", ") + rel;
		if (found.empty())
		{
			continue;
		}
		const std::string full = (stdfs::path(d.root) / found).string();
		std::ifstream f(full, std::ios::binary);
		std::vector<std::uint8_t> head(32);
		f.read((char *)head.data(), (std::streamsize)head.size());
		head.resize((size_t)f.gcount());
		std::string err;
		if (!readHeader(head.data(), head.size(), info, &err))
		{
			info.error = "Could not open VP6 video file for " + title + " - " + full + ": " + err; // RW 0xBDDF4C
			return info;
		}
		info.path = found;
		info.ok = true;
		return info;
	}
	info.error = "Could not open VP6 video file for " + title + " - " + v->filename + " (tried " + tried + ")"; // RW 0xBDDF4C
	return info;
}

bool VideoPlayer::readHeader(const std::uint8_t *data, size_t size, VideoStreamInfo &out, std::string *error)
{
	// "MVhd", the chunk size, "vp60", u16 width, u16 height, u32 frames, u32 largest chunk, u32 rate numerator, u32 rate denominator
	if (size < 32 || data[0] != 'M' || data[1] != 'V' || data[2] != 'h' || data[3] != 'd')
	{
		*error = "not an EA VP6 file (no MVhd chunk)";
		return false;
	}
	if (data[8] != 'v' || data[9] != 'p' || data[10] != '6')
	{
		*error = "the MVhd codec is not vp6";
		return false;
	}
	out.width = (std::uint32_t)data[12] | ((std::uint32_t)data[13] << 8);
	out.height = (std::uint32_t)data[14] | ((std::uint32_t)data[15] << 8);
	out.frames = le32(data + 16);
	out.rateNumerator = le32(data + 24);
	out.rateDenominator = le32(data + 28);
	if (out.rateNumerator == 0 || out.rateDenominator == 0)
	{
		*error = "the MVhd frame rate is zero";
		return false;
	}
	out.durationMs = (double)out.frames * 1000.0 * (double)out.rateDenominator / (double)out.rateNumerator;
	return true;
}

std::vector<std::string> VideoPlayer::stopLines()
{
	return {
		"[S-1710] movies: the VP6 picture is not decoded (a movie shows black for its length, its audio events play as retail's stream does, RW "
		"0x49112C); the movie subtitles (HasSubtitles) and TheGlobalData + 0x9AD are not ported",
	};
}
