// OpenBFME retail tests of the radar's objects (lane RADAR-1): which live objects TheRadar lists and how (Radar::captureObject: RW 0x6D9042 addObject,
// RW 0x68EBE9 getRadarPriority, RW 0x6D8C53 the colour), the overlay of a live game and the view box at the terrain average height. They SKIP when
// ROTWK_INSTALL / BFME2_INSTALL are unset. GPL-3.0.

#include "HudTestUtil.h"

#include "GameClient/GUI/ShellServices.h"
#include "GameClient/InGameHud.h"
#include "GameClient/LogicSnapshot.h"
#include "GameClient/Radar.h"
#include "GameLogic/Damage.h"
#include "GameLogic/Map/TerrainLogic.h"
#include "GameLogic/Object/Object.h"

#include <cmath>

using namespace hudtest;

namespace
{
struct RadarRig
{
	Rig rig;
	RecordingShellServices services;
	std::unique_ptr<InGameHud> hud;
	explicit RadarRig(SharedWorld &s) : rig(s)
	{
		InGameHud::Config cfg{ *rig.game, *s.world, *s.mount->fs, services, rig.view, s.mouse, s.meta, nullptr };
		hud = std::make_unique<InGameHud>(cfg);
		std::string error;
		REQUIRE_MESSAGE(hud->boot(&error), error);
		hud->setWindowSize(1024, 768);
		rig.view.setScreen(1024, 768);
	}
	~RadarRig() { hud.reset(); }
	void frames(int n)
	{
		for (int i = 0; i < n; ++i)
		{
			hud->update(0.033);
			rig.game->advance(0.033);
		}
	}
};

ObjectSnapshot::RadarEntry capture(RadarRig &r, const Object &o)
{
	ObjectSnapshot::RadarEntry e;
	Radar::captureObject(r.rig.logic(), o, r.rig.local, e);
	return e;
}
} // namespace

TEST_CASE("radar1 retail: every horde member is a unit on the radar, the horde object is not; a hero is the hero shape, a keep a command centre")
{
	if (!haveWorld("radar1 retail"))
	{
		return;
	}
	SharedWorld &s = shared();
	RadarRig r(s);
	Player *enemy = r.rig.game->players().findPlayerWithName("Player_2");
	REQUIRE(enemy != nullptr);
	const Coord3D a = r.rig.freeSpot(1500, 1500, 250.0f);
	Object *horde = r.rig.make("GondorFighterHorde", a.x, a.y);
	const Coord3D b = r.rig.freeSpot(1900, 1500, 250.0f);
	Object *barracks = r.rig.make("GondorBarracks", b.x, b.y);
	const Coord3D c = r.rig.freeSpot(1500, 1900, 250.0f);
	Object *hero = r.rig.make("GondorAragorn", c.x, c.y);
	const Coord3D d = r.rig.freeSpot(1100, 1500, 300.0f);
	Object *keep = r.rig.make("GondorCampKeep", d.x, d.y);
	const Coord3D e = r.rig.freeSpot(1900, 1900, 250.0f);
	Object *theirs = r.rig.make("MordorFighterHorde", e.x, e.y, enemy);
	r.frames(10);

	// the horde object has no RadarPriority (INVALID, not garrisonable, not CAPTURABLE): not listed (RW 0x6D8B21)
	const ObjectSnapshot::RadarEntry he = capture(r, *horde);
	CHECK(he.priority == Radar::RADAR_PRIORITY_INVALID);
	CHECK_FALSE(he.listed);
	int members = 0, theirMembers = 0;
	for (Object *o = r.rig.logic().getFirstObject(); o; o = o->getNextObject())
	{
		if (o->getContainedBy() == horde)
		{
			const ObjectSnapshot::RadarEntry m = capture(r, *o);
			CHECK(m.listed);
			CHECK(m.local);
			CHECK(m.priority == Radar::RADAR_PRIORITY_UNIT);
			CHECK(m.shape == Radar::SHAPE_DOT);
			CHECK(m.color == Radar::saturateColor(r.rig.local->getPlayerColor()));
			++members;
		}
		else if (o->getContainedBy() == theirs)
		{
			const ObjectSnapshot::RadarEntry m = capture(r, *o);
			CHECK(m.listed);
			CHECK_FALSE(m.local);
			CHECK(m.color == Radar::saturateColor(enemy->getPlayerColor()));
			++theirMembers;
		}
	}
	CHECK(members > 0);
	CHECK(theirMembers > 0);
	const ObjectSnapshot::RadarEntry be = capture(r, *barracks);
	CHECK(be.listed); // our own structure: the local player is not NEUTRAL to its team
	CHECK(be.priority == Radar::RADAR_PRIORITY_STRUCTURE);
	CHECK(be.shape == Radar::SHAPE_DOT);
	const ObjectSnapshot::RadarEntry ae = capture(r, *hero);
	CHECK(ae.listed);
	CHECK(ae.shape == Radar::SHAPE_HERO);
	const ObjectSnapshot::RadarEntry ke = capture(r, *keep);
	CHECK(ke.listed);
	CHECK(ke.shape == Radar::SHAPE_COMMAND_CENTER);
	CHECK(ke.boundingRadius > 10.0f);

	// the overlay of the game's snapshot: our barracks' 2 x 2 texels, the keep's circle around its centre, the hero's core
	std::shared_ptr<const LogicSnapshot> snap = LogicSnapshot::build(r.rig.logic(), nullptr, false, 0);
	Radar &radar = r.hud->radar();
	REQUIRE(radar.ready());
	std::vector<std::uint32_t> t;
	radar.renderOverlay(*snap, 0, 0xFFFFFFFFu, t);
	float maxX = 0, maxY = 0;
	REQUIRE(r.rig.logic().terrain()->getExtent(0, maxX, maxY));
	const float xs = 128.0f / maxX, ys = 128.0f / maxY;
	auto texel = [&](float wx, float wy, int dx, int dy) { return t[(size_t)((int)(wy * ys) + dy) * Radar::kCells + (size_t)((int)(xs * wx) + dx)]; };
	CHECK(texel(barracks->getPosition()->x, barracks->getPosition()->y, -1, -1) != 0u);
	const std::uint32_t core = texel(hero->getPosition()->x, hero->getPosition()->y, 0, 0);
	CHECK((core >> 24) == 0xFFu);
	CHECK(texel(keep->getPosition()->x, keep->getPosition()->y, 0, 0) == 0u); // the circle is hollow
}

