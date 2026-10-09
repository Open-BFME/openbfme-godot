// OpenBFME unit tests: the BFME update scheduler, the six-tick logic frame and object destruction (lane LOGIC-1).
//
// Every expectation is derived from the RotWK disassembly or the B1 decompile named on the case (RW = RotWK game.dat, caveat S-001;
// spec = ini-and-object-model.md 5.4-5.5) by working the rule by hand, or from an independent reference model in this file.

#include "doctest.h"
#include "LogicTestUtil.h"

using namespace logictest;

namespace
{
const char kObjects[] =
	"Object Plain\n"
	"End\n"
	"Object One\n"
	"  Behavior = LifetimeUpdate ModuleTag_L\n"
	"  End\n"
	"End\n"
	"Object Flam\n"
	"  Behavior = FlammableUpdate ModuleTag_F\n"
	"  End\n"
	"End\n"
	"Object Two\n"
	"  Behavior = LifetimeUpdate ModuleTag_L\n"
	"  End\n"
	"  Behavior = FlammableUpdate ModuleTag_F\n"
	"  End\n"
	"End\n";

struct Fx : LogicWorld
{
	Fx()
	{
		REQUIRE_MESSAGE(loadError.empty(), loadError);
		const std::string err = w.load(kObjects);
		REQUIRE_MESSAGE(err.empty(), err);
	}
	ScriptModule *moduleOf(Object *o, const char *cls) { return dynamic_cast<ScriptModule *>(o->findModule(cls)); }
	UpdateModule *updateOf(Object *o, const char *cls) { return dynamic_cast<UpdateModule *>(o->findModule(cls)); }
};
} // namespace

TEST_CASE("logic scheduler: registerObject appends at the list tail, files modules by phase and sleeping, ids count from 1 (RW 0x62BD6A)")
{
	Fx f;
	auto s = f.bind("LifetimeUpdate");
	Object *a = f.make("One");
	Object *b = f.make("One");
	CHECK(a->getID() == 1);
	CHECK(b->getID() == 2);
	CHECK(f.logic->getFirstObject() == a);
	CHECK(f.logic->getLastObject() == b);
	CHECK(a->getNextObject() == b);
	CHECK(b->getPrevObject() == a);
	CHECK(f.logic->getObjectCount() == 2);
	CHECK(f.logic->findObjectByID(2) == b);
	CHECK(f.logic->findObjectByID(3) == nullptr);
	// four helpers per plain object (SMC, Recovery, Defection, Guarding), all sleeping; the scripted module is due from frame 1
	REQUIRE(a->modules().size() == 5);
	CHECK(f.logic->sleepingVector().size() == 8);
	REQUIRE(f.logic->updateVector(PHASE_NORMAL).size() == 2);
	UpdateModule *ua = f.updateOf(a, "LifetimeUpdate");
	CHECK(ua->friend_getNextCallFrame() == 1); // frame 0 registers with now = max(frame, 1)
	CHECK(ua->friend_getPhaseInLogic() == PHASE_NORMAL);
	CHECK(ua->friend_getIndexInLogic() == 0);
	CHECK(f.updateOf(b, "LifetimeUpdate")->friend_getIndexInLogic() == 1);
	for (size_t i = 0; i < f.logic->sleepingVector().size(); ++i)
	{
		CHECK(f.logic->sleepingVector()[i]->friend_getIndexInLogic() == (int)i);
		CHECK(f.logic->sleepingVector()[i]->friend_getPhaseInLogic() == -1);
		CHECK(f.logic->sleepingVector()[i]->friend_getNextCallFrame() == (UnsignedInt)UPDATE_SLEEP_FOREVER);
	}
}

