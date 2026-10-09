// OpenBFME. GPL-3.0. See ArchiveHandle.h.

#include "GameEngineDevice/Win32Device/Common/ArchiveHandle.h"

#include <filesystem>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <io.h>
#include <share.h>
#include <windows.h>
#else
#include <sys/stat.h>
#endif

namespace
{

// Every handle ever opened and still alive (expired entries are pruned on each open). A vector, not a
// map keyed by identity: two live handles to one file can legitimately coexist (an unprotected one
// plus a protected one, or a file changed in place under an old handle), and an entry for a live
// handle must never be overwritten, or liveHandles() and sharing lose track of it.
struct Registry
{
	std::mutex mutex;
	std::vector<std::weak_ptr<ArchiveHandle>> handles;
};

// Can this platform deny other writers? Same _WIN32 selection as openRead.
#ifdef _WIN32
constexpr bool kCanProtect = true;
#else
constexpr bool kCanProtect = false;
#endif

Registry &registry()
{
	static Registry *r = new Registry(); // never destroyed: handles may outlive static teardown order
	return *r;
}

// The file's real identity, from the open stream.
bool identityOf(std::FILE *f, std::string &key)
{
#ifdef _WIN32
	const HANDLE h = (HANDLE)_get_osfhandle(_fileno(f));
	BY_HANDLE_FILE_INFORMATION info;
	if (h == INVALID_HANDLE_VALUE || !GetFileInformationByHandle(h, &info))
	{
		return false;
	}
	key = "win:" + std::to_string(info.dwVolumeSerialNumber) + ":" + std::to_string(info.nFileIndexHigh) + ":" + std::to_string(info.nFileIndexLow);
	return true;
#else
	struct stat st;
	if (fstat(fileno(f), &st) != 0)
	{
		return false;
	}
	key = "posix:" + std::to_string((unsigned long long)st.st_dev) + ":" + std::to_string((unsigned long long)st.st_ino);
	return true;
#endif
}

std::FILE *openRead(const std::filesystem::path &p, bool protect)
{
#ifdef _WIN32
	// _SH_DENYWR = FILE_SHARE_READ only: no other writer and no rename/delete while open
	return protect ? _wfsopen(p.c_str(), L"rb", _SH_DENYWR) : _wfsopen(p.c_str(), L"rb", _SH_DENYNO);
#else
	(void)protect;
	return std::fopen(p.c_str(), "rb");
#endif
}

int seek64(std::FILE *f, std::uint64_t offset)
{
#ifdef _WIN32
	return _fseeki64(f, (long long)offset, SEEK_SET);
#else
	return fseeko(f, (off_t)offset, SEEK_SET);
#endif
}

int seekEnd64(std::FILE *f)
{
#ifdef _WIN32
	return _fseeki64(f, 0, SEEK_END);
#else
	return fseeko(f, 0, SEEK_END);
#endif
}

long long tell64(std::FILE *f)
{
#ifdef _WIN32
	return _ftelli64(f);
#else
	return (long long)ftello(f);
#endif
}

} // namespace

std::shared_ptr<ArchiveHandle> ArchiveHandle::open(const std::string &path, std::string *error, bool protect)
{
	const std::filesystem::path fsPath = std::filesystem::u8path(path);
	std::FILE *f = openRead(fsPath, protect);
	if (!f)
	{
		if (error)
		{
			*error = "could not open archive file " + path;
		}
		return nullptr;
	}
	if (seekEnd64(f) != 0 || tell64(f) < 0)
	{
		std::fclose(f);
		if (error)
		{
			*error = "could not get the size of archive file " + path;
		}
		return nullptr;
	}
	const std::uint64_t length = (std::uint64_t)tell64(f);
	std::error_code ec;
	const auto t = std::filesystem::last_write_time(fsPath, ec);
	const long long writeTime = ec ? 0 : (long long)t.time_since_epoch().count();
	std::string key;
	if (!identityOf(f, key))
	{
		std::fclose(f);
		if (error)
		{
			*error = "could not determine the identity of archive file " + path;
		}
		return nullptr;
	}

	Registry &reg = registry();
	std::lock_guard<std::mutex> lock(reg.mutex);
	for (auto e = reg.handles.begin(); e != reg.handles.end();)
	{
		std::shared_ptr<ArchiveHandle> live = e->lock();
		if (!live)
		{
			e = reg.handles.erase(e); // drop dead entries
			continue;
		}
		// the same file. A protected live handle serves unprotected requests, and where the platform
		// cannot protect at all an unprotected handle is the best any request can get (S-012 reports it).
		// Only a protected request against a live unprotected handle on a platform that CAN protect needs
		// its own handle. A file whose size or time changed under an old handle also gets a new one.
		if (live->m_key == key && live->m_length == length && live->m_writeTime == writeTime
			&& (live->m_protected || !protect || !kCanProtect))
		{
			std::fclose(f);
			return live;
		}
		++e;
	}

	std::shared_ptr<ArchiveHandle> h(new ArchiveHandle());
	h->m_path = path;
	h->m_key = key;
	h->m_length = length;
	h->m_writeTime = writeTime;
#ifdef _WIN32
	h->m_protected = protect;
#else
	h->m_protected = false; // an in-place overwrite cannot be prevented here (S-012)
#endif
	h->m_file = f;
	reg.handles.push_back(h);
	return h;
}

ArchiveHandle::~ArchiveHandle()
{
	if (m_file)
	{
		std::fclose(m_file);
	}
}

bool ArchiveHandle::readAt(std::uint64_t offset, void *dst, size_t n)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	if (seek64(m_file, offset) != 0)
	{
		return false;
	}
	if (n == 0)
	{
		return true;
	}
	return std::fread(dst, 1, n, m_file) == n;
}

size_t ArchiveHandle::liveHandles()
{
	Registry &reg = registry();
	std::lock_guard<std::mutex> lock(reg.mutex);
	size_t n = 0;
	for (auto &e : reg.handles)
	{
		if (!e.expired())
		{
			++n;
		}
	}
	return n;
}
