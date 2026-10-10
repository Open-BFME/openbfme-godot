// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// HotKeyTranslator (ZH Include/GameClient/HotKey.h, Source/GameClient/MessageStream/HotKey.cpp), lane INPUT-1: a key released over the game presses the
// command button whose label marks that key with '&' ("&Farm", "Ambush For&mation").
//
// TARGET FACTS (RotWK 2.01 game.dat, caveat S-001): GameClient::init (RW 0x646771, BFME2 decomp GameClientDrawableTOC.cpp, tier A) attaches the
// HotKeyTranslator at priority 25 (vtable RW 0xC046B8, translate RW 0x75B23B -> TheHotKeyManager RW 0xDE7870 -> RW 0x75B068). For MSG_RAW_KEY_UP
// (0x16) with no modifier, or Shift alone (KEY_STATE & 0x430; Ctrl 0xC or Alt 0xC0 refuse the key), the key's character (RW 0x63F14D: the keyboard's
// translation of the scan code) is looked up (RW 0x75AEFA): first lower case, then upper case, in the manager's two maps (+ 0x0C, + 0x18), and only
// while the UI takes input (InGameUI + 0x15 / + 0x16). A found action that does not apply (vslot 4) is passed over; one that is disabled (vslot 8)
// plays the disabled sound (RW 0x75A5BC) and keeps the key; one that runs (vslot 0xC) plays the enabled sound (RW 0x75A544) and the key message is
// destroyed. The command bar registers an action per command window (ControlBar::setControlCommand RW 0x71CF3E, the call RW 0x71D139 -> RW 0x75AE14; RW 0x96F890 registers the stance sub-menu's): its key is the character after the first '&' of
// the button's translated TextLabel (RW 0x75A7CB -> RW 0x75A67F).
// NOT PORTED (stop S-280): the manager's message-type actions (a meta message bound to a window, RW 0x75B2BA), the other registrations (the radial / stance sub-menus RW 0x96FCCB / 0x96FDFD, the garrison inventory RW 0x94518D, RW 0x92C0A2 .. 0x92DDE7, RW 0x505B6C), the
// hotkey sounds (EnabledHotKeyPressed / DisabledHotKeyPressed are counted, not played), the RW 0x90F92C and InGameUI vslot 0x17C gates.
// INFERENCE: the character is the layout's (the device passes it with the key); without one the US layout's letter or digit of the scan code is used.

#pragma once

#include "GameClient/HudContext.h"
#include "GameClient/MessageStream/MessageStream.h"

#include <functional>
#include <map>
#include <string>
#include <vector>

class HotKeyTranslator : public MessageTranslator
{
public:
	enum class Availability
	{
		Hidden,   ///< vslot 4: the action does not apply now (passed over)
		Disabled, ///< vslot 8 false: the disabled sound, the key is kept
		Enabled
	};
	struct Entry
	{
		char32_t key = 0; ///< the character after '&' (as the label writes it)
		int slot = -1;
		bool inPalantir = false;
		Availability availability = Availability::Hidden;
	};

	explicit HotKeyTranslator(HudContext &ctx) : m_ctx(ctx) {}
	MessageDisposition translate(const ClientMessage &message) override;

	// the registered actions (the command bar's windows), replaced whenever the bar changes; the press is ControlBar::pressButton
	void setEntries(std::vector<Entry> entries) { m_entries = std::move(entries); }
	const std::vector<Entry> &entries() const { return m_entries; }
	void setPress(std::function<bool(int slot, bool inPalantir)> press) { m_press = std::move(press); }

	// the character after the first '&' of a translated label (RW 0x75A67F), 0 without one
	static char32_t hotkeyOf(const std::u16string &text);
	// the US layout's character of a DirectInput scan code (letters and digits), 0 for others
	static char32_t usCharOf(int scanCode);

	// what the presses did: "enabled" (a button ran), "disabled", "none"
	const std::map<std::string, unsigned> &outcomes() const { return m_outcomes; }

private:
	const Entry *find(char32_t ch, bool &disabledSeen) const;

	HudContext &m_ctx;
	std::vector<Entry> m_entries;
	std::function<bool(int, bool)> m_press;
	std::map<std::string, unsigned> m_outcomes;
};
