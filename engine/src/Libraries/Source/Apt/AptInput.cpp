// OpenBFME. GPL-3.0.
// See AptInput.h for the citations.

#include "Libraries/Source/Apt/AptInput.h"

#include "Libraries/Source/Apt/Apt.h"
#include "Libraries/Source/Apt/AptButtonInst.h"

#include <algorithm>
#include <cmath>
#include <functional>

namespace
{

// Event masks of AptCIH::fire used by input (AptCharacterInst.h) and of the button action dispatch.
constexpr std::uint32_t kAnyMouseMask = 0x9FC38; // hasHandler(0x9FC38) at 0x00AE24D0: the clip handles some mouse event

// Button transition bits (SWF ButtonCondAction low byte; AptInput 0x00AFA100 maps them to events).
constexpr std::uint32_t kIdleToOverUp = 0x01;
constexpr std::uint32_t kOverUpToIdle = 0x02;
constexpr std::uint32_t kOverUpToOverDown = 0x04;
constexpr std::uint32_t kOverDownToOverUp = 0x08;
constexpr std::uint32_t kOverDownToOutDown = 0x10;
constexpr std::uint32_t kOutDownToOverDown = 0x20;
constexpr std::uint32_t kOutDownToIdle = 0x40;

struct ButtonEventEntry
{
	std::uint32_t mask;
	const char *name;
	std::uint32_t arg;
};

// BFME2 0x00DDC898, 7 entries: the event, the member the script may assign to the button, and the argument word.
const ButtonEventEntry kButtonEvents[] = {
	{ APT_EVT_RELEASE, "onRelease", 1 },
	{ APT_EVT_PRESS, "onPress", 0 },
	{ APT_EVT_RELEASEOUTSIDE, "onReleaseOutside", 1 },
	{ APT_EVT_ROLLOVER, "onRollOver", 1 },
	{ APT_EVT_ROLLOUT, "onRollOut", 1 },
	{ APT_EVT_DRAGOVER, "onDragOver", 0 },
	{ APT_EVT_DRAGOUT, "onDragOut", 0 },
};

// BFME2 0x00DDC8EC, 6 entries: the listener events (Mouse / Key objects).
const ButtonEventEntry kListenerEvents[] = {
	{ APT_EVT_KEYUP, "onKeyUp", 0 },
	{ APT_EVT_KEYDOWN, "onKeyDown", 0 },
	{ APT_EVT_MOUSEDOWN, "onMouseDown", 0 },
	{ APT_EVT_MOUSEUP, "onMouseUp", 0 },
	{ APT_EVT_MOUSEMOVE, "onMouseMove", 0 },
	{ APT_EVT_MOUSEWHEEL, "onMouseWheel", 0 },
};

bool alphaVisible(const AptCharacterInst &inst)
{
	// 0x00AE0BF0: every instance up the parent chain has an alpha of at least 0.5
	for (const AptCharacterInst *c = &inst; c; c = c->parent())
	{
		if (!c->visible || c->color.mul[3] < 0.5f)
		{
			return false;
		}
	}
	return true;
}

void walk(AptCharacterInst *inst, const std::function<void(AptCharacterInst *)> &fn)
{
	fn(inst);
	if (AptSpriteInst *s = inst->asSprite())
	{
		std::vector<AptCharacterInst *> kids = s->children();
		for (AptCharacterInst *k : kids)
		{
			walk(k, fn);
		}
	}
}

} // namespace

AptInput::AptInput(Apt &apt) : m_apt(apt)
{
}

void AptInput::postMouseMove(float x, float y)
{
	Event e;
	e.type = EventType::MouseMove;
	e.x = x;
	e.y = y;
	m_queue.push_back(e);
}

void AptInput::postMouseButton(bool down)
{
	Event e;
	e.type = EventType::MouseButton;
	e.down = down;
	m_queue.push_back(e);
}

void AptInput::postMouseWheel(int delta)
{
	Event e;
	e.type = EventType::MouseWheel;
	e.code = delta;
	m_queue.push_back(e);
}

