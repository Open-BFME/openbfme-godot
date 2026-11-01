// OpenBFME tests: extended player presentation, without changing the existing UI tests. GPL-3.0.

#include "doctest.h"
#include "Common/ExtendedPlayerColors.h"

#include <set>
#include <string>

TEST_CASE("p16 ui: extra colours are available only on extended maps and leave retail indices alone")
{
	const std::array<ExtendedPlayerColors::Color, 6> expected{{
		{ "Cyan", 0x00DCDCu },
		{ "Magenta", 0xE600ACu },
		{ "Lime", 0xB7F000u },
		{ "Yellow", 0xFFE119u },
		{ "Navy", 0x16255Cu },
		{ "Brown", 0x633B20u }
	}};
	for (int starts = 1; starts <= 8; ++starts)
	{
		CHECK(ExtendedPlayerColors::count(10, starts) == 10);
		for (int color = -2; color <= 16; ++color)
		{
			CHECK(ExtendedPlayerColors::find(color, 10, starts) == nullptr);
		}
	}
	for (int starts = 9; starts <= 16; ++starts)
	{
		CHECK(ExtendedPlayerColors::count(10, starts) == 16);
		for (int color = -2; color < 10; ++color)
		{
			CHECK(ExtendedPlayerColors::find(color, 10, starts) == nullptr);
		}
		CHECK(ExtendedPlayerColors::find(16, 10, starts) == nullptr);
		std::set<std::uint32_t> rgb;
		std::set<std::string> names;
		for (int color = 10; color < 16; ++color)
		{
			const auto *added = ExtendedPlayerColors::find(color, 10, starts);
			REQUIRE(added);
			const int addition = color - 10;
			CHECK(std::string(added->name) == expected[addition].name);
			CHECK(added->rgb == expected[addition].rgb);
			CHECK(rgb.insert(added->rgb).second);
			CHECK(names.insert(added->name).second);
		}
	}
}
