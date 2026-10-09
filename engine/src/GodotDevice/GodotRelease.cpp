// OpenBFME. GPL-3.0.
// See GodotDevice/GodotRelease.h (lane RELEASE-1).

#include "GodotDevice/GodotRelease.h"

#include "Common/BuildVersion.h"
#include "Common/ConsoleFilter.h"
#include "Common/InstallLocator.h"
#include "GameNetwork/ProfileIdentity.h"

#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

#include <chrono>
#include <filesystem>
#include <thread>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <sys/types.h>
#endif

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

std::string configFile()
{
	return toNative(ProjectSettings::get_singleton()->globalize_path("user://install-paths.cfg"));
}

PackedStringArray toArray(const std::vector<std::string> &v)
{
	PackedStringArray out;
	for (const std::string &s : v)
		out.push_back(toGodot(s));
	return out;
}

Dictionary checkToDict(const InstallLocator::InstallCheck &c)
{
	Dictionary d;
	d["ok"] = c.ok;
	d["game"] = toGodot(c.game);
	d["root"] = toGodot(c.root);
	d["errors"] = toArray(c.errors);
	d["missing"] = toArray(c.missing);
	d["present"] = c.present;
	d["expected"] = c.expected;
	return d;
}
} // namespace

// ---- ReleaseInfo ----

void ReleaseInfo::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("version"), &ReleaseInfo::version);
	ClassDB::bind_method(D_METHOD("commit"), &ReleaseInfo::commit);
	ClassDB::bind_method(D_METHOD("build_date"), &ReleaseInfo::build_date);
	ClassDB::bind_method(D_METHOD("version_line"), &ReleaseInfo::version_line);
	ClassDB::bind_method(D_METHOD("engine_id"), &ReleaseInfo::engine_id);
	ClassDB::bind_method(D_METHOD("build_record"), &ReleaseInfo::build_record);
	ClassDB::bind_method(D_METHOD("redact", "text"), &ReleaseInfo::redact);
	ClassDB::bind_method(D_METHOD("process_alive", "pid"), &ReleaseInfo::process_alive);
}

String ReleaseInfo::version() const
{
	return String(BuildVersion::kVersion);
}

String ReleaseInfo::commit() const
{
	return String(BuildVersion::kCommit);
}

String ReleaseInfo::build_date() const
{
	return String(BuildVersion::kBuildDate);
}

String ReleaseInfo::version_line() const
{
	return toGodot(BuildVersion::versionLine());
}

String ReleaseInfo::engine_id() const
{
	return String(ProfileIdentity::buildId());
}

String ReleaseInfo::build_record() const
{
	return String(BuildVersion::kBuildRecord);
}

String ReleaseInfo::redact(const String &text) const
{
	return toGodot(LogPrivacy::redact(toNative(text), LogPrivacy::hostRules()));
}

bool ReleaseInfo::process_alive(int64_t pid) const
{
	if (pid <= 0)
		return false;
#ifdef _WIN32
	HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, (DWORD)pid);
	if (!h)
		return GetLastError() == ERROR_ACCESS_DENIED; // exists, owned by someone else
	DWORD code = 0;
	const bool alive = GetExitCodeProcess(h, &code) && code == STILL_ACTIVE;
	CloseHandle(h);
	return alive;
#else
	return ::kill((pid_t)pid, 0) == 0 || errno == EPERM;
#endif
}

// ---- SessionLogger ----

void SessionLogger::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("open", "path"), &SessionLogger::open);
	ClassDB::bind_method(D_METHOD("close"), &SessionLogger::close);
	ClassDB::bind_method(D_METHOD("is_open"), &SessionLogger::is_open);
	ClassDB::bind_method(D_METHOD("get_path"), &SessionLogger::get_path);
	ClassDB::bind_method(D_METHOD("get_last_error"), &SessionLogger::get_last_error);
	ClassDB::bind_method(D_METHOD("write_line", "line"), &SessionLogger::write_line);
	ClassDB::bind_method(D_METHOD("filter_console"), &SessionLogger::filter_console);
}

SessionLogger::~SessionLogger()
{
	close();
}

bool SessionLogger::open(const String &path)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	if (m_file)
	{
		std::fclose(m_file);
		m_file = nullptr;
	}
	m_rules = LogPrivacy::hostRules();
	const std::string native = toNative(ProjectSettings::get_singleton()->globalize_path(path));
	std::error_code ec;
	std::filesystem::path p = std::filesystem::u8path(native);
	if (p.has_parent_path())
		std::filesystem::create_directories(p.parent_path(), ec);
#ifdef _WIN32
	m_file = _wfopen(p.wstring().c_str(), L"wb");
#else
	m_file = std::fopen(native.c_str(), "wb");
#endif
	if (!m_file)
	{
		m_error = "could not create the log file " + toGodot(LogPrivacy::redact(native, m_rules));
		return false;
	}
	m_path = path;
	m_error = String();
	const std::string head = BuildVersion::versionLine() + ", commit " + BuildVersion::kCommit + ", engine id " + ProfileIdentity::buildId() + "\n";
	std::fwrite(head.data(), 1, head.size(), m_file);
	std::fflush(m_file);
	return true;
}

void SessionLogger::close()
{
	ConsoleFilter::uninstall();
	std::lock_guard<std::mutex> lock(m_mutex);
	if (m_file)
	{
		std::fclose(m_file);
		m_file = nullptr;
	}
}

bool SessionLogger::is_open() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_file != nullptr;
}

