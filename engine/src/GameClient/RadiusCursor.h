// OpenBFME. GPL-3.0.
//
// RadiusCursor (lane SPELL-4): the aiming marker of a command that wants a position and names a RadiusCursorType, e.g. a spell book power (the owner,
// playing v0.4.0-alpha.3: "it's just a circle"; the port drew a green ring of its own). RotWK draws the InGameUI.ini RadiusCursorTemplate the button names,
// a ground decal of the power's RadiusCursorRadius, throbbing, white over a valid target and red over an invalid one, and hides the pointer.
//
// TARGET FACTS (RotWK game.dat, caveat S-001; the BFME2 twins are tier A / B with byte-matched decomp source where named):
// - InGameUI's field `RadiusCursorTemplate = <name>` (row RW 0xC12xxx -> RW 0x6A4E27, tier A: BFME2 decomp InGameUISuperweapons.cpp:471
//   InGameUI::parseNamedRadiusDecalTemplate): the name's NameKey, a RadiusDecalTemplate built by its constructor (RW 0x732888: Texture / Texture2 empty,
//   Style 0x20 = SHADOW_ALPHA_DECAL, OpacityMin 1.0, OpacityMax 1.0, OpacityThrobTime 1000, Color 0, OnlyVisibleToOwningPlayer yes, MinRadius / MaxRadius /
//   RotationsPerMinute / SpiralAcceleration 0, MaxSelectedUnits 0) and parsed with the 13-row table RW 0xC24278 (RW 0x7327BC, the table the selection
//   decal uses too), stored in InGameUI's hash map (a duplicate name is reported in debug builds only and replaces the earlier one);
// - the button's RadiusCursorType is kept as a NameKey (S-200); the lookup at use (RW 0x6B4E8F) finds nothing for an unknown name and no decal is made;
// - InGameUI::setRadiusCursor (vslot 0x140, RW 0x6A077A; ZH InGameUI::setRadiusCursor is the donor of the name): the same type as the current one only
//   recolours; else setRadiusCursorNone (vslot 0x144, RW 0x69B0E1 -> RadiusDecal::clear RW 0x7327E5), the template by name (none: nothing), the object the
//   radius belongs to (for a SPELL_BOOK command, type 0x20, the local player's spell book object RW 0x6AD0D9; else the first selected drawable's), its
//   controlling player (RW 0x68B678); the radius is the SpecialPower's RadiusCursorRadius (+ 0x54, through RW 0x688D3C's final override), nothing when it is
//   not above 0; RadiusDecalTemplate::createRadiusDecal (RW 0x732DE7, tier B same-shape: BFME2 decomp RadiusDecalTemplateCreate.cpp:60): nothing without a
//   player or a Texture or radius <= 0; with OnlyVisibleToOwningPlayer and a player other than the local one the decal exists but is never made (empty);
//   else Texture / Texture2 in the Style, 2 * radius on both axes, angle 0, the template's Color or (Color 0) the player's colour; then the hint colour:
//   Shadow::setColor (RW 0x73232B, BFME2 decomp Shadow_setOpacity.cpp) with 0xFFFFFFFF when the hint was valid, 0xFFFF1414 when not
//   ((-(valid) & 0xEBEB) - 0xEBEC): the RGB is kept (& 0xFFFFFF) and, for the alpha styles (0x1420), the opacity is the alpha; the additive styles
//   (0x10840) scale the RGB by the opacity instead;
// - InGameUI::createCommandHint (RW 0x6A3B77) for a pending command whose button names a RadiusCursorType: the pointer is set to cursor 0 (None: no
//   Mouse.ini block, no pointer; RW 0x69BD53 -> RW 0x69B06D) and setRadiusCursor(type, power, slot, hint == 0xAE (MSG_VALID_GUICOMMAND_HINT));
// - CommandTranslator (RW 0x81EA88 .. 0x81EBEE): for a SPELL_BOOK command the hint / the click is valid when RW 0x6A4260(button, no object, the location,
//   0, options, the spell book) answers yes, i.e. RW 0x82DFB7(book, location, 0, power, 0, options, 1) (GameLogic/ActionManager.h; the last argument 1
//   also requires RW 0x68DF46 and the power's module 100% ready); valid: hint 0xAE, a click issues the power; invalid: hint 0xAF, a click issues nothing;
// - each client frame InGameUI::handleRadiusCursor (BFME2 decomp InGameUI_handleRadiusCursor.cpp, its RotWK twin not mapped) puts the decal at the
//   terrain point under the pointer, then RadiusDecal::update (RW 0x732926, tier A: BFME2 decomp RadiusDecal.cpp:80): opacity = ((max - min) * p + min)
//   * 255, p = (sin(2 pi * (client frame mod n) / n) + 1) / 2, n = max(ceil(OpacityThrobTime * 0.03), 1) client frames (0 while the UI icons are hidden);
//   RotationsPerMinute turns it (angle = client frame * (1 / 30) * (pi / 180) * RotationsPerMinute * 60).
// INFERENCE (stop S-4640): the radar's pixel-to-world and the shroud's second terrain pass of handleRadiusCursor are not ported (the pointer's terrain
//   point is the view's); the spiral branch of RadiusDecal::update (RotationsPerMinute, SpiralAcceleration and MaxRadius all above 0; no retail
//   RadiusCursorTemplate sets SpiralAcceleration) and the `radius > 110` flag (Shadow + 0x64, meaning not read) are not used; the decal is drawn as the
//   selection decals are (S-2522: a Godot Decal over the terrain layer, not RotWK's projected terrain mesh).

