// OpenBFME unit tests: lane CAMP-2, GameData's Bool fields (PlayIntro, RW 0xC00540: the start-up movies; ShellMapOn). GPL-3.0.

#include "doctest.h"

#include "GameClient/GUI/Shell/Shell.h"

#include <string>

TEST_CASE("camp2 GameData Bool fields: PlayIntro and ShellMapOn read from the GameData block only; a bad value is an error")
{
	bool value = false, found = false;
	std::string error;
	CHECK(Shell::readGameDataBool("Weapon X\n PlayIntro = No\nEnd\nGameData\n  PlayIntro = Yes ; c\nEnd\n", "PlayIntro", value, found, &error));
	CHECK(found);
	CHECK(value);
	CHECK(Shell::readGameDataBool("GameData\n  ShellMapOn = No\nEnd\n", "PlayIntro", value, found, &error));
	CHECK_FALSE(found);
	CHECK_FALSE(Shell::readGameDataBool("GameData\n  playintro = Maybe\nEnd\n", "PlayIntro", value, found, &error));
	CHECK(error.find("PlayIntro") != std::string::npos);
}
