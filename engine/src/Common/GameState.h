// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// GameState (lane SAVE-4a, docs/SAVES.md section 2): the registry of the snapshot blocks, the block lists, the snapshot stream and the post-process pass.
// Port of ZH Include/Common/GameState.h / Source/Common/System/SaveGame/GameState.cpp (addSnapshotBlock, addPostProcessSnapshot, xferSaveData), with
// RotWK's framing; the stream is the SNAP part of the .obfmesave container (Common/System/SaveGame/SaveGameFile.h).
//
// TARGET FACTS (RotWK game.dat, caveat S-001; docs/SAVES-RETAIL.md sections 1 and 2):
//   * GameState::init RW 0x6DF73E registers (token, snapshot, list) triples in five lists (0 a normal save, 1 the deep logic CRC, 3 the multiplayer War of
//     the Ring save, 4 the save between campaign missions, 2 unused).
//   * the stream (XferSave::Open RW 0xA20DD0, GameState::xferSaveData RW 0x6DDF2F): u32 "ALAE", u32 "2STR", u32 version 1 (the loaders refuse more,
//     RW 0x6DEA25), u32 tagged flag (retail writes 0); then per block of the list its token (a string), "BLOK" + end offset, the snapshot's xfer; then the
//     string "SG_EOF".
//   * loading runs every block's xfer, then every queued loadPostProcess in the order they were queued (RW 0x6DD65C).
// DONOR (ZH GameState): addSnapshotBlock(name, snapshot, list), addPostProcessSnapshot (a snapshot asks for its post-process during its load).
// OpenBFME DIFFERENCES (owner decisions in SAVES.md; NOT retail):
//   * the native dialect: OpenBFME blocks whose class is not a 1:1 port of a retail class are named "obfme:<Name>" (the SDK namespace rule of SAVES.md
//     section 5); a 1:1 port keeps retail's token, field order and version (decision D3).
//   * no partial snapshots: every simulation subsystem DECLARES a block. A block declared without an xfer yet (the later SAVE-4 lanes) makes a save
//     request a named refusal listing every such block; so does a section of the state hash (GameLogic::computeStateHashBreakdown) that no block covers.
//     State that is rebuilt from the game's START and the data instead of saved is declared by its hash section with the reason.
//   * loading is strict: an unknown token, a block of the list missing from the stream, a second copy, a block whose load reads less or more than it
//     holds, a version outside a class's window and trailing bytes are each a named error (retail skips unknown and bad blocks; a checkpoint that dropped
//     state would only be found frames later).
//
// Not simulation maths: it moves bytes around the game, never during a logic frame.

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class Snapshot;
class Xfer;

enum SnapshotList
{
	SNAPSHOT_SAVELOAD = 0,          ///< retail list 0: a normal save (OpenBFME's native snapshot)
	SNAPSHOT_DEEPCRC_LOGICONLY = 1, ///< retail list 1: the deep logic CRC (ZH's name)
	SNAPSHOT_LIST_COUNT = 5         ///< retail registers lists 0..4 (RW 0x6DF73E); 3 and 4 are War of the Ring / campaign lists OpenBFME does not build yet
};

class GameState
{
public:
	GameState();
	~GameState();
	GameState(const GameState &) = delete;
	GameState &operator=(const GameState &) = delete;

	static constexpr std::uint32_t kStreamVersion = 1; ///< the word after "2STR" (RW 0x6DE498 passes 1)

	struct Block
	{
		std::string token;
		Snapshot *snapshot = nullptr;     ///< null: declared, no xfer yet
		std::string owner;                ///< who ports it: a lane ("SAVE-4b") for a missing block, "SAVE-4a" ...
		std::vector<std::string> sections; ///< the state hash sections (GameLogic::hashState names) whose state the block holds
		unsigned lists = 0;               ///< bit n: the block is in list n
		std::function<void(Xfer &)> prepareLoad;
		std::function<void()> finishPreparation;
	};

