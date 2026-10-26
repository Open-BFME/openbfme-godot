// OpenBFME unit tests. GPL-3.0.
// Lane SPELL-4: the aiming marker of a spell book power (GameClient/RadiusCursor.h). The owner, playing v0.4.0-alpha.3 as Men of the West: "it's just a
// circle" (the port drew a green ring of its own). RotWK draws the InGameUI.ini RadiusCursorTemplate the button's RadiusCursorType names, 2 * the power's
// RadiusCursorRadius wide, throbbing between OpacityMin and OpacityMax, white where the power may be cast and red (0xFF1414) where not, with no pointer;
// a click where it may not be cast issues nothing. SKIPs without ROTWK_INSTALL / BFME2_INSTALL.

#include "HudTestUtil.h"

#include "Common/Player.h"
#include "Common/PlayerScience.h"
#include "Common/Science.h"
#include "Common/SpecialPower.h"
#include "Common/Team.h"
#include "GameClient/ControlBarCommands.h"
#include "GameClient/GUI/AptScreens/AptPalantir.h"
#include "GameClient/GUI/ShellServices.h"
#include "GameClient/InGameHud.h"
#include "GameClient/RadiusCursor.h"
#include "GameLogic/ActionManager.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/AI/AIPathfind.h"
#include "GameLogic/Module/SpecialPowerModules.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/ExperienceTracker.h"
#include "GameLogic/System/ShroudManager.h"

#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace hudtest;

namespace
{
struct CursorRig
{
	Rig rig;
	RecordingShellServices services;
	std::unique_ptr<InGameHud> hud;
	explicit CursorRig(SharedWorld &s, const char *faction = "FactionMen") : rig(s, "map mp fall back 4p", faction)
	{
		InGameHud::Config cfg{ *rig.game, *s.world, *s.mount->fs, services, rig.view, s.mouse, s.meta, nullptr };
		hud = std::make_unique<InGameHud>(cfg);
		std::string error;
		REQUIRE_MESSAGE(hud->boot(&error), error);
	}
	~CursorRig() { hud.reset(); }
	void frames(int n)
	{
		for (int i = 0; i < n; ++i)
		{
			hud->update(0.034);
			rig.game->advance(0.034);
		}
	}
};

const InGameSpellBookModel::Button *buttonOf(const InGameHud &hud, const std::string &command)
{
	for (const InGameSpellBookModel::Button &b : hud.spellBar().buttons())
	{
		if (b.button && b.button->m_name == command)
		{
			return &b;
		}
	}
	return nullptr;
}

size_t castsQueued(LiveGame &game)
{
	size_t n = 0;
	for (const GameMessage &m : game.commands().messages())
	{
		n += m.getType() == MSG_DO_SPECIAL_POWER_AT_LOCATION ? 1 : 0;
	}
	return n;
}

} // namespace

