// OpenBFME. GPL-3.0.
//
// Lane MODS-1 (PLAN rules 6 and 7, ROADMAP MOD-1): RotWK 2.01's `-mod` switch.
//
// What retail does (read from the RotWK binary; the BFME2 1.06 decomp, Open-BFME-2 Common/CommandLineMod.cpp, Rva00600777.cpp and
// GUI/FileSystemOpenFile.cpp, has the same code; ZH CommandLine.cpp parseMod and ArchiveFileSystem::loadMods are the donors):
//  * parseMod RW 0x7BADB9 (pre-file-system option table RW 0xC35DA8): sets the prefer-local-files flag RW 0xDEC490 first, before any
//    check (RW 0x7BADD0). A path with no ':' that starts with neither '/' nor '\' is taken below the user data folder (RW 0x6427C8,
//    GameClient/UserDataFolder.h). A path that does not exist is ignored (TheFileSystem doesFileExist RW 0xA14220, then _stat). A folder
//    (st_mode & _S_IFDIR) gets a trailing '\' and becomes GlobalData + 0xD38 (m_modDir), anything else GlobalData + 0xD3C (m_modBIG).
//    A second -mod replaces the first value of its kind.
//  * the command-line tail RW 0x7BAA44, after the retail archives are mounted: m_modBIG goes to TheArchiveFileSystem vslot 0x14
//    (loadBigFile RW 0xA183AA, overwrite 1); then m_modDir goes to RW 0xA14313: strcpy(RW 0xDEC498, modDir), the flag, and vslot 0x20
//    (loadBigFilesFromDirectory RW 0xA17FB0, "*.BIG", overwrite 1). That one lists the folder's DIRECT children only (the local file
//    system's listing with searchSubdirectories 0) into a FilenameList and mounts them in the set's order. With overwrite the archive
//    mounted LAST wins a name: a mod beats retail, a folder archive beats m_modBIG, and inside the folder the name that sorts last wins.
//    The FilenameList set compares without case: RW 0x42C33B (find RW 0x42C450, insert RW 0x42C72E) is AsciiString::compareNoCase, msvcr71
//    _memicmp over the shorter length, then the shorter name first (lane MODS-2; the local listing RW 0xA18B2E stores the names unchanged).
//  * loose files: see Common/ArchiveFileSystem.h (RW 0xA149A2 / 0xA14AEB / 0xA14D2B).
//  * movies: RW 0x490D1C adds "<modDir>Data\Movies\" as the "Mod Path" movie folder (GameClient/VideoPlayer.h `modRoot`).
//
// Deviations, each by a standing rule:
//  * "no fallbacks" (CLAUDE.md, PLAN rule 10): a -mod path that does not exist, an archive that does not parse (retail's loadBigFile
//    returns false and goes on, RW 0xA183D3 / 0xA1841F) and an unreadable mod file are errors, and nothing of the mod is mounted.
//  * PLAN rule 7: retail's two install-folder lookups ("lang\<language>\<file>" and "<file>" in the game folder) answer before the
//    archives once the flag is set. A clean install has no loose file there that an archive also has (only movies, cursors, DLLs and
//    asset.dat), so they never decide anything. The mount checks that: a loose file in the RotWK install that would shadow an archive
//    entry under -mod is a mount error naming it (contamination). Retail would read it.
//  * the mod folder is frozen at mount (ModLooseIndex): every loose file is listed, sized and hashed once, the identity carries their digest,
//    and a file that changes afterwards is a read error. Retail asks the disk on every open.
//
// Not supported (an error, never a silent default): a symbolic link inside the mod folder (it could leave the folder or loop); -preferLocalFiles (RW 0x7B9EBE) without a mod, and the flag retail sets itself when
// the local file system has no "shaders.big" (RW 0xA142F2, a developer layout); both would read loose install files.

#pragma once

#include "Common/ArchiveFileSystem.h"
#include "Common/RetailArchivePolicy.h"

#include <cstdint>
#include <string>
#include <vector>

class Win32BIGFileSystem;

// RW 0xDBACD0's initial data: the install language of the "lang\<language>\" lookups. The installs the port mounts are English (S-1561).
extern const char *const kRetailInstallLanguage;

// GlobalData's two -mod fields (RW GlobalData + 0xD38 / + 0xD3C)
struct ModOperand
{
	std::string dir;     // m_modDir, ends with a separator
	std::string archive; // m_modBIG
	bool empty() const { return dir.empty() && archive.empty(); }
};

