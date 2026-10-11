// OpenBFME. GPL-3.0.
//
// InGameHeroSelect (lane UI-4): the hero bar of the Palantir. Palantir.apt loads InGameHeroSelect.swf into its clip HeroSelectUI (InitialSetup); the movie holds
// sixteen buttons Hero1 .. Hero16 and SelectAllHeroesBttn. Each button shows a portrait (a RenderImage whose `_imageMap` is "<movie path>_Hero<n>Image"), a rank
// number (the text record "APT:<movie path>_Hero<n>Rank"), a health bar and a rank bar, a selected highlight and flash / attacked / level-up effects. The local
// player's idle builders take the first button (the count of them as its number) when there are any; the heroes follow.
//
// TARGET FACTS (RotWK game.dat, caveat S-001; the BFME2 twins are tier A / B in tools/re/rw2decomp.py and the BFME2 decomp's
// GameClient/GUI/InGame/Tactical/InGameHeroSelect*.cpp and reverse/attempts/0x005264f5.cpp describe them):
// - the data (Palantir + 0xC0, ctor RW 0x92CCE4): a hero list {object id, last rank, flash frames} and a builder list {object id, used}. Object::initObject
//   (RW 0x693F16) and Object::restoreObjectToWorldInternal (RW 0x6923FB) add an object through RW 0x6D4759 -> 0x92CD78: KindOf HERO (template + 0x113 bit 2) ->
//   RW 0x92C734, else KindOf PORTER (+ 0x119 bit 7) -> RW 0x92C809. Object::tempRemoveObjectFromWorld (RW 0x692397) and ~Object (RW 0x69A876) remove it (RW
//   0x6D4771 -> 0x92C428). Either needs the object's button image (RW 0x73D0BA); a hero already listed is not added twice; while the logic frame is below 6
//   (TheGameLogic + 0x40, RW 0x92C786) a hero is inserted before the first listed hero with a greater HeroSortOrder (template + 0x648), later ones are appended;
// - the button image (RW 0x73D0BA): a CREATE_A_HERO object's Create-a-Hero record image (+ 0x140 of the record found by the object's + 0x74), else the template's
//   ButtonImage (+ 0x78, resolved by RW 0x73CFEF);
// - the interface (InGameHeroSelectInterface::Impl, ctor RW 0x92DDE7, made by AptPalantir::OnHeroSelectLoaded with the movie's clip path): the commands
//   "_level%d.<path>_OnBttnHeroSelect" and "_OnBttnSelectAllHeroes" (strings RW 0xC7EFB0 / 0xC7EF98), the faction (SetFaction with the local side), the hot keys
//   of the command buttons NonCommand_SelectNearestBuilder (RW 0xC7EE04) and NonCommand_SelectAllHeroes (RW 0xC7EE6C, added when the button first shows);
// - the update (RW 0x92CF64): until a selectable hero (RW 0x92BBEF: controlled by the local player, no status NO_HERO_PROPERTIES) or an idle builder exists,
//   nothing but the movie's Show() once one does. Then: slot 0 for the idle builders (RW 0x92BAD7: a drawable, not effectively dead, selectable, a dozer AI with no
//   task pending), its image the first one's, its number their count, rank bar 1, health 100, highlighted when one of them is selected, flashing while the
//   builder flash frames run; then every listed hero that is selectable, while slots remain: KillButtonEffects when the slot changes hero, SetButtonState(_up)
//   when the slot gets its first image, the image key "_level%d.%s_Hero%dImage" (RW 0xC7EF38), the rank record "APT:_level%d.%s_Hero%dRank" (RW 0xC7EEF0) with
//   "%d", SetButtonRankProgress(max(floor(progress * 100 + 0.5), 1), 1 at the last level), SetButtonHealthBar(max(health %, 1)),
//   SetButtonSelectedHighlightState(_show / _hide), SetButtonFlashEffectState, PlayButtonLevelUpEffect when the rank rose; the slots left over are
//   KillButtonEffects, SetButtonState(_unused) with the image key removed. The select-all button shows (_up) while any hero has a slot and _unused otherwise
//   (RW 0x92C0A2 / 0x92C1F2);
// - a button press (RW 0x92DB91, "Hero<n>"): the builder slot selects the nearest idle builder (RW 0x92D9F4); a hero in a container stands for its container
//   (hero KindOf, object + 0x27C), a horde member for its HORDE; without the additive selection (InGameUI + 0x8BA) or with an empty selection: a hero that is
//   already the only selection moves the camera to it (the double click), else the selection is replaced (MSG_CREATE_SELECTED_GROUP 1001 with true); with it, a
//   selected hero is removed (MSG_REMOVE_FROM_SELECTED_GROUP 1005), another is added (1001 with false);
// - select all (RW 0x92C8C4, the button and its hot key): every selectable hero of the slots (not effectively dead; with the additive selection, not already
//   selected), each as its horde when it is in one, selected one after the other (1002, the last one 1001), the selection replaced first unless additive;
// - the nearest builder (RW 0x92D9F4): Shift keeps the camera; with more than one object selected, or once the "used" round timed out (+ 0x1DC), every builder is
//   unused again; the idle builders sorted by their distance to the camera (RW 0x92D8FA, the squared ground distance), the first unused one that is not the
//   on-screen single selection is selected (MSG_CREATE_SELECTED_GROUP_IDLE_WORKER_VOICE 1003 with true), the camera looks at it when it is off screen, and it is
//   marked used.
// INFERENCE (stop S-2521): the port learns of the objects by walking the logic's objects after each logic frame, in id order (the creation order), not from
//   initObject / ~Object; the camera position of the sort is the view's look-at point (View vslot 0x118 was not read); the "used" round's time-out (RW 0x92BA91,
//   + 0x1DC) is not ported (the round resets only on a multiple selection or when every builder was used); the hot key action of the nearest builder keeps the
//   camera rule of a button press; the over-button handlers (RW 0x92BF34: the help box with the button's description) and the attacked effect (OnHeroAttacked)
//   and FlashHeroButton's callers are not ported; a Create-a-Hero hero shows its template's ButtonImage (its record's image is S-1405's).

