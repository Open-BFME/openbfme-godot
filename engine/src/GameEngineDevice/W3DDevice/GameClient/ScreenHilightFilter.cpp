// OpenBFME. GPL-3.0. Retail's glow filter arithmetic (lane GLOW-1): see ScreenHilightFilter.h.

#include "GameEngineDevice/W3DDevice/GameClient/ScreenHilightFilter.h"

#include <algorithm>
#include <cmath>

namespace ScreenHilightFilter
{

std::vector<Sample> createGaussianVector(int taps, float coefficient0, float amplitude0, float coefficient1, float amplitude1, int components)
{
	// RW 0x4779A6 .. 0x4779D2: scale = 15.0 / taps; above 1.0: (1 - scale) * 0.5 + scale
	float scale = 15.0f / (float)taps;
	if (scale > 1.0f)
	{
		scale = (1.0f - scale) * 0.5f + scale;
	}
	struct Pair
	{
		float coefficient, amplitude;
	} pair[2] = { { coefficient0, scale * amplitude0 }, { coefficient1, scale * amplitude1 } };
	const float center = ((float)taps - 1.0f) * 0.5f;
	const float centerSquared = center * center;
	std::vector<Sample> out;
	for (int i = 0; i < taps; ++i)
	{
		Sample s;
		s.x = (float)i - center - 0.1f;
		s.y = 0.0f;
		const float norm = s.x * s.x / centerSquared;
		float z = 0.0f;
		for (int j = 0; j < components && j < 2; ++j)
		{
			// RW 0x477A5F .. 0x477A83: fld norm; fmul coefficient; exp (CRT, a double); fdivr 1.0; fmul amplitude; fadd z; fstp (24 bit precision)
			const float argument = norm * pair[j].coefficient;
			const float inverse = (float)(1.0 / std::exp((double)argument));
			z = z + inverse * pair[j].amplitude;
		}
		s.z = z;
		if (s.z > 0.01f)
		{
			out.push_back(s);
		}
	}
	const size_t padding = (4 - out.size() % 4) % 4; // RW 0x477AB0 .. 0x477AEE
	for (size_t n = 0; n < padding; ++n)
	{
		out.push_back({ 0.0f, 0.0f, 0.0f });
	}
	return out;
}

std::vector<Sample> createGaussianVector(const GlowEffect &effect)
{
	return createGaussianVector(effect.glowDiameter, effect.radiusScale1, effect.amplitude1, effect.radiusScale2, effect.amplitude2, 2);
}

std::vector<Group> blurPass(const std::vector<Sample> &kernel, float weightScale, int textureSize, bool vertical)
{
	std::vector<Group> groups;
	const float inverseSize = 1.0f / (float)textureSize; // RW 0x476F00 .. 0x476F0E
	for (size_t first = 0; first + kSamplesPerGroup <= kernel.size(); first += kSamplesPerGroup)
	{
		Group g;
		g.blend = first != 0; // RW 0x476F24: the group at sample 4 turns the blend on, and it stays on
		for (size_t j = 0; j < (size_t)kSamplesPerGroup; ++j)
		{
			const Sample &s = kernel[first + j];
			g.weight[j] = s.z * weightScale;
			g.offset[j] = vertical ? std::array<float, 2>{ s.y * inverseSize, s.x * inverseSize } : std::array<float, 2>{ s.x * inverseSize, s.y * inverseSize };
		}
		groups.push_back(g);
	}
	return groups;
}

float store8(float v)
{
	const float c = std::min(1.0f, std::max(0.0f, v));
	return std::floor(c * 255.0f + 0.5f) / 255.0f;
}

std::array<float, 3> glowPixel(const std::array<std::array<float, 3>, 4> &taps, const std::array<float, 4> &weight)
{
	std::array<float, 3> r = { 0.0f, 0.0f, 0.0f };
	for (size_t i = 0; i < 4; ++i)
	{
		// ps_1_1 constants and registers saturate; mul r0, t0, c0 then mad r0, tN, cN, r0
		const float c = std::min(1.0f, std::max(-1.0f, weight[i]));
		for (size_t k = 0; k < 3; ++k)
		{
			r[k] = std::min(1.0f, std::max(-1.0f, taps[i][k] * c + r[k]));
		}
	}
	return r;
}

std::array<float, 3> blendOneInvSrcColor(const std::array<float, 3> &src, const std::array<float, 3> &dst)
{
	std::array<float, 3> out;
	for (size_t k = 0; k < 3; ++k)
	{
		out[k] = std::min(1.0f, src[k] + dst[k] * (1.0f - src[k]));
	}
	return out;
}

std::string unusableReason(const GlowEffect &effect)
{
	if (effect.glowTextureWidth <= 0 || effect.glowTextureWidth > kMaxGlowTextureWidth)
	{
		return "GlowTextureWidth " + std::to_string(effect.glowTextureWidth) + " is not a usable texture size";
	}
	if (effect.glowDiameter <= 0)
	{
		return "GlowDiameter " + std::to_string(effect.glowDiameter) + " is not a usable number of taps";
	}
	if (effect.glowDiameter > kMaxGlowDiameter)
	{
		return "GlowDiameter " + std::to_string(effect.glowDiameter) + " is above the device limit of " + std::to_string(kMaxGlowDiameter) + " taps";
	}
	return std::string();
}

const std::vector<const char *> &unverified()
{
	static const std::vector<const char *> lines = {
		"S-5231: the glow scene (pass mode 3) is drawn by Godot into a render of the view's aspect, then point sampled at the centres of the GlowTextureWidth grid; retail rasterises the GlowTextureWidth x GlowTextureWidth target itself",
		"S-5231: GlobalData + 0xD34 (the script-set mode that stretches the back buffer into the glow target) and MultipassGlowEnabled (pass modes 4 and 5) are not ported; the view filters 6, 7 and 8 do not exist here",
		"S-5231: glow-flagged objects are the scripted model draws whose module sets GlowEnabled (map objects and attached models are not); GlowEmissive keeps a mesh when its first material's emissive is above 0.01 (MeshClass::Render), the per-draw emissive test of DX8Wrapper::Draw (0.1) is not applied; blended surfaces of the glow scene blend in the gamma space the main view uses",
		"S-5231: device limit, not retail: a GlowDiameter above 128 taps or a GlowTextureWidth above 4096 is refused and reported (retail builds any kernel, RW 0x477969); every retail block is inside it",
	};
	return lines;
}

} // namespace ScreenHilightFilter
