// OpenBFME unit tests. GPL-3.0.
// Lane MP-2 on the retail game, with real peer processes (tools: the openbfme_peer executable, UDP on localhost): a player whose process stops mid-game
// (no leave: a crash) is dropped by the disconnect path - after its timeout, or at once by the remaining players' Kick votes - and the survivors play on
// in lockstep with equal hashes at every frame; the leaver's assets go to its living ally (MSG_SELF_DESTRUCT { TRUE }, RW 0x77CA3D). SKIP loudly without
// ROTWK_INSTALL / BFME2_INSTALL.

#include "doctest.h"

#include "StartTestUtil.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifndef _WIN32
#include <sys/wait.h>
#include <unistd.h>
#endif

#ifdef OPENBFME_PEER_EXE
namespace
{
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
	{
		if (line.rfind(key + " ", 0) == 0)
		{
			return line.substr(key.size() + 1);
		}
	}
	return "";
}

int runCommand(const std::string &cmdIn)
{
	std::string cmd = cmdIn;
#ifdef _WIN32
	cmd = "\"" + cmd + "\"";
	return std::system(cmd.c_str());
#else
	const int rc = std::system(cmd.c_str());
	return WIFEXITED(rc) ? WEXITSTATUS(rc) : -1;
#endif
}

std::string peerBase()
{
	return std::string("\"") + OPENBFME_PEER_EXE + "\" --rotwk \"" + std::getenv("ROTWK_INSTALL") + "\" --bfme2 \"" + std::getenv("BFME2_INSTALL") + "\"";
}

int testPort(int salt)
{
#ifdef _WIN32
	const int pid = (int)std::chrono::steady_clock::now().time_since_epoch().count();
#else
	const int pid = (int)getpid();
#endif
	return 20000 + ((pid * 7 + salt * 977) % 20000 + 20000) % 20000;
}

struct Run
{
	std::vector<int> rcs;
	std::vector<std::string> reports, hashes;
	double seconds = 0;
};

// a host and joiners, each its own process; extras[i] goes to peer i's command line (the host first)
Run runPeers(const std::string &tag, int salt, const std::string &common, const std::vector<std::string> &extras)
{
	const int port = testPort(salt);
	Run r;
	const size_t n = extras.size();
	r.rcs.assign(n, -1);
	std::vector<std::string> reportFiles, hashFiles, commands;
	for (size_t i = 0; i < n; ++i)
	{
		reportFiles.push_back("mp2_" + tag + "_" + std::to_string(i) + "_report.txt");
		hashFiles.push_back("mp2_" + tag + "_" + std::to_string(i) + "_hashes.txt");
		const std::string role = i == 0 ? " --host " + std::to_string(port) : " --join 127.0.0.1:" + std::to_string(port) + " --name Joiner" + std::to_string(i);
		commands.push_back(peerBase() + " " + common + role + " " + extras[i] + " --report " + reportFiles[i] + " --hashes " + hashFiles[i]);
	}
	const auto t0 = std::chrono::steady_clock::now();
	std::vector<std::thread> threads;
	for (size_t i = 0; i < n; ++i)
	{
		threads.emplace_back([&, i] { r.rcs[i] = runCommand(commands[i]); });
		std::this_thread::sleep_for(std::chrono::milliseconds(300));
	}
	for (std::thread &t : threads)
	{
		t.join();
	}
	r.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
	for (size_t i = 0; i < n; ++i)
	{
		r.reports.push_back(readFile(reportFiles[i]));
		r.hashes.push_back(readFile(hashFiles[i]));
		std::remove(reportFiles[i].c_str());
		std::remove(hashFiles[i].c_str());
	}
	return r;
}

std::string head(const std::string &report)
{
	return report.substr(0, report.find("stop [S-720]"));
}

// the hash lines up to (not including) frame `f`
std::string hashesBefore(const std::string &hashes, int f)
{
	std::istringstream in(hashes);
	std::string line, out;
	while (std::getline(in, line))
	{
		if (std::atoi(line.c_str()) >= f)
		{
			break;
		}
		out += line + "\n";
	}
	return out;
}
} // namespace

