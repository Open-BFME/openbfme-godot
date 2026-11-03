// OpenBFME. GPL-3.0.
// See Common/System/SaveGame/SaveGameFile.h (lane SAVE-2).

#include "Common/System/SaveGame/SaveGameFile.h"
#include "Common/PathIO.h"

#include "Common/RecordingBuild.h"
#include "Common/Sha256.h"
#include "GameLogic/CampaignCarryover.h"
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
// container version 2 (lane WOTR2 milestone 3) adds the fourth part, the War of the Ring strategic game
const char *const kPartTags[4] = { "HEAD", "LOG ", "SNAP", "LWLD" };
const char *const kPartNames[4] = { "header", "command log", "snapshot", "War of the Ring" };

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

// lane CAMPSAVE-1 (review): the identity without its execution profile and engine id lines: what the data and the rules are, not which build
std::string dataOnly(const std::string &text)
{
	std::istringstream in(text);
	std::string line, out;
	while (std::getline(in, line))
	{
		if (line.rfind("profile ", 0) != 0 && line.rfind("engine ", 0) != 0)
		{
			out += line + "\n";
		}
	}
	return out;
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
	// header version 2 (lane CAMPSAVE-1): the save point and the campaign's flags
	h.u32((std::uint32_t)header.savePoint);
	h.u8(header.campaignVictorious ? 1 : 0);
	h.u8(header.campaignReady ? 1 : 0);
	h.u8(header.campaignAdvanced ? 1 : 0);
	h.u8(header.autoSave ? 1 : 0);
	h.str(header.buildDate);
	h.str(header.campaignCarryover); // header version 4 (lane BFME2CAMP1)
	std::vector<std::uint8_t> out(kMagic, kMagic + sizeof(kMagic));
	NetByteWriter v;
	v.u32(kContainerVersion);
	out.insert(out.end(), v.data().begin(), v.data().end());
	putPart(out, 0, h.data());
	putPart(out, 1, log);
	putPart(out, 2, snapshot);
	NetByteWriter lw;
	if (!livingWorld.empty() || !livingWorldSession.empty())
	{
		lw.bytes(livingWorld);
		lw.bytes(livingWorldSession);
	}
	putPart(out, 3, lw.data());
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
	try
	{
		MemorySource src(bytes);
		SaveGameFile parsed;
		if (!parseFrom(src, parsed, error, headerOnly))
		{
			return false;
		}
		out = std::move(parsed);
		return true;
	}
	catch (const std::exception &e)
	{
		return failWith(error, std::string("the save could not be parsed (") + e.what() + ")");
	}
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
	if (version != kContainerVersion && version != 1)
	{
		return failWith(error, "save container version " + std::to_string(version) + " (this build reads versions 1 and " + std::to_string(kContainerVersion) + ")");
	}
	const int partCount = version == 1 ? 3 : 4; // version 1 has no War of the Ring part
	const std::uint64_t limits[4] = { kMaxHeaderBytes, kMaxLogBytes, kMaxSnapshotBytes, kMaxLivingWorldBytes };
	std::vector<std::uint8_t> parts[4];
	std::uint64_t pos = 12;
	for (int i = 0; i < partCount; ++i)
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
		if ((i == 0 || !headerOnly) && length > limits[i])
		{
			return failWith(error, std::string("the save's ") + kPartNames[i] + " part declares " + std::to_string(length) + " bytes (at most "
				+ std::to_string(limits[i]) + ")");
		}
		if (size - pos < length)
		{
			return failWith(error, std::string("the save is truncated (the ") + kPartNames[i] + " part has " + std::to_string(size - pos) + " of its "
				+ std::to_string(length) + " bytes)");
		}
		if (i == 0 || !headerOnly)
		{
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
	if (!r.failed() && (headerVersion < 1 || headerVersion > kHeaderVersion))
	{
		return failWith(error, "save header version " + std::to_string(headerVersion) + " (this build reads versions 1 to " + std::to_string(kHeaderVersion) + ")");
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
	int flags[4] = { 0, 0, 0, 0 };
	if (headerVersion >= 2)
	{
		// lane CAMPSAVE-1: closed sets, refused by name (a version 1 header is a game at its frame with no flags)
		const std::uint32_t point = r.u32();
		for (int &f : flags)
		{
			f = r.u8();
		}
		if (!r.failed() && point > 3)
		{
			return failWith(error, "the save's save point " + std::to_string(point) + " is not one this build saves (0 a game, 1 the campaign menu, 2 the War of the Ring map, "
				"3 a War of the Ring battle)");
		}
		h.savePoint = (int)point;
		for (int i = 0; i < 4 && !r.failed(); ++i)
		{
			if (flags[i] > 1)
			{
				return failWith(error, "the save's campaign flag " + std::to_string(i) + " is " + std::to_string(flags[i]) + " (0 or 1)");
			}
		}
	}
	if (headerVersion >= 3)
	{
		h.buildDate = r.str();
		if (!r.failed() && !RecordingBuild::validDate(h.buildDate))
		{
			return failWith(error, "invalid save recording build date");
		}
	}
	h.campaignVictorious = flags[0] == 1;
	h.campaignReady = flags[1] == 1;
	h.campaignAdvanced = flags[2] == 1;
	h.autoSave = flags[3] == 1;
	if (headerVersion >= 4)
	{
		h.campaignCarryover = r.str(); // lane BFME2CAMP1: checked below
	}
	if (r.failed())
	{
		return failWith(error, "the save is damaged (its header part ends early)");
	}
	if (!r.atEnd())
	{
		return failWith(error, "the save is damaged (" + std::to_string(r.remaining()) + " bytes after its header's fields)");
	}
	// WOTR2 milestone 8 (step 4, Sol wotr2m8s4 #1): 16 is a LAN War of the Ring's map save (retail's WOTRMP type, the LAN lobby's Load Game page; mode
	// 6, RW 0x6DCFDF): a War of the Ring map save as 8, listed only there
	const bool wotr = h.gameKind == 8 || h.gameKind == 16;
	if (h.gameKind != 1 && h.gameKind != 2 && !wotr)
	{
		return failWith(error, "the save's game kind " + std::to_string(h.gameKind) + " is not one this build saves (1 Campaign, 2 Skirmish, 8 War of the Ring, 16 a LAN War of the Ring)");
	}
	if ((h.gameKind == 1 || wotr) != !h.campaign.empty())
	{
		return failWith(error, h.gameKind == 1 ? std::string("the save is a campaign mission without its campaign")
			: wotr ? std::string("the War of the Ring save names no scenario") : "the save is a skirmish that names a campaign");
	}
	// lane WOTR2 milestone 3: the War of the Ring part (the strategic game's stream, then the host's session) belongs to War of the Ring saves, and only
	// to them
	if (!parts[3].empty())
	{
		NetByteReader lw(parts[3]);
		out.livingWorld = lw.bytes();
		out.livingWorldSession = lw.bytes();
		if (lw.failed() || !lw.atEnd())
		{
			return failWith(error, "the save is damaged (its War of the Ring part is not a strategic game and a session)");
		}
	}
	if (!headerOnly && wotr == out.livingWorld.empty())
	{
		return failWith(error, wotr ? std::string("the War of the Ring save carries no strategic game") : "the save carries a War of the Ring game it is not");
	}
	if (h.gameKind == 16 && h.savePoint != 2)
	{
		return failWith(error, "the LAN War of the Ring save is not at the strategic map");
	}
	if (h.gameKind == 8 && h.savePoint != 2 && h.savePoint != 3)
	{
		return failWith(error, "the War of the Ring save is neither at the strategic map nor inside a battle");
	}
	if (h.savePoint == 3)
	{
		// WOTR2 milestone 6: a War of the Ring battle (retail's mode 3, GameState RW 0x6DCFDF): the battle's RTS game at frame F (its log and snapshot) and
		// the strategic game with its session
		if (h.gameKind != 8)
		{
			return failWith(error, "the save is inside a War of the Ring battle without its strategic game");
		}
		if (h.frame == 0 || (!headerOnly && (parts[1].empty() || parts[2].empty())))
		{
			return failWith(error, "the War of the Ring battle save carries no RTS game (a frame, a log and a snapshot)");
		}
	}
	if (h.savePoint == 2)
	{
		// the War of the Ring strategic map: its scenario and its strategic game, no RTS game (no frame, no log, no snapshot)
		if (!wotr)
		{
			return failWith(error, "the save is at the War of the Ring map without its strategic game");
		}
		if (h.frame != 0 || h.commands != 0 || (!headerOnly && (!parts[1].empty() || !parts[2].empty())))
		{
			return failWith(error, "the War of the Ring map save carries an RTS game (a frame, commands, a log or a snapshot)");
		}
	}
	if (h.savePoint == 1)
	{
		// the campaign menu between missions: a campaign, a mission index and difficulty in range, no game (no frame, no log, no snapshot)
		if (h.gameKind != 1)
		{
			return failWith(error, "the save is a campaign menu save without its campaign");
		}
		if (h.campaignMission < 0 || h.campaignDifficulty < 0 || h.campaignDifficulty > 2)
		{
			return failWith(error, "the campaign menu save's mission " + std::to_string(h.campaignMission) + " / difficulty " +
				std::to_string(h.campaignDifficulty) + " is out of range");
		}
		if (h.frame != 0 || h.commands != 0 || (!headerOnly && (!parts[1].empty() || !parts[2].empty())))
		{
			return failWith(error, "the campaign menu save carries a game (a frame, commands, a log or a snapshot)");
		}
	}
	if (h.savePoint != 1 && (h.campaignVictorious || h.campaignReady || h.campaignAdvanced || h.autoSave))
	{
		return failWith(error, "the save is a game at its frame with the campaign menu's flags");
	}
	if (!h.campaignCarryover.empty())
	{
		CampaignCarryover::State st;
		std::string why;
		if (h.gameKind != 1)
		{
			return failWith(error, "the save is a skirmish with a campaign carry-over");
		}
		if (!CampaignCarryover::State::parse(h.campaignCarryover, st, &why))
		{
			return failWith(error, "the save's " + why);
		}
	}
	out.log = std::move(parts[1]);
	out.snapshot = std::move(parts[2]); // lane SAVE-4a
	return true;
}

bool SaveGameFile::load(const std::string &path, SaveGameFile &out, std::string *error, bool headerOnly)
{
	// read part by part: the header-only listing reads the header part and the parts' 40-byte heads, nothing else; every size is checked against the
	// file's before anything is allocated (review: a large foreign file read whole aborted the listing with std::bad_alloc)
	std::error_code ec;
	const std::uintmax_t size = std::filesystem::file_size(PathIO::native(path), ec);
	std::ifstream in(PathIO::native(path), std::ios::binary);
	if (ec || !in)
	{
		return failWith(error, "cannot open the save " + path);
	}
	try
	{
		FileSource src(in, (std::uint64_t)size);
		SaveGameFile parsed;
		if (!parseFrom(src, parsed, error, headerOnly))
		{
			return false;
		}
		out = std::move(parsed);
		return true;
	}
	catch (const std::exception &e)
	{
		return failWith(error, std::string("the save could not be read (") + e.what() + ")");
	}
}

bool SaveGameFile::write(const std::string &path, std::string *error) const
{
	const std::vector<std::uint8_t> bytes = encode();
	const std::filesystem::path target = PathIO::native(path);
	const std::filesystem::path temp = PathIO::native(path + ".tmp");
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

SaveGameCompatibility SaveGameCompatibility::checkMenuSave(const SaveGameHeader &save, const ProfileIdentity &running, const std::string &campaignRefusal)
{
	// a campaign menu save holds no log, so the build that made it does not matter (an update between missions keeps the campaign), but its data and
	// rules must be the running ones (a mod's other campaign list or archives are a named refusal), and its campaign, mission and difficulty must exist
	SaveGameCompatibility c;
	// lane PROFILE-2: a save of a build before PROFILE-2 (a version 1 identity) against the running identity in that format
	const ProfileIdentity a = ProfileIdentity::fromText(dataOnly(ProfileIdentity::inFormatOf(running, save.profile).text));
	const ProfileIdentity b = ProfileIdentity::fromText(dataOnly(save.profile.text));
	if (a != b)
	{
		c.refusal = RecordingBuild::openWith(save.buildVersion, save.buildDate) + " The game data or rules differ from this build.";
		return c;
	}
	if (!campaignRefusal.empty())
	{
		c.refusal = campaignRefusal;
		return c;
	}
	c.loadable = true;
	return c;
}

SaveGameCompatibility SaveGameCompatibility::check(const SaveGameHeader &save, const ProfileIdentity &running, const std::string &runningEngineId,
	const std::string &, bool multiplayer)
{
	SaveGameCompatibility c;
	if (save.engineId != runningEngineId)
	{
		// owner decision 2: a log-only save is refused by another build, naming the build that made it (SAVES.md section 5: replay with that build)
		c.refusal = RecordingBuild::openWith(save.buildVersion, save.buildDate);
		return c;
	}
	const ProfileIdentity a = ProfileIdentity::fromText(withoutProfileLine(ProfileIdentity::inFormatOf(running, save.profile).text)); // lane PROFILE-2
	const ProfileIdentity b = ProfileIdentity::fromText(withoutProfileLine(save.profile.text));
	if (a != b)
	{
		c.refusal = RecordingBuild::openWith(save.buildVersion, save.buildDate) + " The game data or rules differ from this build.";
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