TEST_CASE("SPELL-4 retail: a selected hero's Tornado previews its white area texture and clears on cancel, cast and disabled input")
{
	if (!haveWorld("spell4 hero cursor")) return;
	SharedWorld &s = shared();
	CursorRig r(s);
	auto context = r.hud->enterContext();
	GameLogic &logic = r.rig.logic();
	Object *hero = r.rig.make("ElvenElrond", 800.0f, 650.0f);
	REQUIRE(hero);
	hero->getExperienceTracker()->gainExpForLevel(10, true, false);
	r.hud->input().ui().selectObject(hero->getID());
	r.rig.game->shroud().revealMapForPlayerPermanently(r.rig.local->getPlayerIndex());
	r.frames(20);
	ControlBarButton tornado;
	for (const auto *buttons : { &r.hud->controlBar().palantirButtons(), &r.hud->controlBar().sideButtons() })
		for (const auto &button : *buttons)
			if (button.button && button.button->m_name == "Command_SpecialPowerSpawnWhirlwind") tornado = button;
	REQUIRE(tornado.button);
	REQUIRE(tornado.state == ButtonState::Enabled);
	const Coord3D target{ 800.0f, 700.0f, logic.getGroundHeight(800.0f, 700.0f) };
	r.rig.lookAt(target);
	const ICoord2D pixel = r.rig.screenOf(target);
	r.hud->mouseMove(pixel.x, pixel.y, 0);
	CHECK_FALSE(r.hud->radiusCursor().active());
	REQUIRE(r.hud->controlBar().pressButton(tornado.slot, tornado.inPalantir));
	CHECK_FALSE(r.hud->spellBar().targeting()); // hero targeting uses InGameUI's pending command
	r.hud->update(0.034);
	const auto &decal = r.hud->radiusCursor().decal();
	CHECK(r.hud->radiusCursor().type() == "NaturesWrathRadiusCursor");
	CHECK(decal.visible);
	CHECK(decal.texture == "SCCNaturesWrath");
	CHECK(decal.size == 140.0f); // SpecialPowerSpawnTornado RadiusCursorRadius = 70
	CHECK(decal.valid);
	CHECK(decal.rgb == 0xFFFFFFu);
	CHECK(r.hud->input().ui().cursor() == "None");
	float loX = 0, loY = 0, hiX = 0, hiY = 0;
	logic.aiWorld()->pathfinder().terrainView()->getMaximumPathfindExtent(loX, loY, hiX, hiY);
	const Coord3D edge{ loX + 35.0f, target.y, logic.getGroundHeight(loX + 35.0f, target.y) };
	r.rig.lookAt(edge);
	const auto edgePixel = r.rig.screenOf(edge);
	r.hud->mouseMove(edgePixel.x, edgePixel.y, 0);
	r.hud->update(0.034);
	CHECK_FALSE(decal.valid);
	CHECK(decal.rgb == 0xFF1414u);
	r.hud->input().ui().setInputEnabled(false);
	CHECK_FALSE(r.hud->radiusCursor().active());
	r.hud->input().ui().setInputEnabled(true);
	r.rig.lookAt(target);
	r.hud->mouseMove(pixel.x, pixel.y, 0);
	r.hud->update(0.034);
	CHECK(decal.visible);
	CHECK(decal.rgb == 0xFFFFFFu);
	r.hud->mouseButton(HudInput::Button::Right, true, pixel.x, pixel.y, 0, 1000);
	r.hud->mouseButton(HudInput::Button::Right, false, pixel.x, pixel.y, 0, 1050);
	r.hud->update(0.034);
	CHECK_FALSE(r.hud->radiusCursor().active());
	CHECK(r.hud->input().ui().cursor() == "Arrow");
	REQUIRE(r.hud->controlBar().pressButton(tornado.slot, tornado.inPalantir));
	r.hud->update(0.034);
	CHECK(decal.visible);
	const auto queued = castsQueued(*r.rig.game);
	r.hud->mouseButton(HudInput::Button::Left, true, pixel.x, pixel.y, 0, 1100);
	r.hud->mouseButton(HudInput::Button::Left, false, pixel.x, pixel.y, 0, 1150);
	r.hud->update(0.034);
	CHECK(castsQueued(*r.rig.game) == queued + 1);
	CHECK_FALSE(r.hud->radiusCursor().active());
	CHECK(r.hud->input().ui().cursor() == "Arrow");
}

