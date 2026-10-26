// OpenBFME. GPL-3.0.
//
// InGameNotificationBox (lane CAMPUI-1): the in-game notification box of the map scripts' DISPLAY_NOTIFICATION_BOX ("New Objective", "Objective
// Completed", hints ...): Data\INI\InGameNotificationBox.ini's types and InGameNotificationBox.apt, the movie retail opens at the top of the screen.
// 1:1 port of RotWK's classes (no ZH donor: ZH has no notification box; BFME2 1.06 decomp InGameNotificationBoxMovieClip.cpp is the same code).
//
// TARGET FACTS (RotWK game.dat, caveat S-001; research notes of lane CAMPUI-1, nb/notification-box.md):
//   * TheInGameNotificationBox (RW 0xDE3F04, ctor RW 0x621D2D): block InGameNotificationBox (RW 0x621FFA, table RW 0xBFD050): DefaultOpenAudio (AsciiString,
//     + 0x10), DefaultMessageFont (parseFontDesc RW 0x778C93, + 0x18), DefaultMessageColor (parseColorInt RW 0x42F13E, + 0x24), NotificationType <name>
//     (RW 0x621F44: a new type copied from the defaults, then its own table RW 0xBFCF48: Title (a game text label), OpenAudio, Icon (a MappedImage),
//     MessageFont, MessageColor; a second type of the same name is parsed and dropped). Lookup RW 0x621E3A by name (an unknown type: nothing is shown).
//   * DISPLAY_NOTIFICATION_BOX (589) / ..._WITH_OBJECT_TYPE_IMAGE_OVERRIDE (593) -> RW 0x7C5507(type, label, seconds, override): the type's spec (RW 0x6217B8
//     -> 0x62161D: the title's game text, the audio, the icon, the font, the colour), the override replacing the icon with the ThingTemplate's ButtonImage
//     (RW 0x73CFEF; no template: no change; an empty ButtonImage: no icon), the message the label's game text, the timeout seconds * 1000 ms; shown
//     through InGameUI + 0x10 vslot 2 (RW 0x69BFAE): location "objectives" when the objectives screen is open, "spellStore" when the powers screen is,
//     else "main", opaque over those screens.
//   * the movie clip (InGameUI + 0x9CC, ctor RW 0x8EB371): InGameNotificationBox.apt in a level; commands "_level<n>_OnInitialized" (state 0 -> 1),
//     "_level<n>_OnOpen" (2 -> 3, the timer starts on the wall clock), "_level<n>_OnClosed" (4 -> 1, the icon cleared, the notice dropped),
//     "_level<n>_OnCloseButtonClicked" (in 2 / 3: the movie's Close(), 4); the extern "_level<n>_MessageWidth" (the message clip's width); the render
//     callback "_level<n>_Message" (the message drawn centred in the clip's rectangle); the text "APT:_level<n>_Title" and the image "_level<n>_Icon".
//   * Open RW 0x8EB66D: dropped while disabled (+ 0x50); else the notice is THE pending one (a newer replaces an unshown one: no queue).
//   * update RW 0x8EB9B4 (every client frame, also while paused): a pending notice -> UpdateNotice RW 0x8EB75B: the movie not initialised (width <= 0),
//     no font or the display letterboxed -> CloseImmediately (the notices are dropped, not deferred); else the title, SetIconVisibility when the icon's
//     presence changed, the icon, the message wrapped to the width, the open audio, the movie's Open(text height, opaque, location), state 2. In state 3
//     a timeout passed since _OnOpen (timeGetTime) runs Close() (state 4); a timeout 0 stays until closed.
//   * CloseImmediately RW 0x8EB275: Hide(), state 1, nothing pending or active; DoClose RW 0x8EB2D8: nothing pending, Close() in state 2 / 3; setEnabled
//     RW 0x8EB2BA: false closes immediately (HideControlBar RW 0x804576 disables, ShowControlBar RW 0x8046A1 enables).

#pragma once

#include "GameClient/GUI/AptScreen.h"
#include "GameClient/GUI/GameWindow.h"
#include "GameClient/GlobalLanguage.h"

