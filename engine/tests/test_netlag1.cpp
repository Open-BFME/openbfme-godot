// OpenBFME unit tests. GPL-3.0.
// Lane NETLAG-1 (the owner, playing v0.4.0-alpha.3 multiplayer: "my buildings don't do anything for like half a second"): the time from a player's order to
// its effect in a multiplayer game, measured with real peer processes on real UDP (the openbfme_peer executable, 5 Hz paced clocks: --paced), on a LAN and
// through the online relay (the openbfme_relay executable). RotWK's command delay is one frame: the packet router stamps a command with the current frame
// + 1 (RW 0x8D4301 ConnectionManager::sendLocalCommand, RW 0x8D3AF1 sendLocalCommandDirect; BFME2 decomp tier A, byte-matched), 200 to 400 ms from a click
// to the frame that runs it. MP-1 .. MP-3 ran every network game with a run-ahead of 2 (400 to 600 ms). Each case pins the delay in frames of every local
// order (LockstepDriver::SmoothStats::commandDelayFrames, exact: the scripted player issues on protocol frames) and prints the wall-clock times (issue to
// the batch's acquire, issue to its completion: the effect in the world). SKIP loudly without ROTWK_INSTALL / BFME2_INSTALL.

#include "doctest.h"

#include "StartTestUtil.h"

#include "Common/Recorder.h"
#include "Common/Sha256.h"
#include "GameNetwork/ProfileIdentity.h"
#include "GameNetwork/RelayProtocol.h"
#include "GameNetwork/Transport.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifndef _WIN32
#include <csignal>
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

std::uint16_t freePort()
{
	UDP tmp;
	std::string error;
	REQUIRE_MESSAGE(tmp.bind(0x7F000001u, 0, &error), error);
	return tmp.localAddress().port;
}

struct Run
{
	std::vector<int> rcs;
	std::vector<std::string> reports, hashes;
};

// one process per command line (the first one 300 ms ahead: the host), each writing <tag>_<i>_report.txt / _hashes.txt
Run runAll(const std::string &tag, const std::vector<std::string> &commands)
{
	Run r;
	const size_t n = commands.size();
	r.rcs.assign(n, -1);
	std::vector<std::string> reportFiles, hashFiles, lines;
	for (size_t i = 0; i < n; ++i)
	{
		reportFiles.push_back("netlag1_" + tag + "_" + std::to_string(i) + "_report.txt");
		hashFiles.push_back("netlag1_" + tag + "_" + std::to_string(i) + "_hashes.txt");
		lines.push_back(commands[i] + " --report " + reportFiles[i] + " --hashes " + hashFiles[i]);
	}
	std::vector<std::thread> threads;
	for (size_t i = 0; i < n; ++i)
	{
		threads.emplace_back([&, i] { r.rcs[i] = runCommand(lines[i]); });
		std::this_thread::sleep_for(std::chrono::milliseconds(300));
	}
	for (std::thread &t : threads)
	{
		t.join();
	}
	for (size_t i = 0; i < n; ++i)
	{
		r.reports.push_back(readFile(reportFiles[i]));
		r.hashes.push_back(readFile(hashFiles[i]));
		std::remove(reportFiles[i].c_str());
		std::remove(hashFiles[i].c_str());
	}
	return r;
}

// "1:57" -> { 1: 57 }; the delay in frames of every local order
std::map<int, int> delays(const std::string &report)
{
	std::map<int, int> out;
	std::istringstream in(field(report, "command_delay_frames"));
	std::string bucket;
	while (in >> bucket)
	{
		const size_t colon = bucket.find(':');
		if (colon != std::string::npos)
		{
			out[std::atoi(bucket.c_str())] = std::atoi(bucket.c_str() + colon + 1);
		}
	}
	return out;
}