void AptInput::postKey(int keyCode, bool down)
{
	Event e;
	e.type = EventType::Key;
	e.down = down;
	e.code = keyCode;
	m_queue.push_back(e);
}

void AptInput::processQueued()
{
	// 0x00AFB910: the queue is drained in order; the first event of the batch is flagged (0x00AFB938 `sete al`)
	std::vector<Event> events;
	events.swap(m_queue);
	for (std::size_t i = 0; i < events.size(); ++i)
	{
		processEvent(events[i], i == 0);
	}
}

void AptInput::forget(AptSpriteInst *level)
{
	auto inLevel = [&](AptCharacterInst *c) {
		for (; c; c = c->parent())
		{
			if (c == level)
			{
				return true;
			}
		}
		return false;
	};
	if (inLevel(m_currentButton))
	{
		m_currentButton = nullptr;
		m_pressed = false;
	}
	if (inLevel(m_pressedClip))
	{
		m_pressedClip = nullptr;
	}
	if (inLevel(m_prevHover))
	{
		m_prevHover = nullptr;
	}
	if (inLevel(m_hoverClip))
	{
		m_hoverClip = nullptr;
	}
}

void AptInput::markRoots(AptGC &gc)
{
	gc.mark(m_currentButton);
	gc.mark(m_pressedClip);
	gc.mark(m_prevHover);
	gc.mark(m_hoverClip);
	for (const AptValue &v : m_mouseListeners)
	{
		gc.mark(v);
	}
	for (const AptValue &v : m_keyListeners)
	{
		gc.mark(v);
	}
}

// ---- hit tests ------------------------------------------------------------------------------------------------

void AptInput::collectSprites(std::vector<AptSpriteInst *> &out) const
{
	for (int lvl : m_apt.loadedLevels())
	{
		if (AptSpriteInst *root = m_apt.level(lvl))
		{
			walk(root, [&](AptCharacterInst *c) {
				if (AptSpriteInst *s = c->asSprite())
				{
					if (s->defined())
					{
						out.push_back(s);
					}
				}
			});
		}
	}
}

void AptInput::collectButtons(std::vector<AptButtonInst *> &out) const
{
	for (int lvl : m_apt.loadedLevels())
	{
		if (AptSpriteInst *root = m_apt.level(lvl))
		{
			walk(root, [&](AptCharacterInst *c) {
				if (AptButtonInst *b = c->asButton())
				{
					if (b->defined())
					{
						out.push_back(b);
					}
				}
			});
		}
	}
}

AptButtonInst *AptInput::hitTestButtons(float x, float y)
{
	// 0x00AFA420: from the last button of the list to the first; a Hit record (state mask 8) whose mesh contains the point
	std::vector<AptButtonInst *> buttons;
	collectButtons(buttons);
	for (std::size_t i = buttons.size(); i-- > 0;)
	{
		AptButtonInst *b = buttons[i];
		if (!b->enabled || !alphaVisible(*b))
		{
			continue;
		}
		if (b->hitTest(x, y))
		{
			return b;
		}
	}
	return nullptr;
}

bool AptInput::pointInsideBounds(const AptCharacterInst &clip) const
{
	// 0x00AF9FC0: the cursor against the clip's bounding rectangle (inclusive)
	float cx0, cy0, cx1, cy1;
	if (!clip.contentBounds(cx0, cy0, cx1, cy1))
	{
		return false;
	}
	AptMatrix g = clip.globalMatrix();
	float xs[4] = { cx0, cx1, cx0, cx1 };
	float ys[4] = { cy0, cy0, cy1, cy1 };
	float x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f;
	for (int i = 0; i < 4; ++i)
	{
		float ox, oy;
		g.apply(xs[i], ys[i], ox, oy);
		x0 = std::min(x0, ox);
		y0 = std::min(y0, oy);
		x1 = std::max(x1, ox);
		y1 = std::max(y1, oy);
	}
	float mx, my;
	m_apt.mousePosition(mx, my);
	return mx >= x0 && mx <= x1 && my >= y0 && my <= y1;
}

