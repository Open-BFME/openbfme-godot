// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
// See Common/GameState.h (lane SAVE-4a; ZH Source/Common/System/SaveGame/GameState.cpp).

#include "Common/GameState.h"

#include "Common/Snapshot.h"
#include "Common/XferCRC.h"
#include "Common/XferLoad.h"
#include "Common/XferSave.h"

#include <algorithm>
#include <stdexcept>

namespace
{
const std::uint32_t kMagicALAE = 0x45414C41u; // "ALAE" (RW 0xA20DD0)
const std::uint32_t kMagic2STR = 0x52545332u; // "2STR"
const char *const kEndToken = "SG_EOF";       // GameState::xferSaveData's last token

bool failWith(std::string *error, const std::string &why)
{
	if (error)
	{
		*error = why;
	}
	return false;
}

std::string joined(const std::vector<std::string> &v)
{
	std::string s;
	for (const std::string &x : v)
	{
		s += (s.empty() ? "" : ", ") + x;
	}
	return s;
}

void writeHeader(XferSave &x)
{
	std::uint32_t magic = kMagicALAE, magic2 = kMagic2STR, version = GameState::kStreamVersion, tagged = 0;
	x.xferUnsignedInt(&magic);
	x.xferUnsignedInt(&magic2);
	x.xferUnsignedInt(&version);
	x.xferUnsignedInt(&tagged);
}

void readHeader(XferLoad &x)
{
	std::uint32_t magic = 0, magic2 = 0, version = 0, tagged = 0;
	x.xferUnsignedInt(&magic);
	x.xferUnsignedInt(&magic2);
	if (magic != kMagicALAE || magic2 != kMagic2STR)
	{
		throw XferException("not a snapshot stream (no \"ALAE\" \"2STR\" header)");
	}
	x.xferUnsignedInt(&version);
	if (version != GameState::kStreamVersion)
	{
		throw XferException("snapshot stream version " + std::to_string(version) + " (this build reads version " + std::to_string(GameState::kStreamVersion) + ")");
	}
	x.xferUnsignedInt(&tagged);
	if (tagged != 0)
	{
		throw XferException("a tagged snapshot stream (OpenBFME writes and reads untagged streams only)");
	}
}
} // namespace

GameState::GameState() = default;
GameState::~GameState() = default;

Snapshot *GameState::addOwnedSnapshotBlock(const std::string &token, std::unique_ptr<Snapshot> snapshot, unsigned lists, const std::string &owner,
	const std::vector<std::string> &sections)
{
	Snapshot *s = snapshot.get();
	addSnapshotBlock(token, s, lists, owner, sections);
	m_owned.push_back(std::move(snapshot));
	return s;
}

GameState::Block *GameState::find(const std::string &token)
{
	for (Block &b : m_blocks)
	{
		if (b.token == token)
		{
			return &b;
		}
	}
	return nullptr;
}

const GameState::Block *GameState::findBlock(const std::string &token) const
{
	return const_cast<GameState *>(this)->find(token);
}

void GameState::addSnapshotBlock(const std::string &token, Snapshot *snapshot, unsigned lists, const std::string &owner, const std::vector<std::string> &sections)
{
	if (token.empty() || token == kEndToken || find(token))
	{
		throw std::logic_error("GameState::addSnapshotBlock: the token '" + token + "' is empty, reserved or registered twice");
	}
	Block b;
	b.token = token;
	b.snapshot = snapshot;
	b.owner = owner;
	b.sections = sections;
	b.lists = lists;
	m_blocks.push_back(b);
}

void GameState::declareBlockWithoutXfer(const std::string &token, unsigned lists, const std::string &owner, const std::vector<std::string> &sections)
{
	addSnapshotBlock(token, nullptr, lists, owner, sections);
}

void GameState::declareRebuiltSection(const std::string &section, const std::string &why)
{
	m_rebuilt.emplace_back(section, why);
}

