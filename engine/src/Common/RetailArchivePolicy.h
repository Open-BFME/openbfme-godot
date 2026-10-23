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
//  2. within one install, RotWK's startup order (lane MODS-2, read from the binary: RetailArchiveLoadOrder): lang\English*.big (another
//     language's install: its lang\<Language>*.big in their place, issue #49; the
//     game folder's with overwrite, so lang\englishpatch201.big wins over lang\English.big), then the folder's own *.big in case-insensitive
//     name order (_patch201*.big ahead of INI.big / W3D.big / Maps.big: '_' sorts before letters), then apt\*.big; first mounted wins.
//     Canonical names come from the policy, not the disk.
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
//
// Languages: the policies describe English installs. An archive whose `language` is not "ALL" is a language
// archive (lang/English.big, lang/EnglishAudio.big, lang/englishpatch201.big). LocalizeRetailPolicy looks at the
// install's lang/*.big: when the English ones are there (or no language is found) the policy is used unchanged.
// Otherwise the install's text language is the stem of its single lang/<language>.big (no "audio"/"patch" in the name)
// and its voice language the stem of its single lang/<language>audio.big (issue #49: the launcher can download one
// language's text and another's speech). Each language archive is swapped for lang/<Language><suffix>.big (the audio
// archive with the voice language, the others with the text language), written in the English entry's case style so the
// load order is the English one; an English text or voice keeps the policy's pinned English archive. The swapped archive
// is pinned by engine/data/retail-archives/language-archives.json (generated by tools/release/aio_languages.py from the
// All In One BFME Launcher service's manifests: Dutch, French, German, Italian, Norwegian, Polish, Russian, Spanish,
// Swedish, Turkish) when its language is known there, else it is unpinned: it must exist and open as a valid BIG, its md5
// is computed and reported, and the mount warns (PR #77, owner decision "load with warning": the game shows the player a warning
// once per install, RetailLanguageWarningText below; an archive that is missing or not a valid BIG is still an error). A pin whose size the service does not know (size 0 there) checks the md5
// only. OPENBFME_STRICT_LANGUAGE=1 (an opt-in) refuses unpinned language archives instead. An audio archive several languages share
// (EnglishAudio.big: "EN NL NO PL SV TR") stays the pinned English one when the install has no <language>audio.big of its
// own. The language-independent archives are verified by md5 exactly as for English. None of the language archives feed
// the simulation (the BACKLOG2 determinism vector hashes the same with German archives as with none mounted).

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
	std::string language; // the policy's "language" ("ALL", "EN", "EN NL NO PL SV TR"); "" when the JSON has none
	bool pinned = true;   // false: a language archive of a language with no known md5 (LocalizeRetailPolicy): presence + a valid BIG only
	bool sizeKnown = true; // false: a language archive pinned by md5 only (its size is not known)
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

// the install language and the archives LocalizeRetailPolicy chose
struct RetailLanguageChoice
{
	std::string language;              // the text language, retail-style ("English", "German"); "" when the folder has no lang/<language>.big
	std::string voice;                 // the voice language: the stem of the single lang/<language>audio.big ("" when none or several)
	bool english = true;               // true: the policy is used as it is
	std::vector<std::string> unpinned; // canonical paths of language archives with no known md5 (the mount warns, or refuses in strict mode)
};

// PR #77 (owner decision "load with warning"): a mounted language archive of a language no pin covers (language-archives.json)
struct UnverifiedLanguageArchive
{
	std::string install;  // "rotwk" / "bfme2"
	std::string path;     // canonical, "lang/Klingon.big" (the language part is the file name on disk)
	std::string language; // the file name's stem, "Klingon"
	std::string md5;      // computed during the mount
	std::uint64_t size = 0;
};

