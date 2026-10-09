// OpenBFME. GPL-3.0. The map's colour grade (lane RENDER-4): see W3DLookupTablePostEffect.h.

#include "GameEngineDevice/W3DDevice/GameClient/W3DLookupTablePostEffect.h"

#include <algorithm>
#include <cmath>

namespace W3DLookupTablePostEffect
{

bool VolumeFromStrip(const std::uint8_t *rgba, int width, int height, std::vector<std::uint8_t> &volume, std::string *error)
{
	if (!rgba || width != kSize * kSize || height != kSize)
	{
		if (error)
		{
			*error = "a lookup texture must be a " + std::to_string(kSize * kSize) + " x " + std::to_string(kSize) + " strip of " + std::to_string(kSize) +
				" slices, not " + std::to_string(width) + " x " + std::to_string(height);
		}
		return false;
	}
	volume.assign((size_t)kSize * kSize * kSize * 4, 0);
	for (int z = 0; z < kSize; ++z)
	{
		for (int y = 0; y < kSize; ++y)
		{
			const std::uint8_t *src = rgba + ((size_t)y * (size_t)width + (size_t)z * kSize) * 4;
			std::uint8_t *dst = volume.data() + ((size_t)z * kSize + (size_t)y) * kSize * 4;
			std::copy(src, src + (size_t)kSize * 4, dst);
		}
	}
	return true;
}

namespace
{
// one LINEAR / CLAMP axis: the two texels and the weight of the second (texel centres at (i + 0.5) / kSize)
void axis(float c, int &i0, int &i1, float &w)
{
	float t = c * (float)kSize - 0.5f;
	t = std::min((float)(kSize - 1), std::max(0.0f, t));
	i0 = (int)std::floor(t);
	i1 = std::min(kSize - 1, i0 + 1);
	w = t - (float)i0;
}
} // namespace

void Apply(const std::vector<std::uint8_t> &volume, const float rgb[3], float blend, float out[3])
{
	int x0, x1, y0, y1, z0, z1;
	float wx, wy, wz;
	axis(rgb[0], x0, x1, wx);
	axis(rgb[1], y0, y1, wy);
	axis(rgb[2], z0, z1, wz);
	auto at = [&](int x, int y, int z, int ch) { return (float)volume[(((size_t)z * kSize + (size_t)y) * kSize + (size_t)x) * 4 + (size_t)ch] / 255.0f; };
	for (int ch = 0; ch < 3; ++ch)
	{
		const float c00 = at(x0, y0, z0, ch) * (1 - wx) + at(x1, y0, z0, ch) * wx;
		const float c10 = at(x0, y1, z0, ch) * (1 - wx) + at(x1, y1, z0, ch) * wx;
		const float c01 = at(x0, y0, z1, ch) * (1 - wx) + at(x1, y0, z1, ch) * wx;
		const float c11 = at(x0, y1, z1, ch) * (1 - wx) + at(x1, y1, z1, ch) * wx;
		const float c0 = c00 * (1 - wy) + c10 * wy;
		const float c1 = c01 * (1 - wy) + c11 * wy;
		const float lut = c0 * (1 - wz) + c1 * wz;
		out[ch] = blend * lut + (1.0f - blend) * rgb[ch]; // lrp r0, c0.x, r1, r2
	}
}

const char *StopText()
{
	return "S-1650: the map's LookupTablePostEffect grades the 3D view as postfx_lookuptable.fxo does (lerp(frame, lut(frame.rgb), BlendFactor), "
		   "linear / clamp); inferred: the strip-to-volume layout (the identity Default_vol.tga is the identity) and the grade applied to the 3D view only";
}

} // namespace W3DLookupTablePostEffect
