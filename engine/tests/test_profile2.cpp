// OpenBFME unit tests. GPL-3.0.
// lane PROFILE-2 (owner decision 10-26): the profile identity's digest covers the game data the simulation reads (SimulationData), not the archive
// layout; the archive lines stay in the text. Synthetic BIG archives only (BigTestUtil.h).

#include "BigTestUtil.h"
#include "RetailTestMount.h"
#include "Common/ArchiveFile.h"
#include "Common/MD5.h"
#include "Common/System/SaveGame/SaveGameFile.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"
#include "GameNetwork/OnlineLobbyClient.h"
#include "GameNetwork/ProfileIdentity.h"

#include "doctest.h"

#include <chrono>
#include <cstdio>
#include <memory>

namespace
{
using Files = std::vector<std::pair<std::string, std::string>>;

// the simulation data digest of archives mounted in order (the first one holding a file wins, as RetailArchivePolicy mounts them)
std::string simOf(const std::vector<Files> &archives, size_t *files = nullptr)
{
	Win32BIGFileSystem fs;
	for (size_t i = 0; i < archives.size(); ++i)
	{
		std::string error;
		const std::string name = "a" + std::to_string(i) + ".big";
		auto a = fs.openArchiveFromMemory(name, std::make_shared<const std::vector<std::uint8_t>>(bigtest::makeBig("BIGF", archives[i])), &error);
		REQUIRE_MESSAGE(a, error);
		fs.mountArchive(std::move(a), name, false);
	}
	std::string digest, error;
	REQUIRE_MESSAGE(SimulationData::digest(fs, digest, &error, files), error);
	return digest;
}

std::vector<MountedArchive> archivesNamed(const std::vector<std::string> &names)
{
	std::vector<MountedArchive> v;
	for (const std::string &n : names)
	{
		MountedArchive a;
		a.install = "rotwk";
		a.canonicalPath = n;
		a.md5 = MD5::ofBytes(n.data(), n.size());
		a.size = n.size();
		v.push_back(a);
	}
	return v;
}

const Files kBase = { { "data\\ini\\weapon.ini", "Weapon A\nEnd\n" }, { "data\\ini\\object\\men.ini", "Object M\nEnd\n" },
	{ "data\\scripts\\multiplayerscripts.scb", "SCB1" }, { "maps\\map mp a\\map mp a.map", "MAP1" }, { "maps\\map mp a\\map.ini", "MapIni" },
	{ "maps\\map mp a\\map mp a_art.tga", "PREVIEW1" }, { "art\\textures\\a.tga", "TEX1" }, { "data\\lang\\english.csf", "STR1" }, { "art\\w3d\\gu\\gubow.w3d", "BONES1" } };

Files with(Files f, const std::string &path, const std::string &bytes)
{
	for (auto &e : f)
	{
		if (e.first == path)
		{
			e.second = bytes;
			return f;
		}
	}
	f.emplace_back(path, bytes);
	return f;
}
} // namespace

TEST_CASE("profile2 simdata: covers INI, scripts, maps and map.ini; not textures, strings or map previews")
{
	size_t files = 0;
	const std::string base = simOf({ kBase }, &files);
	CHECK(files == 6);
	CHECK(base.size() == 32);
	CHECK(simOf({ with(kBase, "art\\textures\\a.tga", "TEX2") }) == base);
	CHECK(simOf({ with(kBase, "data\\lang\\english.csf", "STR2") }) == base);
	CHECK(simOf({ with(kBase, "maps\\map mp a\\map mp a_art.tga", "PREVIEW2") }) == base);
	CHECK(simOf({ with(kBase, "data\\ini\\weapon.ini", "Weapon B\nEnd\n") }) != base);
	CHECK(simOf({ with(kBase, "data\\ini\\object\\men.ini", "Object N\nEnd\n") }) != base);
	CHECK(simOf({ with(kBase, "data\\scripts\\multiplayerscripts.scb", "SCB2") }) != base);
	CHECK(simOf({ with(kBase, "maps\\map mp a\\map mp a.map", "MAP2") }) != base);
	CHECK(simOf({ with(kBase, "maps\\map mp a\\map.ini", "MapIni2") }) != base);
	CHECK(simOf({ with(kBase, "data\\ini\\new.ini", "x") }) != base); // an added INI counts
	CHECK(simOf({ with(kBase, "art\\w3d\\gu\\gubow.w3d", "BONES2") }) != base); // a model's launch bone (Sol review P1)
	CHECK(SimulationData::covers("art\\w3d\\gu\\gumaarms_skn.w3d")); // Sol review P1: launch bones and wall footprints come from the models
	CHECK(!SimulationData::covers("art\\textures\\gumaarms.dds"));
	CHECK(SimulationData::covers("data\\ini\\default\\gamedata.ini"));
	CHECK(!SimulationData::covers("maps\\map mp a\\map mp a.tga"));
}

