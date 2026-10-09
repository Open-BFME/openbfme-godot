// OpenBFME. GPL-3.0.
//
// ArchiveHandle: the one backing handle of a disk archive.
//
// Zero Hour keeps the archive open for the life of the file system (Win32BIGFile::attachFile). The
// rebuild keeps that identity guarantee and fixes the cost of it:
//   * ONE handle per archive FILE, shared (shared_ptr) by every Win32BIGFile that mounts it, in any
//     mount, and closed when the last owner goes away. "The same file" is decided by the file's
//     real identity, not its path spelling: volume serial + file index on Windows
//     (GetFileInformationByHandle), st_dev + st_ino elsewhere, so `F:\RotWK\x.big`,
//     `f:\rotwk\X.BIG` and a hard link are one handle.
//   * the directory is parsed and every entry is read through that same handle, so what a mount
//     reads is always the file it verified when it mounted.
//   * on Windows (_WIN32, MSVC and MinGW alike: _wfsopen with _SH_DENYWR) the handle denies other
//     writers and deleters, so the archive cannot be overwritten, renamed over or deleted while any
//     mount holds it. Other platforms cannot do this: a rename-over leaves the open inode alone but
//     an in-place overwrite changes what the mount reads. That is ACCEPTANCE STOP S-012: such a
//     handle reports replacementProtected() == false and the file system reports it as
//     unverifiedIdentity().
//   * every read is atomic (seek + read under one mutex), so threads may read concurrently.

#pragma once

#include <cstdint>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>

class ArchiveHandle
{
public:
	// Opens (or shares) the handle for the file at `path`. nullptr + *error on failure.
	// protect=false skips the deny-write share mode (tests use it to exercise the S-012 report on
	// any platform); a protected live handle is still shared with unprotected requests. Where the
	// platform cannot protect at all (not _WIN32) a protect=true request is compatible with the
	// existing unprotected handle, so it is SHARED (and the mount reports S-012), never reopened.
	static std::shared_ptr<ArchiveHandle> open(const std::string &path, std::string *error, bool protect = true);

	~ArchiveHandle();

	const std::string &path() const { return m_path; }
	// Length of the file when the handle was opened.
	std::uint64_t length() const { return m_length; }
	// True when other processes cannot overwrite, rename over or delete the file while it is open.
	bool replacementProtected() const { return m_protected; }

	// Atomic positional read of exactly n bytes; false on a short read.
	bool readAt(std::uint64_t offset, void *dst, size_t n);

	// Handles currently alive in this process (tests).
	static size_t liveHandles();

private:
	ArchiveHandle() = default;

	std::string m_path;
	std::string m_key; // file identity (volume+index / dev+inode)
	std::uint64_t m_length = 0;
	long long m_writeTime = 0;
	bool m_protected = false;
	std::FILE *m_file = nullptr;
	std::mutex m_mutex;
};
