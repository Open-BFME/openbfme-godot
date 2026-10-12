// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// The headless model of the Skirmish lobby: the slot logic of ZH SkirmishGameOptionsMenu.cpp (SkirmishGameOptionsMenuInit, handlePlayerSelection,
// handleColorSelection, handlePlayerTemplateSelection, handleTeamSelection, handleStartPositionSelection, startPressed, reallyDoStart), the combo
// contents of ZH GameNetwork/GUIUtil.cpp (PopulateColorComboBox, PopulatePlayerTemplateComboBox, PopulateTeamComboBox, the player type entries
// of SkirmishGameInfo) and the GameInfo / GameSlot methods they call (GameSlot::setState, GameInfo::setMap / setMapCRC / setMapSize /
// closeOpenSlots / getNumPlayers / startGame).  BFME1 SkirmishScreenState* (RefreshPlayerTypeCombo 0x00527220: Open / Closed / EasyAI /
// MediumAI / HardAI entries tagged with their SlotState) is the donor of the GUI side that AptSkirmish wires to this model.
//
// Nothing here knows about gadgets or Apt: the screen reads the entry lists, calls the setters and re-reads.

#pragma once

#include "GameClient/GUI/GameTextSource.h"
#include "GameClient/GUI/Skirmish/SkirmishSetup.h"

#include <string>
#include <vector>

// One combo box entry: the text, its colour (0xAARRGGBB) and its item data.
struct SkirmishComboEntry
{
	std::u16string text;
	std::uint32_t color = 0xFFFFFFFFu; // the text colour, for an image entry the tint
	int itemData = 0;
	std::string image;                 // the mapped image of an ImageComboBox entry (empty: a text entry)
};

class SkirmishGameSetup
{
public:
	SkirmishGameSetup(const SkirmishSetupSource &source, const GameTextSource *text);

	// ZH SkirmishGameOptionsMenuInit: the game info is reset, slot 0 is the player (SLOT_PLAYER, `userName`, no colour / template chosen), slot 1
	// is an easy AI (honors.getWins() <= 5), every other slot closed (GameInfo::clearSlotList), the seed and the starting cash are set and
	// the map is `mapName` (empty: the first multiplayer map of the cache, ZH SkirmishPreferences::getPreferredMap's default).
	// False (and `error` set) when the cache has no such map.
	bool init(const std::u16string &userName, std::uint32_t seed, const std::string &mapName, std::string *error);

	const SkirmishGameInfo &info() const { return m_info; }
	const SkirmishSetupSource &source() const { return m_source; }
	const std::string &lastError() const { return m_error; }

	// getDefaultMap(TRUE): the map a lobby without a "Map" preference starts on (empty when the cache has no multiplayer map).
	std::string defaultMapName() const;

	// ---- the combo contents (GUIUtil.cpp Populate*) -----------------------------------------------------------------------------------------
	// SkirmishScreenState::refreshPlayerTypeCombo (BFME1 0x00527220): the local slot gets one blank entry, the others Open, Closed and (when
	// the map allows AI) EasyAI, MediumAI, HardAI, BrutalAI (RotWK 0x841xxx: four AI levels, item data 2..5).  The map allows AI when one of its player records has the flag at +0x11 (not in MapCache's
	// fields: every cached multiplayer map is taken to allow AI [S-178]).
	std::vector<SkirmishComboEntry> playerEntries(int slot) const;
	// RotWK MpGameSetup faction population (0x844BD4, S-001 caveat): Random, then the store's templates in store order whose PlayableSide is true (+0x151)
	// and IsObserver false (+0x150), the text the template's DisplayName label (accessor 0x62772D), item data = the store index.  The read body does not
	// need a StartingBuilding, a "SIDE:" label or one entry per side; its mode / map / start and hero restrictions are not recovered [S-178].
	std::vector<SkirmishComboEntry> templateEntries() const;
	// RotWK colour population (0x8440C7): every configured colour is available; only in strategic mode (the flag at MpGameSetup+0x7C == 1, set here by
	// setStrategicMode) the AvailableInWotR = No colours are dropped.  Entries are image cells (BFME colour combos: AptRandomColor for random, AptWhiteBox
	// tinted with the colour); item data = colour index, -1 for random.  A colour another slot holds is not offered (ZH PopulateColorComboBox).
	std::vector<SkirmishComboEntry> colorEntries(int slot) const;
	// PopulateTeamComboBox: "Team:0" (item data -1) then "Team:1".. "Team:4" (item data 0..3), MAX_SLOTS / 2 of them.
	std::vector<SkirmishComboEntry> teamEntries() const;
	// MapMetaData::bfme_getBaseDisplayName / bfme_getDisplayName / getDescription / bfme_getDescriptionFirstLine (open-bfme-2 GUI/MapMetaData_*.cpp,
	// target RotWK 0x00300AEA / 0x00300C7E / 0x003009CD): the cache's displayName and description are game text labels ("$Map:Camera_Demo": the
	// `$` marks a label of the map's own map.str).  An unknown label falls back to the file name without ".map" (`$` labels) or to the label.
	std::u16string mapBaseDisplayName(const MapCacheEntry &map) const;
	std::u16string mapDisplayName(const MapCacheEntry &map, bool includePlayerCount) const; // " (n)" appended from two players
	std::u16string mapDescription(const MapCacheEntry &map) const;
	std::u16string mapDescriptionFirstLine(const MapCacheEntry &map) const;
	// populateMapListboxNoReset (open-bfme-2 MapUtil.cpp:814): the multiplayer maps with a display name, by player count then display name
	// (case-insensitive).  Indexes into source().maps().
	std::vector<int> listedMaps() const;
	// PopulateStartingCashComboBox: one entry per choice, the text "GUI:StartingMoneyFormat" with the amount, item data = the amount.
	std::vector<SkirmishComboEntry> startingCashEntries() const;