	// ZH addSnapshotBlock; `sections` names the hash sections the block saves (a section may need several blocks: it is covered when all have an xfer)
	void addSnapshotBlock(const std::string &token, Snapshot *snapshot, unsigned lists, const std::string &owner, const std::vector<std::string> &sections);
	// the same, for a snapshot the registry keeps alive (an adapter of its owner's xfer); returns it
	Snapshot *addOwnedSnapshotBlock(const std::string &token, std::unique_ptr<Snapshot> snapshot, unsigned lists, const std::string &owner,
		const std::vector<std::string> &sections);
	// a simulation subsystem without an xfer yet: a save is refused while it is registered
	void declareBlockWithoutXfer(const std::string &token, unsigned lists, const std::string &owner, const std::vector<std::string> &sections);
	// state that load rebuilds from the START and the data (never changed by the logic once the game runs); `why` is the reason, shown in the coverage
	void declareRebuiltSection(const std::string &section, const std::string &why);
	// the snapshot of a declared block arrives (a later lane, or a subsystem created after the registry): replaces a declaration without an xfer
	void provideSnapshot(const std::string &token, Snapshot *snapshot, const std::string &owner);
	// lane SAVE-4b: the same, for a snapshot the registry keeps alive
	void provideOwnedSnapshot(const std::string &token, std::unique_ptr<Snapshot> snapshot, const std::string &owner);
	// Native load dependencies: stage temporary data after full framing validation, before any normal xfer.
	// The cleanup must not throw; it runs after post-processing or on any preparation/load failure.
	void setLoadPreparation(const std::string &token, std::function<void(Xfer &)> prepare, std::function<void()> cleanup);

	const std::vector<Block> &blocks() const { return m_blocks; }
	const Block *findBlock(const std::string &token) const;

	// "token (owner)" of every block of the list without an xfer, in registration order
	std::vector<std::string> missingBlocks(SnapshotList list = SNAPSHOT_SAVELOAD) const;
	// the sections of `stateSections` (a hash breakdown's names, in its order) that neither a block OF THE LIST nor a rebuilt declaration names (review r1:
	// a block of another list covers nothing in this one)
	std::vector<std::string> undeclaredSections(const std::vector<std::string> &stateSections, SnapshotList list = SNAPSHOT_SAVELOAD) const;
	// why a save of `stateSections` is refused ("" when it is not): the undeclared sections, then the blocks without an xfer
	std::string refusal(const std::vector<std::string> &stateSections, SnapshotList list = SNAPSHOT_SAVELOAD) const;

	// the whole list, or a named refusal (missing blocks, undeclared sections, an xfer that threw: the block's token and the error)
	bool saveSnapshot(const std::vector<std::string> &stateSections, std::vector<std::uint8_t> &out, std::string *error,
		SnapshotList list = SNAPSHOT_SAVELOAD);
	// every block of the list from the stream, then the post-process pass; a named error on the first problem (the state is then undefined: the caller
	// discards the game, as retail's "GUI:ErrorLoadingGame" returns to the shell). `stateSections` are the loading game's hash sections: a load is
	// refused for the same coverage reasons as a save (review r1)
	bool loadSnapshot(const std::vector<std::string> &stateSections, const std::vector<std::uint8_t> &bytes, std::string *error,
		SnapshotList list = SNAPSHOT_SAVELOAD);
	// lane SAVE-4e: the stream's framing alone (the header, every block's token in the list's order and its end inside the stream, the end token, nothing
	// after it), without loading a block: what a host can check of a save before it leaves its running game. False + a named *error
	bool checkFraming(const std::vector<std::uint8_t> &bytes, std::string *error, SnapshotList list = SNAPSHOT_SAVELOAD);
	// XferCRC over the list's blocks in order (each block's crc; no tokens); throws XferException when a block has no xfer
	std::uint32_t crcSnapshot(SnapshotList list) const;

	// the acceptance stops of the snapshot (S-4120 .. S-4122), reported with the save stops (CommandLogLoad::stopLines)
	static std::vector<std::string> stopLines();

	// ZH addPostProcessSnapshot: called by a snapshot's xfer while loading; run once, in the order queued, after every block has loaded
	void addPostProcessSnapshot(Snapshot *snapshot);

	// block level (tests, and the measurement of a block's size): the stream of exactly these blocks in this order, no refusal; and its load (the same
	// strictness as loadSnapshot, the post-process included)
	void saveBlocks(const std::vector<std::string> &tokens, std::vector<std::uint8_t> &out) const;
	void loadBlocks(const std::vector<std::string> &tokens, const std::vector<std::uint8_t> &bytes);

private:
	Block *find(const std::string &token);
	void walkBlocks(class XferLoad &x, const std::vector<std::string> &tokens, bool load, bool prepare = false); ///< framing, preparation or normal load

	std::vector<Block> m_blocks;
	std::vector<std::pair<std::string, std::string>> m_rebuilt; ///< (section, why)
	std::vector<Snapshot *> m_postProcess;
	std::vector<std::unique_ptr<Snapshot>> m_owned;
};
