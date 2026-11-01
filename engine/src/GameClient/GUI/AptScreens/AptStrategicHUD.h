// OpenBFME. GPL-3.0.
//
// AptStrategicHUD: the engine side of StrategicHUD.apt, the War of the Ring strategic HUD (lane WOTR-2).  The HUD movie loads its sub-movies itself
// (StrategicPalantir, StrategicEndTurnButton, StrategicStats, StrategicChecklist, StrategicDetailsTray, StrategicNextTurnInd, InGameHelpBox,
// InGameRadialMenuStage) and reports each one with `FSCommand:<this>_On<Part>Loaded <clip>`; the engine then makes the part's UI object, which
// registers the part's own commands under the clip's path.
//
// TARGET FACTS (RotWK game.dat, caveat S-001):
//   * the HUD object (ctor RW 0x83A272, the movie name "StrategicHUD.apt" at RW 0xC53B28, loaded by RW 0x839B32) registers StrategicHUD::OnInitialized,
//     StrategicHUD::OnClosed and, with the prefix "_level%u" of its window, _OnChecklistLoaded / Unloaded, _OnEndTurnButtonLoaded / Unloaded,
//     _OnHelpBoxLoaded / Unloaded, _OnLoadDialogFrameLoaded / Unloaded, _OnNewTurnIndicatorLoaded / Unloaded, _OnPalantirLoaded / Unloaded,
//     _OnRadialMenuStageLoaded / Unloaded, _OnSelectionDetailsLoaded / Unloaded, _OnStatsDisplayLoaded / Unloaded (strings RW 0xC53B3C..0xC53D20);
//   * a part's Loaded handler makes its object once (end turn button RW 0x839CE0 -> ctor RW 0x983517, stats RW 0x83A185 -> RW 0x983EF9, new turn
//     indicator RW 0x839EF6 -> RW 0x9861B0, palantir RW 0x83A034 -> RW 0x982CC0) with the window's level and the clip's path; the part registers
//     "_level%u." + path + its command: the end turn button _OnClicked, the stats _OnStatRollOver / _OnStatRollOut, the new turn indicator _OnDone;
//   * the stats object writes its lines as Apt text records "APT:_level%u.%s%d_Text" (RW 0x983B39: level, path, line) from the game text
//     STRATEGICHUD:StatsTreasury (line 0, the treasury), StatsTreasuryIncome (6, the income), StatsResourceMultiplier (4, a float), StatsPowerPoints
//     (5) and StatsBonus (lines 1..3) (RW 0x9837F8 .. 0x983A51); the stat clips read them as text = "$" + their path + "_Text" (StrategicStats.apt);
//   * the checklist writes "APT:_level%u.%s_TurnNumber" (RW 0x9845B4, L"%d") and "_PhaseTitle" (RW 0x98471D: phase 0 APT:TacticalPhaseTitle, 2
//     APT:BattlePhaseTitle, 4 APT:RetreatPhaseTitle, else empty); the new turn indicator its "_TurnNumber" then the clip's function Go (RW 0x9860EA);
//   * the dialog frame part (RW 0x839E62 -> RW 0x98648F) registers _OnContentLoaded; a dialog is shown by calling the frame clip's LoadContent(<swf>)
//     (RW 0x986404) and closed with UnloadContent (RW 0xC8683C); the battle prompt (RW 0x9FCC9D: "StrategicBattlePrompt.swf") makes its object on the
//     content (RW 0xA0DF80): _OnAllyTabsLoaded / Unloaded, _OnEnemyTabsLoaded / Unloaded, _OnOpen, _OnClosed, _OnAutoResolveButtonClicked,
//     _OnRealTimeButtonClicked, _OnRetreatButtonClicked; it writes "APT:_level%u.%s_RegionName" (RW 0xA0CF53) and sets each button with the clip's
//     SetButtonState(<AutoResolve | RealTime | Retreat>, "_up" | "_disabled") (RW 0xA0CE42, the battle's resolution options RW 0x7F662B bits 1 / 3 / 2).
//   * the palantir (RW 0x83A034 -> RW 0x982CC0) registers _OnCommandUILoaded / Unloaded (RW 0x9826FF: the command UI RW 0x9E21B5),
//     _OnRegionStatsTrayLoaded / Unloaded, _OnRegionUILoaded / Unloaded, _OnSelectionUILoaded / Unloaded, _OnOptionsButtonClicked / RollOver / RollOut,
//     _OnObjectivesButtonClicked / RollOver / RollOut; the command UI registers _OnButtonFrameLoaded (RW 0x9E1C93: "index=<0..5>&name=<clip>", the frame
//     kept by index) / Unloaded, _OnSubMenuLoaded / Unloaded, _OnToggleFlashLoaded / Unloaded; the HUD's update (RW 0x97EE63) puts the toggle selection
//     details command (RW 0x97EA54, its button RW 0x97EA72 -> RW 0x9DB9FC with ToggleSelectionDetailsButton RW 0xDEC120: the magnifier) in command slot
//     0 when the slot is free and the help box (HUD + 0x18) exists, and keeps the selection details part shown (its vtable + 4 (true)).
//   * the selection details tray (RW 0x83A0E6 -> ctor RW 0x985DD6, HUD + 0x34) registers _OnPanelFrameLoaded / Unloaded (the panel frame RW 0x9E461E),
//     _OnToggleButtonClicked (RW 0x985CC2: closed -> Open, open -> Close), _OnOpened (RW 0x985D16: opening -> open), _OnClosed (RW 0x985CFB: closing ->
//     closed); Open (RW 0x985B28) calls the clip's Open and sets opening (+ 0x28 = 1), Close (RW 0x985B5C) its Close and closing (3); vtable slot 0 is
//     the enabled flag (+ 0x2C, RW 0x497F30), slot 1 sets it (RW 0x9859D6), 5 / 6 test open / closed. The HUD's update (RW 0x97EE63) enables and opens
//     the tray when it is first seen and re-enables it while it is disabled; the toggle details button (RW 0x9DB97E) closes an open tray and opens a
//     closed one when the tray is enabled, and is enabled itself only then (RW 0x9DB947: enabled and open or closed).
//   * the region details panel (StrategicInGameUI::RegionDetailsPanel, WorldBuilder twin 0x15FA950; its movie clip StrategicHUD::RegionDetailsMovieClip
//     0x1634010 ..): the tray's panel frame (a dialog frame, RW 0x9E461E -> RW 0x98648F) loads StrategicDetailsRegion.swf (RW 0x9DBC02); the movie loads
//     its three pages and reports each with <clip>_OnPageLoaded "id=<territory | armies | structures>&name=<clip>"; page slots 0 territory, 1 armies,
//     2 structures (WB 0x1FCB2EC); the first page is armies for an army, structures for a build plot (territory when the region is not the local
//     player's), else territory; structures is disabled when the region is not the local player's (SetPageEnabled -> SetTabEnabled(id, false));
//     SetCurrentPage -> SetTabsState("_" + id, "_hide" for none), ShowPage / HidePage -> SetPageVisibility(id, bool); a tab click is
//     <clip>_OnTabClicked with the tab's name;
//   * the territory page (StrategicInGameUI::RegionDetailsTerritoryPage WB 0x1635B00, RegionDetailsTerritoryMovieClip WB 0x165C9B0 ..): the map of the
//     region (maps\<MapName>\<MapName>.map in the map cache) gives "APT:_level%u.%s_TerritoryName" and _TerritoryDescription, then seven bonus lines
//     "APT:_level%u.%s_Bonus%d" (types 0 command points + 0x94, 1 attack + 0x9C, 2 defense + 0xA0, 3 experience + 0xA4, 4 resource + 0x90, 5 power points
//     (Legendary) + 0x98, 6 treasury income (FertileTerritory) + 0xA8; WB 0x1635F20), each STRATEGICHUD:RegionBonusNone below 1, else
//     STRATEGICHUD:RegionAbsoluteBonus (0, 5, 6) / RegionPercentageBonus (1 .. 4) formatted with the value (WB 0x165D040 / 0x1FD3090).
//   * the build queue panel (StrategicInGameUI::BuildQueueDetailsPanel WB 0x1644080 .., StrategicHUD::BuildQueueDetailsMovieClip WB 0x166F700 ..) for a
//     selected finished building with a SpawnArmy nugget (PlanningPhaseBuildingSelection WB 0x15FC870): the tray's frame unloads its movie and loads
//     StrategicDetailsBuildQueue.swf (SetPanel WB 0x15E1450); the in-progress slot and seven queued slots (the retail movie has six), see
//     applyBuildQueuePanel; the recruit radial's QueueUnitButtons are the radial below with each army's production count.
//   * the radial menu (StrategicHUD::RadialMenu WB 0x1672570 .., AptRadialMenu WB 0x14F5450 ..; the build plot's buttons: BuildPlotRadialMenu
//     AppendButtons WB 0x1647B10, the player's available buildings for an empty plot): the stage movie InGameRadialMenuStage.swf (HUD part
//     RadialMenuStage) makes a button with CreateButton(id), reports its frame with <stage>_OnButtonFrameLoaded_<id> (the frame clip), the engine fills
//     the frame with StrategicHUD.apt's StrategicCommandButton (CreateContent, "bttn": <content>_OnInitialized, its image record <content>_Image,
//     <content>_OnClicked / _OnRollOver / _OnRollOut), places it with MoveButton(id, x, y, w, h) (stage units: the display-pixel layout of the in-game
//     ring, RadialMenuLayout, scaled by the stage's size, ButtonShell::Move WB 0x14F6030) and removes it with DestroyButton(id).
// INFERENCE (stop S-1334): the path a part's object keeps (RW 0x815563 / 0x8155D9 were not decompiled) is the clip path without its "_levelN."
// prefix; the stats line order of the constructor (0, 6, 4, 5, 1..3) is kept (the values: RW 0x97E280, LivingWorldLogic::hudStats); the palantir, checklist, selection
// details (the region / army / structure trays), help box, radial menu and load dialog parts are made and reported but their bodies are not ported.

