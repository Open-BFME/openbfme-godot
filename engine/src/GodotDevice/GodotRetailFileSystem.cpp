// OpenBFME. GPL-3.0.

#include "GodotDevice/GodotRetailFileSystem.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"
#include "GameEngineDevice/Win32Device/Common/FolderArchiveFile.h"

#include "Common/RetailArchivePolicy.h"
#include "Common/ArchiveFile.h"
#include "Common/MD5.h"
#include <algorithm>
#include <filesystem>
#include <chrono>
#include <godot_cpp/classes/config_file.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/dir_access.hpp>
#include "GameClient/OptionPreferences.h"
#include <godot_cpp/classes/os.hpp>
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"

#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

namespace godot
{

namespace
{
String toGodot(const std::string &s)
{
	return String::utf8(s.c_str(), (int64_t)s.size());
}

std::string toNative(const String &s)
{
	CharString utf8 = s.utf8();
	return std::string(utf8.get_data(), (size_t)utf8.length());
}
}

RetailFileSystem::RetailFileSystem() = default;
RetailFileSystem::~RetailFileSystem() = default;

String RetailFileSystem::content_signature() const
{
 String identity = m_bfme1 ? "bfme1" : "rotwk";
 for (const auto &archive : m_mountedArchives) identity += String::utf8((archive.canonicalPath + ":" + archive.md5 + "\n").c_str());
 return identity.sha256_text();
}

void RetailFileSystem::_bind_methods()
{
 ClassDB::bind_method(D_METHOD("content_signature"), &RetailFileSystem::content_signature);
	ClassDB::bind_method(D_METHOD("mount_bfme1_path", "root"), &RetailFileSystem::mount_bfme1_path);
	ClassDB::bind_method(D_METHOD("is_bfme1"), &RetailFileSystem::is_bfme1);
	ClassDB::bind_method(D_METHOD("mount_retail"), &RetailFileSystem::mount_retail);
	ClassDB::bind_method(D_METHOD("mount_retail_paths", "rotwk", "bfme2"), &RetailFileSystem::mount_retail_paths); // RELEASE-1
	ClassDB::bind_method(D_METHOD("is_mounted"), &RetailFileSystem::is_mounted);
	ClassDB::bind_method(D_METHOD("file_exists", "path"), &RetailFileSystem::file_exists);
	ClassDB::bind_method(D_METHOD("read_file", "path"), &RetailFileSystem::read_file);
	ClassDB::bind_method(D_METHOD("get_archive_for_file", "path"), &RetailFileSystem::get_archive_for_file);
	ClassDB::bind_method(D_METHOD("get_last_error"), &RetailFileSystem::get_last_error);
	ClassDB::bind_method(D_METHOD("get_loose_files"), &RetailFileSystem::get_loose_files);
}

Dictionary RetailFileSystem::mount_retail()
{
	PackedStringArray errors;
	std::string config = toNative(ProjectSettings::get_singleton()->globalize_path("user://install-paths.cfg"));
	std::string rotwkRoot;
	std::string bfme2Root;
	std::string error;
	if (!ResolveInstallPath("ROTWK_INSTALL", config, rotwkRoot, &error))
	{
		errors.push_back(toGodot(error));
	}
	if (!ResolveInstallPath("BFME2_INSTALL", config, bfme2Root, &error))
	{
		errors.push_back(toGodot(error));
	}
	return mountFrom(rotwkRoot, bfme2Root, errors);
}

Dictionary RetailFileSystem::mount_retail_paths(const String &rotwk, const String &bfme2)
{
	return mountFrom(toNative(rotwk), toNative(bfme2), PackedStringArray());
}

Dictionary RetailFileSystem::mount_bfme1_path(const String &root)
{
	m_fs.reset();
	m_menuFs.reset();
	m_mounted = false;
	m_bfme1 = false;
	m_bfme1Root.clear();
	m_mountedArchives.clear();
	m_loose.clear();
	PackedStringArray errors;
	auto fs = std::make_unique<Win32BIGFileSystem>();
	const auto directory = std::filesystem::u8path(toNative(root));
	std::error_code ec;
	std::vector<std::string> names;
	for (std::filesystem::recursive_directory_iterator it(directory, ec), end; !ec && it != end; it.increment(ec))
	{
		if (!it->is_regular_file()) continue;
		auto extension = it->path().extension().u8string();
		std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return std::tolower(c); });
		if (extension != ".big") continue;
		const auto filename = it->path().filename().u8string();
		// Launcher-generated platform graphics overrides are not part of game rules.
		if (filename.rfind("!!!", 0) == 0) continue;
		auto relative = it->path().lexically_relative(directory).u8string();
		std::replace(relative.begin(), relative.end(), '/', '\\');
		names.push_back(relative);
	}
	if (ec) errors.push_back(toGodot("Cannot read BFME1 folder: " + ec.message()));
	if (names.empty()) errors.push_back("Choose a BFME1 installation containing BIG archives");
	// BFME1 Win32BIGFileSystem_loadBigFiles.cpp (0x009CDB90): descending
	// strcmp order, first mounted wins. Do not mix in BFME2/RotWK archives.
	std::vector<MountedArchive> mounted;
	Ref<ConfigFile> cache;
	cache.instantiate();
	cache->load("user://bfme1-archive-cache.cfg");
	int hashed = 0, cached = 0;
	for (const auto &name : BFMEArchiveLoadOrder(names))
	{
		if (!errors.is_empty()) break;
		auto relative = name;
		std::replace(relative.begin(), relative.end(), '\\', '/');
		const auto path = (directory / std::filesystem::u8path(relative)).u8string();
		std::string error;
		auto archive = fs->openArchiveFile(path, &error);
		if (!archive) { errors.push_back(toGodot(error)); break; }
		MountedArchive entry;
		entry.install = "bfme1";
		entry.canonicalPath = relative;
		entry.diskPath = path;
		entry.size = std::filesystem::file_size(std::filesystem::u8path(path), ec);
		const auto stamp = std::filesystem::last_write_time(std::filesystem::u8path(path), ec).time_since_epoch().count();
		const String key = toGodot(MD5::ofBytes(path.data(), path.size()));
		if (!ec && (int64_t)cache->get_value(key, "size", (int64_t)-1) == (int64_t)entry.size &&
			(int64_t)cache->get_value(key, "mtime", (int64_t)0) == stamp)
		{
			entry.md5 = toNative(cache->get_value(key, "md5", String()));
			++cached;
		}
		if (entry.md5.empty())
		{
			entry.md5 = MD5::ofFile(path, &error);
			++hashed;
			cache->set_value(key, "size", (int64_t)entry.size);
			cache->set_value(key, "mtime", (int64_t)stamp);
			cache->set_value(key, "md5", toGodot(entry.md5));
		}
		if (ec || entry.md5.empty()) { errors.push_back(toGodot("Cannot fingerprint " + path + ": " + error)); break; }
		mounted.push_back(entry);
		fs->mountArchive(std::move(archive), path, false);
	}
	for (const char *required : { "data/ini/gamedata.ini", "data/ini/playertemplate.ini", "maps/mapcache.ini", "MainMenu.apt" })
	{
		if (!fs->doesFileExist(required)) errors.push_back(toGodot(std::string("BFME1 installation is missing ") + required));
	}
	// BFME1 stores these files below lang/<language>; its file-system search
	// exposes them at the root. Build a transient view of the same bytes so
	// all existing UI readers see the selected language consistently.
	if (errors.is_empty())
	{
		std::vector<std::pair<std::string, std::vector<std::uint8_t>>> languageFiles;
		for (const char *name : {"headertemplate.ini", "language.ini"})
		{
			if (fs->doesFileExist(name)) continue;
			std::vector<std::uint8_t> bytes;
			std::string error;
			if (!fs->readFile(std::string("lang/english/") + name, bytes, &error)) { errors.push_back(toGodot(error)); break; }
			languageFiles.emplace_back(name, std::move(bytes));
		}
		if (!languageFiles.empty() && errors.is_empty())
		{
			auto bytes = std::make_shared<std::vector<std::uint8_t>>();
			auto be32 = [&](std::uint32_t value) { for (int i = 3; i >= 0; --i) bytes->push_back((std::uint8_t)(value >> (i * 8))); };
			std::uint32_t header = 16, total = 16;
			for (const auto &file : languageFiles) header += 9 + file.first.size();
			total = header;
			for (const auto &file : languageFiles) total += file.second.size();
			bytes->insert(bytes->end(), {'B', 'I', 'G', 'F'});
			// BIG's length word is little-endian; directory words are big-endian.
			for (int i = 0; i < 4; ++i) bytes->push_back((std::uint8_t)(total >> (i * 8)));
			be32(languageFiles.size()); be32(header);
			std::uint32_t offset = header;
			for (const auto &file : languageFiles)
			{
				be32(offset); be32(file.second.size());
				bytes->insert(bytes->end(), file.first.begin(), file.first.end()); bytes->push_back(0);
				offset += file.second.size();
			}
			for (const auto &file : languageFiles) bytes->insert(bytes->end(), file.second.begin(), file.second.end());
			std::string error;
			auto view = fs->openArchiveFromMemory("bfme1-language-view.big", bytes, &error);
			if (!view) errors.push_back(toGodot(error));
			else fs->mountArchive(std::move(view), "bfme1-language-view.big", false);
		}
	}
	if (errors.is_empty())
	{
		m_fs = std::move(fs);
		m_mountedArchives = std::move(mounted);
		m_mounted = true;
		m_bfme1 = true;
		m_bfme1Root = toNative(root);
		W3D_Set_Flat_Asset_Layout(true); // BFME1's W3D.big has no two-letter folders
	}
	m_lastError = errors.is_empty() ? String() : String(errors[0]);
	Dictionary result;
	result["ok"] = m_mounted;
	result["errors"] = errors;
	result["game"] = "bfme1";
	result["archives_hashed"] = hashed;
	result["md5_cache_hits"] = cached;
	if (m_mounted) cache->save("user://bfme1-archive-cache.cfg");
	result["ui_theme"] = "bfme1";
	result["bfme1_install"] = root;
	return result;
}

