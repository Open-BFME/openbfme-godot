// OpenBFME. GPL-3.0.
//
// Lane RELEASE-1: the Godot view of what a public tester needs.
//   ReleaseInfo    the build's version (Common/BuildVersion.h), the engine id, the privacy filter of logs (Common/LogPrivacy.h)
//   SessionLogger  a Godot Logger (OS.add_logger) writing every message and error of the session to one file, each line through the
//                  privacy filter and flushed at once (a crash loses nothing that was printed)
//   InstallSetup   the install finder (Common/InstallLocator.h): the remembered folders, the discovered ones, checking and remembering
//                  a pick. user://install-paths.cfg is the file RetailFileSystem.mount_retail() reads.

#pragma once

#include "Common/LogPrivacy.h"

#include <godot_cpp/classes/logger.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/classes/script_backtrace.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/typed_array.hpp>

#include <cstdio>
#include <mutex>

namespace godot
{

class ReleaseInfo : public RefCounted
{
	GDCLASS(ReleaseInfo, RefCounted)

public:
	String version() const;      // git describe
	String commit() const;
	String build_date() const;
	String version_line() const; // "OpenBFME <version> (<date>)"
	String engine_id() const;    // GameNetwork/ProfileIdentity::buildId
	String build_record() const; // BuildVersion::kBuildRecord (the provenance package.sh checks)
	String redact(const String &text) const;
	// Is a process with this id running (any process, not only a child: OS.is_process_running only knows children)? The crash marker of
	// scripts/release/release.gd tells a live second instance (two windows of a LAN test) from a crashed run with it.
	bool process_alive(int64_t pid) const;

protected:
	static void _bind_methods();
};

class SessionLogger : public Logger
{
	GDCLASS(SessionLogger, Logger)

public:
	~SessionLogger() override;

	// Opens (truncates) the file and writes the version line first. Returns false with get_last_error() set.
	bool open(const String &path);
	void close();
	bool is_open() const;
	String get_path() const { return m_path; }
	String get_last_error() const { return m_error; }
	// A line of our own (no Godot message), also filtered.
	void write_line(const String &line);
	// Routes the console (stdout / stderr) through the same filter (Common/ConsoleFilter.h) until close(); false + get_last_error() where
	// that is not available (Windows).
	bool filter_console();

	void _log_message(const String &p_message, bool p_error) override;
	void _log_error(const String &p_function, const String &p_file, int32_t p_line, const String &p_code, const String &p_rationale,
		bool p_editor_notify, int32_t p_error_type, const TypedArray<Ref<ScriptBacktrace>> &p_script_backtraces) override;

protected:
	static void _bind_methods();

private:
	void writeRaw(const std::string &utf8);

	mutable std::mutex m_mutex;
	std::FILE *m_file = nullptr;
	String m_path;
	String m_error;
	LogPrivacy::Rules m_rules;
	bool m_crashed = false;
};

class InstallSetup : public RefCounted
{
	GDCLASS(InstallSetup, RefCounted)

public:
	String config_path() const;
	// {source: "env" | "config" | "none", rotwk, bfme2}
	Dictionary configured() const;
	// [{game: "rotwk" | "bfme2", path, source, ok, errors}] every folder found on this machine, each already checked
	Array discover() const;
	// {ok, rotwk: {ok, errors, present, expected}, bfme2: {...}, errors: every error of both}
	Dictionary check(const String &rotwk, const String &bfme2) const;
	// writes user://install-paths.cfg; {ok, error}
	Dictionary remember(const String &rotwk, const String &bfme2) const;

protected:
	static void _bind_methods();
};

} // namespace godot
