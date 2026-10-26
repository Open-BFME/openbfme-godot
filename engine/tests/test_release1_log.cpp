// OpenBFME unit tests. GPL-3.0.
// Lane RELEASE-1: the privacy filter of the session log (Common/LogPrivacy.h) and the version line (Common/BuildVersion.h).

#include "doctest.h"

#include "Common/BuildVersion.h"
#include "Common/LogPrivacy.h"

#include <cstring>
#include <string>
#include <vector>

using LogPrivacy::redact;
using LogPrivacy::Rules;

TEST_CASE("release1: shared diagnostics omit credentials and player content")
{
	const std::vector<std::string> fields = { "Authorization: Bearer synthetic value", "cookie: session=synthetic", "password=two words",
		"access_token=synthetic", "refresh-token: synthetic", "sessionKey=synthetic", "api_key='two words'", "secret=synthetic",
		"player_name=Fixture Player", "displayName=Fixture Player", "GAME ONLINE signed in as Fixture Player (guest)",
		"GAME ONLINE chat Fixture Player: arbitrary file contents", "session key=synthetic", "authToken=synthetic",
		"player name=Fixture Player", "GAME ONLINE remembered name Fixture Player: saved", "GAME ONLINE WALK games: Fixture Player" };
	for (const auto &field : fields)
	{
		const auto clean = redact(field, {});
		CHECK(clean.find(LogPrivacy::kOmitted) != std::string::npos);
		for (const char *privateValue : { "synthetic", "two words", "Fixture Player", "arbitrary file contents" })
			CHECK(clean.find(privateValue) == std::string::npos);
		CHECK(redact(clean, {}) == clean);
	}
	CHECK(redact("ok\nAuthorization: synthetic\nend", {}) == std::string("ok\nAuthorization: ") + LogPrivacy::kOmitted + "\nend");
	CHECK(redact("contact fixture@example.invalid at 192.0.2.10 or [2001:db8::10]", {}) == "contact <address> at <address> or [<address>]");
	CHECK(redact("connect https://fixture.invalid/path?value=synthetic", {}) == "connect https://fixture.invalid/path?value=synthetic");
	CHECK(redact("12:34:56 std::function", {}) == "12:34:56 std::function");
	CHECK(redact("ERROR: token: synthetic; retry failed", {}) == std::string("ERROR: token: ") + LogPrivacy::kOmitted + "; retry failed");
	CHECK(redact("response eyJmaXh0dXJlIjoiMSJ9.eyJmaXh0dXJlIjoyfQ.c3ludGhldGlj failed", {}) == std::string("response ") + LogPrivacy::kOmitted + " failed");
	CHECK(redact("value=abcdefghijklmnopqrstuvwxyz0123456789", {}) == "value=abcdefghijklmnopqrstuvwxyz0123456789");
}

TEST_CASE("release1: hashes source paths parser diagnostics and labelled versions survive")
{
	for (const char *text : { "commit 0123456789abcdef0123456789abcdef01234567", "engine id abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcd",
		"engine/src/GameClient/AnimationSoundClientBehavior.cpp:322", "res://scripts/parser.gd:7 Unexpected token in expression",
		"Unexpected token: '}'", "version 1.2.3.4", "Godot 4.7.2.0", "v1.2.3.4", "version 999.999.999.999" })
		CHECK(redact(text, {}) == text);
	CHECK(redact("version 1.2.3.4 peer 192.0.2.10 port 3000", {}) == "version 1.2.3.4 peer <address> port 3000");
	CHECK(redact("peer=1.2.3.4", {}) == "peer=<address>");
	CHECK(redact("peer 192.0.2.10.", {}) == "peer <address>.");
	CHECK(redact("connect https://Fixture:synthetic@fixture.invalid/path", {}) == std::string("connect https://") + LogPrivacy::kOmitted + "@fixture.invalid/path");
	CHECK(redact("https://fixture.invalid/log?access_token=synthetic&version=1.2.3.4", {}) == std::string("https://fixture.invalid/log?access_token=") + LogPrivacy::kOmitted + "&version=1.2.3.4");
}

