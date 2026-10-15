// OpenBFME retail tests of the radar's objects (lane RADAR-1): which live objects TheRadar lists and how (Radar::captureObject: RW 0x6D9042 addObject,
// RW 0x68EBE9 getRadarPriority, RW 0x6D8C53 the colour), the overlay of a live game and the view box at the terrain average height. They SKIP when
// ROTWK_INSTALL / BFME2_INSTALL are unset. GPL-3.0.

#include "HudTestUtil.h"
#include "ScriptTestUtil.h"

#include "GameClient/GUI/ShellServices.h"
#include "GameClient/InGameHud.h"
#include "GameClient/LogicSnapshot.h"
#include "GameClient/Radar.h"
#include "GameLogic/Damage.h"
#include "GameLogic/Map/TerrainLogic.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ScriptEngine/ScriptEngine.h"

#include <cmath>
#include <limits>

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

// ---- lane BACKLOG3 (S-2452): the scripts' radar event actions, the dozer's finished building and the under-attack message ------------------------------------
namespace
{
ScriptParameter coordParam(float x, float y, float z)
{
	ScriptParameter p;
	p.type = 16;
	p.x = x;
	p.y = y;
	p.z = z;
	return p;
}

int eventsOfType(const Radar &radar, int type)
{
	int n = 0;
	for (int i = 0; i < Radar::kMaxEvents; ++i)
	{
		n += radar.radarEvent(i).active && radar.radarEvent(i).type == type;
	}
	return n;
}

int activeEvents(const Radar &radar)
{
	int n = 0;
	for (int i = 0; i < Radar::kMaxEvents; ++i)
	{
		n += radar.radarEvent(i).active;
	}
	return n;
}

// the ping of the one active event of `type`: its movie symbol and the window pixel the movie was told (RW 0x44DF56) against the event's world position
void checkPing(Radar &radar, int type, const char *name, const Coord3D &world)
{
	const int pic[4] = { 40, 500, 200, 200 };
	const Radar::RadarEventRecord *found = nullptr;
	for (int i = 0; i < Radar::kMaxEvents; ++i)
	{
		const Radar::RadarEventRecord &e = radar.radarEvent(i);
		if (e.active && e.type == type && e.worldLoc.x == world.x && e.worldLoc.y == world.y)
		{
			found = &e;
		}
	}
	REQUIRE(found != nullptr);
	REQUIRE(found->ping != -1); // -1: the event has no ping
	const Radar::Ping &p = radar.pings()[(size_t)found->ping];
	CHECK(p.name == name);
	CHECK(p.created);
	const int rx = std::clamp((int)(world.x / (radar.extentWidth() / 128.0f)), 0, 127), ry = std::clamp((int)(world.y / (radar.extentHeight() / 128.0f)), 0, 127);
	CHECK(p.x == (float)(rx * pic[2] / 128 + pic[0]));
	CHECK(p.y == (float)((127 - ry) * pic[3] / 128 + pic[1]));
}
} // namespace