TEST_CASE("logic scheduler: the frame advances at the start of phase 1 and a module runs once per due frame (RW 0x62E577, 0x62EA34)")
{
	Fx f;
	auto s = f.bind("LifetimeUpdate");
	s->update = [](Object &, GameLogic &) { return UPDATE_SLEEP(3); };
	Object *a = f.make("One");
	ScriptModule *m = f.moduleOf(a, "LifetimeUpdate");
	CHECK(f.logic->getFrame() == 0);
	f.logic->update(1);
	CHECK(f.logic->getFrame() == 1); // the RotWK order: ++frame inside phase 1, before the rest of the phase
	CHECK(f.logic->getLastPhase() == 1);
	for (int phase = 2; phase <= 6; ++phase)
	{
		f.logic->update(phase);
		CHECK(f.logic->getFrame() == 1);
	}
	CHECK(m->calls == 1);
	CHECK(m->lastFrame == 1);
	CHECK(m->friend_getNextCallFrame() == 4); // frame + sleep
	f.logic->runLogicFrame();                 // frame 2
	f.logic->runLogicFrame();                 // frame 3
	CHECK(m->calls == 1);
	f.logic->runLogicFrame();                 // frame 4
	CHECK(m->calls == 2);
	CHECK(m->lastFrame == 4);
	CHECK(f.log.has("update:LifetimeUpdate#1@1.5")); // NORMAL modules run in phase 5
	CHECK(f.log.has("update:LifetimeUpdate#1@4.5"));
}

TEST_CASE("logic scheduler: a sleep below 1 is clamped to 1, FOREVER saturates (RW 0x62EAA4, 0x62EAC1)")
{
	Fx f;
	auto s = f.bind("LifetimeUpdate");
	int mode = 0;
	s->update = [&](Object &, GameLogic &) {
		return mode == 0 ? UPDATE_SLEEP_INVALID : UPDATE_SLEEP_FOREVER;
	};
	Object *a = f.make("One");
	ScriptModule *m = f.moduleOf(a, "LifetimeUpdate");
	f.logic->runLogicFrame();
	CHECK(m->friend_getNextCallFrame() == 2); // 0 -> NONE (1): frame 1 + 1
	mode = 1;
	f.logic->runLogicFrame();
	CHECK(m->friend_getNextCallFrame() == (UnsignedInt)UPDATE_SLEEP_FOREVER);
}

TEST_CASE("logic scheduler: updates[0] is split at size / 2 between phases 3 and 4, each module runs once (RW 0x62E9D4 .. 0x62EA1E)")
{
	Fx f;
	auto s = f.bind("LifetimeUpdate");
	s->phase = PHASE_INITIAL;
	// one module: size / 2 = 0 so phase 3 stops at once, phase 4 starts at 0
	f.make("One");
	f.logic->update(1);
	f.logic->update(2);
	f.logic->update(3);
	CHECK_FALSE(f.log.has("update:LifetimeUpdate#1@1.3"));
	f.logic->update(4);
	CHECK(f.log.has("update:LifetimeUpdate#1@1.4"));
	// two more: [A, B, C] (size 3, half 1): phase 3 runs A, phase 4 runs B and C
	f.make("One");
	f.make("One");
	f.log.events.clear();
	f.logic->runLogicFrame(); // frame 2
	std::vector<std::string> run;
	for (const std::string &e : f.log.events)
	{
		if (e.rfind("update:", 0) == 0)
		{
			run.push_back(e);
		}
	}
	REQUIRE(run.size() == 3);
	CHECK(run[0] == "update:LifetimeUpdate#1@2.3");
	CHECK(run[1] == "update:LifetimeUpdate#2@2.4");
	CHECK(run[2] == "update:LifetimeUpdate#3@2.4");
}