TEST_CASE("SPELL-4: RadiusCursor follows RW 0x6A077A / 0x732DE7 / 0x732926 / 0x73232B (template, size, player colour, owner rule, throb, hint colours)")
{
	RadiusCursorTemplates store;
	std::string error;
	const std::string ini = "InGameUI\n"
							"  MaxSelectionSize = 32\n"
							"  RadiusCursorTemplate = HealRadiusCursor\n"
							"    Texture = SCCHeal\n"
							"    Style = SHADOW_ALPHA_DECAL\n"
							"    OpacityMin = 20%\n"
							"    OpacityMax = 60%\n"
							"    OpacityThrobTime = 1000\n"
							"    Color = R:255 G:255 B:255 A:255\n"
							"    OnlyVisibleToOwningPlayer = Yes\n"
							"  End\n"
							"  RadiusCursorTemplate = Plain\n"
							"    Texture = SCCPlain\n"
							"  End\n"
							"  RadiusCursorTemplate = Additive\n"
							"    Texture = SCCAdd\n"
							"    Style = SHADOW_ADDITIVE_DECAL\n"
							"    OnlyVisibleToOwningPlayer = No\n"
							"  End\n"
							"End\n";
	REQUIRE_MESSAGE(store.scan(ini, &error), error);
	CHECK(store.size() == 3u);
	const RadiusDecalTemplate *heal = store.find("healradiuscursor"); // NameKeys ignore case
	REQUIRE(heal);
	CHECK(heal->texture == "SCCHeal");
	CHECK(heal->style == 0x20u);
	CHECK(heal->opacityMin == 0x1.999998p-3f); // 20% (INI::scanPercentToReal: one ulp under 0.2f)
	CHECK(heal->opacityMax == 0x1.333332p-1f); // 60%
	CHECK(heal->color == 0xFFFFFFFFu);
	// the constructor's defaults (RW 0x732888)
	const RadiusDecalTemplate *plain = store.find("Plain");
	REQUIRE(plain);
	CHECK(plain->style == 0x20u);
	CHECK(plain->opacityMin == 1.0f);
	CHECK(plain->opacityMax == 1.0f);
	CHECK(plain->opacityThrobTime == 1000.0f);
	CHECK(plain->color == 0u);
	CHECK(plain->onlyVisibleToOwningPlayer);
	CHECK(store.find("Unknown") == nullptr);

	// createRadiusDecal: 2 * radius, the template's colour (then the hint's), the player colour when the template has none
	RadiusCursor c;
	c.set("HealRadiusCursor", heal, 150.0f, true, true, 0x123456u, true);
	REQUIRE(c.active());
	CHECK(c.decal().visible);
	CHECK(c.decal().texture == "SCCHeal");
	CHECK(c.decal().size == 300.0f);
	CHECK(c.decal().rgb == 0xFFFFFFu);
	// the throb: n = ceil(1000 * 0.03) = 30 client frames; frame 0: p = 0.5 -> (0.4 * 0.5 + 0.2) * 255 = 101.99999 with the parsed percents: 101 (truncated)
	c.update(Coord3D{ 10.0f, 20.0f, 30.0f }, 0, true);
	CHECK(c.decal().opacity == 101);
	CHECK(c.decal().packed == ((101u << 24) | 0xFFFFFFu));
	CHECK(c.decal().position.x == 10.0f);
	c.update(Coord3D{ 10.0f, 20.0f, 30.0f }, 7, true);
	CHECK(c.decal().opacity == RadiusCursor::throbOpacity(*heal, 7, true));
	CHECK(c.decal().opacity == 152);
	c.update(Coord3D{ 10.0f, 20.0f, 30.0f }, 30, true);
	CHECK(c.decal().opacity == 101);
	c.update(Coord3D{ 10.0f, 20.0f, 30.0f }, 7, false); // the UI icons hidden
	CHECK(c.decal().opacity == 0);
	// the hint: invalid 0xFF1414, the same type only recolours
	c.set("HealRadiusCursor", heal, 150.0f, true, true, 0x123456u, false);
	CHECK(c.decal().rgb == 0xFF1414u);
	CHECK_FALSE(c.decal().valid);
	CHECK(c.decal().packed == 0xFF1414u); // opacity 0 from the last update
	c.set("HealRadiusCursor", heal, 150.0f, true, true, 0x123456u, true);
	CHECK(c.decal().rgb == 0xFFFFFFu);
	// another type replaces it; a template with Color 0 takes the player's colour until the first hint recolours it (the hint comes with every set)
	c.set("Plain", plain, 40.0f, true, true, 0x123456u, true);
	CHECK(c.decal().texture == "SCCPlain");
	CHECK(c.decal().size == 80.0f);
	c.update(Coord3D{ 0.0f, 0.0f, 0.0f }, 3, true);
	CHECK(c.decal().opacity == 255); // OpacityMin = OpacityMax = 1.0
	// another player's cursor of an owner-only template exists (the type is current) but draws nothing
	c.set("HealRadiusCursor", heal, 150.0f, true, false, 0u, true);
	CHECK(c.active());
	CHECK_FALSE(c.decal().visible);
	// no template / no radius / no player: nothing
	c.set("Unknown", nullptr, 150.0f, true, true, 0u, true);
	CHECK_FALSE(c.active());
	c.set("Plain", plain, 0.0f, true, true, 0u, true);
	CHECK_FALSE(c.active());
	c.set("Plain", plain, 10.0f, false, true, 0u, true);
	CHECK_FALSE(c.active());
	// an additive style scales the RGB by the opacity (Shadow::setColor's 0x10840 branch)
	const RadiusDecalTemplate *add = store.find("Additive");
	REQUIRE(add);
	CHECK(add->style == 0x40u);
	CHECK(RadiusCursor::packedColor(add->style, 0xFF1414u, 128) == 0x800A0Au);
	CHECK(RadiusCursor::packedColor(0x20u, 0xFF1414u, 128) == 0x80FF1414u);
	// a block without its End is an error, not a default
	CHECK_FALSE(store.scan("InGameUI\n  RadiusCursorTemplate = Broken\n    Texture = X\n", &error));
	CHECK(!error.empty());
	CHECK_FALSE(store.scan("; nothing\n", &error));
	MESSAGE("no InGameUI: " << error);
	CHECK(error.find("no InGameUI block") != std::string::npos);
}

