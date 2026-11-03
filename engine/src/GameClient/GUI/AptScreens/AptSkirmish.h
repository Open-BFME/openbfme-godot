// OpenBFME. GPL-3.0.
//
// AptSkirmish: the engine side of Skirmish.apt and of the lobby movie it hosts (SkirmishOpenPlay.swf -> MpGameSetup.swf).  CodePrefix
// "AptSkirmish"; spec menus-apt.md 1.3, 3.4 rows "AptSkirmish" / "MpGameSetup" / "AptMapPreview", build steps A4 and A6.
//
// TARGET FACTS (RotWK game.dat string table, S-001 caveat):
//   * AptSkirmish::* (0x0087E7C4..0x0087EB1C): InitGadgets (screen reference) and the commands SuppressStrategicPromo, OnProfilePopupCancel,
//     OnAddProfileAccept, OnExitStatsScreen, OnChangeProfile, OnChangeProfileMenu, OnDeleteProfileMenu, OnNewProfileMenu, OnStatsMenu,
//     OnDeleteProfile, StartGame, Back, Exit, OnClosed, OnInitialized.
//   * MpGameSetup::InitGadgets / IsInitialized / HostMode / OnTabSelect / OnReadyPress / OnKickPlayer / OnSortPlayers / OnSortName / OnSortIcons,
//     AptMpGameRules::ShowChat / ShowClans / InitGadgets / Reset, MpGameRules::NumCheckBoxes / NumComboBoxes, AptMpChat::InitGadgets / Send,
//     AptMpClans::InitGadgets / WebSite / Delete, AptMapPreview::GameMapType / MapGadgetInit / Picture.
//   * Every gadget placeholder of Skirmish.apt and MpGameSetup.apt carries a Construct script that sets the clip variables `_type`, `_Init`
//     (the screen reference to call: AptSkirmish::InitGadgets, MpGameSetup::InitGadgets, AptMapPreview::MapGadgetInit, AptMpGameRules::
//     InitGadgets, AptMpChat::InitGadgets, AptMpClans::InitGadgets) and `_Load` (the skin) [S-172].
//   * The movie reads `MpGameSetup::HostMode` (the host sees `MapList`; `_client` shows the read-only variant), AptMpGameRules::ShowChat /
//     ShowClans (the chat and clan tabs), MpGameRules::NumCheckBoxes / NumComboBoxes (the generated rule rows) and
//     AptMapPreview::GameMapType (Skirmish<OpenPlay|Strategic>.swf), and `_root.vShowAddProfile` (Skirmish.apt frame action: opens the add-profile
//     popup and sends OnNewProfileMenu).
//   * MpGameSetup::InitGadgets (RW 0x840906; open-bfme-2 GUI/MpGameSetupOnInitGadget.cpp names it _bfme_onInitGadget): `MapList` stores the list and sets five column
//     widths {8, 2, 70, 10, 10}; any other name is `<slot>/<leaf>` ("%d" at the start, the leaf after the last marker) with the leaf Player (combo reset
//     plus a list tooltip), PlayerTemplate, Team, Color, Handicap or Hero (combo reset); a slot index >= 6 is hidden when the screen's flag at +0x7C is 1.
// DONOR (ZH SkirmishGameOptionsMenu.cpp / GUIUtil.cpp via SkirmishGameSetup, BFME1 SkirmishScreen*.cpp, AptMapPreview*.cpp, MpGameSetup*.cpp):
//   * AptMapPreview::MapGadgetInit (BFME1 AptMapPreview.cpp:157): MapInfo / MapDescription are list boxes (enabled, audio feedback), MapPicture a window,
//     CurrentMap a window whose children (up to 8) are hidden; every call ends in the map-name update (5217A0): title through setAptText
//     "APT:MapTitle" (the display name with the player count, " " for no map), the MapInfo list holds the one entry `getDescription()`, MapDescription
//     the description's first line, the picture is `<map>_pic.tga` or the image "MissingMap".
//   * Profile list: BFME1 SkirmishScreenRefreshProfile.cpp (the list of names, the current one selected); the RotWK movie uses a list box
//     (Skirmish::SelectProfile) and a text entry (Skirmish::CreatePersonaEntry).
// lane RULES-1: the rule rows are RotWK's AptMpGameRules (AptSkirmishRules.cpp, Common/MpGameRules.h) plus OpenBFME's Legendary Heroes row.
// INFERENCE (stops S-178, S-179): what Hero and Handicap contain, how profiles are stored, whether Skirmish forces a profile
// first (the movie has the hook `vShowAddProfile`; the engine sets it when no profile exists), and everything under S-175 (stats screens).