// parseMod RW 0x7BADB9 for one `-mod <path>`: resolves `path` (relative paths below `userDataDir`), classifies it, and stores it in
// `io` (a second call replaces the field of its kind, as retail does). False + *error when the path does not exist (retail ignores it).
bool ParseModArgument(const std::string &path, const std::string &userDataDir, ModOperand &io, std::string *error);

struct ModMountOptions
{
	std::string md5CachePath;      // the mount's md5 cache (Common/Md5Cache.h); "" = hash every file on every mount
	std::string rotwkInstallRoot;  // the local file system's root (the game folder): checked for loose files that would shadow archives
};

struct ModMountReport
{
	bool ok = false;
	std::vector<std::string> errors;
	std::vector<MountedArchive> archives; // the mod's archives in mount order (install "mod"; the LAST one holding a name wins)
	ModLooseIndex loose;                  // the mod folder's loose files (empty for an archive mod)
	std::string folderName;               // the mod folder's own name ("" for an archive mod)
	std::vector<std::string> identity;    // the ProfileIdentity lines of this mod (ProfileIdentity::compute): bounded, whatever the mod's size
	std::vector<std::string> manifest;    // every archive (mount order) and loose file (byte order) with md5 and size: "modcontent"'s digest input
	std::vector<std::string> stops;       // the stop lines a mounted mod reports (S-5300)
	std::vector<std::string> warnings;    // non-fatal (the md5 cache could not be written)
	std::vector<std::string> notes;       // lane MODS-2: files of an install overlay that are not archives (not used)
	std::vector<MountedArchive> merged;   // lane MODS-2: an install overlay's whole mount order (retail + mod archives; first wins)
	int filesHashed = 0, md5CacheHits = 0;
};

// The -mod tail RW 0x7BAA44 over an already-mounted retail file system: m_modBIG, then m_modDir's direct *.big archives, both with
// overwrite, then the mod folder's loose layer and the flag. Transactional: on any error nothing of the mod is mounted.
ModMountReport MountRetailMod(Win32BIGFileSystem &fileSystem, const ModOperand &mod, const ModMountOptions &options);

// The order RW 0xA17FB0 mounts a mod folder's archives in: the FilenameList set's order (case-insensitive ascending), each with
// overwrite, so the last one wins. Exposed for tests.
std::vector<std::string> ModFolderArchiveOrder(std::vector<std::string> names);

// The stop line every mounted mod reports (docs/STOPS.md S-5300).
std::string ModMountStopLine();

// Lane MODS-2: an INSTALL-FOLDER mod (an "install overlay"): the mod's archives sit in the game folder itself, next to the retail ones, as the
// All In One launcher installs its patch-type packages (Edain, Edain Unchained, Ennorath: Type 0 entries, laid over the base package's files,
// BfmeWorkshopSyncManager.Sync L2 over L4) and as most mod installers do. Retail then has no -mod: the startup mount (RW 0xA14419) reads the
// install's archives with overwrite 0, the mod's among them, and a mod archive with a retail archive's path (Edain's lang\English.big) replaces
// that file. The port keeps the verified retail install untouched and the mod in its own folder (`overlay`: the paths below it are the paths
// in the game folder) and builds the file system the merged folder would give: every RotWK archive of `retail` whose path the overlay does not
// replace, plus every *.big of the overlay, in the RotWK install's load order (RetailArchiveLoadOrder: RotWK's startup order RW 0xA1766D over the merged
// canonical paths, stop S-5301), first one winning, then the BFME2 archives as `retail` mounted them. Non-archive files of the overlay are not used (listed in `notes`). The identity lines name the overlay
// ("mod install <folder>") and digest its archives in mount order. Transactional: on any error `out` is left empty.
ModMountReport MountInstallOverlay(Win32BIGFileSystem &out, const RetailMountReport &retail, const std::string &overlay, const ModMountOptions &options);
// several folders, highest priority first (an All In One package's enhancements, the package, the patches it depends on: WorkshopKit's L1 .. L3);
// the first folder holding a path wins it (noted in `notes`)
ModMountReport MountInstallOverlay(Win32BIGFileSystem &out, const RetailMountReport &retail, const std::vector<std::string> &overlays,
	const ModMountOptions &options);
std::string InstallOverlayStopLine(); // docs/STOPS.md S-5301
