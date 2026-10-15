// OpenBFME. GPL-3.0.
//
// Which retail archives make up "pure RotWK 2.01", and how they are mounted.
//
// RotWK is an expansion: it runs from its own install and also reads the BFME2 install.
// Pure 2.01 = the RotWK 2.01 archives + the BFME2 1.06 archives, each pinned by path, size
// and md5 in engine/data/retail-archives/*.json (copied from the retail packages; names,
// sizes and hashes only).
//
// Load order (first mounted wins, ZH ArchiveFileSystem::loadIntoDirectoryTree overwrite=false):
//  1. every RotWK archive, then every BFME2 archive. ZH does the same for its base game:
//     Win32BIGFileSystem::init loads its own "*.big" and then the original Generals install.
//     Every cross-install overlap in the retail files (RotWK INI.big/W3D.big/lang vs BFME2
//     ini.big/w3d.big/_patch103.big) is RotWK re-shipping a newer copy, which only works if
//     RotWK wins.
//  2. within one install, BFME's order: stable_sort by strcmp then reverse, applied to the
//     retail-canonical relative path with backslashes (e.g. "apt\MainMenu.big",
//     "lang\englishpatch201.big"). Canonical names come from the policy, not the disk, so a
//     copy whose files were renamed to lower case (e.g. "ini.big") still gets retail order.
//     With retail names this puts _patch201*.big ahead of INI.big/W3D.big/Maps.big ('_' > 'I')
//     and lang\englishpatch201.big ahead of lang\English.big ('e' > 'E'): the patches win.
//
// Layouts (lane CONTRIB-1, GitHub issue #21): one game version can sit on disk in more than one known way. RotWK 2.01 has two:
//  * "aio": the All In One package's, the policy's `archives` list (the 2.00 INI.big / Maps.big plus _patch201ini.big /
//    _patch201maps.big, which outrank them by the load order above);
//  * "ea-official-patch": a disc install patched with EA's official 2.01 patch (LotrBfMe2EP1-131073-english.exe), which rewrites
//    INI.big and Maps.big in place and never writes _patch201ini.big / _patch201maps.big. Its two archives are pinned by size and md5
//    from the tester's install (the issue). The AIO's base archive + patch archive merged entry by entry in mount order
//    (INI.big: 763 entries, 75 replaced; Maps.big: 302 entries, 6 replaced + 13 added) reproduce the reported in-place SIZES to the byte
//    (29,059,250 and 86,684,399). That is all it shows: the disc archives' entry sets and bytes are not established, so "the same files" is
//    inference (stop S-4700).
// A layout is chosen from the archives on disk by presence and size of the archives that differ between layouts; a folder that matches
// no layout (half patched, or both layouts' archives at once) is refused with the player-facing error ChooseRetailLayout writes.
//
// Entry-checked archives (`entryChecked` in the policy JSON): Data1.big holds fonts, data\scripts\* and the system Create-a-Heroes,
// and its bytes differ between the AIO package and the discs (the AIO copy carries each font twice). It is verified by its entries:
// every entry's name, size and md5 must be one pinned version of that name, every pinned name must be there, and a name stored twice
// must be stored twice with the same bytes. The policy's archive-level size / md5 of such an archive is the AIO file's (kept for the
// launcher's download pins); the mount reports the file's own md5 and size.
//
// Verification (fail closed; nothing is mounted if any check fails):
//  * every policy archive must exist (case-insensitive) with the pinned size
//  * every archive's md5 must equal the pinned md5. There is no opt-out. A persistent cache
//    (RetailMountOptions::md5CachePath, in the user data dir) keyed by absolute path + size +
//    last-write time makes repeat mounts fast; any key change rehashes the file, and a cached
//    value that disagrees with the policy is rehashed before it can refuse or admit anything.
//    Threat model: this detects wrong or modified files, not deliberate tampering that
//    restores timestamps; cross-play integrity comes from the retail INI/logic CRCs
//  * every archive is opened and its directory validated (entry ranges inside the file) before
//    the first one is mounted, so a failure leaves the file system empty
//  * every *.big found under an install must be in the policy or in that install's explicit
//    exclusion list (known 2.02/HD archives); anything else is an error

#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

class Win32BIGFileSystem;

struct RetailArchiveEntry
{
	std::string path; // canonical, forward slashes, retail case (e.g. "apt/MainMenu.big")
	std::string md5;
	std::uint64_t size = 0;
};

// lane CONTRIB-1: one allowed version of one entry of an entry-checked archive
struct RetailArchiveContentEntry
{
	std::string name; // lower case, backslashes (e.g. "data\\scripts\\scripts.lua")
	std::string md5;
	std::uint64_t size = 0;
};

struct RetailEntryCheckedArchive
{
	std::string path;                                // canonical, as in `archives`
	std::vector<RetailArchiveContentEntry> versions; // a name may have several allowed versions
	std::string advice;                              // player words: what to do when an entry is unknown
};

