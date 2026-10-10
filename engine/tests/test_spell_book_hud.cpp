// OpenBFME unit tests. GPL-3.0.
// Lane SPELL-2 (review r1, S-923): the RETAIL spell book movies driven by the HUD, for every playable faction. The Palantir loads InGameSpellBook
// (OnAptInGameSpellBookLoaded "SpellBookUI", then Shown); SpellStore.apt is opened as the Palantir's store button does and runs its own script
// (OnInitialized, SetLayout, SetSpellButtonState); a click makes a science pending (_purchased), Reset undoes it, the close sends the purchase; the
// spell book then shows the power (_up, its image under InGameSpellBookSpell1Image), the reopened store shows it _already_purchased, and after a
// cast the slot is _notReady with its recharge timer. SKIPs without ROTWK_INSTALL / BFME2_INSTALL.

#include "HudTestUtil.h"

#include "Common/Player.h"
#include "Common/PlayerScience.h"
#include "Common/SpecialPower.h"
#include "Common/Team.h"
#include "GameClient/GUI/ShellServices.h"
#include "GameClient/GUI/LoadScreenInfo.h"
#include "GameClient/GameTextTableSource.h"
#include "GameClient/ControlBarCommands.h"
#include "GameClient/InGameHud.h"
#include "GameLogic/Object/Object.h"
#include "GameClient/GUI/AptScreens/AptPalantir.h"
#include "GameClient/GUI/AptScreens/AptSpellStore.h"
#include "Libraries/Source/Apt/AptButtonInst.h"
#include "Libraries/Source/Apt/AptCharacterInst.h"
#include "Libraries/Source/Apt/AptInput.h"
#include "Libraries/Source/Apt/AptRenderList.h"

#include <string>

using namespace hudtest;

namespace
{
const char *const kFactions[7] = { "FactionMen", "FactionElves", "FactionDwarves", "FactionIsengard", "FactionMordor", "FactionWild", "FactionAngmar" };
const char *const kCitadels[7] = { "MenFortressCitadel", "ElvenCitadel", "DwarvenFortressCitadel", "IsengardFortressCitadel", "MordorFortressCitadel",
	"WildFortressCitadel", "AngmarFortressCitadel" };

struct BookRig
{
	Rig rig;
	RecordingShellServices services;
	GameTextTableSource text; // review r2: the help records need the game text (data/lotr.str)
	std::unique_ptr<InGameHud> hud;
	BookRig(SharedWorld &s, const char *faction) : rig(s, "map mp fall back 4p", faction)
	{
		std::vector<std::uint8_t> bytes;
		std::string textError;
		REQUIRE_MESSAGE(s.mount->fs->readFile("data/lotr.str", bytes, &textError), textError);
		REQUIRE_MESSAGE(text.table.parse(bytes, &textError), textError);
		InGameHud::Config cfg{ *rig.game, *s.world, *s.mount->fs, services, rig.view, s.mouse, s.meta, &text };
		hud = std::make_unique<InGameHud>(cfg);
		std::string error;
		REQUIRE_MESSAGE(hud->boot(&error), error);
	}
	~BookRig() { hud.reset(); }
	void frames(int n)
	{
		for (int i = 0; i < n; ++i)
		{
			hud->update(0.033);
			rig.game->advance(0.033);
		}
	}
	std::string storeState(int i) { return AptSpellStore::stateName(hud->spellStore()->buttonState(i)); }
};
} // namespace