TEST_CASE("radar1 retail: the view box's corners are the view's corners at the terrain average height, in the picture's pixels")
{
	if (!haveWorld("radar1 retail view box"))
	{
		return;
	}
	SharedWorld &s = shared();
	RadarRig r(s);
	Radar &radar = r.hud->radar();
	REQUIRE(radar.ready());
	float maxX = 0, maxY = 0;
	REQUIRE(r.rig.logic().terrain()->getExtent(0, maxX, maxY));
	// the average lies within the map's heights
	float lo = 1e9f, hi = -1e9f;
	for (float y = 50.0f; y < maxY; y += 97.0f)
	{
		for (float x = 50.0f; x < maxX; x += 97.0f)
		{
			lo = std::min(lo, r.rig.logic().getGroundHeight(x, y));
			hi = std::max(hi, r.rig.logic().getGroundHeight(x, y));
		}
	}
	CHECK(radar.terrainAverageZ() >= lo - 1.0f);
	CHECK(radar.terrainAverageZ() <= hi + 1.0f);
	const Coord3D target{ maxX * 0.5f, maxY * 0.5f, radar.terrainAverageZ() };
	r.rig.lookAt(target);
	float box[8];
	REQUIRE(radar.viewBoxCorners(10.0f, 20.0f, 256, 256, box));
	// TL, TR, BR, BL: the top edge is narrower than the bottom one (a perspective view from the south looks north: the far edge is up and wider)
	CHECK(box[0] < box[2]);
	CHECK(box[7] > box[1]);
	CHECK(std::fabs(box[2] - box[0]) > std::fabs(box[4] - box[6]));
	// the middle of the view is near the target's pixel (RW 0x44D41B: (127 - y) rows)
	const float px = target.x * (128.0f / maxX) * 256.0f / 128.0f + 10.0f, py = (127.0f - target.y * (128.0f / maxY)) * 256.0f / 128.0f + 20.0f;
	const float mx = (box[0] + box[2] + box[4] + box[6]) * 0.25f, my = (box[1] + box[3] + box[5] + box[7]) * 0.25f;
	CHECK(std::fabs(mx - px) < 12.0f);
	CHECK(std::fabs(my - py) < 30.0f);
}

