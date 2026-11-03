// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
// See Common/Recorder.h.

#include "Common/Recorder.h"
#include "Common/BuildVersion.h"
#include "Common/PathIO.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>

namespace
{
const char kMagic[8] = { 'O', 'B', 'F', 'M', 'E', 'R', 'E', 'P' };
// WOTR2 milestone 6: 12 adds, after the header's network fields, the War of the Ring battle's start (milestone 7: with its building ids, the
// entries' spawn ids and the players' hero records; format 12 was never released, so it holds them without a new number) (NewGameMessage::livingWorldBattle, which the lobby's
// wire does not carry). Only a battle's recording is written as 12; every other recording stays 11, byte for byte
constexpr std::uint32_t kBattleVersion = 12;
constexpr std::uint32_t kVersion = 11; // 11 (lane CHEAT-1): the GameInfo's Cheat Mode; 10: the build date; 9: the recording build (version, commit); 8 (lane ANTICHEAT-1): the VIEW record; 7 (lane OWNERFIX-2): the GameInfo slots' handicaps; 6 (lane SAVE-2): the execution profile switch record, duplicate records refused; 2: the profile identity, GameMessage format 2 (u32 argument counts), lost-command records; 3 (lane HERO-2): the GameInfo slots' Create-a-Hero records; 4 (lane RULES-1): the GameInfo's game rules and Legendary Heroes factor; 5 (lane UBER-1): the GameInfo's simulation profile
enum : std::uint8_t
{
	RECORD_FRAME = 1,
	RECORD_HASH = 2,
	RECORD_END = 3,
	RECORD_LOST = 4,
	RECORD_PROFILE = 5, // lane SAVE-2
	RECORD_VIEW = 6     // lane ANTICHEAT-1
};
bool failWith(std::string *error, const std::string &why)
{
	if (error)
	{
		*error = why;
	}
	return false;
}
} // namespace

unsigned long long ReplayFile::commandCount() const
{
	unsigned long long n = 0;
	for (const auto &kv : frames)
	{
		n += kv.second.size();
	}
	return n;
}

bool ReplayFile::load(const std::string &path, ReplayFile &out, std::string *error)
{
	try
	{
		std::error_code ec;
		const auto file = PathIO::native(path);
		const std::uintmax_t size = std::filesystem::file_size(file, ec);
		std::ifstream in(file, std::ios::binary);
		if (ec || !in)
		{
			return failWith(error, "cannot open the replay " + path);
		}
		if (size > kMaxBytes)
		{
			return failWith(error, "the replay exceeds the size limit of " + std::to_string(kMaxBytes) + " bytes");
		}
		std::vector<std::uint8_t> bytes((std::size_t)size);
		if (!bytes.empty() && !in.read((char *)bytes.data(), (std::streamsize)bytes.size()))
		{
			return failWith(error, "the replay could not be read (truncated file)");
		}
		ReplayFile parsed;
		if (!parse(bytes, parsed, error))
		{
			return false;
		}
		out = std::move(parsed);
		return true;
	}
	catch (const std::exception &e)
	{
		return failWith(error, std::string("the replay could not be read (") + e.what() + ")");
	}
}

