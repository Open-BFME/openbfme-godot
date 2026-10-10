// OpenBFME unit tests. GPL-3.0.
// Lane MP-3 (NET-4) on the retail game, with real peer processes (openbfme_peer, UDP on localhost, the transport's fault injector GameNetwork/NetImpairment.h
// on every peer's outgoing datagrams): 2 and 4 players finish a skirmish with every frame's state hash identical under 150 ms +- 50 ms one way and 5 %
// loss; a 3 s blackout of one of 4 peers brings up the disconnect screen on every peer, it goes again and the game runs on in lockstep; a player who exits
// mid-game (the quit menu's Exit: MSG_SELF_DESTRUCT { TRUE }, then the leave) hands its army to its ally on every survivor, the survivors stay in lockstep
// and their score lines agree. The tests do not load the retail world in the test process (memory: four peers run at once); they SKIP loudly without
// ROTWK_INSTALL / BFME2_INSTALL.

#include "doctest.h"

#include "RetailTestMount.h"

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
bool haveRetail(const char *test)
{
	if (!std::getenv("ROTWK_INSTALL") || !std::getenv("BFME2_INSTALL"))
	{
		retailtest::printSkip(test);
		return false;
	}
	return true;
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
	{
		if (line.rfind(key + " ", 0) == 0)
		{
			return line.substr(key.size() + 1);
		}
	}
	return "";
}

std::vector<std::string> lines(const std::string &report, const std::string &key)
{
	std::vector<std::string> out;
	std::istringstream in(report);
	std::string line;
	while (std::getline(in, line))
	{
		if (line.rfind(key + " ", 0) == 0)
		{
			out.push_back(line);
		}
	}
	return out;
}

// the value after `key` in a "key value key value ..." line; -1 when the key is missing (a check on it then fails instead of passing vacuously)
long long counter(const std::string &line, const std::string &key)
{
	std::istringstream in(line);
	std::string k;
	long long v = 0;
	while (in >> k)
	{
		if (k == key && in >> v)
		{
			return v;
		}
	}
	return -1;
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
	return 20000 + ((pid * 13 + salt * 1597) % 20000 + 20000) % 20000;
}

struct Run
{
	std::vector<int> rcs;
	std::vector<std::string> reports, hashes, errs; ///< errs: each peer's stderr (why a peer stopped early)
	double seconds = 0;
};

