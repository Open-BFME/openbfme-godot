// OpenBFME. GPL-3.0.
//
// RTS3DSceneOcclusion: the behind-building markers' choice of objects (lane OCCLUDE-1), the Godot-free half of RotWK's occluded-object pass. A unit hidden
// behind a building is drawn as a flat silhouette in its player's colour; the GodotDevice half (W3DInstancer's occlusion passes) draws it.
//
// TARGET FACTS (RotWK game.dat, caveat S-001; tier B same-shape pairs, tools/re/pair_diff.py: only field offsets differ from BFME2):
//   RTS3DScene::Visibility_Check RW 0x470B83 (BFME2 0x471245, decomp W3DDevice/GameClient/RTS3DSceneVisibilityCheck.cpp) sorts every visible drawable
//   (in the spatial index's order) that is not hidden:
//     * translucent (the drawable's opacity RW 0x672FC4 or the render object's transparency below 1) while fewer than GlobalData + 0x974 (512) translucent
//       objects were listed: the translucent list (flags 8), nothing else;
//     * else an OCCLUDER when the template is KindOf STRUCTURE (byte + 0x108 bit 7) and not NONOCCLUDING (bit 111, byte + 0x115 bit 7) while fewer than
//       + 0x978 (512) occluders were listed (flags 2);
//     * else an OCCLUDEE when it has an object, fewer than + 0x97C (512) occludees were listed, the object is not effectively dead (+ 0x458 bit 0), the
//       drawable is any KindOf of INFANTRY / CAVALRY / MONSTER / MACHINE (RW 0x46E771(8, 10, 9, 11)) and none of IGNORED_IN_GUI / INERT (RW 0x46E72F(0x2F,
//       0x59)), the object's safe occlusion frame (+ 0x444) is at most the frame, it has a controlling player (RW 0x68B678), its status lacks
//       INSIDE_GARRISON (0x3A) and its model condition lacks SCALING_WALL (0x1BA, Object + 0x10C) (flags 4);
//     * else, with no flag yet and fewer than + 0x980 (512) listed, a plain object (flags 0x10).
//   The frame is TheGameLogic's (+ 0x40), raised to GameData DefaultOcclusionDelay (+ 0xCC) + 1 when it is not above it (RW 0x470C27 .. 0x470C38).
//   GlobalData's constructor sets the four limits to 0x200 (RW 0x642D4C .. 0x642D63; no INI row). No behind-building gate here (a BFME2 change from ZH).
//   RTS3DScene::flushOccludedObjectsIntoStencil RW 0x470176 (BFME2 0x470841, decomp RTS3DSceneFlushOccludedObjectsIntoStencil.cpp:326; RotWK reads the
//   template's shadow word at + 0x5F2 and the player colour at + 0x2A0) draws the occludees per controlling player writing a colour index into the stencil,
//   then the plain objects (the +0x7FC list misnamed m_nonOccludersOrOccludees there is the occluder list) and the occluders writing bit 0x80 where they
//   pass the depth test; then for each player whose objects were drawn a screen quad in the player's colour (RW 0x46E411 renderStenciledPlayerColor, stencil
//   EQUAL under mask 0x8F, blend SRCALPHA / INVSRCALPHA) where an occludee's pixel was covered by an occluder. The quads are drawn only while GameData
//   UseBehindBuildingMarker (+ 0x68) and TheGameLogic + 0x99 are set and no shadow map is being drawn (RW 0x470744 / 0x4707B6). The colour: the player's
//   0xRRGGBB to HSV, the value times GameData OccludedColorLuminanceScale (+ 0x984), back to RGB, packed by DX8Wrapper::Convert_Color(rgb, 0.5) (x87
//   truncation of each channel * 255). The template's shadow bits 0x380 (SHADOW_VOLUME_NON_SELF_1 .. 3) only group the stencil shadows: not ported.
// DIFFERENCES (INFERENCE, presentation only): the limits are counted in our drawable order, not the spatial index's; where two occludees of different players
// overlap behind a building the first one drawn (the transparent pass orders back to front) gives the colour where retail shows the front one's.

#pragma once

#include "GameLogic/ObjectTypes.h"

#include <cstdint>

class Object;

struct RTS3DSceneOcclusion
{
	enum Role : std::uint8_t
	{
		ROLE_NONE = 0,     ///< a plain object (or translucent): drawn as usual
		ROLE_OCCLUDER = 1, ///< flags 2: marks the stencil where it is in front
		ROLE_OCCLUDEE = 2, ///< flags 4: drawn in its player's colour where an occluder covers it
	};

	// what RW 0x470DD2 .. 0x470EAE read of a drawable's template and object
	struct Facts
	{
		bool structure = false;         ///< KindOf STRUCTURE
		bool nonOccluding = false;      ///< KindOf NONOCCLUDING
		bool hasObject = false;
		bool effectivelyDead = false;   ///< Object + 0x458 bit 0
		bool unitKind = false;          ///< any KindOf of INFANTRY / CAVALRY / MONSTER / MACHINE
		bool ignoredOrInert = false;    ///< any KindOf of IGNORED_IN_GUI / INERT
		UnsignedInt safeOcclusionFrame = 0; ///< Object + 0x444
		bool hasControllingPlayer = false;
		bool insideGarrison = false;    ///< status INSIDE_GARRISON (0x3A)
		bool scalingWall = false;       ///< model condition SCALING_WALL (0x1BA)
	};
	static Facts factsOf(const Object &obj);

	// the frame RW 0x470C15 .. 0x470C38 compares with: the logic frame, or DefaultOcclusionDelay + 1 while the frame is not above the delay
	static UnsignedInt occlusionFrame(UnsignedInt logicFrame, UnsignedInt defaultOcclusionDelay);
	// the roles the drawable qualifies for before the list limits, as Role bits (a STRUCTURE that is also a unit KindOf has both: the occludee test is
	// reached when the occluder list is full); the translucent test comes first, in Budget::admit
	static std::uint8_t eligibility(const Facts &facts, UnsignedInt occlusionFrame);

	// the four list limits of one Visibility_Check (GlobalData + 0x974 .. + 0x980, 0x200 each)
	static constexpr int kMaxVisibleObjects = 0x200;
	struct Budget
	{
		int translucent = 0, occluders = 0, occludees = 0;
		// one render object: its role as listed. A translucent one stays out of the roles while the translucent list has room
		Role admit(std::uint8_t eligible, bool translucent);
	};

	// the colour of a player's occluded silhouettes (RW 0x470326 .. 0x470411): 0xAARRGGBB, alpha 127 (Convert_Color(rgb, 0.5) truncates 127.5)
	static std::uint32_t occludedColor(std::uint32_t playerColor, float luminanceScale);
};
