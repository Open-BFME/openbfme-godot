// OpenBFME unit tests: the portable shlwapi path calls behind #include resolution. GPL-3.0.
// On Windows the REAL PathRemoveFileSpecA / PathAppendA / PathCanonicalizeA are the oracle, over
// random paths; elsewhere only the pinned cases run (and the Windows-only part says so loudly).

#include "doctest.h"

#include "Libraries/file/TextFile.h"
#include "Libraries/file/Win32Path.h"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>

#ifdef _WIN32
#include <windows.h>
#include <shlwapi.h>
#endif

namespace
{
std::string canon(const std::string &p)
{
	std::string out;
	Win32Path::canonicalize(p, out);
	return out;
}
std::string appended(std::string d, const std::string &m)
{
	Win32Path::append(d, m);
	return d;
}
std::string removed(std::string p)
{
	Win32Path::removeFileSpec(p);
	return p;
}
}

// expected values read off the real shlwapi calls on Windows 10
TEST_CASE("Win32Path pinned cases (values from the real shlwapi)")
{
	CHECK(canon("") == "\\");
	CHECK(canon(".") == "\\");
	CHECK(canon("a\\..\\..\\x.inc") == "\\x.inc");
	CHECK(canon("..\\a") == "a");
	CHECK(canon("a\\.\\b") == "a\\b");
	CHECK(canon("C:\\a\\..\\..\\b") == "C:\\b");
	CHECK(canon("C:a\\..\\b") == "\\b");
	CHECK(canon("C:") == "C:\\");
	CHECK(canon("a.") == "a");
	CHECK(canon("a/b\\c") == "a/b\\c"); // '/' is not a separator
	CHECK(canon("a\\b\\") == "a\\b\\");
	CHECK(appended("a", "b") == "a\\b");
	CHECK(appended("a\\b", "..\\..\\x.inc") == "\\x.inc");
	CHECK(appended("a", "C:\\x") == "C:\\x");
	CHECK(appended("a", "\\x") == "a\\x");
	CHECK(appended("C:\\a", "\\x") == "C:\\a\\x");
	CHECK(appended("", "") == "\\");
	CHECK(appended("a", "b/c") == "a\\b/c");
	CHECK(removed("a\\b.ini") == "a");
	CHECK(removed("a") == "");
	CHECK(removed("\\a") == "\\");
	CHECK(removed("C:\\a") == "C:\\");
	CHECK(removed("C:a") == "C:");
	CHECK(removed("a\\") == "a");
	CHECK(removed("a/b") == "");
	std::string changed = "a\\b";
	CHECK(Win32Path::removeFileSpec(changed));
	std::string root = "\\";
	CHECK_FALSE(Win32Path::removeFileSpec(root));
}

// Sol review round 3: past MAX_PATH the real PathAppendA returns false and CLEARS its buffer; retail
// ignores the result (RW 0xA15C97) and canonicalises the empty string to "\"
TEST_CASE("Win32Path reproduces the MAX_PATH failure state of PathAppendA / PathCanonicalizeA")
{
	std::string dir = "Data\\INI\\" + std::string(20, 'd');
	std::string path = dir;
	CHECK_FALSE(Win32Path::append(path, std::string(230, 'x') + ".inc"));
	CHECK(path.empty());
	// the whole retail sequence for the include of Sol's case: "\"
	std::string resolved;
	REQUIRE(TextFile::resolveIncludePath(dir + "\\parent.ini", std::string(230, 'x') + ".inc", resolved));
	CHECK(resolved == "\\");
	// just under the limit it succeeds
	path = dir;
	CHECK(Win32Path::append(path, std::string(100, 'x') + ".inc"));
	CHECK(path == dir + "\\" + std::string(100, 'x') + ".inc");
	// a component of 257 or more fails canonicalisation; retail then keeps the raw buffer
	std::string out = "stale";
	CHECK_FALSE(Win32Path::canonicalize(std::string(300, 'y'), out));
	CHECK(out.empty());
	// a long run that ".." later removes still fails on the peak
	out = "stale";
	CHECK_FALSE(Win32Path::canonicalize(std::string(150, 'a') + "\\" + std::string(150, 'b') + "\\..\\..\\c", out));
	// the boundary band is refused rather than guessed
	CHECK_FALSE(Win32Path::isSupportedPathForm(std::string(255, 'z')));
	CHECK(Win32Path::isSupportedPathForm(std::string(300, 'z')));
}