TEST_CASE("SPELL-2 retail: the retail InGameSpellBook and SpellStore.apt movies of every faction: purchased, available, locked, recharging, close and reopen")
{
	if (!haveWorld("spell2 book hud"))
	{
		return;
	}
	SharedWorld &s = shared();
	for (int f = 0; f < 7; ++f)
	{
		INFO(std::string(kFactions[f]));
		BookRig b(s, kFactions[f]);
		auto ctx = b.hud->enterContext();
		// the spell book's requirement (a COMMANDCENTER): the loaded game's starting base does not count here
		const ThingTemplate *cit = s.world->things().findTemplate(kCitadels[f]);
		REQUIRE(cit);
		Object *c = b.rig.logic().newObject(cit, b.rig.local->getDefaultTeam(), ObjectStatusMaskType{});
		REQUIRE(c);
		Coord3D at{ 600.0f, 600.0f, 0.0f };
		c->setPosition(&at);
		c->friend_onBuildComplete();
		b.frames(40);
		AptPalantir *p = b.hud->palantir();
		REQUIRE(p);
		CHECK(p->spellBookPath() == "SpellBookUI");
		CHECK(p->spellBookShown());
		CHECK(std::string(AptPalantir::spellStateName(p->spellSlotState(0))) == "_unused"); // nothing bought
		// the store
		std::string error;
		REQUIRE_MESSAGE(b.hud->openSpellStore(&error), error);
		b.frames(40);
		AptSpellStore *st = b.hud->spellStore();
		REQUIRE(st);
		CHECK(st->initialized());
		CHECK(st->layout() == 2); // a skirmish: _multiplayer
		int active = 0, disabled = 0, first = -1;
		for (int i = 0; i < SpellStoreModel::MAX_BUTTONS; ++i)
		{
			const std::string state = b.storeState(i);
			active += state == "_active" ? 1 : 0;
			disabled += state == "_disabled" ? 1 : 0;
			if (state == "_active" && first < 0)
			{
				first = i;
			}
		}
		MESSAGE(std::string(kFactions[f]) << ": store " << st->model().commandSetName() << ", " << active << " _active, " << disabled << " _disabled");
		CHECK(active >= 3);   // the tier-1 sciences with the rank-1 points
		CHECK(disabled >= 7); // the dearer ones are locked
		REQUIRE(first >= 0);
		const std::string *points = b.hud->windows().aptText("APT:SpellStoreSpellPoints");
		REQUIRE(points);
		CHECK(*points == "5");
		// a click: pending (_purchased for one update, then _already_purchased), the points drop; Reset undoes it
		CHECK(st->click(first));
		b.frames(1);
		CHECK(b.storeState(first) == "_purchased"); // RW 0x823214: the update after the click (the state was _active)
		b.frames(2);
		CHECK(b.storeState(first) == "_already_purchased"); // then the pending science reads as the proxy's
		CHECK(*b.hud->windows().aptText("APT:SpellStoreSpellPoints") == "0");
		st->reset();
		b.frames(3);
		CHECK(b.storeState(first) == "_active");
		CHECK(st->click(first));
		const ScienceType sci = st->model().buttons()[(size_t)first].science;
		const std::string setName = st->model().commandSetName();
		// the close sends the purchase; the logic runs it on its next frame
		b.hud->closeSpellStore();
		CHECK(b.hud->spellStore() == nullptr);
		b.frames(20);
		CHECK(b.rig.local->science().hasScience(sci));
		CHECK(std::string(AptPalantir::spellStateName(p->spellSlotState(0))) == "_up");
		const AptPalantir::NativeImage *img = p->imageFor("InGameSpellBookSpell1Image");
		REQUIRE(img);
		CHECK(!img->image.empty());
		// reopened: the bought science is _already_purchased
		REQUIRE(b.hud->openSpellStore(&error));
		b.frames(40);
		CHECK(b.storeState(first) == "_already_purchased");
		b.hud->closeSpellStore();
		// the cast through the movie's slot press; a NEED_TARGET_POS power waits for the ground click
		CHECK(p->pressSpellSlot(0));
		if (b.hud->spellBar().targeting())
		{
			b.hud->mouseButton(HudInput::Button::Left, true, 512, 500, 0, 2000);
			b.hud->mouseButton(HudInput::Button::Left, false, 512, 500, 0, 2050);
		}
		CHECK_FALSE(b.hud->spellBar().targeting());
		b.frames(20);
		CHECK(std::string(AptPalantir::spellStateName(p->spellSlotState(0))) == "_notReady");
		const float t = p->timerFor("InGameSpellBookSpell1Timer");
		CHECK(t >= 0.0f);
		CHECK(t < 1.0f);
		MESSAGE(std::string(kFactions[f]) << ": bought " << setName << " button " << first << ", cast, recharge " << t << ", movie calls "
										  << p->moviesCalls() << ", call errors " << p->callErrors().size());
		CHECK(p->callErrors().empty());
	}
}