TEST_CASE("logic scheduler: the phase 3 stop is size() / 2 of the CURRENT size: a module woken during phase 3 moves the midpoint (RW 0x62EA10)")
{
	Fx f;
	auto s = f.bind("LifetimeUpdate");
	s->phase = PHASE_INITIAL;
	auto sf = f.bind("FlammableUpdate");
	sf->phase = PHASE_INITIAL;
	sf->initialNextCall = (UnsignedInt)UPDATE_SLEEP_FOREVER; // starts in the sleeping vector
	Object *a = f.make("One");
	f.make("One");
	f.make("One");
	Object *sleeperObj = f.make("Flam");
	ScriptModule *sleeper = f.moduleOf(sleeperObj, "FlammableUpdate");
	REQUIRE(f.logic->updateVector(PHASE_INITIAL).size() == 3);
	REQUIRE(sleeper->friend_getPhaseInLogic() == -1);
	// A's update wakes the sleeper: it is appended to updates[0] (size 3 -> 4) while phase 3 is running
	s->update = [&](Object &self, GameLogic &) {
		if (&self == a && sleeper->friend_getPhaseInLogic() == -1)
		{
			sleeper->wake(UPDATE_SLEEP_NONE);
		}
		return UPDATE_SLEEP_NONE;
	};
	f.log.events.clear();
	f.logic->update(1);
	f.logic->update(2);
	f.logic->update(3);
	// with the size read once at the start (3 / 2 = 1) phase 3 would run A only; reading it each iteration (4 / 2 = 2) it runs A and B
	std::vector<std::string> ran;
	for (const std::string &e : f.log.events)
	{
		if (e.rfind("update:", 0) == 0)
		{
			ran.push_back(e);
		}
	}
	REQUIRE(ran.size() == 2);
	CHECK(ran[0] == "update:LifetimeUpdate#1@1.3");
	CHECK(ran[1] == "update:LifetimeUpdate#2@1.3");
	CHECK(sleeper->friend_getPhaseInLogic() == PHASE_INITIAL);
	CHECK(sleeper->friend_getNextCallFrame() == 2);
	f.log.events.clear();
	f.logic->update(4); // starts at size / 2 = 2: C; the woken module is not due until frame 2
	REQUIRE(f.log.events.size() >= 1);
	CHECK(f.log.has("update:LifetimeUpdate#3@1.4"));
	CHECK_FALSE(f.log.has("update:FlammableUpdate#4@1.4"));
}

TEST_CASE("logic scheduler: FOREVER modules leave updates[] by the reverse swap-pop scan in phase 4 / 5 / 6, never in phase 3 (RW 0x62EADF)")
{
	Fx f;
	auto s = f.bind("LifetimeUpdate");
	s->phase = PHASE_INITIAL;
	Object *a = f.make("One");
	Object *b = f.make("One");
	Object *c = f.make("One");
	s->update = [&](Object &self, GameLogic &) { return &self == a ? UPDATE_SLEEP_FOREVER : UPDATE_SLEEP_NONE; };
	const size_t helperCount = f.logic->sleepingVector().size(); // 12
	REQUIRE(helperCount == 12);
	UpdateModule *ua = f.updateOf(a, "LifetimeUpdate");
	UpdateModule *ub = f.updateOf(b, "LifetimeUpdate");
	UpdateModule *uc = f.updateOf(c, "LifetimeUpdate");
	f.logic->update(1);
	f.logic->update(2);
	f.logic->update(3); // runs A only (size 3, half 1)
	CHECK(ua->friend_getNextCallFrame() == (UnsignedInt)UPDATE_SLEEP_FOREVER);
	CHECK(f.logic->updateVector(PHASE_INITIAL).size() == 3); // not migrated in phase 3
	f.logic->update(4);                                       // B and C run, then the reverse scan moves A
	REQUIRE(f.logic->updateVector(PHASE_INITIAL).size() == 2);
	CHECK(f.logic->updateVector(PHASE_INITIAL)[0] == uc); // swap-pop: the last module takes A's slot
	CHECK(f.logic->updateVector(PHASE_INITIAL)[1] == ub);
	CHECK(uc->friend_getIndexInLogic() == 0);
	CHECK(uc->friend_getPhaseInLogic() == PHASE_INITIAL);
	CHECK(f.logic->sleepingVector().size() == helperCount + 1);
	CHECK(f.logic->sleepingVector().back() == ua);
	CHECK(ua->friend_getIndexInLogic() == (int)helperCount);
	CHECK(ua->friend_getPhaseInLogic() == -1);
}