namespace
{
// WOTR2 milestone 6 (format 12): the War of the Ring battle's start, GameInfo.h LivingWorldBattleStart
void writeLivingWorldBattle(NetByteWriter &w, const LivingWorldBattleStart &b)
{
	w.u32((std::uint32_t)b.armies.size()); // milestone 7: the armies' starts first
	for (const LivingWorldBattleStart::ArmyStart &a : b.armies)
	{
		w.i32(a.slot);
		w.i32(a.armyId);
		w.i32(a.state);
		w.i32(a.arrivalFrame);
		w.u32((std::uint32_t)a.spawnPoints.size());
		for (const auto &p : a.spawnPoints)
		{
			w.str(p.first);
			w.str(p.second);
		}
	}
	w.i32(b.ownerSlot);
	w.u32((std::uint32_t)b.ownerBuildings.size());
	for (size_t i = 0; i < b.ownerBuildings.size(); ++i)
	{
		w.str(b.ownerBuildings[i]);
		w.i32(i < b.ownerBuildingIds.size() ? b.ownerBuildingIds[i] : 0);
	}
	w.u32((std::uint32_t)b.members.size());
	for (const LivingWorldBattleStart::Member &m : b.members)
	{
		w.i32(m.slot);
		w.i32(m.armyId);
		w.i32(m.memberIndex);
		w.str(m.thingTemplate);
		w.i32(m.quantity);
		w.f32(m.experience);
		w.u32((std::uint32_t)m.upgrades.size());
		for (const std::string &u : m.upgrades)
		{
			w.str(u);
		}
		w.str(m.spawnId);
	}
	w.u32((std::uint32_t)b.slotStarts.size());
	for (const LivingWorldBattleStart::SlotStart &st : b.slotStarts)
	{
		w.i32(st.slot);
		w.u32(st.cash);
		w.i32(st.garrisonArmyId);
		w.u32((std::uint32_t)st.bonusUnits.size());
		for (const std::string &u : st.bonusUnits)
		{
			w.str(u);
		}
		w.u32((std::uint32_t)st.heroes.size());
		for (const LivingWorldBattleStart::SlotStart::Hero &h : st.heroes)
		{
			w.str(h.templateName);
			w.str(h.spawnId);
			w.f32(h.experience);
			w.u8(h.dead ? 1 : 0);
		}
		w.f32(st.skillPoints);
		w.i32(st.bonusPoints);
		for (const std::vector<std::string> *names : { &st.sciences, &st.upgrades })
		{
			w.u32((std::uint32_t)names->size());
			for (const std::string &nm : *names)
			{
				w.str(nm);
			}
		}
	}
}

// an army entry's count (the strategic game's armies hold a few units each; a retail army stays below its command points) and the battle's total
constexpr int kMaxBattleMemberQuantity = 1000;
constexpr std::uint64_t kMaxBattleObjects = 10000;

bool readLivingWorldBattle(NetByteReader &r, LivingWorldBattleStart &b, std::string *error)
{
	// every count is bounded by the bytes left (a damaged count never allocates), every slot is one of the game's
	auto count = [&](std::uint32_t perItem) -> std::uint32_t {
		const std::uint32_t n = r.u32();
		return r.failed() || n > r.remaining() / perItem ? UINT32_MAX : n;
	};
	auto slotOk = [](int slot) { return slot >= 0 && slot < MAX_SLOTS; };
	b = LivingWorldBattleStart();
	b.active = true;
	const std::uint32_t armies = count(20);
	if (armies == UINT32_MAX)
	{
		return failWith(error, "the War of the Ring battle's army count exceeds the replay header");
	}
	for (std::uint32_t i = 0; i < armies && !r.failed(); ++i)
	{
		LivingWorldBattleStart::ArmyStart a;
		a.slot = r.i32();
		a.armyId = r.i32();
		a.state = r.i32();
		a.arrivalFrame = r.i32();
		const std::uint32_t points = count(8);
		if (points == UINT32_MAX || !slotOk(a.slot) || (a.state != 2 && a.state != 4) || a.arrivalFrame < 0)
		{
			return failWith(error, "the War of the Ring battle's army start " + std::to_string(i) + " is damaged");
		}
		for (std::uint32_t k = 0; k < points; ++k)
		{
			std::string spawn = r.str();
			std::string gather = r.str();
			a.spawnPoints.emplace_back(std::move(spawn), std::move(gather));
		}
		b.armies.push_back(std::move(a));
	}
	b.ownerSlot = r.i32();
	if (b.ownerSlot != -1 && !slotOk(b.ownerSlot))
	{
		return failWith(error, "the War of the Ring battle's owner slot " + std::to_string(b.ownerSlot));
	}
	const std::uint32_t buildings = count(8);
	if (buildings == UINT32_MAX)
	{
		return failWith(error, "the War of the Ring battle's building count exceeds the replay header");
	}
	for (std::uint32_t i = 0; i < buildings; ++i)
	{
		b.ownerBuildings.push_back(r.str());
		b.ownerBuildingIds.push_back(r.i32());
	}
	std::uint64_t spawned = 0; // review wotr2m6 (Sol): what the start places is bounded (a crafted header must not make it create objects without end)
	const std::uint32_t members = count(32);
	if (members == UINT32_MAX)
	{
		return failWith(error, "the War of the Ring battle's army member count exceeds the replay header");
	}
	for (std::uint32_t i = 0; i < members && !r.failed(); ++i)
	{
		LivingWorldBattleStart::Member m;
		m.slot = r.i32();
		m.armyId = r.i32();
		m.memberIndex = r.i32();
		m.thingTemplate = r.str();
		m.quantity = r.i32();
		m.experience = r.f32();
		const std::uint32_t upgrades = count(4);
		spawned += (std::uint64_t)std::max(0, m.quantity);
		if (upgrades == UINT32_MAX || !slotOk(m.slot) || m.quantity < 0 || m.quantity > kMaxBattleMemberQuantity || spawned > kMaxBattleObjects ||
			!std::isfinite(m.experience))
		{
			return failWith(error, "the War of the Ring battle's army member " + std::to_string(i) + " is damaged");
		}
		for (std::uint32_t u = 0; u < upgrades; ++u)
		{
			m.upgrades.push_back(r.str());
		}
		m.spawnId = r.str();
		b.members.push_back(std::move(m));
	}
	const std::uint32_t starts = count(20);
	if (starts == UINT32_MAX)
	{
		return failWith(error, "the War of the Ring battle's player start count exceeds the replay header");
	}
	for (std::uint32_t i = 0; i < starts && !r.failed(); ++i)
	{
		LivingWorldBattleStart::SlotStart st;
		st.slot = r.i32();
		st.cash = r.u32();
		st.garrisonArmyId = r.i32();
		const std::uint32_t units = count(4);
		if (units == UINT32_MAX || !slotOk(st.slot))
		{
			return failWith(error, "the War of the Ring battle's player start " + std::to_string(i) + " is damaged");
		}
		for (std::uint32_t u = 0; u < units; ++u)
		{
			st.bonusUnits.push_back(r.str());
		}
		const std::uint32_t heroes = count(13);
		if (heroes == UINT32_MAX)
		{
			return failWith(error, "the War of the Ring battle's player start " + std::to_string(i) + " is damaged");
		}
		for (std::uint32_t h = 0; h < heroes; ++h)
		{
			LivingWorldBattleStart::SlotStart::Hero hero;
			hero.templateName = r.str();
			hero.spawnId = r.str();
			hero.experience = r.f32();
			hero.dead = r.u8() != 0;
			if (!std::isfinite(hero.experience))
			{
				return failWith(error, "the War of the Ring battle's player start " + std::to_string(i) + " is damaged");
			}
			st.heroes.push_back(std::move(hero));
		}
		st.skillPoints = r.f32();
		st.bonusPoints = r.i32();
		if (!std::isfinite(st.skillPoints))
		{
			return failWith(error, "the War of the Ring battle's player start " + std::to_string(i) + " is damaged");
		}
		for (std::vector<std::string> *names : { &st.sciences, &st.upgrades })
		{
			const std::uint32_t k = count(4);
			if (k == UINT32_MAX)
			{
				return failWith(error, "the War of the Ring battle's player start " + std::to_string(i) + " is damaged");
			}
			for (std::uint32_t j = 0; j < k; ++j)
			{
				names->push_back(r.str());
			}
		}
		b.slotStarts.push_back(std::move(st));
	}
	return !r.failed() || failWith(error, "truncated War of the Ring battle in the replay header");
}

bool parseReplay(const std::vector<std::uint8_t> &bytes, ReplayFile &out, std::string *error)
{
	if (bytes.size() > ReplayFile::kMaxBytes)
	{
		return failWith(error, "the replay exceeds the size limit of " + std::to_string(ReplayFile::kMaxBytes) + " bytes");
	}
	if (bytes.size() < sizeof(kMagic) || std::memcmp(bytes.data(), kMagic, sizeof(kMagic)) != 0)
	{
		return failWith(error, "not an OpenBFME replay (magic)");
	}
	NetByteReader r(bytes.data() + sizeof(kMagic), bytes.size() - sizeof(kMagic));
	const std::uint32_t version = r.u32();
	if (version != kBattleVersion && version != kVersion && version != 5 && version != 6 && version != 7 && version != 8 && version != 9 && version != 10)
	{
		// lane SAVE-2: format 6 only adds the profile switch record, so a format 5 replay (UBER-1) still reads (without that record); lane OWNERFIX-2:
		// format 7 only adds the slots' handicaps, so a format 5 / 6 replay or save still reads (every handicap 0)
		return failWith(error, "replay version " + std::to_string(version) + " (this build reads 5 .. " + std::to_string(kBattleVersion) + ")");
	}
	ReplayHeader &h = out.header;
	h.profile = ProfileIdentity::fromText(r.str());
	if (version >= 9)
	{
		h.buildVersion = r.str();
		h.buildCommit = r.str();
		if (version >= 10)
		{
			h.buildDate = r.str();
		}
		if (r.failed() || h.buildVersion.empty() || h.buildVersion.size() > 128 || h.buildCommit.size() > 40 ||
			!RecordingBuild::validDate(h.buildDate) ||
			h.buildVersion.find_first_of("\r\n\t") != std::string::npos ||
			(h.buildCommit != "none" && (h.buildCommit.size() != 40 || h.buildCommit.find_first_not_of("0123456789abcdef") != std::string::npos)))
		{
			return failWith(error, "invalid replay recording build");
		}
	}
	if (!NetPacket::readNewGameMessage(r, h.game, error, version >= 7, version >= 11)) // lane CHEAT-1: the Cheat Mode i32 from format 11
	{
		return false;
	}
	// lane UBER-1: the identity and the game must name the same simulation profile (a replay that says one in its identity and runs the other is refused,
	// never played under the identity's name)
	if (const std::string why = h.profileConflict(); !why.empty())
	{
		return failWith(error, why);
	}
	h.recordingSlot = r.i32();
	const std::uint32_t algorithm = r.u32();
	if (algorithm > (std::uint32_t)RandomAlgorithm::RotWK_GameDat_LCG)
	{
		return failWith(error, "replay RNG algorithm " + std::to_string(algorithm));
	}
	h.algorithm = (RandomAlgorithm)algorithm;
	// lane P16-NET: the index list is sized by the recorded game's serialized slot count — 8 for a legacy game, so an
	// old recording parses byte for byte; 16 for an extended one (its GameInfo tag already makes old readers refuse it).
	for (int i = 0; i < h.game.game.serializedSlotCount(); ++i)
	{
		h.network.slotPlayerIndex[(size_t)i] = r.i32();
	}
	h.network.localSlot = h.recordingSlot;
	h.network.runAhead = r.i32();
	h.network.crcInterval = r.i32();
	if (version >= kBattleVersion && !readLivingWorldBattle(r, h.game.livingWorldBattle, error))
	{
		return false;
	}
	if (r.failed())
	{
		return failWith(error, "truncated replay header");
	}
	while (!r.atEnd())
	{
		const std::uint8_t kind = r.u8();
		if (kind == RECORD_FRAME)
		{
			const UnsignedInt frame = r.u32();
			const std::uint32_t n = r.u32();
			if (out.frames.count(frame))
			{
				return failWith(error, "duplicate command record of frame " + std::to_string(frame)); // lane SAVE-2: one record per frame
			}
			// lane NETDIAG-1 (from a contributor's parser hardening): a count the remaining bytes cannot hold is refused before any command is read
			// (7 bytes is the smallest GameMessage: u16 type, u8 player, u32 argument count, NetPacket.h format 2)
			if (r.failed() || n > r.remaining() / 7)
			{
				return failWith(error, "frame " + std::to_string(frame) + " command count " + std::to_string(n) + " exceeds the record's bytes");
			}
			std::vector<GameMessage> list;
			for (std::uint32_t i = 0; i < n; ++i)
			{
				std::unique_ptr<GameMessage> m = NetPacket::readGameMessage(r, error);
				if (!m)
				{
					return failWith(error, "frame " + std::to_string(frame) + " command " + std::to_string(i) + ": " + (error ? *error : std::string()));
				}
				list.push_back(*m);
			}
			out.frames.emplace(frame, std::move(list));
		}
		else if (kind == RECORD_HASH)
		{
			const UnsignedInt frame = r.u32();
			const std::uint32_t hash = r.u32();
			if (!r.failed() && !out.hashes.emplace(frame, hash).second)
			{
				return failWith(error, "duplicate state hash record of frame " + std::to_string(frame)); // lane SAVE-2
			}
		}
		else if (kind == RECORD_PROFILE && version >= 6)
		{
			// lane SAVE-2: the game went on under another execution profile from this frame (a save of the other profile loaded, UBER-1)
			const UnsignedInt frame = r.u32();
			const std::uint32_t profile = r.u32();
			if (!r.failed() && !simulationProfileValid((std::int32_t)profile))
			{
				return failWith(error, "replay profile switch at frame " + std::to_string(frame) + " to the unknown profile " + std::to_string(profile));
			}
			if (!r.failed() && !out.profileSwitches.empty() && frame <= out.profileSwitches.back().first)
			{
				return failWith(error, "replay profile switches out of order (frame " + std::to_string(frame) + ")");
			}
			out.profileSwitches.push_back({ frame, (SimulationProfile)profile });
		}
		else if (kind == RECORD_VIEW && version >= 8)
		{
			// lane ANTICHEAT-1: a player's view (camera and cursor), reported by its client: not simulation input
			ReplayFile::View v;
			v.frame = r.u32();
			v.slot = r.u8();
			v.cameraX = r.f32();
			v.cameraY = r.f32();
			v.cursorX = r.f32();
			v.cursorY = r.f32();
			v.flags = r.u8();
			if (!r.failed() && v.slot >= MAX_SLOTS)
			{
				return failWith(error, "a view record of slot " + std::to_string(v.slot));
			}
			out.views.push_back(v);
		}
		else if (kind == RECORD_LOST)
		{
			const UnsignedInt frame = r.u32();
			const std::uint32_t index = r.u32();
			out.lost.push_back("frame " + std::to_string(frame) + " command " + std::to_string(index) + ": " + r.str());
		}
		else if (kind == RECORD_END)
		{
			out.finalFrame = r.u32();
			out.finalHash = r.u32();
			out.complete = true;
			if (!r.atEnd())
			{
				return failWith(error, "data after the replay's end record");
			}
		}
		else
		{
			return failWith(error, "unknown replay record " + std::to_string(kind));
		}
		if (r.failed())
		{
			return failWith(error, "truncated replay record");
		}
	}
	if (!out.complete && !out.hashes.empty())
	{
		out.finalFrame = out.hashes.rbegin()->first; // an interrupted recording plays up to its last hashed frame
	}
	return true;
}
} // namespace