	// ---- the handlers (SkirmishGameOptionsMenu.cpp handle*Selection) ----------------------------------------------------------------------
	// handlePlayerSelection: slot 0 and out of range are ignored; GameSlot::setState (name = the game text of the state).
	bool setSlotState(int slot, SlotState state, const std::u16string &title);
	// handleColorSelection: refused (false) when another slot has the colour.
	bool setSlotColor(int slot, int color);
	bool setSlotTemplate(int slot, int playerTemplate);
	bool setSlotTeam(int slot, int team);
	// lane CAH-1: the slot's Create-a-Hero from the lobby's Hero combo (BFME2 0x43DD34 stores the choice on the GameSlot; the record travels whole,
	// SkirmishGameSlot::setCreateAHero); null clears it. False + *error when the record does not fit the setup
	bool setSlotCreateAHero(int slot, const CreateAHeroHero *hero, std::string *error);
	// handleStartPositionSelection: refused when another slot has the position.
	bool setSlotStartPos(int slot, int position);
	bool setStartingCash(int cash);
	// SkirmishMapSelectMenu OK: GameInfo::setMap + setMapCRC + setMapSize + the start positions reset.  False when the cache has no such map.
	bool setMap(const std::string &mapName);
	// MpGameSetup+0x7C: strategic (War of the Ring) mode.  Filters the colours to AvailableInWotR; slots 6 and 7 are hidden by the screen.
	// lane RULES-1: a change of mode resets the rules to the new mode's defaults (AptSkirmish::InitTheGame RW 0x9288D0 takes the mode's defaults)
	void setStrategicMode(bool strategic);
	bool strategicMode() const { return m_strategic; }
	// Slot 0's name (the profile name).
	void setPlayerName(const std::u16string &name);

	// ---- lane RULES-1: the Rules tab (AptMpGameRules, Common/MpGameRules.h) ----
	// the rule tables' mode: RULES_MODE_WOTR in strategic mode, else RULES_MODE_SKIRMISH
	int rulesMode() const { return m_strategic ? RULES_MODE_WOTR : RULES_MODE_SKIRMISH; }
	// a combo row's entries (AptMpGameRules::PopulateComboBoxes RW 0x986B1C): the choice label's text formatted with the value (UnicodeString::format,
	// one %d), the default choice followed by the text of "VALUE:Default"; item data = the value
	std::vector<SkirmishComboEntry> ruleComboEntries(const MpRuleCombo &combo) const;
	// the rule's display name (RW 0x960B14: the game text of its tag; "Invalid Rule" for a rule >= 10)
	std::u16string ruleName(int rule) const;
	// the host's change of one rule (AptMpGameRules message handler RW 0x986776 stores the gadget's value; the panel's ruleChanged RW 0x8421E0 copies the
	// block into the game). AllowCustomHeroes 0 also clears every slot's Create-a-Hero: RW 0x842A85 disables each hero combo and selects its first entry
	// ("-", no hero) through the hero handler. False for a rule >= 10
	bool setRule(int rule, int value);
	// AptMpGameRules::Reset (RW 0x98674C): the mode's defaults (RW 0x960A74); OpenBFME's Legendary Heroes goes back to Off with them
	void resetRules();
	// OpenBFME's Legendary Heroes factor (1 off .. 4); false for a factor the rule does not offer
	bool setLegendaryHeroes(int factor);
	// the Legendary Heroes combo's entries: Off, 2x, 3x, 4x (item data = the factor; Off carries the " *" default mark like a retail row)
	std::vector<SkirmishComboEntry> legendaryHeroesEntries() const;

	// ---- lane MP-2: the LAN game setup (AptLanLobby) ----
	// a network game: the local player is `localSlot` and every human slot is a player (its combo shows the name, never the slot states)
	void setNetwork(int localSlot)
	{
		m_network = true;
		m_localSlot = localSlot;
	}
	bool network() const { return m_network; }
	int localSlot() const { return m_localSlot; }
	// the network's setup replaces the model's (the host's options, LANAPI)
	void replaceInfo(const SkirmishGameInfo &info) { m_info = info; }
	// the slot's player combo shows the player's name (the local slot, or a human of a network game)
	bool slotShowsName(int slot) const { return slot == m_localSlot || (m_network && slot >= 0 && slot < MAX_SLOTS && m_info.slots[slot].isHuman()); }