#pragma once
#include "GameClient/PlayerMessageLog.h"

#include "Common/INIDataTypes.h"

#include <cstdint>
#include <map>
#include <string>

class ArchiveFileSystem;

// RadiusDecalTemplate (RW 0x732888 defaults, RW 0xC24278 fields)
struct RadiusDecalTemplate
{
	std::string texture, texture2;     // + 0x00, + 0x04
	std::uint32_t style = 0x20;        // + 0x08 SHADOW_ALPHA_DECAL
	float opacityMin = 1.0f;           // + 0x0C (percent / 100)
	float opacityMax = 1.0f;           // + 0x10
	float opacityThrobTime = 1000.0f;  // + 0x14 milliseconds
	std::uint32_t color = 0;           // + 0x18 0xAARRGGBB (0: the player's colour)
	bool onlyVisibleToOwningPlayer = true; // + 0x1C
	float minRadius = 0.0f, maxRadius = 0.0f; // + 0x20, + 0x24
	unsigned maxSelectedUnits = 0;     // + 0x28
	float rotationsPerMinute = 0.0f;   // + 0x2C
	float spiralAcceleration = 0.0f;   // + 0x30
};

// lane OWNERFIX-2: InGameUI.ini's resource claim fields (rows RW 0xC12860 .. 0xC12890): TerrainResourceClaimDecal (RW 0x69C374: parseAsciiString, handed to
// the claim feedback holder InGameUI + 0x58C, RW 0x8EC442), PlaceTerrainResourceClaimantDecal (parseAsciiString RW 0x42EE5E, + 0x574),
// PlaceTerrainResourceClaimantFont (parseFontDesc RW 0x778C93, + 0x578: quoted name, size, bold) and PlaceTerrainResourceClaimantFontColor (parseColorInt
// RW 0x42F13E, + 0x584). InGameUI's constructor defaults were not read: the retail file sets all four (INFERENCE, S-5030: empty / 0 when a mod omits one)
struct TerrainResourceUiSettings
{
	std::string claimDecal;      ///< TerrainResourceClaimDecal (the selected resource building's claim decals)
	std::string placeDecal;      ///< PlaceTerrainResourceClaimantDecal (the placement decal)
	std::string fontName;        ///< PlaceTerrainResourceClaimantFont
	int fontSize = 0;
	bool fontBold = false;
	std::uint32_t fontColor = 0; ///< PlaceTerrainResourceClaimantFontColor, ARGB
};

