// OpenBFME: faithful rebuild of The Battle for Middle-earth II: Rise of the Witch-king 2.01.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// data\ini\housecolor.ini: the BFME-only HouseColor blocks mapping a unit's base texture to its team colour mask texture.
//
//   HouseColor
//       BaseTexture  = GUHbtShfA.tga
//       HouseTexture = HC_GUHbtShfA.tga
//   End
//
// Donor facts (Open-BFME-1 GameEngine/Source/GameClient/HouseColorSystem.cpp parseHouseColor and
// HouseColorRegister.cpp bfmeRegisterHouseColor, retail 0x00900D70): each block is parsed into a HouseColorSystem with
// two string fields and registered as key = the texture prototype id of BaseTexture, value = HouseTexture string; a later
// block with the same key replaces the earlier one (std::map operator[]). Here the key is the lower-cased base texture
// name. Retail's id is -1 for a base texture that is not in the asset registry, so all such blocks collide in retail; the
// table does not reproduce that. Unknown fields or stray lines are errors.
//
// NOT ported: how the house colour texture is combined with the base texture (the defaultw3d.fxo HouseColorEnable /
// HouseColorTexture path); spec 3.6 marks the formula [U]. This is the lookup table only.

#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

// Lane CAH-2: RotWK's house colour recolour of a texture (target facts, RotWK game.dat, caveat S-001):
//   * the options (BFME2 decomp Rva0013101E, RenderAssetParseBFME2.cpp): a kind in the low 3 bits (0 none, 1 .. 3 = the number of colours) and three
//     ARGB colours; a Create-a-Hero gets kind 3 with its record's PrimaryColor / SecondaryColor / TertiaryColor (+0x2C / +0x30 / +0x34: RW 0x80AF0B ->
//     RW 0x80959A, then RW 0x6727B0 hands them to every draw module's object-draw interface, W3DModelDraw vslot 0x7C RW 0x4B877D -> the render object's
//     Set_House_Color_Params vslot 0x1F8; a player's team colour is kind 1 with the player's colour, BFME2 0x4B8FA1);
//   * MeshClass::RecolorHouseColor (RW 0x54BDE0 = BFME2 0x54C470) recolours the mesh's house colour texture into a new texture "#<texture>#<options>";
//   * the texel recolour RW 0x531C77 (BFME2 0x5321A7, tier A, no decomp source; read from the disassembly) on an A8R8G8B8 surface: each colour unpacked
//     as (R, G, B) by RW 0x53184D, then per texel with the source channels R, G, B (alpha kept):
//       kind 0: the texel unchanged; kind 1: out = (R * c0) >> 8;
//       kind 2: out = min(255, ((R * c0) >> 8) + ((G * c1) >> 8)); kind 3: out = min(255, ((R * c0) >> 8) + ((G * c1) >> 8) + ((B * c2) >> 8)),
//     per output channel with that channel of each colour. An A4R4G4B4 surface takes a different single colour path (not ported: the house textures are
//     32-bit TGA files), other formats (DXT) are left as they are.
struct HouseColorParams
{
	int kind = 0;                                 ///< 0 .. 3 (the options' low 3 bits)
	std::uint32_t colors[3] = { 0u, 0u, 0u };     ///< ARGB; only R, G, B are used
};

// RW 0x531C77's A8R8G8B8 texel: `rgba` in and out as R, G, B, A bytes
void Recolor_House_Texel(const HouseColorParams &params, const std::uint8_t in[4], std::uint8_t out[4]);
// lane CAH-2 r3: every texel of an R, G, B, A image recoloured in place (the device bakes the texture with it BEFORE any filtering or mipmap, as
// retail recolours the surface's texels: filtering first and recolouring the filtered value differs where >> 8 truncates or the sum clamps)
void Recolor_House_Pixels(const HouseColorParams &params, std::uint8_t *rgba, std::size_t texels);

class HouseColorTable
{
public:
	// False + *error on a malformed block. ';' and '//' start comments.
	bool Parse(const std::string &iniText, std::string *error);

	// Base texture name as in the INI (case-insensitive) -> house texture name as written. nullptr when none.
	const std::string *Find(const std::string &baseTexture) const;

	size_t Block_Count() const { return Blocks; }
	size_t Base_Count() const { return Map.size(); }
	size_t House_Texture_Count() const; // distinct house texture names
	const std::vector<std::string> &Replaced() const { return ReplacedKeys; } // base textures registered more than once

private:
	std::map<std::string, std::string> Map;
	std::vector<std::string> ReplacedKeys;
	size_t Blocks = 0;
};