Dictionary RetailFileSystem::mountFrom(const std::string &rotwkRoot, const std::string &bfme2Root, PackedStringArray errors)
{
	Dictionary result;
	std::string error;
	W3D_Set_Flat_Asset_Layout(false); // RotWK / BFME2 file models in two-letter folders
	m_fs.reset();
	m_menuFs.reset();
	m_mounted = false;
	m_bfme1 = false;
	m_bfme1Root.clear();
	m_mountedArchives.clear();
	m_loose.clear();

	std::vector<RetailInstall> installs(2);
	installs[0].label = "rotwk";
	installs[0].root = rotwkRoot;
	installs[1].label = "bfme2";
	installs[1].root = bfme2Root;
	if (!RetailArchivePolicy::loadBuiltin("rotwk-201", installs[0].policy, &error))
	{
		errors.push_back(toGodot(error));
	}
	if (!RetailArchivePolicy::loadBuiltin("bfme2-106", installs[1].policy, &error))
	{
		errors.push_back(toGodot(error));
	}

	result["rotwk_install"] = toGodot(rotwkRoot);
	result["bfme2_install"] = toGodot(bfme2Root);

	if (errors.is_empty())
	{
		RetailMountOptions options;
		// The UI preference is read before shell startup; gameplay keeps canonical RotWK precedence.
		OptionPreferences preferences;
		String optionsPath = "user://Options.ini";
		const PackedStringArray args = OS::get_singleton()->get_cmdline_user_args();
		for (int64_t i = 0; i < args.size(); ++i)
			if (args[i].begins_with("--options-file=")) optionsPath = args[i].substr(15);
		preferences.load(toNative(ProjectSettings::get_singleton()->globalize_path(optionsPath)));
		const bool bfme2UI = preferences.get("OpenBFMEBFME2UI") != "no";
		result["ui_theme"] = bfme2UI ? "bfme2" : "rotwk";
		// md5 of every archive is always verified; the cache only makes repeat mounts fast.
		options.md5CachePath = toNative(ProjectSettings::get_singleton()->globalize_path("user://retail-md5-cache.tsv"));
		std::unique_ptr<Win32BIGFileSystem> fs = std::make_unique<Win32BIGFileSystem>();
		const auto mountStart = std::chrono::steady_clock::now();
		auto elapsed = [&]() { return (int64_t)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - mountStart).count(); };
		RetailMountReport report = MountRetailArchives(*fs, installs, options);
		UtilityFunctions::print("STARTUP retail verification/index: ", elapsed(), " ms; hashed=", (int64_t)report.archivesHashed, "; cached=", (int64_t)report.md5CacheHits);
		const auto menuStart = elapsed();
		Ref<ConfigFile> wallOptions; wallOptions.instantiate();
		wallOptions->load("res://dev/wall_balance.cfg");
		Ref<ConfigFile> savedWalls; savedWalls.instantiate();
		if (savedWalls->load("user://wall_balance.cfg") == OK) wallOptions = savedWalls;
		const bool wantsTowers = (bool)wallOptions->get_value("walls","new_towers",false);
		const bool hasTowerPack = FileAccess::file_exists("res://mods/wall_towers.big");
		if (report.ok && wantsTowers && !hasTowerPack)
		{
			report.warnings.push_back("Optional wall tower assets are not included; using native wall towers.");
		}
		// lane MODS-1: the enabled mods (user://mods.cfg [mods] enabled, in load order; RotWK's -mod "<folder>" as --mod=<folder> too)
		PackedStringArray modPaths;
		{
			Ref<ConfigFile> modsCfg;
			modsCfg.instantiate();
			if (modsCfg->load("user://mods.cfg") == OK)
			{
				const Array enabled = modsCfg->get_value("mods", "enabled", Array());
				for (int64_t i = 0; i < enabled.size(); ++i)
					modPaths.push_back(String(enabled[i]));
			}
			const PackedStringArray modArgs = OS::get_singleton()->get_cmdline_user_args();
			for (int64_t i = 0; i < modArgs.size(); ++i)
				if (modArgs[i].begins_with("--mod="))
					modPaths.push_back(modArgs[i].substr(6));
		}
		// the BFME2Lua wall tower pack is written against the stock game data (its INI uses the stock gamedata.ini macros): not with a mod
		if (report.ok && wantsTowers && hasTowerPack && !modPaths.is_empty())
		{
			report.warnings.push_back("Wall tower pack not loaded: a mod is enabled (the pack is written for the unmodded game data).");
		}
		if (report.ok && wantsTowers && hasTowerPack && modPaths.is_empty())
		{
			const String resource = "res://mods/wall_towers.big";
			String path = ProjectSettings::get_singleton()->globalize_path(resource);
			std::error_code nativePathError;
			if (!std::filesystem::is_regular_file(toNative(path), nativePathError))
			{
				path = ProjectSettings::get_singleton()->globalize_path("user://mods/wall_towers.big");
				DirAccess::make_dir_recursive_absolute(path.get_base_dir());
				const auto bytes = FileAccess::get_file_as_bytes(resource);
				Ref<FileAccess> output = FileAccess::open(path,FileAccess::WRITE);
				if (output.is_valid() && !bytes.is_empty()) {
					output->store_buffer(bytes);
					output->close(); // the native BIG reader must see a fully written physical file
				} else {
					report.ok = false;
					report.errors.push_back("BFME2Lua wall towers: could not extract packaged tower assets to " + toNative(path));
				}
			}
			std::string problem;
			auto archive = fs->openArchiveFile(toNative(path), &problem);
			if (archive) fs->mountArchive(std::move(archive),toNative(path),true);
			else { report.ok = false; report.errors.push_back("BFME2Lua wall towers: " + problem); }
		}
		std::unique_ptr<Win32BIGFileSystem> menuFs;
		if (report.ok && bfme2UI)
		{
			menuFs = std::make_unique<Win32BIGFileSystem>();
			// The complete verified BFME2 view supplies matching movies, atlases, mapped images and .wnd skins.
			// RotWK remains a fallback for assets BFME2 never shipped (for example expansion-only screens).
			// The primary mount has already verified these exact archives this session.
			// Reuse its canonical plan, changing precedence only, rather than scanning
			// both installations and verifying every archive again for the menu.
			for (const auto &install : { "bfme2", "rotwk" })
			{
				for (const auto &mounted : report.mounted)
				{
					if (mounted.install != install) continue;
					std::string problem;
					auto archive = menuFs->openArchiveFile(mounted.diskPath, &problem);
					if (!archive)
					{
						report.ok = false;
						report.errors.push_back("menu theme: " + problem);
						break;
					}
					menuFs->mountArchive(std::move(archive), mounted.diskPath, false);
				}
				if (!report.ok) break;
			}
		}
		UtilityFunctions::print("STARTUP menu archive view: ", elapsed() - menuStart, " ms");
		// Optional local 2.02 multiplayer map pack, generated by tools/import_rotwk_202_maps.py.
		// Mount into both views: the lobby and the world must use the same map cache and bytes.
		const String mapsResource = "res://mods/rotwk_202_maps.big";
		if (report.ok && FileAccess::file_exists(mapsResource))
		{
			String path = ProjectSettings::get_singleton()->globalize_path(mapsResource);
			std::error_code pathError;
			if (!std::filesystem::is_regular_file(toNative(path), pathError))
			{
				path = ProjectSettings::get_singleton()->globalize_path("user://mods/rotwk_202_maps.big");
				DirAccess::make_dir_recursive_absolute(path.get_base_dir());
				const auto bytes = FileAccess::get_file_as_bytes(mapsResource);
				Ref<FileAccess> output = FileAccess::open(path, FileAccess::WRITE);
				if (output.is_valid() && !bytes.is_empty())
				{
					output->store_buffer(bytes);
					output->close();
				}
				else { report.ok = false; report.errors.push_back("2.02 maps: could not extract packaged map archive"); }
			}
			auto mountMaps = [&](Win32BIGFileSystem &target) {
				std::string problem;
				auto archive = target.openArchiveFile(toNative(path), &problem);
				if (archive) target.mountArchive(std::move(archive), toNative(path), true);
				else { report.ok = false; report.errors.push_back("2.02 maps: " + problem); }
			};
			if (report.ok) mountMaps(*fs);
			if (report.ok && menuFs) mountMaps(*menuFs);
		}
		// lane MODS-1: the enabled mods over both views: each mod's *.big, then its loose files on top (FolderArchiveFile). A later mod overrides
		// an earlier one.
		Array modsMounted;
		std::vector<std::string> modObjectInis; // the mods' Data\INI\Object\*.ini (a GameData block there is the game's: MergeGameDataBlocks)
		if (report.ok)
		{
			for (int64_t i = 0; i < modPaths.size() && report.ok; ++i)
			{
				const std::string modPath = toNative(ProjectSettings::get_singleton()->globalize_path(modPaths[i]));
				Dictionary mod;
				mod["path"] = toGodot(modPath);
				std::error_code dirError;
				std::vector<std::string> bigs;
				size_t looseFiles = 0, skipped = 0;
				const bool isFolder = std::filesystem::is_directory(std::filesystem::u8path(modPath), dirError);
				if (!isFolder)
					bigs.push_back(modPath); // a single .big mod
				std::string problem;
				std::unique_ptr<FolderArchiveFile> loose;
				if (isFolder)
				{
					loose = std::make_unique<FolderArchiveFile>();
					if (!loose->open(modPath, &problem))
					{
						report.ok = false;
						report.errors.push_back("mod " + modPath + ": " + problem);
						break;
					}
					bigs.assign(loose->bigFiles().rbegin(), loose->bigFiles().rend()); // ascending: the first in ZH's descending order is mounted last (wins)
					looseFiles = loose->fileCount();
					skipped = loose->skippedPrograms();
					for (const std::string &p : loose->paths())
						if (p.rfind("data\\ini\\object\\", 0) == 0 && p.size() > 4 && p.compare(p.size() - 4, 4, ".ini") == 0)
							modObjectInis.push_back(p);
				}
				size_t emptyBigs = 0;
				for (const std::string &big : bigs)
				{
					std::error_code sizeError;
					if (std::filesystem::file_size(std::filesystem::u8path(big), sizeError) == 0 && !sizeError)
					{
						// an empty .big holds nothing (RJ RotWK ships art\test.big and art\w3d\ar\s.big at 0 bytes): reported, not mounted
						++emptyBigs;
						report.warnings.push_back("mod " + modPath + ": " + big + " is empty (0 bytes), not mounted");
						continue;
					}
					for (Win32BIGFileSystem *target : { fs.get(), menuFs.get() })
					{
						if (!target) continue;
						auto archive = target->openArchiveFile(big, &problem);
						if (!archive)
						{
							report.ok = false;
							report.errors.push_back("mod " + modPath + ": " + problem);
							break;
						}
						target->mountArchive(std::move(archive), big, true);
					}
				}
				if (loose && report.ok)
				{
					fs->mountArchive(std::move(loose), modPath, true);
					if (menuFs)
					{
						// the BFME2-themed menus draw BFME2's movies: a mod's .apt / .const are RotWK's (RJ RotWK ships RotWK 2.01's .const
						// files), and a BFME2 movie with a RotWK constant file does not load. The menu view takes the mod's other files
						auto menuLoose = std::make_unique<FolderArchiveFile>();
						if (menuLoose->open(modPath, &problem, { ".apt", ".const" }))
						{
							if (menuLoose->skippedByExtension() > 0)
								report.warnings.push_back("mod " + modPath + ": its " + std::to_string(menuLoose->skippedByExtension()) +
									" menu movie file(s) (.apt / .const) are not used by the BFME2-style menus; choose the RotWK menus in Options to see them");
							menuFs->mountArchive(std::move(menuLoose), modPath, true);
						}
					}
				}
				mod["big_files"] = (int64_t)(bigs.size() - emptyBigs);
				mod["empty_big_files"] = (int64_t)emptyBigs;
				mod["loose_files"] = (int64_t)looseFiles;
				mod["skipped_programs"] = (int64_t)skipped;
				modsMounted.push_back(mod);
				report.warnings.push_back("mod " + modPath + ": " + std::to_string(bigs.size() - emptyBigs) + " archive(s) and " + std::to_string(looseFiles) +
					" loose file(s) override the verified game files (not verified; " + std::to_string(skipped) + " program file(s) not mounted)");
			}
		}
		// a mod's GameData block in an object INI (RJ RotWK): one merged Data\INI\GameData.ini over both views, so every GameData reader
		// sees the rows retail applies (its later object files override GameData.ini's rows)
		if (report.ok && !modObjectInis.empty())
		{
			std::sort(modObjectInis.begin(), modObjectInis.end());
			modObjectInis.erase(std::unique(modObjectInis.begin(), modObjectInis.end()), modObjectInis.end());
			std::vector<std::pair<std::string, std::string>> blocks;
			for (const std::string &p : modObjectInis)
			{
				std::vector<std::uint8_t> bytes;
				std::string readError;
				if (!fs->readFile(p, bytes, &readError))
					continue;
				std::string text(bytes.begin(), bytes.end());
				if (!GameDataBlockRows(text).empty())
					blocks.emplace_back(p, std::move(text));
			}
			if (!blocks.empty())
			{
				std::vector<std::uint8_t> base;
				std::string readError;
				fs->readFile("data\\ini\\gamedata.ini", base, &readError);
				const std::string merged = MergeGameDataBlocks(std::string(base.begin(), base.end()), blocks);
				std::string sources;
				for (const auto &b : blocks)
					sources += (sources.empty() ? "" : ", ") + b.first;
				for (Win32BIGFileSystem *target : { fs.get(), menuFs.get() })
				{
					if (!target) continue;
					auto mem = std::make_unique<MemoryArchiveFile>("mods: merged GameData");
					mem->put("data\\ini\\gamedata.ini", std::vector<std::uint8_t>(merged.begin(), merged.end()));
					target->mountArchive(std::move(mem), "mods: merged GameData", true);
				}
				report.warnings.push_back("mods: Data\\INI\\GameData.ini merged with the GameData block of " + sources + " (RotWK reads GameData from any INI)");
			}
		}
		result["mods"] = modsMounted;
		for (const std::string &e : report.errors)
		{
			errors.push_back(toGodot(e));
		}
		Array mounted;
		for (const MountedArchive &m : report.mounted)
		{
			Dictionary d;
			d["install"] = toGodot(m.install);
			d["path"] = toGodot(m.canonicalPath);
			d["disk_path"] = toGodot(m.diskPath);
			d["size"] = (int64_t)m.size;
			mounted.push_back(d);
		}
		PackedStringArray excluded;
		for (const std::string &e : report.excludedFound)
		{
			excluded.push_back(toGodot(e));
		}
		PackedStringArray warnings;
		for (const std::string &w : report.warnings)
		{
			warnings.push_back(toGodot(w));
			UtilityFunctions::push_warning("RetailFileSystem: ", toGodot(w));
		}
		result["warnings"] = warnings;
		result["archives_hashed"] = report.archivesHashed;
		result["md5_cache_hits"] = report.md5CacheHits;
		result["mounted"] = mounted;
		result["excluded_found"] = excluded;
		if (report.ok)
		{
			m_fs = std::move(fs);
			m_menuFs = std::move(menuFs);
			m_mounted = true;
			m_mountedArchives = report.mounted; // MP-1
			// PLAN rule 7 / stop S-039: loose map files in the install folders are contamination; they are
			// reported by the mount and by every map build, never read.
			const auto scanStart = elapsed();
			std::string scanError;
			if (!LooseFileScan::scanInstalls({ { "rotwk", rotwkRoot }, { "bfme2", bfme2Root } }, m_loose, &scanError))
			{
				errors.push_back(toGodot("loose file scan: " + scanError));
				m_fs.reset();
				m_menuFs.reset();
				m_mounted = false;
			}
			UtilityFunctions::print("STARTUP loose file scan: ", elapsed() - scanStart, " ms");
			for (const LooseFileFinding &f : m_loose)
			{
				UtilityFunctions::push_warning("RetailFileSystem: ", toGodot(f.line));
			}
		}
	}

	result["loose_files"] = get_loose_files();
	result["ok"] = m_mounted;
	result["errors"] = errors;
	m_lastError = errors.is_empty() ? String() : errors[0];
	for (int64_t i = 0; i < errors.size(); ++i)
	{
		UtilityFunctions::push_error("RetailFileSystem: ", errors[i]);
	}
	return result;
}

