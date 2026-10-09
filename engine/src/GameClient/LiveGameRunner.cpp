// OpenBFME. GPL-3.0.
//
// LiveGame's runner (lane SMOOTH-1, stop S-810): the render side's presentation (the presented frame and alpha, the client events in order, the poses
// from one snapshot) and the logic worker's thread management. CLIENT / HOST code: it never computes simulation state (the worker calls
// LiveGame::runFrameOwned, which is in LiveGame.cpp), so it is excluded from the simulation audit; its arithmetic is render time and alpha only.

#include "GameClient/LiveGame.h"

#include "Common/NumericState.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace
{
double secondsSince(const std::chrono::steady_clock::time_point &t0)
{
	return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
}
} // namespace

void LiveGame::present(double seconds)
{
	// the render side (the caller's thread): take what was published, apply the events of the presented frames in order, pose from one snapshot
	std::shared_ptr<const LogicSnapshot> latest, prev;
	{
		std::lock_guard<std::mutex> lock(m_pubMutex);
		latest = m_published;
		prev = m_publishedPrev;
		for (ClientEvent &e : m_publishedEvents)
		{
			m_heldEvents.push_back(std::move(e));
		}
		m_publishedEvents.clear();
		// events the main thread caused between frames (createObject, a destroy) when the logic is idle: the recorder is not in use by anyone else
		if (!m_threaded || (m_work.empty() && !m_running))
		{
			for (ClientEvent &e : m_recorder->take(false)) // lane ANIM-1: the flushes are the published frame's (ClientEventRecorder::take)
			{
				m_heldEvents.push_back(std::move(e));
			}
		}
	}
	if (!latest)
	{
		return;
	}
	// the presented time: the clock's, minus the presentation delay (in frames of 200 ms)
	double a = alpha();
	UnsignedInt target = m_dueFrame;
	const bool delayed = !m_options.sixTickPacing && m_options.presentationDelaySeconds > 0.0;
	if (delayed)
	{
		a -= m_options.presentationDelaySeconds * (double)LOGICFRAMES_PER_SECOND + m_extraDelayFrames;
		while (a < 0.0 && target > 0)
		{
			a += 1.0;
			--target;
		}
		if (a < 0.0)
		{
			a = 0.0;
		}
	}
	std::shared_ptr<const LogicSnapshot> shown;
	if (delayed)
	{
		// lane SMOOTH-2 (frame pacing, S-811): the presented time moves continuously. SMOOTH-1 held the last completed frame at its end while the worker was late and
		// then jumped to the clock's time (a freeze and a jump of the whole army: the "lag" the owner saw). Here the presented time follows the clock's at its
		// rate, eased towards it by at most a quarter of the render step (never backwards), stops at the end of the latest completed frame while the worker is
		// late, and each such late frame lengthens the presentation delay a little (decaying again while the worker keeps up), so a worker whose frames take
		// longer than the delay is soon presented without holds. A gap of more than two logic frames (a load, a stall) is not eased: the time is taken over.
		const double desired = (double)target + a;
		const double step = seconds * (double)LOGICFRAMES_PER_SECOND;
		if (!m_presentTimeValid || desired - m_presentTime > 2.0 || m_presentTime - desired > 2.0)
		{
			m_presentTime = desired;
			m_presentTimeValid = true;
		}
		else
		{
			double next = m_presentTime + step;
			const double err = desired - next;
			const double limit = 0.25 * step;
			next += err > limit ? limit : (err < -limit ? -limit : err);
			m_presentTime = next > m_presentTime ? next : m_presentTime;
		}
		const double available = (double)latest->frame + 1.0; // the end of the latest completed frame
		if (m_presentTime > available)
		{
			m_presentTime = available;
			++m_heldPresentations;
			m_extraDelayFrames = std::min(m_extraDelayFrames + kDelayGrowFrames, kMaxExtraDelayFrames);
		}
		else
		{
			m_extraDelayFrames = std::max(0.0, m_extraDelayFrames - step * kDelayDecayPerFrame);
		}
		double whole = std::floor(m_presentTime);
		target = (UnsignedInt)whole;
		a = m_presentTime - whole;
		if (target == latest->frame + 1u)
		{
			target = latest->frame; // exactly the end of the latest frame
			a = 1.0;
		}
		if (latest->frame == target)
		{
			shown = latest;
		}
		else if (prev && prev->frame == target)
		{
			shown = prev;
		}
		else if (prev && target < prev->frame)
		{
			shown = prev; // older than what is kept: the start of the oldest kept frame
			a = 0.0;
		}
		else
		{
			shown = latest; // (a snapshot newer than the presented time without its predecessor: its start)
			a = 0.0;
		}
	}
	else if (latest->frame == target)
	{
		shown = latest;
	}
	else if (latest->frame < target)
	{
		shown = latest; // the worker is late: hold its last completed frame at its end, never a partial one
		a = 1.0;
		++m_heldPresentations;
	}
	else if (prev && prev->frame == target)
	{
		shown = prev;
	}
	else
	{
		shown = latest; // (a snapshot newer than the presented time without its predecessor: its start)
		a = 0.0;
	}
	// the events of every frame up to the presented one, in order; later ones wait
	size_t n = 0;
	while (n < m_heldEvents.size() && m_heldEvents[n].frame <= shown->frame)
	{
		++n;
	}
	if (n > 0)
	{
		std::vector<ClientEvent> now(std::make_move_iterator(m_heldEvents.begin()), std::make_move_iterator(m_heldEvents.begin() + (std::ptrdiff_t)n));
		m_heldEvents.erase(m_heldEvents.begin(), m_heldEvents.begin() + (std::ptrdiff_t)n);
		m_drawables->applyEvents(now);
	}
	m_presented = shown;
	m_presentedAlpha = a;
	m_drawables->advance(seconds * 1000.0);
	m_drawables->syncTransforms(*shown, a, m_renderInterpolation);
}

