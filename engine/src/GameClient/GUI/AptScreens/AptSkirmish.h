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
// INFERENCE (stops S-178, S-179): which rule rows exist, what Hero and Handicap contain, how profiles are stored, whether Skirmish forces a profile
// first (the movie has the hook `vShowAddProfile`; the engine sets it when no profile exists), and everything under S-175 (stats screens).

#pragma once

#include "GameClient/GUI/AptScreen.h"
#include "GameClient/GUI/ShellEnvironment.h"
#include "GameClient/GUI/Skirmish/SkirmishGameSetup.h"

#include <array>
#include <map>
#include <memory>
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
	const std::string &lastError() const { return m_error; }
	// How often the movie sent AptSkirmish::OnInitialized.
	int initializedCount() const { return m_initializedCount; }
	// How often the movie sent AptSkirmish::OnNewProfileMenu (it does when it opens the add-profile popup).
	int newProfileMenuCount() const { return m_newProfileMenuCount; }
	// The retail flag at +0x6C2: the popup's accept button is disabled (an empty raw text).
	bool profileAcceptDisabled() const { return m_acceptDisabled; }
	GameWindow *slotGadget(int slot, const std::string &leaf) const;
	GameWindow *mapListWindow() const { return m_mapList; }
	// lane UI-2: the map preview window (MpGameSetup's CurrentMap, MpMapWindow.wnd) and its start-spot buttons
	GameWindow *currentMapWindow() const { return m_currentMap; }
	const std::vector<GameWindow *> &currentMapSpots() const { return m_currentMapChildren; }
	GameWindow *mapInfoWindow() const { return m_mapInfo; }
	GameWindow *profileListWindow() const { return m_profileList; }
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
	// The map cache keys in the order of the MapList rows.
	const std::vector<std::string> &mapListKeys() const { return m_mapKeys; }

protected:
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
	virtual void onReadyPress(const std::string &argument);
	virtual void onKickPlayer(const std::string &argument);
	virtual void onChatSend(const std::u16string &text);
	// lane UI-1: AptMpGameRules::ShowChat (RW 0x845AE2 id 0): the host interface's answer; the skirmish lobby has no chat
	virtual bool showChat() const { return false; }
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
	void populateMapList();
	void setMapSort(int mode);
	void refreshAll();
	void refreshSlot(int slot);
	void refreshColors(int slot);
	void updateMapPreview();
	void updateCurrentMapWindow(const MapCacheEntry *map); // lane UI-2: RW 0x9772E6 / 0x976F59
	std::string mapPreviewImage(const MapCacheEntry &map);  // lane UI-2: RW 0x702689
	void onComboSelected(GameWindow *combo);
	void onMapListSelected(int row);
	bool startGame();   // the part of the screen update that validates and posts the game (retail: state 10); true when the message was queued
	void update();      // the screen's per-update state machine (RotWK 0x928CDB), run by the WindowManager while the screen exists
	// ---- profiles ----
	void refreshProfileList();
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
	// (invokeRoot / pruneDeadGadgets are protected: the placeholder clips, and so their gadgets, come and go with the movie's frames; every pointer the
	// screen keeps is dropped when its window no longer exists)

	ShellEnvironment &m_env;
	std::unique_ptr<SkirmishGameSetup> m_setup;
	std::unique_ptr<SkirmishProfileStore> m_ownProfiles;
	SkirmishProfileStore *m_profiles = nullptr;
	std::string m_error;
	std::string m_gameMapType = "OpenPlay"; // "OpenPlay" (skirmish) or "Strategic" (War of the Ring skirmish)

	// MpGameSetup state
	std::map<const GameWindow *, SlotGadget> m_slotGadgets;
	GameWindow *m_slots[MAX_SLOTS][6] = {};
	GameWindow *m_mapList = nullptr;
	std::vector<std::string> m_mapKeys;     // the map key of each MapList row (MpGameSetup +0x398)
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
	// Skirmish profile gadgets
	GameWindow *m_profileList = nullptr;
	GameWindow *m_profileEntry = nullptr;
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
};