TEST_CASE("SPELL-2 retail (review r2): the store's help records on hover (name, description, the locked-science warning); a HUD destroyed with its store open")
{
	if (!haveWorld("spell2 book hud help"))
	{
		return;
	}
	SharedWorld &s = shared();
	BookRig b(s, "FactionMen");
	auto ctx = b.hud->enterContext();
	b.frames(30);
	std::string error;
	REQUIRE_MESSAGE(b.hud->openSpellStore(&error), error);
	b.frames(40);
	AptSpellStore *st = b.hud->spellStore();
	REQUIRE(st);
	REQUIRE(st->initialized());
	auto fetch = [&](const std::string &label) {
		std::u16string out;
		REQUIRE_MESSAGE(b.text.fetch(label, out), label);
		return loadScreenU16ToUtf8(out);
	};
	int available = -1, locked = -1;
	for (int i = 0; i < SpellStoreModel::MAX_BUTTONS; ++i)
	{
		const int state = st->buttonState(i);
		if (state == 5 && available < 0)
		{
			available = i;
		}
		if (state == 1 && locked < 0)
		{
			locked = i;
		}
	}
	REQUIRE(available >= 0);
	REQUIRE(locked >= 0);
	auto buttonOf = [&](int i) -> const CommandButton * {
		for (const SpellStoreModel::Button &x : st->model().buttons())
		{
			if (x.index == i)
			{
				return x.button;
			}
		}
		return nullptr;
	};
	// a buyable science: its name and description, no warning
	st->rollOver(available);
	b.frames(2);
	const CommandButton *ab = buttonOf(available);
	REQUIRE(ab);
	const std::string *help = b.hud->windows().aptText("APT:SpellHelpText");
	const std::string *desc = b.hud->windows().aptText("APT:SpellDescription");
	REQUIRE(help);
	REQUIRE(desc);
	CHECK(!help->empty());
	CHECK(*help == fetch(ab->m_textLabel.front()));
	CHECK(*desc == fetch(ab->m_descriptLabel.front()));
	const std::string warning = fetch("TOOLTIP:ScienceDisabled");
	CHECK(desc->find(warning) == std::string::npos);
	MESSAGE("SPELL-2 help: " << *help << " / " << desc->substr(0, 60));
	// a locked one: its description plus "\n" and TOOLTIP:ScienceDisabled
	st->rollOut(available);
	st->rollOver(locked);
	b.frames(2);
	const CommandButton *lb = buttonOf(locked);
	REQUIRE(lb);
	help = b.hud->windows().aptText("APT:SpellHelpText");
	desc = b.hud->windows().aptText("APT:SpellDescription");
	CHECK(*help == fetch(lb->m_textLabel.front()));
	CHECK(*desc == fetch(lb->m_descriptLabel.front()) + "\n" + warning);
	// teardown with the store open and a purchase pending: the store goes before its window manager and sends nothing
	CHECK(st->click(available));
	b.frames(1);
	const unsigned long long scienceCount = b.rig.local->science().sciences().size();
	b.hud.reset();
	b.rig.game->advance(0.2);
	CHECK(b.rig.local->science().sciences().size() == scienceCount);
}

// ---- lane PLAY-1: the store by the player's mouse ------------------------------------------------------------------------------------------------