void GameState::provideSnapshot(const std::string &token, Snapshot *snapshot, const std::string &owner)
{
	Block *b = find(token);
	if (!b || b->snapshot)
	{
		throw std::logic_error("GameState::provideSnapshot: '" + token + "' is not a declared block without an xfer");
	}
	b->snapshot = snapshot;
	b->owner = owner;
}

void GameState::provideOwnedSnapshot(const std::string &token, std::unique_ptr<Snapshot> snapshot, const std::string &owner)
{
	provideSnapshot(token, snapshot.get(), owner);
	m_owned.push_back(std::move(snapshot));
}

std::vector<std::string> GameState::missingBlocks(SnapshotList list) const
{
	std::vector<std::string> out;
	for (const Block &b : m_blocks)
	{
		if ((b.lists & (1u << list)) && !b.snapshot)
		{
			out.push_back(b.token + " (" + b.owner + ")");
		}
	}
	return out;
}

std::vector<std::string> GameState::undeclaredSections(const std::vector<std::string> &stateSections, SnapshotList list) const
{
	std::vector<std::string> out;
	for (const std::string &s : stateSections)
	{
		bool known = std::any_of(m_rebuilt.begin(), m_rebuilt.end(), [&](const std::pair<std::string, std::string> &r) { return r.first == s; });
		for (const Block &b : m_blocks)
		{
			known = known || ((b.lists & (1u << list)) && std::find(b.sections.begin(), b.sections.end(), s) != b.sections.end());
		}
		if (!known && std::find(out.begin(), out.end(), s) == out.end())
		{
			out.push_back(s);
		}
	}
	return out;
}

std::string GameState::refusal(const std::vector<std::string> &stateSections, SnapshotList list) const
{
	std::string why;
	const std::vector<std::string> undeclared = undeclaredSections(stateSections, list);
	if (!undeclared.empty())
	{
		why = "the snapshot is refused: " + std::to_string(undeclared.size()) + " sections of the state hash have no snapshot block: " + joined(undeclared);
	}
	const std::vector<std::string> missing = missingBlocks(list);
	if (!missing.empty())
	{
		why += std::string(why.empty() ? "the snapshot is refused: " : "; ") + std::to_string(missing.size()) + " simulation blocks have no xfer yet: "
			+ joined(missing);
	}
	return why;
}

void GameState::saveBlocks(const std::vector<std::string> &tokens, std::vector<std::uint8_t> &out) const
{
	XferSave x;
	writeHeader(x);
	for (const std::string &t : tokens)
	{
		const Block *b = findBlock(t);
		if (!b || !b->snapshot)
		{
			throw XferException("the block '" + t + "' " + (b ? "has no xfer yet (" + b->owner + ")" : std::string("is not registered")));
		}
		std::string token = t;
		x.xferAsciiString(&token); // GameState::xferSaveData RW 0x6DDF2F: the token, then the block
		x.beginBlock();
		try
		{
			x.xferSnapshot(b->snapshot);
		}
		catch (const std::exception &e)
		{
			throw XferException("block '" + t + "': " + e.what());
		}
		x.endBlock();
	}
	std::string end = kEndToken;
	x.xferAsciiString(&end);
	out = std::move(x.data());
}

void GameState::setLoadPreparation(const std::string &token, std::function<void(Xfer &)> prepare, std::function<void()> cleanup)
{
	Block *block = find(token);
	if (!block || !prepare || !cleanup)
	{
		throw std::logic_error("GameState::setLoadPreparation: invalid preparation for '" + token + "'");
	}
	block->prepareLoad = std::move(prepare);
	block->finishPreparation = std::move(cleanup);
}