#pragma once

#include "GameClient/GUI/AptScreen.h"
#include "GameClient/GUI/ShellEnvironment.h"
#include "GameClient/GUI/Skirmish/SkirmishGameSetup.h"
#include "GameClient/GUI/Skirmish/LivingWorldShellInfo.h" // lane WOTR-1
#include "GameClient/OptionPreferences.h"

struct CreateAHeroHero;
class AptMessageBox;

#include <array>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

class AptSkirmish : public AptScreen
{
public:
	AptSkirmish(WindowManager &windows, Shell &shell, ShellEnvironment &environment);
	~AptSkirmish() override;

	// The AptSkirmish::* command names of the RotWK binary (0x0087E7C4..0x0087EB1C): InitGadgets is the screen reference, the other
	// fifteen are commands.
	static const std::vector<std::string> &retailNames();
	// The names of the lobby movie the screen registers (MpGameSetup, AptMpGameRules, AptMpChat, AptMpClans, AptMapPreview): commands, providers,
	// screen references and the render component, in binary order.
	static const std::vector<std::string> &lobbyNames();

	// ---- state (tests, the host) ----------------------------------------------------------------------------------------------------------
	// Null when the environment lacked the setup source or the new-game sink (reported) or the map cache had no multiplayer map.
	const SkirmishGameSetup *setup() const { return m_setup.get(); }
	SkirmishProfileStore &profiles() { return *m_profiles; }
	// lane UI-6: the kept game (SkirmishGameRecord.h): what the lobby restored at its start ("" when it began from the defaults), and the stop of a kept
	// game it could not use
	const std::string &restoredGame() const { return m_restoredGame; }
	// RW 0x927FEF (AptSkirmish's vtable slot at 0xC7E898, run when the screen goes): the lobby's game into the current profile's entries (RW 0x821EBD: the
	// map, the game and the hero indexes), then the preferences' write. Also before a profile change (RW 0x9293F5 at 0x929440)
	void keepGame();
	void runShutdown(bool *immediate) override;
	const std::string &lastError() const { return m_error; }
	// How often the movie sent AptSkirmish::OnInitialized.
	int initializedCount() const { return m_initializedCount; }
	// How often the movie sent AptSkirmish::OnNewProfileMenu (it does when it opens the add-profile popup).
	int newProfileMenuCount() const { return m_newProfileMenuCount; }
	// The retail flag at +0x6C2: the popup's accept button is disabled (an empty raw text).
	bool profileAcceptDisabled() const { return m_acceptDisabled; }
	GameWindow *slotGadget(int slot, const std::string &leaf) const;
	GameWindow *slotScrollWindow() const { return m_slotScroll; }
	GameWindow *slotRangeWindow() const { return m_slotRange; }
	int firstVisibleSlot() const { return m_firstVisibleSlot; }
	// lane ONLINE-5: the state the slot's Ready box was last given (SetReadyState: "_none", "_enabledChecked" ...; "" before the first)
	const std::string &readyIcon(int slot) const
	{
		static const std::string none;
		return slot >= 0 && slot < MAX_SLOTS ? m_readyIcon[(size_t)slot] : none;
	}
	GameWindow *mapListWindow() const { return m_mapList; }
	GameWindow *mapCapacityFilterWindow() const { return m_mapCapacityFilter; }
	const std::vector<GameWindow *> &mapStartSpotWindows() const { return m_currentMapChildren; } // lane FB7-1 (tests)
	// lane UI-2: the map preview window (MpGameSetup's CurrentMap, MpMapWindow.wnd) and its start-spot buttons
	GameWindow *currentMapWindow() const { return m_currentMap; }
	const std::vector<GameWindow *> &currentMapSpots() const { return m_currentMapChildren; }
	GameWindow *mapInfoWindow() const { return m_mapInfo; }
	GameWindow *profileListWindow() const { return m_profileList; }
	GameWindow *profileComboWindow() const { return m_bfme1ProfileCombo; }
	GameWindow *mapTypeWindow() const { return m_bfme1MapType; }
	bool bfme1OfficialMaps() const { return m_bfme1OfficialMaps; }
	void recordsChanged();
	// lane END-1: dead gadgets are dropped first (a window the movie's popup already destroyed must not be handed out: the lobby hook wrote into one)
	GameWindow *profileEntryWindow()
	{
		pruneDeadGadgets();
		return m_profileEntry;
	}
	// lane UI-1: the chat gadgets AptMpChat::InitGadgets gave (null until the movie places them)
	GameWindow *chatEntryWindow()
	{
		pruneDeadGadgets();
		return m_chatEntry;
	}
	GameWindow *chatListWindow()
	{
		pruneDeadGadgets();
		return m_chatList;
	}
	GameWindow *chatPlayersWindow()
	{
		pruneDeadGadgets();
		return m_chatPlayers;
	}
	// lane CAH-1: the hero combo's choice of a slot (GameSlot + 0x5C: -1 none, -2 random, else the index into the hero list)
	int slotHero(int slot) const { return slot >= 0 && slot < MAX_SLOTS ? m_slotHero[(size_t)slot] : -1; }
	// The map cache keys in the order of the MapList rows (strategic mode: the scenario names).
	const std::vector<std::string> &mapListKeys() const { return m_mapKeys; }
	const std::vector<int> &mapListPlayers() const { return m_mapPlayers; } // lane MODUI-1: each row's player count, in row order
	// lane OWNERFIX-3: the conquest state (RW 0x8460B5's + 0xF4) and the image of each MapList row, in row order
	const std::vector<int> &mapListStates() const { return m_mapStates; }
	const std::vector<std::string> &mapListImages() const { return m_mapImages; }
	// lane RULES-1: the Rules tab's gadgets (AptMpGameRules::InitGadgets files "RuleComboBox_<n>" / "RuleCheckBox_<n>" under n); null when not placed
	GameWindow *ruleComboWindow(int n) const { return n >= 0 && n < (int)m_ruleCombos.size() ? m_ruleCombos[(size_t)n] : nullptr; }
	GameWindow *ruleCheckWindow(int n) const { return n >= 0 && n < (int)m_ruleChecks.size() ? m_ruleChecks[(size_t)n] : nullptr; }
	// the gadget that shows a rule (AptMpGameRules + 0x7C), and OpenBFME's Legendary Heroes combo
	GameWindow *ruleWindow(int rule) const { return (unsigned)rule < (unsigned)NUM_GAME_RULES ? m_ruleWindows[(size_t)rule] : nullptr; }
	GameWindow *legendaryHeroesWindow() const { return m_legendaryCombo; }
	// lane CHEAT-1: OpenBFME's Cheat Mode check row (null while the screen does not offer it)
	GameWindow *cheatModeWindow() const { return m_cheatCheck; }
	AptMessageBox *ruleBox() const { return m_ruleBox.get(); } // lane OWNERFIX-2: the Rules tab's warning box (APT:CommandPointsTooHigh), for the harness
	bool rulesPopulated() const { return m_rulesPopulated; }
	// the rule warning boxes shown (title, text), for tests
	const std::vector<std::pair<std::u16string, std::u16string>> &ruleWarnings() const { return m_ruleWarnings; }
	bool startAvailable() const { return m_profileState == kStateMain || m_profileState == kStateClosed; }
	int profileState() const { return m_profileState; } // lane MENUWORK-1: the menu inventory's evidence

