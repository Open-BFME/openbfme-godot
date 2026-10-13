// OpenBFME. GPL-3.0.
// See Common/System/SaveGame/SaveGameFile.h (lane SAVE-2).

#include "Common/System/SaveGame/SaveGameFile.h"

#include "Common/Sha256.h"
#include "GameNetwork/NetPacket.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <new>
#include <sstream>

namespace
{
const char kMagic[8] = { 'O', 'B', 'F', 'M', 'E', 'S', 'A', 'V' };
const char *const kPartTags[3] = { "HEAD", "LOG ", "SNAP" };
const char *const kPartNames[3] = { "header", "command log", "snapshot" };

bool failWith(std::string *error, const std::string &why)
{
	if (error)
	{
		*error = why;
	}
	return false;
}

void putPart(std::vector<std::uint8_t> &out, int index, const std::vector<std::uint8_t> &payload)
{
	out.insert(out.end(), kPartTags[index], kPartTags[index] + 4);
	NetByteWriter w;
	w.u32((std::uint32_t)payload.size());
	out.insert(out.end(), w.data().begin(), w.data().end());
	const std::array<std::uint8_t, 32> d = Sha256::hash(payload.data(), payload.size());
	out.insert(out.end(), d.begin(), d.end());
	out.insert(out.end(), payload.begin(), payload.end());
}

std::string withoutProfileLine(const std::string &text)
{
	std::istringstream in(text);
	std::string line, out;
	while (std::getline(in, line))
	{
		if (line.rfind("profile ", 0) != 0)
		{
			out += line + "\n";
		}
	}
	return out;
}

std::string shortId(const std::string &id)
{
	return id.size() > 12 ? id.substr(0, 12) : id;
}
} // namespace

std::string SaveGameFile::executionProfileOf(const std::string &identityText)
{
	std::istringstream in(identityText);
	std::string line;
	while (std::getline(in, line))
	{
		if (line.rfind("profile ", 0) == 0)
		{
			return line.substr(8);
		}
	}
	return std::string();
}

std::vector<std::uint8_t> SaveGameFile::encode() const
{
	NetByteWriter h;
	h.u32(kHeaderVersion);
	h.str(header.buildVersion);
	h.str(header.engineId);
	h.str(header.commit);
	h.str(header.profile.text);
	h.str(header.executionProfile);
	h.u32((std::uint32_t)header.gameKind);
	h.u32(header.frame);
	h.u32(header.stateHash);
	h.str(header.description);
	h.str(header.mapPath);
	h.str(header.mapDisplay);
	h.str(header.date);
	h.str(header.time);
	h.u32(header.commands);
	h.str(header.campaign);
	h.i32(header.campaignMission);
	h.i32(header.campaignDifficulty);
	std::vector<std::uint8_t> out(kMagic, kMagic + sizeof(kMagic));
	NetByteWriter v;
	v.u32(kContainerVersion);
	out.insert(out.end(), v.data().begin(), v.data().end());
	putPart(out, 0, h.data());
	putPart(out, 1, log);
	putPart(out, 2, snapshot);
	return out;
}

// where a container is read from: memory, or a file read part by part (nothing is allocated before its declared size is checked against the real size
// and the part's limit)
class SaveGameFile::Source
{
public:
	virtual ~Source() = default;
	virtual std::uint64_t size() const = 0;
	virtual bool read(std::uint64_t offset, void *dst, size_t n) = 0;
};

namespace
{
class MemorySource : public SaveGameFile::Source
{
public:
	explicit MemorySource(const std::vector<std::uint8_t> &b) : m_b(b) {}
	std::uint64_t size() const override { return m_b.size(); }
	bool read(std::uint64_t offset, void *dst, size_t n) override
	{
		if (offset > m_b.size() || m_b.size() - offset < n)
		{
			return false;
		}
		if (n)
		{
			std::memcpy(dst, m_b.data() + offset, n);
		}
		return true;
	}

private:
	const std::vector<std::uint8_t> &m_b;
};

class FileSource : public SaveGameFile::Source
{
public:
	FileSource(std::ifstream &in, std::uint64_t size) : m_in(in), m_size(size) {}
	std::uint64_t size() const override { return m_size; }
	bool read(std::uint64_t offset, void *dst, size_t n) override
	{
		m_in.clear();
		m_in.seekg((std::streamoff)offset);
		m_in.read((char *)dst, (std::streamsize)n);
		return (bool)m_in && (size_t)m_in.gcount() == n;
	}

private:
	std::ifstream &m_in;
	std::uint64_t m_size;
};
} // namespace