TEST_CASE("release1: Godot error messages use code when rationale is empty and redact only private values")
{
	Rules rules;
	rules.home = "/srv/players/Fixture Player";
	rules.user = "Fixture Player";
	for (const char *message : { "Index 7 is out of bounds (size = 2)", "push_error fixture", "push_warning fixture", "Unexpected token in expression" })
		CHECK(LogPrivacy::errorMessage(message, "", rules) == message);
	CHECK(LogPrivacy::errorMessage("Index 7", "Bounded error explanation", rules) == "Bounded error explanation");
	CHECK(LogPrivacy::errorMessage("Load /srv/players/Fixture Player/log; token='synthetic'; retry failed", "", rules) ==
		std::string("Load <user>/log; token='") + LogPrivacy::kOmitted + "'; retry failed");
	const auto longMessage = LogPrivacy::errorMessage("Index out of bounds " + std::string(LogPrivacy::kMaxFieldBytes, 'x'), "", rules);
	CHECK(longMessage == "Index out of bounds [truncated]");
	CHECK(redact("SHADER ERROR: " + longMessage, rules) == "SHADER ERROR: " + longMessage);
	const auto oneWord = LogPrivacy::errorMessage(std::string(LogPrivacy::kMaxFieldBytes + 1, 'x'), "", rules);
	CHECK(oneWord == std::string(LogPrivacy::kMaxFieldBytes - 32, 'x') + " [truncated]");
	CHECK(redact("ERROR: " + oneWord, rules) == "ERROR: " + oneWord);
}

TEST_CASE("release1: reports redact every synthetic profile path even without host environment")
{
	const std::string name = "Fixture Player";
	Rules rules;
	rules.home = "/srv/players/" + name;
	rules.user = name;
	const std::vector<std::string> paths = { rules.home + "/logs/crash.log", "/home/" + name + "/Games", "/Users/" + name + "/Library",
		"C:\\Users\\" + name + "\\AppData", "d:/users/" + name + "/Games", "/media/" + name + "/Games" };
	for (const auto &path : paths)
	{
		const auto report = redact("ERROR failed to load " + path + "\nusername=" + name, rules);
		CHECK(report.find(name) == std::string::npos);
		CHECK(report.find(rules.home) == std::string::npos);
		CHECK(report.find("<user>") != std::string::npos);
	}
	CHECK(redact("/" "home/Fixture/Games", {}) == "<user>/Games");
	CHECK(redact("C:/" "Users/Fixture/Games", {}) == "<user>/Games");
}

TEST_CASE("release1: maximum-sized locations cannot exhaust the privacy filter stack")
{
	for (const auto &prefix : std::vector<std::string>{ "https://", std::string("/") + "home/", "C:\\Users\\" })
	{
		const std::string text = prefix + std::string(LogPrivacy::kMaxFieldBytes - prefix.size(), 'x');
		CHECK(redact(text, {}) == (prefix == "https://" ? text : "<user>"));
	}
	const std::string prefix = "2001:db8::1%";
	CHECK(redact(prefix + std::string(LogPrivacy::kMaxFieldBytes - prefix.size(), 'x'), {}) == "<address>");
}

TEST_CASE("release1: diagnostic fields and whole inputs are bounded without a raw prefix")
{
	CHECK(redact(std::string(LogPrivacy::kMaxFieldBytes + 1, 'x') + "password=fixture", {}) == LogPrivacy::kOversized);
	CHECK(redact(std::string(LogPrivacy::kMaxLogBytes + 1, 'x'), {}) == LogPrivacy::kOversized);
	std::string boundary;
	for (size_t i = 0; i < LogPrivacy::kMaxFieldBytes / 2; ++i)
		boundary += "x ";
	CHECK(redact(boundary, {}) == boundary);
}

TEST_CASE("release1: the session budget retains the head and recent records beyond multiple rotations")
{
	LogPrivacy::Budget budget;
	std::FILE *file = std::tmpfile();
	REQUIRE(file != nullptr);
	const std::string header = "OpenBFME version 1.2.3.4 commit 0123456789abcdef0123456789abcdef01234567";
	REQUIRE(budget.write(file, header, {}));
	const std::string line(8000, 'x');
	for (int i = 0; i < 400; ++i)
	{
		REQUIRE(budget.write(file, "record " + std::to_string(i) + " " + line, {}));
		REQUIRE(budget.written <= LogPrivacy::kMaxLogBytes); // tolerance: the hard file-size cap must hold at every write, regardless of record boundaries.
	}
	REQUIRE(budget.write(file, "ERROR final crash /" "home/Fixture/log; session_key=synthetic", {}));
	const auto snapshot = budget.snapshot();
	CHECK(snapshot.find(header + "\n") == 0);
	CHECK(snapshot.find("record 399 ") != std::string::npos);
	CHECK(snapshot.find("record 100 ") == std::string::npos);
	CHECK(snapshot.find("ERROR final crash <user>/log; session_key=") != std::string::npos);
	CHECK(snapshot.find("synthetic") == std::string::npos);
	CHECK(snapshot.size() == budget.written);
	REQUIRE(std::fseek(file, 0, SEEK_END) == 0);
	CHECK((size_t)std::ftell(file) == snapshot.size());
	std::rewind(file);
	std::string disk(snapshot.size(), '\0');
	REQUIRE(std::fread(disk.data(), 1, disk.size(), file) == disk.size());
	CHECK(disk == snapshot);
	std::fclose(file);
}