TEST_CASE("backlog3 radar scripts: RADAR_CREATE_EVENT / OBJECT_CREATE_RADAR_EVENT / TEAM_CREATE_RADAR_EVENT make the event, its Palantir ping and the "
          "Space jump's position (RW 0x7BC221 / 0x7BC250 / 0x7C0B61 -> 0x6D98FA, 0x5DD1F3)")
{
	if (!haveWorld("backlog3 radar scripts"))
	{
		return;
	}
	using scripttest::act;
	using scripttest::ip;
	using scripttest::sp;
	SharedWorld &s = shared();
	RadarRig r(s);
	Radar &radar = r.hud->radar();
	radar.setPicture(40, 500, 200, 200);
	r.frames(3);
	ScriptEngine &engine = r.rig.logic().scriptEngine();
	REQUIRE(activeEvents(radar) == 0);
	REQUIRE(radar.events().empty());

	// a position: a beacon (6) there, 4 seconds
	const Coord3D a{ 1500.0f, 1200.0f, 7.0f };
	engine.executeAction(act("RADAR_CREATE_EVENT", { coordParam(a.x, a.y, a.z), ip(30, Radar::RADAR_EVENT_BEACON) }));
	r.frames(2);
	CHECK(activeEvents(radar) == 1);
	CHECK(eventsOfType(radar, Radar::RADAR_EVENT_BEACON) == 1);
	checkPing(radar, Radar::RADAR_EVENT_BEACON, "PingBeacon", a);
	REQUIRE(radar.events().size() == 1);
	CHECK(radar.events()[0].position.x == a.x);
	CHECK(radar.events()[0].position.y == a.y);
	CHECK(radar.events()[0].position.z == a.z);

	// a named unit: an information event (0) at its position; an unknown name does nothing
	const Coord3D spot = r.rig.freeSpot(1900, 1500, 250.0f);
	Object *unit = r.rig.make("GondorBarracks", spot.x, spot.y);
	unit->setName("BL3Unit");
	r.frames(1);
	const Coord3D u = *unit->getPosition();
	const size_t notices = r.rig.logic().radarEventNotices().size();
	engine.executeAction(act("OBJECT_CREATE_RADAR_EVENT", { sp(14, "BL3NoSuchUnit"), ip(30, 0) }));
	CHECK(r.rig.logic().radarEventNotices().size() == notices);
	engine.executeAction(act("OBJECT_CREATE_RADAR_EVENT", { sp(14, "BL3Unit"), ip(30, 0) }));
	r.frames(2);
	CHECK(activeEvents(radar) == 2);
	checkPing(radar, 0, "PingInformation", u);
	// the newest position is the list's first (RW 0x75991F): Space looks at it
	REQUIRE(radar.events().size() == 2);
	CHECK(radar.events()[0].position.x == u.x);
	CHECK(radar.events()[1].position.x == a.x);
	radar.tryJumpToNextEvent(r.rig.view);
	CHECK(r.rig.view.position().x == u.x);
	CHECK(r.rig.view.position().y == u.y);

	// a team: at its members' average position (RW 0x7A0ECD), only when it has a unit (RW 0x7A07D8); an unknown team does nothing
	Team *team = r.rig.local->getDefaultTeam();
	REQUIRE(team != nullptr);
	float sx = 0.0f, sy = 0.0f;
	int n = 0, units = 0;
	for (const Object *o = team->getFirstMember(); o; o = o->friend_teamNext())
	{
		sx += o->getPosition()->x;
		sy += o->getPosition()->y;
		++n;
		units += !o->isEffectivelyDead() && !o->isKindOfName("STRUCTURE") && !o->isKindOfName("PROJECTILE") && !o->isKindOfName("IGNORE_FOR_VICTORY");
	}
	REQUIRE(team->getMemberCount() == (unsigned)n);
	REQUIRE(team->getFirstMember() != nullptr);
	const std::string teamName = r.rig.local->getPlayerName() + "/" + team->getName(); // the owner's side and the prototype's name (RW 0x604043)
	engine.executeAction(act("TEAM_CREATE_RADAR_EVENT", { sp(3, "BL3NoSuchTeam"), ip(30, 2) }));
	CHECK(r.rig.logic().radarEventNotices().size() == notices + 1);
	engine.executeAction(act("TEAM_CREATE_RADAR_EVENT", { sp(3, teamName), ip(30, 2) }));
	INFO("team " << teamName << " members " << n << " units " << units);
	if (units == 0)
	{
		// a team of structures only has no unit: no event; a unit joins and it has one
		CHECK(r.rig.logic().radarEventNotices().size() == notices + 1);
		const Coord3D us = r.rig.freeSpot(1700, 1300, 150.0f);
		Object *soldier = r.rig.make("GondorFighter", us.x, us.y);
		REQUIRE(soldier->getTeam() == team);
		sx += soldier->getPosition()->x;
		sy += soldier->getPosition()->y;
		++n;
		engine.executeAction(act("TEAM_CREATE_RADAR_EVENT", { sp(3, teamName), ip(30, 2) }));
	}
	REQUIRE(r.rig.logic().radarEventNotices().size() == notices + 2);
	const Coord3D centre = r.rig.logic().radarEventNotices().back().position;
	const float inv = 1.0f / (float)n; // RW 0x7A0ECD: the sums times 1 / n, in member order
	CHECK(centre.x == sx * inv);
	CHECK(centre.y == sy * inv);
	r.frames(2);
	CHECK(activeEvents(radar) == 3);
	checkPing(radar, 2, "PingUpgrade", centre);

	// the events end after their 4 seconds (120 client frames)
	r.frames(140);
	CHECK(activeEvents(radar) == 0);
}