void GameState::loadBlocks(const std::vector<std::string> &tokens, const std::vector<std::uint8_t> &bytes)
{
	// lane SAVE-4e: first walk the framing alone (every block skipped by its end offset, RW 0xA203D6): the tokens, known,
	// once each and in the list's order, every block inside the stream, the end token and nothing after it; a damaged, truncated, reordered or foreign
	// stream is refused here, before any block touches the game. When registered, a preparation pass stages dependencies without committing them.
	// Finally the blocks load in their unchanged stream order (their contents checked by their own xfers).
	{
		XferLoad framing(bytes);
		walkBlocks(framing, tokens, false);
	}
	struct PreparedBlocks
	{
		std::vector<Block *> blocks;
		~PreparedBlocks()
		{
			for (Block *block : blocks) block->finishPreparation();
		}
	} prepared;
	for (const std::string &token : tokens)
	{
		Block *block = find(token);
		if (block && block->prepareLoad) prepared.blocks.push_back(block);
	}
	if (!prepared.blocks.empty())
	{
		XferLoad preparation(bytes);
		walkBlocks(preparation, tokens, true, true);
	}
	XferLoad x(bytes);
	m_postProcess.clear();
	walkBlocks(x, tokens, true);
	// RW 0x6DD65C: every queued post-process, in queue order (a post-process may queue no further ones)
	const std::vector<Snapshot *> queued = m_postProcess;
	m_postProcess.clear();
	for (Snapshot *s : queued)
	{
		s->loadPostProcess();
	}
}

void GameState::walkBlocks(XferLoad &x, const std::vector<std::string> &tokens, bool load, bool prepare)
{
	{
		readHeader(x);
		std::vector<std::string> seen;
		for (;;)
		{
			std::string token;
			x.xferAsciiString(&token);
			if (token == kEndToken)
			{
				break;
			}
			if (std::find(tokens.begin(), tokens.end(), token) == tokens.end())
			{
				throw XferException("the snapshot carries an unknown block '" + token + "'");
			}
			if (std::find(seen.begin(), seen.end(), token) != seen.end())
			{
				throw XferException("the snapshot carries the block '" + token + "' twice");
			}
			Block *b = find(token);
			if (!b || !b->snapshot)
			{
				throw XferException("the snapshot's block '" + token + "' has no xfer in this build");
			}
			if (!load)
			{
				const size_t at = seen.size();
				if (at < tokens.size() && tokens[at] != token)
				{
					throw XferException("the snapshot's block '" + token + "' stands where this build loads '" + tokens[at] + "' (the blocks load in the list's order)");
				}
				seen.push_back(token);
				x.skipBlock();
				continue;
			}
			seen.push_back(token);
			if (prepare && !b->prepareLoad)
			{
				x.skipBlock();
				continue;
			}
			x.beginBlock();
			try
			{
				if (prepare) b->prepareLoad(x);
				else x.xferSnapshot(b->snapshot);
				x.endBlock();
			}
			catch (const std::exception &e)
			{
				throw XferException("block '" + token + "': " + e.what());
			}
		}
		if (!x.atEnd())
		{
			throw XferException("the snapshot has " + std::to_string(x.size() - x.position()) + " bytes after its end token");
		}
		std::vector<std::string> absent;
		for (const std::string &t : tokens)
		{
			if (std::find(seen.begin(), seen.end(), t) == seen.end())
			{
				absent.push_back(t);
			}
		}
		if (!absent.empty())
		{
			throw XferException("the snapshot lacks the blocks " + joined(absent));
		}
	}
}

bool GameState::checkFraming(const std::vector<std::uint8_t> &bytes, std::string *error, SnapshotList list)
{
	std::vector<std::string> tokens;
	for (const Block &b : m_blocks)
	{
		if (b.lists & (1u << list))
		{
			tokens.push_back(b.token);
		}
	}
	try
	{
		XferLoad x(bytes);
		walkBlocks(x, tokens, false);
	}
	catch (const std::exception &e)
	{
		return failWith(error, std::string("the snapshot is damaged: ") + e.what());
	}
	return true;
}