TEST_CASE("release1: the home folder and user name become <user>")
{
	Rules r;
	r.home = "/srv/players/tester";
	r.user = "tester";
	CHECK(redact("mount: /srv/players/tester/Games/RotWK/INI.big", r) == "mount: <user>/Games/RotWK/INI.big");
	CHECK(redact("user data /srv/players/tester/.local/share/godot/app_userdata/OpenBFME/logs", r) == "user data <user>/.local/share/godot/app_userdata/OpenBFME/logs");
	CHECK(redact("the home folder alone: /srv/players/tester", r) == "the home folder alone: <user>");
	CHECK(redact("\"/srv/players/tester\"", r) == "\"<user>\"");
	// another user's folder that starts with the same letters is left alone; the name as a whole path component elsewhere is removed
	CHECK(redact("/srv/players/testers/x", r) == "/srv/players/testers/x");
	CHECK(redact("/run/media/tester/sdcard/RotWK", r) == "/run/media/<user>/sdcard/RotWK");
	// the name as a bare token goes too (review r1: "user=deck" survived); part of a longer word stays
	CHECK(redact("the tester clicked Start (tester's choice)", r) == "the <user> clicked Start (<user>'s choice)");
	CHECK(redact("testers and protester", r) == "testers and protester");
	// several occurrences, a trailing separator in HOME
	r.home = "/srv/players/tester/";
	CHECK(redact("/srv/players/tester/a and /srv/players/tester/b", r) == "<user>/a and <user>/b");
}

TEST_CASE("release1: names end at any non-name character (review r1)")
{
	Rules r;
	r.home = "/srv/players/tester";
	r.user = "tester";
	CHECK(redact("home=/srv/players/tester;user=tester", r) == "home=<user>;user=<user>");
	CHECK(redact("[home=/srv/players/tester]", r) == "[home=<user>]");
	CHECK(redact("{\"home\":\"/srv/players/tester/x\"}", r) == "{\"home\":\"<user>/x\"}");
	CHECK(redact("username=tester&next", r) == "username=<user>&next");
	// a longer path that merely contains the home is not the home (its user name component still goes)
	CHECK(redact("/backup/srv/players/tester/a", r) == "/backup/srv/players/<user>/a");
	CHECK(redact("/srv/players/tester.old/a", r) == "/srv/players/tester.old/a");
}

TEST_CASE("release1: one- and two-character user names, everywhere they stand as a token (review r1, r2)")
{
	Rules r;
	r.home = "/srv/a";
	r.user = "a";
	CHECK(redact("username=a", r) == "username=<user>");
	CHECK(redact("/mnt/a/private", r) == "/mnt/<user>/private");
	CHECK(redact("~a/private", r) == "~<user>/private");
	CHECK(redact("user:a", r) == "user:<user>");
	CHECK(redact("/srv/a/x", r) == "<user>/x");
	// conservative (review r2): the word itself goes too; longer words that contain it stay
	CHECK(redact("a unit has a sword, data/abc", r) == "<user> unit has <user> sword, data/abc");
	r.user = "jo";
	CHECK(redact("/home2/jo/x and jo said", r) == "/home2/<user>/x and <user> said");
	CHECK(redact("username=\"jo\"", r) == "username=\"<user>\"");
	CHECK(redact("user: jo", r) == "user: <user>");
	CHECK(redact("user:\tjo,", r) == "user:\t<user>,");
	CHECK(redact("hello jo.", r) == "hello <user>.");
	CHECK(redact("--jo and /.jo", r) == "--<user> and /.<user>");
	CHECK(redact("jo.old jon joe major", r) == "jo.old jon joe major");
}

TEST_CASE("release1: Windows spellings, case-insensitive")
{
	Rules r;
	r.home = "C:\\Users\\Ann Smith";
	r.user = "Ann Smith";
	r.caseInsensitive = true;
	CHECK(redact("C:\\Users\\Ann Smith\\AppData\\Roaming\\Godot\\app_userdata\\OpenBFME\\logs\\x.log", r) == "<user>\\AppData\\Roaming\\Godot\\app_userdata\\OpenBFME\\logs\\x.log");
	CHECK(redact("c:/users/ann smith/AppData/Roaming", r) == "<user>/AppData/Roaming");
	CHECK(redact("D:\\Backups\\ANN SMITH\\RotWK", r) == "D:\\Backups\\<user>\\RotWK");
	// a short user name wherever it is a token (review r2)
	r.user = "a";
	CHECK(redact("D:\\a\\b", r) == "D:\\<user>\\b");
	CHECK(redact("a b", r) == "<user> b");
}