TEST_CASE("SPELL-4 retail: InGameUI.ini's radius cursors: every spell book power that wants a position names one with a texture")
{
	if (!haveWorld("spell4 cursor templates"))
	{
		return;
	}
	SharedWorld &s = shared();
	RadiusCursorTemplates store;
	std::string error;
	REQUIRE_MESSAGE(store.load(*s.mount->fs, &error), error);
	CHECK(store.size() == 82u);
	const RadiusDecalTemplate *heal = store.find("HealRadiusCursor");
	REQUIRE(heal);
	CHECK(heal->texture == "SCCHeal");
	CHECK(heal->style == 0x20u);
	CHECK(heal->opacityMin == 0x1.999998p-3f);
	CHECK(heal->opacityMax == 0x1.333332p-1f);
	CHECK(heal->opacityThrobTime == 1000.0f);
	const RadiusDecalTemplate *rally = store.find("RallyRadiusCursor");
	REQUIRE(rally);
	CHECK(rally->texture == "SCCRally");
	CHECK(rally->opacityThrobTime == 1500.0f);
	CHECK(rally->color == 0x80FFFFFFu);
	const RadiusDecalTemplate *rebuild = store.find("RebuildRadiusCursor");
	REQUIRE(rebuild);
	CHECK(rebuild->texture == "SCCRebuild");
	// every SPELL_BOOK button of the seven spell books that wants a position: its RadiusCursorType's template and texture
	std::vector<std::string> missing;
	int targeted = 0;
	for (const char *set : { "MenSpellBookCommandSet", "ElvesSpellBookCommandSet", "DwarvesSpellBookCommandSet", "IsengardSpellBookCommandSet",
			 "MordorSpellBookCommandSet", "WildSpellBookCommandSet", "AngmarSpellBookCommandSet" })
	{
		const CommandSet *cs = s.world->commands().findCommandSet(set);
		REQUIRE_MESSAGE(cs, set);
		for (int i = 0; i < CommandSet::MAX_BUTTONS; ++i)
		{
			const CommandButton *b = cs->getCommandButton(i);
			if (!b || !(b->m_options & COMMAND_OPTION_NEED_TARGET_POS))
			{
				continue;
			}
			++targeted;
			const RadiusDecalTemplate *t = b->m_radiusCursor.empty() ? nullptr : store.find(b->m_radiusCursor);
			if (!t || t->texture.empty())
			{
				missing.push_back(std::string(set) + ":" + b->m_name + "(" + b->m_radiusCursor + ")");
			}
		}
	}
	MESSAGE("targeted spell book buttons " << targeted << ", without a cursor: " << missing.size());
	CHECK(targeted == 75);
	CHECK(missing.empty());
	for (const std::string &m : missing)
	{
		MESSAGE(m);
	}
}