#include <cstdint>
#include <string>
#include <vector>

class ArchiveFileSystem;
class FontMetricsSource;
class GameTextSource;

struct NotificationBoxType
{
	std::string name;
	std::string title;     ///< + 0x00: a game text label ("" no title)
	std::string openAudio; ///< + 0x04
	std::string icon;      ///< + 0x08: a MappedImage name ("" no icon)
	FontDesc font;         ///< + 0x0C
	std::uint32_t color = 0; ///< + 0x18 (ARGB)
};

struct NotificationBoxSettings
{
	NotificationBoxType defaults; ///< box + 0x0C (Title / Icon never set)
	std::vector<NotificationBoxType> types;
	const NotificationBoxType *find(const std::string &name) const; ///< RW 0x621E3A (exact name)
	// Data\INI\InGameNotificationBox.ini; a missing file or block is an error
	static bool load(ArchiveFileSystem &fs, NotificationBoxSettings &out, std::string *error);
	static bool scan(const std::string &ini, NotificationBoxSettings &out, std::string *error);
};

class InGameNotificationBoxMovieClip : public AptScreen
{
public:
	enum State
	{
		STATE_LOADING = 0,
		STATE_IDLE = 1,
		STATE_OPENING = 2,
		STATE_SHOWN = 3,
		STATE_CLOSING = 4
	};
	enum Location
	{
		LOCATION_MAIN = 0,
		LOCATION_SPELL_STORE = 1,
		LOCATION_OBJECTIVES = 2
	};
	struct Notice
	{
		UnicodeString title;
		std::string icon;     ///< MappedImage ("" none)
		std::string audio;    ///< the open audio event ("" none)
		UnicodeString message;
		GameFont font;
		std::uint32_t color = 0;
		unsigned timeoutMs = 0;
		bool opaque = false;
		int location = LOCATION_MAIN;
	};
	InGameNotificationBoxMovieClip(WindowManager &windows, Shell &shell, FontMetricsSource &metrics);
	~InGameNotificationBoxMovieClip() override;
	bool isMenu() const override { return false; }

	// RW 0x7C5507 + RW 0x6217B8: the notice of DISPLAY_NOTIFICATION_BOX(type, label, seconds) (override: the template's ButtonImage, "" none; set
	// `overrideIcon` when an override template exists). False: an unknown type (nothing is shown)
	static bool makeNotice(const NotificationBoxSettings &settings, const GameTextSource *text, const std::string &type, const std::string &label, int seconds,
		bool overrideIcon, const std::string &overrideImage, Notice &out);
	void open(const Notice &notice);     ///< RW 0x8EB66D
	// RW 0x8EB9B4 (nowMs: the wall clock); returns the open audio of a notice that opened now ("" none)
	std::string update(unsigned nowMs, bool letterboxed);
	void doClose();                      ///< RW 0x8EB2D8
	void closeImmediately();             ///< RW 0x8EB275
	void setEnabled(bool enabled);       ///< RW 0x8EB2BA
	bool enabled() const { return m_enabled; }
	int state() const { return m_state; }
	bool hasActive() const { return m_hasActive; }
	const Notice &active() const { return m_active; }
	float messageWidth() const { return m_width; }
	const std::vector<std::string> &callErrors() const { return m_callErrors; }

private:
	bool call(const std::string &fn, const std::vector<std::string> &args);
	void updateNotice(unsigned nowMs, bool letterboxed); // RW 0x8EB75B
	std::string prefix() const { return "_level" + std::to_string(level()); }
	FontMetricsSource &m_metrics;
	int m_state = STATE_LOADING;
	bool m_enabled = false; ///< + 0x50 (starts 0: the control bar's ShowControlBar enables it)
	bool m_iconVisible = true; ///< + 0x51
	float m_width = 0.0f;   ///< + 0x3C
	bool m_hasPending = false, m_hasActive = false;
	Notice m_pending, m_active;
	unsigned m_openedAt = 0; ///< + 0x48
	unsigned m_now = 0;
	std::string m_openAudio;
	std::vector<UnicodeString> m_lines; ///< the message wrapped to the width
	std::vector<std::string> m_callErrors;
};
