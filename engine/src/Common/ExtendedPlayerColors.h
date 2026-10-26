// OpenBFME. GPL-3.0.
// OpenBFME's extended-map palette. Retail MultiplayerColor entries remain in file order;
// these entries are appended only for maps with more than eight starts, never to parsed settings.

#pragma once

#include <array>
#include <cstdint>

namespace ExtendedPlayerColors
{
struct Color
{
	const char *name;
	std::uint32_t rgb;
};

inline constexpr std::array<Color, 6> additions{{
	{ "Cyan", 0x00DCDCu },
	{ "Magenta", 0xE600ACu },
	{ "Lime", 0xB7F000u },
	{ "Yellow", 0xFFE119u },
	{ "Navy", 0x16255Cu },
	{ "Brown", 0x633B20u }
}};

inline constexpr int count(int retailCount, int mapStartCount)
{
	return retailCount + (mapStartCount > 8 ? (int)additions.size() : 0);
}

inline constexpr const Color *find(int colorIndex, int retailCount, int mapStartCount)
{
	return mapStartCount > 8 && colorIndex >= retailCount && colorIndex < count(retailCount, mapStartCount)
		? &additions[(std::size_t)(colorIndex - retailCount)] : nullptr;
}
} // namespace ExtendedPlayerColors
