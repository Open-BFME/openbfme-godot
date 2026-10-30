// OpenBFME. GPL-3.0.
// See GameNetwork/ProfileIdentity.h.

#include "GameNetwork/ProfileIdentity.h"

#include "Common/ArchiveFileSystem.h"
#include "Common/MD5.h"
#include "Common/Sha256.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <cctype>
#include <iterator>
#include <sstream>

// the id is generated at configure time (cmake/EngineId.cmake) or supplied by the release pipeline: a build without one does not compile
#ifndef OPENBFME_ENGINE_ID
#error "OPENBFME_ENGINE_ID is not defined: configure through engine/CMakeLists.txt (cmake/Net.cmake) so the engine compatibility id is generated"
#endif
#ifndef OPENBFME_GIT_PROVENANCE
#define OPENBFME_GIT_PROVENANCE "none"
#endif

namespace
{
// the owned formats and rules a peer must share: bump the number of the one that changes
const char *const kFeatures = "net-wire 3, game-message 2, replay 2, lobby 2, state-hash-sections 1, lockstep-frame-model 1";
const char *const kNumeric = "IEEE binary32/binary64 through the numeric facade, canonical MXCSR 0x1F80 (round to nearest), no FMA contraction";

std::vector<std::string> lines(const std::string &t)
{
	std::vector<std::string> out;
	std::istringstream in(t);
	std::string l;
	while (std::getline(in, l))
	{
		out.push_back(l);
	}
	return out;
}

// lane PROFILE-3 (owner: permissive): the lines of a version 3 text that only say what a player has, never whether two players can play together:
// the archive list (PROFILE-2), the mod's names and content hashes ("mods <n>", "mod folder / archive / install <name>", "modcontent ..."; its sim
// scripts' "sdk sim" line and its engine features still count) and the asset packs ("pack <name> <files> <sha256>", SDK-1c). The simdata line
// (the files the simulation reads, as mounted) is what tells two installs or two mods apart
bool presentationLine(const std::string &l)
{
	for (const char *p : { "archive ", "mods ", "mod ", "modcontent ", "pack " })
	{
		if (l.rfind(p, 0) == 0)
		{
			return true;
		}
	}
	return false;
}

// lane PROFILE-3: up to three entries of `v`, then "and N more" ("none" when empty)
std::string listOf(const std::vector<std::string> &v)
{
	std::string r;
	for (size_t i = 0; i < v.size() && i < 3; ++i)
	{
		r += (i ? ", " : "") + v[i];
	}
	return v.empty() ? std::string("none") : v.size() > 3 ? r + " and " + std::to_string(v.size() - 3) + " more" : r;
}

// lane PROFILE-3: the text's lines that start with one of `prefixes`, in order
std::vector<std::string> linesStarting(const std::string &t, std::initializer_list<const char *> prefixes)
{
	std::vector<std::string> out;
	for (const std::string &l : lines(t))
	{
		for (const char *p : prefixes)
		{
			if (l.rfind(p, 0) == 0)
			{
				out.push_back(l);
				break;
			}
		}
	}
	return out;
}
} // namespace

const char *ProfileIdentity::buildId()
{
	return OPENBFME_ENGINE_ID;
}

const char *ProfileIdentity::gitProvenance()
{
	return OPENBFME_GIT_PROVENANCE;
}

