// OpenBFME. GPL-3.0. Synthetic replay opening, without retail files.
#include "doctest.h"
#include "Common/BuildVersion.h"
#include "Common/Recorder.h"
#include "Common/RecordingBuild.h"
#include "Common/System/SaveGame/SaveGameFile.h"
#include "GameClient/CommandLogLoad.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace
{
ReplayHeader header()
{
	ReplayHeader h;
	h.profile = ProfileIdentity::compute({}, h.algorithm);
	h.game.game.mapName = "maps/synthetic/synthetic.map";
	h.buildVersion = "v0.4.13";
	h.buildCommit = "0123456789abcdef0123456789abcdef01234567";
	h.buildDate = "2026-10-22";
	return h;
}

std::vector<std::uint8_t> recording(const ReplayHeader &h)
{
	ReplayWriter writer;
	writer.setKeepCopy(true);
	std::string error;
	REQUIRE_MESSAGE(writer.open("", h, &error), error);
	CommandList commands;
	commands.append(GameMessage(MSG_DO_STOP, 0));
	writer.recordFrame(0, commands);
	writer.recordHash(1, 0xABCDu);
	std::vector<std::uint8_t> bytes;
	REQUIRE_MESSAGE(writer.logUpTo(1, 0xABCDu, bytes, &error), error);
	return bytes;
}

void writeFile(const std::string &path, const std::vector<std::uint8_t> &bytes, size_t size)
{
	std::ofstream file(std::filesystem::u8path(path), std::ios::binary | std::ios::trunc);
	REQUIRE(file.good());
	file.write((const char *)bytes.data(), (std::streamsize)size);
	REQUIRE(file.good());
}
} // namespace

TEST_CASE("replay open: a synthetic recording reaches the real playback driver without retail assets")
{
	const auto h = header();
	const auto bytes = recording(h);
	const char *fixture = std::getenv("OPENBFME_REPLAY_FIXTURE");
	const std::string path = fixture ? fixture : "replay_open_synthetic.replay";
	writeFile(path, bytes, bytes.size());
	ReplayFile file;
	std::string error;
	REQUIRE_MESSAGE(ReplayFile::load(path, file, &error), error);
	CHECK(file.header.buildVersion == h.buildVersion);
	CHECK(file.header.buildCommit == h.buildCommit);
	CHECK(file.header.buildDate == h.buildDate);
	CHECK(file.profileMismatch(h.profile).empty());
	CHECK(file.complete);
	ReplayPlayback player(file);
	CommandList local;
	local.append(GameMessage(MSG_DO_STOP, 1));
	player.pump(0, local);
	FrameBatch batch;
	REQUIRE(player.acquire(0, batch));
	REQUIRE(batch.commands.size() == 1);
	CHECK(batch.commands[0].getType() == MSG_DO_STOP);
	CHECK(player.localInputIgnored() == 1);
	FrameCompletion completion;
	completion.frame = 1;
	completion.hashed = true;
	completion.hash = 0xABCDu;
	player.completed(completion);
	CHECK(player.hashesCompared() == 1);
	CHECK_FALSE(player.hasMismatch());
	CHECK(player.finished());
	CHECK_FALSE(player.acquire(1, batch));
	if (!fixture)
	{
		std::remove(path.c_str());
	}
}

TEST_CASE("replay open: new recordings carry the actual recording build by default")
{
	auto h = header();
	h.buildVersion.clear();
	h.buildCommit.clear();
	ReplayFile file;
	std::string error;
	REQUIRE_MESSAGE(ReplayFile::parse(recording(h), file, &error), error);
	CHECK(file.header.buildVersion == BuildVersion::kVersion);
	CHECK(file.header.buildCommit == BuildVersion::kCommit);
	CHECK(file.header.buildDate == BuildVersion::kBuildDate);
}