#pragma once

#include "GameClient/CommandButtonHelp.h"
#include "GameClient/InGameHelpBox.h"

#include "GameClient/GUI/AptScreen.h"
#include "Libraries/Source/Apt/AptValue.h"

#include <map>
#include <functional>
#include <memory>
#include <string>
#include <vector>

struct ShellEnvironment;
class AptMessageBox;

struct StrategicHudStats
{
	int treasury = 0;
	int income = 0;
	float resourceMultiplier = 1.0f;
	int powerPoints = 0;
	int bonus[3] = { 0, 0, 0 };
};

class AptStrategicHUD : public AptScreen
{
public:
	AptStrategicHUD(WindowManager &windows, Shell &shell, ShellEnvironment &environment);
	~AptStrategicHUD() override;

	static const std::vector<std::string> &partNames(); // Checklist, EndTurnButton, HelpBox, ...

	bool initialized() const { return commandCalls("StrategicHUD::OnInitialized") > 0; }
	// the parts the movie reported loaded: part name -> clip path (as the movie passed it)
	const std::map<std::string, std::string> &loadedParts() const { return m_parts; }
	// the commands the movie ran, in order (tests, reports)
	const std::vector<std::string> &commandLog() const { return m_log; }

	// the stats lines (written now when the stats part is loaded, else when it loads)
	void setStats(const StrategicHudStats &stats);
	// the text records the stats part was given, by record name
	const std::map<std::string, std::string> &statRecords() const { return m_statRecords; }
	// the turn and logic phase: the checklist's turn number and phase title; a new turn plays the new turn indicator
	void setTurn(int turn, int phase);
	// the battle prompt (StrategicBattlePrompt.swf in the dialog frame): shown for a battle the local player decides; the buttons that may be pressed
	// WOTR2 milestone 8: `realTimeReason` (UTF-8, OpenBFME's text): why Real Time is disabled (a network war's battles, step 3 [S-1339]): shown in the
	// help box while the prompt is up
	void openBattlePrompt(const std::string &regionName, bool autoResolve, bool realTime, bool retreat, const std::string &ally = std::string(),
		const std::string &enemy = std::string(), const std::string &realTimeReason = std::string());
	void closeBattlePrompt();
	// WOTR2 milestone 8: the phase timer (RW 0x97EFB2: the checklist's vslot 0x14 = RW 0x984640 "APT:_level%u.%s_TimeRemaining" formatted with
	// STRATEGICHUD:TurnTimeRemaining (minutes, the locale's time separator, seconds); vslot 0x18 without a timer: here an empty text); -1 hides it
	void setTimeRemaining(int seconds);
	// WOTR2 milestone 8: STRATEGICHUD:WaitMessage as strategic message type 4 (non-interactive, RW 0x97E137 -> RW 0x953B86); false hides it (RW 0x953498)
	void setWaitMessage(bool shown);
	bool waitMessageShown() const { return m_waitShown; }
	bool battlePromptOpen() const { return m_promptWanted; }
	bool battlePromptLoaded() const { return !m_promptPath.empty(); }
	// the palantir's command slot 0 (the toggle selection details button, RW 0x97EE63): shown once its content initialized; its image
	bool toggleDetailsShown() const { return m_toggle.initialized; }
	const std::string &commandFramePath(int slot) const { return m_commandFrames[slot < 0 || slot > 5 ? 0 : slot]; }
	// WOTR2 milestone 4: the palantir command UI's slots 1..5 (CommandUIImpl RW 0x9E21B5: setSlot / clearSlot, the buttons re-evaluated every update).
	// Each is a StrategicCommandButton (StrategicHUD::CommandButtonMovieClip RW 0x9F4287: content "button" in the slot's frame, its image map,
	// Enable(bool)); a left click on an enabled button is the request "command:<id>"; a rollover shows its Title / Help in the help box. The host
	// fills the slots from the selection (which buttons, enabled) on every sync, as retail's selection objects and their update tests
	struct CommandButton
	{
		std::string id;    // "" clears the slot
		std::string image; // the settings' Image (a mapped image)
		std::string title, help; // the settings' Title / Help labels
		bool enabled = true;
	};
	void setCommandButtons(const std::vector<CommandButton> &slots); // slots 1.. in order (at most 5)
	// WOTR2 milestone 5: the palantir's region stats tray (StrategicPalantir `regionStats`, RW 0x9E2C20; filled by RegionDisplay RW 0x9DAD60 every HUD
	// update for the selection's region): BuildPlots (plots built / plots), ArmoryPoints (upgrades used / per turn, the local player's region only:
	// RW 0x6B7ADD), CommandPoints (the local garrison's points / the region's CPLimit or AllyCPLimit: RW 0x7F1029 / 0x7F0FE5); hidden with no region
	struct RegionStats
	{
		bool shown = false;
		int values[3][2] = { { 0, 0 }, { 0, 0 }, { 0, 0 } };
	};
	void setRegionStats(const RegionStats &stats);
	// WOTR2 milestone 5: a strategic notice in AptStrategicMessageBox (GuiFX's "StrategicMessageBox", RW 0x953007): Notice::Show RW 0x900CBC writes the
	// title and text, Show("Ok", long) with the notice's state callback (RW 0x900B6D: shown until _OnHidden); the texts are fetched here and formatted
	// with the args (%ls / %d), each item appended to the text formatted with its own args (LW:AttackRepelledItemText)
	struct NoticeArg
	{
		bool label = false; // true: a game text label to fetch; false: the text itself (UTF-8)
		std::string value;
	};
	struct NoticeItem
	{
		std::string label;
		std::vector<NoticeArg> args;
	};
	// `question` (WOTR2 milestone 8 step 4): the box asks Yes / No (the LAN war's save request, the leave-for-now vote); noticeAnswer: 1 Yes, 0 No, -1 none yet
	void showNotice(const std::string &titleLabel, const std::string &textLabel, const std::vector<NoticeArg> &args, const std::vector<NoticeItem> &items,
		bool question = false);
	int noticeAnswer() const { return m_noticeAnswer; }
	// WOTR2 milestone 8: an OpenBFME notice (no game text label: the LAN war's battle limits, S-1342) in the same box, with its OK
	void showNoticeText(const std::u16string &title, const std::u16string &text);
	bool noticeShown() const { return m_noticeShown; }
	// WOTR2 milestone 6: the checklist's task rows (StrategicHUD::ChecklistUIImpl RW 0x985642; spec T): the client's list replaces the rows by key
	// (a new key: CreateItem RW 0x984FC6; a gone key: DeleteItem RW 0x98541B); each row's text is the label formatted with its args, drawn by the
	// engine in ItemFont (ItemImportantColor for a required row, else ItemNewColor: RotWK never ages a row); a click asks "checklist:<key>"
	struct ChecklistItem
	{
		std::string key;
		std::string label;
		std::vector<NoticeArg> args;
		int importance = 1; // 1 required: the checkbox shows
		bool checked = false;
	};
	void setChecklistItems(const std::vector<ChecklistItem> &items);
	bool hasIncompleteRequiredChecklistItem() const;
	// END PHASE refused (ManualPhaseEnder::OnEndPhase RW 0x9DCEBB): the list opens (or once its closing ends) and each incomplete required row
	// flashes; the keys flagged, in row order (the caller reports their Eva events)
	std::vector<std::string> flagIncompleteChecklistItems();
	void checklistUpdate(); // RW 0x985531, once per HUD update: the relayout and each row's update (RW 0x9E3D78)
	int checklistRowCount() const { return (int)m_rows.size(); }
	std::string checklistRowPath(int i) const { return i >= 0 && i < (int)m_rows.size() ? m_rows[(size_t)i].path : std::string(); }
	// WOTR2 milestone 5: the map rollover help (spec O B; Rollover::Update RW 0x97E6DD shows it after 999 ms, the host times it): a region's (RW 0x9737F9:
	// its DisplayName, the description RW 0x7F2A76), an army's (RW 0x9735D3: the summary's name label; the heroes' names joined with ", ", then a
	// "N Category" line per category, RW 0x620B6A) or a building's (RW 0x9736A0: DisplayName, DisplayDescription and the farm line RW 0x6E0FC4)
	struct MapHelp
	{
		enum Kind
		{
			NONE = 0,
			REGION = 1,
			ARMY = 2,
			BUILDING = 3
		};
		int kind = NONE;
		std::string regionCampaign; // REGION
		int region = -1;
		std::string titleLabel;     // ARMY / BUILDING
		std::vector<std::string> heroLabels;            // ARMY
		std::vector<std::pair<std::string, int>> lines; // ARMY: {category label, count}
		std::string textLabel;      // BUILDING
		int farmTreasury = -1;      // BUILDING: the IncreaseTreasury amount of a fortress or resource building, -1 none
	};
	void showMapHelp(const MapHelp &help);
	// the rollover's release (RW 0x97E675): the box hides soon when it still shows this help
	void hideMapHelp();
	bool mapHelpShown() const { return m_mapHelp && m_helpBox.help() == m_mapHelp.get(); }
	// EndWar / exit (RW 0x6B943A / 0x6B94C6): the notice on screen goes at once
	void clearNotice();
	AptMessageBox *noticeBox() { return m_noticeBox.get(); }
	const std::string &regionStatsClip() const { return m_regionStatsClip; }
	// WOTR2 milestone 4: the help box part (StrategicHUD::HUD::Impl + 0x18: the InGameHelpBoxMovieClip of the battle palantir, RW 0x92E689, made by
	// _OnHelpBoxLoaded RW 0x839D7F) showing an InGameSimpleHelp (title, text: RW 0x9402E7). `metrics` measures the help's fonts (the device's);
	// helpUpdate runs the box's state machine (RW 0x92E462) once per HUD update
	void setHelpMetrics(FontMetricsSource *metrics) { m_helpMetrics = metrics; }
	void showHelp(const std::string &titleLabel, const std::string &helpLabel);
	void hideHelp();
	void helpUpdate();
	const InGameHelpBox &helpBox() const { return m_helpBox; }
	bool commandButtonShown(int slot) const { return slot >= 1 && slot <= 5 && m_slots[slot].initialized && m_slots[slot].shown; }
	bool commandButtonEnabled(int slot) const { return slot >= 1 && slot <= 5 && m_slots[slot].enabledShown; }
	const std::string &commandButtonId(int slot) const { return m_slots[slot < 1 || slot > 5 ? 0 : slot].want.id; }
	// the selection details tray (RW 0x985DD6's + 0x28): 0 closed, 1 opening, 2 open, 3 closing; -1 not loaded
	int detailsTrayState() const { return m_parts.count("SelectionDetails") ? m_detailsState : -1; }
	// the region details panel in the tray (StrategicDetailsRegion.swf); `bonus` the seven bonus types' values (see above), `mapName` the region's MapName
	struct RegionPanel
	{
		std::string mapName;
		int bonus[7] = { 0, 0, 0, 0, 0, 0, 0 };
		bool owned = false; // the local player's region
		int firstPage = 0;  // 0 territory, 1 armies (an army is selected), 2 structures (a build plot is selected)
		// the armies page (RegionDetailsArmiesPage WB 0x1630220: the local player's armies with members in the region, at most 5)
		struct Army
		{
			int id = -1;
			std::string portrait;  // the hero's ButtonImage (WB 0x1654B30), a garrison's GarrisonSelectionPortraitName
			std::string typeImage; // the owner's FactionIcon (WB 0x16557D0)
			std::string name;      // UTF-8 (shown for the selected army)
			int commandPoints = 0, maxCommandPoints = 0;
		};
		std::vector<Army> armies;
		int selectedArmy = -1; // army id
		// the structures page (RegionDetailsStructuresPage: one slot per build plot of the region)
		struct Plot
		{
			bool built = false;     // a building stands or is being built
			std::string portrait;   // its LivingWorldBuilding ConstructButtonImage (+ 0x14)
			int type = 0;           // its Type (+ 0x30): 1 Fortress .. 4 Barracks (StrategicHUD TypeImages BuildingFortress .. BuildingBarracks)
			int turnsLeft = 0;      // > 0: under construction
		};
		std::vector<Plot> plots;
		bool fortress = false;        // the region's CreateAutoFort (its fortress icon)
		std::string fortressPortrait; // Region FortressPortrait (+ 0x140)
		int selectedPlot = -1;
		std::string selectedPlotName; // UTF-8: the selected plot's building name
	};
	void showRegionPanel(const RegionPanel &panel);
	void hideRegionPanel();
	bool regionPanelLoaded() const { return !m_regionClip.empty(); }
	const std::string &regionClipPath() const { return m_regionClip; }
	int regionPanelPage() const { return m_regionPage; }
	const std::string &regionPagePath(int slot) const { return m_regionPages[slot < 0 || slot > 2 ? 0 : slot]; }
	// the build queue panel in the tray (StrategicDetailsBuildQueue.swf; StrategicInGameUI::BuildQueueDetailsPanel WB 0x1644080, its movie clip
	// StrategicHUD::BuildQueueDetailsMovieClip WB 0x166F700) for a selected finished building with a SpawnArmy nugget: the queue, index 0 the unit in
	// production. A slot's left click selects it (the unit's name, build time and CP lines); a right click is the request "bq_dequeue:<index>"
	// (MSG_LW_DEQUEUE_UNIT_AT_INDEX); the back button is "bq_back"
	struct BuildQueuePanel
	{
		std::string buildingName; // the building template's DisplayName text (_BuildingName)
		struct Entry
		{
			std::string portrait;  // SpawnArmy::GetPortraitImage: the ArmyToSpawn's ConstructButtonImage
			int unitType = -1;     // the army's first entry's AutoResolveUnitType: its StrategicHUD TypeImages image (keys RW 0xC91470), -1 none
			int quantity = 0;      // the first entry's unit count ("" when 0)
			int turns = 0;         // GetBuildTimeAtIndex: index 0 BuildTime - turns spent, else BuildTime
			std::string name;      // GetButtonTitle: the ConstructButtonTitle text
			int commandPoints = 0; // the thing's CommandPoints
		};
		std::vector<Entry> queue;
	};
	// the army details panel in the tray (StrategicDetailsArmyRetinue.swf; StrategicInGameUI::ArmyDetailsPanel WB 0x16372F0 / HeroArmyDetailsPanel
	// WB 0x16402B0, their movie clips StrategicHUD::ArmyDetailsMovieClip WB 0x165FFA0 / HeroArmyDetailsMovieClip WB 0x166B710) for a selected army:
	// one icon slot per member (the leader of a hero army in its own icon), in the panel's order (Populate WB 0x1638500)
	struct ArmyPanel
	{
		struct Member
		{
			std::string portrait;  // GetButtonImage WB 0x16548D0: ButtonImage, else SelectPortrait, else BuildingNoArt
			int unitType = -1;     // its AutoResolveUnitType: the TypeImages image
			int quantity = 0;      // a horde's members x the entry count, else the entry count
			int rank = 0;          // the entry's level (+ 0x0C), 0 .. 10
			float progress = -1.0f; // the experience toward the next level, 0 .. 1; -1: at the top level (no progress bar)
			std::string name;      // GetDisplayNameStrategicGame (the rollover line)
			int commandPoints = 0; // the thing's CommandPoints (+ 0x628)
			bool hero = false;     // KindOf HERO: an ArmyHeroIcon (no rank, no upgrades), else an ArmyUnitIcon
		};
		bool heroArmy = false;     // SetLayout("_hero") and the leader icon
		Member leader;             // the hero army's leader (FindLeaderEntry: the entry of the army's hero template)
		std::vector<Member> members;
	};
	void showArmyPanel(const ArmyPanel &panel);
	// WOTR2 milestone 5: the army unit swapper (StrategicArmyUnitSwapper.swf in the dialog frame, ArmyUnitSwapperDialog RW 0xA09237 / Impl RW 0xA09009;
	// spec O part A): the selected army on top (a copy of the tray's member selection), the target army below (nothing selected); each slot its region
	// name, army name and "CP: %d/%d" (STRATEGICHUD:ArmyUnitSwapperCP, max > 0 only) and a StrategicDetailsArmyRetinue panel without its back button.
	// The buttons ask the host ("swapper_move_down" / "swapper_move_up" / "swapper_swap": MoveDown RW 0xA08EE3, MoveUp RW 0xA08F54, Swap RW 0xA08FC5),
	// DONE fades it out (RW 0xA11318) and _OnClosed unloads it ("swapper_closed"); it needs the help box and the dialog frame (RW 0x9F9376)
	struct SwapperSlot
	{
		int armyId = 0;
		std::string regionName, armyName; // texts (UTF-8)
		int cp = 0, maxCp = 0;
		ArmyPanel panel;
		std::vector<int> keys;            // the entry key of each panel member (panel order)
		std::vector<int> selectedKeys;
	};
	bool openUnitSwapper(const SwapperSlot &top, const SwapperSlot &bottom);
	void updateUnitSwapper(const SwapperSlot &top, const SwapperSlot &bottom); // the armies' summaries changed: the CP lines and panels again
	void setSwapperButtons(bool moveDown, bool moveUp, bool swap);             // SetButtonState with caches starting false (RW 0xA11207 ..)
	void closeUnitSwapper();                                                   // released without the fade (the target gone, the selection changed)
	bool unitSwapperOpen() const { return m_swapWanted; }
	const std::string &unitSwapperClip() const { return m_swapClip; }
	std::vector<int> swapperSelectedKeys(int slot) const;
	void hideArmyPanel();
	bool armyPanelLoaded() const { return !m_armyView.clip.empty(); }
	const std::string &armyClipPath() const { return m_armyView.clip; }
	int armySlotsReady() const;
	bool armyMemberSelected(int index) const { return index >= 0 && index < (int)m_armyView.selected.size() && m_armyView.selected[(size_t)index]; }
	bool armyLeaderSelected() const { return m_armyView.leaderSelected; }
	bool armyMemberIndexValid(int index) const { return index >= 0 && index < (int)m_armyView.selected.size(); }
	// the keyboard's Ctrl state at a click (the member icons' Ctrl toggle, RW 0x9F8007); the host provides it (none: never held)
	void setCtrlProvider(std::function<bool()> fn) { m_ctrlDown = std::move(fn); }
	void showBuildQueuePanel(const BuildQueuePanel &panel);
	void hideBuildQueuePanel();
	bool buildQueuePanelLoaded() const { return !m_queueClip.empty(); }
	const std::string &buildQueueClipPath() const { return m_queueClip; }
	int buildQueueSelected() const { return m_queueSelected; }
	// the radial menu around a point (window pixels; the display size and the stage units per pixel); a button's press is the request "radial:<index>"
	struct RadialButton
	{
		std::string image; // a mapped image (a LivingWorldBuilding's or an ArmyToSpawn's ConstructButtonImage)
		bool enabled = true;
		int productionCount = 0; // QueueUnitButton: ShowProductionCount, the queued entries of its army (0: none)
	};
	// the shown radial's buttons changed in place (a queued unit's count, an enabled state): the same count of buttons, no new ring
	void updateRadialButtons(const std::vector<RadialButton> &buttons);
	void showRadialMenu(float centerX, float centerY, int displayW, int displayH, float stagePerPixelX, float stagePerPixelY, const std::vector<RadialButton> &buttons);
	void hideRadialMenu();
	bool radialMenuShown() const { return !m_radialButtons.empty(); }
	int radialButtonsReady() const;
	// the checklist's list (RW 0x985642's + 0x14): 0 closed, 1 opening, 2 open, 3 closing
	int checklistState() const { return m_checklistState; }
	// what the player asked through the HUD since the last call ("end_turn", "new_turn_done"), in order
	std::vector<std::string> takeRequests();

private:
	void onPartLoaded(const std::string &part, const std::string &clip);
	void onPartUnloaded(const std::string &part);
	void writeStats();
	void writeTurn(bool newTurn);
	void updateCommandUI();
	void updateToggleSlot();
	void updateCommandSlots();
	void updateDetailsTray();
	// the tray's panel frame holds one movie (SelectionDetailsPanelFrame::Impl::SetPanel WB 0x15E1450: the old one is unloaded, the new one loaded)
	enum TrayContent
	{
		TRAY_NONE = 0,
		TRAY_REGION,
		TRAY_BUILD_QUEUE,
		TRAY_ARMY
	};
	struct ArmyPanelView;
	void onArmyContentLoaded(ArmyPanelView &v, const std::string &clip);
	void releaseArmyView(ArmyPanelView &v);
	void applyArmyPanel(ArmyPanelView &v);
	void onArmySlotLoaded(ArmyPanelView &v, int index, const std::string &frame);
	void applyArmyIcon(ArmyPanelView &v, size_t index);
	void onArmyIconClicked(ArmyPanelView &v, int index);
	void showArmyStats(ArmyPanelView &v, int hovered);
	void syncTrayContent();
	void unloadTrayContent();
	void onTrayContentLoaded(const std::string &clip);
	void onRegionContentLoaded();
	void onBuildQueueContentLoaded();
	void applyBuildQueuePanel();
	void selectQueueIndex(int index);
	void onQueueSlotClicked(int index);
	void applyRegionPanel();
	void fillTerritoryPage();
	void fillArmiesPage();
	void fillStructuresPage();
	void onRadialFrameLoaded(int id, const std::string &frame);
	void openDetailsTray(bool open);
	void onContentLoaded(const std::string &clip);
	void onSwapperLoaded(const std::string &clip);
	void writeSwapperSlot(int slot);
	void releaseSwapper(bool unload);
	void writePrompt();
	std::string relativePath(const std::string &clip) const;
	void writeRegionStats(bool force);
	std::unique_ptr<AptMessageBox> m_noticeBox; // AptStrategicMessageBox's box (made at the first notice)
	bool m_noticeShown = false;                 // the notice's + 0x25
	int m_noticeAnswer = -1;                    // WOTR2 milestone 8 (step 4): the question's button (1 Yes, 0 No)
	std::shared_ptr<CommandButtonHelp> m_mapHelp; // the map rollover's help (its own source, apart from the buttons' m_helpShown)
	std::string m_regionStatsClip;  // RW 0x9827A1: the tray's clip, kept until it unloads
	RegionStats m_regionStats;      // the host's values
	RegionStats m_regionStatsShown; // what the tray shows (RW tray + 0x08 .. + 0x1C, + 0x68)
	std::string text(const std::string &label) const;