ProfileIdentity ProfileIdentity::compute(const std::vector<MountedArchive> &archives, RandomAlgorithm rng, const std::vector<std::string> &extraFeatures,
	SimulationProfile profile, const std::vector<std::string> &modLines, const std::string &simData, const std::vector<std::string> &packLines)
{
	std::ostringstream o;
	o << "openbfme-profile " << (simData.empty() ? 1 : 3) << "\n"; // lane PROFILE-2 / PROFILE-3: a product caller writes version 3
	o << (profile == SimulationProfile::Uber ? "profile uber\n" : "profile enhanced\n"); // lane UBER-1
	o << "engine " << OPENBFME_ENGINE_ID << "\n";
	std::vector<std::string> extra = extraFeatures;
	std::sort(extra.begin(), extra.end());
	o << "features " << kFeatures;
	for (const std::string &f : extra)
	{
		o << ", " << f;
	}
	o << "\n";
	o << "numeric " << kNumeric << "\n";
	o << "rng " << (rng == RandomAlgorithm::ZH_CarryChain ? "ZH_CarryChain" : "RotWK_GameDat_LCG") << "\n";
	if (modLines.empty())
	{
		o << "mods none\n";
	}
	else
	{
		size_t sources = 0;
		for (const std::string &l : modLines)
		{
			sources += l.compare(0, 4, "mod ") == 0 ? 1 : 0;
		}
		o << "mods " << sources << "\n";
		for (const std::string &l : modLines)
		{
			o << l << "\n";
		}
	}
	if (!simData.empty())
	{
		o << "simdata " << simData << "\n";
		for (const std::string &l : packLines)
		{
			o << l << "\n"; // lane PROFILE-3: after simdata, before the archives: dropping them gives the exact version 2 text
		}
	}
	for (size_t i = 0; i < archives.size(); ++i)
	{
		const MountedArchive &a = archives[i];
		o << "archive " << i << " " << a.install << ":" << a.canonicalPath << " " << a.md5 << " " << a.size << "\n";
	}
	return fromText(o.str());
}

SimulationProfile ProfileIdentity::simulationProfile() const
{
	for (const std::string &l : lines(text))
	{
		if (l == "profile uber")
		{
			return SimulationProfile::Uber;
		}
	}
	return SimulationProfile::Faithful;
}

int ProfileIdentity::version() const
{
	const int header = text.rfind("openbfme-profile 2\n", 0) == 0 ? 2 : text.rfind("openbfme-profile 3\n", 0) == 0 ? 3 : 1;
	if (header == 1)
	{
		return 1;
	}
	for (const std::string &l : lines(text))
	{
		if (l.rfind("simdata ", 0) == 0)
		{
			return header;
		}
	}
	return 1;
}

std::string ProfileIdentity::identityText() const
{
	const int v = version();
	if (v == 1)
	{
		return text;
	}
	std::string out;
	for (const std::string &l : lines(text))
	{
		if (v == 2 ? l.rfind("archive ", 0) != 0 : !presentationLine(l))
		{
			out += l + "\n";
		}
	}
	return out;
}

ProfileIdentity ProfileIdentity::inFormatOf(const ProfileIdentity &ours, const ProfileIdentity &theirs)
{
	const int from = ours.version(), to = theirs.version();
	if (to >= from)
	{
		return ours;
	}
	// lane PROFILE-3: a version 3 text without its pack lines, header 2, is the exact version 2 text; lane PROFILE-2: a version 2 text without its
	// simdata line, header 1, the exact version 1 text
	std::string out;
	for (const std::string &l : lines(ours.text))
	{
		if (l.rfind("pack ", 0) == 0 || (to == 1 && l.rfind("simdata ", 0) == 0))
		{
			continue;
		}
		out += (l.rfind("openbfme-profile ", 0) == 0 ? "openbfme-profile " + std::to_string(to) : l) + "\n";
	}
	return fromText(out);
}

std::vector<std::string> ProfileIdentity::archiveLines() const
{
	std::vector<std::string> out;
	for (const std::string &l : lines(text))
	{
		const size_t sp = l.rfind("archive ", 0) == 0 ? l.find(' ', 8) : std::string::npos;
		if (sp != std::string::npos)
		{
			out.push_back(l.substr(sp + 1));
		}
	}
	return out;
}

ProfileIdentity ProfileIdentity::fromText(const std::string &text)
{
	ProfileIdentity p;
	p.text = text;
	const std::string identity = p.identityText();
	p.digest = MD5::ofBytes(identity.data(), identity.size());
	return p;
}