TEST_CASE("logic scheduler: friend_awakenUpdateModule rules (RW 0x62B921)")
{
	Fx f;
	auto s = f.bind("LifetimeUpdate");
	auto s2 = f.bind("FlammableUpdate");
	s2->initialNextCall = (UnsignedInt)UPDATE_SLEEP_FOREVER;
	Object *o = f.make("Two");
	UpdateModule *life = f.updateOf(o, "LifetimeUpdate");
	UpdateModule *flam = f.updateOf(o, "FlammableUpdate");
	REQUIRE(flam->friend_getPhaseInLogic() == -1);
	const size_t sleepBefore = f.logic->sleepingVector().size(); // 4 helpers + flam
	f.logic->update(1);                                          // frame 1; life is due at 1
	CHECK(life->friend_getNextCallFrame() == 1);
	// same-frame guard: next == now and when == now + 1 is ignored
	f.logic->friend_awakenUpdateModule(o, life, 2);
	CHECK(life->friend_getNextCallFrame() == 1);
	// when equals the next call frame: ignored
	f.logic->friend_awakenUpdateModule(o, life, 1);
	CHECK(life->friend_getNextCallFrame() == 1);
	// anything else sets it
	f.logic->friend_awakenUpdateModule(o, life, 9);
	CHECK(life->friend_getNextCallFrame() == 9);
	// a sleeping module with when < FOREVER is swap-removed from sleeping and appended to its phase vector
	const size_t normalBefore = f.logic->updateVector(PHASE_NORMAL).size();
	f.logic->friend_awakenUpdateModule(o, flam, 7);
	CHECK(flam->friend_getPhaseInLogic() == PHASE_NORMAL);
	CHECK(flam->friend_getIndexInLogic() == (int)normalBefore);
	CHECK(flam->friend_getNextCallFrame() == 7);
	CHECK(f.logic->updateVector(PHASE_NORMAL).back() == flam);
	CHECK(f.logic->sleepingVector().size() == sleepBefore - 1);
	for (size_t i = 0; i < f.logic->sleepingVector().size(); ++i)
	{
		CHECK(f.logic->sleepingVector()[i]->friend_getIndexInLogic() == (int)i); // the moved entry's index was repaired
	}
	// a module already in a phase vector asked to sleep forever stays there (migration happens in the phase loops)
	f.logic->friend_awakenUpdateModule(o, flam, (UnsignedInt)UPDATE_SLEEP_FOREVER + 5);
	CHECK(flam->friend_getNextCallFrame() == (UnsignedInt)UPDATE_SLEEP_FOREVER); // clamped
	CHECK(flam->friend_getPhaseInLogic() == PHASE_NORMAL);
}

TEST_CASE("logic scheduler: a module woken by the module that is running is ignored (u == current, RW 0x62B934)")
{
	Fx f;
	auto s = f.bind("LifetimeUpdate");
	Object *o = f.make("One");
	ScriptModule *m = f.moduleOf(o, "LifetimeUpdate");
	s->update = [&](Object &, GameLogic &) {
		m->wake(UPDATE_SLEEP_FOREVER); // would sleep it forever if not ignored
		return UPDATE_SLEEP(2);
	};
	f.logic->runLogicFrame();
	CHECK(m->friend_getNextCallFrame() == 3); // the update's own return value decides: frame 1 + 2
}

TEST_CASE("logic scheduler: an update before registerObject only sets the frame (the not-in-list branch, RW 0x62BA4D)")
{
	Fx f;
	auto s = f.bind("LifetimeUpdate");
	s->onObjectCreatedHook = [](ScriptModule &m) { m.wake(UPDATE_SLEEP(5)); }; // the object is not in the list yet (frame 0)
	Object *o = f.make("One");
	UpdateModule *u = f.updateOf(o, "LifetimeUpdate");
	CHECK(u->friend_getNextCallFrame() == 5); // registerObject keeps a non-zero frame
	CHECK(u->friend_getPhaseInLogic() == PHASE_NORMAL);
}