double LiveGame::presentationExtraDelaySeconds() const
{
	return m_extraDelayFrames / (double)LOGICFRAMES_PER_SECOND;
}

double LiveGame::presentationExtraDelayMs() const
{
	return presentationExtraDelaySeconds() * 1000.0;
}

void LiveGame::refreshClient(double elapsedMs, double alpha)
{
	waitIdle();
	publish();
	std::vector<ClientEvent> events;
	std::shared_ptr<const LogicSnapshot> snap;
	{
		std::lock_guard<std::mutex> lock(m_pubMutex);
		events.swap(m_publishedEvents);
		snap = m_published;
	}
	for (ClientEvent &e : events)
	{
		m_heldEvents.push_back(std::move(e));
	}
	m_drawables->applyEvents(m_heldEvents);
	m_heldEvents.clear();
	m_presented = snap;
	m_presentedAlpha = alpha;
	m_dueFrame = snap->frame;
	m_drawables->advance(elapsedMs);
	m_drawables->syncTransforms(*snap, alpha, m_renderInterpolation);
}

UnsignedInt LiveGame::frame() const
{
	std::lock_guard<std::mutex> lock(m_pubMutex);
	return m_published ? m_published->frame : 0;
}

int LiveGame::presentedObjectPosition(ObjectID id, Coord3D &position) const
{
	const std::shared_ptr<const LogicSnapshot> snap = m_presented; // main thread (the presentation's own member)
	if (!snap)
	{
		return -1;
	}
	if (const ObjectSnapshot *rec = snap->find(id))
	{
		position = rec->position;
		return 1;
	}
	return id >= snap->nextObjectId ? -1 : 0; // ids only grow: below the snapshot's next id and absent means gone (also the highest id destroyed)
}

std::shared_ptr<const LogicSnapshot> LiveGame::latestSnapshot() const
{
	std::lock_guard<std::mutex> lock(m_pubMutex);
	return m_published;
}

std::vector<std::pair<UnsignedInt, std::uint32_t>> LiveGame::frameHashes() const
{
	std::lock_guard<std::mutex> lock(m_pubMutex);
	return m_frameHashes;
}

size_t LiveGame::pendingClientEvents() const
{
	std::lock_guard<std::mutex> lock(m_pubMutex);
	return m_publishedEvents.size() + m_heldEvents.size();
}

bool LiveGame::logicIdle() const
{
	if (!m_threaded)
	{
		return true;
	}
	std::lock_guard<std::mutex> lock(m_pubMutex);
	return m_work.empty() && !m_running;
}

void LiveGame::waitIdle() const
{
	if (!m_threaded || std::this_thread::get_id() == m_workerId.load())
	{
		return;
	}
	std::unique_lock<std::mutex> lock(m_pubMutex);
	m_pubCv.wait(lock, [this] { return (m_work.empty() && !m_running) || m_workerError; });
	if (m_workerError)
	{
		std::rethrow_exception(m_workerError);
	}
}

void LiveGame::setLogicThread(bool enabled)
{
	if (enabled == m_threaded)
	{
		return;
	}
	if (enabled)
	{
		if (m_options.sixTickPacing || !m_loaded)
		{
			return; // six tick pacing runs its phases on the caller's thread
		}
		{
			std::lock_guard<std::mutex> lock(m_pubMutex);
			m_stop = false;
			m_work.clear();
			m_running = false;
		}
		m_threaded = true;
		m_worker = std::thread([this] { workerLoop(); });
		return;
	}
	waitIdle(); // the frames already requested run first: none is dropped
	stopWorker();
}

void LiveGame::stopWorker()
{
	if (!m_worker.joinable())
	{
		m_threaded = false;
		return;
	}
	{
		std::lock_guard<std::mutex> lock(m_pubMutex);
		m_stop = true;
	}
	m_pubCv.notify_all();
	m_worker.join();
	m_workerId.store(std::thread::id());
	m_threaded = false;
}

void LiveGame::workerLoop()
{
	m_workerId.store(std::this_thread::get_id());
	for (;;)
	{
		WorkItem item;
		{
			std::unique_lock<std::mutex> lock(m_pubMutex);
			m_pubCv.wait(lock, [this] { return m_stop || (!m_work.empty() && !m_workerError); });
			if (m_stop)
			{
				return; // shutdown / switch: requested frames were waited for by setLogicThread(false); at destruction the game ends
			}
			item = std::move(m_work.front()); // SMOOTH-1 + MP-1: the next numbered batch (or clock frame), in order, exactly once
			m_work.pop_front();
			m_running = true;
		}
		const auto t0 = std::chrono::steady_clock::now();
		try
		{
			const auto worldContext = m_world.enterContext();  // this thread's selection of the world's stores (thread-local chains)
			NumericState::normalizeFloatingPointEnvironment(); // the simulation's floating-point environment on this thread
			if (m_options.workerFrameDelayMs > 0)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(m_options.workerFrameDelayMs)); // SMOOTH-2 diagnostic: a slow frame
			}
			runFrameOwned(item);
		}
		catch (...)
		{
			std::lock_guard<std::mutex> lock(m_pubMutex);
			m_workerError = std::current_exception();
		}
		{
			std::lock_guard<std::mutex> lock(m_pubMutex);
			m_running = false;
			m_lastWorkerFrameMs = secondsSince(t0) * 1000.0;
		}
		m_pubCv.notify_all();
	}
}

