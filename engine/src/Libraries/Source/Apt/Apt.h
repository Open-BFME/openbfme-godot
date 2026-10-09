// OpenBFME. GPL-3.0.
//
// The EA Apt player (gApt, BFME2 global at 0x00E176D0): level slots, the per-tick update, the action pool and the
// interval timers.  Menus-apt.md steps A2 (display list, timeline, natives) and A3 (input).
//
// AptUpdate (Apt.cpp 0x00ACD7A0, `AptUpdate(ms)`), target facts read from the clean BFME2 1.06 game.dat:
//   - the elapsed time accumulates in the first level's root instance (+0x30); a step runs only when the
//     accumulated time reaches the movie's ms/frame (the character field at +0x24 of the lowest level);
//   - one step = interval timers with the frame time (0x00AE4150), the display-list sweep of every level
//     (0x00AF7A30: advance, each instance followed by its children), the action pool run (0x00AE6540), the
//     queued input events (0x00AFB910) and the pending loadMovie requests (0x00AD17F0); then the clock advances by
//     the frame time (0x00E176F0) and the loop repeats while time remains (unless a flag at 0x00E17708 is set);
//   - the carry stays in the root instance.
//
// The host side (fscommand, extern providers, loadMovie notification, trace) is the AptHost interface; this lane
// records it, later lanes implement it (Shell / WindowManager, A4+).

#pragma once

#include "Libraries/Source/Apt/AptActionInterpreter.h"
#include "Libraries/Source/Apt/AptCharacterInst.h"
#include "Libraries/Source/Apt/AptHost.h"
#include "Libraries/Source/Apt/AptLoad.h"

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

class AptInput;
class AptRenderList;

// One action pool entry (EA 0x18-byte records, AptAnimation.cpp).
struct AptAction
{
	enum Kind : std::uint8_t
	{
		Code = 1,     // a program: frame action or queued clip event; target = the instance
		Function = 2  // a function call: `function` called with this = target (member handlers)
	};
	Kind kind = Code;
	std::uint32_t eventMask = 0;
	std::uint32_t tag = 0;
	std::int32_t frameGuard = 0; // negative: the action is dropped when the sprite is no longer on frame -guard
	std::shared_ptr<const AptCodeBlock> code;
	std::shared_ptr<const AptFile> codeFile;
	AptValue function;
	AptValue thisValue;         // Function: `this` when the receiver is not a clip (Mouse / Key listener objects)
	AptCharacterInst *target = nullptr;
	std::int32_t argCount = 0;
	std::vector<AptValue> args; // Function: the call arguments (listener events pass the key code / wheel delta)
};

// setInterval / clearInterval (AptTimerFunc, 0x00AE4150).  EA keeps the entries in an array of 0x20-byte records.
struct AptTimer
{
	bool active = false;
	AptValue function;
	AptValue thisValue; // undefined: the function's defining clip is `this`
	float intervalMs = 0;
	float remainingMs = 0;
	std::vector<AptValue> params;
	AptCharacterInst *owner = nullptr; // the clip that defined the function; the timer is cleared with it (0x00AE44F0)
};

// Records the host-facing facts the player cannot express through AptHost.
struct AptNote
{
	std::string kind;    // short stable identity, e.g. "label-not-found"
	std::string detail;
};

class Apt
{
public:
	Apt(AptFileSource &source, AptHost &host);
	~Apt();

	Apt(const Apt &) = delete;
	Apt &operator=(const Apt &) = delete;

	AptGC &gc() { return m_gc; }
	AptActionInterpreter &vm() { return *m_vm; }
	AptLoader &loader() { return m_loader; }
	AptHost &host() { return m_userHost; }

	// ---- levels -------------------------------------------------------------------------------------------
	// Load `movieName` into `_level<level>` (replacing a movie already there): the root instance is created and its
	// first frame is processed at once (0x00AD1A4A).  Imports are resolved first; a missing import is an error.
	bool loadMovie(int level, const std::string &movieName, std::string *error);
	bool unloadLevel(int level);
	AptSpriteInst *level(int level) const;
	std::vector<int> loadedLevels() const;

	// ---- update -------------------------------------------------------------------------------------------
	// AptUpdate: advance by `elapsedMs`.  Returns the number of frames stepped.
	int update(int elapsedMs);
	// One frame step regardless of the accumulated time (tests, the engine's own pacing).
	void stepFrame(int frameMs);
	int msPerFrame() const; // of the lowest loaded level; 0 when nothing is loaded
	std::int64_t clockMs() const { return m_clockMs; }
	std::uint64_t frameCount() const { return m_frameCount; }