TEST_CASE("logic scheduler: disabled objects skip modules that do not process their disabled types; the sleep is NONE (RW 0x62EA46 .. 0x62EA7A)")
{
	Fx f;
	auto s = f.bind("LifetimeUpdate");
	auto s2 = f.bind("FlammableUpdate");
	s2->processWhenDisabled = 1u << 3;
	Object *o = f.make("Two");
	o->setDisabled(3, 100);
	ScriptModule *life = f.moduleOf(o, "LifetimeUpdate");
	ScriptModule *flam = f.moduleOf(o, "FlammableUpdate");
	f.logic->runLogicFrame();
	CHECK(life->calls == 0);
	CHECK(life->friend_getNextCallFrame() == 2); // frame 1 + NONE
	CHECK(flam->calls == 1);                     // its mask intersects the object's
	o->clearDisabled(3);
	f.logic->runLogicFrame();
	CHECK(life->calls == 1);
}

TEST_CASE("logic scheduler: disabled types expire at the end of phase 1 when their frame is no later than the frame (B1 0x1C5780, RW 0x690A42)")
{
	Fx f;
	Object *o = f.make("Plain");
	o->setDisabled(2, 3);
	f.logic->runLogicFrame(); // frame 1
	f.logic->runLogicFrame(); // frame 2
	CHECK(o->getDisabledMask() == (1u << 2));
	f.logic->runLogicFrame(); // frame 3: expiry 3 <= 3
	CHECK(o->getDisabledMask() == 0);
}

TEST_CASE("logic scheduler: a DESTROYED object's modules sleep forever (RW 0x62EA85)")
{
	Fx f;
	auto s = f.bind("LifetimeUpdate");
	Object *o = f.make("One");
	ScriptModule *m = f.moduleOf(o, "LifetimeUpdate");
	o->setStatus(OBJECT_STATUS_DESTROYED, true);
	f.logic->runLogicFrame();
	CHECK(m->calls == 0);
	CHECK(m->friend_getNextCallFrame() == (UnsignedInt)UPDATE_SLEEP_FOREVER);
	CHECK(m->friend_getPhaseInLogic() == -1); // migrated by the phase 5 scan
}

TEST_CASE("logic scheduler: the status expiry checks clear IGNORE_AI_COMMAND and NO_COLLISIONS once their frame is older (B1 0x1CE7B0, 0x1CE7F0)")
{
	Fx f;
	Object *o = f.make("Plain");
	o->setStatus(OBJECT_STATUS_IGNORE_AI_COMMAND, true);
	o->setIgnoreAICommandUntil(2);
	o->setStatus(OBJECT_STATUS_NO_COLLISIONS, true);
	o->setNoCollisionsUntil(3);
	f.logic->runLogicFrame(); // 1
	f.logic->runLogicFrame(); // 2: 2 < 2 false
	CHECK(o->testStatus(OBJECT_STATUS_IGNORE_AI_COMMAND));
	f.logic->runLogicFrame(); // 3: 2 < 3 clears AI; 3 < 3 false
	CHECK_FALSE(o->testStatus(OBJECT_STATUS_IGNORE_AI_COMMAND));
	CHECK(o->testStatus(OBJECT_STATUS_NO_COLLISIONS));
	f.logic->runLogicFrame(); // 4
	CHECK_FALSE(o->testStatus(OBJECT_STATUS_NO_COLLISIONS));
}