TEST_CASE("release1: a cut that never splits a name, the last text of a stream (review r4)")
{
	Rules r;
	r.home = "/srv/players/tester";
	r.user = "tester";
	// the r3 leak: a cut through the complete home folder
	const std::string sol = "home=/srv/players/tester/";
	const size_t c = LogPrivacy::safeCut(sol, r);
	CHECK((c <= sol.find("/srv") || c >= sol.find("/srv") + r.home.size())); // never inside the match
	CHECK(redact(sol.substr(0, c), r).find("/srv") == std::string::npos);
	CHECK(LogPrivacy::safeCut("loading 50%", r) == 11);
	CHECK(LogPrivacy::safeCut("path /srv/players/te", r) == 5);
	CHECK(LogPrivacy::safeCut("user=tes", r) == 5);
	CHECK(LogPrivacy::safeCut("a tester b", r) == 10);
	// the last text of a stream: complete names are scrubbed, a name that never completed is not written
	CHECK(LogPrivacy::redactFinal("user=tester", r) == "user=<user>");
	CHECK(LogPrivacy::redactFinal("user=tes", r) == "user=\xE2\x80\xA6");
	CHECK(LogPrivacy::redactFinal("path /srv/players/te", r) == "path \xE2\x80\xA6");
	CHECK(LogPrivacy::redactFinal("bye /srv/players/tester", r) == "bye <user>");
	CHECK(LogPrivacy::redactFinal("loading", r) == "loading");
}

TEST_CASE("release1: nothing to redact, empty rules")
{
	Rules r;
	CHECK(redact("/srv/x/y", r) == "/srv/x/y");
	r.home = "/";
	CHECK(redact("/srv/x/y", r) == "/srv/x/y"); // "/" as home would rewrite every path: ignored
}

TEST_CASE("release1: the version line")
{
	const std::string line = BuildVersion::versionLine();
	CHECK(line.rfind("OpenBFME ", 0) == 0);
	CHECK(std::strlen(BuildVersion::kVersion) > 0);
	CHECK(std::strlen(BuildVersion::kBuildDate) == 10); // YYYY-MM-DD
	CHECK(line == std::string("OpenBFME ") + BuildVersion::kVersion + " (" + BuildVersion::kBuildDate + ")");
	MESSAGE("version line: " << line << ", commit " << std::string(BuildVersion::kCommit));
}

TEST_CASE("version1: the version players read")
{
	using BuildVersion::displayVersion;
	CHECK(displayVersion("v0.4.5") == "Alpha 0.4.5");
	CHECK(displayVersion("v0.5.0") == "Alpha 0.5.0");
	CHECK(displayVersion("v10.20.300") == "Alpha 10.20.300");
	CHECK(displayVersion("v0.4.0-alpha.3") == "v0.4.0 Alpha 3");
	CHECK(displayVersion("v0.4.0-alpha.12") == "v0.4.0 Alpha 12");
	CHECK(displayVersion("v0.4.13-nightly.20261022") == "Nightly 0.4.13 (2026-10-22)");
	CHECK(displayVersion("v0.4.13-nightly.20260101") == "Nightly 0.4.13 (2026-01-01)");
	CHECK(displayVersion("v0.4.13-nightly.20261322") == "Nightly 0.4.13 build 20261322");
	CHECK(displayVersion("v0.4.13-nightly.7") == "Nightly 0.4.13 build 7");
	// as they are: previews, development builds, and anything outside the launcher's release grammar
	for (const char *same : {"v0.3.0-preview.6", "v0.4.5-3-g0123456789", "0af8b6b84c-dirty", "untracked", "", "v", "v0.4", "v0.4.", "v0.4.5.",
							 "v0.04.5", "v0.4.5-alpha.01", "v0.4.5-alpha.", "v0.4.5-alpha.1x", "v0.4.5-nightly.", "v0.4.5-nightly.01", "v0.4.5-beta.1", "V0.4.5", "v0.4.5 ",
							 "v1234567890.0.0", "v0.4.-1"})
		CHECK_MESSAGE(displayVersion(same) == same, same);
	const std::string line = BuildVersion::displayLine();
	CHECK(line == std::string("OpenBFME ") + displayVersion(BuildVersion::kVersion) + " (" + BuildVersion::kBuildDate + ")");
}