ArchiveFileSystem *RetailFileSystem::archive_fs() const
{
	return m_fs.get();
}

ArchiveFileSystem *RetailFileSystem::menu_archive_fs() const
{
	return m_menuFs ? m_menuFs.get() : m_fs.get();
}

Array RetailFileSystem::get_loose_files() const
{
	Array out;
	for (const LooseFileFinding &f : m_loose)
	{
		Dictionary d;
		d["install"] = toGodot(f.install);
		d["path"] = toGodot(f.file.relativePath);
		d["size"] = (int64_t)f.file.size;
		d["line"] = toGodot(f.line);
		out.push_back(d);
	}
	return out;
}

bool RetailFileSystem::existsNative(const std::string &path) const
{
	return m_fs && m_fs->doesFileExist(path);
}

ArchiveFileSystem *RetailFileSystem::archive()
{
	return m_fs.get();
}

bool RetailFileSystem::readBytes(const std::string &path, std::vector<uint8_t> &out, std::string *error)
{
	if (!m_fs)
	{
		if (error)
		{
			*error = "retail archives are not mounted";
		}
		return false;
	}
	return m_fs->readFile(path, out, error);
}

bool RetailFileSystem::file_exists(const String &path) const
{
	return existsNative(toNative(path));
}

String RetailFileSystem::get_archive_for_file(const String &path) const
{
	if (!m_fs)
	{
		return String();
	}
	return toGodot(m_fs->getArchiveFilenameForFile(toNative(path)));
}

PackedByteArray RetailFileSystem::read_file(const String &path)
{
	PackedByteArray bytes;
	std::vector<uint8_t> data;
	std::string error;
	if (!readBytes(toNative(path), data, &error))
	{
		m_lastError = toGodot(error);
		UtilityFunctions::push_error("RetailFileSystem: ", m_lastError);
		return bytes;
	}
	bytes.resize((int64_t)data.size());
	if (!data.empty())
	{
		memcpy(bytes.ptrw(), data.data(), data.size());
	}
	return bytes;
}

} // namespace godot
