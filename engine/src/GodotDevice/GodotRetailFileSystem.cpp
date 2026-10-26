// OpenBFME. GPL-3.0.

#include "GodotDevice/GodotRetailFileSystem.h"
#include "Common/PathIO.h"
#include "Common/BfmeGame.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"

#include "Common/RetailArchivePolicy.h"
#include "Common/ArchiveFile.h"
#include "GameClient/UserDataFolder.h"
#include "Common/ModEngineFeatures.h"
#include "Common/MD5.h"
#include "GameClient/UserDataFolder.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <chrono>
#include <godot_cpp/classes/config_file.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/dir_access.hpp>
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

// lane MOD-5: the user maps folder of a RotWK install (the user data folder that its gi.dat names in the device's application data folder, then
// RW 0x70254F's "Maps"); "" + `error` when the install's gi.dat cannot be read
std::string userMapFolderOf(const std::string &rotwkRoot, std::string &error)
{
	const std::string userData = UserDataFolder::userDataFolderOfInstall(rotwkRoot, toNative(OS::get_singleton()->get_data_dir()), '/', nullptr, &error);
	return userData.empty() ? std::string() : UserDataFolder::userMapFolder(userData);
}
}

RetailFileSystem::RetailFileSystem() = default;
RetailFileSystem::~RetailFileSystem() = default;

void RetailFileSystem::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("mount_bfme1_path", "root"), &RetailFileSystem::mount_bfme1_path);
	ClassDB::bind_method(D_METHOD("is_bfme1"), &RetailFileSystem::is_bfme1);
	ClassDB::bind_method(D_METHOD("mount_retail"), &RetailFileSystem::mount_retail);
	ClassDB::bind_method(D_METHOD("mount_retail_paths", "rotwk", "bfme2"), &RetailFileSystem::mount_retail_paths); // RELEASE-1
	ClassDB::bind_method(D_METHOD("create_bfme2_shell"), &RetailFileSystem::create_bfme2_shell);
	ClassDB::bind_method(D_METHOD("is_mounted"), &RetailFileSystem::is_mounted);
	ClassDB::bind_method(D_METHOD("is_shell_source"), &RetailFileSystem::is_shell_source);
	ClassDB::bind_method(D_METHOD("install_root"), &RetailFileSystem::install_root);
	ClassDB::bind_method(D_METHOD("file_exists", "path"), &RetailFileSystem::file_exists);
	ClassDB::bind_method(D_METHOD("read_file", "path"), &RetailFileSystem::read_file);
	ClassDB::bind_method(D_METHOD("get_archive_for_file", "path"), &RetailFileSystem::get_archive_for_file);
	ClassDB::bind_method(D_METHOD("get_last_error"), &RetailFileSystem::get_last_error);
	ClassDB::bind_method(D_METHOD("get_loose_files"), &RetailFileSystem::get_loose_files);
	ClassDB::bind_method(D_METHOD("get_user_map_folder"), &RetailFileSystem::get_user_map_folder); // lane MOD-5
	ClassDB::bind_method(D_METHOD("set_mods", "paths"), &RetailFileSystem::set_mods); // MODS-1
	ClassDB::bind_method(D_METHOD("get_mods"), &RetailFileSystem::get_mods);
	ClassDB::bind_method(D_METHOD("get_mod_report"), &RetailFileSystem::get_mod_report);
	ClassDB::bind_method(D_METHOD("get_file_sources", "path"), &RetailFileSystem::get_file_sources);
	ClassDB::bind_method(D_METHOD("set_asset_packs", "folders"), &RetailFileSystem::set_asset_packs); // SDK-0
	ClassDB::bind_method(D_METHOD("set_install_mod", "folder"), &RetailFileSystem::set_install_mod); // MODS-2
	ClassDB::bind_method(D_METHOD("set_install_mods", "folders"), &RetailFileSystem::set_install_mods);
	ClassDB::bind_method(D_METHOD("set_mod_engines", "md5s"), &RetailFileSystem::set_mod_engines); // MODS-3
}