namespace
{
AptButtonInst *firstButtonUnder(AptCharacterInst *c)
{
	if (!c)
	{
		return nullptr;
	}
	if (AptButtonInst *b = c->asButton())
	{
		return b;
	}
	if (AptSpriteInst *sp = c->asSprite())
	{
		for (AptCharacterInst *k : sp->children())
		{
			if (AptButtonInst *b = firstButtonUnder(k))
			{
				return b;
			}
		}
	}
	return nullptr;
}

// a point the input routes to the button at `path` of `level`: the centre of its Hit records' area when that hits, else the hit point nearest it
bool hitPoint(InGameHud &hud, int level, const std::string &path, int &x, int &y)
{
	AptButtonInst *b = firstButtonUnder(hud.apt().resolvePath(hud.apt().level(level), path));
	float x0, y0, x1, y1;
	if (!b || !b->contentBounds(x0, y0, x1, y1) || !b->info())
	{
		return false;
	}
	bool any = false;
	float hx0 = 0, hy0 = 0, hx1 = 0, hy1 = 0;
	for (const AptButtonRecord &rec : b->info()->records)
	{
		if (!(rec.stateMask & 8))
		{
			continue;
		}
		const float cxs[4] = { x0, x1, x1, x0 }, cys[4] = { y0, y0, y1, y1 };
		for (int k = 0; k < 4; ++k)
		{
			const float px = rec.matrix[0] * cxs[k] + rec.matrix[2] * cys[k] + rec.translation[0];
			const float py = rec.matrix[1] * cxs[k] + rec.matrix[3] * cys[k] + rec.translation[1];
			hx0 = any ? std::min(hx0, px) : px;
			hy0 = any ? std::min(hy0, py) : py;
			hx1 = any ? std::max(hx1, px) : px;
			hy1 = any ? std::max(hy1, py) : py;
			any = true;
		}
	}
	if (any)
	{
		x0 = hx0, y0 = hy0, x1 = hx1, y1 = hy1;
	}
	float cx, cy, best = 0.0f;
	bool found = false;
	b->globalMatrix().apply((x0 + x1) / 2, (y0 + y1) / 2, cx, cy);
	for (int gy = 0; gy <= 16; ++gy)
	{
		for (int gx = 0; gx <= 16; ++gx)
		{
			float px, py;
			b->globalMatrix().apply(x0 + (x1 - x0) * (float)gx / 16.0f, y0 + (y1 - y0) * (float)gy / 16.0f, px, py);
			const float d = (px - cx) * (px - cx) + (py - cy) * (py - cy);
			if ((!found || d < best) && b->hitTest((float)(int)px, (float)(int)py) && hud.apt().input().hitTestButtons((float)(int)px, (float)(int)py) == b)
			{
				x = (int)px;
				y = (int)py;
				best = d;
				found = true;
			}
		}
	}
	return found;
}

void clickAt(BookRig &b, int x, int y)
{
	b.hud->mouseMove(x, y);
	b.frames(2);
	b.hud->mouseButton(HudInput::Button::Left, true, x, y, 0, 3000);
	b.frames(2);
	b.hud->mouseButton(HudInput::Button::Left, false, x, y, 0, 3040);
	b.frames(4);
}
} // namespace

