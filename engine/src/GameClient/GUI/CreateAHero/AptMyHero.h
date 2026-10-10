// OpenBFME. GPL-3.0.
//
// AptMyHero (lane CAH-1): the hero the Create-a-Hero builder screen edits (retail GameClient\Gui\GUICallbacks\Apt\AptMyHero.cpp, RW 0xC8A7E0; a
// CreateAHeroHero subclass, constructor RW 0x9C00E7, vtable RW 0xC8A948, the screen's + 0x27C). It holds the record being built, the attribute and
// appearance slots the movie's bars and arrows drive, the spendable points, and answers the MyHero::* providers.
//
// TARGET FACTS (RotWK game.dat, caveat S-001), read from the disassembly:
//   * the constructor (RW 0x9C00E7) makes an empty hero (RW 0x80C572: class 0, subclass 0, colours -1 / 0xFF707070 / -1, flags 0x2FF), clears the text
//     records APT:MyHeroName / MyHeroClass / MyHeroType / MyHeroAttribPoints / MyHeroCost and registers the providers MyHero::BaseAttrib_<i> (the slot's
//     minimum + 1, RW 0x9BF380), MyHero::CurAttrib_<i> (the record's index + 1, RW 0x9BF3C0), MyHero::MaxAttrib_<i> (maximum + 1, RW 0x9BF406) for i < 5,
//     MyHero::NumAppearance_<i> (the slot's count, RW 0x9BF446) for i < 7, MyHero::MaxAttribute (20: RW 0x9BF29B sets RW 0xDEBF2C once), MyHero::MaxAwards
//     (TheCreateAHeroSystem + 0x1E4), MyHero::IsSystemHero ("1" for a system hero unless RW 0xDE3D8C, else "0"), MyHero::HeroBuildCost (RW 0x809CF3: the
//     record's power cost RW 0x809CA6 + the "CreateAHero" template's calcCostToBuild RW 0x73C25F with no player) and MyHero::AwardState_<i> (awards: S-1403);
//   * the slots (RW 0x9C0AEE, run whenever the class / subclass changes or a hero is loaded): the points remaining (+0x15C) and their maximum (+0x160) are
//     the subclass's SpendableAttributePoints (RW 0x619DEF); every bling binder of TheCreateAHeroSystem (in order) owns the slot UISlot of its kind
//     (ATTRIBUTE 0 / APPEARANCE 1; the vector grows to it, RW 0x9C0AB6); a slot already taken keeps its first binder. The record gets the group when it
//     lacks it (RW 0x80A73B, index 0; `added`), then its index `cur`. ATTRIBUTE: min / max / default are the indices of the subclass Attribute's
//     MinValueUpgrade / MaxValueUpgrade / DefaultValueUpgrade in the group's list (RW 0x61BC94 / 0x61BCC6 / 0x61BCF8); a group the subclass has no list
//     for gets min = ((class + 1) * (subclass + 1) * (binder + 1)) % 10 + 5, max = (3 * binder % 10 - class) + 3 + min (RW 0x9C0BB8: debug values);
//     an added group starts at min; the record is set to min (RW 0x80A6C8) and APT:MyHeroAttribute_<slot> to the binder's LabelTag. APPEARANCE: min 0,
//     max = the group's list size - 1 (RW 0x618F3A), default = the subclass's default index (RW 0x61BC63); an added group starts at the default;
//     APT:MyHeroAppearance_<slot> to the LabelTag. Then max < min -> min = max; count = max - min + 1; cur and default outside [min, max] -> min; and
//     the slot is set to cur through RW 0x9BF539; then the hero is applied (vslot 0x14) and the movie's UpdateHeroBaseAttributes is called;
//   * setting a slot (RW 0x9BF539): an index outside [min, max] is refused; ATTRIBUTE: remaining + (record's index - new) must stay in [0, maximum],
//     else refused; remaining is updated and APT:MyHeroAttribPoints = "%d"; APPEARANCE: APT:MyHeroAppearanceVal_<slot> = the game text of the new
//     bling's NameTag (RW 0x619B02); then the record's index (RW 0x80A6C8);
//   * a step (RW 0x9BFD40: Increase / DecreaseAttribute, Next / PrevAppearance with the slot number): needs a slot with a count; ATTRIBUTE: a step that
//     the points cannot pay moves toward 0 (RW 0x9BFD58: remaining - step must not pass the maximum, nor go below 0), then the new index is clamped to
//     [min, max]; APPEARANCE: the index wraps within [min, max]; then RW 0x9BF539 and the apply;
//   * the auto buttons (RW 0x9C3B7D): AppearanceDefault -> every appearance slot to its default (RW 0x9BFE2A(1)) and the subclass's default colours
//     (RW 0x80985A, flags | 0x100); AppearanceRandom -> every appearance slot to a client random value in [min, max] (RW 0x9BF69E: RW 0x6D32E4);
//     AttribReset -> every attribute slot to its minimum (RW 0x9BF6FB(0)); AttribRecommend -> every attribute slot to its minimum, then to its default
//     (RW 0x9BFE2A(0));
//   * the colours (RW 0x809745 / 0x80975A / 0x80976F: primary / secondary / tertiary, flags | 8 when changed); the name (RW 0x80A352: flags | 0x10 when
//     changed, a unique id the first time).
// INFERENCE (stop S-1401): vslot 0x14 ("apply": the text records, the movie's UpdateHeroBuildCost / cost, the preview object's upgrades RW 0x80ACE3) is
// summarised as notify(): the listener (the screen) refreshes the texts and the preview; the client random of AppearanceRandom is the device's.

