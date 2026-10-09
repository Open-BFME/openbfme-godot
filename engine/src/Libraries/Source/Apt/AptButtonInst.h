// OpenBFME. GPL-3.0.
//
// Button instances (AptCIH type 0x0E) and the button state machine (menus-apt.md step A3).
//
// Target facts (BFME2 1.06 game.dat): a button instance is created by AptDisplayList::place for character type 4
// (0x00AF8739: a 0x20-byte button base, constructor 0x00AF8550, state frame at +0x18 = 0).  Its content is a display
// list (+0x1C) holding the records of the button for the current state; the instance advances its children like a
// sprite does (AptCIH advance, button arm 0x00AE2E90).  The state changes and the mouse/key actions are driven by
// AptInput (AptInput.cpp, 0x00AFA020..0x00AFB5B0), see AptInput.h.

#pragma once

#include "Libraries/Source/Apt/AptCharacterInst.h"

class AptButtonInst : public AptCharacterInst
{
public:
	// The state of a button is the record state mask bit that is shown (Up=1, Over=2, Down=4; Hit=8 records are for
	// hit testing): AptCIH 0x00AE1F00 stores the value at +0x18 and places every record whose mask has the bit
	// (0x00AE1F42 `test [esi+0x18], mask`).  A new button has state 0 (no records shown) until the new-instance flush
	// sets Up (0x00AE43FA `push 1; call 0x00AE1F00`).
	enum class State : std::uint8_t
	{
		None = 0,
		Up = 1,
		Over = 2,
		Down = 4
	};

	explicit AptButtonInst(Apt &apt);

	void setup(const AptCharRef &ref);
	const AptButtonInfo *info() const { return m_info; }
	State state() const { return m_state; }

	// Shows the records of `state` (replacing the children).  Reports an error for a record whose character cannot
	// be resolved.
	void setState(State state);
	void advance();
	const std::vector<AptCharacterInst *> &children() const { return m_children; }

	bool contentBounds(float &x0, float &y0, float &x1, float &y1) const override;
	void trace(AptGC &gc) override;
	void destroy(bool fireUnload) override;
	bool forInDecoded() const override { return true; }

	// Hit test in stage coordinates against the button's hit mesh (triangles) or its bounds.
	bool hitTest(float stageX, float stageY) const;

private:
	friend class AptInput;
	void rebuild();

	const AptButtonInfo *m_info = nullptr;
	State m_state = State::None;
	std::vector<AptCharacterInst *> m_children;
};