bool ReplayFile::parse(const std::vector<std::uint8_t> &bytes, ReplayFile &out, std::string *error)
{
	try
	{
		ReplayFile parsed;
		if (!parseReplay(bytes, parsed, error))
		{
			return false;
		}
		out = std::move(parsed);
		return true;
	}
	catch (const std::exception &e)
	{
		return failWith(error, std::string("the replay could not be parsed (") + e.what() + ")");
	}
}

bool ReplayWriter::openTarget(const std::string &path, std::string *error)
{
	// lane SAVE-2: an empty path records in memory only (a game whose recording is not a replay file: a campaign mission, kept for its saves)
	if (path.empty())
	{
		if (!m_keepCopy)
		{
			return failWith(error, "a recording without a file must keep its copy (setKeepCopy)");
		}
		m_memoryOnly = true;
		m_open = true;
		return true;
	}
	m_out.open(PathIO::native(path), std::ios::binary | std::ios::trunc);
	if (!m_out)
	{
		return failWith(error, "cannot write the replay " + path);
	}
	m_open = true;
	return true;
}

bool ReplayWriter::open(const std::string &path, const ReplayHeader &h, std::string *error)
{
	// lane UBER-1: a header whose identity and game disagree on the simulation profile is never written
	if (const std::string why = h.profileConflict(); !why.empty())
	{
		return failWith(error, why);
	}
	if (!openTarget(path, error))
	{
		return false;
	}
	write(std::vector<std::uint8_t>(kMagic, kMagic + sizeof(kMagic)));
	NetByteWriter w;
	const bool battle = h.game.livingWorldBattle.active;
	w.u32(battle ? kBattleVersion : kVersion);
	w.str(h.profile.text);
	w.str(h.buildVersion.empty() ? BuildVersion::kVersion : h.buildVersion);
	w.str(h.buildCommit.empty() ? BuildVersion::kCommit : h.buildCommit);
	w.str(h.buildVersion.empty() ? BuildVersion::kBuildDate : h.buildDate);
	NetPacket::writeNewGameMessage(w, h.game);
	w.i32(h.recordingSlot);
	w.u32((std::uint32_t)h.algorithm);
	// lane P16-NET: a legacy game keeps the old 8-entry list; an extended game's GameInfo already carries the tag
	// old readers refuse, so its 16-entry list needs no new format version.
	for (int i = 0; i < h.game.game.serializedSlotCount(); ++i)
	{
		w.i32(h.network.slotPlayerIndex[(size_t)i]);
	}
	w.i32(h.network.runAhead);
	w.i32(h.network.crcInterval);
	if (battle)
	{
		writeLivingWorldBattle(w, h.game.livingWorldBattle);
	}
	write(w.data());
	return true;
}