// every peer ran the game to its end, in lockstep, and every local order of every peer ran exactly `frames` frames after the frame it was issued on
// (`frames` -1: an adaptive run whose delays follow the measured round trip, reported only)
void checkGame(const Run &r, int frames, const std::string &label)
{
	REQUIRE(r.reports.size() == 2);
	for (size_t i = 0; i < r.reports.size(); ++i)
	{
		INFO(label << ", peer " << i << ": " << r.reports[i].substr(0, 1500));
		CHECK(r.rcs[i] == 0);
		CHECK(field(r.reports[i], "desyncs") == "0");
		const std::map<int, int> d = delays(r.reports[i]);
		REQUIRE_FALSE(d.empty());
		if (frames >= 0)
		{
			CHECK(d.size() == 1);
			CHECK(d.begin()->first == frames);
		}
		CHECK(field(r.reports[i], "input_latency_ms").find("unmatched 0") != std::string::npos);
		MESSAGE(label << ", peer " << i << ": delay frames " << field(r.reports[i], "command_delay_frames") << "; issue -> its batch runs: "
					  << field(r.reports[i], "input_latency_ms") << "; issue -> effect: " << field(r.reports[i], "command_effect_ms") << "; run_ahead "
					  << field(r.reports[i], "run_ahead") << "; stalls " << field(r.reports[i], "stall_ms"));
	}
	CHECK(r.hashes[0] == r.hashes[1]);
	CHECK_FALSE(r.hashes[0].empty());
}

Run lanGame(const std::string &tag, const std::string &extra, const std::string &hostExtra = "")
{
	const std::uint16_t port = freePort();
	const std::string common = "--map \"maps/map mp evendim/map mp evendim.map\" --seed 23 --script --crc-interval 100 --frames 150 --paced " + extra;
	return runAll(tag, { peerBase() + " " + common + " --host " + std::to_string(port) + " --slot human,FactionMen,0,0 --slot human,FactionMordor,1,1 " + hostExtra,
						   peerBase() + " " + common + " --join 127.0.0.1:" + std::to_string(port) + " --name Joiner" });
}
// the host's recording of a game, played back by another process of this build: every frame's hash equal (a replay keeps the run-ahead it was played
// with: its frames hold the commands as they ran)
void checkReplay(const std::string &replay, const Run &game, const std::string &label)
{
	const std::string rr = replay + ".report.txt";
	const int rc = runCommand(peerBase() + " --replay " + replay + " --report " + rr);
	const std::string rep = readFile(rr);
	INFO(label << ": " << rep.substr(0, 1500));
	CHECK(rc == 0);
	CHECK(field(rep, "mismatches") == "0");
	CHECK(field(rep, "hashes_compared") == "149"); // the game starts on frame 1
	CHECK(field(rep, "final_hash").substr(0, 10) == field(game.reports[0], "final_hash"));
	std::remove(rr.c_str());
	std::remove(replay.c_str());
}
} // namespace

// The pinned runs keep the lobby's run-ahead (--fixed-run-ahead): on a loaded test machine a peer process starved for a second measures that as its
// round trip, and the adaptive run-ahead then rightly raises the delay for a while (the unit test "netlag1 run-ahead: ..." in test_mp2_disconnect.cpp pins
// the adaptive target for LAN round trips). The adaptive run is reported beside them.
TEST_CASE("netlag1 net retail: a 2-player LAN game on real UDP with 5 Hz clocks: every order runs on the frame after the one it was issued on (RotWK's "
		  "frame + 1) with the lobby's run-ahead 1; MP-1's run-ahead 2 (alpha.3) took a frame more; the Uber profile the same")
{
	OPENBFME_REQUIRE_START(s);
	const Run now = lanGame("lan", "--fixed-run-ahead", "--record netlag1_ra1.replay");
	checkGame(now, 1, "LAN, run-ahead 1 (the lobby's default)");
	checkReplay("netlag1_ra1.replay", now, "the run-ahead 1 game's replay");
	const Run before = lanGame("lan2", "--run-ahead 2 --fixed-run-ahead", "--record netlag1_ra2.replay");
	checkGame(before, 2, "LAN, run-ahead 2 (v0.4.0-alpha.3)");
	checkReplay("netlag1_ra2.replay", before, "a replay recorded with the run-ahead 2 (alpha.3's)");
	// UBER-1: the Uber profile changes the logic's threads, not the command path
	const Run uber = lanGame("uber", "--uber --fixed-run-ahead");
	checkGame(uber, 1, "LAN, run-ahead 1, Uber profile on both peers");
	// what a player gets: the adaptive run-ahead from the lobby's 1 (the measured delays and the run-ahead it ended with are in the message)
	const Run adaptive = lanGame("adaptive", "");
	checkGame(adaptive, -1, "LAN, adaptive run-ahead from 1");
}