	// ---- lane WOTR-1: the War of the Ring setup (RW 0x929745 with the strategic flag RW 0xDB6BF0) ----
	// AptMapPreview::GameMapType answers "Strategic" (the movie loads SkirmishStrategic.swf), the colours drop AvailableInWotR = No, slots 6 / 7 are hidden,
	// the StrategicScenarioType combo holds the scenarios (RW 0x976B49) and StartGame posts the message with NewGameMessage::livingWorldCampaign
	void enterStrategicMode(const LivingWorldShellInfo &info);
	void leaveStrategicMode(); // WOTR2 milestone 8: the LAN lobby's Open Play page after its War of the Ring page (mode 0 again)
	bool strategicMode() const { return m_strategic; }
	const std::string &selectedScenario() const { return m_scenario; }
	// the StrategicScenarioType combo's row of that scenario (false: no such scenario); `notify` false: a LAN joiner mirrors the host's choice (nothing sent)
	bool selectScenario(const std::string &name, bool notify = true);
	// WOTR2 milestone 8 (tests): `slot`'s start region set directly (the scripted LAN war: each player its own slot; retail's preview click goes to the
	// next slot without a start, clickRegion); the change goes to the host / the players as any slot change. False when the region is not selectable
	bool pickStartRegion(int slot, int region);
	GameWindow *scenarioCombo() const { return m_scenarioCombo; }
	// lane WOTR-2: the strategic setup's map preview (AptMapPreview's LivingWorld gadget, + 0x68). A strategic slot's start position is a region index of
	// the selected scenario's region campaign (RW 0x60E8DB(startPos)). The device draws the map in the gadget's rectangle and reports the region under
	// the mouse (hoverRegion, RW 0x976D64) and clicks (clickRegion: RW 0x845830's start position branch with RW 0x975C3A)
	GameWindow *livingWorldWindow()
	{
		pruneDeadGadgets();
		return m_livingWorldWindow;
	}
	GameWindow *territoryDescriptionWindow()
	{
		pruneDeadGadgets();
		return m_territoryDescription;
	}
	GameWindow *scenarioDescriptionWindow()
	{
		pruneDeadGadgets();
		return m_scenarioDescription;
	}
	const LivingWorldSetupCampaign *strategicCampaign() const;
	bool regionSelectable(int region) const;  // RW 0x975D3F / 0x97572C: an enabled region outside DisallowStartInRegions, not a historical scenario
	void hoverRegion(int region);             // RW 0x976D64 (-1: the mouse left every region)
	bool clickRegion(int region, bool left);  // true when the click was taken
	// the start regions the preview tints (RW 0x9770CF): region index -> the holder's slot; a slot's colour is its MultiplayerColor
	std::vector<std::pair<int, int>> regionHolders() const;
	int nextStartSlot() const { return nextSelectableStartSlot(0); } // RW 0x975C3A(0): whose colour a free region under the mouse shows (RW 0x975E04)
	int hoveredRegion() const { return m_hoverRegion; }
	void showTerritoryDescription(int region); // RW 0x976C4D into StrategicTerritoryDescription

protected:
	void applyScenarioLimits(); // lane WOTR-2: the chosen scenario's limits, factions and historical starts for the start check (single player and LAN)
	void updateExtendedRows();
	WindowMsgHandledType gadgetMessage(GameWindow *from, std::uint32_t msg, WindowMsgData data1, WindowMsgData data2) override;

