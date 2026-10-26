// OpenBFME asset-free save, snapshot and replay reader fuzzing. GPL-3.0.

#include "Common/GameState.h"
#include "Common/Recorder.h"
#include "Common/Snapshot.h"
#include "Common/System/SaveGame/SaveGameFile.h"
#include "Common/Xfer.h"
#include "GameClient/CommandLogLoad.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#ifdef OPENBFME_READER_CORPUS
#include <filesystem>
#include <fstream>
#include <iostream>
#endif

namespace
{
struct ValueBlock : Snapshot
{
	std::int32_t value = 17;
	void crc(Xfer *x) override { xfer(x); }
	void loadPostProcess() override {}
	void xfer(Xfer *x) override
	{
		XferVersion version{ 1, 1 };
		x->xferVersion(version);
		x->xferInt(&value);
	}
};

struct Fixture
{
	GameState state;
	ValueBlock value;
	Fixture() { state.addSnapshotBlock("test:Value", &value, 1u << SNAPSHOT_SAVELOAD, "fuzzer", { "value" }); }
};

ReplayHeader replayHeader()
{
	ReplayHeader h;
	h.profile = ProfileIdentity::fromText(std::string("openbfme-profile 1\nprofile enhanced\nengine ") + ProfileIdentity::buildId()
		+ "\nrng ZH_CarryChain\nmods none\narchive 0 rotwk:INI.big 0123456789abcdef0123456789abcdef 100\n");
	h.game.game.mapName = "maps/synthetic/synthetic.map";
	h.game.game.seed = 1;
	h.game.game.slots[0].state = SLOT_PLAYER;
	h.game.game.slots[0].name = u"Human";
	return h;
}

std::vector<std::uint8_t> replaySeed()
{
	ReplayWriter writer;
	writer.setKeepCopy(true);
	std::string error;
	if (!writer.open("", replayHeader(), &error))
	{
		return {};
	}
	writer.recordHash(1, 0x12345678u);
	std::vector<std::uint8_t> bytes;
	writer.logUpTo(1, 0x12345678u, bytes, &error);
	return bytes;
}

SaveGameFile saveSeed()
{
	SaveGameFile save;
	save.header.buildVersion = "fuzz-seed";
	save.header.engineId = ProfileIdentity::buildId();
	save.header.profile = replayHeader().profile;
	save.header.executionProfile = "enhanced";
	save.header.gameKind = 2;
	save.header.frame = 1;
	save.header.stateHash = 0x12345678u;
	save.log = replaySeed();
	Fixture fixture;
	std::string error;
	fixture.state.saveSnapshot({ "value" }, save.snapshot, &error);
	return save;
}
} // namespace

#ifdef OPENBFME_READER_CORPUS
int main(int argc, char **argv)
{
	if (argc != 2)
	{
		return 1;
	}
	std::filesystem::create_directories(argv[1]);
	auto save = saveSeed().encode();
	auto replay = replaySeed();
	std::string error;
	SaveGameFile parsedSave;
	ReplayFile parsedReplay;
	if (!SaveGameFile::parse(save, parsedSave, &error) || !ReplayFile::parse(replay, parsedReplay, &error) || !parsedReplay.complete)
	{
		std::cerr << "invalid synthetic reader seed: " << error << '\n';
		return 1;
	}
	for (const auto &entry : { std::pair<const char *, std::vector<std::uint8_t>>{ "save", save }, { "replay", replay },
			 { "empty", {} } })
	{
		std::ofstream out(std::filesystem::path(argv[1]) / entry.first, std::ios::binary);
		const std::uint8_t selector = std::strcmp(entry.first, "replay") == 0 ? 1 : 0;
		out.put((char)selector);
		out.write((const char *)entry.second.data(), (std::streamsize)entry.second.size());
		if (!out)
		{
			return 1;
		}
	}
	return 0;
}
#else
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data, size_t size)
{
	if (size == 0 || size > (1u << 16))
	{
		return 0;
	}
	const bool replay = data[0] & 1;
	const std::vector<std::uint8_t> bytes(data + 1, data + size);
	std::string error;
	if (replay)
	{
		ReplayFile parsed;
		ReplayFile::parse(bytes, parsed, &error);
		auto wrapped = replaySeed();
		if (wrapped.size() >= 9)
		{
			wrapped.insert(wrapped.end() - 9, bytes.begin(), bytes.end());
			ReplayFile::parse(wrapped, parsed, &error);
		}
	}
	else
	{
		SaveGameFile parsed;
		SaveGameFile::parse(bytes, parsed, &error);
		SaveGameFile::parse(bytes, parsed, &error, true);
		Fixture fixture;
		fixture.state.checkFraming(bytes, &error);
		fixture.state.loadSnapshot({ "value" }, bytes, &error);
		auto seed = saveSeed();
		seed.log = bytes;
		seed.snapshot = bytes;
		if (SaveGameFile::parse(seed.encode(), parsed, &error))
		{
			CommandLogLoad load;
			load.prepare(parsed, &error);
		}
	}
	return 0;
}
#endif
