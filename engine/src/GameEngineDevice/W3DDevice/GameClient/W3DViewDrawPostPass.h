// OpenBFME. GPL-3.0.
//
// W3DViewDrawPostPass: which of retail's world post passes W3DView::draw runs after the 3D scene (lane GLOW-1). Client / draw code only.
//
// TARGET FACTS (RotWK game.dat, stop S-001 caveat; BFME2 decomp W3DViewDraw.cpp:290-372 is the donor and matches the binary instruction for instruction):
//  - The view draw (RW 0x487897) reads TheGameLODManager (RW 0xDE3B84) + 0x1778 = EffectsLOD (StaticLODState::effectsLOD): `cmp [mgr + 0x1778], 1; jg` -
//    lowLOD is set when the manager exists and its EffectsLOD is VeryLow (0) or Low (1) (RW 0x48789F .. 0x4878A8). EffectsLOD indexes the names VeryLow,
//    Low, Medium, High, UltraHigh (RW 0xD9E6E0).
//  - The special filter is the view filter (this + 0x5C) 7, 6 or 8 (RW 0x4878B1 .. 0x4878C2); the passes run only for the scene's ordinary pass mode 0
//    (RW 0x4878C4: any other mode goes to the end).
//  - Order of the branches (RW 0x4878CF .. 0x487A5C):
//      1. special filter: runs the filter pass-6 render (the glow filter W3DFilters[4] as the blur): always for the filters 6 and 7, for 8 only while
//         GlowEffect.GlowEnabled (RW 0x4878E9 .. 0x4878F7). NOT gated by the effects LOD;
//      2. the map's colour grade (the holder RW 0x501B01, active when its first dword is set, run by RW 0x5017D5) when it is active and not lowLOD
//         (RW 0x4879FB .. 0x487A16) - it PRE-EMPTS the glow;
//      3. the glow (GlowEffect.GlowEnabled, RW 0x487A24 .. 0x487A36) when not lowLOD: pass mode 3 for the glow-flagged objects (MultipassGlowEnabled
//         (RW 0x487AE3, GlowEffect + 0x21) selects the pass modes 6, 4 and 5 instead), then the filter blur and the composite.
//    So a low effects LOD runs neither the grade nor the glow, an active grade runs the grade and no glow, otherwise the glow runs.
//  - The glow scene (RTS3DScene::Customized_Render, RW 0x46FE84) draws, in pass mode 3: the terrain when GlowEffect.TerrainGlow (+ 0x20, RW 0x46FFCA)
//    or the terrain object's own glow flag (vslot 0x1C8) is set, then the render objects whose glow flag (vslot 0x1C8) is set, each with the
//    emissive-only switch (RW 0xDD1E18, vslot 0x1D0) when GlowEmissive. The flags come from the W3DModelDraw module data: GlowEnabled (+ 0x128) and
//    GlowEmissive (+ 0x129, field rows RW 0xBE1530 / 0xBE1540).

#pragma once

namespace W3DViewDrawPostPass
{

enum class Choice
{
	None,          // neither the grade nor the glow
	SpecialFilter, // the view filter 6, 7 or 8's pass (not ported: S-5231)
	ColorGrade,    // the map's LookupTablePostEffect
	Glow,          // the GlowEffect pass
};

struct Inputs
{
	int passMode = 0;            // the scene's custom pass mode of the draw (0: the ordinary one)
	int viewFilter = 0;          // W3DView + 0x5C
	int effectsLOD = 4;          // StaticLODState::effectsLOD
	bool colorGradeActive = false;
	bool glowEnabled = false;    // the active GlowEffect's GlowEnabled
};

// RW 0x48789F: EffectsLOD VeryLow or Low
inline bool lowEffectsLOD(int effectsLOD)
{
	return effectsLOD <= 1;
}

// RW 0x4878B1 .. 0x4878C2
inline bool specialFilter(int viewFilter)
{
	return viewFilter == 6 || viewFilter == 7 || viewFilter == 8;
}

Choice choose(const Inputs &in);

} // namespace W3DViewDrawPostPass
