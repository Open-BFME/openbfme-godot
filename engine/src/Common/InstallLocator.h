// OpenBFME. GPL-3.0.
//
// Lane RELEASE-1: finding the player's RotWK 2.01 and BFME2 1.06 installs on a fresh machine, checking a folder the
// player picked, and remembering the choice. Client / launcher code (no simulation state); the mount itself
// (Common/RetailArchivePolicy.h: MountRetailArchives) still verifies every archive's size and md5.
//
// Where retail and the community launchers look (target facts are from rotwk201_game.exe, "RW <VA>"):
//  * RotWK's own registry key is its gi.dat GameRegPath, "SOFTWARE\Electronic Arts\Electronic Arts\The Lord of the
//    Rings, The Rise of the Witch-king" (RW 0xAAA6C0 reads gi.dat from the install folder; the retail gi.dat holds
//    that value, pinned by test_release1_install.cpp against the real file). RW 0x9787C9 reads its value
//    "InstallPath" (string at RW 0xC850F4) through RW 0x64148E, which tries HKEY_LOCAL_MACHINE (0x80000002) first
//    and then HKEY_CURRENT_USER (0x80000001).
//  * RotWK finds the BFME2 install through HKLM "SOFTWARE\Microsoft\Windows\CurrentVersion\App Paths\lotrbfme2.exe",
//    value "Path" (RW 0x638D22: RegOpenKeyExW(0x80000002, RW 0xBE8368, KEY_READ), RegQueryValueExW(L"Path"), then
//    "\" appended; on failure ERROR:BaseGameNotInstalled).
//  * retail is a 32-bit process, so on 64-bit Windows it sees HKLM's 32-bit view (WOW6432Node): we read that view.
//  * inference (not read in the binary): BFME2's own key is its gi.dat GameRegPath "SOFTWARE\Electronic Arts\
//    Electronic Arts\The Battle for Middle-earth II" with the same "InstallPath" value, and RotWK has an App Paths
//    entry "lotrbfme2ep1.exe" like BFME2's. The BFME All-in-One launcher writes the GameRegPath InstallPath values
//    (the archived OpenBFME launcher, RetailDiscovery.TryReadAllInOneRegistry, mirrors its BfmeKit defaults).
//  * inference: the EA installers' default folders ("<Program Files>\EA Games\<game>" and "...\Electronic Arts\
//    <game>", BFME2 also as "The Battle for Middle-earth (tm) II"). Only a hint list; every hit is checked.
//  * Linux: the same registry values read from the text registry (system.reg = HKLM, user.reg = HKCU) of every Wine
//    prefix found ($WINEPREFIX, ~/.wine, Steam / Proton compatdata of every Steam library, Lutris, Bottles, Heroic,
//    PlayOnLinux), the Windows path mapped through the prefix's dosdevices.
//
// Nothing found is not an error: the first-run screen then asks for the folders. Nothing is used silently either:
// discovered folders are offered to the player, who confirms them (PLAN rule 10).
//
// The only retail facts here are the registry key / value names and folder hints needed to look for an install before
// any retail file can be read; which archives make an install is the policy's (engine/data/retail-archives).

#pragma once