TEST_CASE("logic scheduler: phase 2 records every object's transform once per frame (RW 0x6260E1, 0x62E95F)")
{
	Fx f;
	Object *o = f.make("Plain");
	Coord3D p{ 10, 20, 0 };
	o->setPosition(&p);
	f.logic->runLogicFrame(); // frame 1: first record: previous = current
	CHECK(o->hasRecordedTransform());
	CHECK(o->getRecordedFrame() == 1);
	CHECK(o->getRecordedPosition().x == 10);
	CHECK(o->getPreviousPosition().x == 10);
	Coord3D q{ 30, 40, 0 };
	o->setPosition(&q); // moved by "frame 1's updates"
	f.logic->runLogicFrame(); // frame 2: previous = what was recorded (10), recorded = current (30)
	CHECK(o->getRecordedFrame() == 2);
	CHECK(o->getRecordedPosition().x == 30);
	CHECK(o->getPreviousPosition().x == 10);
	// a second call within the frame does nothing (the frame matches)
	f.logic->update(2);
	CHECK(o->getRecordedFrame() == 2);
}

namespace
{
// An independent model of "vector + swap-pop" removal: erase `victims` in order, fixing nothing else.
std::vector<int> swapPopModel(std::vector<int> v, const std::vector<int> &victims)
{
	for (int victim : victims)
	{
		for (size_t i = 0; i < v.size(); ++i)
		{
			if (v[i] == victim)
			{
				v[i] = v.back();
				v.pop_back();
				break;
			}
		}
	}
	return v;
}
} // namespace

TEST_CASE("logic destruction: destroyObject queues, marks DESTROYED and runs the destroy and delete callbacks at once; the object leaves in phase 5 (spec 5.5)")
{
	Fx f;
	auto s = f.bind("LifetimeUpdate");
	s->hasDestroy = true;
	f.logic->setClientHooks(&f.hooks);
	Object *x = f.make("One");
	Object *y = f.make("One");
	Object *z = f.make("One");
	f.log.events.clear();
	// identify modules by id: the vectors hold UpdateModule pointers; map them to a number for the model
	std::map<const UpdateModule *, int> label;
	int next = 0;
	for (const UpdateModule *u : f.logic->sleepingVector())
	{
		label[u] = next++;
	}
	std::vector<int> sleepModel;
	for (size_t i = 0; i < f.logic->sleepingVector().size(); ++i)
	{
		sleepModel.push_back((int)i);
	}
	std::vector<int> yHelpers;
	for (const std::unique_ptr<BehaviorModule> &m : y->modules())
	{
		if (UpdateModule *u = m->asUpdateModule())
		{
			if (label.count(u))
			{
				yHelpers.push_back(label[u]);
			}
		}
	}
	REQUIRE(yHelpers.size() == 4);
	const UpdateModule *yLife = f.updateOf(y, "LifetimeUpdate");
	const UpdateModule *zLife = f.updateOf(z, "LifetimeUpdate");
	const UpdateModule *xLife = f.updateOf(x, "LifetimeUpdate");

	f.logic->destroyObject(y);
	CHECK(y->isDestroyed());
	CHECK(f.log.has("onDestroy:LifetimeUpdate"));
	CHECK(f.log.has("onDelete:LifetimeUpdate"));
	CHECK(f.logic->destroyQueueSize() == 1);
	CHECK(f.logic->findObjectByID(2) == y); // still registered
	f.logic->destroyObject(y);              // a second request is ignored
	CHECK(f.logic->destroyQueueSize() == 1);
	CHECK(f.log.count("onDestroy:LifetimeUpdate") == 1);

	f.logic->update(1);
	f.logic->update(2);
	f.logic->update(3);
	f.logic->update(4);
	CHECK(f.logic->findObjectByID(2) == y); // phase 5 deletes
	f.logic->update(5);
	CHECK(f.logic->findObjectByID(2) == nullptr);
	CHECK(f.logic->getObjectCount() == 2);
	CHECK(f.logic->getFirstObject() == x);
	CHECK(x->getNextObject() == z);
	CHECK(z->getPrevObject() == x);
	CHECK(f.log.has("client:destroyed#2"));
	CHECK(f.log.count("update:LifetimeUpdate#2@1.5") == 0); // a destroyed object's modules never run
	// the scheduler: updates[2] was [x.L, y.L, z.L]; removing y.L moves z.L into its slot
	REQUIRE(f.logic->updateVector(PHASE_NORMAL).size() == 2);
	CHECK(f.logic->updateVector(PHASE_NORMAL)[0] == xLife);
	CHECK(f.logic->updateVector(PHASE_NORMAL)[1] == zLife);
	CHECK(zLife->friend_getIndexInLogic() == 1);
	(void)yLife;
	// sleeping: the four helpers of y left by swap-pop, in module order (the model above)
	// y's LifetimeUpdate was due in phase 5; DESTROYED makes it sleep forever, so the phase 5 scan moved it into `sleeping` (label 12, at the
	// end) before processDestroyList removed all of y's modules in module order
	sleepModel.push_back(12);
	yHelpers.push_back(12);
	const std::vector<int> expect = swapPopModel(sleepModel, yHelpers);
	REQUIRE(f.logic->sleepingVector().size() == expect.size());
	for (size_t i = 0; i < expect.size(); ++i)
	{
		CHECK(label[f.logic->sleepingVector()[i]] == expect[i]);
		CHECK(f.logic->sleepingVector()[i]->friend_getIndexInLogic() == (int)i);
	}
}

