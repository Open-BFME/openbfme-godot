// OpenBFME. GPL-3.0.
// See GodotDevice/GodotRelease.h (lane RELEASE-1).

#include "GodotDevice/GodotRelease.h"
#include "Common/PathIO.h"

#include "Common/BuildVersion.h"
#include "Common/Recorder.h"
#include "Common/ConsoleFilter.h"
#include "Common/InstallLocator.h"
#include "Common/WindowsCrashDump.h"
#include "GameNetwork/JoinBuild.h"
#include "GameNetwork/ProfileIdentity.h"

#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

#include <chrono>
#include <algorithm>
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

std::string downloadedMarker()
{
	return toNative(ProjectSettings::get_singleton()->globalize_path(String("user://") + InstallLocator::kDownloadedMarkerName));
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
	d["language"] = toGodot(c.language); // issue #49: the folder's text and voice languages (RetailArchivePolicy.h "Languages")
	d["voice"] = toGodot(c.voice);
	d["unpinned_language_archives"] = toArray(c.unpinnedLanguageArchives);
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
	ClassDB::bind_method(D_METHOD("display_line"), &ReleaseInfo::display_line);
	ClassDB::bind_method(D_METHOD("engine_id"), &ReleaseInfo::engine_id);
	ClassDB::bind_method(D_METHOD("build_record"), &ReleaseInfo::build_record);
	ClassDB::bind_method(D_METHOD("inspect_replay", "path"), &ReleaseInfo::inspect_replay);
	ClassDB::bind_method(D_METHOD("redact", "text"), &ReleaseInfo::redact);
	ClassDB::bind_method(D_METHOD("join_refusal_text", "mine", "host"), &ReleaseInfo::join_refusal_text);
	ClassDB::bind_method(D_METHOD("join_details_text", "mine", "mine_profile", "host", "host_profile"), &ReleaseInfo::join_details_text);
	ClassDB::bind_method(D_METHOD("process_alive", "pid"), &ReleaseInfo::process_alive);
	ClassDB::bind_method(D_METHOD("crash_test"), &ReleaseInfo::crash_test);
	ClassDB::bind_method(D_METHOD("crash_test_worker"), &ReleaseInfo::crash_test_worker);
	ClassDB::bind_method(D_METHOD("install_crash_handler"), &ReleaseInfo::install_crash_handler);
}

namespace
{
JoinBuild::Build joinBuildOf(const Dictionary &d)
{
	JoinBuild::Build b;
	b.version = String(d.get("version", String())).utf8().get_data();
	b.date = String(d.get("date", String())).utf8().get_data();
	b.platform = String(d.get("platform", String())).utf8().get_data();
	const Variant official = d.get("official", Variant());
	if (official.get_type() == Variant::BOOL)
	{
		b.official = bool(official) ? 1 : 0;
	}
	return b;
}
} // namespace

String ReleaseInfo::join_refusal_text(const Dictionary &mine, const Dictionary &host) const
{
	return String::utf8(JoinBuild::refusalText(joinBuildOf(mine), joinBuildOf(host)).c_str());
}