	ShellEnvironment &m_env;
	std::map<std::string, std::string> m_parts;
	std::map<std::string, std::vector<std::string>> m_partCommands;
	std::vector<std::string> m_log;
	std::vector<std::string> m_requests;
	StrategicHudStats m_stats;
	bool m_haveStats = false;
	int m_turn = 0, m_phase = 0;
	int m_checklistState = 0; // RW 0x985642's + 0x14: 0 closed, 1 opening, 2 open, 3 closing
	struct ChecklistRow
	{
		ChecklistItem item;
		std::u16string text;
		int name = 0;               // the AS name and depth (+ 0x50)
		std::string path;           // the row clip, without "_levelN."
		float width = 0.0f, height = 0.0f, y = 0.0f; // stage units
		bool widthKnown = false, heightKnown = false, checkedSent = false, visibilitySent = false, ySent = false;
		bool bound = false;                  // its externs and text render registered
		std::string widthValue, heightValue; // the movie's extern answers
		std::vector<std::string> names;      // commands / providers / the render callback registered for it
	};
	std::vector<ChecklistRow> m_rows;
	std::string m_checklistPath;     // + 0x10
	int m_rowCounter = 0;            // + 0x2C
	bool m_relayout = false;         // + 0x38
	bool m_openWhenClosed = false;   // + 0x26
	void deleteChecklistRow(size_t index);
	bool checklistCall(const std::string &path, const std::string &fn, const std::vector<AptValue> &args);
	bool m_checklistButtonUp = false; // RW 0x985642's + 0x25: the expand button's state last sent (RW 0x9849F9)
	std::string m_commandUiClip;      // the palantir's command UI clip (as the movie passed it); empty: not loaded
	std::string m_commandFrames[6];   // the command UI's button frames by index (level prefix removed)
	struct CommandSlot
	{
		CommandButton want;              // the slot's button (id "" none)
		bool created = false, initialized = false, shown = false;
		bool enabledShown = true;        // the impl's + 0x41 (starts 1)
		std::string imageShown;
	};
	CommandSlot m_slots[6];               // 1..5 (0 is the toggle, m_toggle)
	InGameHelpBox m_helpBox;
	HelpBoxSettings m_helpSettings;
	bool m_helpSettingsTried = false;
	FontMetricsSource *m_helpMetrics = nullptr;
	std::shared_ptr<CommandButtonHelp> m_helpShown; // the help the last showHelp gave the box (the hide guard compares it)
	struct ToggleSlot
	{
		bool created = false, initialized = false;
		std::string state; // the content's state last set ("_up" / "_disabled", RW 0x9DB947)
	} m_toggle;
	int m_detailsState = 0;        // RW 0x985DD6's + 0x28
	bool m_detailsEnabled = false; // + 0x2C
	bool m_detailsSeen = false;    // the HUD's update met the tray (RW 0x97EE63: + 0x24 set)
	std::string m_panelFrame;      // the tray's panel frame clip (_OnPanelFrameLoaded)
	int m_trayWanted = TRAY_NONE;  // the panel the selection shows
	int m_trayLoading = TRAY_NONE; // the movie LoadContent was asked for, not reported yet
	int m_trayLoaded = TRAY_NONE;  // the movie in the frame
	std::string m_trayClip;        // its content clip
	BuildQueuePanel m_queue;
	std::string m_queueClip;       // the loaded StrategicDetailsBuildQueue.swf content clip
	std::vector<std::string> m_queueCommands;
	int m_queueSelected = -1;      // Impl + 0x30: the selected queue index
	std::string m_queueInProgressSent;    // the SetInProgressIconSlotState argument last sent
	std::string m_queueSlotSent[7];       // SetQueuedIconSlotState per slot (NUM_QUEUED_ICON_SLOTS 7, WB 0x1FCD7D0)
	std::string m_queueStatsSent[3];      // SetUnitNameState / SetBuildTimeState / SetCommandPointsState
	struct ArmySlot
	{
		int id = 0;                       // the slot's id (a running counter, IconSlot WB 0x165F690)
		std::string frame, icon;          // the slot frame clip, its "icon" content
		bool initialized = false;
		bool overlaysSent = false;        // the overlays and a unit's rank, sent once its icon initialized
		std::string stateSent;
		int x = -1, y = -1;               // MoveUnitIconSlot last sent
	};
	// WOTR2 milestone 5: one StrategicDetailsArmyRetinue panel (the tray's, and the unit swapper's two: ArmyUnitSwapper's CreatePanel RW 0xA113C0 builds
	// the tray's panel classes with the back button hidden)
	struct ArmyPanelView
	{
		ArmyPanel panel;
		std::string clip;                 // the loaded StrategicDetailsArmyRetinue.swf content clip
		bool backButton = true;
		std::vector<std::string> commands, providers;
		std::vector<ArmySlot> slots;
		std::vector<bool> selected;       // the ArmySummaryEntryIDSet
		bool leaderSelected = false;
		int hovered = -1;                 // the member under the mouse (-1 none)
		int nextSlotId = 0;
		float stageW = 0.0f, stageH = 0.0f, slotW = 0.0f, slotH = 0.0f;
		std::string statsSent[4];         // SetMemberNameState / SetMemberRankState / SetMemberRankProgressBarState / SetCommandPointsState
		std::string leaderStateSent;
	};
	ArmyPanelView m_armyView;             // the tray's
	std::function<bool()> m_ctrlDown;
	RegionPanel m_region;
	std::string m_regionClip;      // the loaded StrategicDetailsRegion.swf content clip
	std::string m_regionPages[3];  // the page clips by slot
	bool m_regionPageShown[3] = { false, false, false };
	bool m_regionPageEnabled[3] = { true, true, true };
	int m_regionPage = -1;         // the current page slot
	std::string m_regionTabsSent;  // the SetTabsState argument last sent (a repeated gotoAndPlay of the current label would play the clip on)
	int m_regionTabEnabledSent[3] = { -1, -1, -1 }; // the SetTabEnabled states last sent (-1 none)
	std::vector<std::string> m_regionCommands;
	int m_armySlotsSent = -1;                 // SetIconSlotCount last sent
	std::string m_armySlotStateSent[5];       // SetIconSlotState last sent per slot
	std::string m_armyNameStateSent, m_armyCpStateSent;
	bool m_armiesPageBound = false;           // the page's commands are registered
	bool m_structuresPageBound = false;
	int m_plotSlotsSent = -1;
	std::string m_plotSlotStateSent[8], m_plotProgressSent[8];
	std::string m_fortressStateSent, m_buildingNameStateSent;
	std::string m_radialStage;                   // the RadialMenuStage clip (level prefix removed)
	struct RadialSlot
	{
		RadialButton button;
		std::string frame;     // the reported frame clip
		bool initialized = false;
		int enabledSent = -1;  // the Enable / SetState last sent (-1 none)
		int x = 0, y = 0, w = 0, h = 0; // its place (stage units)
	};
	void applyRadialButton(RadialSlot &slot);
	std::vector<RadialSlot> m_radialButtons;
	std::vector<std::string> m_radialCommands;
	bool m_haveTurn = false;
	bool m_promptWanted = false;
	std::string m_promptRegion, m_promptAlly, m_promptEnemy;
	bool m_promptButtons[3] = { false, false, false }; // AutoResolve, RealTime, Retreat
	std::string m_promptRealTimeReason;                // WOTR2 milestone 8
	std::shared_ptr<CommandButtonHelp> m_promptHelp;   // WOTR2 milestone 8: the reason's help, released with the prompt
	int m_timeRemaining = -2;                          // WOTR2 milestone 8: the last value written (-2 none yet)
	bool m_waitShown = false;
	std::string m_promptPath;
	std::vector<std::string> m_promptCommands;
	bool m_swapWanted = false;
	int m_swapState = 0;               // the movie clip's + 0x18: 0 loading, 1 open, 2 closing, 3 closed
	std::string m_swapClip;
	SwapperSlot m_swapSlots[2];
	ArmyPanelView m_swapViews[2];
	bool m_swapButtons[3] = { false, false, false }; // + 0x4C / + 0x4D / + 0x4E
	std::map<std::string, std::string> m_swapTexts; // the records' caches
	std::vector<std::string> m_swapCommands;
	std::map<std::string, std::string> m_statRecords;
};