bool SaveGameFile::parse(const std::vector<std::uint8_t> &bytes, SaveGameFile &out, std::string *error, bool headerOnly)
{
	MemorySource src(bytes);
	return parseFrom(src, out, error, headerOnly);
}

bool SaveGameFile::parseFrom(Source &src, SaveGameFile &out, std::string *error, bool headerOnly)
{
	out = SaveGameFile();
	const std::uint64_t size = src.size();
	std::uint8_t lead[12];
	if (size < sizeof(kMagic) || !src.read(0, lead, sizeof(kMagic)) || std::memcmp(lead, kMagic, sizeof(kMagic)) != 0)
	{
		return failWith(error, "not an OpenBFME save (the file does not start with OBFMESAV)");
	}
	if (size < 12 || !src.read(8, lead + 8, 4))
	{
		return failWith(error, "the save is truncated (no container version)");
	}
	const std::uint32_t version = NetByteReader(lead + 8, 4).u32();
	if (version != kContainerVersion)
	{
		return failWith(error, "save container version " + std::to_string(version) + " (this build reads version " + std::to_string(kContainerVersion) + ")");
	}
	const std::uint64_t limits[3] = { kMaxHeaderBytes, kMaxLogBytes, 0 };
	std::vector<std::uint8_t> parts[3];
	std::uint64_t pos = 12;
	for (int i = 0; i < 3; ++i)
	{
		std::uint8_t ph[40];
		if (size - pos < 40 || !src.read(pos, ph, 40))
		{
			return failWith(error, std::string("the save is truncated (the ") + kPartNames[i] + " part is missing)");
		}
		if (std::memcmp(ph, kPartTags[i], 4) != 0)
		{
			return failWith(error, std::string("the save is damaged (expected the ") + kPartNames[i] + " part '" + kPartTags[i] + "', found '"
				+ std::string((const char *)ph, 4) + "')");
		}
		const std::uint32_t length = NetByteReader(ph + 4, 4).u32();
		pos += 40;
		if (size - pos < length)
		{
			return failWith(error, std::string("the save is truncated (the ") + kPartNames[i] + " part has " + std::to_string(size - pos) + " of its "
				+ std::to_string(length) + " bytes)");
		}
		if (i == 2 && length > 0 && !headerOnly)
		{
			return failWith(error, "the save carries a snapshot (" + std::to_string(length) + " bytes), which this build cannot read");
		}
		if (i < 2 && (i == 0 || !headerOnly))
		{
			if (length > limits[i])
			{
				return failWith(error, std::string("the save's ") + kPartNames[i] + " part declares " + std::to_string(length) + " bytes (at most "
					+ std::to_string(limits[i]) + ")");
			}
			parts[i].resize(length);
			if (!src.read(pos, parts[i].data(), length))
			{
				return failWith(error, std::string("the save could not be read (the ") + kPartNames[i] + " part)");
			}
			const std::array<std::uint8_t, 32> d = Sha256::hash(parts[i].data(), parts[i].size());
			if (std::memcmp(d.data(), ph + 8, 32) != 0)
			{
				return failWith(error, std::string("the save is damaged (the ") + kPartNames[i] + " part's SHA-256 does not match its content)");
			}
		}
		pos += length;
	}
	if (pos != size)
	{
		return failWith(error, "the save is damaged (" + std::to_string(size - pos) + " bytes after its last part)");
	}
	NetByteReader r(parts[0]);
	const std::uint32_t headerVersion = r.u32();
	if (!r.failed() && headerVersion != kHeaderVersion)
	{
		return failWith(error, "save header version " + std::to_string(headerVersion) + " (this build reads version " + std::to_string(kHeaderVersion) + ")");
	}
	SaveGameHeader &h = out.header;
	h.buildVersion = r.str();
	h.engineId = r.str();
	h.commit = r.str();
	h.profile = ProfileIdentity::fromText(r.str());
	h.executionProfile = r.str();
	h.gameKind = (int)r.u32();
	h.frame = r.u32();
	h.stateHash = r.u32();
	h.description = r.str();
	h.mapPath = r.str();
	h.mapDisplay = r.str();
	h.date = r.str();
	h.time = r.str();
	h.commands = r.u32();
	h.campaign = r.str();
	h.campaignMission = r.i32();
	h.campaignDifficulty = r.i32();
	if (r.failed())
	{
		return failWith(error, "the save is damaged (its header part ends early)");
	}
	if (!r.atEnd())
	{
		return failWith(error, "the save is damaged (" + std::to_string(r.remaining()) + " bytes after its header's fields)");
	}
	if (h.gameKind != 1 && h.gameKind != 2)
	{
		return failWith(error, "the save's game kind " + std::to_string(h.gameKind) + " is not one this build saves (1 Campaign, 2 Skirmish)");
	}
	if ((h.gameKind == 1) != !h.campaign.empty())
	{
		return failWith(error, h.gameKind == 1 ? std::string("the save is a campaign mission without its campaign") : "the save is a skirmish that names a campaign");
	}
	out.log = std::move(parts[1]);
	return true;
}

