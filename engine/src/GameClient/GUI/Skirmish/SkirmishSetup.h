// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// The data and interfaces of the Skirmish lobby (menus-apt.md 3.4 "AptSkirmish" / MpGameSetup, build step A6).
//
//   * SkirmishSetupSource: what the lobby lists (factions, colours, maps, start money).  Retail reads it from TheThePlayerTemplateStore,
//     TheMultiplayerSettings, TheMapCache and TheGlobalData; the port asks an interface so LOGIC-1's PlayerTemplateStore can answer it.
//     IniSkirmishSetupSource (IniSkirmishSetupSource.h) is the interim implementation [S-178].
//   * SkirmishGameInfo / SkirmishGameSlot: ZH GameNetwork/GameInfo.h GameInfo and GameSlot reduced to the fields a skirmish uses
//     (8 slots: state, name, colour, start position, player template, team; map name, CRC, size, content mask; seed; starting cash).
//     BFME1 SkirmishGameInfo_xfer.cpp lists the same fields plus the "orig*" copies (saved by GameLogic once the random choices are
//     resolved: not the lobby's job).
//   * NewGameSink: where StartGame posts the new-game message (ZH SkirmishGameOptionsMenu.cpp reallyDoStart: TheMessageStream->
//     appendMessage(MSG_NEW_GAME) with GAME_SKIRMISH).  LOGIC-1 implements it.
//
// RANDOM CHOICES [S-178]: a slot whose colour, player template or start position is still -1 (random) is handed over UNRESOLVED.  Nothing in the tree
// resolves them yet: LOGIC-1's PlayerList::applySkirmishSlots / SkirmishSetup take already named factions, explicit RGB colours and start indexes, and
// LiveGame applies the slots before its logic RNG exists.  The consumer that must be written (a skirmish-start adapter in the logic, with RNG-call and
// state golden vectors across peers before it may claim a deterministic start) has to follow RETAIL'S ORDER (RotWK game.dat, S-001 caveat; clean BFME2
// 1.06 agrees; static disassembly, not an executed oracle):
//   1. GameLogic start 0x62DC3E calls populateRandomStartPosition (0x62D7EA) FIRST, then populateRandomSideAndColor (0x62DC9D -> 0x62D44C)
//      (BFME2: 0x644D09 -> 0x64485C, then 0x644D0F -> 0x6444BE).  Zero Hour's side-and-colour-before-start order is the WRONG order for this target.
//   2. Start positions: explicit valid positions are reserved first; occupied non-observer slots are then processed in ASCENDING slot order (a
//      random vacant-start branch plus team / distance placement branches with a fixed candidate and tie order); observers are placed after.
//      The geometry helpers must go through the numeric facade.
//   3. Then, for each occupied slot in ASCENDING order: resolve that slot's FACTION, then its COLOUR, before advancing to the next slot.  A random or
//      invalid faction first DISCARDS `seed % 7` LogicRandom(0,1) draws (seed accessor 0x6D3204), builds the ordered candidate list constrained by the
//      chosen start and the mode, and draws LogicRandom(0,1000) % candidateCount with its validity / retry logic; a random colour draws an inclusive index
//      0..NumColors-1 and rejects colours other slots hold.  An explicit valid choice skips its draws.  Every discard / retry draw, the range reduction and
//      the RNG state matter: a shared seed alone does not give lockstep.
// A consumer must refuse (never silently replace with a preferred colour, a null faction or start 0) a message for which unresolvedRandomChoices() is
// not empty until it has resolved them in that order.  AptSkirmish reports the unresolved state at StartGame (note `skirmish-random-unresolved`).

#pragma once

#include "GameClient/MapCache.h"
#include "GameNetwork/GameInfo.h"

#include <cstdint>
#include <string>
#include <vector>

// One PlayerTemplate of the store, in store order (the index is the combo item data and GameSlot::m_playerTemplate).
struct SkirmishFaction
{
	std::string templateName;   // `PlayerTemplate FactionMen`
	std::string side;           // Side = Men
	std::string displayLabel;   // DisplayName = INI:FactionMen
	std::string startingBuilding;
	std::vector<std::string> buildableHeroesMP;
	bool playableSide = false;
	bool isObserver = false;
	std::uint32_t preferredColor = 0; // 0xRRGGBB
};