bool ReplayWriter::openContinuing(const std::string &path, const std::vector<std::uint8_t> &log, std::string *error)
{
	// the log is a complete replay (the save's, already parsed): it ends with the 9-byte end record u8 3, u32 F, u32 hash
	if (log.size() < sizeof(kMagic) + 9 || std::memcmp(log.data(), kMagic, sizeof(kMagic)) != 0 || log[log.size() - 9] != RECORD_END)
	{
		return failWith(error, "the save's log is not a complete replay");
	}
	NetByteReader end(log.data() + log.size() - 8, 8);
	if (!openTarget(path, error))
	{
		return false;
	}
	write(std::vector<std::uint8_t>(log.begin(), log.end() - 9));
	// the continued recording's position: the save's frame F (every batch below it recorded, the hash of F recorded)
	m_anyFrame = false;
	m_anyHash = true;
	m_lastHashFrame = end.u32();
	m_lastHash = end.u32();
	return true;
}

bool ReplayWriter::logUpTo(UnsignedInt frame, std::uint32_t hash, std::vector<std::uint8_t> &out, std::string *error) const
{
	if (!m_keepCopy || m_copy.empty())
	{
		return failWith(error, "the game is not being recorded, so it cannot be saved");
	}
	if (m_writeFailed || !m_errors.empty())
	{
		return failWith(error, "the game's recording is incomplete (" + m_errors.front() + "), so it cannot be saved");
	}
	if (m_anyFrame && m_lastFrame >= frame)
	{
		return failWith(error, "the recording already holds frame " + std::to_string(m_lastFrame) + ", after the frame " + std::to_string(frame) + " being saved");
	}
	if (!m_anyHash || m_lastHashFrame != frame || m_lastHash != hash)
	{
		return failWith(error, "the recording has no state hash of frame " + std::to_string(frame) + " equal to the game's");
	}
	out = m_copy;
	NetByteWriter w;
	w.u8(RECORD_END);
	w.u32(frame);
	w.u32(hash);
	out.insert(out.end(), w.data().begin(), w.data().end());
	return true;
}