#pragma once

#include "GameLogic/ObjectTypes.h"

#include <functional>
#include <list>
#include <string>
#include <vector>

class Object;
struct HudContext;

class InGameHeroSelect
{
public:
	static constexpr int kSlots = 16;

	// what the interface does to its movie: a function of the movie's clip (path relative to the Palantir level), a RenderImage key, a text record
	struct Movie
	{
		std::function<bool(const std::string &function, const std::vector<std::string> &args)> call;
		std::function<void(const std::string &key, const std::string &image)> setImage; ///< "" removes the key
		std::function<void(const std::string &record, const std::string &text)> setText;
	};

	explicit InGameHeroSelect(HudContext &ctx) : m_ctx(ctx) {}

	// ---- the data (Palantir + 0xC0) ----
	void addObject(const Object &obj);    // RW 0x92CD78
	void removeObject(const Object &obj); // RW 0x92C428
	// INFERENCE (S-2521): the logic's objects of this frame, in id order: the new ones added, the gone ones removed
	void trackObjects();
	struct Hero
	{
		ObjectID id = INVALID_ID;
		int rank = 0;        ///< the rank the last update saw
		int flashFrames = 0; ///< FlashHeroButton's countdown
	};
	struct Builder
	{
		ObjectID id = INVALID_ID;
		bool used = false;
	};
	const std::list<Hero> &heroes() const { return m_heroes; }
	const std::list<Builder> &builders() const { return m_builders; }

	// ---- the interface ----
	// AptPalantir::OnHeroSelectLoaded: the movie's clip path ("_level3.HeroSelectUI"), the Palantir's level, the local side for SetFaction
	void attach(int level, const std::string &clipPath, Movie movie, const std::string &faction);
	void detach();
	bool attached() const { return m_attached; }
	const std::string &name() const { return m_name; } ///< the clip path below the level ("HeroSelectUI")
	std::string commandPrefix() const;                 ///< "_level%d.<name>"
	void update();                                     // RW 0x92CF64
	void onButtonPressed(const std::string &param);    // RW 0x92DB91
	void selectAllHeroes();                            // RW 0x92C8C4
	void selectNearestBuilder(bool noCamera);          // RW 0x92D9F4
	bool selectAllShown() const { return m_selectAllShown; }
	bool shown() const { return m_shown; }

	// a slot as the movie was last told (tests, reports)
	struct Slot
	{
		ObjectID hero = INVALID_ID; ///< the listed hero the slot shows (INVALID_ID: none / the builder slot)
		std::string image;
		int rank = -1, rankProgress = -1, health = -1;
		bool selected = false, flash = false, builder = false;
	};
	const Slot &slot(int i) const { return m_slots[i]; }

	static bool isSelectableHero(const HudContext &ctx, const Object &obj); // RW 0x92BBEF
	static bool isReadyBuilder(const HudContext &ctx, const Object &obj);   // RW 0x92BAD7
	static std::string buttonImage(const Object &obj);                      // RW 0x73D0BA

private:
	std::vector<Builder *> readyLocalBuilders(bool readyOnly); // RW 0x92CE90 (BFME2 BuildLocalBuilderList)
	void sortByCameraDistance(std::vector<Builder *> &list);   // RW 0x92D8FA
	Builder *findReadyLocalBuilder(const std::vector<Builder *> &list);
	void resetBuilderRound(); // RW 0x92C080 (BFME2 0x525611)
	void showSelectAll();     // RW 0x92C0A2
	void hideSelectAll();     // RW 0x92C1F2
	std::string slotKey(int slot, const char *suffix) const;
	bool movieCall(const std::string &function, const std::vector<std::string> &args);

	HudContext &m_ctx;
	std::list<Hero> m_heroes;
	std::list<Builder> m_builders;
	int m_builderFlashFrames = 0;
	std::vector<ObjectID> m_known; ///< the objects trackObjects has seen, ascending

	bool m_attached = false;
	int m_level = -1;
	std::string m_name;
	Movie m_movie;
	bool m_shown = false, m_selectAllShown = false, m_builderUsed = false;
	Slot m_slots[kSlots];
};
