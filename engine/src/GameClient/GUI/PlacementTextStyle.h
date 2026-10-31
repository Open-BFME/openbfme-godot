#pragma once

#include <algorithm>
#include <cmath>

struct PlacementTextStyle
{
	int fontSize;
	int pointerGap;
	int shadowOffset;

	static PlacementTextStyle forViewport(int configuredFontSize, int viewportHeight, float uiScale)
	{
		const float resolutionScale = std::max(1.0f, (float)viewportHeight / 600.0f);
		const float scale = resolutionScale * uiScale;
		const int baseFontSize = configuredFontSize > 0 ? configuredFontSize : 14;
		return {
			std::max(1, (int)std::lround((float)baseFontSize * scale)),
			std::max(1, (int)std::lround(20.0f * scale)),
			std::max(1, (int)std::lround(scale))
		};
	}
};
