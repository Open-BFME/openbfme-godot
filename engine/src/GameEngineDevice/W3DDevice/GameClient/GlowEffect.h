// OpenBFME. GPL-3.0.
//
// GlowEffect: the GlowEffect INI block and its saved / active copies (lane GLOW-1). Client / draw code only.
//
// TARGET FACTS (RotWK game.dat, stop S-001 caveat; BFME2 decomp GameEngine/Source/GameClient/Drawable_Rva00309fb3.cpp for the donor):
//  - The block keyword "GlowEffect" (RW 0xBFA150) registers the parser RW 0x70B734 (block table row RW 0xD9E90C). Its field table is RW 0xC1E810, ten rows
//    (parseBool RW 0x42E558, parseInt RW 0x42EC5E, parseReal RW 0x42ED00): GlowEnabled + 0x00, GlowDiameter + 0x04, GlowIntensity + 0x08,
//    GlowTextureWidth + 0x0C, RadiusScale1 + 0x10, RadiusScale2 + 0x18, Amplitude1 + 0x14, Amplitude2 + 0x1C, TerrainGlow + 0x20,
//    MultipassGlowEnabled + 0x21 (in that order: the two scale rows come before the amplitudes).
//  - The record is 0x24 bytes (nine dwords copied). RW 0xDA28C0 is the saved copy, RW 0xDA28E4 the active one; both are zero before an INI is read
//    (no constructor writes them; the only references are the parser and the reset). The parser copies saved -> active, reads the block into
//    active, and copies active -> saved unless the load type is INI_LOAD_CREATE_OVERRIDES (map.ini) or 4; it sets the dirty byte RW 0xDE4ED4 either way.
//  - RW 0x70B5B1 copies saved -> active and sets the dirty byte (called by RW 0x6478C0: a map.ini override is undone this way); RW 0x70B5CC returns the
//    active record, which the glow passes read (RW 0x4878E9 / 0x487A24 GlowEnabled, 0x487AE3 MultipassGlowEnabled, 0x46FFCA TerrainGlow, 0x4FA8E7
//    the kernel parameters, 0x4FA801 GlowIntensity).
//  - RotWK data: Data\INI\environment.ini sets GlowEnabled = Yes, GlowDiameter = 15, GlowIntensity = 1.3, GlowTextureWidth = 256, RadiusScale1 = 0.18,
//    Amplitude1 = 0.06, RadiusScale2 = 4.5, Amplitude2 = 0.11, TerrainGlow = No, MultipassGlowEnabled = No. maps\shellmap1\map.ini overrides it
//    (GlowDiameter 20, GlowIntensity 0.5; the other rows as the default).

#pragma once

class INI;
class INIBlockRegistry;
struct FieldParse;

// The retail record (RW 0xDA28E4), field for field.
struct GlowEffect
{
	bool glowEnabled = false;            // + 0x00
	int glowDiameter = 0;                // + 0x04
	float glowIntensity = 0.0f;          // + 0x08
	int glowTextureWidth = 0;            // + 0x0C
	float radiusScale1 = 0.0f;           // + 0x10
	float amplitude1 = 0.0f;             // + 0x14
	float radiusScale2 = 0.0f;           // + 0x18
	float amplitude2 = 0.0f;             // + 0x1C
	bool terrainGlow = false;            // + 0x20
	bool multipassGlowEnabled = false;   // + 0x21
};

// The saved and active copies of one world's INI state (RetailObjectWorld owns one; reloading the world makes a fresh one).
class GlowEffectStore
{
public:
	// RW 0x70B5CC
	const GlowEffect &active() const { return m_active; }
	const GlowEffect &saved() const { return m_saved; }
	// RW 0xDE4ED4: set by every parse and reset; the device clears it when it rebuilds the kernel (RW 0x4FA8F5)
	bool dirty() const { return m_dirty; }
	void clearDirty() { m_dirty = false; }
	// RW 0x70B734
	void parse(INI *ini);
	// RW 0x70B5B1
	void reset();
	// registers "GlowEffect" (the stub of the recording list is skipped for it)
	void registerBlock(INIBlockRegistry &registry);
	// RW 0xC1E810
	static const FieldParse *fieldTable();

private:
	GlowEffect m_saved;
	GlowEffect m_active;
	bool m_dirty = false;
};
