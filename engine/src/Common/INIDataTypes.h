// OpenBFME. GPL-3.0.
//
// Plain value types the INI field parsers store into. Layout follows ZH
// GameEngine/Include/Common/Coord.h and GameClient/Color.h.

#pragma once

#include <cstdint>

struct Coord2D
{
	float x = 0.0f, y = 0.0f;
};

// Zero-initialised by default: Common/MapObject.h and the map code share this type (MAPOBJ-1 merged the duplicate definition).
struct Coord3D
{
	float x = 0.0f, y = 0.0f, z = 0.0f;
};

struct ICoord2D
{
	int x = 0, y = 0;
};

// ZH GameClient/Color.h RGBColor
struct RGBColor
{
	float red, green, blue;
};

// ZH GameClient/Color.h RGBAColorInt
struct RGBAColorInt
{
	int red, green, blue, alpha;
};

// ZH GameClient/Color.h GameMakeColor: (alpha<<24) | (red<<16) | (green<<8) | blue
inline std::uint32_t GameMakeColor(int red, int green, int blue, int alpha)
{
	return ((std::uint32_t)alpha << 24) | ((std::uint32_t)red << 16) | ((std::uint32_t)green << 8) | (std::uint32_t)blue;
}