	// ---- mouse / stage state ------------------------------------------------------------------------------
	void mousePosition(float &x, float &y) const { x = m_mouseX; y = m_mouseY; }
	void setMousePosition(float x, float y) { m_mouseX = x; m_mouseY = y; }
	void noteBackgroundColor(const std::uint8_t rgba[4]);
	const std::uint8_t *backgroundColor() const { return m_background; }

	// ---- queueing (used by instances) ---------------------------------------------------------------------
	void pushAction(const AptAction &action);        // AptAnimation 0x00AE4B80 (back)
	void pushActionFront(const AptAction &action);   // 0x00AE4C70 (front)
	void pushFunctionCall(AptCharacterInst *ctx, const AptValue &function, std::int32_t argCount, std::uint32_t mask, std::uint32_t tag, bool front);
	void pushFunctionCallOn(const AptValue &thisValue, const AptValue &function, const std::vector<AptValue> &args, std::uint32_t mask, std::uint32_t tag, bool front);
	void noteNewInstance(AptCharacterInst *inst);    // gpPool->nNewInsts list (0x00AF8AD4)
	void dropActionsOf(AptCharacterInst *inst);      // 0x00AE4D50: queued actions of a destroyed instance
	// duplicateMovieClip / CloneSprite and removeMovieClip / RemoveSprite
	// `created` (optional) receives the new instance.
	bool duplicateClip(AptSpriteInst *context, const AptValue &source, const AptValue &newName, const AptValue &depth, AptCharacterInst **created = nullptr);
	// The one conversion of a script depth argument to a display-list depth (script depth + 0x4000), shared by attachMovie,
	// duplicateMovieClip, createEmptyMovieClip and swapDepths.  The sum is computed in 64 bits; a request whose display-list depth does
	// not fit 32 bits is reported (script error plus the note `script-depth-out-of-range`) and declined with false - what the retail
	// natives store for such a depth (a 32-bit wrap) was not read (S-104).
	bool scriptDepth(const AptValue &arg, const char *native, int &out);
	// The script depth of a display-list depth (depth - 0x4000, computed in 64 bits and wrapped to 32 as the binary's 32-bit subtraction does).
	static std::int32_t scriptDepthOf(int displayDepth);
	bool removeClipByTarget(AptSpriteInst *context, const AptValue &target);
	void runActions();                               // 0x00AE6540
	std::uint32_t currentTag() const { return m_tag; }

	// ---- scripts ------------------------------------------------------------------------------------------
	// Run a decoded program with `target` as this/timeline (queued frame actions, clip events, init actions).
	bool executeProgram(const std::shared_ptr<const AptCodeBlock> &code, AptCharacterInst *target);
	// Run a clip-event program as a function (immediate events: Initialize, Construct, Unload: 0x00AE2216 builds a
	// script function from the code and calls it with this = the instance).
	void callEventProgram(const std::shared_ptr<const AptCodeBlock> &code, AptCharacterInst *target);
	// Called by natives: the instance the running script belongs to (for relative loadMovie targets).
	AptCharacterInst *scriptTarget() const { return m_scriptTarget; }

	// Engine -> movie: call the function `function` of `scope` (a level root or any clip) with string arguments and return its
	// result as a string (BFME1 Rva00893410InvokeStrings.cpp / BfmeLevelPathAN.cpp: "OnFocus", "ShowInGameBackground",
	// "SetBarPercent" ...).  False + `error` when the member is missing or not a function.  The instances the call created run
	// their first frame before it returns (the new-instance flush that follows every action).
	bool invoke(AptCharacterInst *scope, const std::string &function, const std::vector<std::string> &args, std::string *result, std::string *error);
	// the same with typed arguments (lane UI-1: the message box's Show(type, isLarge) takes a boolean, RW 0x9530D6)
	bool invokeValues(AptCharacterInst *scope, const std::string &function, const std::vector<AptValue> &values, std::string *result, std::string *error);

	// The value of a clip path ("_root.a.b", "../c", "/x") relative to `base`; null when it does not resolve.
	AptCharacterInst *resolvePath(AptCharacterInst *base, const std::string &path);

	// A new instance of `ref` (not yet in any display list): the sprite/button/shape/text/opaque class by character type
	// (AptDisplayList::place 0x00AF8734-0x00AF8889).
	AptCharacterInst *createInstance(const AptCharRef &ref);

	// ---- movie data ---------------------------------------------------------------------------------------
	// Character `id` of `file` with imports resolved to the exporting movie.  False + error on failure.
	bool resolveCharacter(const std::shared_ptr<const AptFile> &file, std::uint32_t id, AptCharRef &out, std::string *error);
	const std::map<std::string, int> &labelsOf(const std::vector<AptFrame> *frames);
	// InitAction items (0x00B0F3D0 loop and 0x00AE6A30): run once, then the stored sprite id is negated in place.
	void runInitActionsFor(const std::shared_ptr<const AptFile> &file, std::uint32_t characterId, AptCharacterInst *target);
	void runFrameInitActions(const std::shared_ptr<const AptFile> &file, const AptFrame &frame, AptCharacterInst *target);