	// ---- start (startPressed, reallyDoStart) -----------------------------------------------------------------------------------------------
	// lane GAMEMODES-1: RotWK's start check, AptMpGameSetup RW 0x8431B8 (BFME2 0x840BDF, decomp attempt reverse/attempts/0x00440bdf.cpp and
	// GUI/AptMpGameSetupRva00440BDF.cpp: recovered, not byte-matching; RotWK added LAN:TooFewPlayers and GUI:DisabledFaction). The owner's lines go out through
	// its vslot 0x44 with a kind (0 / 1 / 2); a refusal is RW 0x8406DB's (vslot 0x44 kind 1). In order:
	//   no map in the cache -> LAN:NoMapSelected; the map's player limits (RW 0x9758E3: MapCache + 0x20 clamped to 8 (RW 0xC53F2C); RW 0x97597F: 0 outside the
	//   War of the Ring) with no maximum -> refused silently; per slot: a human without the map (no transfer) -> GUI:PlayerNoMap (kind 2), a human not accepted,
	//   a non-observer human's clan tag (none here: counted missing); an occupied non-observer slot counts as a user (a human one also as a human), its team
	//   (-1: a random team), its start position (-1: none); then the (flags 0x10 or War of the Ring enemy) gate on humans <= 1 -> GUI:NeedHumanPlayers (kind 0),
	//   a missing map -> GUI:CouldNotTransferMap, users > maximum -> LAN:TooManyPlayers, users < minimum -> LAN:TooFewPlayers, GlobalData + 0xA44 (the
	//   command line's minimum players, RW 0x7BA380; 0) with no human -> GUI:NeedHumanPlayers, War of the Ring: GUI:DisabledFaction / GUI:NeedStartSpots,
	//   users < + 0xA44 -> GUI:NeedHumanPlayers, teams + random teams < + 0xA44 -> LAN:NeedMoreTeams, teams + random teams < 2 -> the War of the Ring
	//   enemy flag: GUI:NeedWOTREnemy, else GUI:SandboxMode (kind 0) and the game goes on as a SANDBOX; the clan game rule (rule 1 == 1): the clan errors;
	//   flags 0x20 with a human not accepted -> GUI:NotifiedStartIntent; the NAT check (the owner's vslot 0x5C: a skirmish / LAN owner is ready).
	struct StartCheck
	{
		bool ok = false;
		std::u16string refusal;              ///< the refusal's text (empty when ok, or for the silent refusal of a map without players)
		std::string refusalLabel;            ///< its game text label
		struct Line
		{
			std::u16string text;
			int kind = 0; ///< the owner's vslot 0x44 kind: 0 a local line, 1 the refusal (RW 0x8406DB), 2 a line to every player
		};
		std::vector<Line> lines; ///< every vslot 0x44 call in order: GUI:PlayerNoMap (2), GUI:SandboxMode (0), the refusal's (1 and / or 2, or 0)
		bool sandbox = false;                ///< GUI:SandboxMode was shown: one team (GameInfo::isSandbox decides in the game)
		int numHumans = 0;                   ///< + 0x3A8 for the countdown
		std::vector<std::string> stops;      ///< "[S-3610] ..." when the check met what it does not port (a War of the Ring setup)
	};
	// `flags` is the setup's + 0x3A4 (0x10 needs two humans, 0x20 notifies the start intent, 0x100 the War of the Ring needs an enemy: none of them in a
	// skirmish); `willTransfer` RW 0x702617 (the map can be sent to a player without it)
	StartCheck checkStart(int flags = 0, bool willTransfer = false) const;
	// the skirmish lobby's form: true when the game may start; otherwise false and `title` / `text` hold the message box of the skirmish owner's vslot 0x44
	// (RW 0x928482: only kind 1 shows, MessageBoxOk(L"", text) RW 0x84ED5D: an empty title); `text` is empty for a refusal without a kind 1 line
	bool validateStart(std::u16string &title, std::u16string &text) const;
	// reallyDoStart: GameInfo::startGame -> closeOpenSlots; the message ZH appends (MSG_NEW_GAME).
	NewGameMessage makeNewGameMessage();

	// The text of a slot state (GameSlot::setState): GUI:Open, GUI:Closed, GUI:EasyAI, GUI:MediumAI, GUI:HardAI, GUI:BrutalAI.
	std::u16string stateText(SlotState state) const;
	// The faction a template index names (null for random / observer / out of range).
	const SkirmishFaction *faction(int playerTemplate) const;

private:
	void setSlotStateInternal(SkirmishGameSlot &slot, SlotState state, const std::u16string &name);
	const MapCacheEntry *findMap(const std::string &mapName) const;
	std::u16string label(const std::string &label) const { return fetchOrMissing(m_text, label); }

	const SkirmishSetupSource &m_source;
	const GameTextSource *m_text;
	SkirmishGameInfo m_info;
	std::string m_error;
	bool m_strategic = false;
	bool m_network = false; // lane MP-2
	int m_localSlot = 0;
};