bool ReplayWriter::flush(std::string *error)
{
	if (m_open && !m_memoryOnly)
	{
		m_out.flush();
		if (!m_out.good() && !m_writeFailed)
		{
			m_writeFailed = true;
			m_errors.push_back("writing the replay failed (disk full or the file went away): the recording is incomplete");
		}
	}
	if (m_writeFailed || !m_errors.empty())
	{
		return failWith(error, m_errors.empty() ? std::string("writing the replay failed") : m_errors.front());
	}
	return true;
}

void ReplayWriter::write(const std::vector<std::uint8_t> &b)
{
	if (m_keepCopy)
	{
		m_copy.insert(m_copy.end(), b.begin(), b.end());
	}
	if (m_memoryOnly)
	{
		return;
	}
	m_out.write((const char *)b.data(), (std::streamsize)b.size());
	if (!m_out.good() && !m_writeFailed)
	{
		m_writeFailed = true;
		m_errors.push_back("writing the replay failed (disk full or the file went away): the recording is incomplete");
	}
}

void ReplayWriter::recordFrame(UnsignedInt frame, const CommandList &commands)
{
	if (!m_open || commands.messages().empty())
	{
		return;
	}
	// every command is encoded on its own: one that cannot be is recorded as lost (never the whole frame), and reported
	std::vector<std::vector<std::uint8_t>> encoded;
	std::vector<std::pair<std::uint32_t, std::string>> lost;
	for (size_t i = 0; i < commands.messages().size(); ++i)
	{
		NetByteWriter one;
		std::string error;
		if (NetPacket::writeGameMessage(one, commands.messages()[i], &error))
		{
			encoded.push_back(one.take());
		}
		else
		{
			lost.push_back({ (std::uint32_t)i, error });
			m_errors.push_back("frame " + std::to_string(frame) + " command " + std::to_string(i) + ": " + error);
		}
	}
	m_anyFrame = true;
	m_lastFrame = frame;
	NetByteWriter w;
	if (!encoded.empty())
	{
		w.u8(RECORD_FRAME);
		w.u32(frame);
		w.u32((std::uint32_t)encoded.size());
		write(w.data());
		for (const std::vector<std::uint8_t> &b : encoded)
		{
			write(b);
		}
	}
	for (const auto &l : lost)
	{
		NetByteWriter x;
		x.u8(RECORD_LOST);
		x.u32(frame);
		x.u32(l.first);
		x.str(l.second);
		write(x.data());
	}
}