	// ---- timers -------------------------------------------------------------------------------------------
	std::int32_t setInterval(const AptValue &function, const AptValue &thisValue, float ms, const std::vector<AptValue> &params, AptCharacterInst *owner);
	bool clearInterval(std::int32_t id);
	void clearTimersOf(AptCharacterInst *owner);

	// ---- notes (stops, silent EA behaviours) --------------------------------------------------------------
	void note(const std::string &kind, const std::string &detail);
	const std::vector<AptNote> &notes() const { return m_notes; }
	std::size_t noteCount(const std::string &kind) const;

	// ---- native classes -----------------------------------------------------------------------------------
	AptObject *movieClipPrototype() const { return m_movieClipProto; }
	AptObject *textFieldPrototype() const { return m_textFieldProto; }

	// ---- input (step A3, AptInput.cpp) ----------------------------------------------------------------------
	AptInput &input() { return *m_input; }

	// ---- GC -----------------------------------------------------------------------------------------------
	void collectGarbage();

	// ---- debugging ----------------------------------------------------------------------------------------
	std::string dumpTree(AptCharacterInst *root, int maxDepth) const;

	// Rendering (AptRenderList.h)
	void buildRenderList(AptRenderList &out);

private:
	friend class AptPlayerHost;
	friend class AptCharacterInst;
	friend class AptSpriteInst;
	friend class AptInput;

	void markRoots(AptGC &gc);
	void notifyComponentCreated(AptCharacterInst *inst);   // AptHost::componentInstanceCreated for component symbols
	void noteInstanceDestroyed(AptCharacterInst *inst);    // AptHost::componentInstanceDestroyed for notified instances
	void flushNewInstances();                // 0x00AE4390
	void runTimers(int frameMs);             // 0x00AE4150
	void runTimerEntry(std::size_t index, int frameMs);
	void processLoads();                     // 0x00AD17F0
	void requestLoad(const std::string &movie, const std::string &target);
	void requestLoadInto(const std::string &movie, AptObject *targetClip);
	void installNatives();                   // AptNatives.cpp
	bool importsFor(const std::shared_ptr<const AptFile> &file, std::string *error);
	AptSpriteInst *makeRoot(const std::shared_ptr<const AptFile> &movie);

	AptFileSource &m_source;
	AptHost &m_userHost;
	std::unique_ptr<AptHost> m_playerHost;
	AptGC m_gc;
	std::unique_ptr<AptActionInterpreter> m_vm;
	AptLoader m_loader;

	std::map<int, AptSpriteInst *> m_levels;
	std::map<const AptFile *, std::vector<AptResolvedImport>> m_imports;
	std::map<const std::vector<AptFrame> *, std::map<std::string, int>> m_labels;
	// Per-file InitAction state: (item file offset) -> the stored sprite id; negative = already run (0x00B0F441).
	std::map<std::pair<const AptFile *, std::uint32_t>, std::int32_t> m_initState;
	std::vector<std::shared_ptr<const AptFile>> m_keepMovies;

	std::deque<AptAction> m_pool;
	bool m_running = false;
	std::vector<AptCharacterInst *> m_newInsts;
	std::vector<AptTimer> m_timers;
	std::uint32_t m_tag = 0;
	AptCharacterInst *m_scriptTarget = nullptr;

	struct LoadRequest
	{
		std::string movie;
		std::string target;
		AptCharacterInst *context;
		AptCharacterInst *targetClip;
	};
	std::vector<LoadRequest> m_loadRequests;

	std::int64_t m_clockMs = 0;
	std::int64_t m_carryMs = 0;
	std::uint64_t m_frameCount = 0;
	std::vector<AptNote> m_notes;
	float m_mouseX = 0, m_mouseY = 0;
	std::uint8_t m_background[4] = { 0, 0, 0, 0 };

	AptObject *m_movieClipProto = nullptr;
	AptObject *m_textFieldProto = nullptr;
	AptObject *m_colorProto = nullptr;
	std::unique_ptr<AptInput> m_input;
	struct CachedGeometry
	{
		std::shared_ptr<const AptGeometry> geometry; // null when the load failed
		std::string error;                           // the failure, reported by every build that needs the shape
	};
	std::map<std::pair<std::string, std::uint32_t>, CachedGeometry> m_geometry;
	std::map<std::string, std::shared_ptr<const AptImageMap>> m_imageMaps;
	std::size_t m_gcAfter = 4096;
	// Component notifications: the export names of a character (static movie data) and the instances the host was told about.
	std::map<std::pair<const AptFile *, std::uint32_t>, std::vector<std::string>> m_exportNames;
	std::set<AptCharacterInst *> m_componentInstances;
};
