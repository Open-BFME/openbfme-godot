// OpenBFME. GPL-3.0.
//
// Device layer: exposes the retail archive file system to Godot. Godot never reads retail
// files itself; everything goes through the SAGE-port ArchiveFileSystem.

#pragma once

#include "Common/LooseFileScan.h"
#include "Common/RetailArchivePolicy.h"

#include <godot_cpp/classes/ref_counted.hpp>
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
	// lane RELEASE-1: the same mount from two folders the player picked on the first-run screen (nothing is read from the
	// environment or the config file; remembering the pick is InstallSetup.remember's job once this succeeded).
	Dictionary mount_retail_paths(const String &rotwk, const String &bfme2);

	bool is_mounted() const { return m_mounted; }
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

protected:
	static void _bind_methods();

private:
	Dictionary mountFrom(const std::string &rotwkRoot, const std::string &bfme2Root, PackedStringArray errors);

	std::unique_ptr<Win32BIGFileSystem> m_fs;
	bool m_mounted = false;
	String m_lastError;
	std::vector<LooseFileFinding> m_loose;
	std::vector<MountedArchive> m_mountedArchives;
};

} // namespace godot