// The language of a folder's lang/*.big (`onDiskSizes` as for ChooseRetailLayout; only the keys are read). "" when none or ambiguous.
std::string DetectRetailLanguage(const std::map<std::string, std::uint64_t> &onDiskSizes);
// The same from an install directory on disk (lang/ listed case-insensitively).
std::string DetectRetailLanguageOfInstall(const std::string &installRoot);
// The voice language: the stem of the folder's single lang/<language>audio.big (English wins over others), "" when none or ambiguous.
std::string DetectRetailVoiceLanguage(const std::map<std::string, std::uint64_t> &onDiskSizes);
std::string DetectRetailVoiceLanguageOfInstall(const std::string &installRoot);
// The policy for the language the folder holds (see "Languages" above). With an English or language-less folder it is `policy` unchanged.
RetailArchivePolicy LocalizeRetailPolicy(const RetailArchivePolicy &policy, const std::map<std::string, std::uint64_t> &onDiskSizes,
	RetailLanguageChoice *choice = nullptr);
// OPENBFME_STRICT_LANGUAGE is set to a value other than "" / "0"
bool RetailStrictLanguage();

// PR #77, the warning a player sees when the mount took language archives no pin covers (Release.mount_retail shows it once per install, with
// "Copy details" and "Don't show this again"; GodotDevice/GodotRetailFileSystem.cpp passes these texts on). Every text that comes from the disk (the
// language and file names) is copied in as it is, with control characters replaced (RetailPrintableText), and never used as a format string.
// The player-facing message: what was recognised, what an unchecked language can mean, what is true for online play, how to help.
std::string RetailLanguageWarningText(const std::vector<UnverifiedLanguageArchive> &archives);
// The details "Copy details" puts on the clipboard (and the log): the version line, the languages, each file's install, name, size and md5;
// no folder path (the details are meant to be posted).
std::string RetailLanguageWarningDetails(const std::vector<UnverifiedLanguageArchive> &archives, const std::string &versionLine);
// The md5 of the archives' install, name, size and md5 lines: "Don't show this again" holds for these files; other files show the warning again.
std::string RetailLanguageWarningSignature(const std::vector<UnverifiedLanguageArchive> &archives);
// `text` with every control character (below 0x20, 0x7F) replaced by '?': a file name cannot add lines to a message or the log
std::string RetailPrintableText(const std::string &text);

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
	std::vector<std::string> warnings;      // non-fatal (e.g. the md5 cache could not be written, a language archive with no known md5)
	std::vector<std::string> languages;     // "<install>:<language>" of every install with a language archive ("rotwk:English"; another voice:
	                                        // "rotwk:German (voice English)")
	std::vector<std::string> stops;         // lane MODS-2: the stop lines this mount reports (S-5302 when a base install's apt\ archives are mounted)
	std::vector<UnverifiedLanguageArchive> unverifiedLanguage; // PR #77: the mounted language archives no pin covers (filled only when ok)
	int archivesHashed = 0;                 // md5 computed from the file during this mount
	int md5CacheHits = 0;                   // md5 taken from the cache (path+size+mtime unchanged)
};

// installs are in precedence order (RotWK first). On any error nothing is mounted.
RetailMountReport MountRetailArchives(Win32BIGFileSystem &fileSystem, const std::vector<RetailInstall> &installs,
	const RetailMountOptions &options);

// The within-install order used above, as a first-wins sequence (lane MODS-2: RotWK's startup mount RW 0xA1766D / 0xA14419, see the
// definition): `gameInstall` = the game's own folder (RotWK: its lang\ archives are loaded with overwrite, the last one winning, and its apt\ folder
// is mounted) rather than the base install it reads (BFME2). Exposed for tests.
std::vector<RetailArchiveEntry> RetailArchiveLoadOrder(const std::vector<RetailArchiveEntry> &archives, bool gameInstall = true);
// docs/STOPS.md S-5302: the base install's apt\ archives, which retail RotWK never mounts, are mounted last
std::string RetailBaseAptStopLine();

// Install directories come from the environment (ROTWK_INSTALL, BFME2_INSTALL) or, when a
// variable is unset, from a KEY=VALUE config file. Returns false + *error if neither has it.
bool ResolveInstallPath(const std::string &key, const std::string &configPath, std::string &out, std::string *error);