// a host and joiners, each its own process; extras[i] goes to peer i's command line (the host first)
Run runPeers(const std::string &tag, int salt, const std::string &common, const std::vector<std::string> &extras)
{
	const int port = testPort(salt);
	Run r;
	const size_t n = extras.size();
	r.rcs.assign(n, -1);
	std::vector<std::string> reportFiles, hashFiles, errFiles, commands;
	for (size_t i = 0; i < n; ++i)
	{
		reportFiles.push_back("mp3_" + tag + "_" + std::to_string(i) + "_report.txt");
		hashFiles.push_back("mp3_" + tag + "_" + std::to_string(i) + "_hashes.txt");
		errFiles.push_back("mp3_" + tag + "_" + std::to_string(i) + "_stderr.txt");
		const std::string role = i == 0 ? " --host " + std::to_string(port) : " --join 127.0.0.1:" + std::to_string(port) + " --name Joiner" + std::to_string(i);
		commands.push_back(peerBase() + " " + common + role + " " + extras[i] + " --lobby-timeout 900 --report " + reportFiles[i] + " --hashes " + hashFiles[i]
			+ " 2> " + errFiles[i]);
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
		r.errs.push_back(readFile(errFiles[i]));
		std::remove(reportFiles[i].c_str());
		std::remove(hashFiles[i].c_str());
		std::remove(errFiles[i].c_str());
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

void checkLockstep(const Run &r, int frames, int crcs)
{
	for (size_t i = 0; i < r.reports.size(); ++i)
	{
		INFO("peer " << i);
		CHECK(r.rcs[i] == 0);
		CHECK(field(r.reports[i], "frames") == std::to_string(frames));
		CHECK(field(r.reports[i], "desyncs") == "0");
		CHECK(field(r.reports[i], "crc_checks_passed") == std::to_string(crcs));
		CHECK(r.reports[i].find("network_error") == std::string::npos);
		CHECK(r.reports[i].find("transport_error") == std::string::npos);
		CHECK(field(r.reports[i], "disconnected_slots") == "none");
		CHECK(!r.hashes[i].empty());
		CHECK(r.hashes[i] == r.hashes[0]);
		CHECK(field(r.reports[i], "final_hash") == field(r.reports[0], "final_hash"));
		// the faults were applied (some datagrams lost on every peer's links)
		// the faults were applied: the injector's counters (the "impairment" row; "impairment_config" is the configuration) report losses
		const std::string im = field(r.reports[i], "impairment");
		INFO(im);
		CHECK(!field(r.reports[i], "impairment_config").empty());
		CHECK(counter(im, "scheduled") > 0);
		CHECK(counter(im, "lost") > 0);
		// the lane's stops are in every report
		CHECK(r.reports[i].find("stop [S-1890]") != std::string::npos);
		CHECK(r.reports[i].find("stop [S-1892]") != std::string::npos);
	}
}

const char *kFaults = "--link latency=150,jitter=50,loss=50";
} // namespace

TEST_CASE("mp3 net retail: 2 players finish a skirmish with every frame's hash identical under 150 ms +- 50 ms one way and 5 % loss")
{
	if (!haveRetail("mp3 net retail (2 peers under faults)"))
	{
		return;
	}
	const int kFrames = 500;
	const std::string common = std::string("--map \"maps/map mp evendim/map mp evendim.map\" --seed 31 --script --crc-interval 100 --run-ahead 1 ") + kFaults
		+ " --frames " + std::to_string(kFrames);
	Run r = runPeers("faults2", 1, common, { "--slot human,FactionMen,0,0 --slot human,FactionMordor,1,1", "" });
	for (size_t i = 0; i < r.reports.size(); ++i)
	{
		MESSAGE("peer " << i << " rc " << r.rcs[i] << " stderr: " << r.errs[i] << "\n" << head(r.reports[i]).substr(0, 2400));
	}
	MESSAGE("two peers: " << r.seconds << " s");
	REQUIRE(r.reports.size() == 2);
	checkLockstep(r, kFrames, 4);
	for (size_t i = 0; i < 2; ++i)
	{
		// "run_ahead N changes C worst_latency_ms L": the router measured the round trip of 300 +- 100 ms and changed the common command delay (it may
		// have come back to the lobby's 2 by the end: the value at the end is not the measure)
		const std::string ra = field(r.reports[i], "run_ahead");
		INFO(ra);
		CHECK(std::atoi(ra.substr(ra.find("changes ") + 8).c_str()) >= 1);
		if (i == 0)
		{
			CHECK(std::atoi(ra.substr(ra.find("worst_latency_ms ") + 17).c_str()) >= 200); // the router's measurement
		}
	}
}

TEST_CASE("mp3 net retail: 4 players finish a skirmish with every frame's hash identical under 150 ms +- 50 ms one way and 5 % loss")
{
	if (!haveRetail("mp3 net retail (4 peers under faults)"))
	{
		return;
	}
	const int kFrames = 500;
	const std::string common = std::string("--map \"maps/map mp fall back 4p/map mp fall back 4p.map\" --seed 32 --script --crc-interval 100 --run-ahead 2 ")
		+ kFaults + " --frames " + std::to_string(kFrames);
	const std::string slots = "--slot human,FactionMen,0,0 --slot human,FactionElves,1,0 --slot human,FactionIsengard,2,1 --slot human,FactionAngmar,3,1";
	Run r = runPeers("faults4", 2, common, { slots, "", "", "" });
	for (size_t i = 0; i < r.reports.size(); ++i)
	{
		MESSAGE("peer " << i << " rc " << r.rcs[i] << " stderr: " << r.errs[i] << "\n" << head(r.reports[i]).substr(0, 2400));
	}
	MESSAGE("four peers: " << r.seconds << " s");
	REQUIRE(r.reports.size() == 4);
	checkLockstep(r, kFrames, 4);
}

TEST_CASE("mp3 net retail: a 3 s blackout of one of 4 peers under the faults brings up the disconnect screen on every peer; it goes again, nobody is "
		  "dropped, every frame's hash stays identical")
{
	if (!haveRetail("mp3 net retail (blackout)"))
	{
		return;
	}
	const int kFrames = 500;
	// NetworkDisconnectTime 1 s for the test (RotWK 2.01's GameData: 15 s, where a 3 s blackout only holds the game)
	const std::string common = std::string("--map \"maps/map mp fall back 4p/map mp fall back 4p.map\" --seed 33 --script --crc-interval 100 --run-ahead 2 ")
		+ kFaults + " --disconnect-ms 1000 --frames " + std::to_string(kFrames);
	const std::string slots = "--slot human,FactionDwarves,0,0 --slot human,FactionWild,1,0 --slot human,FactionMordor,2,1 --slot human,FactionMen,3,1";
	Run r = runPeers("blackout", 3, common, { slots, "", "--blackout-at 200:3000", "" });
	for (size_t i = 0; i < r.reports.size(); ++i)
	{
		MESSAGE("peer " << i << " rc " << r.rcs[i] << " stderr: " << r.errs[i] << "\n" << head(r.reports[i]).substr(0, 2400));
	}
	REQUIRE(r.reports.size() == 4);
	checkLockstep(r, kFrames, 4);
	CHECK(field(r.reports[2], "blackout") == "frame 200 for 3000 ms");
	CHECK(counter(field(r.reports[2], "impairment"), "blackouts") == 1);
	CHECK(counter(field(r.reports[2], "impairment"), "blackout_in") > 0);
	CHECK(counter(field(r.reports[2], "impairment"), "blackout_out") > 0);
	for (size_t i = 0; i < r.reports.size(); ++i)
	{
		INFO("peer " << i);
		CHECK(r.reports[i].find("disconnect_log disconnect screen on") != std::string::npos);
		CHECK(r.reports[i].find("disconnect_log disconnect screen off") != std::string::npos);
		CHECK(field(r.reports[i], "dropped_by_me") == "none");
		CHECK(field(r.reports[i], "disconnect_quit") == "none");
		CHECK(field(r.reports[i], "self_destruct").rfind("executed 0 ", 0) == 0);
	}
}

TEST_CASE("mp3 net retail: a player exits mid-game under the faults (MSG_SELF_DESTRUCT { TRUE }, then the leave): its ally gets its army on every "
		  "survivor, the survivors play on in lockstep and agree on every score line")
{
	if (!haveRetail("mp3 net retail (a player exits)"))
	{
		return;
	}
	const int kFrames = 600;
	const std::string common = std::string("--map \"maps/map mp fall back 4p/map mp fall back 4p.map\" --seed 34 --script --crc-interval 100 --run-ahead 2 ")
		+ kFaults + " --frames " + std::to_string(kFrames);
	// slots 1 and 2 are taken by the joiners in join order; both are on team 0 with the host, so the leaver always has living human allies (the first
	// mutual ally in player list order gets the assets, RW 0x77CA3D)
	const std::string slots = "--slot human,FactionMen,0,0 --slot human,FactionElves,1,0 --slot human,FactionDwarves,2,0 --slot medium,FactionMordor,3,1";
	Run r = runPeers("exit", 4, common, { slots, "", "--quit-at 250" });
	for (size_t i = 0; i < r.reports.size(); ++i)
	{
		MESSAGE("peer " << i << " rc " << r.rcs[i] << " stderr: " << r.errs[i] << "\n" << head(r.reports[i]).substr(0, 3000));
	}
	REQUIRE(r.reports.size() == 3);
	CHECK(r.rcs[2] == 0);
	CHECK(field(r.reports[2], "quit_at") == "250");
	const std::string gone = field(r.reports[2], "slot");
	REQUIRE((gone == "1" || gone == "2"));
	for (size_t i = 0; i < 2; ++i)
	{
		INFO("survivor " << i);
		CHECK(r.rcs[i] == 0);
		CHECK(field(r.reports[i], "frames") == std::to_string(kFrames));
		CHECK(field(r.reports[i], "desyncs") == "0");
		CHECK(field(r.reports[i], "crc_checks_passed") == "5");
		CHECK(r.reports[i].find("network_error") == std::string::npos);
		CHECK(field(r.reports[i], "disconnect_quit") == "none");
		CHECK(field(r.reports[i], "self_destruct").rfind("executed 1 transfers 1 kills 1", 0) == 0);
		CHECK(field(r.reports[i], "slot_objects " + gone) == "0 defeated 1");
		// right after the frame that executed it: the first living mutual ally in player list order (RW 0x77CA3D -> 0x6AF598), the host in slot 0,
		// owns every object that changed hands
		const std::string cp = field(r.reports[i], "self_destruct_checkpoint");
		INFO(cp);
		CHECK(counter(cp, "leaver_slot") == std::atoi(gone.c_str()));
		CHECK(counter(cp, "ally_slot") == 0);
		CHECK(counter(cp, "transferred") > 0);
		CHECK(counter(cp, "ally_owned") == counter(cp, "transferred"));
		CHECK(cp == field(r.reports[0], "self_destruct_checkpoint"));
		CHECK(std::atoi(field(r.reports[i], "packet_router").c_str()) == 0); // the router of the last frame: it stayed
		CHECK(std::atoi(field(r.reports[i], "peers_retired").c_str()) >= 1); // the leaver's connection after its PLAYERLEAVE (and the host's at the end)
	}
	CHECK(r.hashes[0] == r.hashes[1]);
	CHECK(lines(r.reports[0], "score") == lines(r.reports[1], "score"));
	CHECK(lines(r.reports[0], "score").size() == 4);
	// the leaver's score line on the survivors is the one it had when it left (nothing more is counted for a defeated player)
	CHECK(field(r.reports[0], "score " + gone) == field(r.reports[2], "score " + gone));
	// up to its exit the leaver ran the survivors' frames
	CHECK(hashesBefore(r.hashes[2], 250) == hashesBefore(r.hashes[0], 250));
}
#endif

#ifdef OPENBFME_PEER_EXE
TEST_CASE("mp3 net retail: the host (the packet router) exits mid-game under the faults: the next router takes over, its ally gets its army, the two "
		  "survivors play on in lockstep and agree on every score line")
{
	if (!haveRetail("mp3 net retail (the host exits)"))
	{
		return;
	}
	const int kFrames = 600;
	const std::string common = std::string("--map \"maps/map mp fall back 4p/map mp fall back 4p.map\" --seed 35 --script --crc-interval 100 --run-ahead 2 ")
		+ kFaults + " --frames " + std::to_string(kFrames);
	const std::string slots = "--slot human,FactionIsengard,0,0 --slot human,FactionAngmar,1,0 --slot human,FactionWild,2,0 --slot medium,FactionElves,3,1";
	Run r = runPeers("hostexit", 5, common, { slots + " --quit-at 250", "", "" });
	for (size_t i = 0; i < r.reports.size(); ++i)
	{
		MESSAGE("peer " << i << " rc " << r.rcs[i] << " stderr: " << r.errs[i] << "\n" << head(r.reports[i]).substr(0, 3000));
	}
	REQUIRE(r.reports.size() == 3);
	CHECK(r.rcs[0] == 0);
	CHECK(field(r.reports[0], "quit_at") == "250");
	for (size_t i = 1; i < 3; ++i)
	{
		INFO("survivor " << i);
		CHECK(r.rcs[i] == 0);
		CHECK(field(r.reports[i], "frames") == std::to_string(kFrames));
		CHECK(field(r.reports[i], "desyncs") == "0");
		CHECK(field(r.reports[i], "crc_checks_passed") == "5");
		CHECK(r.reports[i].find("network_error") == std::string::npos);
		CHECK(field(r.reports[i], "disconnect_quit") == "none");
		CHECK(field(r.reports[i], "self_destruct").rfind("executed 1 transfers 1 kills 1", 0) == 0);
		CHECK(field(r.reports[i], "slot_objects 0") == "0 defeated 1");
		// the first living mutual ally after slot 0 in player list order is slot 1; it owns every object that changed hands right after the frame
		const std::string cp = field(r.reports[i], "self_destruct_checkpoint");
		INFO(cp);
		CHECK(counter(cp, "leaver_slot") == 0);
		CHECK(counter(cp, "ally_slot") == 1);
		CHECK(counter(cp, "transferred") > 0);
		CHECK(counter(cp, "ally_owned") == counter(cp, "transferred"));
		CHECK(cp == field(r.reports[1], "self_destruct_checkpoint"));
		// review r1: the router's graceful leave hands the role to the next slot of the fallback order at its leave frame
		CHECK(std::atoi(field(r.reports[i], "packet_router").c_str()) == 1); // the router of the last frame run
	}
	CHECK(r.hashes[1] == r.hashes[2]);
	CHECK(lines(r.reports[1], "score") == lines(r.reports[2], "score"));
	CHECK(hashesBefore(r.hashes[0], 250) == hashesBefore(r.hashes[1], 250));
}
#endif
