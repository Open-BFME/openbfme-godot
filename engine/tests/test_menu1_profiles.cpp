// OpenBFME unit tests. GPL-3.0.
//
// Lane MENU-1: Skirmish.ini, the profiles a player made (GameClient/GUI/Skirmish/SkirmishPreferences.h): RotWK's quoted-printable text and the
// UserNames / CurrentUserName entries, kept across starts.

#include "doctest.h"

#include "GameClient/GUI/Skirmish/SkirmishPreferences.h"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace
{
std::string tempIni(const char *name)
{
	const std::filesystem::path p = std::filesystem::temp_directory_path() / (std::string("openbfme-menu1-") + name + ".ini");
	std::filesystem::remove(p);
	return p.string();
}

std::string readAll(const std::string &path)
{
	std::ifstream f(path, std::ios::binary);
	std::stringstream ss;
	ss << f.rdbuf();
	return ss.str();
}
} // namespace

TEST_CASE("menu1 profiles: quoted-printable is RotWK's (RW 0x93BD82 / 0x93BF28): each UTF-16 unit low byte first, a non-alphanumeric byte as _XX")
{
	CHECK(SkirmishPreferences::unicodeToQuotedPrintable(u"asdasd") == "a_00s_00d_00a_00s_00d_00");
	CHECK(SkirmishPreferences::unicodeToQuotedPrintable(u"Gimli,") == "G_00i_00m_00l_00i_00_2C_00");
	CHECK(SkirmishPreferences::unicodeToQuotedPrintable(u"é x") == "_E9_00_20_00x_00");
	CHECK(SkirmishPreferences::quotedPrintableToUnicode("a_00s_00d_00a_00s_00d_00") == u"asdasd");
	CHECK(SkirmishPreferences::quotedPrintableToUnicode("_E9_00_20_00x_00") == u"é x");
	CHECK(SkirmishPreferences::quotedPrintableToUnicode("_e9_00") == u"é"); // lower-case digits read too (RW 0x93BD52)
	CHECK(std::string(SkirmishPreferences::unverified()).find("[S-3504]") == 0);
}

TEST_CASE("menu1 profiles: a profile made on the first run is the current one at the next start; names compare without case; removal moves current")
{
	const std::string path = tempIni("profiles");
	{
		SkirmishPreferences first(path);
		CHECK(first.errors().empty());
		CHECK(first.names().empty());
		CHECK(first.current().empty());
		REQUIRE(first.add(u"asdasd"));
		REQUIRE(first.setCurrent(u"asdasd"));
		CHECK_FALSE(first.add(u"ASDASD"));
		CHECK_FALSE(first.add(u""));
		CHECK(first.errors().empty());
	}
	const std::string text = readAll(path);
	CHECK(text.find("CurrentUserName = a_00s_00d_00a_00s_00d_00") != std::string::npos);
	CHECK(text.find("UserNames = a_00s_00d_00a_00s_00d_00_2C_00") != std::string::npos);
	{
		SkirmishPreferences again(path);
		CHECK(again.names() == std::vector<std::u16string>{ u"asdasd" });
		CHECK(again.current() == u"asdasd");
		REQUIRE(again.add(u"Gimli"));
		REQUIRE(again.setCurrent(u"Gimli"));
		REQUIRE(again.remove(u"Gimli"));
		CHECK(again.current() == u"asdasd");
	}
	SkirmishPreferences third(path);
	CHECK(third.names() == std::vector<std::u16string>{ u"asdasd" });
	CHECK(third.current() == u"asdasd");
	std::filesystem::remove(path);
}

TEST_CASE("menu1 profiles: a Skirmish.ini that cannot be inspected or read is an error, never a first run")
{
	// a path through a regular file: the folder cannot be inspected (ENOTDIR)
	const std::string blocker = tempIni("blocker");
	{
		std::ofstream f(blocker);
		f << "x";
	}
	SkirmishPreferences prefs(blocker + "/Skirmish.ini");
	CHECK_FALSE(prefs.errors().empty());
	std::filesystem::remove(blocker);
}
