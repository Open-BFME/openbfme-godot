// OpenBFME. GPL-3.0.
//
// Apt script object model: property-bag objects with a prototype chain, arrays and functions
// (script-defined and native), plus the mark-and-sweep heap that owns them (AptGC).
//
// Reference: BFME1 decomp game/Libraries/Source/Apt/AptGC.cpp (EA's collector marks objects
// reachable from roots and walks children through virtual item()/count(); this port keeps that
// shape: AptObject::trace marks children), AptArray.cpp, AptObject/AptScriptFunction.cpp, and for
// value-to-string of each object kind Rva008985C0ValueString.cpp (getName): arrays join with ",",
// script functions are "[function]", native functions "[native function 0x%08x]", plain objects
// "[object Object]", clips "[MovieClip]".
//
// Property storage is the EA hash table (AptNativeHash), ported from the clean BFME2 1.06 game.dat
// (virtual addresses; donor RVA + 0x400000).  Target facts decoded from the binary:
//   key hash       0x00AD3D10 / 0x00AD3800: FNV-1a over ASCII-lowercased signed bytes, low 16 bits, 0 -> 0x4567
//                  (the two stored hashes 0x6BBD "__proto__" and 0x0699 "prototype" are asserted by the
//                  BFME2 constructor 0x00B0A740 and match this function)
//   key equality   0x00AD36F0: equal 16-bit hashes then _strcmpi (case-insensitive)
//   Find           0x00B0AF90: slot = hash & (size-1); an empty slot ends the search; a used slot with an
//                  equal key hits; otherwise a +-8 window is scanned forward (slot+1 .. hi) then backward
//                  (slot-1 .. lo), an empty slot ending the scan; tombstones (key = the empty string) are
//                  skipped.  Window (compare chain at 0x00B0B017): lo = slot-8; if lo < 0 { lo = 0;
//                  hi = size > 16 ? 16 : size-1 } else { hi = slot+8; if hi > size-1 { hi = size-1;
//                  lo = max(hi-16, 0) } }
//   Set            0x00B0AC90: home slot empty -> insert; home slot tombstone -> remember it; equal key ->
//                  replace; window scan inserts into the first empty slot, replaces an equal key, and (as
//                  compiled) only moves the remembered tombstone when one was seen at the home slot; with no
//                  empty slot and no remembered tombstone the table doubles (0x00B0ABC0, re-inserting live
//                  entries in slot order) and the Set restarts; otherwise the remembered tombstone is reused
//   Remove         0x00B0B2C0: Find, then the key becomes the empty string (tombstone) and the value is dropped
//   SetMember      0x00B0B410: an empty key is ignored; "prototype"/"__proto__" go to two separate fields
//                  and never occupy a slot; the slot array is allocated at the first Set (0x00B0B49B)
//   first/next     0x00B0AA40 / 0x00B0AAA0: slot order, used slots with a non-empty key
//   plain Object   initial table size 8 (constructor callers at 0x00AD6524, 0x00AD91D7 and others push 8)
// Native methods are not in the hash in EA (they are resolved separately); setNative() keeps them in a
// side table that get/has see but enumeration never does.
// ACCEPTANCE STOP: where natives sit relative to a hash slot of the same name is not decoded (0x00B0B380
// decodes only hash, then the two reserved fields).  get() returns the hash entry, but AptObject::setOwn
// reports a pending error whenever a script stores a property whose name is a native member of the same
// object, so the order is never applied silently.

#pragma once

#include "Libraries/Source/Apt/AptActionDecoder.h"
#include "Libraries/Source/Apt/AptValue.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class AptGC;
class AptActionInterpreter;

// EA AptNativeHash: case-insensitive keys, ported slot layout and probing (see the header comment).
class AptPropertyMap
{
public:
	static constexpr int kDefaultSize = 8;

	explicit AptPropertyMap(int initialSize = kDefaultSize) : m_size(initialSize) {}

