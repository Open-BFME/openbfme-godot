// OpenBFME. GPL-3.0. See Win32Path.h.

#include "Libraries/file/Win32Path.h"

#include <cctype>
#include <vector>

namespace Win32Path
{

static bool inBand(size_t v);
static size_t peakLength(const std::string &p);
static size_t longestComponent(const std::string &p);
static std::string canonicalizeUnlimited(const std::string &p);

static bool inBandForm(const std::string &p)
{
	return inBand(p.size()) || inBand(peakLength(p)) || inBand(longestComponent(p)) || inBand(canonicalizeUnlimited(p).size());
}

bool inLengthBand(const std::string &path)
{
	return inBandForm(path);
}

bool isSupportedPathForm(const std::string &path)
{
	if (path.compare(0, 2, "\\\\") == 0)
	{
		return false; // UNC and device paths
	}
	for (size_t i = 0; i < path.size(); ++i)
	{
		if ((unsigned char)path[i] >= 0x80 || path[i] == 0)
		{
			return false;
		}
		if (path[i] == '\\' && i + 1 < path.size() && path[i + 1] == '\\')
		{
			return false;
		}
		// only a drive prefix "X:" at index 1 is ported; any other ':' is an embedded drive component
		if (path[i] == ':' && !(i == 1 && std::isalpha((unsigned char)path[0])))
		{
			return false;
		}
	}
	return !inBandForm(path);
}

// The classic shlwapi algorithm (Wine's PathRemoveFileSpecA); matches the Windows 10 calls inside
// the supported domain.
bool removeFileSpec(std::string &s)
{
	const size_t n = s.size();
	size_t i = 0;
	size_t filespec = 0;
	if (i < n && s[i] == '\\')
	{
		++i;
		filespec = i;
	}
	if (i < n && s[i] == '\\')
	{
		++i;
		filespec = i;
	}
	while (i < n)
	{
		if (s[i] == '\\')
		{
			filespec = i;
		}
		else if (s[i] == ':')
		{
			++i;
			filespec = i;
			if (i < n && s[i] == '\\')
			{
				++filespec;
			}
		}
		++i;
	}
	if (filespec < n)
	{
		s.erase(filespec);
		return true;
	}
	return false;
}

static std::string canonicalizeUnlimited(const std::string &p)
{
	if (p.empty())
	{
		return "\\";
	}
	std::string root;
	std::string rest;
	bool driveRelative = false;
	if (p[0] == '\\')
	{
		root = "\\";
		rest = p.substr(1);
	}
	else if (p.size() > 1 && p[1] == ':')
	{
		if (p.size() > 2 && p[2] == '\\')
		{
			root = p.substr(0, 3);
			rest = p.substr(3);
		}
		else
		{
			root = p.substr(0, 2);
			rest = p.substr(2);
			driveRelative = true;
		}
	}
	else
	{
		rest = p;
	}

	std::vector<std::string> comps;
	{
		size_t pos = 0;
		for (;;)
		{
			const size_t next = rest.find('\\', pos);
			comps.push_back(rest.substr(pos, next == std::string::npos ? std::string::npos : next - pos));
			if (next == std::string::npos)
			{
				break;
			}
			pos = next + 1;
		}
	}

	std::vector<std::string> out;
	for (size_t idx = 0; idx < comps.size(); ++idx)
	{
		const std::string &c = comps[idx];
		const bool last = idx + 1 == comps.size();
		const bool firstAfterDrive = driveRelative && idx == 0; // "C:." is not a dot component
		if (c == "." && !firstAfterDrive)
		{
			continue;
		}
		if (c == ".." && !firstAfterDrive)
		{
			if (!out.empty())
			{
				out.pop_back();
				if (out.empty() && (root.empty() || driveRelative))
				{
					root = "\\"; // popping a relative path empty roots it (and drops a bare drive)
					driveRelative = false;
				}
			}
			continue; // a ".." with nothing to pop is dropped
		}
		if (c.empty() && last)
		{
			out.push_back(std::string()); // trailing separator
			continue;
		}
		out.push_back(c);
	}

	std::string res = root;
	for (size_t i = 0; i < out.size(); ++i)
	{
		if (i)
		{
			res += '\\';
		}
		res += out[i];
	}
	if (res.empty() || res.back() != '\\')
	{
		while (!res.empty() && res.back() == '.') // trailing dots of the last component are dropped
		{
			res.pop_back();
		}
	}
	if (res.empty())
	{
		res = "\\";
	}
	if (res.size() == 2 && res[1] == ':')
	{
		res += '\\';
	}
	return res;
}

// Length of the output while the components are copied, counting components that a later ".."
// removes (the real call fails on the peak, not only on the final length).
static size_t peakLength(const std::string &p)
{
	std::string rest;
	size_t rootLen = 0;
	if (!p.empty() && p[0] == '\\')
	{
		rootLen = 1;
		rest = p.substr(1);
	}
	else if (p.size() > 1 && p[1] == ':')
	{
		rootLen = (p.size() > 2 && p[2] == '\\') ? 3 : 2;
		rest = p.substr(rootLen);
	}
	else
	{
		rest = p;
	}
	std::vector<size_t> out; // component lengths
	size_t sum = 0;
	size_t peak = rootLen;
	size_t pos = 0;
	for (;;)
	{
		const size_t next = rest.find('\\', pos);
		const std::string c = rest.substr(pos, next == std::string::npos ? std::string::npos : next - pos);
		const bool last = next == std::string::npos;
		if (c == ".")
		{
		}
		else if (c == "..")
		{
			if (!out.empty())
			{
				sum -= out.back();
				out.pop_back();
			}
		}
		else if (!(c.empty() && last))
		{
			out.push_back(c.size());
			sum += c.size();
			const size_t cur = rootLen + sum + (out.size() - 1);
			peak = cur > peak ? cur : peak;
		}
		if (last)
		{
			break;
		}
		pos = next + 1;
	}
	return peak;
}

static size_t longestComponent(const std::string &p)
{
	size_t longest = 0;
	size_t pos = 0;
	for (;;)
	{
		const size_t next = p.find('\\', pos);
		const size_t len = (next == std::string::npos ? p.size() : next) - pos;
		longest = len > longest ? len : longest;
		if (next == std::string::npos)
		{
			return longest;
		}
		pos = next + 1;
	}
}

static bool inBand(size_t v)
{
	return v >= 250 && v <= 262;
}

bool canonicalize(const std::string &p, std::string &out)
{
	const std::string res = canonicalizeUnlimited(p);
	if (longestComponent(p) >= 257 || peakLength(p) >= 260 || res.size() >= 260)
	{
		out.clear();
		return false;
	}
	out = res;
	return true;
}

bool append(std::string &path, const std::string &moreIn)
{
	std::string more = moreIn;
	if (!more.empty() && more[0] == '\\')
	{
		more.erase(0, 1); // PathAppend removes one leading backslash of the appended part
	}
	if (path.size() >= 260 || moreIn.size() >= 260)
	{
		path.clear();
		return false;
	}
	std::string joined;
	if (more.size() > 1 && more[1] == ':')
	{
		joined = more; // a drive-qualified part replaces the directory
	}
	else if (path.empty())
	{
		joined = more;
	}
	else if (more.empty())
	{
		joined = path;
	}
	else
	{
		joined = path + (path.back() == '\\' ? "" : "\\") + more;
	}
	std::string out;
	if (!canonicalize(joined, out))
	{
		path.clear();
		return false;
	}
	path = out;
	return true;
}

} // namespace Win32Path