TEST_CASE("SPELL-4 retail: Men of the West: each targeted spell book power aims with its own radius cursor, refuses an invalid spot, casts on a valid one")
{
	if (!haveWorld("spell4 men cursor"))
	{
		return;
	}
	SharedWorld &s = shared();
	CursorRig r(s);
	auto ctx = r.hud->enterContext();
	GameLogic &logic = r.rig.logic();
	Player &men = *r.rig.local;
	// the spell book's requirement (a COMMANDCENTER), as SPELL-2's HUD test
	const ThingTemplate *cit = s.world->things().findTemplate("MenFortressCitadel");
	REQUIRE(cit);
	Object *citadel = logic.newObject(cit, men.getDefaultTeam(), ObjectStatusMaskType{});
	REQUIRE(citadel);
	const Coord3D home{ 600.0f, 600.0f, 0.0f };
	citadel->setPosition(&home);
	citadel->friend_onBuildComplete();
	// every Men science of the book
	for (const char *sci : { "SCIENCE_Heal", "SCIENCE_RallyingCallMP", "SCIENCE_Rebuild", "SCIENCE_HobbitAllies", "SCIENCE_ArrowVolleyGood", "SCIENCE_TomBombadil",
			 "SCIENCE_SpawnLoneTower", "SCIENCE_DunedainAllies", "SCIENCE_CloudBreak", "SCIENCE_RohanAllies", "SCIENCE_ArmyoftheDead", "SCIENCE_Earthquake" })
	{
		const ScienceType st = TheScienceStore->getScienceFromInternalName(sci);
		REQUIRE_MESSAGE(st != SCIENCE_INVALID, sci);
		men.science().grantScience(st);
	}
	// the location tests of some powers want the spot CLEAR of shroud for the player (RW 0x82E913's final test): the whole map is revealed
	r.rig.game->shroud().revealMapForPlayerPermanently(men.getPlayerIndex());
	r.frames(20);
	RadiusCursorTemplates templates;
	std::string error;
	REQUIRE_MESSAGE(templates.load(*s.mount->fs, &error), error);
	// the invalid spot: inside the terrain but closer to the pathfinding extent's edge than the power's radius (RW 0x82D56A)
	float loX = 0, loY = 0, hiX = 0, hiY = 0;
	REQUIRE(logic.aiWorld());
	REQUIRE(logic.aiWorld()->pathfinder().terrainView());
	logic.aiWorld()->pathfinder().terrainView()->getMaximumPathfindExtent(loX, loY, hiX, hiY);
	const Coord3D target{ 800.0f, 700.0f, logic.getGroundHeight(800.0f, 700.0f) };
	struct Expect
	{
		const char *command, *cursor;
	};
	const Expect cases[] = {
		{ "Command_SpellBookHeal", "HealRadiusCursor" }, { "Command_SpellBookRallyingCall", "RallyRadiusCursor" },
		{ "Command_SpellBookRebuild", "RebuildRadiusCursor" }, { "Command_SpellBookTomBombadil", "TomBombadilRadiusCursor" },
		{ "Command_SpellBookHobbitAllies", "ElvenAlliesRadiusCursor" }, { "Command_SpellBookLoneTower", "LoneTowerRadiusCursor" },
		{ "Command_SpellBookArrowVolleyGood", "ArrowStormRadiusCursor" }, { "Command_SpellBookRohanAllies", "RohanAlliesRadiusCursor" },
		{ "Command_SpellBookDunedainAllies", "DunedainAlliesRadiusCursor" }, { "Command_SpellBookArmyoftheDead", "ArmyOfTheDeadRadiusCursor" },
		{ "Command_SpellBookEarthquake", "EarthquakeRadiusCursor" },
	};
	for (const Expect &e : cases)
	{
		INFO(std::string(e.command));
		const InGameSpellBookModel::Button *b = buttonOf(*r.hud, e.command);
		REQUIRE(b);
		REQUIRE(b->power);
		CHECK(b->button->m_radiusCursor == e.cursor);
		const RadiusDecalTemplate *t = templates.find(e.cursor);
		REQUIRE(t);
		const float radius = b->power->getRadiusCursorRadius();
		REQUIRE(r.hud->pressSpell(b->index));
		REQUIRE(r.hud->spellBar().targeting());
		// aim at a valid spot: the button's template, 2 * RadiusCursorRadius, white, the throb of this client frame, no pointer
		r.rig.lookAt(target);
		const ICoord2D px = r.rig.screenOf(target);
		r.hud->mouseMove(px.x, px.y, 0);
		r.hud->update(0.034);
		const RadiusCursorDecal &d = r.hud->radiusCursor().decal();
		CHECK(r.hud->radiusCursor().type() == e.cursor);
		CHECK(d.visible);
		CHECK(d.texture == t->texture);
		CHECK(d.size == 2.0f * radius);
		CHECK(d.valid);
		CHECK(d.rgb == 0xFFFFFFu);
		CHECK(r.hud->input().ui().cursor() == "None");
		CHECK(d.position.x == doctest::Approx(target.x).epsilon(0.01)); // tolerance: the pixel grid of the 1024 x 768 test view
		CHECK(d.position.y == doctest::Approx(target.y).epsilon(0.01)); // tolerance: as above
		// aim beside the map's pathfinding edge: red, and a click there issues nothing (the targeting stays)
		const Coord3D edge{ loX + radius * 0.5f, target.y, logic.getGroundHeight(loX + radius * 0.5f, target.y) };
		r.rig.lookAt(edge);
		const ICoord2D epx = r.rig.screenOf(edge);
		r.hud->mouseMove(epx.x, epx.y, 0);
		r.hud->update(0.034);
		CHECK_FALSE(r.hud->radiusCursor().decal().valid);
		CHECK(r.hud->radiusCursor().decal().rgb == 0xFF1414u);
		const size_t queued = castsQueued(*r.rig.game);
		r.hud->mouseButton(HudInput::Button::Left, true, epx.x, epx.y, 0, 1000);
		r.hud->mouseButton(HudInput::Button::Left, false, epx.x, epx.y, 0, 1050);
		CHECK(castsQueued(*r.rig.game) == queued);
		CHECK(r.hud->spellBar().targeting());
		// back to the valid spot: the click casts there, the cursor goes and the pointer comes back
		r.rig.lookAt(target);
		r.hud->mouseMove(px.x, px.y, 0);
		r.hud->update(0.034);
		CHECK(r.hud->radiusCursor().decal().valid);
		r.hud->mouseButton(HudInput::Button::Left, true, px.x, px.y, 0, 1100);
		r.hud->mouseButton(HudInput::Button::Left, false, px.x, px.y, 0, 1150);
		CHECK(castsQueued(*r.rig.game) == queued + 1);
		CHECK_FALSE(r.hud->spellBar().targeting());
		r.frames(12); // the message runs in the next logic frame (5 a second)
		CHECK_FALSE(r.hud->radiusCursor().active());
		CHECK(r.hud->input().ui().cursor() == "Arrow");
		const InGameSpellBookModel::Button *after = buttonOf(*r.hud, e.command);
		REQUIRE(after);
		CHECK_FALSE(after->ready); // the cast took its recharge
		// lane SUMMONLOC-1: what the cast left at the spot (the Lone Tower, the summoned allies) goes, or the next summon's spot test (RW 0x82E586) would
		// refuse the spot under it, as retail does
		r.frames(25);
		for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
		{
			const float dx = o->getPosition()->x - target.x, dy = o->getPosition()->y - target.y;
			if (o != citadel && dx * dx + dy * dy < 300.0f * 300.0f)
			{
				logic.destroyObject(o);
			}
		}
		r.frames(2);
	}
	// the powers whose location test is not ported (S-1874) were let through and reported, once each (S-4641)
	std::vector<std::string> unported;
	int s4640 = 0;
	for (const std::string &l : r.hud->stops())
	{
		s4640 += l.rfind("[S-4640] radius cursor (lane SPELL-4)", 0) == 0 ? 1 : 0; // review r1 #3: the runtime report names S-4640, once
		if (l.rfind("[S-4641]", 0) == 0)
		{
			unported.push_back(l.substr(30, l.find(" is not ported") - 30));
		}
	}
	CHECK(s4640 == 1);
	// (lane SUMMONLOC-1: the summons' spot test runs now: Tom Bombadil and the Hobbit, Rohan and Dunedain allies are tested, not reported)
	CHECK(unported == std::vector<std::string>{ "SpellBookRebuild", "SpellBookSpawnLoneTower" });
}

