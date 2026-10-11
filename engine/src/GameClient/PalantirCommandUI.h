// OpenBFME. GPL-3.0.
//
// PalantirCommandUI (RotWK; no ZH counterpart), lane HUD-2: the part of the Palantir that follows the control bar's context, the movie's `CommandUI` globe UI
// (ShowCommandInterface / HideCommandInterface) and the selection's portrait in its `CommandUI.Portrait` clip.
//
// TARGET FACTS (RotWK game.dat, caveat S-001):
// - the control bar hands the Palantir the context's drawable, or NULL, at every context switch (callers RW 0x9443BD / 0x9448FC (NULL) / 0x9450B3 of
//   AptPalantir::setDrawable 0x6D4671, which forwards to PalantirCommandUI::setContext 0x930809 through 0x930C8C);
// - setContext(id) does nothing when it already has a context with the same drawable id and the same CommandSet name (0x930832 .. 0x93084A); otherwise it resets
//   (0x92F990: HideRankInterface when shown, HideCostModifierUpgradeInterface, HideCommandInterface, the portrait cleared) and records the context, even for id 0
//   (0x93085F `mov byte [esi+0x2C], 1`);
// - each update (0x93089B) calls ShowCommandInterface once a context is recorded and not yet shown (0x9308B8 -> 0x92F6AA), then picks the portrait (0x92FE33) and,
//   when the image changed, hands it to the window manager under the clip path "CommandUI/Portrait" (0x6236DE), or clears it there (0x623790);
// - the portrait (0x92FE33): with a context drawable, the drawable's portrait (0x694F06); without one, over the selected drawables (TheInGameUI 0xDE4830 vfunc
//   0x124, the selection list in its order): the first one's portrait when every selected drawable has that same portrait image; otherwise the first selected
//   object's team's controlling player's template `MultiSelectionPortrait` (Object +0x31C -> Team +0x30 -> +8, PlayerTemplate +0x1D8 through 0x5FC966,
//   field table RW 0xBF84F8), and when that names no image the mapped image "MultiPortrait" (0x92FEEC);
// - a drawable's portrait in the common case is its template's `SelectPortrait` (template +0x74, resolved by TheMappedImageCollection at 0x73CF31, a name
//   without an image logs "is looking for Portrait ... but can't find it" and yields no image).
// NOT PORTED (stop S-760): the two other branches of 0x694F06 (a draw module's template override, 0x694BF8 -> vfunc 0x54; a template with the kind-of bit at
// +0x11F & 0x40 checked against the local player through 0x68FBD3 / 0x6ADBEB), the rank and the cost modifier interfaces (ShowRankInterface,
// SetRankProgressBar, ShowCostModifierUpgradeInterface), and a context drawable without an object (the null-object branch 0x92FE53).
// INFERENCE: image names stand for the Image pointers retail compares (two names that resolve to no image compare unequal here and equal there; a name
// without an image draws nothing and is reported by the device).

#pragma once

#include "GameClient/HudContext.h"

#include <string>
#include <vector>

namespace PalantirCommandUI
{
// the portrait image name of a selection ("" none). `contextObject`: the control bar's context object (INVALID_ID: none, the selection decides).
std::string portraitFor(const HudContext &ctx, ObjectID contextObject, const std::vector<ObjectID> &selected);
// the portrait of one object (its template's SelectPortrait, "" none)
std::string objectPortrait(const Object &obj);

// lane HUD-5: the rank interface of the selection (RW 0x92F778 per object, RW 0x9305CE over the selection): type 0 the rank (APT:HeroRank = APT:RankLabel with
// `rank`, the bar = the progress into the next level, -1 at the last level), type 1 a time left (LifetimeUpdate: the bar = the part of the lifetime left), 2 none.
// TARGET FACTS (RotWK game.dat, caveat S-001):
//   * RW 0x92F778(object): a LifetimeUpdate not waiting for its wake-up (module + 0x28 clear, RW 0x7A7E79) is type 1 from its start (+0x24) and death frames (+0x20): when death > start,
//     progress = (death - now) / (death - start), 0 once past death, at most 1; else none. (A TEMPORARILY_DEFECTED object's TemporarilyDefectUpdate and a status 16
//     object's ToggleHiddenSpecialAbilityUpdate are type 1 the same way: not ported, S-1955.) Otherwise the rank: none unless the template IsTrainable (+0x5F7;
//     a disguised enemy's shown template, RW 0x69194E, and its rank 1: not ported) and RW 0x9D2437 answers;
//   * RW 0x9D2437 (RotWK only): the tracker's current level (+0x26C -> RW 0x79D12A) must exist; rank = its rank (RW 0x688EEC); with a next level (RW 0x6891A4)
//     and the experience below the last level's (RW 0x6893E2), progress = (experience - required(current)) / (required(next) - required(current)) in [0, 1] when
//     required(next) > required(current), else -1; -1 without a next level. Nothing for rank <= 1 with progress < 0;
//   * RW 0x9305CE: the context object's, else the selected objects' (TheInGameUI vslot 0x124, in order): the first one's, none when another differs (RW 0x92F5D5).
struct RankInfo
{
	int type = 2;
	int rank = 0;
	float progress = 0.0f;
	bool operator==(const RankInfo &o) const { return type == o.type && rank == o.rank && progress == o.progress; }
};
RankInfo rankInfo(const HudContext &ctx, const Object &obj);
// lane UI-4: RW 0x9D2437 on its own (the hero bar's rank and rank bar, InGameHeroSelect): false when the object has no rank; `progress` -1 at the last level
bool objectRankAndProgress(const Object &obj, int &rank, float &progress);
RankInfo rankInfoFor(const HudContext &ctx, ObjectID contextObject, const std::vector<ObjectID> &selected);

std::vector<std::string> acceptanceStops();
} // namespace PalantirCommandUI