TEST_CASE("replay open: a continued saved recording preserves build provenance and its original commands")
{
	const auto h = header();
	const auto log = recording(h);
	ReplayWriter continued;
	continued.setKeepCopy(true);
	std::string error;
	REQUIRE_MESSAGE(continued.openContinuing("", log, &error), error);
	continued.recordHash(2, 0x1234u);
	std::vector<std::uint8_t> bytes;
	REQUIRE_MESSAGE(continued.logUpTo(2, 0x1234u, bytes, &error), error);
	ReplayFile file;
	REQUIRE_MESSAGE(ReplayFile::parse(bytes, file, &error), error);
	CHECK(file.header.buildVersion == h.buildVersion);
	CHECK(file.header.buildCommit == h.buildCommit);
	CHECK(file.header.buildDate == h.buildDate);
	CHECK(file.commandCount() == 1);
	CHECK(file.hashes.at(1) == 0xABCDu);
	CHECK(file.finalFrame == 2);
	CHECK(file.finalHash == 0x1234u);
}

TEST_CASE("replay open: formats 5 through 9 remain readable without invented build provenance")
{
	const auto h = header();
	for (std::uint8_t version : { 5, 6, 7, 8, 9 })
	{
		NetByteWriter game;
		NetPacket::writeNewGameMessage(game, h.game);
		auto message = game.data();
		message.resize(message.size() - 4); // lane CHEAT-1: the Cheat Mode i32 that ends the GameInfo came with format 11
		if (version < 7)
		{
			message.resize(message.size() - LEGACY_MAX_SLOTS * 4); // handicaps were appended in format 7 (an eight-slot game keeps the legacy list)
		}
		NetByteWriter payload;
		payload.u32(version);
		payload.str(h.profile.text);
		if (version >= 9)
		{
			payload.str(h.buildVersion);
			payload.str(h.buildCommit);
		}
		std::vector<std::uint8_t> bytes{ 'O', 'B', 'F', 'M', 'E', 'R', 'E', 'P' };
		bytes.insert(bytes.end(), payload.data().begin(), payload.data().end());
		bytes.insert(bytes.end(), message.begin(), message.end());
		NetByteWriter tail;
		tail.i32(h.recordingSlot);
		tail.u32((std::uint32_t)h.algorithm);
		for (int i = 0; i < h.game.game.serializedSlotCount(); ++i) // P16: an eight-slot game keeps the legacy eight-entry list
		{
			tail.i32(h.network.slotPlayerIndex[(size_t)i]);
		}
		tail.i32(h.network.runAhead);
		tail.i32(h.network.crcInterval);
		tail.u8(3);
		tail.u32(0);
		tail.u32(0);
		bytes.insert(bytes.end(), tail.data().begin(), tail.data().end());
		ReplayFile file;
		std::string error;
		REQUIRE_MESSAGE(ReplayFile::parse(bytes, file, &error), error);
		const std::string expectedVersion = version >= 9 ? h.buildVersion : "";
		const std::string expectedCommit = version >= 9 ? h.buildCommit : "";
		CHECK(file.header.buildVersion == expectedVersion);
		CHECK(file.header.buildCommit == expectedCommit);
		CHECK(file.header.buildDate.empty());
		CHECK(file.complete);
	}
}