std::string ProfileIdentity::difference(const ProfileIdentity &ours, const ProfileIdentity &theirs)
{
	if (ours == theirs)
	{
		return "";
	}
	// lane PROFILE-2: the identity lines (a version 2 text without its archive lines: those differ harmlessly when the simdata line is equal)
	const std::vector<std::string> a = lines(ours.identityText()), b = lines(theirs.identityText());
	std::string out = "profile " + ours.digest + " != " + theirs.digest;
	int shown = 0;
	bool archives = false, simdata = false, sdk = false;
	for (size_t i = 0; i < std::max(a.size(), b.size()); ++i)
	{
		const std::string x = i < a.size() ? a[i] : "(none)", y = i < b.size() ? b[i] : "(none)";
		if (x != y)
		{
			archives = archives || x.rfind("archive ", 0) == 0 || y.rfind("archive ", 0) == 0;
			simdata = simdata || x.rfind("simdata ", 0) == 0 || y.rfind("simdata ", 0) == 0;
			sdk = sdk || x.rfind("sdk sim ", 0) == 0 || y.rfind("sdk sim ", 0) == 0; // lane PROFILE-3
			if (shown < 3)
			{
				out += "; ours: '" + x + "' theirs: '" + y + "'";
			}
			++shown;
		}
	}
	if (simdata)
	{
		// lane PROFILE-2: the game data differ; the archives that differ say where (owner decision 10-26 "A"): each side's archives the other lacks
		// without their index: it moves when an archive is added or missing
		std::vector<std::string> sa = ours.archiveLines(), sb = theirs.archiveLines();
		std::sort(sa.begin(), sa.end());
		std::sort(sb.begin(), sb.end());
		std::vector<std::string> onlyOurs, onlyTheirs;
		std::set_difference(sa.begin(), sa.end(), sb.begin(), sb.end(), std::back_inserter(onlyOurs));
		std::set_difference(sb.begin(), sb.end(), sa.begin(), sa.end(), std::back_inserter(onlyTheirs));
		out += "; archives only ours: " + listOf(onlyOurs) + "; only theirs: " + listOf(onlyTheirs);
		archives = true;
	}
	if ((simdata || sdk) && (ours.version() == 3 || theirs.version() == 3))
	{
		// lane PROFILE-3: the mod lines no longer count, so the refusal names each side's mod (the data or the sim scripts it changes differ)
		const std::vector<std::string> ma = linesStarting(ours.text, { "mod ", "mods none" }), mb = linesStarting(theirs.text, { "mod ", "mods none" });
		out += "; mods ours: " + listOf(ma) + "; theirs: " + listOf(mb);
	}
	if (archives)
	{
		// lane CONTRIB-1 (review): in player words. A disc install patched with EA's official patch and an All In One install keep different
		// archive lists (S-4700), so they cannot play together even though both are RotWK 2.01
		out += ". The two players' game files differ (for example one has a disc install patched with EA's official patch and the other the All In One "
			"install, or a mod): both players need the same game install";
	}
	return out;
}

std::string ProfileIdentity::presentationDifference(const ProfileIdentity &ours, const ProfileIdentity &theirs)
{
	// lane PROFILE-3: what the players had that never refuses a game: the packs, the mod's names and content hashes, the archives (sorted, without
	// their index: it moves when one is added or missing)
	std::string out;
	auto add = [&](const char *what, std::vector<std::string> a, std::vector<std::string> b, bool sorted) {
		if (sorted)
		{
			std::sort(a.begin(), a.end());
			std::sort(b.begin(), b.end());
		}
		if (a == b)
		{
			return;
		}
		std::vector<std::string> onlyA, onlyB;
		std::vector<std::string> sa = a, sb = b;
		std::sort(sa.begin(), sa.end());
		std::sort(sb.begin(), sb.end());
		std::set_difference(sa.begin(), sa.end(), sb.begin(), sb.end(), std::back_inserter(onlyA));
		std::set_difference(sb.begin(), sb.end(), sa.begin(), sa.end(), std::back_inserter(onlyB));
		out += std::string(out.empty() ? "" : "; ") + what + (onlyA.empty() && onlyB.empty() ? std::string(" in another order") : " only ours: "
			+ listOf(onlyA) + "; only theirs: " + listOf(onlyB));
	};
	add("packs", linesStarting(ours.text, { "pack " }), linesStarting(theirs.text, { "pack " }), false);
	add("mods", linesStarting(ours.text, { "mods ", "mod ", "modcontent " }), linesStarting(theirs.text, { "mods ", "mod ", "modcontent " }), false);
	add("archives", ours.archiveLines(), theirs.archiveLines(), true);
	return out;
}

