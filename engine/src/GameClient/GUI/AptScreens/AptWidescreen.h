#pragma once

// The HUD's widescreen presentation correction (docs/HUD_SCALING.md) and the movies mods author for 16:9.
//
// OpenBFME inference, not a retail rule: retail stretches the 1024x768 stage to the window with independent x and y scales (S-130), so a
// mod that wants a proportional HUD on 16:9 authors its clips at 3/4 of their width (0.75 x 4/3 = 1). Edain 4.8.5.4
// (___edain_apt_widescreen.big: Palantir, StrategicPalantir, SpellStore) and Patch 2.02 9.8.6 (__patch202.big: Palantir, SpellStore) do
// this. No BFME2 1.06 or RotWK 2.01 movie meets authoredSqueeze's rule (every .apt of both installs: at most 2 such
// placements, nested, in StrategicDetailsTray). The port's own correction already makes the HUD proportional, so on a squeezed movie it
// only removes what the mod's squeeze leaves: on 16:9 the movie then looks as it does in retail with the mod, wider windows stay
// proportional, 4:3 is retail's.

#include <cmath>
#include <cstddef>
#include <memory>
#include <vector>

#include "Libraries/Source/Apt/AptFile.h"

namespace AptWidescreen
{
// (4/3) / (16/9): the horizontal factor a mod applies to a clip it authors for 16:9 under retail's stretch
constexpr float kModSqueeze = 0.75f;

inline bool squeezedPlacement(const AptPlaceObject &p)
{
	if (!(p.flags & APT_PLACE_HASMATRIX) || p.matrix[1] != 0.0f || p.matrix[2] != 0.0f || p.matrix[3] <= 1e-3f)
	{
		return false;
	}
	return std::fabs(p.matrix[0] / p.matrix[3] - kModSqueeze) < 0.002f;
}

inline std::size_t countSqueezed(const std::vector<AptFrame> &frames)
{
	std::size_t n = 0;
	for (const AptFrame &frame : frames)
	{
		for (const AptFrameItem &item : frame.items)
		{
			if (item.type == APT_ITEM_PLACEOBJECT && item.place && squeezedPlacement(*item.place))
			{
				++n;
			}
		}
	}
	return n;
}

// kModSqueeze when the movie is authored for 16:9: a clip of its root timeline carries the squeeze (Edain's Palantir, StrategicPalantir and
// SpellStore, 2.02's Palantir), or at least 10 placements anywhere do (2.02's SpellStore squeezes inside its tree: 51). Else 1.
inline float authoredSqueeze(const AptFile &movie)
{
	const std::size_t root = countSqueezed(movie.frames);
	std::size_t all = root;
	for (const AptCharacter &ch : movie.characters)
	{
		if (ch.type == APT_CHAR_SPRITE)
		{
			all += countSqueezed(ch.frames);
		}
	}
	return root >= 1 || all >= 10 ? kModSqueeze : 1.0f;
}

// The horizontal correction for a clip of that movie: retail's min(1, scaleY / scaleX) when the movie is authored for 4:3; divided by the
// mod's squeeze otherwise, never wider than the authored clip (1).
inline float correction(float stageWidth, float stageHeight, int width, int height, float squeeze)
{
	const float proportional = ((float)height / stageHeight) / ((float)width / stageWidth);
	return std::fmin(1.0f, proportional / squeeze);
}

// The squeeze of one clip's movie, scanned once per movie (the strategic HUD sets its correction on every tick).
class SqueezeCache
{
public:
	float of(const std::shared_ptr<const AptFile> &movie)
	{
		if (!movie)
		{
			return 1.0f;
		}
		if (movie != m_movie)
		{
			m_movie = movie;
			m_squeeze = authoredSqueeze(*movie);
		}
		return m_squeeze;
	}

private:
	std::shared_ptr<const AptFile> m_movie;
	float m_squeeze = 1.0f;
};
} // namespace AptWidescreen