	bool get(const std::string &name, AptValue &out) const;
	bool has(const std::string &name) const;
	void set(const std::string &name, const AptValue &value);
	bool erase(const std::string &name);
	// Native members: visible to get/has, invisible to enumeration, no hash slot.
	void setNative(const std::string &name, const AptValue &value);
	bool hasNative(const std::string &name) const { return m_natives.count(foldKey(name)) != 0; }
	// Slot order of the used hash entries (the prototype/__proto__ fields and natives are not listed).
	void enumerate(std::vector<std::string> &out) const;
	void names(std::vector<std::string> &out) const { enumerate(out); }
	std::size_t size() const { return m_used; }
	int tableSize() const { return m_size; }
	// Slot holding `name`, or -1 (test hook for the layout).
	int slotOf(const std::string &name) const;
	template <class F>
	void forEachValue(F &&f) const
	{
		for (const Slot &s : m_slots)
		{
			if (s.state == Slot::Used)
			{
				f(s.value);
			}
		}
		if (m_hasPrototype)
		{
			f(m_prototype);
		}
		for (const auto &n : m_natives)
		{
			f(n.second);
		}
	}

	static std::string foldKey(const std::string &name);
	// BFME2 0x00AD3D10.
	static std::uint16_t hash16(const std::string &name);

private:
	struct Slot
	{
		enum State : std::uint8_t
		{
			Empty,
			Tomb,
			Used
		};
		State state = Empty;
		std::uint16_t hash = 0;
		std::string key;
		AptValue value;
	};

	static bool isPrototypeName(const std::string &name, std::uint16_t hash);
	bool keyEquals(const Slot &s, const std::string &name, std::uint16_t hash) const;
	int find(const std::string &name, std::uint16_t hash) const;
	void rawSet(const std::string &name, std::uint16_t hash, const AptValue &value);
	void place(int slot, const std::string &name, std::uint16_t hash, const AptValue &value);
	void grow();
	static void window(int slot, int size, int &lo, int &hi);

	int m_size;
	std::size_t m_used = 0;
	std::vector<Slot> m_slots; // empty until the first Set
	bool m_hasPrototype = false;
	AptValue m_prototype;
	std::unordered_map<std::string, AptValue> m_natives; // folded name -> value
};

enum class AptObjectKind : std::uint8_t
{
	Object,
	Array,
	Function,
	Clip // movie clip instances (step A2); present so getName/typeof can tell them apart
};

// A timeline/clip request the interpreter forwards to the target object (step A2 implements it on
// clip instances; objects without a timeline report "unsupported").
enum class AptTimelineOp : std::uint8_t
{
	NextFrame,
	Play,
	Stop,
	GotoFrame,    // a = 0-based frame number
	GotoLabel,    // a = label string
	GotoFrame2,   // a = frame number or label (from the stack), flag = play after the jump
	CloneSprite,  // a = source path, b = new name, c = depth
	RemoveSprite  // a = target path
};

struct AptTimelineRequest
{
	AptTimelineOp op = AptTimelineOp::Stop;
	AptValue a, b, c;
	bool flag = false;
};

class AptObject
{
public:
	explicit AptObject(AptObjectKind kind = AptObjectKind::Object) : m_kind(kind) {}
	virtual ~AptObject() = default;

	AptObjectKind kind() const { return m_kind; }
	AptObject *proto() const { return m_proto; }
	void setProto(AptObject *p) { m_proto = p; }

	// Lookup through the prototype chain / assignment on the object itself.
	// Lookup through the prototype chain.  The walk visits at most kMaxProtoLinks objects (an implementation
	// limit, docs/STOPS.md S-015); a longer chain stops with a pending error, which is not the same as a miss.
	static constexpr int kMaxProtoLinks = 64;
	bool getMember(const std::string &name, AptValue &out) const;
	void setMember(const std::string &name, const AptValue &value) { setOwn(name, value); }
	bool hasMember(const std::string &name) const;

	// "__proto__" is the prototype link (EA keeps it in a field of the hash, never in a slot).
	virtual bool getOwn(const std::string &name, AptValue &out) const;
	virtual void setOwn(const std::string &name, const AptValue &value);
	virtual bool deleteOwn(const std::string &name);
	virtual void ownNames(std::vector<std::string> &out) const { props.names(out); }
	// ActionEnumerate2 support (S-006): true when this object's enumeration order is decoded.  Plain objects always; sprite and
	// button instances too (their hash is the AptNativeHash of size 8 / 4 built at 0x00AED0DA / 0x00AF85B7, enumerated by
	// 0x00B00170 through vtable slot 3); arrays, functions and the other instance classes are not.
	virtual bool forInDecoded() const { return m_kind == AptObjectKind::Object; }
	// Built-in (native) members: not in the hash, never enumerated.
	void setNativeMember(const std::string &name, const AptValue &value) { props.setNative(name, value); }

