// OpenBFME. GPL-3.0.
//
// TimeLine.apt (CodePrefix AptTimeLine, lane END-1): the score screen after a skirmish or LAN game (GameLogic::clearGameData RW 0x7792BC -> RW 0x927898 pushes
// it with the game type at + 0x284 and fills it, RW 0x9275EC; the data contract is GameClient/EndGame.h ScoreScreenData).
//
// TARGET FACTS (RotWK game.dat, caveat S-001; the registration RW 0x9265BD):
//   * commands: AptTimeLine::OnInitialized (RW 0x924C63), ::OnButtonContinue (RW 0x925699 via RW 0x925FF2: for the skirmish / LAN types the shell
//     music starts again with a fade (TheAudio 0xDE42FC vslot 0x138(2) + 0x9C, the event RW 0x6DA95E / setShouldFade RW 0x6DA774, vslot 0x64) and TheGameEngine
//     + 0x310 is set (RW 0x62215B); the engine's next update (RW 0x624EE7) pops the shell's top screen (RW 0x75DB34, Shell::pop): the way back to the menus
//     is the screen the shell kept under the game; lane QA2-FIX corrected END-1's reading of TheAudio as TheShell, stop S-1771), ::OnButtonSaveReplay (RW 0x924C83),
//     ::CaHAwardNumber (RW 0x9253C5); the render component AptTimeLine::RenderGraph (RW 0x9257F2, registered with RW 0x624348).
//   * providers (RW 0x925026, table RW 0xC7E0E8): TimeLine:ScreenMode = "OtherSingle" for the types 1 / 2 / 8, "OtherOnlineLan" for 3 / 4 / 5, "WOTRSingle" for 6,
//     "WOTROnlineLan" for 7; TimeLine:ShowSaveReplay = "0" when GlobalData + 0x9D4 & 3, else "%d" of (type 3 or 4); TimeLine:NumOfPlayers = the entries;
//     TimeLine:NumCahAwards = the Create-a-Hero awards (RW 0xDEA388 vector: none in a skirmish); TimeLine:LocalPlayerIsObserver = "1" / "0" (+ 0x294);
//     TimeLine:StrategicEnd = "0" outside the War of the Ring; every provider answers "0" first (RW 0x925033).
//   * per entry i (0 .. 7): TimeLine:PlayerColor:%d (RW 0x925188: "0", then sprintf("%#x", colour & 0xFFFFFF)), TimeLine:PlayerFaction:%d (RW 0x925569: "",
//     then the side name), TimeLine:GraphFocus:%d (RW 0x92536C, setting only: + 0x2B8[i] = atoi(value) * 0.01f).
//   * RW 0x92632E: the text TimeLine:PlayerName:%d = the entry's name; the image record TimeLine:PlayerFactionIcon:%d = the MappedImage "AptIcon" + the side
//     (RW 0x6236F6 / 0x6236DE; the movie's RenderImage clips name it in `_imageMap`, RW program 93632: "TimeLine:PlayerFactionIcon:" + the parent's name
//     from character 6) (lane END-2).
//   * the statistics page (lane END-2; the object at + 0x280, RW 0x9CDE57, registered by RW 0x9CE5CA): AptTimeLine::InitGadgets (RW 0x9CE51E) takes the
//     ListBox gadget named AptTimeLine::StatsList: 2 columns of 50 % for a single entry, else 4 of 25 % (RW 0x9CE4DF / 0x9F0768), then the rows
//     (ScoreScreenData::statRows) are listed (RW 0x9F13D8: the top row kept, the list reset, each row RW 0x9F0B50: the label in column 0, the shown
//     entries' values in columns 1 .. 3 centred (RW 0x725D96(.., 2)), an empty row); AptTimeLine::SetPlayerFocus (RW 0x9CD723) picks the shown entries
//     (ScoreScreenData::focusColumns; they start as {0, 0, 0}, RW 0x64048F(3)) and lists them again.
//   * RenderGraph (RW 0x9257F2): the movie's _graphMode ("Units", "Structures", "Resources", "Territories", else "FinalScore") picks the per-frame value
//     (ScoreScreenData::graphValue); the axes (ScoreScreenData::axis) set the texts Timeline:YAxis:0..10, Timeline:XAxis:0..10, Timeline:TotalTime and
//     Timeline:XAxisDescription (the APT:TimeDescription* label of the chosen format) when the top or the sample count changed (+ 0x2B0 / + 0x2B4); each
//     entry's line is drawn twice (width 3 with alpha, width 1 opaque: RW 0x924DD1), then its result icon at its last sample (+ 0x298[result]:
//     AptTimelineVictorious, Defeated, Disconnected; none for 3 / 4) and its fortress marks (AptTimelineFortress).
// NOT PORTED (stop S-1063): the device drawing of RenderGraph and of the faction icons is the presentation layer's (it reads lines() / axis() and the
// image records); Save Replay, the Create-a-Hero awards and the War of the Ring modes.

#pragma once

#include "GameClient/EndGame.h"
#include "GameClient/GUI/AptScreen.h"

#include <array>
#include <string>
#include <vector>

struct ShellEnvironment;

class AptTimeLine : public AptScreen
{
public:
	AptTimeLine(WindowManager &windows, Shell &shell, ShellEnvironment &environment);

	void runInit() override;

	// the graph the RenderGraph component shows for `mode` (ScoreScreenData::graphMode): sets the axis texts when they changed, returns the axis
	const ScoreScreenData::Axis &graph(int mode);
	int graphMode() const { return m_mode; }
	float graphFocus(int entry) const { return entry >= 0 && entry < 8 ? m_focus[(size_t)entry] : 0.0f; }
	bool renderComponentRequested() const { return m_componentRequests > 0; }
	int continuePressed() const { return m_continue; }
	const ScoreScreenData *data() const;
	// lane END-2: the statistics page
	const std::vector<ScoreScreenData::StatRow> &statRows() const { return m_rows; }
	const std::array<int, 3> &shownColumns() const { return m_shown; }
	GameWindow *statsList();
	int statsListFills() const { return m_fills; }

private:
	void populate();
	void fillStats();

	ShellEnvironment &m_env;
	ScoreScreenData::Axis m_axis;
	int m_mode = -1;
	float m_lastTop = -1.0f; // + 0x2B0
	int m_lastSamples = -1;  // + 0x2B4
	std::array<float, 8> m_focus{};
	int m_componentRequests = 0;
	int m_continue = 0;
	GameWindow *m_statsList = nullptr;
	std::array<int, 3> m_shown{ 0, 0, 0 };
	std::vector<ScoreScreenData::StatRow> m_rows;
	int m_fills = 0;
};
