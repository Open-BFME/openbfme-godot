// OpenBFME. GPL-3.0.
//
// EA Apt ActionScript interpreter (spec menus-apt.md step A1).
//
// Model: BFME1 decomp game/Libraries/Source/EA/Apt/AptActionInterpreter*.cpp.  The EA interpreter
// is a table of per-opcode handlers (AptActionInterpreter::_Function...) over a value stack, a
// register array and a bytecode cursor; arithmetic, comparison, coercion and string semantics come
// from the matched handlers (see AptValue.h).  Semantics the decomp does not contain (the handler
// bodies for Equals2, ToNumber, Greater, Increment/Decrement, CallMethod, the Get/Set variable and
// member family and the 0x3x/0x4x object opcodes sit in unmatched regions of game.exe) follow the
// SWF file format specification v19 and OpenSAGE's EA-opcode names/operands, and are marked
// "SWF spec" at each handler.
//
// Registers: DefineFunction2 functions get their own register file (registerCount entries, preload
// flags fill registers 1.. in the SWF order this, arguments, super, _root, _parent, _global, then the
// EA PreloadExtern); top-level programs and DefineFunction (v1) functions share one global file,
// which is why EA compiled code saves registers on the value stack (PushData of a register, restore
// with SetRegister+Pop at the function's end, e.g. MainMenu root frame 0).
//
// EA fused opcodes:
//   0xA6 SetStringVar: pops the variable name, the operand string is the VALUE
//        (BFME1 PushStringFusedOps008CEB20.cpp: pushes the literal then runs the SetVariable dispatch)
//   0xA7 SetStringMember: pops member name and object, operand is the value (same file, member store)
//   0xB1/0xB3/0x5E (the non-Pop calls): call, then pop the variable name that sits below the
//        arguments and DefineLocal it to the result (OpenSAGE Function.cs CallNamedMethod; the corpus
//        reads the variable right after the call with no store in between, e.g. cahclass
//        "push 'NewStatValue' ... call-named-method GetExtern; ... push-value 'NewStatValue'").
//        No EA source covers these handlers.  Evidence for treating 0xB1 and 0x5E the same way: with
//        this rule all 4,767 corpus programs end with an empty value stack, while a plain "push the
//        result" rule leaves values behind in 5 of them (test "retail corpus: every program ...").
//   0xB0/0xB2/0x5B/0x5D (Pop variants): call and discard the result.

#pragma once

