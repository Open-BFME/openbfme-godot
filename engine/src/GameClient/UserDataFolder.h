// OpenBFME. GPL-3.0.
//
// UserDataFolder (lane CAH-2): where RotWK keeps the player's own files (Options.ini's folder, the Create-a-Hero saves), read from the install's gi.dat.
// Client code: no simulation state.
//
// TARGET FACTS (RotWK game.dat, caveat S-001):
//   * RW 0xAAA6C0 reads "gi.dat" (RW 0xCFE5F4) from the install folder: the 32-bit magic 0x20204947 ("GI  "), a 32-bit count, then count pairs of
//     NUL-terminated strings (key, value); RW 0xAAA5A0 keeps the value of the keys SkuName, GameName, GameRegPath, InstallerRegPath, OnlineServer,
//     UserDataLeafName, G1 .. G4 (compared with _strcmpi) and ignores any other key. A missing or bad file leaves every value "GI_DAT_ERROR"
//     (RW 0xDC1340 ..);
//   * the user data folder (GlobalData + 0x1240, set by RW 0x644148): SHGetSpecialFolderPathW(0, buf, CSIDL_APPDATA 0x1A, create 1), a '\' appended when
//     missing, the leaf name, '\' again, CreateDirectoryW. The leaf name (RW 0x64126F) is the registry value "UserDataLeafName" under GameRegPath
//     (RW 0x641160 -> RW 0x64148E: HKLM, then HKCU) and, when there is none, gi.dat's UserDataLeafName (RW 0xAAA8E0); RotWK 2.01's gi.dat holds
//     "My The Lord of the Rings, The Rise of the Witch-king Files";
//   * the Create-a-Hero saves are "MyHero*.cah" in the user data folder + "Save\" (RW 0x6DD398 with L"Save\" RW 0xC1A320; CreateAHeroHeroList.h).
// OpenBFME DIFFERENCES (stop S-1407): the registry value is not read (the leaf name is gi.dat's; the installer writes the same text); a missing or bad gi.dat
// is an error (retail's "GI_DAT_ERROR" folder is not reproduced); the application data folder is the device's (Godot OS::get_data_dir: %APPDATA% on
// Windows as CSIDL_APPDATA, the XDG data folder on Linux).

#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace UserDataFolder
{

// RW 0xAAA6C0 / 0xAAA5A0: the known keys of a gi.dat image (key spelling as the file has it, matched without case). False + *error for a bad image.
bool parseGameInfo(const std::vector<std::uint8_t> &bytes, std::map<std::string, std::string> &values, std::string *error);
// the UserDataLeafName of a gi.dat image ("" + *error when the image is bad or has none)
std::string leafName(const std::vector<std::uint8_t> &bytes, std::string *error);
// RW 0x644148: appData + separator + leaf + separator (`separator` '\\' as retail; the device passes '/')
std::string userDataFolder(const std::string &appData, const std::string &leaf, char separator = '\\');
// RW 0x6DD398: the Create-a-Hero save folder, the user data folder + "Save" + separator
std::string heroSaveFolder(const std::string &userData, char separator = '\\');

} // namespace UserDataFolder