void ReplayWriter::recordProfileSwitch(UnsignedInt frame, SimulationProfile profile)
{
	if (!m_open)
	{
		return;
	}
	NetByteWriter w;
	w.u8(RECORD_PROFILE);
	w.u32(frame);
	w.u32((std::uint32_t)profile);
	write(w.data());
}

void ReplayWriter::recordView(const ReplayFile::View &v)
{
	if (!m_open)
	{
		return;
	}
	NetByteWriter w;
	w.u8(RECORD_VIEW);
	w.u32(v.frame);
	w.u8(v.slot);
	w.f32(v.cameraX);
	w.f32(v.cameraY);
	w.f32(v.cursorX);
	w.f32(v.cursorY);
	w.u8(v.flags);
	write(w.data());
}

void ReplayWriter::recordHash(UnsignedInt frameAfter, std::uint32_t hash)
{
	if (!m_open)
	{
		return;
	}
	NetByteWriter w;
	w.u8(RECORD_HASH);
	w.u32(frameAfter);
	w.u32(hash);
	write(w.data());
	m_anyHash = true;
	m_lastHashFrame = frameAfter;
	m_lastHash = hash;
}

void ReplayWriter::close(UnsignedInt finalFrame, std::uint32_t finalHash)
{
	if (!m_open)
	{
		return;
	}
	NetByteWriter w;
	w.u8(RECORD_END);
	w.u32(finalFrame);
	w.u32(finalHash);
	write(w.data());
	m_open = false;
	if (m_memoryOnly)
	{
		return;
	}
	m_out.close();
	if (m_out.fail() && !m_writeFailed)
	{
		m_writeFailed = true;
		m_errors.push_back("closing the replay failed: the recording is incomplete");
	}
}

