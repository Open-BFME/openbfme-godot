// OpenBFME. GPL-3.0.
// See Common/LooseFileScan.h.

#include "Common/LooseFileScan.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

namespace stdfs = std::filesystem;

bool LooseFileScan::findLooseMaps(const std::string &installRoot, std::vector<LooseFile> &out, std::string *error)
{
	out.clear();
	std::error_code ec;
	stdfs::path root(installRoot);
	if (!stdfs::is_directory(root, ec))
	{
		if (error)
		{
			*error = "not a directory: " + installRoot;
		}
		return false;
	}
	stdfs::recursive_directory_iterator it(root, stdfs::directory_options::skip_permission_denied, ec), end;
	for (; !ec && it != end; it.increment(ec))
	{
		if (!it->is_regular_file(ec))
		{
			continue;
		}
		std::string ext = it->path().extension().string();
		for (char &c : ext)
		{
			c = (char)std::tolower((unsigned char)c);
		}
		if (ext != ".map" && ext != ".scb")
		{
			continue;
		}
		LooseFile f;
		f.relativePath = stdfs::relative(it->path(), root, ec).generic_string();
		f.size = (std::uint64_t)it->file_size(ec);
		out.push_back(std::move(f));
	}
	if (ec)
	{
		if (error)
		{
			*error = "cannot enumerate " + installRoot + ": " + ec.message();
		}
		return false;
	}
	std::sort(out.begin(), out.end(), [](const LooseFile &a, const LooseFile &b) { return a.relativePath < b.relativePath; });
	return true;
}

std::string LooseFileScan::describeFinding(const std::string &install, const LooseFile &file)
{
	return "S-039 contamination (PLAN rule 7): loose " + install + " file " + file.relativePath + " (" + std::to_string(file.size)
		+ " bytes) is not part of retail; it is never read, the archive copy is used"
		+ (file.size == 0 ? " (a 0-byte loose map would hide that archive copy if loose files were mounted)" : "");
}

bool LooseFileScan::scanInstalls(const std::vector<std::pair<std::string, std::string>> &installs, std::vector<LooseFileFinding> &out, std::string *error)
{
	out.clear();
	for (const auto &install : installs)
	{
		std::vector<LooseFile> found;
		if (!findLooseMaps(install.second, found, error))
		{
			out.clear();
			return false;
		}
		for (LooseFile &f : found)
		{
			LooseFileFinding finding;
			finding.install = install.first;
			finding.line = describeFinding(install.first, f);
			finding.file = std::move(f);
			out.push_back(std::move(finding));
		}
	}
	return true;
}