TEST_CASE("mp2 net retail: a player whose process stops at frame 300 is dropped after its timeout; the two survivors play on to 800 in lockstep and its "
		  "ally inherits its army")
{
	OPENBFME_REQUIRE_START(s);
	const int kFrames = 800;
	const std::string common = "--map \"maps/map mp fall back 4p/map mp fall back 4p.map\" --seed 41 --script --crc-interval 100 --run-ahead 2 --drop 30 "
							   "--jitter 10 --disconnect-ms 3000 --player-timeout-ms 8000 --frames " + std::to_string(kFrames);
	// the joiners take the human slots 1 and 2 in the order they join (they load at their own pace); either way the leaver has an ally: slot 1 (Mordor,
	// team 1) the Medium AI, slot 2 (Elves, team 0) the host
	const std::string slots = "--slot human,FactionMen,0,0 --slot human,FactionMordor,1,1 --slot human,FactionElves,2,0 --slot medium,FactionWild,3,1";
	Run r = runPeers("timeout", 1, common, { slots, "", "--vanish-at 300" });
	for (size_t i = 0; i < r.reports.size(); ++i)
	{
		MESSAGE("peer " << i << " rc " << r.rcs[i] << ":\n" << head(r.reports[i]));
	}
	MESSAGE("three peers: " << r.seconds << " s");
	REQUIRE(r.rcs.size() == 3);
	CHECK(r.rcs[2] == 0);
	CHECK(field(r.reports[2], "vanished_at") == "300");
	const std::string gone = field(r.reports[2], "slot");
	REQUIRE((gone == "1" || gone == "2"));
	for (size_t i = 0; i < 2; ++i)
	{
		INFO("survivor " << i);
		CHECK(r.rcs[i] == 0);
		CHECK(field(r.reports[i], "frames") == std::to_string(kFrames));
		CHECK(field(r.reports[i], "desyncs") == "0");
		CHECK(field(r.reports[i], "crc_checks_passed") == "7");
		CHECK(field(r.reports[i], "disconnected_slots") == gone);
		CHECK(field(r.reports[i], "disconnect_quit") == "none");
		CHECK(r.reports[i].find("network_error") == std::string::npos);
		// the leaver's player has nothing left and is defeated; its ally got its army
		CHECK(field(r.reports[i], "self_destruct").rfind("executed 1 transfers 1 kills 1", 0) == 0);
		CHECK(field(r.reports[i], "slot_objects " + gone) == "0 defeated 1");
		CHECK(r.reports[i].find("automatic vote for slot " + gone) != std::string::npos);
	}
	CHECK(field(r.reports[0], "dropped_by_me") == gone); // the host is the next packet router
	CHECK(field(r.reports[1], "dropped_by_me") == "none");
	CHECK(field(r.reports[0], "final_hash") == field(r.reports[1], "final_hash"));
	CHECK(!r.hashes[0].empty());
	CHECK(r.hashes[0] == r.hashes[1]);
	// before it stopped, the leaver's hashes were the survivors'
	CHECK(hashesBefore(r.hashes[2], 300) == hashesBefore(r.hashes[0], 300));
}

TEST_CASE("mp2 net retail: the remaining player presses Kick: a vanished opponent is dropped long before the 60 s timeout and its ally, a computer "
		  "player, takes its army")
{
	OPENBFME_REQUIRE_START(s);
	const int kFrames = 600;
	const std::string common = "--map \"maps/map mp evendim/map mp evendim.map\" --seed 17 --script --crc-interval 100 --run-ahead 2 --disconnect-ms 2000 "
							   "--frames " + std::to_string(kFrames);
	// slot 1 (Mordor, team 1) has a Hard AI ally (slot 2, team 1); the host keeps GameData's 60 s player timeout and presses Kick
	const std::string slots = "--slot human,FactionMen,0,0 --slot human,FactionMordor,1,1 --slot hard,FactionIsengard,2,1";
	Run r = runPeers("kick", 2, common, { slots + " --kick", "--vanish-at 200" });
	for (size_t i = 0; i < r.reports.size(); ++i)
	{
		MESSAGE("peer " << i << " rc " << r.rcs[i] << ":\n" << head(r.reports[i]));
	}
	CHECK(r.rcs[0] == 0);
	CHECK(r.rcs[1] == 0);
	CHECK(field(r.reports[0], "frames") == std::to_string(kFrames));
	CHECK(field(r.reports[0], "disconnected_slots") == "1");
	CHECK(field(r.reports[0], "dropped_by_me") == "1");
	CHECK(field(r.reports[0], "self_destruct").rfind("executed 1 transfers 1 kills 1", 0) == 0);
	CHECK(field(r.reports[0], "slot_objects 1") == "0 defeated 1");
	CHECK(r.reports[0].find("vote of slot 0 to drop slot 1") != std::string::npos);
	CHECK(r.reports[0].find("automatic vote") == std::string::npos); // Kick, not the timeout
	CHECK(r.reports[0].find("network_error") == std::string::npos);
	CHECK(field(r.reports[0], "desyncs") == "0");
}
#endif