TEST_CASE("replay open: PR35 hardened disk reads and parsing refuse mutations transactionally")
{
	const auto h = header();
	ReplayWriter writer;
	writer.setKeepCopy(true);
	std::string error;
	REQUIRE_MESSAGE(writer.open("", h, &error), error);
	writer.recordHash(1, 0xABCDu);
	std::vector<std::uint8_t> good;
	REQUIRE_MESSAGE(writer.logUpTo(1, 0xABCDu, good, &error), error);
	const size_t end = good.size() - 9;
	const std::string path = "replay_open_mutations.replay";
	ReplayFile out;
	for (size_t cut = 0; cut < good.size(); ++cut)
	{
		writeFile(path, good, cut);
		out.finalFrame = 123;
		if (cut == end || cut == end - 9)
		{
			// The parser intentionally accepts an interrupted recording at any complete record boundary.
			if (ReplayFile::load(path, out, &error))
			{
				CHECK_FALSE(out.complete);
				continue;
			}
		}
		CHECK_MESSAGE((!ReplayFile::load(path, out, &error) && !error.empty()), "cut at byte ", cut);
		CHECK(out.finalFrame == 123);
	}
	std::filesystem::resize_file(path, ReplayFile::kMaxBytes + 1);
	CHECK_FALSE(ReplayFile::load(path, out, &error));
	CHECK(error == "the replay exceeds the size limit of 268435456 bytes");
	CHECK(out.finalFrame == 123);
	std::remove(path.c_str());
	REQUIRE_MESSAGE(ReplayFile::parse(good, out, &error), error);
	for (const auto change : { std::pair<size_t, std::uint8_t>{ 0, 'X' }, { 8, 255 }, { end, 255 } })
	{
		auto bad = good;
		bad[change.first] = change.second;
		CHECK_FALSE(ReplayFile::parse(bad, out, &error));
		CHECK_FALSE(error.empty());
		CHECK(out.complete);
	}
	auto oversized = good;
	oversized[12] = oversized[13] = oversized[14] = oversized[15] = 255;
	CHECK_FALSE(ReplayFile::parse(oversized, out, &error));
	CHECK(out.complete);
	auto count = good;
	count.insert(count.begin() + (std::ptrdiff_t)end, { 1, 0, 0, 0, 0, 0, 0, 0, 0x80 });
	CHECK_FALSE(ReplayFile::parse(count, out, &error));
	CHECK(error.find("command count 2147483648 exceeds the record's bytes") != std::string::npos);
	auto length = good;
	length.insert(length.begin() + (std::ptrdiff_t)end, { 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x80 });
	CHECK_FALSE(ReplayFile::parse(length, out, &error));
	CHECK(error == "truncated replay record");
	CHECK(out.complete);
}

TEST_CASE("replay open: foreign profiles, unsupported formats and invalid build labels refuse by name")
{
	const auto h = header();
	ReplayFile file;
	std::string error;
	REQUIRE_MESSAGE(ReplayFile::parse(recording(h), file, &error), error);
	CHECK_FALSE(file.profileMismatch(ProfileIdentity::fromText("other profile\n")).empty());
	auto future = recording(h);
	future[8] = 99;
	CHECK_FALSE(ReplayFile::parse(future, file, &error));
	CHECK(error == "replay version 99 (this build reads 5 .. 12)");
	auto bad = h;
	bad.buildVersion = "bad\nversion";
	CHECK_FALSE(ReplayFile::parse(recording(bad), file, &error));
	CHECK(error == "invalid replay recording build");
	bad = h;
	bad.buildDate = "2026-13-99";
	CHECK_FALSE(ReplayFile::parse(recording(bad), file, &error));
	CHECK(error == "invalid replay recording build");
}