#include "Libraries/Source/Apt/AptActionDecoder.h"
#include "Libraries/Source/Apt/AptHost.h"
#include "Libraries/Source/Apt/AptObject.h"
#include "Libraries/Source/Apt/AptValue.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class AptActionInterpreter
{
public:
	AptActionInterpreter(AptGC &gc, AptHost &host);
	~AptActionInterpreter();

	AptGC &gc() { return m_gc; }
	AptHost &host() { return m_host; }
	AptObject *global() const { return m_global; }

	// Run a decoded program with `target` as both `this` and the timeline in scope.  `root` is _root.
	// False when the program faulted (stack underflow, instruction budget); runtime errors that the
	// script survives (calling an undefined function...) are recorded in errors() either way.
	bool execute(const AptCodeBlock &block, AptObject *target, AptObject *root);

	// Call a script or native function value.  Undefined (and an error) for non-functions.
	AptValue callFunction(const AptValue &function, const AptValue &thisValue, const std::vector<AptValue> &args, bool isConstruct = false);
	// obj.name(args) with the member/prototype rules of CallMethod.
	AptValue callMethod(const AptValue &object, const std::string &name, const std::vector<AptValue> &args);

	AptValue getMember(const AptValue &object, const std::string &name);
	void setMember(const AptValue &object, const std::string &name, const AptValue &value);

	// Object creation helpers (natives and hosts).
	AptObject *newObject();
	AptArray *newArray();
	AptFunction *newNativeFunction(const std::string &name, AptFunction::Native fn);
	// global[name] = native function
	AptFunction *defineGlobalNative(const std::string &name, AptFunction::Native fn);
	void defineNative(AptObject *owner, const std::string &name, AptFunction::Native fn);

	AptObject *objectPrototype() const { return m_objectProto; }
	AptObject *functionPrototype() const { return m_functionProto; }
	AptObject *arrayPrototype() const { return m_arrayProto; }
	AptObject *stringPrototype() const { return m_stringProto; }
	AptObject *numberPrototype() const { return m_numberProto; }
	AptObject *booleanPrototype() const { return m_booleanProto; }

	// Marks the interpreter's own roots (global object, prototypes) for AptGC::collect.
	void markRoots(AptGC &gc);

	// Faults and skipped operations, in order.  Tests assert this is empty for clean programs.
	const std::vector<std::string> &errors() const { return m_errors; }
	void clearErrors() { m_errors.clear(); }
	// VM-integrity faults since construction: a running program or function stopped because of a stack
	// underflow/overflow, the instruction budget, a bad operand or a call stack overflow.  Survivable script
	// errors (calling a missing method, an extern with no provider) are in errors() but are not faults.
	std::uint64_t faultCount() const { return m_faults; }
	void reportError(const std::string &message);
	// A call whose callee `name` is not a function (S-380, AptHost::scriptCallWithoutFunction): listed in errors() like every survivable
	// report, handed to the host as a call without a function - except a name of the binary's native table this port does not implement
	// (S-104), which stays a script error: there the missing callee is the port's gap, not the movie's.
	void reportCallWithoutFunction(const std::string &name, const std::string &message);
	// The S-104 natives: named in the binary's native name table, not implemented by this port.
	static bool isUnportedRetailNative(const std::string &name);

	void setInstructionBudget(std::uint64_t budget) { m_budget = budget; }
	std::uint64_t instructionsExecuted() const { return m_executed; }
	// Values left on the value stack when the last execute() returned (compiled scripts end balanced).
	std::size_t lastStackDepth() const { return m_lastStackDepth; }

	// Number of registers in the shared file used by top-level code and DefineFunction v1.
	static const std::size_t kGlobalRegisterCount = 256;
	// Resource bound (an implementation limit, not an EA value): the most values one frame's stack may hold.
	// Pushing past it faults the running code with a reported error instead of growing without limit.
	static const std::size_t kMaxValueStack = 1u << 16;

private:
	struct Frame;
	bool run(Frame &frame, const AptCodeBlock &block);
	AptValue callScript(AptFunction *fn, const AptValue &thisValue, const std::vector<AptValue> &args);
	AptValue construct(Frame &frame, const std::string &className, const std::vector<AptValue> &args);

	bool lookupVariable(Frame &f, const std::string &name, AptValue &out, AptObject **holder = nullptr);
	void assignVariable(Frame &f, const std::string &name, const AptValue &value);
	void defineLocal(Frame &f, const std::string &name, const AptValue &value);
	AptValue constantToValue(Frame &f, const AptConstRef &c);
	AptObject *resolveTarget(Frame &f, const AptValue &path);
	void dispatchUrl(Frame &f, const std::string &url, const std::string &arg);
	bool timeline(Frame &f, const AptTimelineRequest &req, const char *what);
	std::vector<std::string> enumerateNames(const AptValue &object);
	void storeMember(AptObject *object, const std::string &name, const AptValue &value);
	bool fetchMember(const AptObject *object, const std::string &name, AptValue &out);
	bool memberExists(const AptObject *object, const std::string &name);

	AptGC &m_gc;
	AptHost &m_host;
	AptObject *m_global;
	AptObject *m_objectProto;
	AptObject *m_functionProto;
	AptObject *m_arrayProto;
	AptObject *m_stringProto;
	AptObject *m_numberProto;
	AptObject *m_booleanProto;
	std::vector<AptValue> m_globalRegisters;
	std::vector<std::string> m_errors;
	std::uint64_t m_budget = 20000000ull;
	std::uint64_t m_executed = 0;
	std::uint64_t m_executionStart = 0; // m_executed at the start of the outermost execute()
	int m_depth = 0;
	std::size_t m_lastStackDepth = 0;
	std::uint64_t m_faults = 0;
};

// Installs Object, Array, String, Number, Boolean, Function, Math and the global conversion
// functions on an interpreter.  Called by the AptActionInterpreter constructor.
void aptInstallCoreNatives(AptActionInterpreter &vm);