Dictionary RetailFileSystem::set_asset_packs(const PackedStringArray &folders)
{
	AssetPacks packs;
	std::vector<std::string> errors;
	for (int64_t i = 0; i < folders.size(); ++i)
	{
		packs.addPack(toNative(folders[i]), errors);
	}
	Dictionary d;
	PackedStringArray e, names, notes;
	for (const std::string &s : errors)
	{
		e.push_back(toGodot(s));
		UtilityFunctions::push_error("RetailFileSystem: ", toGodot(s));
	}
	if (errors.empty())
	{
		m_packs = std::move(packs); // all or nothing: a pack with an error changes nothing
	}
	for (const std::string &s : m_packs.packs())
	{
		names.push_back(toGodot(s));
	}
	for (const std::string &s : m_packs.notes())
	{
		notes.push_back(toGodot(s));
	}
	d["ok"] = errors.empty();
	d["errors"] = e;
	d["packs"] = names;
	d["textures"] = (int64_t)m_packs.textureCount();
	d["models"] = (int64_t)m_packs.modelCount();
	d["notes"] = notes;
	d["not_covered"] = toGodot(AssetPacks::notCovered());
	return d;
}

std::unique_ptr<W3DFileSource> RetailFileSystem::renderW3DSource()
{
	std::unique_ptr<W3DFileSource> base(new ArchiveW3DFileSource(*m_fs));
	if (m_packs.empty())
	{
		return base;
	}
	return std::unique_ptr<W3DFileSource>(new AssetPackW3DFileSource(std::move(base), m_packs));
}

void RetailFileSystem::set_mods(const PackedStringArray &paths)
{
	m_modArgs = paths;
}

void RetailFileSystem::set_install_mod(const String &folder)
{
	m_installModArgs.clear();
	if (!folder.is_empty())
	{
		m_installModArgs.push_back(folder);
	}
}

void RetailFileSystem::set_install_mods(const PackedStringArray &folders)
{
	m_installModArgs = folders;
}

void RetailFileSystem::set_mod_engines(const PackedStringArray &md5s)
{
	m_modEngines = md5s;
}

std::vector<std::string> RetailFileSystem::modIdentity() const
{
	std::vector<std::string> lines = m_installModReport.identity; // the install-folder mod first: it is mounted first
	lines.insert(lines.end(), m_modReport.identity.begin(), m_modReport.identity.end());
	return lines;
}

Dictionary RetailFileSystem::get_mod_report() const
{
	Dictionary d;
	d["mounted"] = m_modReport.ok;
	Array archives;
	for (const MountedArchive &a : m_modReport.archives)
	{
		Dictionary e;
		e["path"] = toGodot(a.canonicalPath);
		e["size"] = (int64_t)a.size;
		archives.push_back(e);
	}
	d["archives"] = archives;
	d["folder"] = toGodot(m_modReport.folderName);
	d["loose_files"] = (int64_t)m_modReport.loose.files.size();
	PackedStringArray identity, stops;
	for (const std::string &l : m_modReport.identity)
	{
		identity.push_back(toGodot(l));
	}
	for (const std::string &l : m_modReport.stops)
	{
		stops.push_back(toGodot(l));
	}
	d["identity"] = identity;
	d["stops"] = stops;
	d["install_mod"] = toGodot(m_installModReport.folderName);
	d["install_mod_archives"] = (int64_t)m_installModReport.archives.size();
	d["files_hashed"] = m_modReport.filesHashed;
	d["md5_cache_hits"] = m_modReport.md5CacheHits;
	return d;
}

PackedStringArray RetailFileSystem::get_file_sources(const String &path) const
{
	PackedStringArray out;
	if (m_fs)
	{
		for (const std::string &s : m_fs->sourcesOf(toNative(path)))
		{
			out.push_back(toGodot(s));
		}
	}
	return out;
}