TEST_CASE("backlog3 radar scripts, hostile values: a position that is not a number is refused by name, a huge one lands in cell 0 as cvttss2si does, an unknown "
          "type has no ping, and a flood keeps 64 notices, 64 events and 64 Space positions")
{
	if (!haveWorld("backlog3 radar scripts hostile"))
	{
		return;
	}
	using scripttest::act;
	using scripttest::ip;
	SharedWorld &s = shared();
	RadarRig r(s);
	Radar &radar = r.hud->radar();
	radar.setPicture(40, 500, 200, 200);
	r.frames(3);
	GameLogic &logic = r.rig.logic();
	ScriptEngine &engine = logic.scriptEngine();
	const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
	auto refused = [&]() {
		size_t n = 0;
		for (const std::string &e : logic.report().errors)
		{
			n += e == "script action RADAR_CREATE_EVENT: a radar event position that is not a finite number is refused";
		}
		return n;
	};
	REQUIRE(refused() == 0);
	const size_t notices0 = logic.radarEventNotices().size();
	engine.executeAction(act("RADAR_CREATE_EVENT", { coordParam(nan, 100.0f, 0.0f), ip(30, 6) }));
	engine.executeAction(act("RADAR_CREATE_EVENT", { coordParam(100.0f, -inf, 0.0f), ip(30, 6) }));
	engine.executeAction(act("RADAR_CREATE_EVENT", { coordParam(100.0f, 100.0f, inf), ip(30, 6) }));
	CHECK(logic.radarEventNotices().size() == notices0); // refused: nothing recorded
	CHECK(refused() == 1);                               // and said so (each distinct text once)
	r.frames(2);
	CHECK(activeEvents(radar) == 0);
	CHECK(radar.events().empty());
	// a finite position outside the int range of its cell: cell 0 (RW 0x6D881F: cvttss2si's 0x80000000, clamped), the far side stays 127
	engine.executeAction(act("RADAR_CREATE_EVENT", { coordParam(3.0e38f, 3.0e30f, 0.0f), ip(30, 6) }));
	r.frames(2);
	REQUIRE(activeEvents(radar) == 1);
	for (int i = 0; i < Radar::kMaxEvents; ++i)
	{
		if (radar.radarEvent(i).active)
		{
			CHECK(radar.radarEvent(i).radarX == 0);
			CHECK(radar.radarEvent(i).radarY == 0);
		}
	}
	// a type outside the ping table (RW 0xBDAD30) and outside the enumeration: an event without a ping, as retail's createEvent takes any type
	const size_t pings0 = radar.pings().size();
	engine.executeAction(act("RADAR_CREATE_EVENT", { coordParam(900.0f, 900.0f, 0.0f), ip(30, 0x7FFFFFFF) }));
	engine.executeAction(act("RADAR_CREATE_EVENT", { coordParam(900.0f, 900.0f, 0.0f), ip(30, -5) }));
	engine.executeAction(act("RADAR_CREATE_EVENT", { coordParam(900.0f, 900.0f, 0.0f) })); // no type parameter: type 0
	r.frames(2);
	CHECK(activeEvents(radar) == 4);
	CHECK(radar.pings().size() == pings0 + 1); // the type 0 event's PingInformation only
	// a flood in one logic frame: the logic keeps the newest 64 notices, the radar its 64 records, the Space list its 64 newest positions
	for (int i = 0; i < 500; ++i)
	{
		engine.executeAction(act("RADAR_CREATE_EVENT", { coordParam(1000.0f + (float)i, 1000.0f, 0.0f), ip(30, 6) }));
	}
	CHECK(logic.radarEventNotices().size() == GameLogic::kRadarEventNotices);
	CHECK(logic.radarEventNotices().back().position.x == 1499.0f);
	r.frames(2);
	CHECK(activeEvents(radar) == Radar::kMaxEvents);
	CHECK(radar.pings().size() == (size_t)Radar::kMaxEvents); // one PingBeacon per record, the replaced records' pings left the list
	CHECK(radar.events().size() == Radar::kMaxEvaPositions);
	CHECK(radar.events()[0].position.x == 1499.0f); // the newest first
	for (int round = 0; round < 3; ++round)
	{
		for (int i = 0; i < 500; ++i)
		{
			engine.executeAction(act("RADAR_CREATE_EVENT", { coordParam(2000.0f + (float)i, 1000.0f, 0.0f), ip(30, 6) }));
		}
		r.frames(2);
		radar.tryJumpToNextEvent(r.rig.view); // the cursor survives the list's turnover
	}
	CHECK(radar.events().size() == Radar::kMaxEvaPositions);
	CHECK(radar.pings().size() == (size_t)Radar::kMaxEvents); // one PingBeacon per record, the replaced records' pings left the list
	CHECK(logic.radarEventNotices().size() == GameLogic::kRadarEventNotices);
	CHECK(refused() == 1);
}

