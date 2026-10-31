// OpenBFME. GPL-3.0.
//
// Device layer: exposes the retail archive file system to Godot. Godot never reads retail
// files itself; everything goes through the SAGE-port ArchiveFileSystem.

#pragma once

#include "Common/LooseFileScan.h"
#include "Common/ModMount.h"
#include "GameClient/AssetPacks.h"
#include "Common/RetailArchivePolicy.h"

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/string.hpp>

#include <memory>
#include <string>
#include <vector>

class Win32BIGFileSystem;
class ArchiveFileSystem;

namespace godot
{

class RetailFileSystem : public RefCounted
{
	GDCLASS(RetailFileSystem, RefCounted)

public:
	RetailFileSystem();
	~RetailFileSystem() override;

	// Mount pure RotWK 2.01 (RotWK 2.01 + BFME2 1.06 archives). Install paths come from the
	// ROTWK_INSTALL / BFME2_INSTALL environment variables, else user://install-paths.cfg
	// (KEY=VALUE lines). Every archive's size and md5 are verified against the pinned policy
	// (hashes cached in user://retail-md5-cache.tsv by path+size+mtime). Returns a report; on any
	// error nothing is mounted.
	Dictionary mount_retail();
	// Standalone BFME1 installation, using its own archives for both menus and gameplay.
	Dictionary mount_bfme1_path(const String &root);
	bool is_bfme1() const { return m_bfme1; }
	const std::string &bfme1_root() const { return m_bfme1Root; }
	// lane RELEASE-1: the same mount from two folders the player picked on the first-run screen (nothing is read from the
	// environment or the config file; remembering the pick is InstallSetup.remember's job once this succeeded).
	Dictionary mount_retail_paths(const String &rotwk, const String &bfme2);
	// Build a presentation-only source from the verified BFME2 install of this
	// normal RotWK mount. The returned source never becomes a GameWorld source.
	Ref<RetailFileSystem> create_bfme2_shell();
	// lane MODS-1: the `-mod` operands of the next mount (RotWK's -mod, Common/ModMount.h), in command-line order; a relative path is taken
	// below the user data folder named by the RotWK install's gi.dat. Empty = no mod. The next mount_retail / mount_retail_paths mounts them
	// over the verified retail archives and fails as a whole when one cannot be mounted.
	void set_mods(const PackedStringArray &paths);
	// lane MODS-2: an install-folder mod (Common/ModMount.h MountInstallOverlay: e.g. Edain as the All In One launcher installs it), laid over the
	// RotWK install for the next mount; "" = none. Mounted before the -mod operands.
	void set_install_mod(const String &folder);
	// several folders, highest priority first (an All In One package with its enhancements and patches)
	void set_install_mods(const PackedStringArray &folders);
	// lane MODS-3: the MD5s of the mod's modified game binaries (Common/ModEngineFeatures.h); the next mount turns their features on and fails
	// when one is unknown. Empty = retail engine.
	void set_mod_engines(const PackedStringArray &md5s);
	PackedStringArray get_mods() const { return m_modArgs; }
	// GDScript view of the mounted mod: {mounted, archives: [{path, size}], loose_files, identity: [...], stops: [...]}
	Dictionary get_mod_report() const;
	// the source list of one virtual path, the winner first (ArchiveFileSystem::sourcesOf; "mod-folder:<path>" for a loose mod file)
	PackedStringArray get_file_sources(const String &path) const;
	// lane SDK-0 (GameClient/AssetPacks.h): the cosmetic asset packs, in order (the last wins a name). Replaces the packs of this file system;
	// {ok, errors, packs, textures, models, notes, not_covered}. Takes effect for asset managers and material factories made afterwards.
	Dictionary set_asset_packs(const PackedStringArray &folders);
	// lane SDK-1b (b): per pack, the models left out (they do not fit), each with its reason (the launcher's card)
	Dictionary get_asset_pack_report() const;

	bool is_mounted() const { return m_mounted; }
	bool is_shell_source() const { return m_shellSource; }
	String install_root() const;
	bool file_exists(const String &path) const;
	PackedByteArray read_file(const String &path);
	String get_archive_for_file(const String &path) const;
	String get_last_error() const { return m_lastError; }