// lane CONTRIB-1: another known way the same game version sits on disk (see "Layouts" above)
struct RetailArchiveLayout
{
	std::string id;
	std::string name;                         // player words: what this layout is
	std::vector<RetailArchiveEntry> replace;  // archives whose file differs in this layout (path, size, md5)
	std::vector<std::string> absent;          // policy archives this layout does not have
};

struct RetailArchivePolicy
{
	std::string game;
	std::string patch;
	std::vector<RetailArchiveEntry> archives;
	std::vector<std::string> excluded; // canonical paths never to mount
	std::string baseLayoutId = "base";  // the layout `archives` describes
	std::string baseLayoutName;         // player words ("" when the policy has a single layout)
	std::vector<RetailArchiveLayout> layouts; // the other known layouts
	std::string advice;                       // player words: what to do when a folder matches no layout
	std::vector<RetailEntryCheckedArchive> entryChecked;

	// the archive list of a layout (nullptr = the base layout)
	std::vector<RetailArchiveEntry> archivesOf(const RetailArchiveLayout *layout) const;
	// the entry-checked description of `path`, or nullptr
	const RetailEntryCheckedArchive *entryCheckOf(const std::string &path) const;

	// Parse a policy JSON (openbfme.retail-archive-policy v1) and, optionally, an exclusion
	// JSON (openbfme.retail-archive-exclusions v1; pass empty text for none).
	static bool parse(const std::string &policyJson, const std::string &exclusionJson, RetailArchivePolicy &out, std::string *error);

	// Built-in policies (embedded copies of engine/data/retail-archives/*.json).
	static bool loadBuiltin(const std::string &id, RetailArchivePolicy &out, std::string *error); // "rotwk-201", "bfme2-106"
};

// lane CONTRIB-1: which layout a folder holds, from the presence and size of the archives that differ between layouts
struct RetailLayoutChoice
{
	bool ok = false;
	const RetailArchiveLayout *layout = nullptr; // nullptr = the base layout (when ok)
	std::string layoutId, layoutName;
	// the archives to verify: the chosen layout's, or (not ok) the archives every layout shares, so their own problems still show
	std::vector<RetailArchiveEntry> archives;
	std::string error; // player words when not ok: what the folder has, which file differs, what to do
};

// `onDiskSizes`: every *.big of the folder by lower-case relative path with '/' (the mount's key) -> size. `label` starts the error
// ("rotwk"). An entry-checked archive counts by presence only (its size is not pinned).
RetailLayoutChoice ChooseRetailLayout(const RetailArchivePolicy &policy, const std::string &label, const std::map<std::string, std::uint64_t> &onDiskSizes);

// lane CONTRIB-1: checks an entry-checked archive's bytes (a whole BIG file) against its pinned entry versions. Returns "" when every entry
// is known, else the player-facing problem naming the entry. `label` and `path` only name the archive in the message.
std::string CheckArchiveEntries(const RetailEntryCheckedArchive &check, const std::string &label, const std::uint8_t *data, std::uint64_t size);

struct RetailInstall
{
	std::string label; // "rotwk", "bfme2"
	std::string root;  // install directory
	RetailArchivePolicy policy;
};

struct MountedArchive
{
	std::string install;
	std::string canonicalPath;
	std::string diskPath;
	std::uint64_t size = 0; // md5 verified against the policy for every mounted archive
	std::string md5;        // the verified md5 (the policy's): the archive's content identity (lane MP-1: GameNetwork/ProfileIdentity.h)
};

struct RetailMountOptions
{
	// Where the md5 cache lives (the caller's user data dir, e.g. Godot user://). Empty = no
	// cache, every archive is hashed on every mount. md5 verification itself cannot be turned off.
	std::string md5CachePath;
};

struct RetailMountReport
{
	bool ok = false;
	std::vector<std::string> errors;
	std::vector<MountedArchive> mounted;   // in load order (earlier wins); empty unless ok
	std::vector<std::string> excludedFound; // "<install>:<path>" skipped on purpose
	std::vector<std::string> layouts;       // lane CONTRIB-1: "<install>:<layout id>" of every install whose policy knows several layouts
	std::vector<std::string> warnings;      // non-fatal (e.g. the md5 cache could not be written)
	int archivesHashed = 0;                 // md5 computed from the file during this mount
	int md5CacheHits = 0;                   // md5 taken from the cache (path+size+mtime unchanged)
};

// installs are in precedence order (RotWK first). On any error nothing is mounted.
RetailMountReport MountRetailArchives(Win32BIGFileSystem &fileSystem, const std::vector<RetailInstall> &installs,
	const RetailMountOptions &options);

// The within-install order used above: BFMEArchiveLoadOrder over canonical paths with
// '/' turned into '\'. Exposed for tests.
std::vector<RetailArchiveEntry> RetailArchiveLoadOrder(const std::vector<RetailArchiveEntry> &archives);

// Install directories come from the environment (ROTWK_INSTALL, BFME2_INSTALL) or, when a
// variable is unset, from a KEY=VALUE config file. Returns false + *error if neither has it.
bool ResolveInstallPath(const std::string &key, const std::string &configPath, std::string &out, std::string *error);
