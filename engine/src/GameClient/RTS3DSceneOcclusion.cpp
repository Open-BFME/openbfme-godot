// OpenBFME. GPL-3.0.
// See GameClient/RTS3DSceneOcclusion.h.

#include "GameClient/RTS3DSceneOcclusion.h"

#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ObjectTemplateInfo.h"

#include <cmath>

namespace
{
// colorspace.h (ZH / BFME WW3D2): RGB_To_HSV RW 0x46DB66 and HSV_To_RGB RW 0x46DC7B, out of line in retail (BFME2 decomp ColorSpaceO1Inlines.cpp)
struct Hsv
{
	float x, y, z;
};

Hsv rgbToHsv(float r, float g, float b)
{
	float mx = r > g ? r : g;
	mx = mx > b ? mx : b;
	float mn = r < g ? r : g;
	mn = mn < b ? mn : b;
	Hsv hsv;
	hsv.z = mx;
	hsv.y = mx != 0.0f ? (mx - mn) / mx : 0.0f;
	if (hsv.y == 0.0f)
	{
		hsv.x = -1.0f;
	}
	else
	{
		const float delta = mx - mn;
		hsv.x = 0.0f;
		if (r == mx)
		{
			hsv.x = (g - b) / delta;
		}
		else if (g == mx)
		{
			hsv.x = 2.0f + (b - r) / delta;
		}
		else if (b == mx)
		{
			hsv.x = 4.0f + (r - g) / delta;
		}
		hsv.x *= 60.0f;
		if (hsv.x < 0.0f)
		{
			hsv.x += 360.0f;
		}
	}
	return hsv;
}

void hsvToRgb(const Hsv &hsv, float rgb[3])
{
	float h = hsv.x;
	const float s = hsv.y;
	const float v = hsv.z;
	if (s == 0.0f)
	{
		rgb[0] = rgb[1] = rgb[2] = v;
		return;
	}
	if (h == 360.0f)
	{
		h = 0.0f;
	}
	h /= 60.0f;
	const int i = (int)std::floor(h);
	const float f = h - (float)i;
	const float p = v * (1.0f - s);
	const float q = v * (1.0f - (s * f));
	const float t = v * (1.0f - (s * (1.0f - f)));
	switch (i)
	{
	case 0: rgb[0] = v; rgb[1] = t; rgb[2] = p; break;
	case 1: rgb[0] = q; rgb[1] = v; rgb[2] = p; break;
	case 2: rgb[0] = p; rgb[1] = v; rgb[2] = t; break;
	case 3: rgb[0] = p; rgb[1] = q; rgb[2] = v; break;
	case 4: rgb[0] = t; rgb[1] = p; rgb[2] = v; break;
	case 5: rgb[0] = v; rgb[1] = p; rgb[2] = q; break;
	default: break; // retail's switch has no default: the (uninitialised) rgb stays; h is in [0, 360) here, so never
	}
}

// DX8Wrapper::Convert_Color's inline asm: the control word's rounding set to chop (bits 10-11) for the four fistp; channel * 255 truncated
std::uint32_t chop255(float c)
{
	return (std::uint32_t)(std::int32_t)(c * 255.0f);
}
} // namespace

RTS3DSceneOcclusion::Facts RTS3DSceneOcclusion::factsOf(const Object &obj)
{
	static const int kStructure = ObjectTemplateInfoBuilder::kindOfIndex("STRUCTURE");
	static const int kNonOccluding = ObjectTemplateInfoBuilder::kindOfIndex("NONOCCLUDING");
	static const int kUnit[4] = { ObjectTemplateInfoBuilder::kindOfIndex("INFANTRY"), ObjectTemplateInfoBuilder::kindOfIndex("MONSTER"),
		ObjectTemplateInfoBuilder::kindOfIndex("CAVALRY"), ObjectTemplateInfoBuilder::kindOfIndex("MACHINE") }; // RW 0x470E40: push 0xB, 9, 0xA, 8
	static const int kOut[2] = { ObjectTemplateInfoBuilder::kindOfIndex("IGNORED_IN_GUI"), ObjectTemplateInfoBuilder::kindOfIndex("INERT") }; // RW 0x470E5F
	static const int kScalingWall = CombatNames::modelCondition("SCALING_WALL");
	auto kind = [&](int bit) { return bit >= 0 && obj.isKindOf((unsigned)bit); };
	Facts f;
	f.structure = kind(kStructure);
	f.nonOccluding = kind(kNonOccluding);
	f.hasObject = true;
	f.effectivelyDead = obj.isEffectivelyDead();
	f.unitKind = kind(kUnit[0]) || kind(kUnit[1]) || kind(kUnit[2]) || kind(kUnit[3]);
	f.ignoredOrInert = kind(kOut[0]) || kind(kOut[1]);
	f.safeOcclusionFrame = obj.getSafeOcclusionFrame();
	f.hasControllingPlayer = obj.getControllingPlayer() != nullptr;
	f.insideGarrison = obj.testStatus((unsigned)CombatNames::statuses().insideGarrison);
	f.scalingWall = obj.testModelCondition(kScalingWall);
	return f;
}

UnsignedInt RTS3DSceneOcclusion::occlusionFrame(UnsignedInt logicFrame, UnsignedInt defaultOcclusionDelay)
{
	return logicFrame <= defaultOcclusionDelay ? defaultOcclusionDelay + 1 : logicFrame; // RW 0x470C32: frame above the delay (ja) or delay + 1 (RW 0x470C37)
}

std::uint8_t RTS3DSceneOcclusion::eligibility(const Facts &f, UnsignedInt frame)
{
	std::uint8_t bits = 0;
	if (f.structure && !f.nonOccluding)
	{
		bits |= ROLE_OCCLUDER; // RW 0x470DDE / 0x470DF5
	}
	if (f.hasObject && !f.effectivelyDead && f.unitKind && !f.ignoredOrInert && f.safeOcclusionFrame <= frame && f.hasControllingPlayer && !f.insideGarrison &&
		!f.scalingWall)
	{
		bits |= ROLE_OCCLUDEE; // RW 0x470E19 .. 0x470EC5
	}
	return bits;
}

RTS3DSceneOcclusion::Role RTS3DSceneOcclusion::Budget::admit(std::uint8_t eligible, bool isTranslucent)
{
	if (isTranslucent && translucent < kMaxVisibleObjects)
	{
		++translucent; // RW 0x470D96 .. 0x470DBF: flags 8, the translucent list
		return ROLE_NONE;
	}
	if ((eligible & ROLE_OCCLUDER) && occluders < kMaxVisibleObjects)
	{
		++occluders;
		return ROLE_OCCLUDER;
	}
	if ((eligible & ROLE_OCCLUDEE) && occludees < kMaxVisibleObjects) // a full occluder list falls through to the occludee test (RW 0x470DED jge 0x470E16)
	{
		++occludees;
		return ROLE_OCCLUDEE;
	}
	return ROLE_NONE;
}

std::uint32_t RTS3DSceneOcclusion::occludedColor(std::uint32_t playerColor, float luminanceScale)
{
	Hsv hsv = rgbToHsv((float)((playerColor >> 16) & 0xFF) / 255.0f, (float)((playerColor >> 8) & 0xFF) / 255.0f, (float)(playerColor & 0xFF) / 255.0f);
	hsv.z *= luminanceScale;
	float rgb[3];
	hsvToRgb(hsv, rgb);
	return (chop255(0.5f) << 24) | ((chop255(rgb[0]) & 0xFF) << 16) | ((chop255(rgb[1]) & 0xFF) << 8) | (chop255(rgb[2]) & 0xFF);
}
