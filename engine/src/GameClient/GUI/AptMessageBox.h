// OpenBFME. GPL-3.0.
//
// AptMessageBox (lane UI-1): RotWK's message box, the object TheMessageBox (RW 0xDE8A9C) built by RW 0x81A3FB on `_level13`'s clip "MessageBox", the
// MessageBox clip of GuiFX.apt.
//
// TARGET FACTS (RotWK game.dat, caveat S-001):
//   * construction RW 0x9534DE: the level and the clip path, the type 5 (none), and eight commands "_level%u." + path + "_OnButtonOk / _OnButtonCancel /
//     _OnButtonYes / _OnButtonNo / _OnShowing / _OnShown / _OnHiding / _OnHidden" (RW 0xC816B4 .. 0xC81710), the names the clip's buttons send
//     ("FSCommand:" + _parent + "_OnButtonOk", GuiFX.apt);
//   * show RW 0x953861 (the global RW 0x81A375 / 0x81A452 / 0x81A4A4): a box already up is hidden first (RW 0x953243 unless the type is 5); the type, the
//     button and state callbacks are kept, the texts go to the records "APT:_level%u.%s_" + "Title" / "Text" (RW 0xC8171C; the clip's fields read
//     "$" + _parent._parent + "_Title"), the box is "large" when the text is longer than 256 characters, and the clip's Show(type name, large) is called
//     (RW 0x9532F3 -> 0x9530D6: "Ok", "OkCancel", "YesNo", "Cancel", "NonInteractive" for the types 0 .. 4);
//   * hide RW 0x953243 (RW 0x81A38D): the type becomes 5 and the clip's Hide(instant) (RW 0x92E25E with "Hide");
//   * a button command RW 0x953141 .. 0x953197: the type becomes 5 and the button callback gets 0 Ok, 1 Cancel, 2 Yes, 3 No; the state commands
//     (RW 0x9531B4 ..) give the state callback 0 showing, 1 shown, 2 hiding, 3 hidden.
// OpenBFME DIFFERENCES: GuiFX.apt is at its BFME1 level 11 here (RotWK: 13, see EndGame.h / S-1060); the records and commands use the level the movie is
// in, so the clip's own names match. The box's timeout (+ 0x24, timeGetTime) and Change (state 4) are not used by the callers ported here.

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

class AptScreen;
class Shell;
class WindowManager;

class AptMessageBox
{
public:
	enum Type
	{
		TYPE_OK = 0,
		TYPE_OK_CANCEL = 1,
		TYPE_YES_NO = 2,
		TYPE_CANCEL = 3,
		TYPE_NON_INTERACTIVE = 4,
		TYPE_NONE = 5
	};
	enum Button
	{
		BUTTON_OK = 0,
		BUTTON_CANCEL = 1,
		BUTTON_YES = 2,
		BUTTON_NO = 3
	};
	using ButtonFn = std::function<void(int button)>;

	// Loads GuiFX.apt (AptGuiFXScreen) for the box's clip.
	AptMessageBox(WindowManager &windows, Shell &shell);
	~AptMessageBox();
	AptMessageBox(const AptMessageBox &) = delete;
	AptMessageBox &operator=(const AptMessageBox &) = delete;

	// RW 0x953861: the texts are the already fetched game texts (UTF-16)
	void show(Type type, const std::u16string &title, const std::u16string &text, ButtonFn onButton = ButtonFn());
	// RW 0x953243
	void hide(bool instant = false);
	// RW 0x9532F3: a pending show / hide reaches the clip once the movie is loaded (the lobby calls it every update)
	void update();

	Type type() const { return m_type; }
	int level() const;
	// the clip calls made, in order ("Show Ok false", "Hide false"), for tests
	const std::vector<std::string> &calls() const { return m_calls; }
	std::u16string title() const { return m_title; }
	std::u16string text() const { return m_text; }

private:
	void registerCommands();
	std::string prefix() const; // "_level<n>.MessageBox"

	WindowManager &m_windows;
	std::unique_ptr<AptScreen> m_guiFX;
	Type m_type = TYPE_NONE;
	int m_pending = 0; // RW + 0x14: 0 nothing, 2 show, 3 hide
	bool m_instant = false;
	bool m_large = false;
	ButtonFn m_onButton;
	std::u16string m_title, m_text;
	std::vector<std::string> m_calls;
	std::vector<std::string> m_commands;
};