#include "Common/RetailArchivePolicy.h"

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace InstallLocator
{

// "rotwk" or "bfme2" (RetailInstall::label).
extern const char *const kRotwk;
extern const char *const kBfme2;

// The environment variables / config keys (ResolveInstallPath) of each game.
std::string configKey(const std::string &game); // "ROTWK_INSTALL", "BFME2_INSTALL"

struct RegistryQuery
{
	std::string hive;   // "HKLM" or "HKCU"
	std::string key;    // without the hive, e.g. "SOFTWARE\Electronic Arts\..."
	std::string value;  // e.g. "InstallPath"
	std::string why;    // the citation (target fact or inference)
};

// The registry values that name a game's folder, in the order they are tried.
std::vector<RegistryQuery> registryQueries(const std::string &game);

// Folder names below a Program Files style root (inference, see above).
std::vector<std::string> folderHints(const std::string &game);

// Reads a value of the host's registry (Windows; 32-bit view of HKLM like retail). Returns false when absent.
using RegistryReader = std::function<bool(const RegistryQuery &query, std::string &out)>;

struct DiscoveryEnvironment
{
	bool windows = false;
	std::string home;                       // HOME (Linux) / USERPROFILE (Windows)
	std::map<std::string, std::string> env; // WINEPREFIX, XDG_DATA_HOME, ProgramFiles(x86), ProgramFiles, ProgramW6432
	std::vector<std::string> drives;        // Windows: the fixed drive roots ("C:\", "D:\")
	RegistryReader registry;                // Windows only (empty elsewhere)

	// The machine this runs on.
	static DiscoveryEnvironment host();
};

struct Candidate
{
	std::string game;   // kRotwk / kBfme2
	std::string path;   // host path of the folder (exists)
	std::string source; // where it was found, e.g. "registry HKLM ...\InstallPath" or "Wine prefix ~/.wine: registry ..."
};

// Every existing folder found for either game, de-duplicated, most trustworthy first (registry before folder hints).
std::vector<Candidate> discover(const DiscoveryEnvironment &environment);

// ---- Wine helpers (exposed for tests) ----

// The Wine prefixes on this machine (directories holding system.reg), in search order.
std::vector<std::string> winePrefixes(const DiscoveryEnvironment &environment);

// A value from a Wine text registry file (system.reg / user.reg): key without the hive ("Software\\..." unescaped,
// case-insensitive), value name (case-insensitive). REG_SZ and str(2) values; Wine's escapes are decoded to UTF-8.
bool wineRegistryValue(const std::string &regText, const std::string &key, const std::string &value, std::string &out);

// A Windows path ("C:\Program Files\x") inside a Wine prefix: dosdevices/<drive>: (or drive_c for C:), then each
// component matched case-insensitively. Empty when the drive is not mapped.
std::string wineToHost(const std::string &prefix, const std::string &windowsPath);

// ---- checking a folder ----

struct InstallCheck
{
	bool ok = false;
	std::string game;
	std::string root;
	std::vector<std::string> errors;   // player-facing, each naming what is wrong
	std::vector<std::string> missing;  // policy archives not found (canonical paths)
	std::vector<std::string> wrongSize; // "path (size N, 2.01 has M)"
	std::vector<std::string> unknown;   // archives in the folder that are neither in the policy nor known exclusions
	int present = 0;                    // policy archives found with the right size
	int expected = 0;
};

// Is `root` the folder of `game` described by `policy`? Fast: presence and size of every policy archive
// (case-insensitive like the mount) and no unknown archives; the md5s are the mount's job. `other` (the other game's
// policy, may be null) only improves the message for a swapped pick.
InstallCheck checkInstall(const std::string &game, const std::string &root, const RetailArchivePolicy &policy, const RetailArchivePolicy *other);

// Both folders with the built-in policies (rotwk-201, bfme2-106).
std::vector<InstallCheck> checkPair(const std::string &rotwkRoot, const std::string &bfme2Root);

// ---- the remembered choice ----

struct Configured
{
	std::string source; // "env" (ROTWK_INSTALL / BFME2_INSTALL both set), "config" (both from the file), "none"
	std::string rotwk;
	std::string bfme2;
};

// Both installs from the environment (developer / test override) or both from the config file; a half-configured
// state is "none" with what was found filled in.
Configured configured(const std::string &configPath);

// Writes ROTWK_INSTALL= / BFME2_INSTALL= to the config file (replaced atomically: temp file + rename).
bool remember(const std::string &configPath, const std::string &rotwkRoot, const std::string &bfme2Root, std::string *error);

} // namespace InstallLocator