String ReleaseInfo::join_details_text(const Dictionary &mine, const String &mine_profile, const Dictionary &host, const String &host_profile) const
{
	return String::utf8(JoinBuild::detailsText(joinBuildOf(mine), mine_profile.utf8().get_data(), joinBuildOf(host), host_profile.utf8().get_data()).c_str());
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

Dictionary ReleaseInfo::inspect_replay(const String &path) const
{
	ReplayFile file;
	std::string error;
	Dictionary result;
	result["ok"] = ReplayFile::load(toNative(path), file, &error);
	result["error"] = toGodot(error);
	result["version"] = toGodot(file.header.buildVersion);
	result["commit"] = toGodot(file.header.buildCommit);
	return result;
}

String ReleaseInfo::version_line() const
{
	return toGodot(BuildVersion::versionLine());
}

String ReleaseInfo::display_line() const
{
	return toGodot(BuildVersion::displayLine());
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

void ReleaseInfo::crash_test() const
{
	if (!OS::get_singleton()->is_debug_build())
		return;
	std::fprintf(stderr, "RELEASE --crash-test: crashing on purpose (a write through a null pointer)\n");
	std::fflush(stderr);
	volatile int *volatile target = nullptr;
	*target = 0x0BADF00D;
}

void ReleaseInfo::crash_test_worker() const
{
	if (!OS::get_singleton()->is_debug_build())
		return;
	// Do not call the Godot API from the crashing worker.
	std::thread worker([] {
		std::fprintf(stderr, "RELEASE --crash-test-worker: deliberate worker access violation\n");
		std::fflush(stderr);
		volatile int *volatile target = nullptr;
		*target = 0x0BADF00D;
	});
	worker.join();
}

String ReleaseInfo::install_crash_handler() const
{
	std::string error;
	if (!WindowsCrashDump::install(toNative(ProjectSettings::get_singleton()->globalize_path("user://logs/minidumps")), error))
		return toGodot(error);
	return String();
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
	m_budget = {};
	m_crashed = false;
	const std::string native = toNative(ProjectSettings::get_singleton()->globalize_path(path));
	std::error_code ec;
	std::filesystem::path p = PathIO::native(native);
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
	if (!m_budget.write(m_file, BuildVersion::versionLine() + ", commit " + BuildVersion::kCommit + ", engine id " + ProfileIdentity::buildId(), m_rules))
	{
		std::fclose(m_file);
		m_file = nullptr;
		m_error = "session log write failed";
		return false;
	}
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
	static thread_local bool writing = false;
	if (writing)
		return;
	writing = true;
	struct Reset { bool &value; ~Reset() { value = false; } } reset{writing};
	// called from any thread (Godot's loggers are), and from the crash handler: a crash while another thread held the lock must not hang
	// the dump, so the lock is tried for at most 200 ms. One write + flush per message keeps lines whole and on disk.
	std::unique_lock<std::mutex> lock(m_mutex, std::defer_lock);
	for (int i = 0; i < 200 && !lock.try_lock(); ++i)
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	if (!lock.owns_lock())
		return;
	// Godot's crash handler (CrashHandler::handle_crash) starts its dump with this line; from then on the console gets every message directly
	// (ConsoleFilter::crashWrite), the filter thread may not run again before the process dies
	if (!m_crashed && utf8.find("Program crashed with signal") != std::string::npos)
		m_crashed = true;
	if (m_crashed)
		ConsoleFilter::crashWrite(utf8);
	if (!m_file)
		return;
	if (!m_budget.write(m_file, utf8, m_rules))
	{
		std::fclose(m_file);
		m_file = nullptr;
		m_error = "session log write failed";
	}
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
	const auto field = [](const String &value, int64_t limit) { return value.length() > limit ? String(LogPrivacy::kOversized) : value; };
	String text = String(kind) + ": " + toGodot(LogPrivacy::errorMessage(toNative(p_code.left(LogPrivacy::kMaxFieldBytes + 1)), toNative(p_rationale.left(LogPrivacy::kMaxFieldBytes + 1)), m_rules)) + "\n   at: " + field(p_function, 1024) + " (" + field(p_file, 4096) + ":" + String::num_int64(p_line) + ")";
	for (int64_t i = 0; i < std::min<int64_t>(p_script_backtraces.size(), 16); ++i)
	{
		Ref<ScriptBacktrace> bt = p_script_backtraces[i];
		if (bt.is_valid())
			for (int32_t frame = 0; frame < std::min<int32_t>(bt->get_frame_count(), 32); ++frame)
				text += "\n   at: " + field(bt->get_frame_function(frame), 1024) + " (" + field(bt->get_frame_file(frame), 4096) + ":" + String::num_int64(bt->get_frame_line(frame)) + ")";
	}
	writeRaw(toNative(text));
}

// ---- InstallSetup ----

void InstallSetup::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("config_path"), &InstallSetup::config_path);
	ClassDB::bind_method(D_METHOD("configured"), &InstallSetup::configured);
	ClassDB::bind_method(D_METHOD("discover"), &InstallSetup::discover);
	ClassDB::bind_method(D_METHOD("downloaded_problem"), &InstallSetup::downloaded_problem);
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

String InstallSetup::downloaded_problem() const
{
	const std::string marker = downloadedMarker();
	std::error_code ec;
	if (!std::filesystem::exists(PathIO::native(marker), ec))
		return String();
	InstallLocator::Downloaded downloaded;
	std::string why;
	if (InstallLocator::readDownloaded(marker, downloaded, &why))
		return String();
	return toGodot("The game folders the OpenBFME launcher downloaded are not offered: its record " + why + ". Choose the folders below, or download them again with the launcher.");
}

Array InstallSetup::discover() const
{
	Array out;
	RetailArchivePolicy rotwk, bfme2;
	std::string error;
	const bool policies = RetailArchivePolicy::loadBuiltin("rotwk-201", rotwk, &error) && RetailArchivePolicy::loadBuiltin("bfme2-106", bfme2, &error);
	InstallLocator::DiscoveryEnvironment environment = InstallLocator::DiscoveryEnvironment::host();
	// lane AIO-1: the launcher's record of the folders it downloaded (a file that cannot be read: downloaded_problem, shown on the screen)
	environment.downloadedMarker = downloadedMarker();
	for (const InstallLocator::Candidate &c : InstallLocator::discover(environment))
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