TEST_CASE("radar1 retail events: an event lives 4 s of client frames, fades half a second before, and a second one within 600 units and 10 s is refused")
{
	if (!haveWorld("radar1 retail events"))
	{
		return;
	}
	SharedWorld &s = shared();
	RadarRig r(s);
	Radar &radar = r.hud->radar();
	REQUIRE(radar.ready());
	radar.setClientFrame(1000);
	const Coord3D at{ 1200.0f, 900.0f, 0.0f };
	REQUIRE(radar.tryUnderAttackEvent(at));
	const Radar::RadarEventRecord &e = radar.radarEvent(0);
	CHECK(e.type == Radar::RADAR_EVENT_UNDER_ATTACK);
	CHECK(e.active);
	CHECK(e.createFrame == 1000u);
	CHECK(e.dieFrame == 1120u);  // 30 frames a second x 4 s
	CHECK(e.fadeFrame == 1105u); // half a second before
	// near and soon: refused; far, or the same place 10 s later: a new event
	radar.setClientFrame(1200);
	CHECK_FALSE(radar.tryUnderAttackEvent({ 1500.0f, 1300.0f, 0.0f })); // 500 units away
	CHECK(radar.tryUnderAttackEvent({ 1900.0f, 900.0f, 0.0f }));        // 700 units away
	radar.setClientFrame(1300);
	CHECK(radar.tryUnderAttackEvent(at));
	CHECK(radar.radarEvent(2).createFrame == 1300u);
	// the first event ended at its die frame
	radar.update();
	CHECK_FALSE(radar.radarEvent(0).active);
	CHECK(radar.radarEvent(2).active);
	// the ping protocol: a new ping is created and moved to the event's pixel, then faded after the fade frame
	std::vector<Radar::PingCall> calls;
	radar.drawEvents(100, 50, 256, 256, calls);
	REQUIRE(calls.size() >= 4); // two live events: create + move each
	CHECK(calls[calls.size() - 4].kind == Radar::PingCall::Create);
	CHECK(calls[calls.size() - 4].name == "PingAttack");
	CHECK(calls[calls.size() - 3].kind == Radar::PingCall::Move);
	calls.clear();
	radar.setClientFrame(1300 + 106);
	radar.drawEvents(100, 50, 256, 256, calls);
	bool faded = false;
	for (const Radar::PingCall &c : calls)
	{
		faded = faded || c.kind == Radar::PingCall::FadeOut;
	}
	CHECK(faded);
	// every event ended: the pings are released, faded and gone from the list
	radar.setClientFrame(2000);
	radar.update();
	calls.clear();
	radar.drawEvents(100, 50, 256, 256, calls);
	CHECK(radar.pings().empty());
	// a long fight keeps the list to the live events' pings
	for (unsigned f = 3000; f < 3000 + 40 * 400; f += 400)
	{
		radar.setClientFrame(f);
		radar.update();
		REQUIRE(radar.tryUnderAttackEvent(at));
		radar.drawEvents(100, 50, 256, 256, calls);
		CHECK(radar.pings().size() == 1);
	}
}

TEST_CASE("radar1 retail events: an enemy's hit on our unit makes an UNDER_ATTACK event and the Palantir's PingAttack, without movie errors")
{
	if (!haveWorld("radar1 retail attack"))
	{
		return;
	}
	SharedWorld &s = shared();
	RadarRig r(s);
	Player *enemy = r.rig.game->players().findPlayerWithName("Player_2");
	REQUIRE(enemy != nullptr);
	const Coord3D b = r.rig.freeSpot(1900, 1500, 250.0f);
	Object *barracks = r.rig.make("GondorBarracks", b.x, b.y);
	r.frames(3);
	DamageInfo hit;
	hit.m_input.m_sourcePlayerMask = 1u << enemy->getPlayerIndex();
	hit.m_input.m_damageType = DAMAGE_SLASH;
	hit.m_input.m_amount = 5.0f;
	barracks->attemptDamage(hit);
	const UnsignedInt hitFrame = barracks->radarAttackFrame();
	CHECK(hitFrame == r.rig.logic().getFrame());
	// our own player's hit is not an attack
	Object *ours = r.rig.make("GondorBarracks", b.x + 400.0f, b.y);
	DamageInfo own;
	own.m_input.m_sourcePlayerMask = 1u << r.rig.local->getPlayerIndex();
	own.m_input.m_damageType = DAMAGE_SLASH;
	own.m_input.m_amount = 5.0f;
	ours->attemptDamage(own);
	CHECK(ours->radarAttackFrame() == 0xFFFFFFFFu);
	Radar &radar = r.hud->radar();
	radar.setPicture(40, 500, 200, 200);
	r.frames(3);
	int attacks = 0;
	for (int i = 0; i < Radar::kMaxEvents; ++i)
	{
		attacks += radar.radarEvent(i).type == Radar::RADAR_EVENT_UNDER_ATTACK;
	}
	CHECK(attacks == 1);
	REQUIRE(r.hud->palantir() != nullptr);
	CHECK(r.hud->palantir()->radarPingCalls() >= 2); // CreateRadarPing, MoveRadarPing
	for (const std::string &e : r.hud->palantir()->callErrors())
	{
		CHECK_MESSAGE(e.find("RadarPing") == std::string::npos, e);
	}
}