TEST_CASE("backlog3 radar construction: a finished building's event (RW 0x88E125) is the PingGeneric of the dozer's local controller only")
{
	if (!haveWorld("backlog3 radar construction"))
	{
		return;
	}
	SharedWorld &s = shared();
	RadarRig r(s);
	Radar &radar = r.hud->radar();
	radar.setPicture(40, 500, 200, 200);
	r.frames(3);
	Player *enemy = r.rig.game->players().findPlayerWithName("Player_2");
	REQUIRE(enemy != nullptr);
	REQUIRE(activeEvents(radar) == 0);
	// another player's building: nothing on our radar
	const Coord3D theirs{ 2500.0f, 2200.0f, 0.0f };
	r.rig.logic().noteRadarEvent(1, theirs, GameLogic::RadarEventNotice::CONSTRUCTION, enemy->getPlayerIndex());
	r.frames(2);
	CHECK(activeEvents(radar) == 0);
	CHECK(radar.pings().empty());
	// ours: the event and its ping; no Space position (only the scripts call RW 0x5DD1F3)
	const Coord3D ours{ 1500.0f, 1200.0f, 0.0f };
	r.rig.logic().noteRadarEvent(1, ours, GameLogic::RadarEventNotice::CONSTRUCTION, r.rig.local->getPlayerIndex());
	r.frames(2);
	CHECK(activeEvents(radar) == 1);
	checkPing(radar, 1, "PingGeneric", ours);
	CHECK(radar.events().empty());
	// a notice is applied once
	r.frames(5);
	CHECK(activeEvents(radar) == 1);
	CHECK(radar.pings().size() == 1);
	// a notice the radar meets later than the event would have lived (4 s: a HUD made again in a running game) is not replayed
	r.frames(140);
	REQUIRE(activeEvents(radar) == 0);
	const Coord3D late{ 1800.0f, 1400.0f, 0.0f };
	// the logic alone, `n` frames on (no HUD update reads the notice); r.rig.logic() waits for a completed frame
	auto logicFrames = [&](UnsignedInt n) {
		const UnsignedInt target = r.rig.logic().getFrame() + n;
		for (int guard = 0; guard < 2000 && r.rig.logic().getFrame() != target; ++guard)
		{
			const UnsignedInt before = r.rig.logic().getFrame();
			r.rig.game->advance(0.2);
			REQUIRE(r.rig.logic().getFrame() - before <= 1u); // tolerance: a 0.2 s step is at most one logic frame (5 a second), so the loop stops exactly on `target`
		}
		REQUIRE(r.rig.logic().getFrame() == target);
	};
	r.rig.logic().noteRadarEvent(1, late, GameLogic::RadarEventNotice::CONSTRUCTION, r.rig.local->getPlayerIndex());
	logicFrames(Radar::kNoticeLifeLogicFrames + 1);
	r.hud->update(0.033);
	CHECK(activeEvents(radar) == 0);
	// one frame younger it is still shown
	r.rig.logic().noteRadarEvent(1, late, GameLogic::RadarEventNotice::CONSTRUCTION, r.rig.local->getPlayerIndex());
	logicFrames(Radar::kNoticeLifeLogicFrames);
	r.hud->update(0.033);
	CHECK(activeEvents(radar) == 1);
}

