// OpenBFME unit tests. GPL-3.0.
// Lane SAVE-2 (docs/SAVES.md): the save container (Common/System/SaveGame/SaveGameFile.h) and its SHA-256, every failure mode with its named error (T5), the
// load rules (another build refused by name, other data refused with the differing line, a save of the other execution profile loadable in single player
// only), and the recorder's save hooks (ReplayWriter::logUpTo / openContinuing). No game files needed.

#include "doctest.h"

#include "Common/Recorder.h"
#include "Common/Sha256.h"
#include "Common/RecordingBuild.h"
#include "Common/System/SaveGame/SaveGameFile.h"
#include "GameClient/CommandLogLoad.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace
{
std::string hexOf(const std::string &s)
{
	const std::array<std::uint8_t, 32> d = Sha256::hash(s.data(), s.size());
	return HexBytes(d.data(), d.size());
}

ProfileIdentity identity(const std::string &profileLine, const std::string &archiveMd5 = "0123456789abcdef0123456789abcdef")
{
	return ProfileIdentity::fromText("openbfme-profile 1\nprofile " + profileLine + "\nengine " + ProfileIdentity::buildId()
		+ "\nrng ZH_CarryChain\nmods none\narchive 0 rotwk:INI.big " + archiveMd5 + " 100\n");
}

SaveGameFile sample()
{
	SaveGameFile f;
	SaveGameHeader &h = f.header;
	h.buildVersion = "v0.3.0-test";
	h.engineId = ProfileIdentity::buildId();
	h.commit = "abc";
	h.profile = identity("enhanced");
	h.executionProfile = "enhanced";
	h.gameKind = 2;
	h.frame = 321;
	h.stateHash = 0xDEADBEEFu;
	h.description = "Battle at Evendim";
	h.mapPath = "maps/map mp evendim/map mp evendim.map";
	h.mapDisplay = "Evendim";
	h.date = "2026-10-12";
	h.time = "13:14:15";
	h.commands = 7;
	f.log = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
	return f;
}

std::string parseError(const std::vector<std::uint8_t> &bytes, bool headerOnly = false)
{
	SaveGameFile out;
	std::string error;
	if (SaveGameFile::parse(bytes, out, &error, headerOnly))
	{
		return "(parsed)";
	}
	return error;
}

// the offset of a part's tag in an encoded container (8 magic + 4 version, then tag / u32 length / 32 digest / payload)
size_t partOffset(const std::vector<std::uint8_t> &bytes, int index)
{
	size_t pos = 12;
	for (int i = 0; i < index; ++i)
	{
		std::uint32_t length = 0;
		std::memcpy(&length, bytes.data() + pos + 4, 4);
		pos += 40 + length;
	}
	return pos;
}

NewGameMessage smallGame()
{
	NewGameMessage m;
	m.game.mapName = "maps/map mp evendim/map mp evendim.map";
	m.game.seed = 7;
	m.game.slots[0].state = SLOT_PLAYER;
	m.game.slots[0].name = u"Human";
	return m;
}

std::vector<std::uint8_t> readAll(const std::string &path)
{
	std::ifstream in(path, std::ios::binary);
	return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}
} // namespace