// lane NETLAG-1 (Sol's review): a recording made by the build BEFORE this lane (archive 593e83cd, run-ahead 2, its own engine id) still plays back here,
// with every frame's hash equal: the fixture was recorded by that build (a 2-player LAN game of openbfme_peer, 150 frames), not by this one
// (merge queue q15: re-recorded by the archive build 593e83cd with the same command line; the earlier fixture, recorded at de94ba60, stopped matching when q12-q14 changed the simulation)
TEST_CASE("netlag1 replay: a recording made by the pre-lane build (archive 593e83cd, run-ahead 2) plays back on this build with every hash equal")
{
	OPENBFME_REQUIRE_START(s);
	(void)s;
	const std::string fixture = std::string(OPENBFME_TEST_DATA_DIR) + "/netlag1/prelane_runahead2.replay";
	ReplayFile file;
	std::string error;
	REQUIRE_MESSAGE(ReplayFile::load(fixture, file, &error), error);
	CHECK(file.header.network.runAhead == 2);
	CHECK(file.complete);
	// it names the pre-lane build's engine id, which is not this build's, and that id is a verified one
	const std::string preLane = "976a1229ea71f55c95f05da0b8530fc7b5a5b181663ede0396ee43ae3b6be361";
	CHECK(file.header.profile.text.find("\nengine " + preLane + "\n") != std::string::npos);
	CHECK(std::string(ProfileIdentity::buildId()) != preLane);
	CHECK(ProfileIdentity::verifiedReplayEngines().front().id == preLane);
	const std::string rr = "netlag1_prelane.report.txt";
	const int rc = runCommand(peerBase() + " --replay \"" + fixture + "\" --report " + rr);
	const std::string rep = readFile(rr);
	INFO(rep.substr(0, 1500));
	CHECK(rc == 0);
	CHECK(field(rep, "mismatches") == "0");
	CHECK(field(rep, "hashes_compared") == "149");
	const std::string fin = field(rep, "final_hash"); // "0x<played> recorded 0x<recorded>"
	CHECK(fin.substr(0, 10) == fin.substr(fin.find("recorded ") + 9, 10));
	std::remove(rr.c_str());
}

// the compatibility is for recordings only: live peers (the lobby's JOIN / START compare the whole identity), another line's difference and an engine id
// nobody verified are all refused, by name
TEST_CASE("netlag1 replay: only the engine line of a verified older build is tolerated, and only for a recording")
{
	const std::string a = "openbfme-profile 1\nprofile enhanced\nengine AAAA\nfeatures x\nnumeric n\nrng ZH_CarryChain\nmods none\narchive 0 r:a.big 11 5\n";
	const ProfileIdentity ours = ProfileIdentity::fromText(a);
	auto withLine = [&](const std::string &from, const std::string &to) {
		std::string t = a;
		t.replace(t.find(from), from.size(), to);
		return ProfileIdentity::fromText(t);
	};
	const std::string verified = ProfileIdentity::verifiedReplayEngines().front().id;
	const ProfileIdentity older = withLine("engine AAAA", "engine " + verified);
	CHECK(ProfileIdentity::replayCompatible(ours, older)); // the recording of a verified build
	CHECK_FALSE(ours == older);                            // a live peer of that build is still another profile
	CHECK_FALSE(ProfileIdentity::difference(ours, older).empty());
	CHECK_FALSE(ProfileIdentity::replayCompatible(ours, withLine("engine AAAA", "engine BBBB"))); // an engine nobody verified
	// the verified engine with another line different: the data, the numerics, the formats
	std::string t = older.text;
	t.replace(t.find("archive 0 r:a.big 11 5"), 22, "archive 0 r:a.big 12 5");
	CHECK_FALSE(ProfileIdentity::replayCompatible(ours, ProfileIdentity::fromText(t)));
	std::string t2 = older.text;
	t2.replace(t2.find("features x"), 10, "features y");
	CHECK_FALSE(ProfileIdentity::replayCompatible(ours, ProfileIdentity::fromText(t2)));
	CHECK_FALSE(ProfileIdentity::replayCompatible(ours, ProfileIdentity::fromText("")));            // nothing
	CHECK_FALSE(ProfileIdentity::replayCompatible(ours, ProfileIdentity::fromText(a + "extra 1\n"))); // another line count
}

