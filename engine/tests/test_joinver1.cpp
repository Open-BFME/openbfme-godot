// OpenBFME. GPL-3.0.
//
// Lane JOINVER-1: a join refused for a different build, in plain words (GameNetwork/JoinBuild.h), and what the lobby client tells and reads of
// builds (OnlineLobbyClient: the create / join bodies' `build`, the list rows' host build and profile, the 409 "profile"'s both builds).
#include "doctest.h"

#include "GameNetwork/JoinBuild.h"
#include "GameNetwork/OnlineLobbyClient.h"

#include <string>

namespace
{
JoinBuild::Build build(const std::string &version, const std::string &date, int official, const std::string &platform)
{
	JoinBuild::Build b;
	b.version = version;
	b.date = date;
	b.official = official;
	b.platform = platform;
	return b;
}

OnlineLobbyClient::Options options(const std::string &version)
{
	OnlineLobbyClient::Options o;
	o.profileDigest = "0123456789abcdef0123456789abcdef";
	o.playerTemplateCount = 4;
	o.colorCount = 8;
	o.clientVersion = version;
	o.buildDate = "2026-10-22";
	o.buildOfficial = 1;
	o.platform = PlatformKind::Windows;
	return o;
}
} // namespace

TEST_CASE("joinver1: a build as players read it: version name, official or not, system")
{
	CHECK(JoinBuild::describe(build("v0.4.12-nightly.20261022", "2026-10-22", 1, "windows")) == "Nightly 0.4.12 (2026-10-22), official release, Windows");
	CHECK(JoinBuild::describe(build("v0.4.11", "2026-10-18", 0, "linux")) == "Alpha 0.4.11 (2026-10-18), unofficial, Linux");
	CHECK(JoinBuild::describe(build("v0.4.5-3-g0123456789-dirty", "2026-10-20", 0, "macos")) ==
		"a development build (v0.4.5-3-g0123456789-dirty, built 2026-10-20), unofficial, macOS");
	CHECK(JoinBuild::describe(build("v0.4.11", "", -1, "")) == "Alpha 0.4.11");
	CHECK(JoinBuild::describe(JoinBuild::Build{}) == "an unknown version");
	CHECK(JoinBuild::releaseTag("v0.4.12-nightly.20261022"));
	CHECK(JoinBuild::releaseTag("v0.4.0-alpha.3"));
	CHECK_FALSE(JoinBuild::releaseTag("v0.4.5-3-g0123456789"));
	CHECK_FALSE(JoinBuild::releaseTag("untracked"));
	CHECK_FALSE(JoinBuild::releaseTag("v0.04.5"));
}

TEST_CASE("joinver1: versions are ordered as releases: a nightly before its plain release, numbers before anything")
{
	bool comparable = false;
	CHECK(JoinBuild::compare("v0.4.12", "v0.4.11", &comparable) == 1);
	CHECK(comparable);
	CHECK(JoinBuild::compare("v0.4.12-nightly.20261022", "v0.4.12", &comparable) == -1);
	CHECK(JoinBuild::compare("v0.4.12-nightly.20261023", "v0.4.12-nightly.20261022", &comparable) == 1);
	CHECK(JoinBuild::compare("v0.4.12", "v0.4.12", &comparable) == 0);
	CHECK(comparable);
	CHECK(JoinBuild::compare("v0.4.12", "v0.4.5-3-g0123456789", &comparable) == 0);
	CHECK_FALSE(comparable);
}

TEST_CASE("joinver1: the refusal says both builds and what to do; the profile ids only in the details")
{
	const JoinBuild::Build host = build("v0.4.12-nightly.20261022", "2026-10-22", 1, "windows");
	const JoinBuild::Build mine = build("v0.4.11", "2026-10-18", 1, "linux");
	CHECK(JoinBuild::refusalText(mine, host) ==
		"You can't join this game: it runs a different OpenBFME version.\n"
		"Host: Nightly 0.4.12 (2026-10-22), official release, Windows\n"
		"You: Alpha 0.4.11 (2026-10-18), official release, Linux\n"
		"The host has a newer version: update OpenBFME through the launcher, then join again.");
	CHECK(JoinBuild::refusalText(host, mine).find("The host is on an older version: the host needs to update through the launcher.") != std::string::npos);
	const JoinBuild::Build selfBuilt = build("v0.4.11", "2026-10-19", 0, "linux");
	CHECK(JoinBuild::refusalText(selfBuilt, mine).find("One of you runs an unofficial build") != std::string::npos);
	CHECK(JoinBuild::refusalText(mine, mine).find("The versions match, but the game files differ") != std::string::npos);
	CHECK(JoinBuild::refusalText(mine, JoinBuild::Build{}).find("Host: an unknown version\n") != std::string::npos);
	CHECK(JoinBuild::refusalText(mine, JoinBuild::Build{}).find("ask the host which one they have") != std::string::npos);
	CHECK(JoinBuild::refusalText(build("v0.4.5-3-g01", "", 0, "linux"), mine).find("install the official release through the launcher") != std::string::npos);
	const std::string details = JoinBuild::detailsText(mine, "0123456789abcdef0123456789abcdef", host, "fedcba9876543210fedcba9876543210");
	CHECK(details == "Host: v0.4.12-nightly.20261022, built 2026-10-22, windows, official, profile fedcba9876543210fedcba9876543210\n"
					 "You: v0.4.11, built 2026-10-18, linux, official, profile 0123456789abcdef0123456789abcdef");
	CHECK(JoinBuild::refusalText(mine, host).find("0123456789abcdef") == std::string::npos);
}