TEST_CASE("profile2 simdata: the resolved file counts, whichever archive and layout supplies it, and names compare without case")
{
	const std::string base = simOf({ kBase });
	// a patch archive in front overriding the INI with the same bytes, and the rest split across two archives (another layout): same data
	Files first = { { "data\\ini\\weapon.ini", "Weapon A\nEnd\n" }, { "art\\textures\\b.tga", "TEX" } };
	Files rest = with(kBase, "data\\ini\\weapon.ini", "Weapon OLD\nEnd\n");
	CHECK(simOf({ first, rest }) == base);
	// precedence: the OLD bytes in front win, so the data differ
	CHECK(simOf({ rest, first }) != base);
	// case and separators
	Files cased;
	for (const auto &f : kBase)
	{
		std::string p = f.first;
		for (char &c : p)
		{
			c = c == '\\' ? '/' : (char)std::toupper((unsigned char)c);
		}
		cased.emplace_back(p, f.second);
	}
	CHECK(simOf({ cased }) == base);
}

TEST_CASE("profile2 simdata: a mount without game data is an error, not an empty digest")
{
	Win32BIGFileSystem fs;
	std::string error;
	auto a = fs.openArchiveFromMemory("t.big", std::make_shared<const std::vector<std::uint8_t>>(bigtest::makeBig("BIGF", { { "art\\a.tga", "x" } })), &error);
	REQUIRE(a);
	fs.mountArchive(std::move(a), "t.big", false);
	std::string digest;
	CHECK(!SimulationData::digest(fs, digest, &error));
	CHECK(error.find("no file under") != std::string::npos);
}

TEST_CASE("profile2 simdata cache: a mount's digest is computed once per key, then read back; another key is computed again")
{
	const std::string cache = "profile2_simdata_cache.tsv";
	std::remove(cache.c_str());
	Win32BIGFileSystem fs;
	std::string error;
	auto a = fs.openArchiveFromMemory("a.big", std::make_shared<const std::vector<std::uint8_t>>(bigtest::makeBig("BIGF", kBase)), &error);
	REQUIRE(a);
	fs.mountArchive(std::move(a), "a.big", false);
	std::string first, warning;
	REQUIRE(SimulationData::cachedDigest(fs, "rotwk:a.big 00 1\n", cache, first, &error, &warning));
	CHECK(warning.empty());
	CHECK(first == simOf({ kBase }));
	Win32BIGFileSystem empty; // nothing to hash: only the cache can answer
	std::string again;
	REQUIRE(SimulationData::cachedDigest(empty, "rotwk:a.big 00 1\n", cache, again, &error));
	CHECK(again == first);
	CHECK_FALSE(SimulationData::cachedDigest(empty, "rotwk:b.big 00 1\n", cache, again, &error)); // another mount: computed, and this one cannot be
	std::remove(cache.c_str());
}

