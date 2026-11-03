// OpenBFME. GPL-3.0.
//
// The draw mode of an Apt RenderImage clip (community PR #37, review r1). TARGET FACTS (BFME2, Open-BFME-2 Common/AptUtils.cpp, matched:
// 0x412AF3, called by the image draw 0x412B94): the clip's `_mode` parameter, compared case-insensitively with SOLID 0, GRAYSCALE 1, ALPHA 2,
// ADDITIVE 3, SOLID_GRAYSCALE 4, SOLID_TEXTURE 5; empty or anything else is ALPHA (2); the mode goes to W3DDisplay's image draw.
// INFERENCE (stop S-5016): the draw's render states per mode were not read; the Godot shader (aptImageModeShader) ignores the texture alpha for
// SOLID*, uses luma weights for *GRAYSCALE and Godot's blend_add for ADDITIVE.
#pragma once

#include <string>
#include <vector>

enum AptImageMode
{
	APT_IMAGE_SOLID = 0,
	APT_IMAGE_GRAYSCALE = 1,
	APT_IMAGE_ALPHA = 2,
	APT_IMAGE_ADDITIVE = 3,
	APT_IMAGE_SOLID_GRAYSCALE = 4,
	APT_IMAGE_SOLID_TEXTURE = 5
};

// 0x412AF3: the `_mode` value of the clip (its text; "" when the clip has none)
AptImageMode parseAptImageMode(const std::string &mode);
// the canvas_item shader of a mode (empty for ALPHA: the default draw). The texture is sampled once; the vertex colour (the clip's colour transform)
// reaches the fragment through a varying, because a canvas_item fragment's COLOR already holds texture * vertex colour
std::string aptImageModeShader(AptImageMode mode);
// [S-5016] (with the farm placement feedback's eligibility and the `_imageMap` lookup order, community PR #37)
std::vector<std::string> aptImageModeStops();