TEST_CASE("save2: the SHA-256 of the save parts (ONLINE-2's Common/Sha256) gives the FIPS 180-4 test vectors")
{
	CHECK(hexOf("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
	CHECK(hexOf("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
	CHECK(hexOf("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
	const std::string million(1000000, 'a');
	CHECK(hexOf(million) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
	// fed in pieces that straddle the 64-byte blocks: the same digest
	Sha256 s;
	for (size_t i = 0; i < million.size(); i += 777)
	{
		s.update(million.data() + i, std::min<size_t>(777, million.size() - i));
	}
	const std::array<std::uint8_t, 32> d = s.finish();
	CHECK(HexBytes(d.data(), d.size()) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

TEST_CASE("save2: the container writes its header, log and empty snapshot and reads them back field by field")
{
	const SaveGameFile f = sample();
	const std::vector<std::uint8_t> bytes = f.encode();
	REQUIRE(bytes.size() > 12); // tolerance: a guard before the parts' tags are read (the exact layout is checked below, partOffset(bytes, 3) == size)
	CHECK(std::string((const char *)bytes.data(), 8) == "OBFMESAV");
	CHECK(std::string((const char *)bytes.data() + partOffset(bytes, 0), 4) == "HEAD");
	CHECK(std::string((const char *)bytes.data() + partOffset(bytes, 1), 4) == "LOG ");
	CHECK(std::string((const char *)bytes.data() + partOffset(bytes, 2), 4) == "SNAP");
	CHECK(partOffset(bytes, 3) == bytes.size()); // the empty snapshot is the last part
	SaveGameFile back;
	std::string error;
	REQUIRE_MESSAGE(SaveGameFile::parse(bytes, back, &error), error);
	const SaveGameHeader &h = back.header;
	CHECK(h.buildVersion == "v0.3.0-test");
	CHECK(h.engineId == ProfileIdentity::buildId());
	CHECK(h.commit == "abc");
	CHECK(h.profile == f.header.profile);
	CHECK(h.executionProfile == "enhanced");
	CHECK(h.gameKind == 2);
	CHECK(h.frame == 321u);
	CHECK(h.stateHash == 0xDEADBEEFu);
	CHECK(h.description == "Battle at Evendim");
	CHECK(h.mapPath == "maps/map mp evendim/map mp evendim.map");
	CHECK(h.mapDisplay == "Evendim");
	CHECK(h.date == "2026-10-12");
	CHECK(h.time == "13:14:15");
	CHECK(h.commands == 7u);
	CHECK(h.campaign.empty());
	// a campaign mission's start travels in the header
	SaveGameFile mission = sample();
	mission.header.gameKind = 1;
	mission.header.campaign = "ANGMAR_CAMPAIGN";
	mission.header.campaignMission = 3;
	mission.header.campaignDifficulty = 2;
	SaveGameFile missionBack;
	REQUIRE_MESSAGE(SaveGameFile::parse(mission.encode(), missionBack, &error), error);
	CHECK(missionBack.header.campaign == "ANGMAR_CAMPAIGN");
	CHECK(missionBack.header.campaignMission == 3);
	CHECK(missionBack.header.campaignDifficulty == 2);
	CHECK(back.log == f.log);
	CHECK(back.snapshot.empty());
	// the listing reads the header only: the log is not kept
	SaveGameFile listed;
	REQUIRE(SaveGameFile::parse(bytes, listed, &error, true));
	CHECK(listed.header.description == "Battle at Evendim");
	CHECK(listed.log.empty());
	// the identity's profile line
	CHECK(SaveGameFile::executionProfileOf(identity("uber").text) == "uber");
	CHECK(SaveGameFile::executionProfileOf("openbfme-profile 1\n").empty());
	// written through a temporary file and renamed over the target; an existing save is replaced
	const std::string path = "save2_container.obfmesave";
	REQUIRE_MESSAGE(f.write(path, &error), error);
	SaveGameFile other = sample();
	other.header.description = "second";
	REQUIRE_MESSAGE(other.write(path, &error), error);
	SaveGameFile loaded;
	REQUIRE_MESSAGE(SaveGameFile::load(path, loaded, &error), error);
	CHECK(loaded.header.description == "second");
	CHECK(readAll(path + ".tmp").empty()); // no temporary file is left behind
	std::remove(path.c_str());
}

TEST_CASE("save2 T5: a damaged, truncated or foreign file is refused with its named error, never read as a default")
{
	const std::vector<std::uint8_t> good = sample().encode();
	SUBCASE("not a save")
	{
		std::vector<std::uint8_t> b = good;
		b[0] = 'X';
		CHECK(parseError(b) == "not an OpenBFME save (the file does not start with OBFMESAV)");
		CHECK(parseError({}) == "not an OpenBFME save (the file does not start with OBFMESAV)");
	}
	SUBCASE("truncated anywhere")
	{
		CHECK(parseError(std::vector<std::uint8_t>(good.begin(), good.begin() + 10)) == "the save is truncated (no container version)");
		CHECK(parseError(std::vector<std::uint8_t>(good.begin(), good.begin() + 30)) == "the save is truncated (the header part is missing)");
		const size_t logAt = partOffset(good, 1);
		CHECK(parseError(std::vector<std::uint8_t>(good.begin(), good.begin() + (std::ptrdiff_t)(logAt + 40 + 4)))
			== "the save is truncated (the command log part has 4 of its 10 bytes)");
		const size_t snapAt = partOffset(good, 2);
		CHECK(parseError(std::vector<std::uint8_t>(good.begin(), good.begin() + (std::ptrdiff_t)snapAt)) == "the save is truncated (the snapshot part is missing)");
		// every shorter length fails (none parses)
		int parsed = 0;
		for (size_t n = 0; n < good.size(); ++n)
		{
			parsed += parseError(std::vector<std::uint8_t>(good.begin(), good.begin() + (std::ptrdiff_t)n)) == "(parsed)" ? 1 : 0;
		}
		CHECK(parsed == 0);
	}
	SUBCASE("a changed byte in a part")
	{
		std::vector<std::uint8_t> b = good;
		b[partOffset(good, 1) + 40 + 3] ^= 0x40;
		CHECK(parseError(b) == "the save is damaged (the command log part's SHA-256 does not match its content)");
		CHECK(parseError(b, true) == "(parsed)"); // the listing reads the header only; the load names it
		std::vector<std::uint8_t> h = good;
		h[partOffset(good, 0) + 40 + 10] ^= 0x01;
		CHECK(parseError(h, true) == "the save is damaged (the header part's SHA-256 does not match its content)");
	}
	SUBCASE("another container version")
	{
		std::vector<std::uint8_t> b = good;
		b[8] = 2;
		CHECK(parseError(b) == "save container version 2 (this build reads version 1)");
	}
	SUBCASE("a part out of order, bytes after the end")
	{
		std::vector<std::uint8_t> b = good;
		std::memcpy(b.data() + partOffset(good, 1), "SNAP", 4);
		CHECK(parseError(b) == "the save is damaged (expected the command log part 'LOG ', found 'SNAP')");
		std::vector<std::uint8_t> t = good;
		t.push_back(0);
		t.push_back(0);
		t.push_back(0);
		CHECK(parseError(t) == "the save is damaged (3 bytes after its last part)");
	}
	SUBCASE("a snapshot (SAVE-4), an unknown game kind, a header of another version")
	{
		// lane SAVE-4a: the container reads the snapshot part. Lane SAVE-4e: a snapshot no longer refuses the save; its log is still checked first
		// (this sample's is not a replay), and its bytes when the game takes them (test_save2_retail.cpp's save4e cases)
		SaveGameFile s = sample();
		s.snapshot = { 1, 2, 3, 4 };
		SaveGameFile withSnapshot;
		std::string snapError;
		REQUIRE_MESSAGE(SaveGameFile::parse(s.encode(), withSnapshot, &snapError), snapError);
		CHECK(withSnapshot.snapshot == s.snapshot);
		CommandLogLoad snapLoad;
		CHECK_FALSE(snapLoad.prepare(withSnapshot, &snapError));
		CHECK(snapError.rfind("the save's command log is damaged: ", 0) == 0);
		CHECK_FALSE(snapLoad.hasSnapshot());
		SaveGameFile k = sample();
		k.header.gameKind = 4;
		CHECK(parseError(k.encode()) == "the save's game kind 4 is not one this build saves (1 Campaign, 2 Skirmish)");
		SaveGameFile c = sample();
		c.header.gameKind = 1;
		CHECK(parseError(c.encode()) == "the save is a campaign mission without its campaign");
		SaveGameFile sk = sample();
		sk.header.campaign = "ANGMAR_CAMPAIGN";
		CHECK(parseError(sk.encode()) == "the save is a skirmish that names a campaign");
		std::vector<std::uint8_t> v = good;
		const size_t head = partOffset(good, 0);
		v[head + 40] = 4; // unknown header version, with a valid digest
		const std::array<std::uint8_t, 32> d = Sha256::hash(v.data() + head + 40, (size_t)(partOffset(good, 1) - head - 40));
		std::memcpy(v.data() + head + 8, d.data(), 32);
		CHECK(parseError(v) == "save header version 4 (this build reads versions 1, 2 and 3)");
	}
	SUBCASE("a missing file")
	{
		SaveGameFile out;
		std::string error;
		CHECK_FALSE(SaveGameFile::load("save2_no_such_file.obfmesave", out, &error));
		CHECK(error == "cannot open the save save2_no_such_file.obfmesave");
	}
}

TEST_CASE("save2 T5: no part is allocated before its declared size is checked: an oversized header or log is a named refusal, the listing reads the "
		  "header only")
{
	std::string error;
	// a header part declaring more than its limit, present in full: refused by name, not read
	{
		SaveGameFile big = sample();
		big.header.description = std::string(2u << 20, 'x');
		const std::vector<std::uint8_t> bytes = big.encode();
		std::uint32_t declared = 0;
		std::memcpy(&declared, bytes.data() + partOffset(bytes, 0) + 4, 4);
		CHECK(parseError(bytes) == "the save's header part declares " + std::to_string(declared) + " bytes (at most 1048576)");
	}
	// a file whose command log part declares 300 MiB (the file is that large, sparse): the load refuses it by name before allocating, the header-only
	// listing reads the header and only the parts' heads
	const std::vector<std::uint8_t> good = sample().encode();
	const size_t logAt = partOffset(good, 1);
	const std::uint32_t declared = 300u << 20;
	std::vector<std::uint8_t> head(good.begin(), good.begin() + (std::ptrdiff_t)(logAt + 40));
	std::memcpy(head.data() + logAt + 4, &declared, 4);
	const std::vector<std::uint8_t> snapHead(good.begin() + (std::ptrdiff_t)partOffset(good, 2), good.end());
	const std::string path = "save2_large.obfmesave";
	{
		std::ofstream o(path, std::ios::binary | std::ios::trunc);
		o.write((const char *)head.data(), (std::streamsize)head.size());
	}
	std::filesystem::resize_file(path, head.size() + declared);
	{
		std::ofstream o(path, std::ios::binary | std::ios::app);
		o.write((const char *)snapHead.data(), (std::streamsize)snapHead.size());
	}
	SaveGameFile f;
	CHECK_FALSE(SaveGameFile::load(path, f, &error));
	CHECK(error == "the save's command log part declares 314572800 bytes (at most 268435456)");
	SaveGameFile listed;
	REQUIRE_MESSAGE(SaveGameFile::load(path, listed, &error, true), error);
	CHECK(listed.header.description == "Battle at Evendim");
	CHECK(listed.log.empty());
	// a log declaring 4 GiB in a small file: truncated, named, nothing allocated
	std::vector<std::uint8_t> lying = good;
	const std::uint32_t huge = 0xFFFFFFF0u;
	std::memcpy(lying.data() + logAt + 4, &huge, 4);
	{
		std::ofstream o(path, std::ios::binary | std::ios::trunc);
		o.write((const char *)lying.data(), (std::streamsize)lying.size());
	}
	CHECK_FALSE(SaveGameFile::load(path, f, &error, true));
	CHECK(error == "the save is truncated (the command log part has " + std::to_string(lying.size() - logAt - 40) + " of its 4294967280 bytes)");
	std::remove(path.c_str());
}

TEST_CASE("save reader: malformed part boundaries, tags, sizes, versions and foreign identities are refused by name")
{
	const std::vector<std::uint8_t> good = sample().encode();
	const size_t head = partOffset(good, 0), log = partOffset(good, 1), snap = partOffset(good, 2);
	REQUIRE(parseError(good) == "(parsed)");
	for (size_t cut : { size_t(0), size_t(8), size_t(12), head + 4, head + 8, head + 40, log - 1, log, log + 4, log + 8,
		log + 40, snap - 1, snap, snap + 4, snap + 8, snap + 40, good.size() - 1 })
	{
		if (cut == good.size())
		{
			continue; // the synthetic save has an empty SNAP part
		}
		const std::string error = parseError(std::vector<std::uint8_t>(good.begin(), good.begin() + (std::ptrdiff_t)cut));
		CHECK_MESSAGE((!error.empty() && error != "(parsed)"), "truncation at byte ", cut);
	}
	for (const auto &change : { std::pair<size_t, char>{ log, 'X' }, { log, 'H' }, { snap, 'H' } })
	{
		auto bad = good;
		bad[change.first] = (std::uint8_t)change.second;
		CHECK(parseError(bad).find("expected the ") != std::string::npos);
	}
	for (const auto &part : { std::pair<size_t, size_t>{ head, log }, { log, snap }, { snap, good.size() } })
	{
		auto missing = good;
		missing.erase(missing.begin() + (std::ptrdiff_t)part.first, missing.begin() + (std::ptrdiff_t)part.second);
		CHECK(parseError(missing) != "(parsed)");
		auto duplicate = good;
		duplicate.insert(duplicate.begin() + (std::ptrdiff_t)part.first, good.begin() + (std::ptrdiff_t)part.first,
			good.begin() + (std::ptrdiff_t)part.second);
		CHECK(parseError(duplicate) != "(parsed)");
	}
	for (size_t offset : { head, log, snap })
	{
		auto bad = good;
		const std::uint32_t huge = 1u << 31;
		std::memcpy(bad.data() + offset + 4, &huge, 4);
		CHECK(parseError(bad).find("declares 2147483648 bytes") != std::string::npos);
		CHECK(parseError(bad, true).find(offset == head ? "declares 2147483648 bytes" : "of its 2147483648 bytes") != std::string::npos);
		bad = good;
		std::fill_n(bad.begin() + (std::ptrdiff_t)offset + 4, 4, offset == snap ? 1 : 0);
		CHECK(parseError(bad) != "(parsed)");
	}
	auto version = good;
	version[head + 40] = 99;
	const auto digest = Sha256::hash(version.data() + head + 40, log - head - 40);
	std::memcpy(version.data() + head + 8, digest.data(), digest.size());
	CHECK(parseError(version).find("save header version 99") != std::string::npos);
	SaveGameFile foreign = sample();
	foreign.header.engineId = "another-engine-identity";
	SaveGameFile read;
	std::string error;
	REQUIRE_MESSAGE(SaveGameFile::parse(foreign.encode(), read, &error), error);
	const SaveGameCompatibility compatible = SaveGameCompatibility::check(read.header, identity("enhanced"), ProfileIdentity::buildId(), "v0.3.0-test", false);
	CHECK_FALSE(compatible.loadable);
	CHECK(compatible.refusal == RecordingBuild::openWith(read.header.buildVersion, read.header.buildDate)); // another build: named by its release, not its engine id
	CHECK_FALSE(parseError({}).empty());
	read.header.description = "unchanged";
	auto damaged = good;
	damaged[log] = 'X';
	CHECK_FALSE(SaveGameFile::parse(damaged, read, &error));
	CHECK(read.header.description == "unchanged");
	const std::string path = "save2_damaged.obfmesave";
	{
		std::ofstream file(path, std::ios::binary);
		file.write((const char *)damaged.data(), (std::streamsize)damaged.size());
		REQUIRE(file.good());
	}
	CHECK_FALSE(SaveGameFile::load(path, read, &error));
	CHECK_FALSE(error.empty());
	CHECK(read.header.description == "unchanged");
	std::remove(path.c_str());
}

TEST_CASE("save reader: sparse large and maximum replay frame numbers have bounded coverage checks")
{
	for (const UnsignedInt frame : { 1u << 31, 0xFFFFFFFFu })
	{
		SaveGameFile save = sample();
		save.header.frame = frame;
		save.header.stateHash = 0x1BADB002u;
		ReplayHeader h;
		h.game = smallGame();
		h.profile = save.header.profile;
		ReplayWriter writer;
		writer.setKeepCopy(true);
		std::string error;
		REQUIRE_MESSAGE(writer.open("", h, &error), error);
		writer.recordHash(1, save.header.stateHash);
		REQUIRE_MESSAGE(writer.logUpTo(1, save.header.stateHash, save.log, &error), error);
		std::memcpy(save.log.data() + save.log.size() - 8, &frame, 4);
		CommandLogLoad load;
		CHECK_FALSE(load.prepare(save, &error));
		CHECK(error == "the save's command log has no state hash of frame 2");
	}
	SaveGameFile save = sample();
	save.header.frame = 0xFFFFFFFFu;
	save.header.stateHash = 0x1BADB002u;
	ReplayHeader h;
	h.game = smallGame();
	h.profile = save.header.profile;
	ReplayWriter writer;
	writer.setKeepCopy(true);
	std::string error;
	REQUIRE_MESSAGE(writer.open("", h, &error), error);
	writer.recordHash(save.header.frame, save.header.stateHash);
	REQUIRE_MESSAGE(writer.logUpTo(save.header.frame, save.header.stateHash, save.log, &error), error);
	CommandLogLoad load;
	CHECK_MESSAGE(load.prepare(save, &error), error);
}

TEST_CASE("replay reader: disk truncations, unknown records, oversized fields and foreign profiles have named errors")
{
	ReplayHeader h;
	h.game = smallGame();
	h.profile = identity("enhanced");
	ReplayWriter writer;
	writer.setKeepCopy(true);
	std::string error;
	REQUIRE_MESSAGE(writer.open("", h, &error), error);
	writer.recordHash(1, 0xABCDu);
	std::vector<std::uint8_t> good;
	REQUIRE_MESSAGE(writer.logUpTo(1, 0xABCDu, good, &error), error);
	const size_t end = good.size() - 9;
	const std::string path = "save_reader_robustness.replay";
	ReplayFile out;
	for (size_t cut = 0; cut < good.size(); ++cut)
	{
		std::ofstream file(path, std::ios::binary | std::ios::trunc);
		file.write((const char *)good.data(), (std::streamsize)cut);
		file.close();
		out.finalFrame = 123;
		if (cut == end || cut == end - 9)
		{
			REQUIRE_MESSAGE(ReplayFile::load(path, out, &error), error);
			CHECK_FALSE(out.complete); // supported relay/interrupted recordings end between intact records
			continue;
		}
		CHECK_MESSAGE((!ReplayFile::load(path, out, &error) && !error.empty()), "replay truncated at byte ", cut);
		CHECK(out.finalFrame == 123);
	}
	std::filesystem::resize_file(path, ReplayFile::kMaxBytes + 1);
	out.finalFrame = 123;
	CHECK_FALSE(ReplayFile::load(path, out, &error));
	CHECK(error == "the replay exceeds the size limit of 268435456 bytes");
	CHECK(out.finalFrame == 123);
	std::remove(path.c_str());
	REQUIRE_MESSAGE(ReplayFile::parse(good, out, &error), error);
	CHECK(out.complete);
	for (std::vector<std::uint8_t> bad : {
		std::vector<std::uint8_t>(good.begin(), good.begin() + 12),
		std::vector<std::uint8_t>(good.begin(), good.end() - 1),
		std::vector<std::uint8_t>(good.begin(), good.end()) })
	{
		bad[8] = 255;
		CHECK_FALSE(ReplayFile::parse(bad, out, &error));
		CHECK_FALSE(error.empty());
	}
	auto unknown = good;
	unknown[end] = 255;
	CHECK_FALSE(ReplayFile::parse(unknown, out, &error));
	CHECK(error == "unknown replay record 255");
	auto count = good;
	count.insert(count.begin() + (std::ptrdiff_t)end, { 1, 0, 0, 0, 0, 0, 0, 0, 0x80 });
	CHECK_FALSE(ReplayFile::parse(count, out, &error));
	CHECK(error.find("command count 2147483648 exceeds the record's bytes") != std::string::npos);
	auto length = good;
	length.insert(length.begin() + (std::ptrdiff_t)end, { 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x80 });
	CHECK_FALSE(ReplayFile::parse(length, out, &error));
	CHECK(error == "truncated replay record");
	CHECK(out.complete); // a failed parse must not replace the previous valid result
	ReplayHeader foreign = h;
	foreign.profile = identity("uber");
	foreign.game.game.simulationProfile = SimulationProfile::Uber;
	ReplayWriter other;
	other.setKeepCopy(true);
	REQUIRE_MESSAGE(other.open("", foreign, &error), error);
	other.recordHash(1, 0xABCDu);
	std::vector<std::uint8_t> otherBytes;
	REQUIRE_MESSAGE(other.logUpTo(1, 0xABCDu, otherBytes, &error), error);
	REQUIRE_MESSAGE(ReplayFile::parse(otherBytes, out, &error), error);
	CHECK_FALSE(out.profileMismatch(h.profile).empty());
}

TEST_CASE("save2: a save loads only in the build that made it (refused by name), with the same data; the other execution profile loads in single player")
{
	const ProfileIdentity running = identity("enhanced");
	SaveGameHeader h = sample().header;
	SUBCASE("the same build and data")
	{
		const SaveGameCompatibility c = SaveGameCompatibility::check(h, running, ProfileIdentity::buildId(), "v0.3.0-test", false);
		CHECK(c.loadable);
		CHECK_FALSE(c.crossProfile);
		CHECK(c.refusal.empty());
	}
	SUBCASE("another build")
	{
		h.engineId = "1111111111112222222222223333333333334444444444445555555555556666";
		h.buildVersion = "v0.2.9-preview.3";
		const std::string ours = ProfileIdentity::buildId();
		const SaveGameCompatibility c = SaveGameCompatibility::check(h, running, ours, "v0.3.0-test", false);
		CHECK_FALSE(c.loadable);
		CHECK(c.refusal == "Made with v0.2.9-preview.3 (build date unavailable). Open it with that version from the launcher.");
	}
	SUBCASE("other game data")
	{
		h.profile = identity("enhanced", "ffffffffffffffffffffffffffffffff");
		const SaveGameCompatibility c = SaveGameCompatibility::check(h, running, ProfileIdentity::buildId(), "v0.3.0-test", false);
		CHECK_FALSE(c.loadable);
		CHECK(c.refusal == "Made with an unknown build. Open it with the build that made it from the launcher. The game data or rules differ from this build.");
	}
	SUBCASE("Uber save, Faithful build, and the reverse: single player only")
	{
		h.profile = identity("uber");
		h.executionProfile = "uber";
		const SaveGameCompatibility c = SaveGameCompatibility::check(h, running, ProfileIdentity::buildId(), "v0.3.0-test", false);
		CHECK(c.loadable);
		CHECK(c.crossProfile);
		CHECK(c.note == "saved under the uber profile: the saved game is replayed under it, then goes on under enhanced (this player's profile)");
		// the profile at F decides, not the one the log started under (a loaded Uber save that went on under Faithful)
		SaveGameHeader switched = h;
		switched.executionProfile = "enhanced";
		CHECK_FALSE(SaveGameCompatibility::check(switched, running, ProfileIdentity::buildId(), "v0.3.0-test", false).crossProfile);
		const SaveGameCompatibility r = SaveGameCompatibility::check(sample().header, identity("uber"), ProfileIdentity::buildId(), "v0.3.0-test", false);
		CHECK(r.loadable);
		CHECK(r.crossProfile);
		const SaveGameCompatibility mp = SaveGameCompatibility::check(h, running, ProfileIdentity::buildId(), "v0.3.0-test", true);
		CHECK_FALSE(mp.loadable);
		CHECK(mp.refusal == "this game was saved under the uber execution profile; a multiplayer game needs the same profile on every player (this one runs "
							"enhanced)");
	}
}

TEST_CASE("save2: the recorder gives the log up to the saved frame, refuses one it cannot give, and a loaded game's recording continues the saved log")
{
	const std::string path = "save2_writer.replay", path2 = "save2_writer_continued.replay";
	ReplayHeader header;
	header.game = smallGame();
	header.profile = identity("enhanced");
	std::string error;
	std::vector<std::uint8_t> log;
	{
		ReplayWriter plain;
		REQUIRE_MESSAGE(plain.open(path, header, &error), error);
		CHECK_FALSE(plain.logUpTo(0, 0, log, &error));
		CHECK(error == "the game is not being recorded, so it cannot be saved");
		plain.close(0, 0);
	}
	ReplayWriter w;
	w.setKeepCopy(true);
	REQUIRE_MESSAGE(w.open(path, header, &error), error);
	CommandList commands;
	GameMessage m(MSG_DO_MOVETO, 0);
	m.appendLocationArgument(Coord3D{ 1.0f, 2.0f, 3.0f });
	commands.append(m);
	w.recordFrame(1, commands);
	w.recordHash(2, 0x1234u);
	w.recordFrame(2, commands);
	w.recordHash(3, 0x5678u);
	CHECK_FALSE(w.logUpTo(2, 0x1234u, log, &error)); // frame 2's batch is already recorded
	CHECK(error == "the recording already holds frame 2, after the frame 2 being saved");
	CHECK_FALSE(w.logUpTo(3, 0x9999u, log, &error));
	CHECK(error == "the recording has no state hash of frame 3 equal to the game's");
	REQUIRE_MESSAGE(w.logUpTo(3, 0x5678u, log, &error), error);
	ReplayFile saved;
	REQUIRE_MESSAGE(ReplayFile::parse(log, saved, &error), error);
	CHECK(saved.complete);
	CHECK(saved.finalFrame == 3u);
	CHECK(saved.finalHash == 0x5678u);
	CHECK(saved.commandCount() == 2u);
	CHECK(saved.hashes.size() == 2u);
	CHECK(saved.header.profile == header.profile);
	w.close(3, 0x5678u);
	// the loaded game's recording: the saved log without its end record, then the new frames
	ReplayWriter c;
	c.setKeepCopy(true);
	REQUIRE_MESSAGE(c.openContinuing(path2, log, &error), error);
	c.recordFrame(3, commands);
	c.recordHash(4, 0xABCDu);
	std::vector<std::uint8_t> again;
	REQUIRE_MESSAGE(c.logUpTo(4, 0xABCDu, again, &error), error); // a second save holds the whole game
	c.close(4, 0xABCDu);
	ReplayFile whole;
	REQUIRE_MESSAGE(ReplayFile::load(path2, whole, &error), error);
	CHECK(whole.complete);
	CHECK(whole.finalFrame == 4u);
	CHECK(whole.commandCount() == 3u);
	CHECK(whole.hashes.at(2) == 0x1234u);
	CHECK(whole.hashes.at(3) == 0x5678u);
	CHECK(whole.hashes.at(4) == 0xABCDu);
	CHECK(readAll(path2) == again); // the file and the in-memory copy are the same bytes
	// a recording in memory only (a campaign mission): no file, the same log
	ReplayWriter memoryOnly;
	CHECK_FALSE(memoryOnly.open(std::string(), header, &error));
	CHECK(error == "a recording without a file must keep its copy (setKeepCopy)");
	ReplayWriter memory;
	memory.setKeepCopy(true);
	REQUIRE_MESSAGE(memory.open(std::string(), header, &error), error);
	CHECK(memory.isOpen());
	memory.recordFrame(1, commands);
	memory.recordHash(2, 0x1234u);
	std::vector<std::uint8_t> inMemory;
	REQUIRE_MESSAGE(memory.logUpTo(2, 0x1234u, inMemory, &error), error);
	ReplayFile parsedMemory;
	REQUIRE_MESSAGE(ReplayFile::parse(inMemory, parsedMemory, &error), error);
	CHECK(parsedMemory.commandCount() == 1u);
	memory.close(2, 0x1234u);
	CHECK_FALSE(memory.isOpen());
	// a log that is not a complete replay is refused
	ReplayWriter bad;
	CHECK_FALSE(bad.openContinuing("save2_bad.replay", std::vector<std::uint8_t>(log.begin(), log.end() - 1), &error));
	CHECK(error == "the save's log is not a complete replay");
	// a lost command makes the recording unsaveable, named
	ReplayWriter lost;
	lost.setKeepCopy(true);
	REQUIRE(lost.open(path, header, &error));
	CommandList odd;
	odd.append(GameMessage(MSG_DO_MOVETO, 300)); // a player index the encoding cannot carry
	lost.recordFrame(1, odd);
	lost.recordHash(2, 1u);
	REQUIRE_FALSE(lost.errors().empty());
	CHECK_FALSE(lost.logUpTo(2, 1u, log, &error));
	CHECK(error == "the game's recording is incomplete (frame 1 command 0: GameMessage player index 300 out of range), so it cannot be saved");
	lost.close(2, 1u);
	std::remove(path.c_str());
	std::remove(path2.c_str());
}

TEST_CASE("save2: the lane's acceptance stops are reported (S-4000 .. S-4005, SAVE-4a's S-4120 .. S-4122, SAVE-4b's S-4190 .. S-4193, SAVE-4c's S-4220 .. "
		  "S-4222, SAVE-4d's S-4250 .. S-4256 and SAVE-4e's S-5010 .. S-5014)")
{
	const std::vector<std::string> lines = CommandLogLoad::stopLines();
	REQUIRE(lines.size() == 28u);
	const char *ids[28] = { "[S-4000]", "[S-4001]", "[S-4002]", "[S-4003]", "[S-4004]", "[S-4005]", "[S-4120]", "[S-4121]", "[S-4122]", "[S-4190]",
		"[S-4191]", "[S-4192]", "[S-4193]", "[S-4220]", "[S-4221]", "[S-4222]", "[S-4250]", "[S-4251]", "[S-4252]", "[S-4253]", "[S-4254]", "[S-4255]",
		"[S-4256]", "[S-5010]", "[S-5011]", "[S-5012]", "[S-5013]", "[S-5014]" };
	for (size_t i = 0; i < 28; ++i)
	{
		CHECK(lines[i].rfind(ids[i], 0) == 0);
	}
}

namespace
{
// a log as the recorder writes it: `frames` batches from frame 1 (frame 1 with one command), every state hash (or the ones `keep` accepts), ended at F
std::vector<std::uint8_t> makeLog(UnsignedInt frames, const std::function<bool(UnsignedInt)> &keep, std::uint32_t &finalHash)
{
	ReplayHeader header;
	header.game = smallGame();
	header.profile = identity("enhanced");
	ReplayWriter w;
	w.setKeepCopy(true);
	std::string error;
	REQUIRE_MESSAGE(w.open(std::string(), header, &error), error);
	CommandList commands;
	GameMessage m(MSG_DO_MOVETO, 0);
	m.appendLocationArgument(Coord3D{ 1.0f, 2.0f, 3.0f });
	commands.append(m);
	for (UnsignedInt f = 1; f <= frames; ++f)
	{
		if (f == 1)
		{
			w.recordFrame(f, commands);
		}
		if (keep(f + 1))
		{
			w.recordHash(f + 1, 1000u + f);
		}
	}
	finalHash = 1000u + frames;
	std::vector<std::uint8_t> log;
	if (!keep(frames + 1))
	{
		w.recordHash(frames + 1, finalHash); // logUpTo needs the hash of F
	}
	REQUIRE_MESSAGE(w.logUpTo(frames + 1, finalHash, log, &error), error);
	return log;
}

std::string prepareError(const std::vector<std::uint8_t> &log, UnsignedInt frame, std::uint32_t hash)
{
	SaveGameFile save = sample();
	save.header.profile = identity("enhanced");
	save.header.frame = frame;
	save.header.stateHash = hash;
	save.log = log;
	CommandLogLoad cl;
	std::string error;
	return cl.prepare(save, &error) ? "(prepared)" : error;
}

// the 9-byte record `kind`, `a`, `b` removed from (or found in) a log
size_t findRecord(const std::vector<std::uint8_t> &log, std::uint8_t kind, std::uint32_t a, std::uint32_t b)
{
	std::uint8_t rec[9] = { kind };
	std::memcpy(rec + 1, &a, 4);
	std::memcpy(rec + 5, &b, 4);
	for (size_t i = 8; i + 9 <= log.size(); ++i)
	{
		if (std::memcmp(log.data() + i, rec, 9) == 0)
		{
			return i;
		}
	}
	return std::string::npos;
}
} // namespace

TEST_CASE("save2: a save's log has the state hash of every frame up to F, once, and nothing after F (review: a log keeping one hash loaded)")
{
	std::uint32_t finalHash = 0;
	const std::vector<std::uint8_t> full = makeLog(20, [](UnsignedInt) { return true; }, finalHash);
	CHECK(prepareError(full, 21, finalHash) == "(prepared)");
	// a hash missing in the middle
	const std::vector<std::uint8_t> gap = makeLog(20, [](UnsignedInt f) { return f != 11; }, finalHash);
	CHECK(prepareError(gap, 21, finalHash) == "the save's command log has no state hash of frame 11");
	// a duplicate hash record (a recorder never writes one)
	std::vector<std::uint8_t> dup = full;
	const size_t at = findRecord(full, 2, 5, 1004u);
	REQUIRE(at != std::string::npos);
	dup.insert(dup.end() - 9, full.begin() + (std::ptrdiff_t)at, full.begin() + (std::ptrdiff_t)(at + 9));
	CHECK(prepareError(dup, 21, finalHash) == "the save's command log is damaged: duplicate state hash record of frame 5");
	// a hash after F (the end record at 21, a hash of 22 before it)
	std::vector<std::uint8_t> after = full;
	std::uint8_t extra[9] = { 2 };
	const std::uint32_t f22 = 22, h22 = 7;
	std::memcpy(extra + 1, &f22, 4);
	std::memcpy(extra + 5, &h22, 4);
	after.insert(after.end() - 9, extra, extra + 9);
	CHECK(prepareError(after, 21, finalHash) == "the save's command log has a state hash of frame 22, after its end at frame 21");
	// a command record at F
	std::vector<std::uint8_t> late = full;
	const size_t frameRec = findRecord(full, 1, 1, 1u);
	REQUIRE(frameRec != std::string::npos);
	const std::uint32_t f21 = 21;
	std::memcpy(late.data() + frameRec + 1, &f21, 4);
	CHECK(prepareError(late, 21, finalHash) == "the save's command log has a command record of frame 21, at or after its end at frame 21");
}

TEST_CASE("save2: the replay records an execution profile switch, refuses a bad one, and a second record of a frame")
{
	ReplayHeader header;
	header.game = smallGame();
	header.profile = identity("enhanced");
	ReplayWriter w;
	w.setKeepCopy(true);
	std::string error;
	REQUIRE_MESSAGE(w.open(std::string(), header, &error), error);
	w.recordHash(2, 5u);
	w.recordProfileSwitch(2, SimulationProfile::Uber);
	w.recordHash(3, 6u);
	std::vector<std::uint8_t> log;
	REQUIRE_MESSAGE(w.logUpTo(3, 6u, log, &error), error);
	ReplayFile parsed;
	REQUIRE_MESSAGE(ReplayFile::parse(log, parsed, &error), error);
	REQUIRE(parsed.profileSwitches.size() == 1u);
	CHECK(parsed.profileSwitches[0].first == 2u);
	CHECK(parsed.profileSwitches[0].second == SimulationProfile::Uber);
	const size_t at = findRecord(log, 5, 2, 1u);
	REQUIRE(at != std::string::npos);
	std::vector<std::uint8_t> bad = log;
	bad[at + 5] = 7; // profile 7
	CHECK_FALSE(ReplayFile::parse(bad, parsed, &error));
	CHECK(error == "replay profile switch at frame 2 to the unknown profile 7");
	std::vector<std::uint8_t> twice = log;
	twice.insert(twice.end() - 9, log.begin() + (std::ptrdiff_t)at, log.begin() + (std::ptrdiff_t)(at + 9));
	CHECK_FALSE(ReplayFile::parse(twice, parsed, &error));
	CHECK(error == "replay profile switches out of order (frame 2)");
}