	// Engine-side access for other device classes.
	ArchiveFileSystem *archive_fs() const;
	bool readBytes(const std::string &path, std::vector<uint8_t> &out, std::string *error);
	bool existsNative(const std::string &path) const;
	// The mounted archive file system (nullptr until mount_retail() succeeded); engine-side only.
	ArchiveFileSystem *archive();
	// Loose .map/.scb files found in the mounted installs at mount time (PLAN rule 7 contamination,
	// stop S-039). Never read; every map build reports them.
	const std::vector<LooseFileFinding> &looseFindings() const { return m_loose; }
	// lane MP-1: the mounted archives in load order with their verified md5 (the profile identity of a network game or replay)
	const std::vector<MountedArchive> &mountedArchives() const { return m_mountedArchives; }
	// GDScript view of the same list: [{install, path, size, line}].
	Array get_loose_files() const;
	// lane MOD-5: the user maps folder of the RotWK mount (UserDataFolder::userMapFolder: the user data folder that the install's gi.dat names, in the
	// device's application data folder, + "Maps"), which the archive file system reads from disk (ArchiveFileSystem::setLocalFolder). "" when the
	// mount has none (not mounted, BFME1, or gi.dat unreadable: the mount report's user_map_error says why).
	String get_user_map_folder() const;
	// Engine side: the folder a map name's maps are in (MapClassification::mapDirectory): "maps", or with userMap the user maps folder. False +
	// *error when there is no user maps folder.
	bool mapsFolder(bool userMap, std::string &out, std::string *error) const;
	// lane MODS-1: the mounted mod's ProfileIdentity lines (empty without a mod) and its folder (the movie player's "Mod Path", RW 0x490D1C)
	std::vector<std::string> modIdentity() const; // the install-folder mod's lines, then the -mod's
	// lane PROFILE-2: SimulationData::digest of this mount (the profile identity's simdata line, GameNetwork/ProfileIdentity.h), computed once per
	// mounted archive list and mod; "" + *error when nothing is mounted or a listed file cannot be read
	std::string simulationData(std::string *error) const;
	// lane PROFILE-3: the profile identity's pack lines (AssetPacks::identityLines) of the packs this game uses: none while they are suspended or none
	// is loaded. Read once per set of packs (every used file is hashed); a file that cannot be read gives the one line "pack-error" (the reason goes
	// to the log): presentation only, it never refuses a game, but a checker sees the packs were in use
	std::vector<std::string> packIdentity() const;
	const std::string &modFolder() const { return m_modReport.loose.root; }
	// lane MODCOMPAT-1: the install-folder mods' folders (--mod-install), highest priority first: their loose files (movies) sit in the game folder in retail
	std::vector<std::string> installOverlayFolders() const;
	// lane SDK-0: the packs (empty without any) and the W3D source of the RENDER side: the mounted files with the packs in front. Never for
	// the simulation's asset manager (LiveGame's m_simAssetSource reads the archive file system directly).
	const AssetPacks &assetPacks() const { return m_packs; }
	AssetPacks &assetPacksMutable() { return m_packs; } // lane SDK-1b: GameWorld::set_asset_packs_used
	std::unique_ptr<W3DFileSource> renderW3DSource();

protected:
	static void _bind_methods();

private:
	Dictionary mountFrom(const std::string &rotwkRoot, const std::string &bfme2Root, PackedStringArray errors);
	Dictionary mountBfme2Shell(const std::string &bfme2Root);
	void mountMods(const std::string &rotwkRoot, PackedStringArray &errors); // lane MODS-1

	std::unique_ptr<Win32BIGFileSystem> m_fs;
	bool m_mounted = false;
	bool m_shellSource = false;
	std::string m_installRoot;
	bool m_bfme1 = false;
	std::string m_bfme1Root;
	String m_lastError;
	std::vector<LooseFileFinding> m_loose;
	std::vector<MountedArchive> m_mountedArchives;
	std::string m_userMapFolder; // lane MOD-5
	std::string m_userMapError;
	PackedStringArray m_modArgs;    // lane MODS-1
	ModMountReport m_modReport;     // the mounted mod (ok == false without one)
	PackedStringArray m_installModArgs; // lane MODS-2: highest priority first
	PackedStringArray m_modEngines;     // lane MODS-3
	ModMountReport m_installModReport;
	AssetPacks m_packs;             // lane SDK-0
	mutable std::string m_simDataKey, m_simData; // lane PROFILE-2: simulationData's cache (the archive lines and mod lines it was computed for)
	mutable bool m_packLinesRead = false;         // lane PROFILE-3: packIdentity's cache (cleared when the packs or their checked models change)
	mutable std::vector<std::string> m_packLines;
};

} // namespace godot