TEST_CASE("joinver1: the lobby client tells its build at create and join, reads the host's from the list and both from a 409 profile")
{
	OnlineLobbyClient c(options("v0.4.11"));
	c.useSession("t.t");
	auto rs = c.takeRequests();
	REQUIRE(rs.size() == 1);
	c.deliver(rs[0].id, 200, R"({"v":1,"games":[]})", "");
	c.takeEvents();
	c.joinGame("0123456789abcdef", "");
	rs = c.takeRequests();
	REQUIRE(rs.size() == 1);
	CHECK(rs[0].body == R"({"profile":"0123456789abcdef0123456789abcdef","mode":"normal","platform":"windows","build":{"version":"v0.4.11","date":"2026-10-22","official":true,"platform":"windows"}})");
	c.deliver(rs[0].id, 409, R"j({"v":1,"error":"profile","message":"You can't join this game: it runs a different OpenBFME version (host: Nightly 0.4.12 (2026-10-22), official, Windows; you: Alpha 0.4.11, official, Windows). Both players need the same version: update through the launcher.","host_build":{"version":"v0.4.12-nightly.20261022","date":"2026-10-22","official":true,"platform":"windows"},"your_build":{"version":"v0.4.11","date":"2026-10-22","official":true,"platform":"windows"},"host_profile":"fedcba9876543210fedcba9876543210","your_profile":"0123456789abcdef0123456789abcdef"})j", "");
	const auto &r = c.profileRefusal();
	REQUIRE(r.valid);
	CHECK(r.host.version == "v0.4.12-nightly.20261022");
	CHECK(r.host.official == 1);
	CHECK(r.host.platform == "windows");
	CHECK(r.hostProfile == "fedcba9876543210fedcba9876543210");
	CHECK(r.yours.version == "v0.4.11");
	bool refused = false;
	for (const auto &e : c.takeEvents())
	{
		if (e.kind == OnlineLobbyClient::Event::Refused && e.code == "profile") refused = true;
	}
	CHECK(refused);
	// an older lobby: the refusal names no builds (valid false: the screens use the listed game's, else unknown)
	c.joinGame("0123456789abcdef", "");
	rs = c.takeRequests();
	REQUIRE(rs.size() == 1);
	c.deliver(rs[0].id, 409, R"j({"v":1,"error":"profile","message":"your game files or build differ from the host's (profile a vs b)"})j", "");
	CHECK_FALSE(c.profileRefusal().valid);
	// the list's host build and profile (an older lobby's row has neither)
	c.refreshList(false);
	rs = c.takeRequests();
	REQUIRE(rs.size() == 1);
	c.deliver(rs[0].id, 200, R"({"v":1,"games":[{"id":"1111111111111111","name":"New","host":"Ann","map":"m","players":1,"openSlots":1,"state":"open","password":false,"profile":"fedcba9876543210fedcba9876543210","build":{"version":"v0.4.12","date":"","official":false,"platform":"linux"}},{"id":"2222222222222222","name":"Old","host":"Bea","map":"m","players":1,"openSlots":1,"state":"open","password":false}]})", "");
	REQUIRE(c.games().size() == 2);
	CHECK(c.games()[0].profile == "fedcba9876543210fedcba9876543210");
	CHECK(c.games()[0].build.version == "v0.4.12");
	CHECK(c.games()[0].build.official == 0);
	CHECK(c.games()[1].profile.empty());
	CHECK_FALSE(c.games()[1].build.known());
	// a version the lobby would refuse is not told (the join never fails on it); no build day or official flag told: left out
	OnlineLobbyClient odd(options("v0.4.11 local"));
	odd.useSession("t.t");
	rs = odd.takeRequests();
	odd.deliver(rs[0].id, 200, R"({"v":1,"games":[]})", "");
	odd.joinGame("0123456789abcdef", "");
	rs = odd.takeRequests();
	REQUIRE(rs.size() == 1);
	CHECK(rs[0].body.find("\"build\"") == std::string::npos);
	OnlineLobbyClient::Options plain = options("v0.4.11");
	plain.buildDate.clear();
	plain.buildOfficial = -1;
	OnlineLobbyClient untold(plain);
	untold.useSession("t.t");
	rs = untold.takeRequests();
	untold.deliver(rs[0].id, 200, R"({"v":1,"games":[]})", "");
	untold.joinGame("0123456789abcdef", "");
	rs = untold.takeRequests();
	REQUIRE(rs.size() == 1);
	CHECK(rs[0].body.find(R"("build":{"version":"v0.4.11","platform":"windows"})") != std::string::npos);
}