	// ActionEnumerate2 name list: this object's hash slots, then its __proto__ chain's, in slot order with no
	// de-duplication (BFME2 0x00B00170).  Only plain objects are supported: arrays, functions and clips put
	// their members somewhere other than the hash and the class hash sizes are unidentified, so they return
	// false with `error` set (acceptance stop) - whether the starting object or any link of its chain is one.
	// A non-object "__proto__" value anywhere in the chain is a stop as well.
	bool enumerateForIn(std::vector<std::string> &out, std::string &error) const;

	// A pending resource-bound error raised by a mutation (array growth); empty when none.
	std::string takeError()
	{
		std::string e;
		e.swap(m_error);
		return e;
	}
	virtual void trace(AptGC &gc);
	virtual std::string displayString() const;

	// Timeline commands (NextFrame, Play, Goto*, ...). Returns false when this object has no timeline.
	virtual bool timelineOp(const AptTimelineRequest &) { return false; }

	AptPropertyMap props;

private:
	friend class AptGC;
	AptObjectKind m_kind;
	AptObject *m_proto = nullptr;
	// "__proto__" assigned a non-object: EA's setter keeps the boxed value (BFME2 0x00ADB36E) and the getter
	// returns it (0x00B0B3F4); the prototype chain does not continue through it.
	AptValue m_protoValue;
	bool m_hasProtoValue = false;
	bool m_marked = false;

protected:
	mutable std::string m_error; // pending resource-bound / acceptance-stop report (see takeError)
};

class AptArray : public AptObject
{
public:
	AptArray() : AptObject(AptObjectKind::Array) {}
	std::vector<AptValue> items;

	bool getOwn(const std::string &name, AptValue &out) const override;
	void setOwn(const std::string &name, const AptValue &value) override;
	bool deleteOwn(const std::string &name) override;
	void ownNames(std::vector<std::string> &out) const override;
	void trace(AptGC &gc) override;
	std::string displayString() const override;

	// "123" -> index; false for names that are not canonical non-negative integers.
	static bool parseIndex(const std::string &name, std::size_t &index);

	// Resource bound (an implementation limit, not an EA value): the most elements an array may be grown to
	// by index or `length` assignment.  Exceeding it leaves the array unchanged and raises a pending error.
	static constexpr std::size_t kMaxLength = 1u << 18;
};

// What a native function receives.  args[0] is the first argument.
struct AptCallInfo
{
	AptActionInterpreter &vm;
	AptValue thisValue;
	std::vector<AptValue> args;
	bool isConstruct = false; // called through NewObject: the native builds and returns the new object
};

// The local-variable scope of one running script function.  Functions defined inside a function keep
// the enclosing scope alive (AVM1 scope chain), so they can read and write its variables.
struct AptScope
{
	AptPropertyMap locals;
	std::shared_ptr<AptScope> parent;
};

class AptFunction : public AptObject
{
public:
	typedef std::function<AptValue(AptCallInfo &)> Native;

	AptFunction() : AptObject(AptObjectKind::Function) {}

	bool isNative() const { return static_cast<bool>(native); }

	Native native;
	std::string nativeName;
	std::shared_ptr<const AptFunctionDef> def;
	std::shared_ptr<const std::vector<AptValue>> pool; // constant pool active at definition
	AptObject *scopeTarget = nullptr;                   // target (timeline) in scope at definition
	AptObject *scopeRoot = nullptr;
	std::shared_ptr<AptScope> outerScope; // locals of the function this one was defined in (null at timeline level)

	void trace(AptGC &gc) override;
	std::string displayString() const override;
};

// ---- heap -----------------------------------------------------------------------------------

class AptGC
{
public:
	template <class T, class... Args>
	T *create(Args &&...args)
	{
		std::unique_ptr<T> p = std::make_unique<T>(std::forward<Args>(args)...);
		T *raw = p.get();
		m_objects.push_back(std::move(p));
		return raw;
	}

	std::size_t objectCount() const { return m_objects.size(); }

	// Marking (call from a root marker or from AptObject::trace).
	void mark(const AptValue &v);
	void mark(AptObject *o);

	// Mark everything the root marker reaches, free the rest.  Returns the number freed.  Only call
	// between script executions: values on a running interpreter's stack are not roots.
	std::size_t collect(const std::function<void(AptGC &)> &markRoots);

private:
	std::vector<std::unique_ptr<AptObject>> m_objects;
	std::vector<AptObject *> m_work;
};

// SWF GetProperty/SetProperty index -> property name ("_x" ...), or nullptr when out of range.
const char *AptPropertyName(int index);