// lane SUMMONLOC-1: the summons' spot test (RW 0x82E586, the first scan the partition's geometry overlap RW 0xA3C7D0 -> RW 0xAD2CE0): no live
// IMMOBILE / STRUCTURE object (neither BASE_SITE nor CRATE) may overlap the RadiusCursorRadius circle. A structure is not in the powers'
// NO_SUMMON_NEAR_OBJECT_FILTER, so only the spot test refuses the spot on a citadel.
TEST_CASE("SUMMONLOC-1 retail: Rohan allies, the Balrog and Summon Orcs (General Summon) accept a clear spot and refuse one on a structure (RW 0x82E586)")
{
	if (!haveWorld("summonloc1 spell book"))
	{
		return;
	}
	SharedWorld &s = shared();
	struct Case
	{
		const char *faction, *citadel, *science, *power, *command;
		int type;
	};
	const Case cases[] = {
		{ "FactionMen", "MenFortressCitadel", "SCIENCE_RohanAllies", "SpellBookRohanAllies", "Command_SpellBookRohanAllies", 0x5D },
		{ "FactionMordor", "MordorFortressCitadel", "SCIENCE_BalrogAlly", "SpellBookBalrogAlly", "Command_SpellBookBalrogAlly", 0x7C },
		{ "FactionAngmar", "AngmarFortressCitadel", "SCIENCE_SummonOrcs", "SpellBookSummonOrcs", "Command_SpellBookSummonOrcs", 0x9A },
	};
	for (const Case &c : cases)
	{
		INFO(std::string(c.power));
		CursorRig r(s, c.faction);
		auto ctx = r.hud->enterContext();
		GameLogic &logic = r.rig.logic();
		Player &me = *r.rig.local;
		const ThingTemplate *cit = s.world->things().findTemplate(c.citadel);
		REQUIRE(cit);
		Object *citadel = logic.newObject(cit, me.getDefaultTeam(), ObjectStatusMaskType{});
		REQUIRE(citadel);
		const Coord3D home{ 600.0f, 600.0f, logic.getGroundHeight(600.0f, 600.0f) };
		citadel->setPosition(&home);
		citadel->friend_onBuildComplete();
		const ScienceType st = TheScienceStore->getScienceFromInternalName(c.science);
		REQUIRE(st != SCIENCE_INVALID);
		me.science().grantScience(st);
		r.rig.game->shroud().revealMapForPlayerPermanently(me.getPlayerIndex());
		r.frames(20);
		const SpecialPowerTemplate *power = TheSpecialPowerStore->findSpecialPowerTemplate(c.power);
		REQUIRE(power);
		CHECK(power->getSpecialPowerType() == c.type);
		Object *book = SpecialPowerModules::getSpellBookObject(logic, me);
		REQUIRE(book);
		const Coord3D clear{ 800.0f, 700.0f, logic.getGroundHeight(800.0f, 700.0f) };
		bool unported = true;
		CHECK(ActionManager::canDoSpecialPowerAtLocation(*book, clear, power, 0, &unported));
		CHECK_FALSE(unported);
		unported = true;
		CHECK_FALSE(ActionManager::canDoSpecialPowerAtLocation(*book, home, power, 0, &unported));
		CHECK_FALSE(unported); // refused by the ported spot test, not counted as unported
		// the cursor shows the same: white on the clear spot, red on the citadel, and a click there issues nothing
		const InGameSpellBookModel::Button *b = buttonOf(*r.hud, c.command);
		REQUIRE(b);
		REQUIRE(r.hud->pressSpell(b->index));
		CHECK(r.hud->spellBar().targetVerdict(logic, clear) == InGameSpellBookModel::Target::VALID);
		CHECK(r.hud->spellBar().targetVerdict(logic, home) == InGameSpellBookModel::Target::INVALID);
	}
}