TEST_CASE("logic destruction: an object destroyed by a module update in phase 5 is deleted at the end of that same phase (RW 0x62EBB1)")
{
	Fx f;
	auto s = f.bind("LifetimeUpdate");
	Object *victim = f.make("Plain");
	Object *killer = f.make("One");
	const ObjectID victimId = victim->getID();
	s->update = [&](Object &self, GameLogic &logic) {
		if (&self == killer)
		{
			logic.destroyObject(victim);
		}
		return UPDATE_SLEEP_NONE;
	};
	for (int phase = 1; phase <= 4; ++phase)
	{
		f.logic->update(phase);
		CHECK(f.logic->findObjectByID(victimId) == victim);
	}
	f.logic->update(5);
	CHECK(f.logic->findObjectByID(victimId) == nullptr);
}

TEST_CASE("logic destruction: objects queued during the pass are processed by the same pass (RW 0x62A2C9: the list stays live)")
{
	Fx f;
	auto s = f.bind("LifetimeUpdate");
	Object *a = f.make("One");
	Object *b = f.make("Plain");
	Object *c = f.make("Plain");
	const ObjectID idB = b->getID(), idC = c->getID();
	s->onDeleteHook = [&](ScriptModule &m) {
		if (m.getObject() == a)
		{
			f.logic->destroyObject(b); // a's deletion destroys b ...
			f.logic->destroyObject(c); // ... and c
		}
	};
	f.logic->destroyObject(a);
	CHECK(f.logic->destroyQueueSize() == 3);
	f.logic->processDestroyList();
	CHECK(f.logic->getObjectCount() == 0);
	CHECK(f.logic->findObjectByID(idB) == nullptr);
	CHECK(f.logic->findObjectByID(idC) == nullptr);
	CHECK(f.logic->destroyQueueSize() == 0);
}

TEST_CASE("logic destruction: reset deletes every object and restarts the frame and the id counter")
{
	Fx f;
	f.bind("LifetimeUpdate");
	f.make("One");
	f.make("Two");
	f.logic->runLogicFrame();
	f.logic->reset();
	CHECK(f.logic->getObjectCount() == 0);
	CHECK(f.logic->getFrame() == 0);
	CHECK(f.logic->updateVector(PHASE_NORMAL).empty());
	CHECK(f.logic->sleepingVector().empty());
	CHECK(f.make("One")->getID() == 1);
}

