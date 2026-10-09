// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
// See Common/Recorder.h.

#include "Common/Recorder.h"

#include <cstring>
#include <iterator>

namespace
{
const char kMagic[8] = { 'O', 'B', 'F', 'M', 'E', 'R', 'E', 'P' };
constexpr std::uint32_t kVersion = 3; // 2: the profile identity, GameMessage format 2 (u32 argument counts), lost-command records; 3 (lane HERO-2): the GameInfo slots' Create-a-Hero records
enum : std::uint8_t
{
	RECORD_FRAME = 1,
	RECORD_HASH = 2,
	RECORD_END = 3,
	RECORD_LOST = 4
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
	if (version != kVersion)
	{
		return failWith(error, "replay version " + std::to_string(version) + " (this build reads " + std::to_string(kVersion) + ")");
	}
	ReplayHeader &h = out.header;
	h.profile = ProfileIdentity::fromText(r.str());
	if (!NetPacket::readNewGameMessage(r, h.game, error))
	{
		return false;
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
			out.hashes[frame] = r.u32();
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

bool ReplayWriter::open(const std::string &path, const ReplayHeader &h, std::string *error)
{
	m_out.open(path, std::ios::binary | std::ios::trunc);
	if (!m_out)
	{
		return failWith(error, "cannot write the replay " + path);
	}
	m_out.write(kMagic, sizeof(kMagic));
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

void ReplayWriter::write(const std::vector<std::uint8_t> &b)
{
	m_out.write((const char *)b.data(), (std::streamsize)b.size());
	if (!m_out.good() && !m_writeFailed)
	{
		m_writeFailed = true;
		m_errors.push_back("writing the replay failed (disk full or the file went away): the recording is incomplete");
	}
}

void ReplayWriter::recordFrame(UnsignedInt frame, const CommandList &commands)
{
	if (!m_out.is_open() || commands.messages().empty())
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

void ReplayWriter::recordHash(UnsignedInt frameAfter, std::uint32_t hash)
{
	if (!m_out.is_open())
	{
		return;
	}
	NetByteWriter w;
	w.u8(RECORD_HASH);
	w.u32(frameAfter);
	w.u32(hash);
	write(w.data());
}

void ReplayWriter::close(UnsignedInt finalFrame, std::uint32_t finalHash)
{
	if (!m_out.is_open())
	{
		return;
	}
	NetByteWriter w;
	w.u8(RECORD_END);
	w.u32(finalFrame);
	w.u32(finalHash);
	write(w.data());
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