TEST_CASE("play1 spell store by the mouse: the Palantir's button opens it, a power is bought, RESET undoes it, ACCEPT buys and closes, the locked powers are drawn grey")
{
	if (!haveWorld("play1 spell store mouse"))
	{
		return;
	}
	SharedWorld &s = shared();
	BookRig b(s, "FactionMen");
	b.hud->setWindowSize(1024, 768);
	b.rig.view.setScreen(1024, 768);
	{
		auto ctx = b.hud->enterContext();
		const ThingTemplate *cit = s.world->things().findTemplate("MenFortressCitadel");
		REQUIRE(cit);
		Object *c = b.rig.logic().newObject(cit, b.rig.local->getDefaultTeam(), ObjectStatusMaskType{});
		REQUIRE(c);
		Coord3D at{ 600.0f, 600.0f, 0.0f };
		c->setPosition(&at);
		c->friend_onBuildComplete();
	}
	b.frames(40);
	const int pal = b.hud->palantir()->level();
	int x = 0, y = 0;
	REQUIRE(hitPoint(*b.hud, pal, "PalantirButtons.Buttons.PlayerMagic", x, y));
	clickAt(b, x, y);
	b.frames(40);
	AptSpellStore *st = b.hud->spellStore();
	REQUIRE_MESSAGE(st, b.hud->spellStoreError());
	CHECK(b.hud->spellStoreRequests() == 1);
	const int lvl = st->level();
	// the locked powers are drawn grey: the movie's _disabled frame shows the RenderImageDisabled component (RW 0x8229B5 sets the states)
	{
		AptRenderList rl;
		b.hud->apt().buildRenderList(rl);
		std::map<std::string, std::string> symbolOf;
		for (const AptRenderCommand &c : rl.commands)
		{
			if (c.kind == AptRenderCommand::Kind::Placeholder && c.level == lvl && c.path.find(".Buttons.Spell") != std::string::npos)
			{
				const size_t at = c.path.find(".Buttons.Spell") + 14;
				symbolOf["Spell" + c.path.substr(at, c.path.find('.', at) - at)] = c.symbolName;
			}
		}
		for (int i = 0; i < 12; ++i)
		{
			const std::string state = b.storeState(i);
			const std::string sym = symbolOf["Spell" + std::to_string(i + 1)];
			INFO("Spell" << i + 1 << " " << state << " draws " << sym);
			AptCharacterInst *en = b.hud->apt().resolvePath(b.hud->apt().level(lvl), "SpellStore.Buttons.Spell" + std::to_string(i + 1) + ".~Enabled");
			REQUIRE(en);
			// the _disabled frame places the power's image at alpha 0.396 (dimmed); the device multiplies the clip's colour into the image (GodotInGameHud)
			if (state == "_active")
			{
				CHECK(en->globalColor().mul[3] == doctest::Approx(1.0f));
			}
			else if (state == "_disabled")
			{
				CHECK(en->globalColor().mul[3] < 0.5f);
			}
			CHECK(sym == "RenderImage");
		}
	}
	int first = -1;
	for (int i = 0; i < 12 && first < 0; ++i)
	{
		first = b.storeState(i) == "_active" ? i : -1;
	}
	REQUIRE(first >= 0);
	const std::string spellPath = "SpellStore.Buttons.Spell" + std::to_string(first + 1);
	REQUIRE(hitPoint(*b.hud, lvl, spellPath, x, y));
	clickAt(b, x, y);
	b.frames(3);
	CHECK(b.storeState(first) == "_already_purchased");
	CHECK(*b.hud->windows().aptText("APT:SpellStoreSpellPoints") == "0");
	REQUIRE(hitPoint(*b.hud, lvl, "SpellStore.Buttons.ButtonsMain.Reset", x, y));
	const size_t errs0 = b.hud->apt().vm().errors().size();
	clickAt(b, x, y);
	b.frames(30); // the generic button's callback runs when its _down animation ends
	for (size_t i = errs0; i < b.hud->apt().vm().errors().size(); ++i)
	{
		MESSAGE("vm error after Reset: " << b.hud->apt().vm().errors()[i]);
	}
	CHECK(b.hud->apt().vm().errors().size() == errs0);
	CHECK(b.storeState(first) == "_active");
	CHECK(*b.hud->windows().aptText("APT:SpellStoreSpellPoints") == "5");
	REQUIRE(hitPoint(*b.hud, lvl, spellPath, x, y));
	clickAt(b, x, y);
	b.frames(3);
	CHECK(b.storeState(first) == "_already_purchased");
	const ScienceType sci = st->model().buttons()[(size_t)first].science;
	REQUIRE(hitPoint(*b.hud, lvl, "SpellStore.Buttons.ButtonsMain.Accept", x, y));
	clickAt(b, x, y);
	b.frames(40);
	CHECK(b.hud->spellStore() == nullptr);
	CHECK(b.rig.local->science().hasScience(sci));
}