Dictionary RetailFileSystem::mount_retail()
{
	if (m_shellSource)
	{
		m_lastError = "a presentation-only BFME2 source cannot be mounted as a game file system";
		Dictionary result;
		PackedStringArray errors;
		errors.push_back(m_lastError);
		result["ok"] = false;
		result["errors"] = errors;
		return result;
	}
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
	if (m_shellSource)
	{
		m_lastError = "a presentation-only BFME2 source cannot be mounted as a game file system";
		Dictionary result;
		PackedStringArray errors;
		errors.push_back(m_lastError);
		result["ok"] = false;
		result["errors"] = errors;
		return result;
	}
	return mountFrom(toNative(rotwk), toNative(bfme2), PackedStringArray());
}

Dictionary RetailFileSystem::mount_bfme1_path(const String &root)
{
	setBfme1Mounted(false); // Sol review r1: a failed mount leaves no BFME1 client behaviour behind (set again only once this one succeeds)
	m_fs.reset();
	m_mounted = false;
	m_bfme1 = false;
	m_bfme1Root.clear();
	m_mountedArchives.clear();
	m_loose.clear();
	m_userMapFolder.clear();
	m_userMapError.clear();
	PackedStringArray errors;
	std::string pathError;
	if (!PathIO::checkInstallPath(toNative(root), pathError))
	{
		errors.push_back(toGodot(pathError));
		Dictionary r;
		r["ok"] = false;
		r["errors"] = errors;
		return r;
	}
	auto fs = std::make_unique<Win32BIGFileSystem>();
	const auto directory = PathIO::native(toNative(root));
	std::error_code ec;
	std::vector<std::string> names;
	PackedStringArray excluded; // community PR #36 review: reported, never silently skipped (PLAN rule 7)
	for (std::filesystem::recursive_directory_iterator it(directory, ec), end; !ec && it != end; it.increment(ec))
	{
		if (!it->is_regular_file()) continue;
		auto extension = it->path().extension().u8string();
		std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return std::tolower(c); });
		if (extension != ".big") continue;
		const auto filename = it->path().filename().u8string();
		auto relative = it->path().lexically_relative(directory).u8string();
		// the AIO launcher's generated graphics overrides ("!!!*.big") are not BFME1 1.03 data: left out, and named in the report
		if (filename.rfind("!!!", 0) == 0)
		{
			excluded.push_back(toGodot(relative));
			continue;
		}
		std::replace(relative.begin(), relative.end(), '/', '\\');
		names.push_back(relative);
	}
	if (ec) errors.push_back(toGodot("Cannot read BFME1 folder '" + toNative(root) + "': " + ec.message()));
	if (names.empty()) errors.push_back("Choose a BFME1 installation containing BIG archives: " + root);
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
		entry.size = std::filesystem::file_size(PathIO::native(path), ec);
		const auto stamp = std::filesystem::last_write_time(PathIO::native(path), ec).time_since_epoch().count();
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
	// Sol review r1: the archives must be BFME1's by the same test every gate uses (isBfme1Data: its legend without BFME2's expansion legend), so the
	// client paths (bfme1Mounted) and the parsing / simulation (the data) never select different games
	if (errors.is_empty() && !isBfme1Data(*fs))
	{
		errors.push_back("not BFME1 data: Data\\INI\\Default\\SubsystemLegend.ini without SubsystemLegendExpansion1.ini is required (a BFME2 / RotWK install mounts with mount_retail)");
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
		setBfme1Mounted(true); // the client paths (Common/BfmeGame.h); the model / texture layout comes from the archives themselves
	}
	m_lastError = errors.is_empty() ? String() : String(errors[0]);
	Dictionary result;
	result["ok"] = m_mounted;
	result["errors"] = errors;
	result["game"] = "bfme1";
	result["excluded_archives"] = excluded;
	PackedStringArray stops;
	for (const std::string &l : bfme1StopLines())
	{
		stops.push_back(toGodot(l));
	}
	result["stops"] = stops; // S-5017
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
	setBfme1Mounted(false); // RotWK / BFME2
	m_fs.reset();
	m_mounted = false;
	m_bfme1 = false;
	m_bfme1Root.clear();
	m_mountedArchives.clear();
	m_loose.clear();
	m_userMapFolder.clear();
	m_userMapError.clear();
	m_shellSource = false;
	m_installRoot.clear();
	m_modReport = ModMountReport();
	m_installModReport = ModMountReport();
	{
		// lane MODS-3: the mod's engine features, before anything is parsed (Common/ModEngineFeatures.h); an unknown game binary fails the mount
		std::vector<std::string> engines;
		for (int64_t i = 0; i < m_modEngines.size(); ++i)
		{
			engines.push_back(toNative(m_modEngines[i]));
		}
		std::string engineError;
		if (!ModEngineFeatures::activate(engines, &engineError))
		{
			ModEngineFeatures::reset();
			errors.push_back(toGodot(engineError));
		}
		for (const std::string &p : ModEngineFeatures::activeProfiles())
		{
			UtilityFunctions::print("RetailFileSystem: mod engine profile: ", toGodot(p));
		}
		for (const std::string &l : ModEngineFeatures::stopLines())
		{
			UtilityFunctions::print("RetailFileSystem: ", toGodot(l));
		}
	}

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
		// md5 of every archive is always verified; the cache only makes repeat mounts fast.
		options.md5CachePath = toNative(ProjectSettings::get_singleton()->globalize_path("user://retail-md5-cache.tsv"));
		std::unique_ptr<Win32BIGFileSystem> fs = std::make_unique<Win32BIGFileSystem>();
		RetailMountReport report = MountRetailArchives(*fs, installs, options);
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
		PackedStringArray stops; // lane MODS-2 (S-5302)
		for (const std::string &l : report.stops)
		{
			stops.push_back(toGodot(l));
		}
		result["stops"] = stops;
		result["archives_hashed"] = report.archivesHashed;
		result["md5_cache_hits"] = report.md5CacheHits;
		result["mounted"] = mounted;
		result["excluded_found"] = excluded;
		if (report.ok && !m_installModArgs.is_empty())
		{
			// lane MODS-2: an install-folder mod: the file system the merged game folder would give (Common/ModMount.h MountInstallOverlay)
			std::unique_ptr<Win32BIGFileSystem> merged = std::make_unique<Win32BIGFileSystem>();
			ModMountOptions overlayOptions;
			overlayOptions.md5CachePath = toNative(ProjectSettings::get_singleton()->globalize_path("user://mod-md5-cache.tsv"));
			std::vector<std::string> folders;
			for (int64_t i = 0; i < m_installModArgs.size(); ++i)
			{
				folders.push_back(toNative(m_installModArgs[i]));
			}
			m_installModReport = MountInstallOverlay(*merged, report, folders, overlayOptions);
			for (const std::string &e : m_installModReport.errors)
			{
				errors.push_back(toGodot(e));
			}
			for (const std::string &l : m_installModReport.notes)
			{
				UtilityFunctions::print("RetailFileSystem: ", toGodot(l));
			}
			for (const std::string &l : m_installModReport.stops)
			{
				UtilityFunctions::print("RetailFileSystem: ", toGodot(l));
			}
			if (m_installModReport.ok)
			{
				fs = std::move(merged);
			}
			else
			{
				report.ok = false; // the mod the player asked for, or nothing (never retail instead)
			}
		}
		if (report.ok)
		{
			m_fs = std::move(fs);
			m_mounted = true;
			m_installRoot = bfme2Root;
			m_mountedArchives = m_installModReport.ok ? m_installModReport.merged : report.mounted; // MP-1 (MODS-2: the merged order of an install mod)
			// PLAN rule 7 / stop S-039: loose map files in the install folders are contamination; they are
			// reported by the mount and by every map build, never read.
			std::string scanError;
			if (!LooseFileScan::scanInstalls({ { "rotwk", rotwkRoot }, { "bfme2", bfme2Root } }, m_loose, &scanError))
			{
				errors.push_back(toGodot("loose file scan: " + scanError));
				m_fs.reset();
				m_mounted = false;
			}
			for (const LooseFileFinding &f : m_loose)
			{
				UtilityFunctions::push_warning("RetailFileSystem: ", toGodot(f.line));
			}
			// lane MOD-5: the user maps folder, the one folder the archive file system reads from disk (RW 0xA149A2's local step)
			if (m_fs)
			{
				m_userMapFolder = userMapFolderOf(rotwkRoot, m_userMapError);
				m_fs->setLocalFolder(m_userMapFolder);
				if (!m_userMapError.empty())
				{
					UtilityFunctions::push_warning("RetailFileSystem: no user maps folder: ", toGodot(m_userMapError));
				}
			}
		}
		// lane MODS-1: the -mod operands over the verified retail set (RW 0x7BAA44 runs after the archives are mounted)
		if (m_mounted && !m_modArgs.is_empty())
		{
			mountMods(rotwkRoot, errors);
		}
	}

	result["mod"] = get_mod_report();
	result["loose_files"] = get_loose_files();
	result["user_map_folder"] = toGodot(m_userMapFolder);
	if (!m_userMapError.empty())
	{
		result["user_map_error"] = toGodot(m_userMapError);
	}
	result["ok"] = m_mounted;
	result["errors"] = errors;
	m_lastError = errors.is_empty() ? String() : errors[0];
	for (int64_t i = 0; i < errors.size(); ++i)
	{
		UtilityFunctions::push_error("RetailFileSystem: ", errors[i]);
	}
	return result;
}

