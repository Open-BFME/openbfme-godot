// OpenBFME unit tests. GPL-3.0.
// Lane RELEASE-1: the privacy filter of the session log (Common/LogPrivacy.h) and the version line (Common/BuildVersion.h).

#include "doctest.h"

#include "Common/BuildVersion.h"
#include "Common/LogPrivacy.h"

#include <cstring>
#include <string>

using LogPrivacy::redact;
using LogPrivacy::Rules;

TEST_CASE("release1: the home folder becomes ~, the user name <user>")
{
	Rules r;
	r.home = "/srv/players/tester";
	r.user = "tester";
	CHECK(redact("mount: /srv/players/tester/Games/RotWK/INI.big", r) == "mount: ~/Games/RotWK/INI.big");
	CHECK(redact("user data /srv/players/tester/.local/share/godot/app_userdata/OpenBFME/logs", r) == "user data ~/.local/share/godot/app_userdata/OpenBFME/logs");
	CHECK(redact("the home folder alone: /srv/players/tester", r) == "the home folder alone: ~");
	CHECK(redact("\"/srv/players/tester\"", r) == "\"~\"");
	// another user's folder that starts with the same letters is left alone; the name as a whole path component elsewhere is removed
	CHECK(redact("/srv/players/testers/x", r) == "/srv/players/testers/x");
	CHECK(redact("/run/media/tester/sdcard/RotWK", r) == "/run/media/<user>/sdcard/RotWK");
	// the name as a bare token goes too (review r1: "user=deck" survived); part of a longer word stays
	CHECK(redact("the tester clicked Start (tester's choice)", r) == "the <user> clicked Start (<user>'s choice)");
	CHECK(redact("testers and protester", r) == "testers and protester");
	// several occurrences, a trailing separator in HOME
	r.home = "/srv/players/tester/";
	CHECK(redact("/srv/players/tester/a and /srv/players/tester/b", r) == "~/a and ~/b");
}

TEST_CASE("release1: names end at any non-name character (review r1)")
{
	Rules r;
	r.home = "/srv/players/tester";
	r.user = "tester";
	CHECK(redact("home=/srv/players/tester;user=tester", r) == "home=~;user=<user>");
	CHECK(redact("[home=/srv/players/tester]", r) == "[home=~]");
	CHECK(redact("{\"home\":\"/srv/players/tester/x\"}", r) == "{\"home\":\"~/x\"}");
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
	CHECK(redact("/srv/a/x", r) == "~/x");
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
	CHECK(redact("C:\\Users\\Ann Smith\\AppData\\Roaming\\Godot\\app_userdata\\OpenBFME\\logs\\x.log", r) == "~\\AppData\\Roaming\\Godot\\app_userdata\\OpenBFME\\logs\\x.log");
	CHECK(redact("c:/users/ann smith/AppData/Roaming", r) == "~/AppData/Roaming");
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
	CHECK(LogPrivacy::redactFinal("bye /srv/players/tester", r) == "bye ~");
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