void SessionLogger::writeRaw(const std::string &utf8)
{
	// called from any thread (Godot's loggers are), and from the crash handler: a crash while another thread held the lock must not hang
	// the dump, so the lock is tried for at most 200 ms. One write + flush per message keeps lines whole and on disk.
	std::unique_lock<std::mutex> lock(m_mutex, std::defer_lock);
	for (int i = 0; i < 200 && !lock.try_lock(); ++i)
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	// Godot's crash handler (CrashHandler::handle_crash) starts its dump with this line; from then on the console gets every message directly
	// (ConsoleFilter::crashWrite), the filter thread may not run again before the process dies
	if (!m_crashed && utf8.find("Program crashed with signal") != std::string::npos)
		m_crashed = true;
	if (m_crashed)
		ConsoleFilter::crashWrite(utf8);
	if (!m_file)
		return;
	const std::string clean = LogPrivacy::redact(utf8, m_rules);
	std::fwrite(clean.data(), 1, clean.size(), m_file);
	if (clean.empty() || clean.back() != '\n')
		std::fputc('\n', m_file);
	std::fflush(m_file);
}

bool SessionLogger::filter_console()
{
	std::string error;
	if (ConsoleFilter::install(LogPrivacy::hostRules(), &error))
		return true;
	m_error = toGodot(error);
	return false;
}

void SessionLogger::write_line(const String &line)
{
	writeRaw(toNative(line));
}

void SessionLogger::_log_message(const String &p_message, bool p_error)
{
	(void)p_error; // printerr text arrives here as is; Godot's own errors come through _log_error
	writeRaw(toNative(p_message));
}

void SessionLogger::_log_error(const String &p_function, const String &p_file, int32_t p_line, const String &p_code, const String &p_rationale,
	bool p_editor_notify, int32_t p_error_type, const TypedArray<Ref<ScriptBacktrace>> &p_script_backtraces)
{
	(void)p_editor_notify;
	// Logger.ErrorType: 0 error, 1 warning, 2 script error, 3 shader error (the prefixes Godot's own log uses)
	static const char *const kinds[] = { "ERROR", "WARNING", "SCRIPT ERROR", "SHADER ERROR" };
	const char *kind = p_error_type >= 0 && p_error_type < 4 ? kinds[p_error_type] : "ERROR";
	String text = String(kind) + ": " + (p_rationale.is_empty() ? p_code : p_rationale) + "\n   at: " + p_function + " (" + p_file + ":" + String::num_int64(p_line) + ")";
	for (int64_t i = 0; i < p_script_backtraces.size(); ++i)
	{
		Ref<ScriptBacktrace> bt = p_script_backtraces[i];
		if (bt.is_valid() && !bt->is_empty())
			text += "\n" + bt->format(3, 4);
	}
	writeRaw(toNative(text));
}

// ---- InstallSetup ----

void InstallSetup::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("config_path"), &InstallSetup::config_path);
	ClassDB::bind_method(D_METHOD("configured"), &InstallSetup::configured);
	ClassDB::bind_method(D_METHOD("discover"), &InstallSetup::discover);
	ClassDB::bind_method(D_METHOD("check", "rotwk", "bfme2"), &InstallSetup::check);
	ClassDB::bind_method(D_METHOD("remember", "rotwk", "bfme2"), &InstallSetup::remember);
}

String InstallSetup::config_path() const
{
	return toGodot(configFile());
}

Dictionary InstallSetup::configured() const
{
	InstallLocator::Configured c = InstallLocator::configured(configFile());
	Dictionary d;
	d["source"] = toGodot(c.source);
	d["rotwk"] = toGodot(c.rotwk);
	d["bfme2"] = toGodot(c.bfme2);
	return d;
}

Array InstallSetup::discover() const
{
	Array out;
	RetailArchivePolicy rotwk, bfme2;
	std::string error;
	const bool policies = RetailArchivePolicy::loadBuiltin("rotwk-201", rotwk, &error) && RetailArchivePolicy::loadBuiltin("bfme2-106", bfme2, &error);
	for (const InstallLocator::Candidate &c : InstallLocator::discover(InstallLocator::DiscoveryEnvironment::host()))
	{
		Dictionary d;
		d["game"] = toGodot(c.game);
		d["path"] = toGodot(c.path);
		d["source"] = toGodot(c.source);
		if (policies)
		{
			const bool isRotwk = c.game == InstallLocator::kRotwk;
			InstallLocator::InstallCheck check = InstallLocator::checkInstall(c.game, c.path, isRotwk ? rotwk : bfme2, isRotwk ? &bfme2 : &rotwk);
			d["ok"] = check.ok;
			d["errors"] = toArray(check.errors);
		}
		else
		{
			d["ok"] = false;
			PackedStringArray e;
			e.push_back(toGodot(error));
			d["errors"] = e;
		}
		out.push_back(d);
	}
	return out;
}

Dictionary InstallSetup::check(const String &rotwk, const String &bfme2) const
{
	std::vector<InstallLocator::InstallCheck> checks = InstallLocator::checkPair(toNative(rotwk), toNative(bfme2));
	Dictionary d;
	PackedStringArray errors;
	bool ok = true;
	for (const InstallLocator::InstallCheck &c : checks)
	{
		d[toGodot(c.game)] = checkToDict(c);
		ok = ok && c.ok;
		for (const std::string &e : c.errors)
		{
			if (!errors.has(toGodot(e)))
				errors.push_back(toGodot(e));
		}
	}
	d["ok"] = ok;
	d["errors"] = errors;
	return d;
}

Dictionary InstallSetup::remember(const String &rotwk, const String &bfme2) const
{
	std::string error;
	Dictionary d;
	d["ok"] = InstallLocator::remember(configFile(), toNative(rotwk), toNative(bfme2), &error);
	d["error"] = toGodot(error);
	return d;
}

} // namespace godot