// MultiplayerColor (Multiplayer.ini): the lobby colour list, in file order (the index is GameSlot::m_color).
struct SkirmishColor
{
	std::string name;         // ColorBlue
	std::string tooltipName;  // Color:Blue (a game text label)
	std::uint32_t rgb = 0;    // RGBColor, 0xRRGGBB
	bool availableInWotR = true;
};

class SkirmishSetupSource
{
public:
	virtual ~SkirmishSetupSource() = default;
	virtual const std::vector<SkirmishFaction> &factions() const = 0;
	virtual const std::vector<SkirmishColor> &colors() const = 0;
	// The map cache, in file order.
	virtual const std::vector<MapCacheEntry> &maps() const = 0;
	// GlobalData DefaultStartingCash.
	virtual int defaultStartingCash() const = 0;
	// MultiplayerSettings InitialCredits*: the starting money choices, ascending.
	virtual const std::vector<int> &startingCashChoices() const = 0;
	// ZH GameInfo::setMap: bit 1 = the map is known, 2 = preview .tga, 4 = map.ini, 8 = map.str, 16 = solo.ini, 32 = assetusage.txt, 64 = readme.txt.
	virtual int mapContentsMask(const MapCacheEntry &map) const = 0;
	// lane UI-2: TheFileSystem->doesFileExist (RW 0xA14AEB) for a path in the mounted archives, '\\' separators (the lobby's map picture
	// `<map>_pic.tga`, RW 0x975F23). A source without a file system answers false and says so in the screen's report (AptSkirmish).
	virtual bool fileExists(const std::string &path) const = 0;
	virtual bool hasFileSystem() const = 0;
};

class NewGameSink
{
public:
	virtual ~NewGameSink() = default;
	// ZH reallyDoStart: appends MSG_NEW_GAME to the message stream.  The consumer starts the game on the next logic frame (LOGIC-1).
	// A message may carry unresolved random choices (SkirmishGameInfo::unresolvedRandomChoices): see the RANDOM CHOICES contract at the top of this file.
	virtual void queueNewGame(const NewGameMessage &message) = 0;
	// ZH startPressed MessageBoxOk(title, text): the lobby refused to start (unknown map, too many players).
	virtual void startRefused(const std::u16string &title, const std::u16string &text) = 0;
	// ZH SkirmishGameInfo::setSeed(GetTickCount()) at lobby entry, InitGameLogicRandom(seed) at start: the seed is injected so a lobby is
	// deterministic and a replay can record it.
	virtual std::uint32_t newGameSeed() = 0;
};

class RecordingNewGameSink : public NewGameSink
{
public:
	std::vector<NewGameMessage> messages;
	std::vector<std::pair<std::u16string, std::u16string>> refusals;
	std::uint32_t seed = 1;
	void queueNewGame(const NewGameMessage &message) override { messages.push_back(message); }
	void startRefused(const std::u16string &title, const std::u16string &text) override { refusals.emplace_back(title, text); }
	std::uint32_t newGameSeed() override { return seed; }
};

// The Skirmish profiles (ZH SkirmishPreferences user name; BFME's profile list at SkirmishScreenRefreshProfile.cpp copyStringAt14).  Persistence
// is the host's [S-179]; MemorySkirmishProfiles is the in-memory implementation.
class SkirmishProfileStore
{
public:
	virtual ~SkirmishProfileStore() = default;
	virtual std::vector<std::u16string> names() const = 0;
	virtual std::u16string current() const = 0; // empty when there is none
	virtual bool add(const std::u16string &name) = 0;   // false: empty or a duplicate (compared without case)
	virtual bool remove(const std::u16string &name) = 0;
	virtual bool setCurrent(const std::u16string &name) = 0;
};

class MemorySkirmishProfiles : public SkirmishProfileStore
{
public:
	std::vector<std::u16string> names() const override { return m_names; }
	std::u16string current() const override { return m_current; }
	bool add(const std::u16string &name) override;
	bool remove(const std::u16string &name) override;
	bool setCurrent(const std::u16string &name) override;

private:
	int find(const std::u16string &name) const;
	std::vector<std::u16string> m_names;
	std::u16string m_current;
};