TEST_CASE("radar1 retail events: hordes that fight make UNDER_ATTACK events for the local player only")
{
	if (!haveWorld("radar1 retail fight"))
	{
		return;
	}
	SharedWorld &s = shared();
	RadarRig r(s);
	Player *enemy = r.rig.game->players().findPlayerWithName("Player_2");
	REQUIRE(enemy != nullptr);
	const Coord3D a = r.rig.freeSpot(1500, 1500, 300.0f);
	Object *ours = r.rig.make("GondorFighterHorde", a.x, a.y);
	Object *theirs = r.rig.make("MordorFighterHorde", a.x + 80.0f, a.y, enemy);
	(void)theirs;
	r.hud->radar().setPicture(40, 500, 200, 200);
	UnsignedInt lastHit = 0xFFFFFFFFu;
	for (int i = 0; i < 400 && lastHit == 0xFFFFFFFFu; ++i)
	{
		r.frames(1);
		for (Object *o = r.rig.logic().getFirstObject(); o; o = o->getNextObject())
		{
			if (o->getContainedBy() == ours && o->radarAttackFrame() != 0xFFFFFFFFu)
			{
				lastHit = o->radarAttackFrame();
			}
		}
	}
	REQUIRE(lastHit != 0xFFFFFFFFu);
	r.frames(2);
	int attacks = 0;
	for (int i = 0; i < Radar::kMaxEvents; ++i)
	{
		attacks += r.hud->radar().radarEvent(i).type == Radar::RADAR_EVENT_UNDER_ATTACK;
	}
	CHECK(attacks >= 1);
	MESSAGE("radar1 fight: first hit on our horde at logic frame ", lastHit, ", ", attacks, " events, ", r.hud->palantir()->radarPingCalls(), " ping calls");
}

TEST_CASE("radar1 retail overlay: a wall segment is its footprint in its indicator colour, inside its bounding square (RW 0x44D814)")
{
	if (!haveWorld("radar1 retail wall"))
	{
		return;
	}
	SharedWorld &s = shared();
	RadarRig r(s);
	const Coord3D w = r.rig.freeSpot(1500, 1500, 300.0f);
	Object *wall = r.rig.make("GondorCastleWallSegment", w.x, w.y);
	const ObjectSnapshot::RadarEntry e = capture(r, *wall);
	REQUIRE(e.listed);
	REQUIRE(e.shape == Radar::SHAPE_WALL);
	std::shared_ptr<const LogicSnapshot> snap = LogicSnapshot::build(r.rig.logic(), nullptr, false, 0);
	std::vector<std::uint32_t> t;
	r.hud->radar().renderOverlay(*snap, 0, 0xFFFFFFFFu, t);
	float maxX = 0, maxY = 0;
	REQUIRE(r.rig.logic().terrain()->getExtent(0, maxX, maxY));
	const float xs = 128.0f / maxX, ys = 128.0f / maxY;
	const int cx = (int)(wall->getPosition()->x * xs), cy = (int)(wall->getPosition()->y * ys), rad = (int)e.boundingRadius;
	// our colour's texels lie in [ (int)(c - R * scale), (int)(R * scale + c) ) on each axis, in the indicator colour (not saturated), alpha 0x80 (the 30-unit sphere) or 0xFF (5)
	const int x0 = std::max(0, (int)((float)cx - (float)rad * xs)), x1 = (int)((float)rad * xs + (float)cx);
	const int y0 = std::max(0, (int)((float)cy - (float)rad * ys)), y1 = (int)((float)rad * ys + (float)cy);
	int drawn = 0;
	for (int y = 0; y < Radar::kCells; ++y)
	{
		for (int x = 0; x < Radar::kCells; ++x)
		{
			const std::uint32_t v = t[(size_t)y * Radar::kCells + (size_t)x];
			if (v == 0u || (v & 0xFFFFFFu) != (e.indicator & 0xFFFFFFu))
			{
				continue; // other objects of the map (neutral props of other colours)
			}
			++drawn;
			CHECK((x >= x0 && x < x1 && y >= y0 && y < y1));
			const std::uint32_t alpha = v >> 24;
			CHECK((alpha == 0x80u || alpha == 0xFFu));
		}
	}
	CHECK(drawn > 0);
	MESSAGE("radar1 wall: bounding radius ", e.boundingRadius, ", ", drawn, " texels");
}