TEST_CASE("save route: dated synthetic save and replay refusals name the build and launcher; the current build still loads")
{
	auto h = header();
	h.buildVersion = "v0.4.12-nightly.20261022";
	ReplayFile replay;
	std::string error;
	REQUIRE_MESSAGE(ReplayFile::parse(recording(h), replay, &error), error);
	CHECK(replay.loadRefusal(h.profile).empty());
	const auto other = ProfileIdentity::fromText("other build and archive order\n");
	const std::string expected = "Made with Nightly 0.4.12 (2026-10-22). Open it with that version from the launcher.";
	CHECK(replay.loadRefusal(other) == expected);
	SaveGameFile save;
	save.header.buildVersion = h.buildVersion;
	save.header.buildDate = h.buildDate;
	save.header.commit = h.buildCommit;
	save.header.engineId = ProfileIdentity::buildId();
	save.header.profile = h.profile;
	save.header.executionProfile = SaveGameFile::executionProfileOf(h.profile.text);
	save.header.date = "2030-01-02"; // Save date is not the build date.
	save.header.frame = 1;
	save.header.stateHash = 0xABCDu;
	save.log = recording(h);
	SaveGameFile loaded;
	REQUIRE_MESSAGE(SaveGameFile::parse(save.encode(), loaded, &error), error);
	CHECK(loaded.header.buildDate == "2026-10-22");
	CHECK(SaveGameCompatibility::check(loaded.header, h.profile, loaded.header.engineId, h.buildVersion, false).loadable);
	CommandLogLoad load;
	REQUIRE_MESSAGE(load.prepare(loaded, &error), error);
	const auto verdict = SaveGameCompatibility::check(loaded.header, other, "new-engine", "new-version", false);
	CHECK_FALSE(verdict.loadable);
	CHECK(verdict.refusal == expected);
	CHECK(SaveGameCompatibility::check(loaded.header, other, loaded.header.engineId, h.buildVersion, false).refusal ==
		expected + " The game data or rules differ from this build.");
	if (const char *path = std::getenv("OPENBFME_SAVE_FIXTURE"))
	{
		save.header.engineId = "synthetic-old-engine";
		REQUIRE_MESSAGE(save.write(path, &error), error);
	}
	loaded.header.buildDate.clear();
	CHECK(SaveGameCompatibility::check(loaded.header, other, "new-engine", "new-version", false).refusal.find("build date unavailable") != std::string::npos);
	save.header.buildDate = "2026-99-01";
	CHECK_FALSE(SaveGameFile::parse(save.encode(), loaded, &error));
	CHECK(error == "invalid save recording build date");
}

TEST_CASE("save route: only routed release versions and build dates enter refusal labels")
{
	const std::string unknown = "Made with an unknown build. Open it with the build that made it from the launcher.";
	for (const std::string version : { "", "dev", "/forged/build/label", "Z:\\recordings\\forged", "v1.2.3\n", "v1.2.3\t", "v1.2.3 ",
		"v01.2.3", "v1.2.3-beta.1", "v1.2.3+meta", "v1.2.3-alpha.01", "v1.2.3-nightly.-1", "v1.2.3-nightly.1000000000",
		"v1000000000.2.3", "v1.2.3-4-g0123456789", "Made with <junk>" })
	{
		CAPTURE(version);
		CHECK_FALSE(BuildVersion::isReleaseVersion(version));
		CHECK(RecordingBuild::openWith(version, "2026-10-22") == unknown);
	}
	for (const std::string version : { "v0.0.0", "v1.2.3", "v0.4.0-alpha.4", "v0.3.0-preview.4", "v0.4.12-nightly.20261022", "v1.2.3-nightly.7" })
	{
		CAPTURE(version);
		CHECK(BuildVersion::isReleaseVersion(version));
		CHECK(RecordingBuild::openWith(version, "2026-10-22").find("an unknown build") == std::string::npos);
		CHECK(RecordingBuild::openWith(version, "").find("build date unavailable") != std::string::npos);
	}
	for (const std::string date : { "/not/date", "2026-01-01\n", "2026-00-01", "2026-01-00", "2026-13-01", "2026-01-32", "2026-1-01", "2026-xx-01" })
		CHECK(RecordingBuild::openWith("v1.2.3", date) == unknown);

	auto h = header();
	h.buildVersion = "/forged/build/label";
	ReplayFile replay;
	std::string error;
	REQUIRE_MESSAGE(ReplayFile::parse(recording(h), replay, &error), error);
	const auto other = ProfileIdentity::fromText("another build\n");
	CHECK(replay.loadRefusal(other) == unknown);
	SaveGameFile save;
	save.header.buildVersion = h.buildVersion;
	save.header.buildDate = h.buildDate;
	save.header.engineId = "old-engine";
	save.header.profile = h.profile;
	SaveGameFile parsed;
	REQUIRE_MESSAGE(SaveGameFile::parse(save.encode(), parsed, &error), error);
	CHECK(SaveGameCompatibility::check(parsed.header, other, "new-engine", "v1.2.3", false).refusal == unknown);
}
