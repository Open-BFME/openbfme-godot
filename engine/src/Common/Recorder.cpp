// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
// See Common/Recorder.h.

#include "Common/Recorder.h"

#include <cstring>
#include <iterator>

namespace
{
const char kMagic[8] = { 'O', 'B', 'F', 'M', 'E', 'R', 'E', 'P' };
constexpr std::uint32_t kVersion = 6; // 6 (lane SAVE-2): the execution profile switch record, duplicate records refused; 2: the profile identity, GameMessage format 2 (u32 argument counts), lost-command records; 3 (lane HERO-2): the GameInfo slots' Create-a-Hero records; 4 (lane RULES-1): the GameInfo's game rules and Legendary Heroes factor; 5 (lane UBER-1): the GameInfo's simulation profile
enum : std::uint8_t
{
	RECORD_FRAME = 1,
	RECORD_HASH = 2,
	RECORD_END = 3,
	RECORD_LOST = 4,
	RECORD_PROFILE = 5 // lane SAVE-2
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
	std::ifstream in(path, std::ios::binary);
	if (!in)
	{
		return failWith(error, "cannot open the replay " + path);
	}
	std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	return parse(bytes, out, error);
}

bool ReplayFile::parse(const std::vector<std::uint8_t> &bytes, ReplayFile &out, std::string *error)
{
	out = ReplayFile();
	if (bytes.size() < sizeof(kMagic) || std::memcmp(bytes.data(), kMagic, sizeof(kMagic)) != 0)
	{
		return failWith(error, "not an OpenBFME replay (magic)");
	}
	NetByteReader r(bytes.data() + sizeof(kMagic), bytes.size() - sizeof(kMagic));
	const std::uint32_t version = r.u32();
	if (version != kVersion && version != 5)
	{
		// lane SAVE-2: format 6 only adds the profile switch record, so a format 5 replay (UBER-1) still reads (without that record)
		return failWith(error, "replay version " + std::to_string(version) + " (this build reads 5 and " + std::to_string(kVersion) + ")");
	}
	ReplayHeader &h = out.header;
	h.profile = ProfileIdentity::fromText(r.str());
	if (!NetPacket::readNewGameMessage(r, h.game, error))
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
	for (int &p : h.network.slotPlayerIndex)
	{
		p = r.i32();
	}
	h.network.localSlot = h.recordingSlot;
	h.network.runAhead = r.i32();
	h.network.crcInterval = r.i32();
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
			std::vector<GameMessage> &list = out.frames[frame];
			for (std::uint32_t i = 0; i < n; ++i)
			{
				std::unique_ptr<GameMessage> m = NetPacket::readGameMessage(r, error);
				if (!m)
				{
					return failWith(error, "frame " + std::to_string(frame) + " command " + std::to_string(i) + ": " + (error ? *error : std::string()));
				}
				list.push_back(*m);
			}
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
	m_out.open(path, std::ios::binary | std::ios::trunc);
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
	w.u32(kVersion);
	w.str(h.profile.text);
	NetPacket::writeNewGameMessage(w, h.game);
	w.i32(h.recordingSlot);
	w.u32((std::uint32_t)h.algorithm);
	for (int p : h.network.slotPlayerIndex)
	{
		w.i32(p);
	}
	w.i32(h.network.runAhead);
	w.i32(h.network.crcInterval);
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
