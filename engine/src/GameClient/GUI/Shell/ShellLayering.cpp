// OpenBFME. GPL-3.0.
// See GameClient/GUI/Shell/ShellLayering.h.

#include "GameClient/GUI/Shell/ShellLayering.h"

#include "GameClient/GUI/AptScreen.h"
#include "GameClient/GUI/GameWindow.h"
#include "GameClient/GUI/Shell/Shell.h"
#include "GameClient/GUI/WindowManager.h"
#include "Libraries/Source/Apt/Apt.h"
#include "Libraries/Source/Apt/AptCharacterInst.h"
#include "Libraries/Source/Apt/AptRenderList.h"

#include <algorithm>
#include <map>
#include <set>

namespace
{
struct TextBox
{
	std::string text;
	std::string unit;
	float r[4];
};

int levelOfPath(const std::string &path)
{
	// "_level2.OpenPlay.x" -> 2
	if (path.rfind("_level", 0) != 0)
	{
		return -1;
	}
	int n = 0;
	std::size_t i = 6;
	if (i >= path.size() || path[i] < '0' || path[i] > '9')
	{
		return -1;
	}
	while (i < path.size() && path[i] >= '0' && path[i] <= '9')
	{
		n = n * 10 + (path[i] - '0');
		++i;
	}
	return n;
}

bool stageRect(const AptRenderCommand &c, float out[4])
{
	const float xs[4] = { c.bounds[0], c.bounds[2], c.bounds[2], c.bounds[0] };
	const float ys[4] = { c.bounds[1], c.bounds[1], c.bounds[3], c.bounds[3] };
	out[0] = out[1] = 1e30f;
	out[2] = out[3] = -1e30f;
	for (int i = 0; i < 4; ++i)
	{
		float x, y;
		c.matrix.apply(xs[i], ys[i], x, y);
		out[0] = std::min(out[0], x);
		out[1] = std::min(out[1], y);
		out[2] = std::max(out[2], x);
		out[3] = std::max(out[3], y);
	}
	return out[2] > out[0] && out[3] > out[1];
}

bool blank(const std::string &s)
{
	return s.find_first_not_of(" \t\r\n") == std::string::npos;
}
} // namespace

ShellLayeringReport checkShellLayering(WindowManager &windows, const Shell &shell, const AptRenderList &renderList)
{
	ShellLayeringReport r;
	r.componentInputBlocked = windows.componentInputBlocked();
	const AptScreen *top = shell.top();
	r.topScreen = top ? top->filename() : std::string();
	const int topLevel = top ? top->level() : -1;

	// the sub-movie a path lies in: the outermost clip below the level's root that became a loaded movie (loadMovie into a clip); "" for the
	// level's own timeline
	Apt &apt = windows.apt();
	std::map<std::string, bool> isMovie;
	auto clipIsMovie = [&](const std::string &prefix) {
		auto it = isMovie.find(prefix);
		if (it != isMovie.end())
		{
			return it->second;
		}
		bool movie = false;
		const int level = levelOfPath(prefix);
		const std::size_t dot = prefix.find('.');
		if (level >= 0 && dot != std::string::npos)
		{
			const AptCharacterInst *inst = apt.resolvePath(apt.level(level), prefix.substr(dot + 1));
			movie = inst && inst->type() == AptCharacterInst::Type::Movie;
		}
		isMovie.emplace(prefix, movie);
		return movie;
	};
	auto movieOf = [&](const std::string &path) {
		std::size_t dot = path.find('.');
		while (dot != std::string::npos)
		{
			const std::size_t next = path.find('.', dot + 1);
			const std::string prefix = path.substr(0, next);
			if (clipIsMovie(prefix))
			{
				return prefix;
			}
			dot = next;
		}
		return std::string();
	};

	std::set<int> drawnLevels;
	std::set<std::string> drawnMovies;
	std::vector<TextBox> texts;
	for (const AptRenderCommand &c : renderList.commands)
	{
		if ((c.kind != AptRenderCommand::Kind::Shape && c.kind != AptRenderCommand::Kind::Text && c.kind != AptRenderCommand::Kind::Placeholder) || c.color.mul[3] <= 0.0f)
		{
			continue;
		}
		drawnLevels.insert(c.level);
		if (c.level != topLevel)
		{
			continue;
		}
		const std::string movie = movieOf(c.path);
		if (!movie.empty())
		{
			drawnMovies.insert(movie);
		}
		if (c.kind == AptRenderCommand::Kind::Text)
		{
			const std::string shown = !c.variable.empty() ? "{" + c.variable + "}" + c.text : c.text;
			TextBox t;
			if (!blank(c.text) && stageRect(c, t.r))
			{
				t.text = shown;
				t.unit = movie;
				texts.push_back(t);
			}
		}
	}
	// the gadgets: a shown component window counts for its placeholder's level and sub-movie
	for (const WindowManager::ComponentRecord &rec : windows.components())
	{
		if (!rec.instance || !rec.window || rec.window->winIsHidden())
		{
			continue;
		}
		r.shownGadgets += 1;
		if (BitTest(rec.window->winGetStatus(), WIN_STATUS_ENABLED))
		{
			r.enabledGadgets += 1;
		}
		drawnLevels.insert(rec.level);
		if (rec.level == topLevel)
		{
			const std::string movie = movieOf(rec.instancePath);
			if (!movie.empty())
			{
				drawnMovies.insert(movie);
			}
		}
	}

	for (int i = 0; i < shell.screenCount(); ++i)
	{
		const AptScreen *s = shell.screenAt(i);
		if (s->level() >= 0 && drawnLevels.count(s->level()))
		{
			r.visibleScreens.push_back(s->filename());
			if (s != top)
			{
				r.problems.push_back("the covered screen " + s->filename() + " is drawn under " + r.topScreen);
			}
		}
	}
	r.visibleMovies.assign(drawnMovies.begin(), drawnMovies.end());
	if (r.visibleMovies.size() > 1)
	{
		std::string list;
		for (const std::string &m : r.visibleMovies)
		{
			list += (list.empty() ? "" : ", ") + m;
		}
		r.problems.push_back("two sub-screens of " + r.topScreen + " are drawn at once: " + list);
	}
	for (std::size_t i = 0; i < texts.size(); ++i)
	{
		for (std::size_t j = i + 1; j < texts.size(); ++j)
		{
			const TextBox &a = texts[i], &b = texts[j];
			if (a.text == b.text)
			{
				continue;
			}
			const float w = std::min(a.r[2], b.r[2]) - std::max(a.r[0], b.r[0]);
			const float h = std::min(a.r[3], b.r[3]) - std::max(a.r[1], b.r[1]);
			if (w <= 0 || h <= 0)
			{
				continue;
			}
			const float areaA = (a.r[2] - a.r[0]) * (a.r[3] - a.r[1]), areaB = (b.r[2] - b.r[0]) * (b.r[3] - b.r[1]);
			if (w * h > 0.5f * std::min(areaA, areaB))
			{
				r.overprints.push_back(a.text + " | " + b.text);
			}
		}
	}
	for (const std::string &o : r.overprints)
	{
		r.problems.push_back("texts overprint: " + o);
	}
	return r;
}
