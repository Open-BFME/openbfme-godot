// OpenBFME. GPL-3.0.

#include "GodotDevice/GodotRetailFileSystem.h"

#include "Common/RetailArchivePolicy.h"
#include "Common/ArchiveFile.h"
#include <filesystem>
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

void RetailFileSystem::_bind_methods()
{
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

Dictionary RetailFileSystem::mountFrom(const std::string &rotwkRoot, const std::string &bfme2Root, PackedStringArray errors)
{
	Dictionary result;
	std::string error;
	m_fs.reset();
	m_menuFs.reset();
	m_mounted = false;
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
		RetailMountReport report = MountRetailArchives(*fs, installs, options);
		Ref<ConfigFile> wallOptions; wallOptions.instantiate();
		wallOptions->load("res://dev/wall_balance.cfg");
		Ref<ConfigFile> savedWalls; savedWalls.instantiate();
		if (savedWalls->load("user://wall_balance.cfg") == OK) wallOptions = savedWalls;
		if (report.ok && (bool)wallOptions->get_value("walls","new_towers",false))
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
			RetailMountReport uiReport = MountRetailArchives(*menuFs, { installs[1], installs[0] }, options);
			report.archivesHashed += uiReport.archivesHashed;
			report.md5CacheHits += uiReport.md5CacheHits;
			for (const auto &message : uiReport.errors) report.errors.push_back("menu theme: " + message);
			for (const auto &message : uiReport.warnings) report.warnings.push_back("menu theme: " + message);
			report.ok = uiReport.ok;
		}
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
			std::string scanError;
			if (!LooseFileScan::scanInstalls({ { "rotwk", rotwkRoot }, { "bfme2", bfme2Root } }, m_loose, &scanError))
			{
				errors.push_back(toGodot("loose file scan: " + scanError));
				m_fs.reset();
				m_menuFs.reset();
				m_mounted = false;
			}
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