TEST_CASE("logic scheduler: the phase work table names every row, pins the unported ones (stop S-143) and takes installed functions")
{
	Fx f;
	std::vector<std::string> builtin, unported;
	for (const GameLogic::PhaseWork &w : f.logic->phaseWork())
	{
		(w.builtin ? builtin : unported).push_back(w.name);
	}
	CHECK(builtin == std::vector<std::string>{ "frameAdvance", "recordTransforms", "scheduler", "processDestroyList", "endOfFrameObjectChecks", "updatePendingDamage" });
	CHECK(unported == std::vector<std::string>{ "logicDebugFrame", "freezeGate", "commandOnlyTransition", "frame2ObjectBlock", "pathfinderQueue", "subsystemsPhase1", "logicCrc",
		"recorderAndStats", "commandList", "drawableCallback", "partitionAndCollision", "aiUpdate", "deferredEntriesDrain", "subsystemsBeforeDestroy",
		"subsystemsAfterDestroy", "logicTimeNotify" });
	CHECK(f.logic->unportedPhaseWork().size() == unported.size());
	int ran = 0;
	CHECK(f.logic->installPhaseWork("commandList", [&] { ++ran; }));
	CHECK_FALSE(f.logic->installPhaseWork("scheduler", [] {}));        // builtin rows are not replaceable
	CHECK_FALSE(f.logic->installPhaseWork("noSuchRow", [] {}));
	f.logic->runLogicFrame();
	CHECK(ran == 1); // phase 1 only
	CHECK(f.logic->unportedPhaseWork().size() == unported.size() - 1);
}

TEST_CASE("logic hash: two identical runs give the same state hash every frame; any state change changes it")
{
	auto run = [](unsigned seed, int extraMoney) {
		Fx f;
		auto s = f.bind("LifetimeUpdate");
		s->update = [](Object &, GameLogic &logic) {
			(void)logic.random().getValue(0, 99, "test", 1);
			return UPDATE_SLEEP_NONE;
		};
		f.logic->random().seedRandom(seed);
		f.make("One", f.teamOf("Alice"));
		f.make("Two", f.teamOf("Bob"));
		f.make("Plain");
		f.players.findPlayerWithName("Alice")->getMoney()->deposit((unsigned)extraMoney);
		std::vector<std::uint32_t> hashes;
		for (int i = 0; i < 12; ++i)
		{
			f.logic->runLogicFrame();
			hashes.push_back(f.logic->computeStateHash());
		}
		return hashes;
	};
	const std::vector<std::uint32_t> a = run(7, 0), b = run(7, 0), c = run(8, 0), d = run(7, 1);
	CHECK(a == b);
	CHECK(a != c);
	CHECK(a != d);
	for (size_t i = 1; i < a.size(); ++i)
	{
		CHECK(a[i] != a[i - 1]); // the frame is in the hash
	}
}

TEST_CASE("logic hash: the state hash does not depend on the order name keys were first seen in or on where objects live in memory")
{
	auto run = [](bool perturb) {
		LogicWorld f0; // not used: only to keep the fixture style symmetric
		(void)f0;
		Fx f;
		if (perturb)
		{
			for (int i = 0; i < 500; ++i)
			{
				f.w.keys.nameToKey("PerturbKey" + std::to_string(i * 7919 % 500)); // different key numbers for every name made afterwards
			}
		}
		std::vector<std::unique_ptr<int[]>> ballast;
		for (int i = 0; perturb && i < 200; ++i)
		{
			ballast.push_back(std::unique_ptr<int[]>(new int[1 + (i * 37) % 301])); // moves the heap: pointer values differ between the runs
		}
		auto s = f.bind("LifetimeUpdate");
		s->update = [](Object &, GameLogic &logic) {
			(void)logic.random().getValue(0, 9, "t", 1);
			return UPDATE_SLEEP_NONE;
		};
		f.make("One", f.teamOf("Alice"));
		f.make("Two", f.teamOf("Bob"));
		std::vector<std::uint32_t> hashes;
		for (int i = 0; i < 6; ++i)
		{
			f.logic->runLogicFrame();
			hashes.push_back(f.logic->computeStateHash());
		}
		return hashes;
	};
	CHECK(run(false) == run(true));
}