bool GameState::saveSnapshot(const std::vector<std::string> &stateSections, std::vector<std::uint8_t> &out, std::string *error, SnapshotList list)
{
	const std::string why = refusal(stateSections, list);
	if (!why.empty())
	{
		return failWith(error, why);
	}
	std::vector<std::string> tokens;
	for (const Block &b : m_blocks)
	{
		if (b.lists & (1u << list))
		{
			tokens.push_back(b.token);
		}
	}
	try
	{
		saveBlocks(tokens, out);
	}
	catch (const std::exception &e)
	{
		return failWith(error, std::string("the snapshot could not be written: ") + e.what());
	}
	return true;
}

bool GameState::loadSnapshot(const std::vector<std::string> &stateSections, const std::vector<std::uint8_t> &bytes, std::string *error, SnapshotList list)
{
	const std::string why = refusal(stateSections, list);
	if (!why.empty())
	{
		// the same coverage as a save: "the snapshot is refused: ..." becomes "this build cannot load a snapshot: ..."
		return failWith(error, "this build cannot load a snapshot: " + why.substr(std::string("the snapshot is refused: ").size()));
	}
	std::vector<std::string> tokens;
	for (const Block &b : m_blocks)
	{
		if (b.lists & (1u << list))
		{
			tokens.push_back(b.token);
		}
	}
	try
	{
		loadBlocks(tokens, bytes);
	}
	catch (const std::exception &e)
	{
		m_postProcess.clear();
		return failWith(error, std::string("the snapshot could not be loaded: ") + e.what());
	}
	return true;
}

std::uint32_t GameState::crcSnapshot(SnapshotList list) const
{
	XferCRC x;
	for (const Block &b : m_blocks)
	{
		if (!(b.lists & (1u << list)))
		{
			continue;
		}
		if (!b.snapshot)
		{
			throw XferException("the block '" + b.token + "' has no xfer yet (" + b.owner + ")");
		}
		x.xferSnapshot(b.snapshot);
	}
	return x.getCRC();
}

std::vector<std::string> GameState::stopLines()
{
	return {
		"[S-4120] snapshots: the Xfer framework and GameState are ported (SAVE-4a), with the core blocks (teams, players, the frame state, the logic RNG); a "
		"snapshot is refused while any simulation block lacks an xfer or a state hash section has no block, naming them; since SAVE-4e every block has "
		"its xfer (SAVE-4b, 4c, 4d), a save carries the snapshot and a load restores it (S-5010); OpenBFME's own list numbering only: retail's lists 3 "
		"and 4 (the War of the Ring and between-missions saves) are not built",
		"[S-4121] snapshots: the core blocks are OpenBFME's native layouts (obfme:*), not retail's CHUNK_* blocks: their classes are not 1:1 ports of "
		"retail's (Player::DoXfer RW 0x6B205F (1, 10), TeamFactory::xfer RW 0x7A634B (1, 3), ScoreKeeper::DoXfer RW 0x79ECCB (1, 12), CHUNK_GameLogic "
		"RW 0x6308F3 (1, 8)); only the relation maps (RW 0x6AE9B7 / 0x7A56B6) and the framing follow retail; the logic RNG state is saved (retail seeds "
		"again at a load, ZH saves none); retail import (SAVE-7) needs a translator per block",
		"[S-4122] snapshots: a load is stricter than retail (INFERENCE: retail skips an unknown or bad block and its untagged endBlock checks nothing): an "
		"unknown, missing or repeated block, a block read short or long and bytes after the end are named errors; the relation maps are transferred in "
		"ascending key order (INFERENCE from RW 0x5E0A39 / 0x6BFEFD)",
	};
}

void GameState::addPostProcessSnapshot(Snapshot *snapshot)
{
	if (std::find(m_postProcess.begin(), m_postProcess.end(), snapshot) == m_postProcess.end())
	{
		m_postProcess.push_back(snapshot);
	}
}