// Sol review round 4, P1: ".\C:\..\x.inc" from "parent.ini" passed the domain check; the real calls give
// "C:\x.inc" (PathAppend/PathCanonicalize treat the inner C: as a drive) while the port gave "\x.inc".
// The port refuses embedded drive components instead (ACCEPTANCE STOP S-011).
TEST_CASE("Win32Path refuses embedded drive components (S-011)")
{
	std::string out;
	CHECK_FALSE(Win32Path::isSupportedPathForm(".\\C:\\..\\x.inc"));
	CHECK_FALSE(TextFile::resolveIncludePath("parent.ini", ".\\C:\\..\\x.inc", out));
	CHECK_FALSE(TextFile::resolveIncludePath("a\\parent.ini", "..\\D:\\..\\x.inc", out));
	CHECK_FALSE(Win32Path::isSupportedPathForm("a\\C:"));
	CHECK_FALSE(Win32Path::isSupportedPathForm("a:b:c"));
	CHECK_FALSE(Win32Path::isSupportedPathForm(":x"));
	CHECK_FALSE(Win32Path::isSupportedPathForm("1:\\x")); // a digit is not a drive letter
	CHECK_FALSE(TextFile::resolveIncludePath("C:\\a\\b.ini", "x\\C:\\y.inc", out));
	// the genuine drive prefixes stay supported
	CHECK(Win32Path::isSupportedPathForm("C:\\x\\y.inc"));
	CHECK(Win32Path::isSupportedPathForm("D:x.inc"));
	CHECK(Win32Path::isSupportedPathForm("e:"));
	REQUIRE(TextFile::resolveIncludePath("a\\b.ini", "C:\\x\\y.inc", out));
	CHECK(out == "C:\\x\\y.inc");
}

TEST_CASE("Win32Path refuses forms outside the ported domain")
{
	CHECK_FALSE(Win32Path::isSupportedPathForm("\\\\srv\\share"));
	CHECK_FALSE(Win32Path::isSupportedPathForm("\\\\?\\C:\\x"));
	CHECK_FALSE(Win32Path::isSupportedPathForm("a\\\\b"));
	CHECK_FALSE(Win32Path::isSupportedPathForm("a\xE9"));
	CHECK(Win32Path::isSupportedPathForm("Data\\INI\\Campaigns\\RiskCampaign.ini"));
	CHECK(Win32Path::isSupportedPathForm("..\\..\\x/y.inc"));
}

#ifdef _WIN32
namespace
{
std::string genPath(std::mt19937 &rng)
{
	static const std::string longSeg[] = { std::string(100, 'x'), std::string(128, 'y'), std::string(200, 'z'), std::string(230, 'w'), std::string(256, 'v'), std::string(257, 'u'), std::string(20, 'd') };
	static const char *segs[] = { "a", "bb", ".", "..", "c.d", "x y", ".a", "..a", "x/y", "/", "./", "a/..", "a.", "...", ".. ", "WOTR.inc", "Common",
		// shlwapi-inert special characters (Sol round 4: the domain check must be audited against everything the generator can build)
		"a*b", "a?b", "q\"r", "a<b", "x|y", "a\tb", "a%b", "~1", "a;b", "a,b", "a=b", "a+b", "a[b]", "\x01",
		// embedded drive components: the port must REFUSE these (S-011), never answer differently from shlwapi
		"C:", "c:\\..", "a:b", ":", "C:x" };
	static const char *pre[] = { "", "", "", "C:", "C:\\", "\\", "D:x\\", "e:" };
	std::string s = pre[rng() % (sizeof(pre) / sizeof(pre[0]))];
	const int k = (int)(rng() % 7);
	for (int j = 0; j < k; ++j)
	{
		if (rng() % 5 == 0)
		{
			s += longSeg[rng() % (sizeof(longSeg) / sizeof(longSeg[0]))];
		}
		else
		{
			s += segs[rng() % (sizeof(segs) / sizeof(segs[0]))];
		}
		if (j < k - 1 || rng() % 4 == 0)
		{
			s += '\\';
		}
	}
	return s;
}

typedef std::pair<bool, std::string> Result;

Result realCanon(const std::string &p)
{
	char buf[1040] = {};
	const bool ok = PathCanonicalizeA(buf, p.c_str()) != FALSE;
	return { ok, buf };
}
Result realAppend(const std::string &d, const std::string &m)
{
	char buf[1040] = {};
	std::snprintf(buf, sizeof(buf), "%s", d.c_str());
	const bool ok = PathAppendA(buf, m.c_str()) != FALSE;
	return { ok, buf };
}
std::string realRemove(const std::string &p)
{
	char buf[1040] = {};
	std::snprintf(buf, sizeof(buf), "%s", p.c_str());
	PathRemoveFileSpecA(buf);
	return buf;
}
// Wine (lane WIN-1 runs this executable under it) implements shlwapi itself: its PathCanonicalizeA / PathAppendA are not the oracle (a drive-relative "e:" path
// canonicalizes differently there), so the random comparison only runs on a real Windows. ntdll exports wine_get_version only under Wine.
bool runningUnderWine()
{
	const HMODULE ntdll = GetModuleHandleA("ntdll.dll");
	return ntdll && GetProcAddress(ntdll, "wine_get_version") != nullptr;
}
}

