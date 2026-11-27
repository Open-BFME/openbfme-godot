// OpenBFME. GPL-3.0.
//
// W3DTaintMask: the terrain's taint mask (W3DTaintMask.h holds the target facts).

#include "GameEngineDevice/W3DDevice/GameClient/W3DTaintMask.h"

#include "GameClient/FireSetting.h"
#include "GameLogic/SimMath.h"

namespace W3DTaintMask
{
// RW 0x497F60
RGBColor fireColour(const FireSetting &fire, int type)
{
	if (type == 1)
	{
		return fire.fuelIndicatorColor; // RW 0x497F9B: the manager's + 0x20
	}
	if (type == 2)
	{
		return fire.burntTerrainColor; // RW 0x497FA5: + 0x14
	}
	return RGBColor{ 1.0f, 1.0f, 1.0f }; // RW 0x497F66: 1.0 (RW 0xBD1908)
}

namespace
{
// RW 0x473952 .. 0x47398B: fild the factor (255 or shroudLevel), fld the channel, fmul (x87, precision control 24), _ftol (RW 0xA3CFA4, truncation), and 0xFF
std::uint32_t channel(float c, std::uint8_t factor = 0xFFu)
{
	return SimMath::ftol2Low32(SimMath::pc24MulW((double)c, (double)factor)) & 0xFFu;
}
} // namespace

std::uint32_t texel(int value, const RGBColor &colour)
{
	std::uint32_t t = (std::uint32_t)value & 0xFFu;
	t = (t << 8) | channel(colour.red);
	t = (t << 8) | channel(colour.green);
	t = (t << 8) | channel(colour.blue);
	return t;
}

// RW 0x47365F .. 0x4738CC (DONOR BFME2 0x00073CC0 W3DTaint.cpp)
RGBColor lowColour(int x, int y, int value, const RGBColor &taintColor, const RGBColor &elvenWoodColor, const RGBColor &fire)
{
	const int v = value & 0xFF;
	float r = 1.0f, g = 1.0f, b = 1.0f; // RW 0x473620: 1.0f (RW 0xBD1908)
	if (v < 0x80 || v > 0x80)
	{
		// RW 0x473668 .. 0x473684 (< 0x80: 1 / 128 = 0.0078125f, RW 0xBDAD58) / RW 0x47374E .. 0x473766 (> 0x80: 1 / 127 = 0.0078740157f, RW 0xBDC534)
		const float t = (v < 0x80)
			? SimMath::mulf32((float)(0x80 - v), 0.0078125f)
			: SimMath::mulf32((float)(v - 0x80), 0.0078740157f);
		const float oneMinusT = SimMath::subf32(1.0f, t);
		// RW 0x473673 .. 0x473704 / 0x473755 .. 0x4737E6: per-cell pattern offsets (RW 0xBDC540 = 0.03f, 0xBDC53C = -0.02f, 0xBDC538 = -0.03f, 0xBDC320 = 0.02f)
		const float dr = (x % 5 == 0 && y % 2 == 0) ? 0.03f : -0.02f;
		const float dg = (x % 3 == 0 && y % 5 == 0) ? -0.02f : 0.03f;
		const float db = (x % 2 == 0 && y % 3 == 0) ? -0.03f : 0.02f;
		const RGBColor &base = (v < 0x80) ? taintColor : elvenWoodColor;
		r = SimMath::addf32(SimMath::mulf32(SimMath::addf32(base.red, dr), t), oneMinusT);
		g = SimMath::addf32(SimMath::mulf32(SimMath::addf32(base.green, dg), t), oneMinusT);
		b = SimMath::addf32(SimMath::mulf32(SimMath::addf32(base.blue, db), t), oneMinusT);
	}
	// RW 0x473836 .. 0x4738CC: multiply by TheFireManager's colour
	return RGBColor{
		SimMath::mulf32(fire.red, r),
		SimMath::mulf32(fire.green, g),
		SimMath::mulf32(fire.blue, b),
	};
}

std::uint32_t lowTexel(int x, int y, int value, const RGBColor &taintColor, const RGBColor &elvenWoodColor, const RGBColor &fire, std::uint8_t shroudLevel)
{
	const RGBColor c = lowColour(x, y, value, taintColor, elvenWoodColor, fire);
	std::uint32_t t = (std::uint32_t)value & 0xFFu;
	t = (t << 8) | channel(c.red, shroudLevel);
	t = (t << 8) | channel(c.green, shroudLevel);
	t = (t << 8) | channel(c.blue, shroudLevel);
	return t;
}

int cellCentre(int n, float cellSize)
{
	return SimMath::truncToInt32(SimMath::mulf32(SimMath::addf32((float)n, 0.5f), cellSize));
}

// RW 0x514628 .. 0x51465F (DONOR BFME2 0x001149EB W3DTerrainBackground.cpp):
// u1 = (float)(i - minX) / (float)m_width; v1 = 1.0f - (float)(j - minY) / (float)m_width
void tileVertexUv(int cellX, int cellY, int tileOriginX, int tileOriginY, float &outU, float &outV, int tileWidth)
{
	const float w = (float)(tileWidth > 0 ? tileWidth : kBackgroundTileCells);
	outU = SimMath::divf32((float)(cellX - tileOriginX), w);
	outV = SimMath::subf32(1.0f, SimMath::divf32((float)(cellY - tileOriginY), w));
}

void terrainUv(float worldX, float worldY, float borderWidth, float cellSize, float &outU, float &outV)
{
	const float span = SimMath::mulf32((float)kBackgroundTileCells, cellSize);
	outU = SimMath::divf32(SimMath::addf32(worldX, borderWidth), span);
	outV = SimMath::subf32(1.0f, SimMath::divf32(SimMath::addf32(worldY, borderWidth), span));
}

void build(const TaintView &view, const FireSetting &fire, std::uint8_t borderLevel, std::vector<std::uint32_t> &texels, int &width, int &height)
{
	width = view.countX + 2;
	height = view.countY + 2;
	const std::uint32_t b = borderLevel;
	const std::uint32_t border = (b << 24) | (b << 16) | (b << 8) | b; // RW 0x47323E .. 0x47324D
	texels.assign((size_t)width * (size_t)height, border);
	for (int y = 0; y < view.countY; ++y)
	{
		for (int x = 0; x < view.countX; ++x)
		{
			const size_t i = (size_t)y * (size_t)view.countX + (size_t)x;
			texels[(size_t)(y + 1) * (size_t)width + (size_t)(x + 1)] = texel(view.values[i], fireColour(fire, view.fireTypes[i]));
		}
	}
}
} // namespace W3DTaintMask