// lane SUMMONLOC-1: a hero's summon clicked onto a structure: RotWK's CommandTranslator case 0x18 (RW 0x81E9C5 -> RW 0x6A4260 -> RW 0x82DFB7) issues
// nothing there and keeps the command; on a clear spot it issues MSG_DO_SPECIAL_POWER_AT_LOCATION
TEST_CASE("SUMMONLOC-1 retail: Dain's Royal Guard (SPELL_BOOK_MEN_OF_DALE_ALLIES) clicked onto a structure issues nothing, on a clear spot it casts")
{
	if (!haveWorld("summonloc1 hero click"))
	{
		return;
	}
	SharedWorld &s = shared();
	CursorRig r(s, "FactionDwarves");
	auto context = r.hud->enterContext();
	GameLogic &logic = r.rig.logic();
	Player &me = *r.rig.local;
	Object *dain = r.rig.make("DwarvenDain", 800.0f, 650.0f);
	REQUIRE(dain);
	dain->getExperienceTracker()->gainExpForLevel(10, true, false);
	const ThingTemplate *cit = s.world->things().findTemplate("DwarvenFortressCitadel");
	REQUIRE(cit);
	Object *citadel = logic.newObject(cit, me.getDefaultTeam(), ObjectStatusMaskType{});
	REQUIRE(citadel);
	const Coord3D blocked{ 920.0f, 650.0f, logic.getGroundHeight(920.0f, 650.0f) }; // 120 from Dain: inside MaxCastRange 150
	citadel->setPosition(&blocked);
	citadel->friend_onBuildComplete();
	r.hud->input().ui().selectObject(dain->getID());
	r.rig.game->shroud().revealMapForPlayerPermanently(me.getPlayerIndex());
	r.frames(20);
	ControlBarButton guard;
	for (const auto *buttons : { &r.hud->controlBar().palantirButtons(), &r.hud->controlBar().sideButtons() })
		for (const auto &button : *buttons)
			if (button.button && button.button->m_name == "Command_SpecialAbilityDainSummonRoyalGuard") guard = button;
	REQUIRE(guard.button);
	REQUIRE(guard.state == ButtonState::Enabled);
	const SpecialPowerTemplate *power = TheSpecialPowerStore->findSpecialPowerTemplate("SpecialAbilityDainSummonRoyalGuard");
	REQUIRE(power);
	CHECK(power->getSpecialPowerType() == 0x5B);
	const Coord3D clear{ 680.0f, 650.0f, logic.getGroundHeight(680.0f, 650.0f) }; // 120 from Dain, the other side
	CHECK(ActionManager::canDoSpecialPowerAtLocation(*dain, clear, power, guard.button->m_options));
	CHECK_FALSE(ActionManager::canDoSpecialPowerAtLocation(*dain, blocked, power, guard.button->m_options));
	REQUIRE(r.hud->controlBar().pressButton(guard.slot, guard.inPalantir));
	r.hud->update(0.034);
	const size_t queued = castsQueued(*r.rig.game);
	r.rig.lookAt(blocked);
	const ICoord2D bpx = r.rig.screenOf(blocked);
	r.hud->mouseMove(bpx.x, bpx.y, 0);
	r.hud->update(0.034);
	CHECK_FALSE(r.hud->radiusCursor().decal().valid);
	r.hud->mouseButton(HudInput::Button::Left, true, bpx.x, bpx.y, 0, 1000);
	r.hud->mouseButton(HudInput::Button::Left, false, bpx.x, bpx.y, 0, 1050);
	r.hud->update(0.034);
	CHECK(castsQueued(*r.rig.game) == queued);
	CHECK(r.hud->input().ui().getGUICommand() == guard.button); // the command waits for a valid spot
	r.rig.lookAt(clear);
	const ICoord2D cpx = r.rig.screenOf(clear);
	r.hud->mouseMove(cpx.x, cpx.y, 0);
	r.hud->update(0.034);
	CHECK(r.hud->radiusCursor().decal().valid);
	r.hud->mouseButton(HudInput::Button::Left, true, cpx.x, cpx.y, 0, 1100);
	r.hud->mouseButton(HudInput::Button::Left, false, cpx.x, cpx.y, 0, 1150);
	r.hud->update(0.034);
	CHECK(castsQueued(*r.rig.game) == queued + 1);
	CHECK(r.hud->input().ui().getGUICommand() == nullptr);
}