#ifndef _WIN32
namespace
{
// the build's profile identity MD5 and its faction indices (openbfme_peer --print-profile)
bool printProfile(std::string &digest, std::map<std::string, int> &factions)
{
	const std::string cmd = peerBase() + " --print-profile";
	FILE *p = popen(cmd.c_str(), "r");
	if (!p)
	{
		return false;
	}
	std::string text;
	char buf[4096];
	size_t n;
	while ((n = fread(buf, 1, sizeof(buf), p)) > 0)
	{
		text.append(buf, n);
	}
	if (pclose(p) != 0)
	{
		return false;
	}
	std::istringstream in(text);
	std::string line;
	std::getline(in, digest);
	while (std::getline(in, line))
	{
		std::istringstream l(line);
		std::string word, name;
		int index = -1;
		if (l >> word >> index >> name && word == "faction")
		{
			factions[name] = index;
		}
	}
	return digest.size() == 32 && !factions.empty();
}
} // namespace

TEST_CASE("netlag1 net retail: a 2-player online game through the relay process (real UDP, 5 Hz clocks): every order runs on the frame after the one it "
		  "was issued on, as on a LAN; a spectator follows the relay's log to the same final state")
{
	OPENBFME_REQUIRE_START(s);
#ifndef OPENBFME_RELAY_EXE
	MESSAGE("SKIP: this build has no openbfme_relay");
	return;
#else
	std::string digest;
	std::map<std::string, int> factions;
	REQUIRE(printProfile(digest, factions));
	REQUIRE(factions.count("FactionMen") == 1);
	REQUIRE(factions.count("FactionMordor") == 1);
	const std::filesystem::path dir = std::filesystem::temp_directory_path() / ("openbfme_netlag1_" + std::to_string(getpid()));
	std::filesystem::create_directories(dir / "log");
	std::vector<std::uint8_t> key;
	for (int i = 0; i < 32; ++i)
	{
		key.push_back((std::uint8_t)(i * 7 + 3));
	}
	{
		std::ofstream k(dir / "keys.txt");
		k << "1:" << HexBytes(key.data(), key.size()) << "\n";
	}
	const std::uint16_t port = freePort();
	const std::string relayAddr = "127.0.0.1:" + std::to_string(port), keys = (dir / "keys.txt").string(), logs = (dir / "log").string(),
					  status = (dir / "status.json").string();
	const pid_t relay = fork();
	REQUIRE(relay >= 0); // tolerance: fork gives any process id, -1 is its failure
	if (relay == 0)
	{
		execl(OPENBFME_RELAY_EXE, OPENBFME_RELAY_EXE, "--bind", relayAddr.c_str(), "--keys", keys.c_str(), "--log-dir", logs.c_str(), "--status",
			status.c_str(), "--stats-s", "0", "--idle-s", "60", (char *)nullptr);
		_exit(127);
	}
	std::this_thread::sleep_for(std::chrono::milliseconds(500));
	// the lobby service's START for each player (services/online/lobby: kind openbfme-start, its run-ahead 1)
	const std::uint64_t game = 0x0B0E7A9100000001ull;
	const std::string setup = "{\"map\":\"maps/map mp evendim/map mp evendim.map\",\"seed\":23,\"startingCash\":1500,\"superweaponRestriction\":0,"
							  "\"rules\":null,\"legendaryHeroes\":1,\"slots\":[{\"state\":\"player\",\"name\":\"A\",\"faction\":"
		+ std::to_string(factions["FactionMen"]) + ",\"color\":0,\"startPos\":0,\"team\":0},{\"state\":\"player\",\"name\":\"B\",\"faction\":"
		+ std::to_string(factions["FactionMordor"]) + ",\"color\":1,\"startPos\":1,\"team\":1}]}";
	std::vector<std::string> commands;
	for (int slot = 0; slot < 2; ++slot)
	{
		RelayProtocol::Ticket t;
		t.keyId = 1;
		t.gameId = game;
		t.slot = (std::uint8_t)slot;
		t.slotMask = 0x03;
		t.expires = 2000000000u;
		const auto ticket = RelayProtocol::signTicket(t, key);
		char gameHex[17];
		std::snprintf(gameHex, sizeof(gameHex), "%016llx", (unsigned long long)game);
		const std::string start = std::string("{\"v\":1,\"kind\":\"openbfme-start\",\"game\":\"") + gameHex + "\",\"slot\":" + std::to_string(slot)
			+ ",\"relay\":\"" + relayAddr + "\",\"ticket\":\"" + RelayProtocol::base64UrlEncode(ticket.data(), ticket.size())
			+ "\",\"runAhead\":1,\"crcInterval\":100,\"profile\":\"" + digest + "\",\"setup\":" + setup + "}";
		const std::string file = (dir / ("start-" + std::to_string(slot) + ".json")).string();
		std::ofstream(file) << start;
		commands.push_back(peerBase() + " --online-start \"" + file + "\" --bind 127.0.0.1 --script --frames 150 --paced --fixed-run-ahead --desync-dir \""
			+ dir.string() + "\"");
	}
	{
		// a spectator (ONLINE-3) follows the relay's log of the game with no delay and compares the players' CRCs
		RelayProtocol::Ticket t;
		t.keyId = 1;
		t.gameId = game;
		t.slot = 0;
		t.slotMask = 0x03;
		t.expires = 2000000000u;
		t.spectator = true;
		t.delaySeconds = 0;
		const std::vector<std::uint8_t> ticket = RelayProtocol::signSpectatorTicket(t, key);
		char gameHex[17];
		std::snprintf(gameHex, sizeof(gameHex), "%016llx", (unsigned long long)game);
		const std::string start = std::string("{\"v\":1,\"kind\":\"openbfme-spectate\",\"game\":\"") + gameHex + "\",\"spectator\":0,\"delay\":0,\"relay\":\""
			+ relayAddr + "\",\"ticket\":\"" + RelayProtocol::base64UrlEncode(ticket.data(), ticket.size()) + "\",\"runAhead\":1,\"crcInterval\":100,\"profile\":\""
			+ digest + "\",\"setup\":" + setup + "}";
		const std::string file = (dir / "spectate-0.json").string();
		std::ofstream(file) << start;
		commands.push_back(peerBase() + " --online-start \"" + file + "\" --bind 127.0.0.1 --frames 150");
	}
	Run r = runAll("relay", commands);
	kill(relay, SIGTERM);
	int st = 0;
	waitpid(relay, &st, 0);
	REQUIRE(r.reports.size() == 3);
	const std::string spectator = r.reports[2];
	r.reports.pop_back();
	r.hashes.pop_back();
	for (const std::string &rep : r.reports)
	{
		CHECK(field(rep, "relay").find(" bound 1 ") != std::string::npos);
	}
	checkGame(r, 1, "online through the relay, run-ahead 1");
	INFO("spectator: " << spectator.substr(0, 1500));
	CHECK(r.rcs[2] == 0);
	CHECK(field(spectator, "spectate").rfind("state ended reached 150 ", 0) == 0);
	CHECK(field(spectator, "spectate").find(" crc_mismatches 0 ") != std::string::npos);
	CHECK(field(spectator, "spectate").find(" crcs_compared 0 ") == std::string::npos);
	CHECK(field(spectator, "final_hash") == field(r.reports[0], "final_hash"));
	std::error_code ec;
	std::filesystem::remove_all(dir, ec);
#endif
}
#endif
#endif
