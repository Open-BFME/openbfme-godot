// OpenBFME unit tests. GPL-3.0.
// lane PROFILE-3: the simdata coverage check. Under profile 3 a mod's names and content hashes no longer count (ProfileIdentity.h), so the simdata line
// is the only thing that tells two mods' game data apart: every file the game asks the mounted files for has to be one the digest covers
// (SimulationData::covers) or one whose reason not to count is written here. A retail game and a mod game (the SDK sample, a sim script) run in two
// headless peers each; the host's --read-trace lists every path asked for after the profile identity was computed: the object world's load, the
// map, the AI, every frame.

#include "RetailTestMount.h"
#include "Common/MiniJson.h"
#include "GameNetwork/ProfileIdentity.h"

#include "doctest.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifndef _WIN32
#include <sys/wait.h>
#endif

namespace fs = std::filesystem;

namespace
{
#ifdef OPENBFME_PEER_EXE
std::string readText(const std::string &path)
{
	std::ifstream in(path, std::ios::binary);
	return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

int runCommand(const std::string &cmd)
{
#ifdef _WIN32
	return std::system(("\"" + cmd + "\"").c_str());
#else
	const int rc = std::system(cmd.c_str());
	return WIFEXITED(rc) ? WEXITSTATUS(rc) : -1;
#endif
}

std::string peerBase()
{
	return std::string("\"") + OPENBFME_PEER_EXE + "\" --rotwk \"" + std::getenv("ROTWK_INSTALL") + "\" --bfme2 \"" + std::getenv("BFME2_INSTALL") + "\"";
}

int port()
{
	const long long t = (long long)std::chrono::steady_clock::now().time_since_epoch().count();
	return 23000 + (int)((t / 1000) % 16000);
}

fs::path sample()
{
	// engine/tests/data -> the repository
	return fs::u8path(retailtest::dataDir()).parent_path().parent_path().parent_path() / "tools" / "sdk" / "samples" / "income-banner";
}

// a host (traced) and a joiner, `frames` frames; the host's trace lines, or empty with a MESSAGE when a peer failed
std::vector<std::string> tracedGame(const std::string &tag, int frames, const std::string &extra)
{
	const int p = port();
	const std::string base = "profile3_" + tag;
	const std::string common = " --frames " + std::to_string(frames) + " --lobby-timeout 300 --map \"maps/map mp evendim/map mp evendim.map\" --seed 11 "
		"--cash 2000 --script " + extra;
	const std::string host = peerBase() + common + " --host " + std::to_string(p)
		+ " --slot human,FactionMen,0,0 --slot human,FactionMordor,1,1 --slot hard,FactionDwarves,2,2 --slot hard,FactionIsengard,3,3 --read-trace "
		+ base + "_trace.txt --report " + base + "_h.txt 2> " + base + "_he.txt";
	const std::string join = peerBase() + common + " --join 127.0.0.1:" + std::to_string(p) + " --name Joiner --report " + base + "_j.txt 2> " + base
		+ "_je.txt";
	int hostRc = -1;
	std::thread th([&] { hostRc = runCommand(host); });
	std::this_thread::sleep_for(std::chrono::milliseconds(300));
	const int joinRc = runCommand(join);
	th.join();
	std::vector<std::string> lines;
	std::istringstream in(readText(base + "_trace.txt"));
	for (std::string l; std::getline(in, l);)
	{
		lines.push_back(l);
	}
	if (hostRc != 0 || joinRc != 0 || lines.empty())
	{
		MESSAGE(tag << ": host exit " << hostRc << ", joiner exit " << joinRc << ", " << lines.size() << " trace lines\nhost stderr:\n" << readText(base + "_he.txt")
					<< "joiner stderr:\n" << readText(base + "_je.txt"));
		lines.clear();
	}
	for (const char *s : { "_trace.txt", "_h.txt", "_he.txt", "_j.txt", "_je.txt" })
	{
		std::remove((base + s).c_str());
	}
	return lines;
}
#endif

// the paths a game asks for that the simdata digest does not cover, each with the reason it need not: "" when `line` is covered or reasoned
std::string uncovered(const std::string &line)
{
	const size_t sp = line.find(' ');
	const std::string kind = line.substr(0, sp), path = line.substr(sp + 1);
	if (kind == "list")
	{
		// a listing's directory: the files below it that the game then reads are traced on their own; the listing decides only which exist
		for (const char *dir : { "data\\ini\\", "data\\scripts\\", "maps\\", "art\\w3d\\", "bases\\", "libraries\\" })
		{
			if (path.rfind(dir, 0) == 0)
			{
				return "";
			}
		}
		return line;
	}
	if (SimulationData::covers(path))
	{
		return "";
	}
	return line;
}
} // namespace

#ifdef OPENBFME_PEER_EXE
namespace
{
// the trace's paths outside the simdata digest; MESSAGE the summary line the report quotes
size_t checkTrace(const std::string &name, const std::vector<std::string> &trace)
{
	std::string missing;
	size_t n = 0, reads = 0;
	for (const std::string &l : trace)
	{
		reads += l.rfind("read ", 0) == 0 ? 1 : 0;
		const std::string u = uncovered(l);
		if (!u.empty())
		{
			missing += "  " + u + "\n";
			++n;
		}
	}
	MESSAGE(name << ": " << trace.size() << " paths asked for (" << reads << " read), " << n << " not covered");
	CHECK_MESSAGE(n == 0, name << ": paths outside the simdata digest:\n" << missing);
	return n;
}

bool traced(const std::vector<std::string> &trace, const std::string &line)
{
	return std::find(trace.begin(), trace.end(), line) != trace.end();
}
} // namespace

TEST_CASE("profile3 retail: every file a retail game and a mod game ask the mounted files for is one the simdata digest covers")
{
	if (!std::getenv("ROTWK_INSTALL") || !std::getenv("BFME2_INSTALL"))
	{
		retailtest::printSkip("profile3 retail (the simdata coverage check)");
		return;
	}
	REQUIRE(fs::exists(sample() / "openbfme" / "sdk" / "mod.json"));
	for (const auto &game : { std::make_pair(std::string("retail"), std::string()),
			 std::make_pair(std::string("mod"), " --mod \"" + sample().u8string() + "\"") })
	{
		const std::vector<std::string> trace = tracedGame(game.first, 900, game.second);
		REQUIRE_MESSAGE(!trace.empty(), game.first);
		checkTrace(game.first, trace);
		CHECK(traced(trace, "read bases\\fortress_mordor\\fortress_mordor.bse")); // the starting fortress unpacked
	}
}

TEST_CASE("profile3 mod corpus: Edain, Edain Unchained and Patch 2.02 games past their starting castle's unpack read nothing outside the simdata digest")
{
	// owner objective retail-mods-100: profile 3 never lets two players with different gameplay mod files into one game. The corpus's All In One
	// packages (tools/mod/aio_package.py) mounted as the launcher does: their stack's install/ folders, highest priority first, and the engine
	// edits' md5s (MODS-3)
	const char *corpus = std::getenv("OPENBFME_MOD_CORPUS");
	if (!corpus || !*corpus || !std::getenv("ROTWK_INSTALL") || !std::getenv("BFME2_INSTALL"))
	{
		retailtest::printSkip("profile3 mod corpus (OPENBFME_MOD_CORPUS unset)");
		return;
	}
	const fs::path c = fs::u8path(corpus);
	// the mount from the package's record, as test_mods1_retail and the launcher build it: every stack member's install/ folder that exists, in
	// the record's order (a member listed without parts still has its install/ folder from its own package: Edain Unchained's Edain_assets),
	// and the engine edits' md5s. Patch 2.02 is a dependency-only package (no stack): its own install/ folder and the exe edit the packages
	// that stack it name ("Patch 2.02: lotrbfme2ep1.exe md5 ...")
	auto mountArgs = [&](const char *package, std::string &args, std::string &error) {
		std::ifstream in(c / package / "openbfme-aio.json", std::ios::binary);
		const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		JsonValue root;
		if (text.empty() || !JsonValue::parse(text, root, &error))
		{
			error = std::string(package) + ": no readable openbfme-aio.json " + error;
			return false;
		}
		const JsonValue *stack = root.get("stack");
		if (!stack || !stack->isArray())
		{
			args += " --mod-install \"" + (c / package / "install").u8string() + "\" --mod-engine 7a4068f87ea5c3b98e03ae4e83e2b806";
			return fs::is_directory(c / package / "install");
		}
		for (const JsonValue &member : stack->array)
		{
			const fs::path install = c / fs::u8path(member.get("folder")->string) / "install";
			if (fs::is_directory(install))
			{
				args += " --mod-install \"" + install.u8string() + "\"";
			}
		}
		if (const JsonValue *edits = root.get("engine_edits"))
		{
			for (const JsonValue &e : edits->array)
			{
				const size_t at = e.string.rfind(" md5 "); // "<package>: <file> md5 <hex>" (tools/mod/aio_package.py)
				if (at != std::string::npos)
				{
					args += " --mod-engine " + e.string.substr(at + 5);
				}
			}
		}
		return true;
	};
	struct Game
	{
		const char *name, *package;
		const char *castle; // a model of a starting base the game unpacks at its start: the trace is past the unpack
	};
	const Game games[] = {
		{ "Edain", "Edain", "read art\\w3d\\mb\\mbfzitadella.w3d" },
		{ "Edain Unchained", "Edain_Unchained", "read art\\w3d\\mb\\mbfzitadella.w3d" },
		{ "Patch 2.02", "Patch_2.02", "read art\\w3d\\mb\\mbfortress.w3d" },
	};
	for (const Game &g : games)
	{
		const std::string name = g.name; // doctest prints a const char * as a pointer
		std::string args, error;
		REQUIRE_MESSAGE(mountArgs(g.package, args, error), name << ": " << error);
		MESSAGE(name << ":" << args);
		const std::vector<std::string> trace = tracedGame(std::string("corpus_") + g.package, 1800, args);
		REQUIRE_MESSAGE(!trace.empty(), name);
		checkTrace(g.name, trace);
		CHECK_MESSAGE(traced(trace, g.castle), name << ": the starting base did not unpack (" << std::string(g.castle) << " never read)");
	}
}
#endif