TEST_CASE("backlog3 radar attack message: each of RW 0x6D9BF3's four messages for its kind of object, one per new event; a refused event shows none")
{
	if (!haveWorld("backlog3 radar attack message"))
	{
		return;
	}
	SharedWorld &s = shared();
	RadarRig r(s);
	Player *enemy = r.rig.game->players().findPlayerWithName("Player_2");
	REQUIRE(enemy != nullptr);
	InGameUI &ui = r.hud->input().ui();
	auto hit = [&](Object *o) {
		DamageInfo d;
		d.m_input.m_sourcePlayerMask = 1u << enemy->getPlayerIndex();
		d.m_input.m_damageType = DAMAGE_SLASH;
		d.m_input.m_amount = 5.0f;
		o->attemptDamage(d);
	};
	auto radarMessages = [&]() {
		std::vector<std::string> out;
		for (const std::string &m : ui.takeMessages())
		{
			if (m.compare(0, 6, "RADAR:") == 0)
			{
				out.push_back(m);
			}
		}
		return out;
	};
	// one retail object per branch of RW 0x6D9C2A .. (KindOf bits 8 INFANTRY / 9 CAVALRY, 16 HARVESTER, 7 STRUCTURE + 37 MP_COUNT_FOR_VICTORY), each hit more
	// than tryEvent's 10 seconds (300 client frames, RW 0x6D9B4D) after the last event; the expected label is the literal of the binary's string, never the
	// implementation's answer
	struct Case
	{
		const char *tmpl;
		float x, y;
		bool unit, harvester, countedStructure; ///< the kinds the object must have for its branch (checked by name)
		const char *label;
	};
	const Case cases[] = {
		{ "GondorBoromir", 1900.0f, 1500.0f, true, false, false, "RADAR:UnitUnderAttack" },
		{ "WildLaborer", 2200.0f, 1500.0f, true, true, false, "RADAR:HarvesterUnderAttack" },
		{ "GondorBarracks", 1900.0f, 1800.0f, false, false, true, "RADAR:StructureUnderAttack" },
		{ "GondorTrebuchet", 2200.0f, 1800.0f, false, false, false, "RADAR:UnderAttack" },
	};
	std::vector<Object *> made;
	for (const Case &c : cases)
	{
		INFO(c.tmpl);
		const Coord3D at = r.rig.freeSpot(c.x, c.y, 200.0f);
		Object *o = r.rig.make(c.tmpl, at.x, at.y);
		made.push_back(o);
		r.frames(330); // 330 HUD updates of 0.033 s: 326 client frames, past the 300 of the event before
		REQUIRE((o->isKindOfName("INFANTRY") || o->isKindOfName("CAVALRY")) == c.unit);
		REQUIRE(o->isKindOfName("HARVESTER") == c.harvester);
		REQUIRE((o->isKindOfName("STRUCTURE") && o->isKindOfName("MP_COUNT_FOR_VICTORY")) == c.countedStructure);
		REQUIRE(capture(r, *o).listed); // on the radar's list (Object + 0x268, RW 0x67B57B)
		radarMessages();
		hit(o);
		r.frames(3);
		CHECK(radarMessages() == std::vector<std::string>{ c.label });
		CHECK(std::string(Radar::underAttackLabel(*o)) == c.label);
		// a second hit within 10 seconds and 600 units: tryEvent refuses (RW 0x6D9B4D), no second message
		hit(o);
		r.frames(3);
		CHECK(radarMessages() == std::vector<std::string>{});
	}
}
