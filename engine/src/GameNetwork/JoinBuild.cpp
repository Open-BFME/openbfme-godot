// OpenBFME. GPL-3.0.
//
// Lane JOINVER-1: see JoinBuild.h.
#include "GameNetwork/JoinBuild.h"

#include "Common/BuildVersion.h"

#include <array>
#include <cstdint>

namespace JoinBuild
{
namespace
{
struct Parsed
{
	std::array<std::uint32_t, 3> number{};
	bool pre = false;      ///< "-<kind>.<n>": before the plain release of the same number
	std::uint32_t pre_n = 0;
};

bool digits(const std::string &s, std::size_t &at, std::uint32_t &out)
{
	const std::size_t start = at;
	std::uint64_t v = 0;
	while (at < s.size() && s[at] >= '0' && s[at] <= '9' && at - start < 9)
	{
		v = v * 10 + (std::uint64_t)(s[at] - '0');
		++at;
	}
	if (at == start || (at - start > 1 && s[start] == '0') || (at < s.size() && s[at] >= '0' && s[at] <= '9')) return false;
	out = (std::uint32_t)v;
	return true;
}

bool parse(const std::string &v, Parsed &out)
{
	if (v.size() < 6 || v[0] != 'v') return false;
	std::size_t at = 1;
	for (int i = 0; i < 3; ++i)
	{
		if (!digits(v, at, out.number[i])) return false;
		if (i < 2)
		{
			if (at >= v.size() || v[at] != '.') return false;
			++at;
		}
	}
	if (at == v.size()) return true;
	if (v[at] != '-') return false;
	++at;
	bool kind = false;
	for (const char *k : { "alpha.", "beta.", "preview.", "nightly." })
	{
		const std::string prefix(k);
		if (v.compare(at, prefix.size(), prefix) == 0)
		{
			at += prefix.size();
			kind = true;
			break;
		}
	}
	if (!kind || !digits(v, at, out.pre_n) || at != v.size()) return false;
	out.pre = true;
	return true;
}

std::string systemName(const std::string &platform)
{
	if (platform == "windows") return "Windows";
	if (platform == "linux") return "Linux";
	if (platform == "macos") return "macOS";
	return std::string();
}
} // namespace

bool releaseTag(const std::string &version)
{
	Parsed p;
	return parse(version, p);
}

std::string describe(const Build &b)
{
	if (!b.known()) return "an unknown version";
	std::string out;
	if (releaseTag(b.version))
	{
		out = BuildVersion::displayVersion(b.version);
		// a nightly names its day; a release or an alpha gets its build day when told
		if (out.find('(') == std::string::npos && !b.date.empty()) out += " (" + b.date + ")";
	}
	else
	{
		out = "a development build (" + b.version + (b.date.empty() ? std::string() : ", built " + b.date) + ")";
	}
	if (b.official == 1) out += ", official release";
	else if (b.official == 0) out += ", unofficial";
	const std::string system = systemName(b.platform);
	if (!system.empty()) out += ", " + system;
	return out;
}

int compare(const std::string &a, const std::string &b, bool *comparable)
{
	Parsed pa, pb;
	const bool ok = parse(a, pa) && parse(b, pb);
	if (comparable) *comparable = ok;
	if (!ok) return 0;
	for (int i = 0; i < 3; ++i)
	{
		if (pa.number[i] != pb.number[i]) return pa.number[i] < pb.number[i] ? -1 : 1;
	}
	if (pa.pre != pb.pre) return pa.pre ? -1 : 1; // a pre-release comes before the plain release of its number
	if (pa.pre_n != pb.pre_n) return pa.pre_n < pb.pre_n ? -1 : 1;
	return 0;
}

std::string refusalText(const Build &mine, const Build &host)
{
	std::string out = "You can't join this game: it runs a different OpenBFME version.\n";
	out += "Host: " + describe(host) + "\n";
	out += "You: " + describe(mine) + "\n";
	if (!host.known())
	{
		return out + "Both players need the same version: ask the host which one they have, and update through the launcher.";
	}
	bool comparable = false;
	const int order = compare(host.version, mine.version, &comparable);
	if (comparable && order > 0) return out + "The host has a newer version: update OpenBFME through the launcher, then join again.";
	if (comparable && order < 0) return out + "The host is on an older version: the host needs to update through the launcher. Both of you must use the same version.";
	if (comparable && (host.official == 0 || mine.official == 0))
	{
		return out + "One of you runs an unofficial build: both of you must use the official release from the launcher.";
	}
	if (comparable)
	{
		return out + "The versions match, but the game files differ: both of you need the official release and unmodified RotWK 2.01 files.";
	}
	return out + "Both of you must use the same version: install the official release through the launcher.";
}

std::string detailsText(const Build &mine, const std::string &myProfile, const Build &host, const std::string &hostProfile)
{
	auto line = [](const char *who, const Build &b, const std::string &profile) {
		std::string s = std::string(who) + ": " + (b.known() ? b.version : std::string("unknown"));
		if (!b.date.empty()) s += ", built " + b.date;
		if (!b.platform.empty()) s += ", " + b.platform;
		s += b.official == 1 ? ", official" : b.official == 0 ? ", unofficial" : "";
		s += ", profile " + (profile.empty() ? std::string("unknown") : profile);
		return s;
	};
	return line("Host", host, hostProfile) + "\n" + line("You", mine, myProfile);
}
} // namespace JoinBuild