AptSpriteInst *AptInput::hitTestClips(float x, float y)
{
	float savedX, savedY;
	m_apt.mousePosition(savedX, savedY);
	m_apt.setMousePosition(x, y);
	std::vector<AptSpriteInst *> sprites;
	collectSprites(sprites);
	AptSpriteInst *best = nullptr;
	for (AptSpriteInst *s : sprites)
	{
		if (!s->enabled || !alphaVisible(*s) || !s->hasHandler(kAnyMouseMask) || !pointInsideBounds(*s))
		{
			continue;
		}
		// 0x00AFB1F4..0x00AFB227: a descendant of the best so far, or a clip that sorts after it, replaces it; the walk is
		// in display order, so a later clip is above an earlier one
		best = s;
	}
	m_apt.setMousePosition(savedX, savedY);
	return best;
}

// ---- event processing -----------------------------------------------------------------------------------------

void AptInput::fireOn(AptCharacterInst *inst, std::uint32_t mask, std::uint32_t arg, bool member)
{
	if (AptSpriteInst *s = inst ? inst->asSprite() : nullptr)
	{
		s->fire(mask, arg, member);
	}
}

void AptInput::processEvent(const Event &e, bool first)
{
	if (e.type == EventType::MouseMove)
	{
		m_apt.setMousePosition(e.x, e.y); // 0x00AFB887: gApt+0x74 / +0x78
	}
	if (e.type == EventType::Key)
	{
		if (e.down)
		{
			m_keys.insert(e.code);
			m_lastKey = e.code;
		}
		else
		{
			m_keys.erase(e.code);
		}
	}
	dispatchToClips(e, first);
	broadcastToListeners(e);
	buttonLogic(e);
}

void AptInput::dispatchToClips(const Event &e, bool first)
{
	// 0x00AFB120
	const bool isMouse = e.type != EventType::Key;
	std::vector<AptSpriteInst *> sprites;
	collectSprites(sprites);
	AptSpriteInst *best = nullptr;
	bool firedDown = false, firedUp = false, keyPressFired = false;
	int receivers = 0;
	for (AptSpriteInst *s : sprites)
	{
		if (!s->defined() || !s->enabled)
		{
			continue; // 0x00AFB183: a blocked instance is skipped
		}
		if (isMouse && first && alphaVisible(*s) && s->hasHandler(kAnyMouseMask) && pointInsideBounds(*s))
		{
			best = s;
		}
		switch (e.type)
		{
			case EventType::MouseButton:
				if (e.down)
				{
					receivers += s->fire(APT_EVT_MOUSEDOWN, 0, true) ? 1 : 0; // 0x00AFB27F
					firedDown = true;
				}
				else
				{
					receivers += s->fire(APT_EVT_MOUSEUP, 0, true) ? 1 : 0; // 0x00AFB2BF
					firedUp = true;
				}
				break;
			case EventType::MouseMove:
				receivers += s->fire(APT_EVT_MOUSEMOVE, 0, true) ? 1 : 0; // 0x00AFB2E4
				break;
			case EventType::Key:
				if (e.down)
				{
					s->fire(APT_EVT_KEYDOWN, (std::uint32_t)e.code, false); // 0x00AFB3E6
					if (!keyPressFired)
					{
						keyPressFired = s->fire(APT_EVT_KEYPRESS, (std::uint32_t)e.code << 17, true); // 0x00AFB3FD
					}
				}
				else
				{
					s->fire(APT_EVT_KEYUP, (std::uint32_t)e.code, false); // 0x00AFB364
				}
				break;
			default:
				break;
		}
	}
	if (receivers > 1)
	{
		// S-107: EA walks the input set in registration order, the port walks the display tree
		m_apt.note("input-delivery-order", std::to_string(receivers) + " clips handle one mouse event");
	}
	if (e.type == EventType::Key && e.down && m_currentButton)
	{
		// S-108: a script that gave the current button directional members expects the focus to move; not ported
		for (const char *dir : { "_up", "_down", "_left", "_right" })
		{
			AptValue v;
			if (m_currentButton->props.get(dir, v))
			{
				m_apt.note("focus-navigation-member-ignored", std::string(dir) + " of " + m_currentButton->targetPath());
			}
		}
	}
	if (e.type == EventType::Key && e.down && !keyPressFired)
	{
		// 0x00AFB429..0x00AFB561: a key-only button action (the key code in bits 9..15 of the condition word) runs
		// with the button's parent as target
		std::vector<AptButtonInst *> buttons;
		collectButtons(buttons);
		for (AptButtonInst *b : buttons)
		{
			if (!b->defined() || !b->enabled || !b->info())
			{
				continue;
			}
			bool matched = false;
			for (const AptButtonAction &a : b->info()->actions)
			{
				if (((a.keyCode >> 1) & 0x7F) != 0 && (int)((a.keyCode >> 1) & 0x7F) == e.code)
				{
					std::string error;
					std::shared_ptr<const AptCodeBlock> code = b->charRef().file->codeAt(a.codeOffset, &error);
					if (!code)
					{
						m_apt.vm().reportError("button key action at file offset " + std::to_string(a.codeOffset) + " cannot be decoded: " + error);
						continue;
					}
					AptAction act;
					act.kind = AptAction::Code;
					act.eventMask = 0x400000;
					act.tag = m_apt.currentTag();
					act.code = code;
					act.codeFile = b->charRef().file;
					act.target = b->parent();
					m_apt.pushAction(act);
					matched = true;
					break;
				}
			}
			if (matched)
			{
				return;
			}
		}
	}
	if (isMouse && first)
	{
		m_hoverClip = best; // 0x00AFB534
	}
	if (isMouse)
	{
		processClipMouse(firedDown, firedUp, 0);
		updateButtonHover(); // 0x00AFB552 (device == mouse)
	}
}

