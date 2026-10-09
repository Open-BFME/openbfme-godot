// OpenBFME. GPL-3.0.
//
// Mouse and key input of the EA Apt player (menus-apt.md step A3).  Ported from the BFME2 1.06 game.dat
// (AptInput.cpp 0x00AFA020..0x00AFB910; the state lives in gApt):
//
//   queue            events are posted as one dword each and drained once per update step, after the action pool
//                    ran (AptUpdate 0x00ACD862..0x00ACD878; drain 0x00AFB910 -> 0x00AFB860 per event).  The first
//                    event of a batch also recomputes the clip under the cursor (0x00AFB534).
//   per event        0x00AFB120 (clip events and key shortcuts) -> 0x00AFB5B0 (Mouse / Key listener objects) ->
//                    0x00AFAF80 (button state changes).
//   clip events      every clip with a handler gets MouseDown (0x10) / MouseUp (0x20) / MouseMove (0x8) / KeyDown
//                    (0x40) / KeyUp (0x80) through AptCIH::fire; the topmost clip under the cursor that handles
//                    any of the mouse masks 0x9FC38 (bounding-rect test 0x00AF9FC0, ancestors' alpha >= 0.5) is the
//                    hit clip: Press (0x400) on mouse down and Release (0x800) / ReleaseOutside (0x1000) on mouse up
//                    for the clip pressed before, RollOver (0x2000) / RollOut (0x4000) while idle, DragOut
//                    (0x10000) / DragOver (0x8000) while pressed (0x00AFA7E0).
//   buttons          the topmost button whose Hit records' mesh contains the cursor (0x00AFA420) is the current
//                    button; its state follows hover (Up=1, Over=2) and press (Down=4) (0x00AFAA20, 0x00AFAF80)
//                    and every change runs the button's actions for the transition (0x00AFA100): the programs of
//                    the matching button actions are queued at the BACK of the pool with the button's PARENT as
//                    target, the member handlers the script assigned (onPress ...) at the FRONT, then the pool
//                    runs at once (0x00AE6540), so an fscommand of a button action fires inside the input call.
//
// Focus: the EA input has no separate focus object; the button the pointer (or the keyboard / gamepad navigation) is on is
// gApt+0x6C, the "current button", and is the one in state Over / Down.  `focus()` returns it.  Moving it by the
// `_up/_down/_left/_right` members of a button (0x00AFAD29..0x00AFAD35 resolve those members, 0x00AFA340 re-targets the current
// button) is not ported.
//
// Unverified (docs/STOPS.md S-107, S-108): the delivery order of the global mouse / key events among clips (EA walks
// a registration-ordered set; the port walks the display tree), the meaning of gApt+0x44 in the release test (taken
// as "no capture"), the source of the button hit list (EA fills it while rendering; the port derives it from the
// tree), the key ids of the Key natives, and the directional focus navigation.

#pragma once

#include "Libraries/Source/Apt/AptObject.h"

#include <cstdint>
#include <set>
#include <string>
#include <vector>

class Apt;
class AptSpriteInst;
class AptCharacterInst;
class AptButtonInst;

class AptInput
{
public:
	enum class EventType : std::uint8_t
	{
		MouseMove,
		MouseButton, // button 0: `down` says pressed or released
		MouseWheel,
		Key
	};

	struct Event
	{
		EventType type = EventType::MouseMove;
		float x = 0, y = 0;
		bool down = false;
		int code = 0; // key code, or the wheel delta
	};

	explicit AptInput(Apt &apt);

	// ---- posting (host side) --------------------------------------------------------------------------------
	void postMouseMove(float x, float y);
	void postMouseButton(bool down);
	void postMouseWheel(int delta);
	void postKey(int keyCode, bool down);
	std::size_t queued() const { return m_queue.size(); }

	// Drain the queue (AptUpdate calls this once per step).
	void processQueued();

	// ---- observation ----------------------------------------------------------------------------------------
	AptButtonInst *currentButton() const { return m_currentButton; }
	bool mouseDown() const { return m_pressed; }
	AptCharacterInst *hoverClip() const { return m_hoverClip; }
	AptCharacterInst *pressedClip() const { return m_pressedClip; }
	AptButtonInst *focus() const { return m_currentButton; } // the current button is the focus (see the header comment)
	bool isKeyDown(int keyCode) const { return m_keys.count(keyCode) != 0; }
	int lastKeyCode() const { return m_lastKey; }

	// ---- scripting side ---------------------------------------------------------------------------------------
	void addMouseListener(const AptValue &listener);
	void removeMouseListener(const AptValue &listener);
	void addKeyListener(const AptValue &listener);
	void removeKeyListener(const AptValue &listener);

	// The topmost button under (x, y), skipping disabled and invisible ones (0x00AFA420); null when none.
	AptButtonInst *hitTestButtons(float x, float y);
	// The topmost clip under (x, y) that handles a mouse event (the inputSet walk of 0x00AFB120).
	AptSpriteInst *hitTestClips(float x, float y);

	void forget(AptSpriteInst *level); // a level was unloaded: drop references into it
	void markRoots(AptGC &gc);

private:
	void processEvent(const Event &e, bool first);
	void dispatchToClips(const Event &e, bool first);
	void broadcastToListeners(const Event &e);
	void buttonLogic(const Event &e);
	void updateButtonHover();                       // 0x00AFAA20
	void doButtonTransition(AptButtonInst *b, std::uint32_t transition); // 0x00AFA100
	void processClipMouse(bool down, bool up, std::uint32_t code);        // 0x00AFA7E0
	bool pointInsideBounds(const AptCharacterInst &clip) const;
	void collectButtons(std::vector<AptButtonInst *> &out) const;
	void collectSprites(std::vector<AptSpriteInst *> &out) const;
	void fireOn(AptCharacterInst *inst, std::uint32_t mask, std::uint32_t arg, bool member);

	Apt &m_apt;
	std::vector<Event> m_queue;
	bool m_pressed = false;                    // gApt+0x70
	AptButtonInst *m_currentButton = nullptr;  // gApt+0x6C
	AptCharacterInst *m_pressedClip = nullptr; // gApt+0x60
	AptCharacterInst *m_prevHover = nullptr;   // gApt+0x64
	AptCharacterInst *m_hoverClip = nullptr;   // gApt+0x68
	std::set<int> m_keys;
	int m_lastKey = 0;
	std::vector<AptValue> m_mouseListeners;
	std::vector<AptValue> m_keyListeners;
};