// lane NETLAG-1: historical engine identities whose recordings are accepted when only the engine line differs. The fixture test reads the old
// recording, checks its identity and plays every frame; historical hash mismatches are reported, not required to be zero. Hash equality is checked
// separately with a recording made by the current simulation using the old command path and engine id; this list does not guarantee it for old builds.
// Ids come from cmake -DOPENBFME_ID_ROOT=<engine dir> -P cmake/EngineId.cmake at the commit.
const std::vector<ProfileIdentity::VerifiedEngine> &ProfileIdentity::verifiedReplayEngines()
{
	static const std::vector<VerifiedEngine> v = {
		{ "e2a8e912ec797b1969fafeb7df6d18a264b9aa2723ced5109490ff5b2ce0bfbb", "q19 tree b093e098 with NETLAG-1's command-delay sources reverted (before NETLAG-1), fixture engine/tests/data/netlag1/prelane_runahead2.replay" },
	};
	return v;
}

// BATCH-P4 (the online batch's `net-wire 3`): the features line's `net-wire N` names the live datagram format only (peers, relay, lobby); a recording
// never reads it, so for REPLAY playback two features lines that differ in that entry alone are the same. Every other feature still has to match
static bool sameFeaturesButNetWire(const std::string &a, const std::string &b)
{
	if (a.rfind("features ", 0) != 0 || b.rfind("features ", 0) != 0)
	{
		return false;
	}
	auto strip = [](std::string line) {
		const size_t at = line.find("net-wire ");
		if (at == std::string::npos)
		{
			return line;
		}
		size_t end = line.find(", ", at);
		end = end == std::string::npos ? line.size() : end + 2;
		return line.erase(at, end - at);
	};
	return strip(a) == strip(b);
}

bool ProfileIdentity::replayCompatible(const ProfileIdentity &ours, const ProfileIdentity &theirs, std::string *engine)
{
	// lane PROFILE-2: an older build's (version 1) recording is compared with this build's identity in its format; two version 2 identities by their
	// identity lines (the archive lines may differ harmlessly)
	const std::vector<std::string> a = lines(inFormatOf(ours, theirs).identityText()), b = lines(theirs.identityText());
	if (a.size() != b.size())
	{
		return false;
	}
	std::string theirEngine;
	for (size_t i = 0; i < a.size(); ++i)
	{
		if (a[i] == b[i] || sameFeaturesButNetWire(a[i], b[i]))
		{
			continue;
		}
		if (a[i].rfind("engine ", 0) != 0 || b[i].rfind("engine ", 0) != 0 || !theirEngine.empty())
		{
			return false; // another line differs (or a second one): the data, the numerics or the formats are not the same
		}
		theirEngine = b[i].substr(7);
	}
	if (engine)
	{
		*engine = theirEngine;
	}
	if (theirEngine.empty())
	{
		return true; // lane PROFILE-2 (Sol review P3): every line the same once in the recording's format (a version 1 recording of this very build)
	}
	for (const VerifiedEngine &v : verifiedReplayEngines())
	{
		if (theirEngine == v.id)
		{
			return true;
		}
	}
	return false;
}

// lane PROFILE-2 (ProfileIdentity.h: SimulationData)
bool SimulationData::covers(const std::string &p)
{
	auto ends = [&](const char *ext) {
		const size_t n = std::char_traits<char>::length(ext);
		return p.size() >= n && p.compare(p.size() - n, n, ext) == 0;
	};
	// art\w3d\*.w3d (Sol review P1): the logic reads models too: a projectile's launch bone (LiveGame m_launchBones, RW 0x6CAB85) and a wall's
	// footprint mesh (m_wallMeshes) come from the W3D the archives hold (WW3DAssetManager: art\w3d\<xx>\<name>.w3d)
	// lane PROFILE-3 (the read trace of a retail and a mod game, tests/test_profile3_retail.cpp): the skirmish AI's base layouts (bases\...\*.bse)
	// and script libraries (libraries\...\*.map), and a map folder's every file but its pictures and strings (its scripts.lua and
	// scriptevents.xml are asked for at load): an unknown kind of file there counts
	if (p.rfind("maps\\", 0) == 0)
	{
		for (const char *shown : { ".tga", ".dds", ".jpg", ".jpeg", ".png", ".bmp", ".str", ".csf" })
		{
			if (ends(shown))
			{
				return false;
			}
		}
		return true;
	}
	return p.rfind("data\\ini\\", 0) == 0 || p.rfind("data\\scripts\\", 0) == 0 || p.rfind("bases\\", 0) == 0 || p.rfind("libraries\\", 0) == 0
		|| (p.rfind("art\\w3d\\", 0) == 0 && ends(".w3d"));
}