TEST_CASE("profile2 identity: a product identity (version 3 since PROFILE-3) matches on everything but the archive lines")
{
	const auto disc = archivesNamed({ "INI.big", "_patch201.big", "Textures0.big" });
	const auto aio = archivesNamed({ "INI.big", "Textures0.big", "Textures_aio.big" });
	const ProfileIdentity a = ProfileIdentity::compute(disc, RandomAlgorithm::ZH_CarryChain, {}, SimulationProfile::Faithful, {}, "0123456789abcdef0123456789abcdef");
	const ProfileIdentity b = ProfileIdentity::compute(aio, RandomAlgorithm::ZH_CarryChain, {}, SimulationProfile::Faithful, {}, "0123456789abcdef0123456789abcdef");
	CHECK(a.version() == 3); // lane PROFILE-3
	CHECK(a.text.rfind("openbfme-profile 3\n", 0) == 0);
	CHECK(a.text.find("\nsimdata 0123456789abcdef0123456789abcdef\narchive 0 rotwk:INI.big ") != std::string::npos); // (A): the archive lines stay
	CHECK(a.digest == b.digest);
	CHECK(a == b);
	CHECK(ProfileIdentity::difference(a, b).empty());
	CHECK(ProfileIdentity::fromText(a.text) == a);

	const ProfileIdentity c = ProfileIdentity::compute(aio, RandomAlgorithm::ZH_CarryChain, {}, SimulationProfile::Faithful, {}, "ffffffffffffffffffffffffffffffff");
	CHECK(a != c);
	const std::string d = ProfileIdentity::difference(a, c);
	CHECK(d.find("ours: 'simdata 0123456789abcdef0123456789abcdef' theirs: 'simdata ffffffffffffffffffffffffffffffff'") != std::string::npos);
	CHECK(d.find("archives only ours: rotwk:_patch201.big ") != std::string::npos);
	CHECK(d.find("only theirs: rotwk:Textures_aio.big ") != std::string::npos);
	CHECK(d.find("both players need the same game install") != std::string::npos);

	// every other line still counts
	const ProfileIdentity uber = ProfileIdentity::compute(disc, RandomAlgorithm::ZH_CarryChain, {}, SimulationProfile::Uber, {}, "0123456789abcdef0123456789abcdef");
	CHECK(uber != a);
	// lane PROFILE-3: a mod's name no longer counts (its data is in the simdata line); in the version 2 format it still does
	const ProfileIdentity mod = ProfileIdentity::compute(disc, RandomAlgorithm::ZH_CarryChain, {}, SimulationProfile::Faithful, { "mod folder BigMod" }, "0123456789abcdef0123456789abcdef");
	CHECK(mod == a);
	auto asV2 = [](const ProfileIdentity &p) { return ProfileIdentity::fromText("openbfme-profile 2" + p.text.substr(p.text.find('\n'))); };
	CHECK(asV2(mod) != asV2(a));
}

TEST_CASE("profile2 identity: version 1 texts keep their digest; a version 2 identity reads as the version 1 text it extends")
{
	const auto disc = archivesNamed({ "INI.big", "_patch201.big" });
	const ProfileIdentity v1 = ProfileIdentity::compute(disc, RandomAlgorithm::ZH_CarryChain);
	CHECK(v1.version() == 1);
	CHECK(v1.text.rfind("openbfme-profile 1\n", 0) == 0);
	CHECK(v1.text.find("simdata") == std::string::npos);
	CHECK(v1.digest == MD5::ofBytes(v1.text.data(), v1.text.size()));
	// "openbfme-profile 2" without a simdata line is not version 2: the whole text counts
	CHECK(ProfileIdentity::fromText("openbfme-profile 2\narchive 0 x\n").version() == 1);

	const ProfileIdentity v2 = ProfileIdentity::compute(disc, RandomAlgorithm::ZH_CarryChain, {}, SimulationProfile::Faithful, {}, "0123456789abcdef0123456789abcdef"); // version 3 since PROFILE-3
	CHECK(ProfileIdentity::inFormatOf(v2, v1).text == v1.text);
	CHECK(ProfileIdentity::inFormatOf(v2, v1) == v1);
	CHECK(ProfileIdentity::inFormatOf(v2, v2).text == v2.text);
	CHECK(ProfileIdentity::inFormatOf(v1, v2).text == v1.text);
	CHECK(v1 != v2); // a live peer of a build before PROFILE-2 is refused (another build anyway)
}