LiveGameFrameDriver::Capture ReplayPlayback::capture() const
{
	Capture c;
	c.hashEveryFrame = true; // every recorded hash is compared
	return c;
}

void ReplayPlayback::pump(UnsignedInt protocolFrame, CommandList &pending)
{
	m_ignored += pending.messages().size(); // ZH playback: the local player's input does not reach the logic
	pending.reset();
	m_protocolFrame = protocolFrame;
	m_pumped = true;
}

bool ReplayPlayback::acquire(UnsignedInt frame, FrameBatch &out)
{
	if (frame >= m_replay.finalFrame)
	{
		return false; // the recording ends here
	}
	out.frame = frame;
	out.commands.clear();
	out.switchProfile = false;
	for (const auto &sw : m_replay.profileSwitches)
	{
		if (sw.first == frame)
		{
			// lane SAVE-2: the recorded game ran this batch and the later ones under that profile (the simulation owner applies it with the batch)
			out.switchProfile = true;
			out.profile = sw.second;
		}
	}
	const auto it = m_replay.frames.find(frame);
	if (it != m_replay.frames.end())
	{
		out.commands = it->second;
	}
	return true;
}

void ReplayPlayback::completed(const FrameCompletion &c)
{
	m_protocolFrame = c.frame;
	const auto it = m_replay.hashes.find(c.frame);
	if (it == m_replay.hashes.end() || !c.hashed)
	{
		return;
	}
	const std::uint32_t played = c.hash;
	++m_compared;
	if (played != it->second)
	{
		++m_mismatches;
		if (!m_hasMismatch)
		{
			m_hasMismatch = true;
			m_first.frame = c.frame;
			m_first.recorded = it->second;
			m_first.played = played;
		}
	}
}