void RetailFileSystem::mountMods(const std::string &rotwkRoot, PackedStringArray &errors)
{
	// the user data folder of a relative -mod path (RW 0x6427C8 = GlobalData + 0x1244, set by RW 0x644148 from the install's gi.dat leaf name)
	std::string userData;
	{
		std::vector<std::uint8_t> bytes;
		std::ifstream in(std::filesystem::u8path(rotwkRoot + "/gi.dat"), std::ios::binary);
		if (in)
		{
			bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
			std::string leafError;
			const std::string leaf = UserDataFolder::leafName(bytes, &leafError);
			if (!leaf.empty())
			{
				userData = UserDataFolder::userDataFolder(toNative(OS::get_singleton()->get_data_dir()), leaf, '/');
			}
		}
	}
	ModOperand operand;
	bool ok = true;
	for (int64_t i = 0; i < m_modArgs.size(); ++i)
	{
		std::string error;
		if (!ParseModArgument(toNative(m_modArgs[i]), userData, operand, &error))
		{
			errors.push_back(toGodot(error));
			ok = false;
		}
	}
	if (ok)
	{
		ModMountOptions options;
		options.md5CachePath = toNative(ProjectSettings::get_singleton()->globalize_path("user://mod-md5-cache.tsv"));
		options.rotwkInstallRoot = rotwkRoot;
		m_modReport = MountRetailMod(*m_fs, operand, options);
		for (const std::string &e : m_modReport.errors)
		{
			errors.push_back(toGodot(e));
		}
		for (const std::string &w : m_modReport.warnings)
		{
			UtilityFunctions::push_warning("RetailFileSystem: ", toGodot(w));
		}
		for (const std::string &l : m_modReport.stops)
		{
			UtilityFunctions::print("RetailFileSystem: ", toGodot(l));
		}
		ok = m_modReport.ok;
	}
	if (!ok)
	{
		// a mod that cannot be mounted fails the whole mount: playing retail data instead would be a silent fallback
		m_fs.reset();
		m_mounted = false;
		m_mountedArchives.clear();
		m_modReport = ModMountReport();
	}
}