	// ---- lane MP-2: the LAN lobby (AptLanLobby, LanLobby.apt) hosts the same MpGameSetup movie with a network model ----
	// `lan`: only the lobby movie's names are registered (MpGameSetup, AptMpGameRules, AptMpChat, AptMpClans, AptMapPreview), not AptSkirmish's own
	// commands, profiles and update; the model is a network game (SkirmishGameSetup::setNetwork)
	AptSkirmish(WindowManager &windows, Shell &shell, ShellEnvironment &environment, const char *filename, const char *codePrefix, bool lan);
	// after the local player changed a slot or the map through the gadgets (the LAN lobby sends it on)
	virtual void lobbyChanged() {}
	// may the local player change the slot's combos (-1: the map); a slot it may not is shown disabled
	virtual bool lobbyEditable(int slot) const
	{
		(void)slot;
		return true;
	}
	// MpGameSetup::OnReadyPress / OnKickPlayer and AptMpChat::Send (the skirmish lobby has no use for them: reported)
	// lane CAH-1: the owner's applySlotHero (BFME2 MpGameSetup owner vslot 6 after 0x43DD34): the skirmish lobby stores the record on the slot; the LAN
	// lobby asks the host (LANAPI::requestSlotCreateAHero). `hero` null: no hero
	virtual bool applySlotHero(int slot, const CreateAHeroHero *hero);
	virtual void onReadyPress(const std::string &argument);
	virtual void onKickPlayer(const std::string &argument);
	virtual void onChatSend(const std::u16string &text);
	// lane ONLINE-5: the setup panel's flags (AptMpGameSetup + 0x3A4, the constructor's second argument): the online custom match screen passes
	// 0x200001F0 (RW 0x9AF464), the LAN lobby 0x400001A0 (RW 0x848C6B). Bit 0x20 is the Ready column (RW 0x84214C) and the host's start notice (RW
	// 0x8431B8: GUI:NotifiedStartIntent). The skirmish screen's own value: 0x10000007 (lane OWNERFIX-3: RotWK's AptSkirmish constructor pushes it, RW
	// 0x929773; BFME2 decomp AptSkirmish.cpp the same): no 0x20 / 0x40; bit 0 (1) reads the profile's battle honors into the map list (RW 0x8460B5)
	virtual int setupFlags() const { return 0x10000007; }
	// the slot's accepted flag (GameSlot + 8, RW 0x842185): the model's; the LAN lobby's are LANAPI's
	virtual bool slotAccepted(int slot) const;
	// lane ONLINE-5: a function of the MpGameSetup movie (RW 0x841330 with the owner's Apt path, vslot 0x58): false when the movie does not have it (yet)
	virtual bool callSetupMovie(const std::string &function, const std::vector<std::string> &args);
	// lane ONLINE-5: AptMpGameSetup::UpdateReadyIcon (RW 0x84211D, BFME2 0x43FB5C byte-matched decomp MpGameSetupSlots.cpp): the slot's Ready box through
	// the movie's SetReadyState(slot, state): "_none" without flag 0x20 or for an empty slot; slot 0 (the host) and an AI "_disabledChecked"; a human
	// "_enabledChecked" / "_enabledUnchecked" for the local player's own box (the only one that takes a press), "_disabledChecked" /
	// "_disabledUnchecked" for the others. `force`: sent even when the box showed that state already (the boxes were made again)
	void updateReadyIcon(int slot, bool force = false);
	// MpGameSetup::OnReadyPress without an argument (RW 0x841590's first branch, BFME2 0x43EFD3: + 0x2B9, the slots are refreshed): the movie's Ready
	// boxes were just made (MpGameSetup.apt: the first box's first frame sends it once, vReadyInitialized) and get their states
	void readyBoxesMade();
	// the per-update part (RW 0x846BA4's test of + 0x2B9): boxes whose state could not be given yet (the setup movie defines SetReadyState a frame
	// after its boxes ask) get it now. Every screen that hosts the setup movie runs it from its update
	void serviceReadyBoxes();
	// may the slot's combos be changed now: lobbyEditable, and (RW 0x843F6E UpdateSlot, BFME2 0x441B15: a local player who accepted and is not the host
	// has every gadget disabled, RW 0x8404AC(false, -1)) not after the local player's own Ready
	bool slotGadgetsEnabled(int slot) const;
	// lane ONLINE-5: may a slot's Hero combo be used at all (false: shown locked; every lobby carries the record since lane ONLINEMENU-1)
	virtual bool heroChoiceAvailable() const { return true; }
	// lane OWNERFIX-2: whether the lobby can carry a slot's handicap to the game (false: its combo stays empty and locked; the skirmish and LAN lobbies through
	// NetPacket's GameInfo, the online one through the lobby's START since lane ONLINEMENU-1)
	virtual bool handicapChoiceAvailable() const { return true; }
	// lane CHEAT-1: whether the Rules tab offers OpenBFME's Cheat Mode row (every custom game; the online room of a Quick Match or a tournament does not)
	virtual bool cheatModeOffered() const { return true; }
	// lane ONLINE-5: the chat panel's Players list (AptMpChat's "ChatPlayers", RW 0x9BEA20): the human players of the network game, in slot order.
	// INFERENCE [S-4551]: retail's chat helper fills it with the chat's recipients (the room's players; its fill, RW 0x9BECCB's source, was not read)
	// and sends a line only to the ones selected; here it lists who is in the room and every line goes to everyone
	void refreshChatPlayers();
	// lane ONLINEMENU-1 (owner, 2026-10-20: "the player can just see their ping on the side"): a second column after a player's name in that list (the
	// online room: the ping); "" none (the list keeps one column)
	virtual UnicodeString chatPlayerNote(int slot, Color *color) const
	{
		(void)slot;
		(void)color;
		return UnicodeString();
	}
	// lane UI-1: AptMpGameRules::ShowChat (RW 0x845AE2 id 0): the host interface's answer; the skirmish lobby has no chat
	virtual bool showChat() const { return false; }
	// lane RULES-1: the panel's ruleChanged (RW 0x8421E0) on the host posts "GUI:RuleChangeWarning" + the rule's name (kind 10 with a reset:
	// "GUI:RuleResetWarning") through the owner's slot 17: the LAN lobby's system chat; the skirmish lobby has no chat (nothing)
	virtual void ruleNotice(const std::u16string &line) { (void)line; }
	// lane RULES-1: RW 0x8421E0 kind 3 above 100: MessageBoxOk("RULE:CommandPointFactor", "APT:CommandPointsTooHigh"); the LAN lobby uses its own box
	virtual void showRuleWarning(const std::u16string &title, const std::u16string &text);
	// lane RULES-1: the rule gadgets follow the model (RW 0x986630 for every rule, AptMpGameRules::rva0057E6C1)
	void refreshRuleGadgets();
	// lane UI-1: a line of the lobby chat (RW 0x78419D: the history and the Chat list)
	void addChatLine(const std::u16string &line, Color color);
	SkirmishGameSetup *setupModel() { return m_setup.get(); }
	ShellEnvironment &environment() { return m_env; }
	// the model changed from outside (the network): every gadget follows it
	void refreshLobby();
	void setHostModeText(const std::string &text)
	{
		m_hostModeText = text;
		m_hostMode = text == "1";
	}
	void invokeRoot(const char *function);
	void pruneDeadGadgets();

private:
	enum class Leaf
	{
		Player,
		PlayerTemplate,
		Team,
		Color,
		Handicap,
		Hero
	};
	struct SlotGadget
	{
		int slot = 0;
		Leaf leaf = Leaf::Player;
	};