// InGameUI's RadiusCursorTemplate map (InGameUI.ini)
class RadiusCursorTemplates
{
public:
	const PlayerMessageSettings &messageUi() const { return m_messageUi; }
	const TerrainResourceUiSettings &resourceUi() const { return m_resourceUi; } // lane OWNERFIX-2
	// Data\INI\InGameUI.ini from the mount; false with `error` when it is missing or does not parse
	bool load(ArchiveFileSystem &fs, std::string *error);
	// the same from text (tests)
	bool scan(const std::string &inGameUiIni, std::string *error);
	// RW 0x6B4E8F: null for a name the file has no template of
	const RadiusDecalTemplate *find(const std::string &name) const;
	size_t size() const { return m_templates.size(); }
	const std::map<std::string, RadiusDecalTemplate> &all() const { return m_templates; } // keyed by the name lower-cased (NameKeys ignore case)

private:
	std::map<std::string, RadiusDecalTemplate> m_templates;
	TerrainResourceUiSettings m_resourceUi;
	PlayerMessageSettings m_messageUi;
};

// the decal the device draws this frame
struct RadiusCursorDecal
{
	bool visible = false;      ///< a decal exists and was made (OnlyVisibleToOwningPlayer for another player: none)
	std::string type;          ///< the RadiusCursorType
	std::string texture, texture2;
	std::uint32_t style = 0;
	Coord3D position{ 0.0f, 0.0f, 0.0f };
	float size = 0.0f;         ///< 2 * radius, both axes
	float angle = 0.0f;        ///< radians
	std::uint32_t rgb = 0;     ///< the colour the decal is tinted with (0xRRGGBB)
	int opacity = 0;           ///< 0..255 (RadiusDecal::update)
	bool valid = true;         ///< the last hint
	std::uint32_t packed = 0;  ///< what Shadow::setColor stores: alpha styles opacity << 24 | rgb; additive styles rgb * opacity / 255, alpha 0
};

// InGameUI + 0x888 / + 0x88C: the current cursor type and its RadiusDecal
class RadiusCursor
{
public:
	// RW 0x6A077A. `tmpl` null (unknown type) or radius <= 0 or no controlling player: no decal (cleared). `playerColor` 0xRRGGBB of the radius's player.
	void set(const std::string &type, const RadiusDecalTemplate *tmpl, float radius, bool havePlayer, bool playerIsLocal, std::uint32_t playerColor, bool valid);
	// RW 0x69B0E1
	void clear();
	bool active() const { return !m_type.empty(); }
	const std::string &type() const { return m_type; }
	// handleRadiusCursor + RadiusDecal::update (RW 0x732926) at `clientFrame`; `drawIconUI` false: opacity 0
	void update(const Coord3D &position, unsigned clientFrame, bool drawIconUI);
	const RadiusCursorDecal &decal() const { return m_decal; }

	// RadiusDecal::update's opacity for a template at a client frame (0..255)
	static int throbOpacity(const RadiusDecalTemplate &t, unsigned clientFrame, bool drawIconUI);
	// Shadow::setColor (RW 0x73232B): the stored colour of a decal of `style` with `color` and `opacity`
	static std::uint32_t packedColor(std::uint32_t style, std::uint32_t color, int opacity);
	static constexpr std::uint32_t kValidColor = 0xFFFFFFFFu;   // (-1 & 0xEBEB) - 0xEBEC
	static constexpr std::uint32_t kInvalidColor = 0xFFFF1414u; // (0 & 0xEBEB) - 0xEBEC

private:
	std::string m_type;
	const RadiusDecalTemplate *m_template = nullptr;
	RadiusCursorDecal m_decal;
};