TEST_CASE("Win32Path matches the real shlwapi calls over random paths, long ones included")
{
	if (runningUnderWine())
	{
		std::printf("SKIP: the shlwapi oracle for Win32Path needs Microsoft's shlwapi; this process runs under Wine, whose shlwapi is its own implementation\n");
		return;
	}
	std::mt19937 rng(20260930);
	size_t bad = 0, compared = 0, skippedBand = 0, failures = 0, colonRefused = 0;
	const int N = 40000;
	for (int i = 0; i < N; ++i)
	{
		std::string a = genPath(rng);
		const std::string b = genPath(rng);
		if (a.size() > 1000 || b.size() > 1000)
		{
			continue;
		}
		// retail copies the including file with strncpy(.., 260)
		if (a.size() > 260)
		{
			a.resize(260);
		}
		if (!Win32Path::isSupportedPathForm(a) || !Win32Path::isSupportedPathForm(b))
		{
			// count the refusals caused by an embedded drive component (a ':' that is not the "X:" prefix)
			const auto embeddedColon = [](const std::string &s) {
				for (size_t k = 0; k < s.size(); ++k)
				{
					if (s[k] == ':' && !(k == 1 && std::isalpha((unsigned char)s[0])))
					{
						return true;
					}
				}
				return false;
			};
			colonRefused += (embeddedColon(a) || embeddedColon(b)) ? 1 : 0;
			++skippedBand;
			continue;
		}
		// the whole retail include sequence, with the real calls; the MAX_PATH failures included
		std::string tmp = a;
		PathRemoveFileSpecA(&tmp[0]);
		tmp.resize(std::strlen(tmp.c_str()));
		const std::string joinedForBand = tmp + "\\" + ((!b.empty() && b[0] == '\\') ? b.substr(1) : b);
		if (!Win32Path::isSupportedPathForm(tmp) || Win32Path::inLengthBand(joinedForBand))
		{
			++skippedBand;
			continue;
		}
		++compared;
		const Result realA = realAppend(tmp, b);
		std::string full = realA.second;
		for (char &ch : full)
		{
			if (ch == '/')
			{
				ch = '\\';
			}
		}
		const Result realC = realCanon(full);
		const std::string expected = realC.first ? realC.second : full;
		failures += realA.first ? 0 : 1;
		std::string got;
		REQUIRE(TextFile::resolveIncludePath(a, b, got));
		if (got != expected && ++bad <= 10)
		{
			MESSAGE("MISMATCH include sequence (" << a.size() << " chars | " << b.size() << " chars): real '" << expected.substr(0, 40) << "' (" << expected.size() << ") port '"
				<< got.substr(0, 40) << "' (" << got.size() << ")");
		}
		// each call on its own
		if (a.size() < 1000)
		{
			const Result rc = realCanon(a);
			std::string pc;
			const bool pok = Win32Path::canonicalize(a, pc);
			if ((rc.first != pok || rc.second != pc) && ++bad <= 10)
			{
				MESSAGE("MISMATCH canonicalize (" << a.size() << " chars)");
			}
			std::string pa = tmp;
			const bool aok = Win32Path::append(pa, b);
			if ((realA.first != aok || realA.second != pa) && ++bad <= 10)
			{
				MESSAGE("MISMATCH append (" << tmp.size() << " | " << b.size() << " chars): real ok=" << realA.first << " port ok=" << aok);
			}
			std::string rm = a;
			Win32Path::removeFileSpec(rm);
			if (realRemove(a) != rm && ++bad <= 10)
			{
				MESSAGE("MISMATCH removeFileSpec (" << a.size() << " chars)");
			}
		}
	}
	MESSAGE("shlwapi oracle: " << compared << " path pairs compared (" << failures << " with a MAX_PATH PathAppend failure), " << skippedBand << " outside the ported domain");
	CHECK(bad == 0);
	CHECK(failures > 100); // the generator really reaches the MAX_PATH failure state
	CHECK(colonRefused > 1000); // and really builds embedded drive components, which the port refuses
}
#else
TEST_CASE("Win32Path shlwapi comparison")
{
	std::printf("SKIP: the shlwapi oracle for Win32Path only runs on Windows\n");
}
#endif