	void registerAll();
	void setFirstVisibleSlot(int first);
	void destroySlotScroll();
	void registerLobby();
	void registerProfileCommands();
	// ---- gadget initialisation (the `_Init` screen references) ----
	void initSkirmishGadget(const std::string &name, GameWindow *window);
	void initMpGameSetupGadget(const std::string &name, GameWindow *window);
	void initMapPreviewGadget(const std::string &name, GameWindow *window);
	void initChatGadget(const std::string &name, GameWindow *window);
	void sendChat();
	// ---- lobby ----
	void populateSlotGadget(int slot, Leaf leaf, GameWindow *combo);
	void fillHeroCombo(int slot, GameWindow *combo); ///< lane CAH-1, RW 0x842C93
	void onHeroSelected(int slot, int value);        ///< lane CAH-1, BFME2 0x43E04D / 0x43DD34
	int heroSide(int slot) const;                    ///< the slot's faction side as a Create-a-Hero faction index (-1: any)
	bool heroUsable(int slot, const CreateAHeroHero &hero) const;
	void populateMapList();
	void createMapCapacityFilter();
	void setMapSort(int mode);
	void refreshAll();
	void refreshSlot(int slot);
	void updatePlatformBadge(int slot, GameWindow *nameWindow); // lane PLATFORMICON-1
	bool m_platformNoted = false;
	void refreshColors(int slot);
	void updateMapPreview();
	void updateCurrentMapWindow(const MapCacheEntry *map); // lane UI-2: RW 0x9772E6 / 0x976F59
	std::string mapPreviewImage(const MapCacheEntry &map);  // lane UI-2: RW 0x702689
	void onComboSelected(GameWindow *combo);
	// ---- lane RULES-1: AptMpGameRules (AptSkirmishRules.cpp) ----
	void initRuleGadget(const std::string &name, GameWindow *window); ///< RW 0x987092
	bool tryPopulateRules();                                          ///< RW 0x57ED2B's RotWK twin: once every row of the mode is filed
	void refileRules();                                               ///< RW 0x57EF18's RotWK twin: forget the rule windows and populate again
	void populateRuleCombos();                                        ///< RW 0x986B1C
	void populateRuleChecks();                                        ///< RW 0x986810
	void enableRuleGadgets();                                         ///< RW 0x57E97A's RotWK twin: enabled = host and shown
	void updateRuleGadget(int rule);                                  ///< RW 0x986624
	void updateLegendaryGadget();
	void updateCheatGadget(); // lane CHEAT-1
	bool onRuleGadget(GameWindow *gadget);                            ///< RW 0x986776: true when `gadget` is a rule gadget
	void ruleChanged(int kind, bool reset);                           ///< RW 0x8421E0
	void resetRules();                                                ///< AptMpGameRules::Reset RW 0x98674C
	void pruneRuleGadgets(const std::set<const GameWindow *> &alive);
	int ruleComboCount() const;                                       ///< MpGameRules::NumComboBoxes: the mode's rows, + 1 for Legendary Heroes in mode 0
	int ruleCheckCount() const;                                       ///< MpGameRules::NumCheckBoxes: the mode's rows, + 1 for Cheat Mode (lane CHEAT-1) in mode 0
	bool amIHost() const { return m_hostMode; }
	// lane FB7-1: a click on a start spot of the map window (RW 0x845830, messages 0x4008 / 0x4009 = GBM_SELECTED / GBM_SELECTED_RIGHT); true when
	// `button` is one of the spots
	bool onStartSpotSelected(GameWindow *button, bool left);
	// RW 0x845830's start position branch for a position index (a map spot, or a War of the Ring region index: lane WOTR-2)
	bool startPositionClicked(int position, bool left);
	int nextSelectableStartSlot(int from) const; // RW 0x975C3A
	// lane FB7-1: updateMapStartSpots (RW 0x7052BE with onLoadScreen 0, run by the screen update when a start position changed, RW 0x846BB9)
	void updateMapStartSpots();
	void onMapListSelected(int row);
	bool startGame();   // the part of the screen update that validates and posts the game (retail: state 10); true when the message was queued
	void update();      // the screen's per-update state machine (RotWK 0x928CDB), run by the WindowManager while the screen exists
	// ---- profiles ----
	void refreshProfileList();
	void refreshBfme1ProfileDisplay();
	void refreshBfme1ProfileCombo();
	bool selectProfile(const std::u16string &name);
	std::u16string playerName() const;
	void applyProfileName();
	// RotWK 0x9294B3 (the Enter key of the persona entry, a double click in the profile list): close the popup through the movie's CloseProfilePopup,
	// then add the typed name in the new-profile state or change to the selected profile in the change-profile state.
	void acceptAction();
	bool addProfileFromEntry();
	bool changeProfileFromList();
	bool deleteProfileFromList();
	void closePopupState();
	void setGameMapType(const std::string &type);
	// lane UI-6: RW 0x9288D0 at 0x9289AD: the profile's kept game of `mode` (0 skirmish, 1 War of the Ring) instead of the defaults
	void restoreGame(int mode);
	// lane UI-6 review: InitTheGame again for the profile now current (a new, changed or surviving profile): the defaults, then its kept game
	void reloadGameForProfile();
	// OpenBFME's own profile keys beside RotWK's record: the Legendary Heroes factor and the slots' Create-a-Hero choices (by unique id)
	void restoreExtensions(int mode);
	std::string extensionHeroes() const;
	// (invokeRoot / pruneDeadGadgets are protected: the placeholder clips, and so their gadgets, come and go with the movie's frames; every pointer the
	// screen keeps is dropped when its window no longer exists)