void AptInput::processClipMouse(bool down, bool up, std::uint32_t code)
{
	// 0x00AFA7E0
	(void)code;
	if (up)
	{
		if (!m_pressedClip || !m_pressedClip->defined())
		{
			return;
		}
		// gApt+0x44 (the release test's capture) is taken as undefined: Release when the cursor is inside the pressed
		// clip and it is also the clip under the cursor, ReleaseOutside otherwise (0x00AFA829..0x00AFA85E)
		bool inside = pointInsideBounds(*m_pressedClip);
		if (inside && m_hoverClip == m_pressedClip)
		{
			fireOn(m_pressedClip, APT_EVT_RELEASE, 0, true);
		}
		else
		{
			fireOn(m_pressedClip, APT_EVT_RELEASEOUTSIDE, 0, true);
		}
		m_pressedClip = nullptr;
		return;
	}
	if (down)
	{
		if (!m_hoverClip || !m_hoverClip->defined())
		{
			return;
		}
		fireOn(m_hoverClip, APT_EVT_PRESS, 0, true); // 0x00AFA8A9
		m_pressedClip = m_hoverClip;
		return;
	}
	// movement
	if (m_pressedClip && m_pressedClip->defined())
	{
		bool inside = pointInsideBounds(*m_pressedClip);
		if (m_prevHover && !inside)
		{
			fireOn(m_pressedClip, APT_EVT_DRAGOUT, 0, true); // 0x00AFA907
			m_prevHover = nullptr;
		}
		else if (!m_prevHover && inside)
		{
			fireOn(m_pressedClip, APT_EVT_DRAGOVER, 0, true); // 0x00AFA93E
			m_prevHover = m_pressedClip;
		}
		return;
	}
	AptCharacterInst *cur = m_hoverClip;
	if (cur && cur->defined() && cur != m_prevHover)
	{
		if (m_prevHover && m_prevHover->defined() && m_prevHover != m_pressedClip)
		{
			fireOn(m_prevHover, APT_EVT_ROLLOUT, 0, true); // 0x00AFA989
		}
		fireOn(cur, APT_EVT_ROLLOVER, 0, true); // 0x00AFA9A5
		m_prevHover = cur;
	}
	else if ((!cur || !cur->defined()) && m_prevHover && m_prevHover->defined() && !pointInsideBounds(*m_prevHover))
	{
		fireOn(m_prevHover, APT_EVT_ROLLOUT, 0, true); // 0x00AFAA0E
		m_prevHover = nullptr;
	}
}