TEST_CASE("profile2 replay: an older build's version 1 recording is compared in its format; two version 2 identities ignore the archive lines")
{
	const std::string verified = ProfileIdentity::verifiedReplayEngines().front().id;
	auto text = [&](int version, const std::string &engine, const std::string &archive, bool simdata) {
		return "openbfme-profile " + std::to_string(version) + "\nprofile enhanced\nengine " + engine + "\nfeatures f\nnumeric n\nrng ZH_CarryChain\nmods none\n"
			+ (simdata ? "simdata 0123456789abcdef0123456789abcdef\n" : "") + "archive 0 rotwk:" + archive + "\n";
	};
	const ProfileIdentity ours = ProfileIdentity::fromText(text(2, "CURRENT", "INI.big 11 5", true));
	std::string engine;
	CHECK(ProfileIdentity::replayCompatible(ours, ProfileIdentity::fromText(text(1, verified, "INI.big 11 5", false)), &engine));
	CHECK(engine == verified);
	// Sol review P3: a version 1 recording identical once in its format (this very engine) plays
	CHECK(ProfileIdentity::replayCompatible(ours, ProfileIdentity::fromText(text(1, "CURRENT", "INI.big 11 5", false))));
	// a version 1 recording of other archives still does not play
	CHECK(!ProfileIdentity::replayCompatible(ours, ProfileIdentity::fromText(text(1, verified, "INI.big 12 5", false))));
	// version 2: other archives, same data
	CHECK(ProfileIdentity::replayCompatible(ours, ProfileIdentity::fromText(text(2, verified, "Other.big 12 5", true))));
	CHECK(!ProfileIdentity::replayCompatible(ours, ProfileIdentity::fromText(text(2, "UNKNOWN", "INI.big 11 5", true))));
}

TEST_CASE("profile2 saves: a campaign menu save of a version 1 build loads under a version 2 build with the same data")
{
	const auto disc = archivesNamed({ "INI.big", "_patch201.big" });
	SaveGameHeader save;
	save.profile = ProfileIdentity::compute(disc, RandomAlgorithm::ZH_CarryChain);
	const ProfileIdentity running = ProfileIdentity::compute(disc, RandomAlgorithm::ZH_CarryChain, {}, SimulationProfile::Faithful, {}, "0123456789abcdef0123456789abcdef");
	CHECK(SaveGameCompatibility::checkMenuSave(save, running, "").loadable);
	save.profile = ProfileIdentity::compute(archivesNamed({ "INI.big" }), RandomAlgorithm::ZH_CarryChain);
	CHECK(!SaveGameCompatibility::checkMenuSave(save, running, "").loadable);
}

TEST_CASE("profile2 retail: the pure 2.01 mount's simulation data digest (the files it covers, the time it takes)")
{
	retailtest::Mount *m = retailtest::pureMount();
	if (!m)
	{
		std::printf("SKIP profile2 retail: ROTWK_INSTALL / BFME2_INSTALL unset\n");
		return;
	}
	REQUIRE_MESSAGE(m->error.empty(), m->error);
	const auto t0 = std::chrono::steady_clock::now();
	std::string digest, error;
	size_t files = 0;
	REQUIRE_MESSAGE(SimulationData::digest(*m->fs, digest, &error, &files), error);
	const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
	std::printf("profile2 retail: simdata %s over %zu files in %.0f ms\n", digest.c_str(), files, ms);
	CHECK(digest.size() == 32);
	CHECK(files == 15780); // the pure 2.01 mount: data\ini\, data\scripts\, bases\, libraries\, the maps' files but pictures / strings and art\w3d\'s
	                       // models (15521 before PROFILE-3 added bases\, libraries\ and the maps' other files)
	std::string again;
	REQUIRE(SimulationData::digest(*m->fs, again, &error));
	CHECK(again == digest);
	const ProfileIdentity p = ProfileIdentity::compute(m->identityArchives, RandomAlgorithm::ZH_CarryChain, {}, SimulationProfile::Faithful, {}, digest);
	CHECK(p.version() == 3); // lane PROFILE-3
}

namespace
{
// answers every request the client sends (the lobby's health with `features`, the rest a bare lobby answer) and returns them in order
std::vector<OnlineLobbyClient::HttpRequest> drive(OnlineLobbyClient &c, const std::string &features)
{
	std::vector<OnlineLobbyClient::HttpRequest> seen;
	for (int round = 0; round < 64; ++round)
	{
		const auto rs = c.takeRequests();
		if (rs.empty())
		{
			break;
		}
		for (const auto &r : rs)
		{
			seen.push_back(r);
			const std::string answer = r.path == "/v1/health" ? R"({"v":1,"ok":true,"games":0,"relays":[],"features":)" + features + "}"
				: r.path == "/v1/mine" ? std::string(R"({"v":1,"games":[]})") : std::string(R"({"v":1})");
			c.deliver(r.id, 200, answer, "");
		}
	}
	return seen;
}

OnlineLobbyClient::Options archiveOptions()
{
	OnlineLobbyClient::Options o;
	o.profileDigest = "0123456789abcdef0123456789abcdef";
	o.playerTemplateCount = 4;
	o.colorCount = 8;
	o.enableSocial = true;
	o.profileArchives = { "rotwk:INI.big 0123456789abcdef0123456789abcdef 100", "bfme2:ini.big fedcba9876543210fedcba9876543210 200" };
	return o;
}
} // namespace