	ShellEnvironment &m_env;
	std::unique_ptr<SkirmishGameSetup> m_setup;
	std::unique_ptr<class AptStats> m_statsPage; // lane ONLINEHUB-1: the profile page's statistics (Stats.swf, GUI/AptScreens/AptStats.h)
	std::unique_ptr<SkirmishProfileStore> m_ownProfiles;
	SkirmishProfileStore *m_profiles = nullptr;
	std::string m_error;
	std::string m_gameMapType = "OpenPlay"; // "OpenPlay" (skirmish) or "Strategic" (War of the Ring skirmish)
	std::string m_restoredGame;             // lane UI-6: the record the lobby began from ("" = the defaults)

	// MpGameSetup state
	std::map<const GameWindow *, SlotGadget> m_slotGadgets;
	GameWindow *m_slots[MAX_SLOTS][6] = {};
	const AptCharacterInst *m_extendedRowsParent = nullptr;
	std::array<float, LEGACY_MAX_SLOTS> m_retailRowY{};
	GameWindow *m_slotScroll = nullptr;
	GameWindow *m_slotRange = nullptr;
	int m_firstVisibleSlot = 0;
	int m_layoutCapacity = LEGACY_MAX_SLOTS;
	GameWindow *m_mapList = nullptr;
	GameWindow *m_mapCapacityFilter = nullptr;
	bool m_largeMapsOnly = false;
	std::vector<std::string> m_mapKeys;     // the map key of each MapList row (MpGameSetup +0x398)
	std::vector<int> m_mapPlayers; // lane MODUI-1
	std::vector<int> m_mapStates;           // lane OWNERFIX-3: each row's conquest state
	std::vector<std::string> m_mapImages;   // lane OWNERFIX-3: each row's column 0 image
	int m_sortPrimary = 2;                  // MpGameSetup +0x3AC (RW 0x8451A9): 0/1 name, 2/3 players, 4/5 conquest state (ascending / descending)
	int m_sortSecondary = 0;                // +0x3B0
	bool m_hostMode = true;
	std::string m_hostModeText = "1";
	std::string m_isInitializedText = "0";
	std::string m_activeTab;
	bool m_refreshing = false;
	// AptMapPreview state
	GameWindow *m_mapInfo = nullptr;
	GameWindow *m_mapDescription = nullptr;
	GameWindow *m_mapPicture = nullptr;
	GameWindow *m_currentMap = nullptr;
	std::vector<GameWindow *> m_currentMapChildren;
	bool m_strategic = false; // AptMapPreview::GameMapType == "Strategic": MpGameSetup+0x7C == 1
	const LivingWorldShellInfo *m_livingWorld = nullptr; // lane WOTR-1: the scenarios of the strategic setup
	std::string m_scenario;                              // lane WOTR-1: the selected scenario (LivingWorldCampaign name)
	const LivingWorldScenarioEntry *selectedScenarioEntry() const;
	void populateScenarioCombo();
	GameWindow *m_scenarioCombo = nullptr;        // lane WOTR-1: AptMapPreview StrategicScenarioType (+0x50)
	GameWindow *m_scenarioDescription = nullptr;  // StrategicScenarioDescription (+0x54)
	GameWindow *m_territoryDescription = nullptr; // StrategicTerritoryDescription (+0x58)
	GameWindow *m_livingWorldWindow = nullptr;    // lane WOTR-2: LivingWorld (+0x68)
	int m_hoverRegion = -1;
	// Skirmish profile gadgets
	GameWindow *m_profileList = nullptr;
	GameWindow *m_profileEntry = nullptr;
	GameWindow *m_bfme1ProfileCombo = nullptr;
	GameWindow *m_bfme1MapType = nullptr;
	bool m_bfme1OfficialMaps = true;
	OptionPreferences m_bfme1Honors;
	int m_bfme1ProfileMovie = -1;
	int m_bfme1ProfileProgress = -1;
	bool m_suppressStrategicPromo = false;
	// The profile state of RotWK's AptSkirmish (field +0x6B8): 1 set by OnInitialized (0x9280DB), 10 by StartGame (0x9280F0), 8 by the stats commands
	// (0x9280FD); the update (0x928CDB) takes 1 -> ShowMain -> 5; the popup states 2 (new profile), 4 (change profile) and 3 (delete) close to 7 / 5
	// (0x92810A).  The numbers are retail's; which command sets 2 / 3 / 4 is inferred from the page it opens [S-179].
	enum ProfileState
	{
		kStateNone = 0,
		kStateInitialized = 1,
		kStateNewProfile = 2,
		kStateDeleteProfile = 3,
		kStateChangeProfile = 4,
		kStateMain = 5,
		kStateClosed = 7,
		kStateStats = 8,
		kStateStart = 10,
		kStateStarting = 11 // validation passed, CloseMain sent (0x928D46)
	};
	int m_profileState = kStateNone;
	// The flag at +0x6C2: 1 while the popup's accept button is disabled.  Enable is only sent when it is 1, so the popup must start with it 1: the movie
	// shows the popup without its Select button (observed) and the first non-empty text enables it.
	bool m_acceptDisabled = true;
	int m_initializedCount = 0;
	int m_newProfileMenuCount = 0;
	bool m_lan = false;                         // lane MP-2: hosted by AptLanLobby
	// lane UI-1: the chat gadgets (AptMpChat::InitGadgets) and channel 1's lines (RW 0xDE7D40 + 0xC: retail's history is global)
	GameWindow *m_chatList = nullptr;
	GameWindow *m_chatPlayers = nullptr;
	GameWindow *m_chatEntry = nullptr;
	std::vector<std::pair<std::u16string, Color>> m_chatHistory;
	std::array<int, MAX_SLOTS> m_playerComboKind{ { -1, -1, -1, -1, -1, -1, -1, -1 } }; // lane MP-2: 1 the name entry, 0 the slot states
	std::array<int, MAX_SLOTS> m_slotHero{ { -1, -1, -1, -1, -1, -1, -1, -1 } };        // lane CAH-1: GameSlot + 0x5C
	bool m_heroStopReported = false;
	// lane RULES-1: AptMpGameRules + 0x64 / + 0x70 (the combo and check widgets by row), + 0x7C (the widget of each rule), + 0x88 (input gate), + 0x89
	// (populated), and the per-rule states of the RotWK panel (1 shown, 2 disabled by GlobalData, 3 hidden: ClanGame outside online play)
	std::vector<GameWindow *> m_ruleCombos, m_ruleChecks;
	std::array<GameWindow *, NUM_GAME_RULES> m_ruleWindows{};
	std::array<int, NUM_GAME_RULES> m_ruleStates{};
	GameWindow *m_legendaryCombo = nullptr;
	GameWindow *m_cheatCheck = nullptr; // lane CHEAT-1
	bool m_ruleInput = true;
	bool m_rulesPopulated = false;
	std::unique_ptr<AptMessageBox> m_ruleBox;
	std::vector<std::pair<std::u16string, std::u16string>> m_ruleWarnings;

	bool m_rulesStopReported = false; // lane RULES-1
	// lane ONLINE-5: the size each chat list had when it was last filled (a list made while its tab still plays in is filled again at its final size)
	int m_chatListW = -1, m_chatListH = -1, m_chatPlayersW = -1, m_chatPlayersH = -1;
	void serviceChatLists();
	bool m_readyDirty = false;                      // lane ONLINE-5: AptMpGameSetup + 0x2B9 (as far as the Ready boxes go)
	std::array<std::string, MAX_SLOTS> m_readyIcon; // lane ONLINE-5: what each Ready box shows (the last SetReadyState that reached the movie)

public:
	static const char *const kHeroComboStop;
	static const char *const kRulesStop; ///< lane RULES-1: "[S-3600] .. [S-3602] ...", a "rules-unverified" note when the rule rows are first populated ///< lane CAH-1: "[S-1406] ...", a "create-a-hero-stop" note when a Hero combo is first filled
};