#pragma once

#include "Common/CreateAHeroRecord.h"

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class CreateAHeroSystem;
class GameTextSource;
class WindowManager;

class AptMyHero
{
public:
	enum Kind
	{
		KIND_ATTRIBUTE = 0,
		KIND_APPEARANCE = 1
	};
	struct Slot ///< RW 0x14 bytes at + 0x17C + 12 * kind
	{
		std::string group; ///< +0 (a name key in retail; "" = no binder)
		int count = 0;     ///< +4
		int minimum = 0;   ///< +8
		int maximum = 0;   ///< +0xC
		int defaultIndex = 0; ///< +0x10
	};
	enum
	{
		NUM_ATTRIBUTE_BARS = 5,   ///< RW 0x9C0185: the providers' loop
		NUM_APPEARANCE_SLOTS = 7, ///< RW 0x9C0435
		MAX_ATTRIBUTE = 20        ///< RW 0x9BF2A4
	};

	// windows / text may be null (tests of the model alone): the text records and the movie calls are then skipped
	AptMyHero(const CreateAHeroSystem &system, WindowManager *windows, const GameTextSource *text);

	CreateAHeroHero &hero() { return m_hero; }
	const CreateAHeroHero &hero() const { return m_hero; }
	bool isNew() const { return m_new; }

	// RW 0x9C0E03 (with RW 0x80BC55 copying the displayed hero): edit `hero`; a new hero (`fresh`) gets its attributes at their minimum and its default
	// appearance (RW 0x9C0E89 .. : RW 0x9BF6FB(0), RW 0x9BFE2A(1)).
	void edit(const CreateAHeroHero &hero, bool fresh);
	// the class page's choice (the record's class / subclass; RW 0x9C47D3 shows the class page's hero of that pair)
	void setClass(std::uint32_t cls, std::uint32_t sub);

	void rebuild();                                   ///< RW 0x9C0AEE
	bool setSlotIndex(int kind, int slot, int index); ///< RW 0x9BF539
	void step(int kind, int slot, int delta);         ///< RW 0x9BFD40
	void resetToMinimum(int kind);                    ///< RW 0x9BF6FB
	void setDefaults(int kind);                       ///< RW 0x9BFE2A
	void randomize(int kind, const std::function<int(int, int)> &random); ///< RW 0x9BF69E
	void setDefaultColors();                          ///< RW 0x80985A
	void setPrimaryColor(std::uint32_t c);            ///< RW 0x809745
	void setSecondaryColor(std::uint32_t c);          ///< RW 0x80975A
	void setTertiaryColor(std::uint32_t c);           ///< RW 0x80976F
	void setName(const std::u16string &name);         ///< RW 0x80A352

	int remainingPoints() const { return m_remaining; } ///< + 0x15C
	int maximumPoints() const { return m_maximum; }     ///< + 0x160
	const std::vector<Slot> &slots(int kind) const { return m_slots[(size_t)(kind == KIND_APPEARANCE)]; }
	int currentIndex(int kind, int slot) const;
	// the build cost (RW 0x809CF3); -1 when the "CreateAHero" template or the command store is missing
	int buildCost() const;

	// the providers (registered by the screen); false when the name is not one of them
	bool provide(const std::string &name, std::string &value) const;
	static std::vector<std::string> providerNames();

	// called after every change that RW applies (vslot 0x14): the screen refreshes the texts / the preview
	std::function<void()> onChanged;

private:
	void notify();
	void setText(const std::string &name, const std::u16string &text);
	std::u16string label(const std::string &tag) const;

	const CreateAHeroSystem &m_system;
	WindowManager *m_windows;
	const GameTextSource *m_text;
	CreateAHeroHero m_hero;
	bool m_new = false;
	int m_remaining = 0;
	int m_maximum = 0;
	std::array<std::vector<Slot>, 2> m_slots;
};