#ifdef OPENBFME_PEER_EXE
TEST_CASE("mp2 net retail: a desync writes RW 0x6290C7's dump on both peers: DESYNC-Frame<n>-<exe>-<player>.txt with the frame, both halves' breakdown "
		  "and every object hash, and a copy of the replay")
{
	OPENBFME_REQUIRE_START(s);
	const std::string dir = "mp2_desync_dir";
#ifdef _WIN32
	runCommand("mkdir " + dir);
#else
	runCommand("mkdir -p " + dir);
#endif
	const std::string common = "--map \"maps/map mp evendim/map mp evendim.map\" --seed 5 --script --crc-interval 100 --run-ahead 2 --frames 300 --desync-dir " + dir;
	Run r = runPeers("desync", 3, common, { "--slot human,FactionMen,0,0 --slot human,FactionMordor,1,1 --record " + dir + "/host.replay", "--inject-divergence 150" });
	for (size_t i = 0; i < r.reports.size(); ++i)
	{
		MESSAGE("peer " << i << " rc " << r.rcs[i] << ":\n" << head(r.reports[i]).substr(0, 3000));
	}
	REQUIRE(r.reports.size() == 2);
	const std::string hostDump = field(r.reports[0], "desync_dump");
	const std::string joinDump = field(r.reports[1], "desync_dump");
	CHECK(hostDump == dir + "/DESYNC-Frame202-openbfme_peer-Human0.txt"); // the CRCs of frame 200 are compared on frame 202
	CHECK(joinDump == dir + "/DESYNC-Frame202-openbfme_peer-Joiner1.txt");
	for (const std::string &path : { hostDump, joinDump })
	{
		const std::string text = readFile(path);
		INFO(path);
		CHECK(text.rfind("Frame #202\n\n", 0) == 0);
		CHECK(text.find("DESYNC: the state hash of logic frame 200 differs") != std::string::npos);
		CHECK(text.find("<== DIFFERS") != std::string::npos);
		CHECK(text.find("\nslot 0 (frame 200") != std::string::npos); // both halves
		CHECK(text.find("\nslot 1 (frame 200") != std::string::npos);
		CHECK(text.find("\nobject ") != std::string::npos);
		CHECK(text.find("REPLAY FILE") != std::string::npos);
		std::remove(path.c_str());
	}
	CHECK(readFile(hostDump.substr(0, hostDump.size() - 4) + ".replay").size() > 100); // the copy of the host's recording
	std::remove((hostDump.substr(0, hostDump.size() - 4) + ".replay").c_str());
	std::remove((dir + "/host.replay").c_str());
#ifdef _WIN32
	runCommand("rmdir " + dir);
#else
	runCommand("rmdir " + dir);
#endif
}
#endif

#ifdef OPENBFME_PEER_EXE
TEST_CASE("mp2 net retail: on a slow link (200 ms each way, jitter) the peers measure the round trip and raise their command delay; the game stays in "
		  "lockstep")
{
	OPENBFME_REQUIRE_START(s);
	const std::string common = "--map \"maps/map mp evendim/map mp evendim.map\" --seed 23 --script --crc-interval 100 --run-ahead 2 --frames 400 "
							   "--delay 200 --jitter 30 --drop 20";
	Run r = runPeers("latency", 4, common, { "--slot human,FactionMen,0,0 --slot human,FactionMordor,1,1", "" });
	for (size_t i = 0; i < r.reports.size(); ++i)
	{
		MESSAGE("peer " << i << " rc " << r.rcs[i] << ":\n" << head(r.reports[i]).substr(0, 1200));
	}
	REQUIRE(r.reports.size() == 2);
	for (size_t i = 0; i < 2; ++i)
	{
		CHECK(r.rcs[i] == 0);
		CHECK(field(r.reports[i], "desyncs") == "0");
		CHECK(field(r.reports[i], "crc_checks_passed") == "3");
		// "run_ahead N changes C worst_latency_ms L": the router measured a round trip of 400+ ms (plus four times its variation): about 215 ms each way
		// is 2 frames (rounded up, + 10 %), + 1: 3; every peer took the router's RUNAHEAD (one change at least)
		const std::string ra = field(r.reports[i], "run_ahead");
		INFO(ra);
		CHECK(std::atoi(ra.c_str()) >= 3);
		CHECK(std::atoi(ra.substr(ra.find("changes ") + 8).c_str()) >= 1);
		if (i == 0)
		{
			CHECK(std::atoi(ra.substr(ra.find("worst_latency_ms ") + 17).c_str()) >= 400); // the router's measurement
		}
	}
	CHECK(r.hashes[0] == r.hashes[1]);
}
#endif