bool SaveGameFile::load(const std::string &path, SaveGameFile &out, std::string *error, bool headerOnly)
{
	// read part by part: the header-only listing reads the header part and the parts' 40-byte heads, nothing else; every size is checked against the
	// file's before anything is allocated (review: a large foreign file read whole aborted the listing with std::bad_alloc)
	std::error_code ec;
	const std::uintmax_t size = std::filesystem::file_size(std::filesystem::u8path(path), ec);
	std::ifstream in(std::filesystem::u8path(path), std::ios::binary);
	if (ec || !in)
	{
		return failWith(error, "cannot open the save " + path);
	}
	try
	{
		FileSource src(in, (std::uint64_t)size);
		return parseFrom(src, out, error, headerOnly);
	}
	catch (const std::exception &e)
	{
		out = SaveGameFile();
		return failWith(error, std::string("the save could not be read (") + e.what() + ")");
	}
}

bool SaveGameFile::write(const std::string &path, std::string *error) const
{
	const std::vector<std::uint8_t> bytes = encode();
	const std::filesystem::path target = std::filesystem::u8path(path);
	const std::filesystem::path temp = std::filesystem::u8path(path + ".tmp");
	{
		std::ofstream o(temp, std::ios::binary | std::ios::trunc);
		if (!o)
		{
			return failWith(error, "cannot write the save " + path + ".tmp");
		}
		o.write((const char *)bytes.data(), (std::streamsize)bytes.size());
		o.close();
		if (o.fail())
		{
			std::error_code ec;
			std::filesystem::remove(temp, ec);
			return failWith(error, "writing the save " + path + " failed (disk full?)");
		}
	}
	std::error_code ec;
	std::filesystem::rename(temp, target, ec);
	if (ec)
	{
		std::filesystem::remove(temp, ec);
		return failWith(error, "cannot replace the save " + path + ": " + ec.message());
	}
	return true;
}

SaveGameCompatibility SaveGameCompatibility::check(const SaveGameHeader &save, const ProfileIdentity &running, const std::string &runningEngineId,
	const std::string &runningBuildVersion, bool multiplayer)
{
	SaveGameCompatibility c;
	if (save.engineId != runningEngineId)
	{
		// owner decision 2: a log-only save is refused by another build, naming the build that made it (SAVES.md section 5: replay with that build)
		c.refusal = "this game was saved by OpenBFME " + save.buildVersion + " (engine " + shortId(save.engineId) + "); this is OpenBFME "
			+ runningBuildVersion + " (engine " + shortId(runningEngineId) + "). A save loads only in the build that made it.";
		return c;
	}
	const ProfileIdentity a = ProfileIdentity::fromText(withoutProfileLine(running.text));
	const ProfileIdentity b = ProfileIdentity::fromText(withoutProfileLine(save.profile.text));
	if (a != b)
	{
		c.refusal = "this game was saved with other game data or rules: " + ProfileIdentity::difference(a, b);
		return c;
	}
	const std::string ours = SaveGameFile::executionProfileOf(running.text);
	const std::string theirs = save.executionProfile;
	if (ours != theirs)
	{
		if (multiplayer)
		{
			c.refusal = "this game was saved under the " + theirs + " execution profile; a multiplayer game needs the same profile on every player (this one runs "
				+ ours + ")";
			return c;
		}
		// owner decision 7: in single player a save of one profile loads under the other (SAVES.md 5): its log is replayed under the profiles it ran
		// (every hash compared), then the game goes on under this player's profile from the saved frame
		c.crossProfile = true;
		c.note = "saved under the " + theirs + " profile: the saved game is replayed under it, then goes on under " + ours + " (this player's profile)";
	}
	c.loadable = true;
	return c;
}