bool SimulationData::digest(ArchiveFileSystem &fs, std::string &out, std::string *error, size_t *files)
{
	FilenameList listed;
	for (const char *dir : { "data\\ini\\", "data\\scripts\\", "maps\\", "art\\w3d\\", "bases\\", "libraries\\" })
	{
		fs.getFileListInDirectory(std::string(), dir, "*", listed, true);
	}
	std::vector<std::pair<std::string, std::string>> entries; // (normalized path, the path as listed)
	for (const std::string &f : listed)
	{
		std::string key = f;
		for (char &c : key)
		{
			c = c == '/' ? '\\' : (char)std::tolower((unsigned char)c);
		}
		if (covers(key))
		{
			entries.emplace_back(key, f);
		}
	}
	std::sort(entries.begin(), entries.end());
	entries.erase(std::unique(entries.begin(), entries.end(), [](const auto &x, const auto &y) { return x.first == y.first; }), entries.end());
	if (entries.empty())
	{
		if (error)
		{
			*error = "simulation data: the mounted archives hold no file under data\\ini\\, data\\scripts\\ or maps\\";
		}
		return false;
	}
	std::string manifest;
	std::vector<std::uint8_t> bytes;
	for (const auto &e : entries)
	{
		std::string why;
		if (!fs.readFile(e.second, bytes, &why))
		{
			if (error)
			{
				*error = "simulation data: '" + e.second + "' is listed but cannot be read: " + why;
			}
			return false;
		}
		manifest += e.first + " " + MD5::ofBytes(bytes.data(), bytes.size()) + "\n";
	}
	out = MD5::ofBytes(manifest.data(), manifest.size());
	if (files)
	{
		*files = entries.size();
	}
	return true;
}

bool SimulationData::cachedDigest(ArchiveFileSystem &fs, const std::string &key, const std::string &cachePath, std::string &out, std::string *error,
	std::string *warning)
{
	// what the digest covers (covers() above): a change of it is a new key, never an old answer
	static const std::string kDefinition = "simdata 3 (data\\ini, data\\scripts, bases, libraries, maps but pictures / strings, art\\w3d *.w3d)\n";
	const std::string named = kDefinition + key;
	const auto h = Sha256::hash(named.data(), named.size());
	const std::string cacheKey = HexBytes(h.data(), h.size());
	if (!cachePath.empty())
	{
		std::ifstream in(cachePath);
		std::string line;
		while (std::getline(in, line))
		{
			if (line.size() == 64 + 1 + 32 && line.compare(0, 64, cacheKey) == 0 && line[64] == '\t'
				&& line.find_first_not_of("0123456789abcdef", 65) == std::string::npos)
			{
				out = line.substr(65);
				return true;
			}
		}
	}
	if (!digest(fs, out, error))
	{
		return false;
	}
	if (!cachePath.empty())
	{
		std::error_code ec;
		std::filesystem::create_directories(std::filesystem::path(cachePath).parent_path(), ec);
		std::ofstream app(cachePath, std::ios::app);
		app << cacheKey << '\t' << out << '\n'; // one short line: appends of several processes do not interleave
		if (!app && warning)
		{
			*warning = "simulation data: the cache " + cachePath + " could not be written (computed again next start)";
		}
	}
	return true;
}

std::string SimulationData::defaultCachePath()
{
	if (const char *p = std::getenv("OPENBFME_SIMDATA_CACHE"))
	{
		return p;
	}
#ifdef _WIN32
	if (const char *p = std::getenv("LOCALAPPDATA"))
	{
		return std::string(p) + "\\OpenBFME\\simdata-cache.tsv";
	}
#else
	if (const char *p = std::getenv("XDG_CACHE_HOME"); p && *p)
	{
		return std::string(p) + "/openbfme/simdata-cache.tsv";
	}
	if (const char *p = std::getenv("HOME"); p && *p)
	{
		return std::string(p) + "/.cache/openbfme/simdata-cache.tsv";
	}
#endif
	return "";
}