TEST_CASE("radar1 retail clock: the client clock runs 30 frames a second whatever the render rate; an event lasts its 4 s at 60 and 144 Hz")
{
	if (!haveWorld("radar1 retail clock"))
	{
		return;
	}
	SharedWorld &s = shared();
	for (int hz : { 60, 144, 30, 20 })
	{
		RadarRig r(s);
		Radar &radar = r.hud->radar();
		const double dt = 1.0 / (double)hz;
		// one second of render updates is 30 client frames
		const unsigned before = radar.clientFrame();
		for (int i = 0; i < hz; ++i)
		{
			r.hud->update(dt);
		}
		CHECK_MESSAGE(radar.clientFrame() - before >= 29u, hz, " Hz");
		CHECK_MESSAGE(radar.clientFrame() - before <= 30u, hz, " Hz");
		radar.createEvent({ 1000.0f, 1000.0f, 0.0f }, Radar::RADAR_EVENT_UNDER_ATTACK, 4.0f);
		int e = -1;
		for (int i = 0; i < Radar::kMaxEvents; ++i)
		{
			if (radar.radarEvent(i).active)
			{
				e = i;
			}
		}
		REQUIRE(e >= 0);
		const int updates39 = (int)(3.9 * hz), updates41 = (int)(4.1 * hz);
		for (int i = 0; i < updates39; ++i)
		{
			r.hud->update(dt);
		}
		CHECK_MESSAGE(radar.radarEvent(e).active, hz, " Hz: still live after 3.9 s");
		for (int i = updates39; i < updates41; ++i)
		{
			r.hud->update(dt);
		}
		CHECK_MESSAGE(!radar.radarEvent(e).active, hz, " Hz: ended after 4.1 s");
	}
}

TEST_CASE("radar1 clock: the overlay is rebuilt once per client frame divisible by 6, also when draws skip frames (RW 0x45011C)")
{
	CHECK(Radar::overlayDue(5, ~0u)); // never built
	CHECK_FALSE(Radar::overlayDue(5, 4));
	CHECK(Radar::overlayDue(6, 5));
	CHECK_FALSE(Radar::overlayDue(7, 6));
	CHECK(Radar::overlayDue(13, 11)); // frame 12 was not drawn: the next draw rebuilds
	CHECK_FALSE(Radar::overlayDue(17, 13));
	CHECK(Radar::overlayDue(25, 13));
}

TEST_CASE("radar1 retail gate: the radar's update forces it on (RW 0x6D8E41); a new map's reset clears it until the next client frame")
{
	if (!haveWorld("radar1 retail gate"))
	{
		return;
	}
	SharedWorld &s = shared();
	RadarRig r(s);
	Radar &radar = r.hud->radar();
	REQUIRE(radar.setupFromTerrain()); // the reset (RW 0x6D8E25)
	CHECK_FALSE(radar.forced());
	CHECK_FALSE(radar.drawn(false));
	CHECK(radar.drawn(true)); // a player with radar
	CHECK(radar.advanceClock(Radar::kClientFrameSeconds * 1.5) == 1u);
	CHECK(radar.forced());
	CHECK(radar.drawn(false));
	radar.update();
	CHECK(radar.forced());
}

TEST_CASE("radar1 retail plane: the terrain average height is every radar cell's truncated ground height, the dry ones averaged (RW 0x6D8EBE)")
{
	if (!haveWorld("radar1 retail plane"))
	{
		return;
	}
	SharedWorld &s = shared();
	RadarRig r(s);
	Radar &radar = r.hud->radar();
	REQUIRE(radar.ready());
	float maxX = 0, maxY = 0;
	const TerrainLogic *t = r.rig.logic().terrain();
	REQUIRE(t->getExtent(0, maxX, maxY));
	const float xs = maxX * 0.0078125f, ys = maxY * 0.0078125f;
	float sum = 0.0f, every2 = 0.0f;
	int n = 0, n2 = 0;
	for (int y = 0; y < 128; ++y)
	{
		for (int x = 0; x < 128; ++x)
		{
			const float g = r.rig.logic().getGroundHeight((float)x * xs, (float)y * ys);
			float w = 0.0f;
			if (t->getStandingWaterHeight((float)x * xs, (float)y * ys, w) && w > g)
			{
				continue;
			}
			sum += (float)(int)g;
			++n;
			if (x % 2 == 0 && y % 2 == 0)
			{
				every2 += g;
				++n2;
			}
		}
	}
	REQUIRE(n > 0);
	CHECK(radar.terrainAverageZ() == sum / (float)n);
	MESSAGE("radar1 plane: all cells truncated ", sum / (float)n, ", every second cell (Generals) ", n2 ? every2 / (float)n2 : 0.0f);
}