void AptInput::broadcastToListeners(const Event &e)
{
	// 0x00AFB5B0: Mouse / Key listener objects get the event as a function call at the back of the pool
	std::uint32_t mask = 0;
	const std::vector<AptValue> *list = nullptr;
	switch (e.type)
	{
		case EventType::MouseButton:
			mask = e.down ? APT_EVT_MOUSEDOWN : APT_EVT_MOUSEUP;
			list = &m_mouseListeners;
			break;
		case EventType::MouseMove:
			mask = APT_EVT_MOUSEMOVE;
			list = &m_mouseListeners;
			break;
		case EventType::MouseWheel:
			mask = APT_EVT_MOUSEWHEEL;
			list = &m_mouseListeners;
			break;
		case EventType::Key:
			mask = e.down ? APT_EVT_KEYDOWN : APT_EVT_KEYUP;
			list = &m_keyListeners;
			break;
	}
	if (!list || list->empty())
	{
		return;
	}
	std::vector<AptValue> snapshot = *list;
	for (const AptValue &l : snapshot)
	{
		if (!l.isObject() || !l.asObject())
		{
			continue;
		}
		for (const ButtonEventEntry &entry : kListenerEvents)
		{
			if (entry.mask != mask)
			{
				continue;
			}
			AptValue fn = m_apt.vm().getMember(l, entry.name);
			if (fn.isObject() && fn.asObject() && fn.asObject()->kind() == AptObjectKind::Function)
			{
				std::vector<AptValue> args;
				if (mask == APT_EVT_MOUSEWHEEL)
				{
					args.push_back(AptValue::integer(e.code));
				}
				m_apt.pushFunctionCallOn(l, fn, args, mask, 0, false);
			}
		}
	}
}

void AptInput::buttonLogic(const Event &e)
{
	// 0x00AFAF80 (event type 0 = mouse button 0: 0x00AFB03E)
	if (e.type != EventType::MouseButton)
	{
		return;
	}
	AptButtonInst *cur = m_currentButton;
	if (!cur)
	{
		m_pressed = e.down; // 0x00AFB045: no current button: the pressed flag follows the state
		return;
	}
	if (!m_pressed && e.down)
	{
		m_pressed = true;
		cur->setState(AptButtonInst::State::Down);
		doButtonTransition(cur, kOverUpToOverDown);
	}
	else if (m_pressed && !e.down)
	{
		m_pressed = false;
		if (cur->state() == AptButtonInst::State::Over)
		{
			// pressed and dragged outside: released outside (0x00AFB09E..0x00AFB0DA)
			cur->setState(AptButtonInst::State::Up);
			doButtonTransition(cur, kOutDownToIdle);
			if (m_currentButton)
			{
				m_currentButton->setState(AptButtonInst::State::Over);
				doButtonTransition(m_currentButton, kIdleToOverUp);
			}
		}
		else
		{
			cur->setState(AptButtonInst::State::Over); // 0x00AFB0E8
			doButtonTransition(cur, kOverDownToOverUp);
		}
	}
}

void AptInput::updateButtonHover()
{
	// 0x00AFAA20
	float mx, my;
	m_apt.mousePosition(mx, my);
	AptButtonInst *hit = hitTestButtons(mx, my);
	AptButtonInst *cur = m_currentButton;
	if (m_pressed)
	{
		if (cur && cur->defined())
		{
			if (hit != cur)
			{
				if (cur->state() == AptButtonInst::State::Down)
				{
					cur->setState(AptButtonInst::State::Over); // 0x00AFAA83
					doButtonTransition(cur, kOverDownToOutDown);
				}
			}
			else if (cur->state() == AptButtonInst::State::Over)
			{
				cur->setState(AptButtonInst::State::Down); // 0x00AFAAA4
				doButtonTransition(cur, kOutDownToOverDown);
			}
		}
		return;
	}
	if (hit == cur)
	{
		return;
	}
	if (cur && cur->defined())
	{
		cur->setState(AptButtonInst::State::Up);
		doButtonTransition(cur, kOverUpToIdle); // 0x00AFAADF
	}
	m_currentButton = hit;
	if (hit && hit->defined())
	{
		hit->setState(AptButtonInst::State::Over);
		doButtonTransition(hit, kIdleToOverUp); // 0x00AFAB1F
	}
}