Ref<RetailFileSystem> RetailFileSystem::create_bfme2_shell()
{
	if (!m_mounted || m_shellSource)
	{
		m_lastError = m_shellSource ? "a presentation-only source cannot create another shell source" : "the normal retail file system is not mounted";
		return Ref<RetailFileSystem>();
	}
	if (m_installRoot.empty())
	{
		m_lastError = "the mounted retail file system has no BFME2 install root";
		return Ref<RetailFileSystem>();
	}
	Ref<RetailFileSystem> shell;
	shell.instantiate();
	const Dictionary report = shell->mountBfme2Shell(m_installRoot);
	if (!(bool)report.get("ok", false))
	{
		m_lastError = shell->get_last_error();
		return Ref<RetailFileSystem>();
	}
	return shell;
}

Dictionary RetailFileSystem::mountBfme2Shell(const std::string &bfme2Root)
{
	Dictionary result;
	PackedStringArray errors;
	m_fs.reset();
	m_mounted = false;
	m_shellSource = false;
	m_installRoot.clear();
	m_loose.clear();
	m_userMapFolder.clear();
	m_userMapError.clear();
	m_mountedArchives.clear();

	RetailInstall install;
	install.label = "bfme2";
	install.root = bfme2Root;
	std::string error;
	if (!RetailArchivePolicy::loadBuiltin("bfme2-106", install.policy, &error))
	{
		errors.push_back(toGodot(error));
	}
	result["bfme2_install"] = toGodot(bfme2Root);
	if (errors.is_empty())
	{
		RetailMountOptions options;
		options.md5CachePath = toNative(ProjectSettings::get_singleton()->globalize_path("user://retail-md5-cache.tsv"));
		auto fs = std::make_unique<Win32BIGFileSystem>();
		RetailMountReport report = MountRetailArchives(*fs, { install }, options);
		for (const std::string &e : report.errors)
		{
			errors.push_back(toGodot(e));
		}
		Array mounted;
		for (const MountedArchive &a : report.mounted)
		{
			Dictionary d;
			d["install"] = toGodot(a.install);
			d["path"] = toGodot(a.canonicalPath);
			d["disk_path"] = toGodot(a.diskPath);
			d["size"] = (int64_t)a.size;
			mounted.push_back(d);
		}
		result["mounted"] = mounted;
		result["archives_hashed"] = report.archivesHashed;
		result["md5_cache_hits"] = report.md5CacheHits;
		PackedStringArray excluded;
		for (const std::string &e : report.excludedFound)
		{
			excluded.push_back(toGodot(e));
		}
		result["excluded_found"] = excluded;
		PackedStringArray warnings;
		for (const std::string &w : report.warnings)
		{
			warnings.push_back(toGodot(w));
			UtilityFunctions::push_warning("RetailFileSystem: ", toGodot(w));
		}
		result["warnings"] = warnings;
		if (report.ok)
		{
			std::string scanError;
			if (!LooseFileScan::scanInstalls({ { "bfme2", bfme2Root } }, m_loose, &scanError))
			{
				errors.push_back(toGodot("loose file scan: " + scanError));
			}
			if (errors.is_empty())
			{
				m_fs = std::move(fs);
				m_mounted = true;
				m_shellSource = true;
				m_installRoot = bfme2Root;
				m_mountedArchives = report.mounted;
			}
		}
	}
	result["loose_files"] = get_loose_files();
	result["ok"] = m_mounted;
	result["errors"] = errors;
	m_lastError = errors.is_empty() ? String() : String(errors[0]);
	for (int64_t i = 0; i < errors.size(); ++i)
	{
		UtilityFunctions::push_error("RetailFileSystem: ", errors[i]);
	}
	return result;
}

String RetailFileSystem::install_root() const
{
	return toGodot(m_installRoot);
}

ArchiveFileSystem *RetailFileSystem::archive_fs() const
{
	return m_fs.get();
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

bool RetailFileSystem::mapsFolder(bool userMap, std::string &out, std::string *error) const
{
	if (!userMap)
	{
		out = "maps";
		return true;
	}
	out = m_userMapFolder;
	if (out.empty() && error)
	{
		*error = "no user maps folder: " + (m_userMapError.empty() ? std::string("only the RotWK 2.01 mount has one") : m_userMapError);
	}
	return !out.empty();
}

String RetailFileSystem::get_user_map_folder() const
{
	return toGodot(m_userMapFolder);
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