TEST_CASE("profile2 lobby client: a lobby with the archives feature is sent the archive list once, then a join names it by its SHA-256")
{
	const std::string id = OnlineLobbyClient::archivesId(archiveOptions().profileArchives);
	CHECK(id.size() == 64);
	OnlineLobbyClient c(archiveOptions());
	c.useSession("t.t");
	drive(c, R"(["social","archives"])");
	c.joinGame("0123456789abcdef", "");
	auto rs = drive(c, R"(["social","archives"])");
	std::vector<std::string> paths;
	for (const auto &r : rs)
	{
		paths.push_back(r.path);
	}
	REQUIRE(paths.size() >= 2); // tolerance: the social session's own requests (presence, inbox) may follow the two this test reads
	CHECK(paths[0] == "/v1/profile-archives");
	CHECK(rs[0].body == "{\"id\":\"" + id + "\",\"lines\":[\"rotwk:INI.big 0123456789abcdef0123456789abcdef 100\",\"bfme2:ini.big fedcba9876543210fedcba9876543210 200\"]}");
	CHECK(paths[1] == "/v1/games/0123456789abcdef/join");
	CHECK(rs[1].body.find(",\"archives\":\"" + id + "\"") != std::string::npos);
	CHECK(c.lastArchivesError().empty());
	// a second join: the lobby holds the list already
	c.joinGame("0123456789abcdef", "");
	rs = drive(c, R"(["social","archives"])");
	REQUIRE(!rs.empty());
	CHECK(rs[0].path == "/v1/games/0123456789abcdef/join");
	CHECK(rs[0].body.find(id) != std::string::npos);
}

TEST_CASE("profile2 lobby client: a client without the social session still learns the lobby's archives feature")
{
	OnlineLobbyClient::Options o = archiveOptions();
	o.enableSocial = false; // no install id: the social session never starts (OnlineSocialClient), the upload still does
	OnlineLobbyClient c(o);
	c.useSession("t.t");
	drive(c, R"(["archives"])");
	c.joinGame("0123456789abcdef", "");
	auto rs = drive(c, R"(["archives"])");
	REQUIRE(rs.size() == 2);
	CHECK(rs[0].path == "/v1/profile-archives");
	CHECK(rs[1].body.find("\"archives\"") != std::string::npos);
}

TEST_CASE("profile2 lobby client: a failed upload is kept and retried, never a refusal; an older lobby is sent nothing")
{
	OnlineLobbyClient c(archiveOptions());
	c.useSession("t.t");
	drive(c, R"(["social","archives"])");
	c.takeEvents();
	c.joinGame("0123456789abcdef", "");
	auto rs = c.takeRequests();
	REQUIRE(!rs.empty());
	REQUIRE(rs[0].path == "/v1/profile-archives");
	c.deliver(rs[0].id, 413, R"({"v":1,"error":"too_big","message":"x"})", "");
	for (const auto &e : c.takeEvents())
	{
		CHECK_FALSE((e.kind == OnlineLobbyClient::Event::Refused && e.request == "archives"));
	}
	CHECK(c.lastArchivesError() == "HTTP 413");
	drive(c, R"(["social","archives"])");
	c.joinGame("0123456789abcdef", "");
	rs = drive(c, R"(["social","archives"])");
	REQUIRE(!rs.empty());
	CHECK(rs[0].path == "/v1/profile-archives"); // sent again

	OnlineLobbyClient old(archiveOptions());
	old.useSession("t.t");
	drive(old, R"(["social"])");
	old.joinGame("0123456789abcdef", "");
	rs = drive(old, R"(["social"])");
	REQUIRE(!rs.empty());
	CHECK(rs[0].path == "/v1/games/0123456789abcdef/join");
	CHECK(rs[0].body.find("archives") == std::string::npos);
}
