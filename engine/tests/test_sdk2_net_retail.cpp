// OpenBFME unit tests: lane SDK-2, the SDK sample mod (tools/sdk/samples/income-banner) in real two-peer games over the player's RotWK 2.01.
// GPL-3.0. Needs ROTWK_INSTALL and BFME2_INSTALL (SKIP otherwise); the mods here are the committed sample and a copy the test changes.

#include "doctest.h"
#include "ProcessTestUtil.h"
#include "RetailTestMount.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

#ifdef OPENBFME_PEER_EXE
namespace
{

using processtest::runCommand;

bool haveRetail()
{
	return std::getenv("ROTWK_INSTALL") && std::getenv("BFME2_INSTALL");
}

std::string peerBase()
{
	return std::string("\"") + OPENBFME_PEER_EXE + "\" --rotwk \"" + std::getenv("ROTWK_INSTALL") + "\" --bfme2 \"" + std::getenv("BFME2_INSTALL") + "\"";
}

int port()
{
	const long long t = (long long)std::chrono::steady_clock::now().time_since_epoch().count();
	return 21000 + (int)((t / 1000) % 18000);
}

std::string readFile(const std::string &path)
{
	std::ifstream in(path, std::ios::binary);
	return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

std::string field(const std::string &report, const std::string &key)
{
	std::istringstream in(report);
	std::string line;
	while (std::getline(in, line))
		if (line.rfind(key + " ", 0) == 0)
			return line.substr(key.size() + 1);
	return "";
}

fs::path sample()
{
	// engine/tests/data -> the repository
	return fs::u8path(retailtest::dataDir()).parent_path().parent_path().parent_path() / "tools" / "sdk" / "samples" / "income-banner";
}

struct Pair
{
	int hostRc = -1, joinRc = -1;
	std::string hostReport, joinReport, hostHashes, joinHashes, hostErr, joinErr;
};

// a host and a joiner, each its own process, with their own extras (the mod). Each peer starts its lobby clock after it has loaded the game
// and the mod, so on a loaded machine the host can wait long for the joiner: `lobbySeconds` is generous, also for the pair that is refused
// (60 s: its joiner must arrive to be refused; the host then waits its window out)
Pair runPair(const std::string &tag, int frames, const std::string &common, const std::string &hostExtra, const std::string &joinExtra,
	int lobbySeconds = 120)
{
	const int p = port();
	Pair r;
	const std::string base = "sdk2_" + tag;
	const std::string lobby = " --lobby-timeout " + std::to_string(lobbySeconds) + " ";
	const std::string host = peerBase() + " --frames " + std::to_string(frames) + lobby + common + " --host " + std::to_string(p) + " " + hostExtra +
		" --report " + base + "_h.txt --hashes " + base + "_hh.txt 2> " + base + "_he.txt";
	const std::string join = peerBase() + " --frames " + std::to_string(frames) + lobby + common + " --join 127.0.0.1:" + std::to_string(p) +
		" --name Joiner " + joinExtra + " --report " + base + "_j.txt --hashes " + base + "_jh.txt 2> " + base + "_je.txt";
	std::thread th([&] { r.hostRc = runCommand(host); });
	std::this_thread::sleep_for(std::chrono::milliseconds(300));
	r.joinRc = runCommand(join);
	th.join();
	r.hostReport = readFile(base + "_h.txt");
	r.joinReport = readFile(base + "_j.txt");
	r.hostHashes = readFile(base + "_hh.txt");
	r.joinHashes = readFile(base + "_jh.txt");
	r.hostErr = readFile(base + "_he.txt");
	r.joinErr = readFile(base + "_je.txt");
	for (const char *s : { "_h.txt", "_j.txt", "_hh.txt", "_jh.txt", "_he.txt", "_je.txt" })
		std::remove((base + s).c_str());
	if (r.hostRc != 0 || r.joinRc != 0)
		MESSAGE(tag << ": host exit " << r.hostRc << ", joiner exit " << r.joinRc << "\nhost stderr:\n" << r.hostErr << "joiner stderr:\n" << r.joinErr);
	return r;
}

struct TempCopy
{
	fs::path path;
	explicit TempCopy(const fs::path &from)
	{
		std::mt19937_64 rng((unsigned long long)std::chrono::steady_clock::now().time_since_epoch().count());
		path = fs::temp_directory_path() / ("openbfme-sdk2-net-" + std::to_string(rng())) / "income-banner";
		fs::create_directories(path.parent_path());
		fs::copy(from, path, fs::copy_options::recursive);
	}
	~TempCopy()
	{
		std::error_code ec;
		fs::remove_all(path.parent_path(), ec);
	}
};

const char kCommon[] = "--map \"maps/map mp evendim/map mp evendim.map\" --seed 7 --cash 1000";
const char kSlots[] = "--slot human,FactionMen,0,0 --slot human,FactionMordor,1,1";

} // namespace

TEST_CASE("SDK-2 net retail: two peers with the sample mod's sim script stay in lockstep every frame; it changes the game; other sim scripts are refused")
{
	if (!haveRetail())
	{
		retailtest::printSkip("SDK-2 net retail (the sample mod in two-peer games)");
		return;
	}
	REQUIRE(fs::exists(sample() / "openbfme" / "sdk" / "mod.json"));
	const std::string mod = " --mod \"" + sample().u8string() + "\"";
	const int kFrames = 160; // three paydays (frames 50, 100, 150)
	Pair withMod = runPair("mod", kFrames, std::string(kCommon) + mod, kSlots, "");
	MESSAGE("host:\n" << withMod.hostReport << withMod.hostErr);
	CHECK(withMod.hostRc == 0);
	CHECK(withMod.joinRc == 0);
	CHECK(field(withMod.hostReport, "desyncs") == "0");
	CHECK(field(withMod.joinReport, "desyncs") == "0");
	REQUIRE(!withMod.hostHashes.empty());
	CHECK(withMod.hostHashes == withMod.joinHashes); // every frame

	Pair plain = runPair("plain", kFrames, kCommon, kSlots, "");
	CHECK(plain.hostRc == 0);
	CHECK(field(plain.hostReport, "final_hash") != field(withMod.hostReport, "final_hash")); // the paydays changed the game

	// the joiner's copy of the mod pays 300 instead of 250: another sim script, so another game: refused before it starts
	TempCopy tuned(sample());
	{
		const fs::path script = tuned.path / "openbfme" / "sdk" / "sim" / "income.luau";
		std::string text = readFile(script.u8string());
		const size_t at = text.find("local AMOUNT = 250");
		REQUIRE(at != std::string::npos);
		text.replace(at, 18, "local AMOUNT = 300");
		std::ofstream(script, std::ios::binary) << text;
	}
	Pair refused = runPair("refused", 50, kCommon, std::string(kSlots) + mod, " --mod \"" + tuned.path.u8string() + "\"", 60);
	MESSAGE("joiner refused:\n" << refused.joinErr);
	CHECK(refused.hostRc == 3); // the host's game never filled
	CHECK(refused.joinRc == 3); // the joiner was refused
	CHECK((refused.hostErr + refused.joinErr).find("'sdk sim ") != std::string::npos); // the message quotes the scripts' line (the mod mismatch's path)

	// a presentation-only change is the same game: they play together
	TempCopy look(sample());
	std::ofstream(look.path / "openbfme" / "sdk" / "presentation" / "banner.luau", std::ios::binary) << "function onGameStart() ui.banner('hi') end\n";
	Pair together = runPair("look", 60, kCommon, std::string(kSlots) + mod, " --mod \"" + look.path.u8string() + "\"");
	CHECK(together.hostRc == 0);
	CHECK(together.joinRc == 0);
	CHECK(together.hostHashes == together.joinHashes);
}
#endif
