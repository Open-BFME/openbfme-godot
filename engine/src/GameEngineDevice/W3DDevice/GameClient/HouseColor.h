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

#include <map>
#include <string>
#include <vector>

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