void AptInput::doButtonTransition(AptButtonInst *b, std::uint32_t transition)
{
	// 0x00AFA100
	if (!b || !b->info())
	{
		return;
	}
	const AptButtonInfo &info = *b->info();
	for (const AptButtonAction &a : info.actions)
	{
		if (!(a.transitionMask & transition))
		{
			continue;
		}
		std::string error;
		std::shared_ptr<const AptCodeBlock> code = b->charRef().file->codeAt(a.codeOffset, &error);
		if (!code)
		{
			m_apt.vm().reportError("button action at file offset " + std::to_string(a.codeOffset) + " cannot be decoded: " + error);
			continue;
		}
		AptAction act;
		act.kind = AptAction::Code;
		act.eventMask = 0x400000; // 0x00AFA1E4
		act.tag = m_apt.currentTag();
		act.code = code;
		act.codeFile = b->charRef().file;
		act.target = b->parent(); // 0x00AFA1DD: the button's PARENT is the script's `this`
		m_apt.pushAction(act);
	}
	std::uint32_t events = 0;
	if (transition & kOverDownToOverUp)
	{
		events |= APT_EVT_RELEASE;
	}
	if (transition & kOverUpToOverDown)
	{
		events |= APT_EVT_PRESS;
	}
	if (transition & kOutDownToIdle)
	{
		events |= APT_EVT_RELEASEOUTSIDE;
	}
	if (transition & kIdleToOverUp)
	{
		events |= APT_EVT_ROLLOVER;
	}
	if (transition & kOverUpToIdle)
	{
		events |= APT_EVT_ROLLOUT;
	}
	if (transition & kOutDownToOverDown)
	{
		events |= APT_EVT_DRAGOVER;
	}
	if (transition & kOverDownToOutDown)
	{
		events |= APT_EVT_DRAGOUT;
	}
	for (const ButtonEventEntry &entry : kButtonEvents)
	{
		if (!(entry.mask & events))
		{
			continue;
		}
		AptValue fn;
		if (b->props.get(entry.name, fn) && fn.isObject() && fn.asObject() && fn.asObject()->kind() == AptObjectKind::Function)
		{
			m_apt.pushFunctionCall(b, fn, 0, entry.mask, (entry.arg << 10) | 5u, true); // 0x00AFA2A6: front of the pool
		}
	}
	// The button sounds (0x00AFA2D2: callback 0x00E17784 with the sound id of the transition) are the audio host's; the
	// parser has no sound table for buttons yet (the 0x3C pointer of the button character is not decoded)
	m_apt.runActions(); // 0x00AFA314
}

// ---- focus and listeners -----------------------------------------------------------------------------------------

void AptInput::addMouseListener(const AptValue &listener)
{
	m_mouseListeners.push_back(listener);
}

void AptInput::removeMouseListener(const AptValue &listener)
{
	m_mouseListeners.erase(std::remove_if(m_mouseListeners.begin(), m_mouseListeners.end(), [&](const AptValue &v) { return v.isObject() && listener.isObject() && v.asObject() == listener.asObject(); }), m_mouseListeners.end());
}

void AptInput::addKeyListener(const AptValue &listener)
{
	m_keyListeners.push_back(listener);
}

void AptInput::removeKeyListener(const AptValue &listener)
{
	m_keyListeners.erase(std::remove_if(m_keyListeners.begin(), m_keyListeners.end(), [&](const AptValue &v) { return v.isObject() && listener.isObject() && v.asObject() == listener.asObject(); }), m_keyListeners.end());
}
