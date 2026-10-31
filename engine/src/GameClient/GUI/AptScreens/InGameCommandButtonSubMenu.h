// OpenBFME. GPL-3.0.
//
// InGameCommandButtonSubMenu (lane CAMPUI-1): the engine side of one libInGameUI.CommandButtonSubMenu clip of the Palantir's command arc (palantir.apt sprite 114
// places subMenu0 .. subMenu3 next to the arc buttons 0 .. 3). The name follows the retail role: the sub-menu object PalantirCommandUI keeps per arc slot
// (+ 4 of the slot record) whose items a command button content (InGameToggleStanceCommandButton) adds and opens.
//
// TARGET FACTS (RotWK game.dat, caveat S-001; decompiles in the lane's notes re-stance.md section 3):
//   * RW 0x9D353B (the wrapper, vtable RW 0xC8C46C) makes the impl RW 0x9D3380 (0x48 bytes): + 0 the owner, + 4 the Apt level, + 8 the clip path (the movie's
//     `name=` with the "_level<N>." prefix removed, RW 0x815563), + 0x18 open, + 0x1C the item count, + 0x20 three item records of 0xC bytes ({ item, frame,
//     content }), + 0x44 fading, + 0x45 close requested. The ctor registers "_level%u." + path + "_OnButtonFrameLoaded" (RW 0x9D305F),
//     "..._OnButtonFrameUnloaded" (RW 0x9D31B3) and "..._OnClosed" (RW 0x9D3235).
//   * RW 0x9D305F: atoi(index) in [0, count) whose record has an item and no frame yet takes the frame clip from `name` (level prefix removed).
//   * RW 0x9D31B3: atoi(index) in [0, 3): that record's frame is released.
//   * RW 0x9D3235 (_OnClosed): only while fading: fading cleared, and when open every item is dropped (count down to 0), open cleared, the owner told (vslot 8).
//   * RW 0x9D3285 -> 0x9D2EDA addItem: the record at count gets the item, count + 1 (the caller keeps count <= 3, RW 0x96FAC6).
//   * RW 0x9D3334 -> 0x9D32F9 open: the movie call <path>.Show("%d" of count) (RW 0x92BC2A), open set, the owner told (vslot 4).
//   * RW 0x9D2E1D requestClose: + 0x45 set. RW 0x9D2E0F isOpen (+ 0x18), RW 0x9D2E16 itemCount (+ 0x1C).
//   * RW 0x9D2F92 update: a close request on an open, not yet fading menu calls <path>.FadeOut() and sets fading; the request is cleared either way; then every
//     item with a frame gets its content made (RW 0x9D2E25: the item's vslot 8) and updated (the content's vslot 4).
//   * RW 0x9D32CD -> 0x9D2F21 hide: when open: <path>.Hide(), every item dropped, fading and open cleared, the owner told (vslot 8).
//   * The item content's destructor (InGameCommandButtonMovieClip, RW 0x9D6EC7 -> 0x9D2640) calls DeleteContent on its frame when it made one.
// The movie side (libingameui.apt sprite 26): Show(n) plays trays "_<n>" (n SubMenuButtonTray clips '0' .. n-1 whose MovieClipFrame reports
// "<path>_OnButtonFrameLoaded" with index=<tray>) and goes to `_show`; Hide() goes to `_hide`; FadeOut() plays `_fadeOut`, whose last frame sends
// "<path>_OnClosed" and goes to `_hide`.
// NOT PORTED: the owner notifications (vslots 4 / 8 of + 0, the PalantirCommandUI listener: what they do was not read); the item content is the owner's
// (createItemContent / dropItemContent callbacks), as the item's vslot 8 is in retail.
#pragma once

#include <functional>
#include <string>
#include <vector>

class InGameCommandButtonSubMenu
{
public:
	static constexpr int kMaxItems = 3; ///< RW 0x9D3380: three records at + 0x20

	struct Item
	{
		int value = -1;            ///< the item's data (InGameToggleStanceCommandButton: the stance, RW 0x96F62F)
		std::string frame;         ///< + 4: the item frame clip ("" none), level prefix removed
		bool content = false;      ///< + 8: the item's content was made in the frame
		bool initialized = false;  ///< the content's _OnInitialized arrived (RW 0x9D6E4D)
		std::string sig;           ///< the content state last sent (owner's cache)
	};

	using CallFn = std::function<bool(const std::string &path, const std::string &fn, const std::vector<std::string> &args)>;

	// `path`: the clip path without the level prefix ("CommandButtons.subMenu0"); `call`: a movie function call on the Palantir's level
	InGameCommandButtonSubMenu(int level, std::string path, CallFn call);

	// RW 0x9D3380: the three command names the ctor registers ("_level<N>.<path>" + suffix)
	std::string commandName(const char *suffix) const;
	static const char *const kFrameLoaded;   ///< "_OnButtonFrameLoaded"
	static const char *const kFrameUnloaded; ///< "_OnButtonFrameUnloaded"
	static const char *const kClosed;        ///< "_OnClosed"

	const std::string &path() const { return m_path; }
	bool isOpen() const { return m_open; }            ///< RW 0x9D2E0F
	int itemCount() const { return m_count; }          ///< RW 0x9D2E16
	bool fading() const { return m_fading; }
	bool closeRequested() const { return m_closeRequested; }
	const Item &item(int i) const { return m_items[i]; }
	Item &item(int i) { return m_items[i]; }

	bool addItem(int value);  ///< RW 0x9D3285 / 0x9D2EDA; false when the records are full (reported by the caller)
	bool open();              ///< RW 0x9D32F9: <path>.Show(count); the result of the movie call
	void requestClose() { m_closeRequested = true; } ///< RW 0x9D2E1D
	// RW 0x9D2F92: the FadeOut of a requested close; then `createItemContent(index)` for each item with a frame and no content (true: made), and
	// `updateItemContent(index)` for each item with content
	void update(const std::function<bool(int)> &createItemContent, const std::function<void(int)> &updateItemContent);
	void hide(); ///< RW 0x9D2F21

	// the movie's callbacks (RW 0x9D305F / 0x9D31B3 / 0x9D3235)
	void onButtonFrameLoaded(const std::string &arg);
	void onButtonFrameUnloaded(const std::string &arg);
	void onClosed();

	// the content of a dropped item goes (RW 0x9D6EC7 -> 0x9D2640: DeleteContent on its frame); set by the owner
	void setDropItemContent(std::function<void(const Item &)> fn) { m_dropContent = std::move(fn); }

	// the movie calls made ("Show(3)", "FadeOut()", "Hide()"), the last 64, for tests and reports; failed calls end in " FAILED"
	const std::vector<std::string> &calls() const { return m_calls; }

	// "index=1&name=_level5.X" -> the value of `key` ("" none)
	static std::string queryValue(const std::string &arg, const std::string &key);
	// RW 0x815563: a clip path without its "_level<N>." prefix
	static std::string stripLevel(const std::string &path);

private:
	bool call(const char *fn, const std::vector<std::string> &args);
	void dropItems();

	int m_level;
	std::string m_path;
	CallFn m_call;
	bool m_open = false;           ///< + 0x18
	int m_count = 0;               ///< + 0x1C
	Item m_items[kMaxItems];       ///< + 0x20
	bool m_fading = false;         ///< + 0x44
	bool m_closeRequested = false; ///< + 0x45
	std::function<void(const Item &)> m_dropContent;
	std::vector<std::string> m_calls;
};
