#pragma once
// W3DShroud's texel: the level of a shroud cell packed into the shroud texture's A4R4G4B4 pixel.
//
// TARGET FACTS (RotWK 2.01, game.dat): RW 0x47266B is the pixel function (BFME2 0x472D5D, Rva00072D5DShroudPixel in W3DShroudMaterialPass.cpp; ZH inlines it
// in each fill). It scales the level by the three GlobalData shroud colour floats (red +0xBDC, green +0xBE0, blue +0xBE4), each product through _ftol
// (RW 0xA3CFA4, truncation), and packs  alpha<<8 | (red & 0xF0)<<4 | (green & 0xF0) | (blue >> 4) & 0xF  with alpha = min(max(level - 4, 0) * 255 / 46, 255);
// level 255 is opaque white (255 in every channel). The high nibble of each channel is all the texture keeps: a 4-bit channel. The fog level 127 (FogAlpha
// 127) with ShroudColor 255 255 255 (1.0f each) is red 0x7F -> nibble 7, which Direct3D reads back as 7/15 = 0.467, not 127/255 = 0.498.
// The terrain effect reads only the red channel (terrain.fxo ps_2_0 `mul r0.xyz, r0.x, r1`; ps_1_1 `dp3 t0.xyz, c4, t3` with c4 = (1,0,0)), through
// ShroudSampler (MinFilter / MagFilter / MipFilter LINEAR, address CLAMP): the shroud multiplies the fogged terrain colour by that red value.
// The port keeps the texture one byte a texel (the red channel); the shaders multiply by it directly.
#include "GameLogic/SimMath.h"

#include <cstdint>

namespace ShroudTexel
{
// GlobalData ShroudColor (parseRGBColor: 0 .. 1 per channel)
struct Colour
{
	float red = 1.0f, green = 1.0f, blue = 1.0f;
};

// Rva00072D5DShroudPixel / RW 0x47266B: the A4R4G4B4 texel of a level
inline std::uint16_t pixel(std::uint8_t level, const Colour &c)
{
	unsigned int blue = (unsigned int)SimMath::truncToInt32(SimMath::mulf32((float)level, c.blue));
	unsigned int green = (unsigned int)SimMath::truncToInt32(SimMath::mulf32((float)level, c.green));
	unsigned int red = (unsigned int)SimMath::truncToInt32(SimMath::mulf32((float)level, c.red));
	int alpha = ((level > 4 ? (int)level - 4 : 0) * 255) / 46;
	if (alpha > 255)
	{
		alpha = 255;
	}
	if (level == 255)
	{
		red = green = blue = 255;
		alpha = 255;
	}
	return (std::uint16_t)(((alpha & 0xF0) << 8) | ((red & 0xF0) << 4) | (green & 0xF0) | ((blue >> 4) & 0xF));
}

// the red nibble of a texel (0 .. 15)
inline unsigned int redNibble(std::uint16_t texel)
{
	return (texel >> 8) & 0xF;
}

// Direct3D's A4R4G4B4 -> 8 bit expansion of a nibble (n * 17 = n / 15 of full scale): the byte the port uploads so that the shader's texture read is the
// value the GPU reads from retail's 4444 texture
inline std::uint8_t redByte(std::uint8_t level, const Colour &c)
{
	return (std::uint8_t)(redNibble(pixel(level, c)) * 17);
}
} // namespace ShroudTexel
