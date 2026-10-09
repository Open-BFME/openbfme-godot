// OpenBFME unit tests: the Apt ActionScript interpreter (spec step A1).
//
// Programs are assembled byte by byte (AptTestUtil.h) and run through the real decoder and VM.
// Expected values come from the EA decompile (BFME1 game/Libraries/Source/EA/Apt/...), the SWF file
// format specification v19 and the retail disassembly in the spec, never from this engine's output.
// GPL-3.0.

#include "doctest.h"
#include "AptTestUtil.h"
#include "AptRetail.h"

#include "Libraries/Source/Apt/AptActionInterpreter.h"

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <limits>
#include <map>
#include <set>

using namespace apttest;

namespace
{

// A clip stand-in that records the timeline requests the VM forwards.
class TestClip : public AptObject
{
public:
	TestClip() : AptObject(AptObjectKind::Clip) {}
	std::vector<std::string> log;
	bool timelineOp(const AptTimelineRequest &r) override
	{
		std::string s;
		switch (r.op)
		{
			case AptTimelineOp::NextFrame: s = "nextFrame"; break;
			case AptTimelineOp::Play: s = "play"; break;
			case AptTimelineOp::Stop: s = "stop"; break;
			case AptTimelineOp::GotoFrame: s = "gotoFrame:" + r.a.toString(); break;
			case AptTimelineOp::GotoLabel: s = "gotoLabel:" + r.a.toString(); break;
			case AptTimelineOp::GotoFrame2: s = "gotoFrame2:" + r.a.toString() + (r.flag ? ":play" : ":stop"); break;
			case AptTimelineOp::CloneSprite: s = "clone:" + r.a.toString() + ":" + r.b.toString() + ":" + r.c.toString(); break;
			case AptTimelineOp::RemoveSprite: s = "remove:" + r.a.toString(); break;
		}
		log.push_back(s);
		return true;
	}
};

struct Fx
{
	AptGC gc;
	AptRecordingHost host;
	AptActionInterpreter vm;
	AptObject *root;

	Fx() : vm(gc, host) { root = vm.newObject(); }

	// Assemble with `build`, append End, decode and execute on `target` (default: the root object).
	bool run(int swfVersion, const std::function<void(TestMovie &, Asm &)> &build, AptObject *target = nullptr)
	{
		TestMovie m(swfVersion);
		Asm a = m.program();
		build(m, a);
		a.op(APT_OP_END);
		std::uint32_t off = m.commit(a);
		m.setRootFrames({ { m.addActionItem(off) } });
		AptFile f;
		std::string err;
		REQUIRE_MESSAGE(m.parse(f, &err), err);
		std::shared_ptr<const AptCodeBlock> block = f.codeAt(off, &err);
		REQUIRE_MESSAGE(block, err);
		return vm.execute(*block, target ? target : root, root);
	}
	bool run(const std::function<void(TestMovie &, Asm &)> &build, AptObject *target = nullptr) { return run(7, build, target); }

	AptValue var(const char *name, AptObject *on = nullptr)
	{
		AptValue v;
		(on ? on : root)->getMember(name, v);
		return v;
	}
	std::string errorsText() const
	{
		std::string s;
		for (const std::string &e : vm.errors())
		{
			s += e + "; ";
		}
		return s;
	}
};

// name = <value ops>: pushes the name string, runs `value`, SetVariable.
void setVar(Asm &a, const char *name, const std::function<void()> &value)
{
	a.pushString(name);
	value();
	a.op(APT_OP_SETVARIABLE);
}

} // namespace

// ---- pushes and arithmetic ---------------------------------------------------------------------

TEST_CASE("VM: push opcodes produce the values the operands encode")
{
	Fx fx;
	REQUIRE(fx.run([&](TestMovie &, Asm &a) {
		setVar(a, "b", [&] { a.pushByte(-1); });
		setVar(a, "s", [&] { a.pushShort(-300); });
		setVar(a, "l", [&] { a.pushLong(123456); });
		setVar(a, "f", [&] { a.pushFloat(2.5f); });
		setVar(a, "str", [&] { a.pushString("txt"); });
		setVar(a, "t", [&] { a.op(APT_OP_EA_PUSHTRUE); });
		setVar(a, "fl", [&] { a.op(APT_OP_EA_PUSHFALSE); });
		setVar(a, "n", [&] { a.op(APT_OP_EA_PUSHNULL); });
		setVar(a, "u", [&] { a.op(APT_OP_EA_PUSHUNDEFINED); });
		setVar(a, "z", [&] { a.op(APT_OP_EA_PUSHZERO); });
		setVar(a, "o", [&] { a.op(APT_OP_EA_PUSHONE); });
		setVar(a, "me", [&] { a.op(APT_OP_EA_PUSHTHISVAR); });
		setVar(a, "g", [&] { a.op(APT_OP_EA_PUSHGLOBALVAR); });
	}));
	CHECK(fx.errorsText() == "");
	CHECK(fx.var("b").isInteger());
	CHECK(fx.var("b").asInteger() == -1);
	CHECK(fx.var("s").asInteger() == -300);
	CHECK(fx.var("l").asInteger() == 123456);
	CHECK(fx.var("f").isFloat());
	CHECK(fx.var("f").asFloat() == 2.5f);
	CHECK(fx.var("str").asString() == "txt");
	CHECK(fx.var("t").asBool());
	CHECK(fx.var("fl").isBoolean());
	CHECK_FALSE(fx.var("fl").asBool());
	// BFME2 0x00B05320: PushNull (0x75) and PushUndefined (0x76) push the same undefined singleton
	CHECK(fx.var("n").isUndefined());
	CHECK(fx.var("u").isUndefined());
	CHECK(fx.var("z").isInteger());
	CHECK(fx.var("o").asInteger() == 1);
	CHECK(fx.var("me").asObject() == fx.root);
	CHECK(fx.var("g").asObject() == fx.vm.global());
}

TEST_CASE("VM: arithmetic uses the EA semantics (1.5+1.5 == 3, float division, SWF 7 undefined)")
{
	Fx fx;
	REQUIRE(fx.run([&](TestMovie &, Asm &a) {
		setVar(a, "sum", [&] { a.pushFloat(1.5f).pushFloat(1.5f).op(APT_OP_ADD2); });
		setVar(a, "eq", [&] { a.pushFloat(1.5f).pushFloat(1.5f).op(APT_OP_ADD2).pushByte(3).op(APT_OP_EQUALS2); });
		setVar(a, "div", [&] { a.pushByte(7).pushByte(2).op(APT_OP_DIVIDE); });
		setVar(a, "mod", [&] { a.pushByte(7).pushByte(3).op(APT_OP_MODULO); });
		setVar(a, "sub", [&] { a.pushByte(7).pushByte(2).op(APT_OP_SUBTRACT); });
		setVar(a, "mul", [&] { a.pushByte(6).pushFloat(0.5f).op(APT_OP_MULTIPLY); });
		setVar(a, "divzero", [&] { a.pushByte(1).pushByte(0).op(APT_OP_DIVIDE); });
		setVar(a, "undefplus", [&] { a.op(APT_OP_EA_PUSHUNDEFINED).pushByte(1).op(APT_OP_ADD2); });
		setVar(a, "cat", [&] { a.pushString("n=").pushByte(5).op(APT_OP_ADD2); });
		setVar(a, "intadd", [&] { a.pushByte(2).pushByte(3).op(APT_OP_ADD2); });
		setVar(a, "lt", [&] { a.pushByte(1).pushByte(2).op(APT_OP_LESS2); });
		setVar(a, "gt", [&] { a.pushByte(3).pushByte(2).op(APT_OP_GREATER); });
		setVar(a, "and", [&] { a.pushShort(0xF0).pushShort(0x3C).op(APT_OP_BITAND); });
		setVar(a, "shr", [&] { a.pushByte(-16).pushByte(2).op(APT_OP_BITRSHIFT); });
		setVar(a, "not", [&] { a.pushByte(0).op(APT_OP_NOT); });
		setVar(a, "toint", [&] { a.pushFloat(3.7f).op(APT_OP_TOINTEGER); });
		setVar(a, "tonum", [&] { a.pushString("2.5").op(APT_OP_TONUMBER); });
		setVar(a, "tostr", [&] { a.pushFloat(2.5f).op(APT_OP_TOSTRING); });
		setVar(a, "inc", [&] { a.pushByte(4).op(APT_OP_INCREMENT); });
		setVar(a, "dec", [&] { a.pushByte(4).op(APT_OP_DECREMENT); });
		setVar(a, "streq", [&] { a.pushString("a").pushString("a").op(APT_OP_STRINGEQUALS); });
		setVar(a, "typeof", [&] { a.pushString("x").op(APT_OP_TYPEOF); });
	}));
	CHECK(fx.errorsText() == "");
	CHECK(fx.var("sum").isFloat());
	CHECK(fx.var("sum").asFloat() == 3.0f);
	CHECK(fx.var("eq").asBool());
	CHECK(fx.var("div").asFloat() == 3.5f);
	CHECK(fx.var("mod").asFloat() == 1.0f);
	CHECK(fx.var("sub").isInteger()); // BFME2 0x00B00880: integer - integer is an integer
	CHECK(fx.var("sub").asInteger() == 5);
	CHECK(fx.var("mul").asFloat() == 3.0f);
	CHECK(fx.var("divzero").isUndefined());
	CHECK(fx.var("undefplus").isUndefined());
	CHECK(fx.var("cat").asString() == "n=5");
	CHECK(fx.var("intadd").isInteger());
	CHECK(fx.var("intadd").asInteger() == 5);
	CHECK(fx.var("lt").asBool());
	CHECK(fx.var("gt").asBool());
	CHECK(fx.var("and").asInteger() == (0xF0 & 0x3C));
	CHECK(fx.var("shr").asInteger() == -4);
	CHECK(fx.var("not").asBool());
	CHECK(fx.var("toint").asInteger() == 3);
	CHECK(fx.var("tonum").asFloat() == 2.5f);
	CHECK(fx.var("tostr").asString() == "2.500000");
	CHECK(fx.var("inc").asInteger() == 5);
	CHECK(fx.var("dec").asInteger() == 3);
	CHECK(fx.var("streq").asBool());
	CHECK(fx.var("typeof").asString() == "string");
}

TEST_CASE("VM: SWF version 6 movies coerce undefined where version 7 keeps it undefined or stringifies it")
{
	Fx v7, v6;
	auto prog = [&](TestMovie &, Asm &a) {
		setVar(a, "n", [&] { a.op(APT_OP_EA_PUSHUNDEFINED).pushByte(1).op(APT_OP_ADD2); });
		setVar(a, "s", [&] { a.pushString("a").op(APT_OP_EA_PUSHUNDEFINED).op(APT_OP_ADD2); });
		setVar(a, "c", [&] { a.pushString("a").op(APT_OP_EA_PUSHUNDEFINED).op(APT_OP_STRINGCONCAT); });
		setVar(a, "tos", [&] { a.op(APT_OP_EA_PUSHUNDEFINED).op(APT_OP_TOSTRING); });
		setVar(a, "zero", [&] { a.pushString("0"); });
		a.getStringVar("zero").branchIfTrue("yes");
		setVar(a, "branch", [&] { a.pushString("no"); });
		a.branchAlways("end").label("yes");
		setVar(a, "branch", [&] { a.pushString("yes"); });
		a.label("end");
	};
	REQUIRE(v7.run(7, prog));
	REQUIRE(v6.run(6, prog));
	CHECK(v7.var("n").isUndefined());
	CHECK(v6.var("n").asInteger() == 1);
	CHECK(v7.var("s").asString() == "aundefined");
	CHECK(v6.var("s").asString() == "a");
	CHECK(v7.var("c").asString() == "aundefined");
	CHECK(v6.var("c").asString() == "a");
	CHECK(v7.var("tos").asString() == "undefined");
	CHECK(v6.var("tos").asString() == "");
	CHECK(v7.var("branch").asString() == "yes"); // "0" is a non-empty string in SWF 7
	CHECK(v6.var("branch").asString() == "no");  // ... and atof("0") == 0 in SWF 6
}

// ---- stack, variables, members -----------------------------------------------------------------

TEST_CASE("VM: Pop and PushDuplicate")
{
	Fx fx;
	REQUIRE(fx.run([&](TestMovie &, Asm &a) {
		setVar(a, "dup", [&] { a.pushByte(3).op(APT_OP_PUSHDUPLICATE).op(APT_OP_ADD2); });
		a.pushByte(9).op(APT_OP_POP);
	}));
	CHECK(fx.var("dup").asInteger() == 6);
}

TEST_CASE("VM: variables - SetVariable/GetVariable, the string forms, ZeroVar, DefineLocal, Delete2")
{
	Fx fx;
	REQUIRE(fx.run([&](TestMovie &, Asm &a) {
		setVar(a, "x", [&] { a.pushByte(5); });
		setVar(a, "viaget", [&] { a.pushString("x").op(APT_OP_GETVARIABLE); });
		setVar(a, "viastr", [&] { a.getStringVar("x"); });
		a.pushString("fused").setStringVar("literal"); // A6: the operand is the VALUE, the name is popped
		a.pushString("zeroed").op(APT_OP_EA_ZEROVAR);
		a.pushString("loc").pushByte(7).op(APT_OP_DEFINELOCAL); // timeline level: a variable on the target
		a.pushString("gone").pushByte(1).op(APT_OP_SETVARIABLE);
		setVar(a, "deleted", [&] { a.pushString("gone").op(APT_OP_DELETE2); });
		setVar(a, "missing", [&] { a.getStringVar("neverDefined"); });
		setVar(a, "case", [&] { a.getStringVar("X"); }); // names are case-insensitive
		a.pushString("declared").op(APT_OP_DEFINELOCAL2);
	}));
	CHECK(fx.errorsText() == "");
	CHECK(fx.var("viaget").asInteger() == 5);
	CHECK(fx.var("viastr").asInteger() == 5);
	CHECK(fx.var("fused").asString() == "literal");
	CHECK(fx.var("zeroed").asInteger() == 0);
	CHECK(fx.var("loc").asInteger() == 7);
	CHECK(fx.var("deleted").asBool());
	CHECK_FALSE(fx.root->hasMember("gone"));
	CHECK(fx.var("missing").isUndefined());
	CHECK(fx.var("case").asInteger() == 5);
	CHECK(fx.root->hasMember("declared"));
}

TEST_CASE("VM: members - SetMember/GetMember, GetStringMember, SetStringMember, GetNamedMember, Delete")
{
	Fx fx;
	REQUIRE(fx.run([&](TestMovie &, Asm &a) {
		a.constantPool({ "inner", "pooled" });
		setVar(a, "obj", [&] { a.pushByte(0).op(APT_OP_INITOBJECT); });
		a.getStringVar("obj").pushString("k").pushByte(11).op(APT_OP_SETMEMBER);
		a.getStringVar("obj").pushString("fusedkey").setStringMember("fusedvalue"); // A7: operand is the value
		a.getStringVar("obj").pushString("pooled").pushByte(3).op(APT_OP_SETMEMBER);
		setVar(a, "k", [&] { a.getStringVar("obj").pushString("K").op(APT_OP_GETMEMBER); });
		setVar(a, "fused", [&] { a.getStringVar("obj").getStringMember("fusedkey"); });
		setVar(a, "named", [&] { a.getStringVar("obj").getNamedMember(1); });
		setVar(a, "del", [&] { a.getStringVar("obj").pushString("k").op(APT_OP_DELETE); });
		setVar(a, "after", [&] { a.getStringVar("obj").pushString("k").op(APT_OP_GETMEMBER); });
		setVar(a, "onUndefined", [&] { a.op(APT_OP_EA_PUSHUNDEFINED).pushString("anything").op(APT_OP_GETMEMBER); });
	}));
	CHECK(fx.errorsText() == "");
	CHECK(fx.var("k").asInteger() == 11);
	CHECK(fx.var("fused").asString() == "fusedvalue");
	CHECK(fx.var("named").asInteger() == 3);
	CHECK(fx.var("del").asBool());
	CHECK(fx.var("after").isUndefined());
	CHECK(fx.var("onUndefined").isUndefined()); // member of undefined is undefined (no fault)
}

TEST_CASE("VM: a counting loop with BranchIfTrue and a backward BranchAlways sums 1..10")
{
	Fx fx;
	REQUIRE(fx.run([&](TestMovie &, Asm &a) {
		setVar(a, "i", [&] { a.pushByte(0); });
		setVar(a, "sum", [&] { a.pushByte(0); });
		a.label("top");
		setVar(a, "i", [&] { a.getStringVar("i").op(APT_OP_INCREMENT); });
		setVar(a, "sum", [&] { a.getStringVar("sum").getStringVar("i").op(APT_OP_ADD2); });
		a.getStringVar("i").pushByte(10).op(APT_OP_LESS2).branchIfTrue("top");
	}));
	CHECK(fx.var("sum").asInteger() == 55);
	CHECK(fx.var("i").asInteger() == 10);
}

TEST_CASE("VM: registers - SetRegister keeps the value, PushData of a register reads it, the save/restore idiom works")
{
	Fx fx;
	REQUIRE(fx.run([&](TestMovie &m, Asm &a) {
		std::uint32_t r1 = m.constRegister(1);
		std::uint32_t r2 = m.constRegister(2);
		// r1 = 7 (value stays on the stack), pop; r2 = r1 * 2
		a.pushByte(7).setRegister(1).op(APT_OP_POP);
		a.pushDataIdx({ r1 }).pushByte(2).op(APT_OP_MULTIPLY).setRegister(2).op(APT_OP_POP);
		setVar(a, "r1", [&] { a.pushDataIdx({ r1 }); });
		setVar(a, "r2", [&] { a.pushDataIdx({ r2 }); });
		// the EA save/restore idiom around a "function body" that clobbers r1
		a.pushDataIdx({ r1 });                       // save r1 on the stack
		a.pushByte(99).setRegister(1).op(APT_OP_POP); // clobber
		setVar(a, "clobbered", [&] { a.pushDataIdx({ r1 }); });
		a.setRegister(1).op(APT_OP_POP);              // restore from the stack
		setVar(a, "restored", [&] { a.pushDataIdx({ r1 }); });
	}));
	CHECK(fx.errorsText() == "");
	CHECK(fx.var("r1").asInteger() == 7);
	CHECK(fx.var("r2").toNumber() == 14.0f);
	CHECK(fx.var("clobbered").asInteger() == 99);
	CHECK(fx.var("restored").asInteger() == 7);
}

// ---- functions ---------------------------------------------------------------------------------

TEST_CASE("VM: DefineFunction (v1) defines a named function on the target; parameters are named locals; Return yields the value")
{
	Fx fx;
	REQUIRE(fx.run([&](TestMovie &, Asm &a) {
		a.defineFunction("add", { "p", "q" }, [&](Asm &f) { f.getStringVar("p").getStringVar("q").op(APT_OP_ADD2).op(APT_OP_RETURN); });
		setVar(a, "anon", [&] {
			a.defineFunction("", {}, [&](Asm &f) { f.pushString("anonymous").op(APT_OP_RETURN); });
		});
		a.pushByte(30).pushByte(12).pushByte(2).pushString("add").op(APT_OP_CALLFUNCTION); // add(12, 30): arg0 is nearest the top
		a.setRegister(5).op(APT_OP_POP);
	}));
	CHECK(fx.errorsText() == "");
	AptValue add = fx.var("add");
	REQUIRE(add.isObject());
	CHECK(add.asObject()->kind() == AptObjectKind::Function);
	CHECK(fx.vm.callFunction(add, AptValue(), { AptValue::integer(1), AptValue::integer(2) }).asInteger() == 3);
	CHECK(fx.vm.callFunction(add, AptValue(), { AptValue::string("a"), AptValue::string("b") }).asString() == "ab");
	CHECK(fx.vm.callFunction(add, AptValue(), { AptValue::integer(1) }).isUndefined()); // missing argument is undefined: 1 + undefined (SWF 7)
	AptValue anon = fx.var("anon");
	REQUIRE(anon.isObject());
	CHECK(fx.vm.callFunction(anon, AptValue(), {}).asString() == "anonymous");
	CHECK(fx.errorsText() == "");
}

TEST_CASE("VM: recursion through the named-function lookup (fib(10) == 55)")
{
	Fx fx;
	REQUIRE(fx.run([&](TestMovie &, Asm &a) {
		a.defineFunction("fib", { "n" }, [&](Asm &f) {
			f.getStringVar("n").pushByte(2).op(APT_OP_LESS2).branchIfTrue("base");
			f.getStringVar("n").pushByte(1).op(APT_OP_SUBTRACT).pushByte(1).pushString("fib").op(APT_OP_CALLFUNCTION);
			f.getStringVar("n").pushByte(2).op(APT_OP_SUBTRACT).pushByte(1).pushString("fib").op(APT_OP_CALLFUNCTION);
			f.op(APT_OP_ADD2).op(APT_OP_RETURN);
			f.label("base").getStringVar("n").op(APT_OP_RETURN);
		});
		setVar(a, "result", [&] { a.pushByte(10).pushByte(1).pushString("fib").op(APT_OP_CALLFUNCTION); });
	}));
	CHECK(fx.errorsText() == "");
	AptValue r = fx.var("result");
	CHECK(r.toNumber() == 55.0f);
}

TEST_CASE("VM: DefineFunction2 - registers, parameter registers, preload flags in SWF order, per-call register files")
{
	Fx fx;
	AptObject *clip = fx.vm.newObject();
	fx.root->setMember("clip", AptValue::object(clip));
	REQUIRE(fx.run([&](TestMovie &m, Asm &a) {
		std::uint32_t r1 = m.constRegister(1), r2 = m.constRegister(2), r3 = m.constRegister(3), r4 = m.constRegister(4);
		// params (reg 3 'a'), (reg 2 'b'), nothing preloaded: a - b
		a.defineFunction2("sub", 4, APT_FN2_SUPPRESS_THIS | APT_FN2_SUPPRESS_ARGUMENTS | APT_FN2_SUPPRESS_SUPER, { { 3, "a" }, { 2, "b" } },
			[&](Asm &f) { f.pushDataIdx({ r3, r2 }).op(APT_OP_SUBTRACT).op(APT_OP_RETURN); });
		// this -> r1, _root -> r2, _global -> r3, extern -> r4
		a.defineFunction2("preloads", 5, APT_FN2_PRELOAD_THIS | APT_FN2_PRELOAD_ROOT | APT_FN2_PRELOAD_GLOBAL | APT_FN2_PRELOAD_EXTERN, {},
			[&](Asm &f) {
				f.pushString("this").pushDataIdx({ r1 }).op(APT_OP_SETVARIABLE);
				f.pushString("root").pushDataIdx({ r2 }).op(APT_OP_SETVARIABLE);
				f.pushString("global").pushDataIdx({ r3 }).op(APT_OP_SETVARIABLE);
				f.pushString("ext").pushDataIdx({ r4 }).op(APT_OP_SETVARIABLE);
			});
		// arguments -> r1; a parameter without a register stays a named local
		a.defineFunction2("args", 2, APT_FN2_PRELOAD_ARGUMENTS, { { 0, "named" } }, [&](Asm &f) {
			f.pushDataIdx({ r1 }).getStringMember("length").getStringVar("named").op(APT_OP_ADD2).op(APT_OP_RETURN);
		});
		// every call gets a fresh register file: r1 is undefined on entry
		a.defineFunction2("fresh", 2, APT_FN2_SUPPRESS_THIS, {}, [&](Asm &f) {
			f.pushDataIdx({ r1 }).op(APT_OP_RETURN);
		});
	}));
	CHECK(fx.errorsText() == "");
	CHECK(fx.vm.callFunction(fx.var("sub"), AptValue(), { AptValue::integer(10), AptValue::integer(4) }).asInteger() == 6);
	// preloads: this is the object the method is called on
	fx.vm.callFunction(fx.var("preloads"), AptValue::object(clip), {});
	CHECK(fx.errorsText() == "");
	// the body's SetVariable ran with the function's defining target (the root), so the values land there
	CHECK(fx.var("this").asObject() == clip);
	CHECK(fx.var("root").asObject() == fx.root);
	CHECK(fx.var("global").asObject() == fx.vm.global());
	CHECK(fx.var("ext").isExtern());
	CHECK(fx.vm.callFunction(fx.var("args"), AptValue(), { AptValue::integer(7), AptValue::integer(8) }).toNumber() == 9.0f); // arguments.length (2) + the named local (arg0 = 7)
	CHECK(fx.vm.callFunction(fx.var("fresh"), AptValue(), {}).isUndefined());
	fx.vm.clearErrors();
}

TEST_CASE("VM: a function defined inside a function keeps reading and writing the enclosing function's locals")
{
	Fx fx;
	REQUIRE(fx.run([&](TestMovie &, Asm &a) {
		a.defineFunction("makeCounter", { "start" }, [&](Asm &f) {
			f.op(APT_OP_EA_PUSHTHISVAR).pushString("next");
			f.defineFunction("", {}, [&](Asm &g) {
				g.pushString("start").getStringVar("start").op(APT_OP_INCREMENT).op(APT_OP_SETVARIABLE); // assigns the OUTER local
				g.getStringVar("start").op(APT_OP_RETURN);
			});
			f.op(APT_OP_SETMEMBER);
			f.pushString("held").pushByte(0).op(APT_OP_INITOBJECT).op(APT_OP_DEFINELOCAL); // an object only the scope references
		});
	}));
	AptObject *counter = fx.vm.newObject();
	fx.vm.callFunction(fx.var("makeCounter"), AptValue::object(counter), { AptValue::integer(10) });
	AptValue next;
	REQUIRE(counter->getMember("next", next));
	CHECK(fx.vm.callFunction(next, AptValue(), {}).asInteger() == 11);
	CHECK(fx.vm.callFunction(next, AptValue(), {}).asInteger() == 12);
	CHECK_FALSE(fx.root->hasMember("start")); // never leaked to the timeline
	// the collector keeps what the captured scope holds
	fx.gc.collect([&](AptGC &g) {
		fx.vm.markRoots(g);
		g.mark(fx.root);
		g.mark(counter);
	});
	CHECK(fx.vm.callFunction(next, AptValue(), {}).asInteger() == 13);
	CHECK(fx.errorsText() == "");
}

TEST_CASE("VM: a function keeps the constant pool and defining timeline of the program that defined it")
{
	Fx fx;
	AptObject *timelineA = fx.vm.newObject();
	AptObject *other = fx.vm.newObject();
	timelineA->setMember("freeVar", AptValue::string("from A"));
	other->setMember("freeVar", AptValue::string("from other"));
	REQUIRE(fx.run([&](TestMovie &, Asm &a) {
		a.constantPool({ "pooled in A", "freeVar" });
		a.defineFunction("f", {}, [&](Asm &f) {
			f.pushConstByte(0).pushValueOfVar(1).op(APT_OP_ADD2).op(APT_OP_RETURN); // pool[0] + freeVar
		});
	}, timelineA));
	// a second program installs a different pool and never touches f
	REQUIRE(fx.run([&](TestMovie &, Asm &a) { a.constantPool({ "unrelated", "strings" }); }, other));
	AptValue f;
	REQUIRE(timelineA->getMember("f", f));
	// called with `this` = other: the pool is A's, the free variable resolves on A (the defining timeline)
	AptValue r = fx.vm.callFunction(f, AptValue::object(other), {});
	CHECK(r.asString() == "pooled in Afrom A");
	CHECK(fx.errorsText() == "");
}

TEST_CASE("VM: method calls - CallMethod pushes the result, the Pop forms discard it, undefined name calls the function value")
{
	Fx fx;
	REQUIRE(fx.run([&](TestMovie &, Asm &a) {
		a.constantPool({ "m", "obj" });
		setVar(a, "obj", [&] { a.pushByte(0).op(APT_OP_INITOBJECT); });
		a.getStringVar("obj").pushString("m");
		a.defineFunction("", { "x" }, [&](Asm &f) {
			f.getStringVar("x").pushByte(100).op(APT_OP_ADD2).op(APT_OP_RETURN);
		});
		a.op(APT_OP_SETMEMBER);
		// obj.m(5) through CallMethod (0x52): stack [5, 1, obj, "m"]
		setVar(a, "r52", [&] { a.pushByte(5).pushByte(1).getStringVar("obj").pushString("m").op(APT_OP_CALLMETHOD); });
		// pop form 0x5D discards: stack unchanged afterwards (a following store must still see its own operands)
		a.pushByte(5).pushByte(1).getStringVar("obj").pushString("m").op(APT_OP_EA_CALLMETHODPOP);
		// B3 fused with the variable name below the arguments
		a.pushString("fused3").pushByte(6).pushByte(1).getStringVar("obj").opU8(APT_OP_EA_CALLNAMEDMETHOD, 0);
		// 5E fused (stack-named method)
		a.pushString("fused5").pushByte(7).pushByte(1).getStringVar("obj").pushString("m").op(APT_OP_EA_CALLMETHOD);
		// B2 pop form
		a.pushByte(8).pushByte(1).getStringVar("obj").opU8(APT_OP_EA_CALLNAMEDMETHODPOP, 0);
		// an undefined method name calls the object itself as the function
		a.pushString("direct").pushByte(9).pushByte(1).getStringVar("obj").getStringMember("m").op(APT_OP_EA_PUSHUNDEFINED).op(APT_OP_EA_CALLMETHOD);
	}));
	CHECK(fx.errorsText() == "");
	CHECK(fx.var("r52").toNumber() == 105.0f);
	CHECK(fx.var("fused3").toNumber() == 106.0f); // DefineLocal at timeline level: a variable on the target
	CHECK(fx.var("fused5").toNumber() == 107.0f);
	CHECK(fx.var("direct").toNumber() == 109.0f);
}

TEST_CASE("VM: named function calls - CallNamedFunc (B1) stores into the variable below the arguments, B0 discards")
{
	Fx fx;
	REQUIRE(fx.run([&](TestMovie &, Asm &a) {
		a.constantPool({ "twice" });
		a.defineFunction("twice", { "n" }, [&](Asm &f) { f.getStringVar("n").pushByte(2).op(APT_OP_MULTIPLY).op(APT_OP_RETURN); });
		a.pushString("res").pushByte(21).pushByte(1).opU8(APT_OP_EA_CALLNAMEDFUNC, 0);  // res = twice(21)
		a.pushByte(21).pushByte(1).opU8(APT_OP_EA_CALLNAMEDFUNCPOP, 0);               // result dropped
		a.pushByte(4).pushByte(1).pushString("twice").op(APT_OP_EA_CALLFUNCPOP);      // 0x5B
	}));
	CHECK(fx.errorsText() == "");
	CHECK(fx.var("res").toNumber() == 42.0f);
}

TEST_CASE("VM: NewObject builds natives and script classes; InitArray and InitObject element order")
{
	Fx fx;
	REQUIRE(fx.run([&](TestMovie &, Asm &a) {
		// class Point: Point.prototype.sum = function() { return this.x + this.y }
		a.defineFunction("Point", { "x", "y" }, [&](Asm &f) {
			f.op(APT_OP_EA_PUSHTHISVAR).pushString("x").getStringVar("x").op(APT_OP_SETMEMBER);
			f.op(APT_OP_EA_PUSHTHISVAR).pushString("y").getStringVar("y").op(APT_OP_SETMEMBER);
		});
		a.getStringVar("Point").pushString("prototype").pushByte(0).op(APT_OP_INITOBJECT).op(APT_OP_SETMEMBER);
		a.getStringVar("Point").getStringMember("prototype").pushString("sum");
		a.defineFunction("", {}, [&](Asm &f) {
			f.op(APT_OP_EA_PUSHTHISVAR).getStringMember("x").op(APT_OP_EA_PUSHTHISVAR).getStringMember("y").op(APT_OP_ADD2).op(APT_OP_RETURN);
		});
		a.op(APT_OP_SETMEMBER);
		setVar(a, "p", [&] { a.pushByte(4).pushByte(3).pushByte(2).pushString("Point").op(APT_OP_NEWOBJECT); }); // new Point(3, 4)
		setVar(a, "sum", [&] { a.pushByte(0).getStringVar("p").pushString("sum").op(APT_OP_CALLMETHOD); });
		setVar(a, "arr", [&] { a.pushByte(30).pushByte(20).pushByte(10).pushByte(3).op(APT_OP_INITARRAY); }); // [10,20,30]: the top is element 0
		setVar(a, "sized", [&] { a.pushByte(5).pushByte(1).pushString("Array").op(APT_OP_NEWOBJECT); });   // new Array(5)
		setVar(a, "obj", [&] { a.pushString("a").pushByte(1).pushString("b").pushByte(2).pushByte(2).op(APT_OP_INITOBJECT); });
		setVar(a, "unknown", [&] { a.pushByte(0).pushString("NoSuchClass").op(APT_OP_NEWOBJECT); });
	}));
	CHECK(fx.var("p").isObject());
	CHECK(fx.var("sum").toNumber() == 7.0f);
	AptValue arr = fx.var("arr");
	REQUIRE(arr.isObject());
	REQUIRE(arr.asObject()->kind() == AptObjectKind::Array);
	const AptArray *items = static_cast<const AptArray *>(arr.asObject());
	REQUIRE(items->items.size() == 3);
	CHECK(items->items[0].asInteger() == 10);
	CHECK(items->items[2].asInteger() == 30);
	CHECK(static_cast<const AptArray *>(fx.var("sized").asObject())->items.size() == 5);
	AptValue b;
	REQUIRE(fx.var("obj").asObject()->getMember("b", b));
	CHECK(b.asInteger() == 2); // InitObject pops value then name for each property
	CHECK(fx.var("unknown").isUndefined());
	REQUIRE(fx.vm.errors().size() == 1);
	CHECK(fx.vm.errors()[0].find("class 'NoSuchClass' is not defined") != std::string::npos);
}

namespace
{
// Pops `count` names then the terminator off the value stack into variables n0.. and "end".
void popEnumeration(TestMovie &m, Asm &a, int count)
{
	std::vector<std::uint32_t> regs;
	for (int k = 0; k <= count; ++k)
	{
		regs.push_back(m.constRegister((std::uint32_t)(k + 1)));
	}
	for (int k = 0; k <= count; ++k)
	{
		a.setRegister(k + 1).op(APT_OP_POP);
	}
	for (int k = 0; k < count; ++k)
	{
		setVar(a, ("n" + std::to_string(k)).c_str(), [&] { a.pushDataIdx({ regs[(std::size_t)k] }); });
	}
	setVar(a, "end", [&] { a.pushDataIdx({ regs[(std::size_t)count] }); });
}
} // namespace

TEST_CASE("VM: Enumerate2 pushes the undefined terminator, then own hash slots, then the __proto__ chain (BFME2 0x00B00170)")
{
	// Expected orders come from the independent model of the BFME2 AptNativeHash in engine/tests/data/apt/apt_hash_model.py,
	// decoded from 0x00B0AC90 (Set), 0x00B0AF90 (Find) and 0x00B0ABC0 (resize): own names land in slots
	// two=1 three=3 four=5 five=6 one=7 of the size-8 table; the prototype's in shared=4 own=7.
	Fx fx;
	AptObject *proto = fx.vm.newObject();
	proto->setMember("shared", AptValue::integer(1));
	proto->setMember("own", AptValue::integer(2));
	AptObject *o = fx.vm.newObject();
	o->setProto(proto);
	for (const char *n : { "one", "two", "three", "four", "five" })
	{
		o->setMember(n, AptValue::integer(1));
	}
	o->setMember("prototype", AptValue::integer(9)); // reserved: never enumerated
	fx.root->setMember("o", AptValue::object(o));
	const std::vector<std::string> expected = { "two", "three", "four", "five", "one", "shared", "own" };
	REQUIRE(fx.run([&](TestMovie &m, Asm &a) {
		a.getStringVar("o").op(APT_OP_ENUMERATE2);
		popEnumeration(m, a, (int)expected.size());
	}));
	CHECK(fx.errorsText() == "");
	// the last name pushed is popped first
	for (std::size_t k = 0; k < expected.size(); ++k)
	{
		CHECK_MESSAGE(fx.var(("n" + std::to_string(k)).c_str()).toString() == expected[expected.size() - 1 - k], "pop " << k);
	}
	CHECK(fx.var("end").isUndefined()); // the terminator is the undefined singleton, below every name
	CHECK(fx.vm.faultCount() == 0);
}

TEST_CASE("VM: Enumerate2 on an array, function or clip is a reported acceptance stop, not a silent empty list")
{
	Fx fx;
	fx.root->setMember("arr", AptValue::object(fx.vm.newArray()));
	REQUIRE(fx.run([&](TestMovie &, Asm &a) { a.getStringVar("arr").op(APT_OP_ENUMERATE2).op(APT_OP_POP); }));
	REQUIRE(fx.vm.errors().size() == 1);
	CHECK(fx.vm.errors()[0].find("acceptance stop") != std::string::npos);
}

TEST_CASE("VM: Enumerate2 sums to the loop the old test counted: two members, terminator ends the loop")
{
	Fx fx;
	AptObject *o = fx.vm.newObject();
	o->setMember("one", AptValue::integer(1));
	o->setMember("two", AptValue::integer(2));
	fx.root->setMember("o", AptValue::object(o));
	REQUIRE(fx.run([&](TestMovie &, Asm &a) {
		setVar(a, "count", [&] { a.pushByte(0); }); // Increment keeps a SWF 7 undefined undefined (BFME2 0x00B03F40)
		a.getStringVar("o").op(APT_OP_ENUMERATE2);
		a.label("loop");
		a.setRegister(0).op(APT_OP_EA_PUSHNULL).op(APT_OP_EQUALS2).branchIfTrue("done");
		a.pushString("count").getStringVar("count").op(APT_OP_INCREMENT).op(APT_OP_SETVARIABLE);
		a.branchAlways("loop");
		a.label("done");
	}));
	CHECK(fx.var("count").toInteger() == 2);
}

// ---- resource bounds ---------------------------------------------------------------------------

TEST_CASE("VM: array index and length growth are bounded and reported (resource bound)")
{
	Fx fx;
	AptArray *arr = fx.vm.newArray();
	fx.root->setMember("arr", AptValue::object(arr));
	fx.vm.setMember(AptValue::object(arr), "999999999", AptValue::integer(1));
	CHECK(arr->items.empty());
	REQUIRE(fx.vm.errors().size() == 1);
	CHECK(fx.vm.errors()[0].find("exceeds the resource bound") != std::string::npos);
	fx.vm.setMember(AptValue::object(arr), "length", AptValue::integer(1 << 30));
	CHECK(arr->items.empty());
	CHECK(fx.vm.errors().size() == 2);
	// growth within the bound still works
	fx.vm.setMember(AptValue::object(arr), "3", AptValue::integer(7));
	CHECK(arr->items.size() == 4);
	CHECK(fx.vm.errors().size() == 2);
	// the same through the SetMember opcode
	REQUIRE(fx.run([&](TestMovie &, Asm &a) { a.getStringVar("arr").pushString("999999999").pushByte(1).op(APT_OP_SETMEMBER); }));
	CHECK(arr->items.size() == 4);
	CHECK(fx.vm.errors().size() == 3);
}

TEST_CASE("VM: the value stack is bounded: a push loop faults with a reported overflow")
{
	Fx fx;
	CHECK_FALSE(fx.run([&](TestMovie &, Asm &a) {
		a.label("grow");
		a.pushByte(1);
		a.branchAlways("grow");
	}));
	REQUIRE(fx.vm.errors().size() == 1);
	CHECK(fx.vm.errors()[0].find("value stack overflow") != std::string::npos);
	CHECK(fx.vm.faultCount() == 1);
}

TEST_CASE("VM: Enumerate2 reports the acceptance stop when an array, function or clip is reached through the prototype chain")
{
	Fx fx;
	AptFunction *fn = fx.vm.newNativeFunction("f", [](AptCallInfo &) -> AptValue { return AptValue(); });
	std::vector<AptObject *> specials = { fx.vm.newArray(), fn, fx.gc.create<TestClip>() };
	for (AptObject *special : specials)
	{
		fx.vm.clearErrors();
		AptObject *o = fx.vm.newObject();
		o->setMember("own", AptValue::integer(1));
		o->setProto(special);
		fx.root->setMember("o", AptValue::object(o));
		REQUIRE(fx.run([&](TestMovie &, Asm &a) { a.getStringVar("o").op(APT_OP_ENUMERATE2).op(APT_OP_POP); }));
		REQUIRE(fx.vm.errors().size() == 1);
		CHECK(fx.vm.errors()[0].find("inherited") != std::string::npos);
		CHECK(fx.vm.errors()[0].find("acceptance stop") != std::string::npos);
	}
}

TEST_CASE("VM: Enumerate2 resolves a string operand as a variable name (BFME2 0x00B001D4 -> 0x00AFFD80, the call GetVariable makes at 0x00B01940)")
{
	Fx fx;
	AptObject *o = fx.vm.newObject();
	o->setMember("x", AptValue::integer(1));
	fx.root->setMember("o", AptValue::object(o));
	REQUIRE(fx.run([&](TestMovie &m, Asm &a) {
		a.pushString("o").op(APT_OP_ENUMERATE2); // the operand is the string "o", not the object
		popEnumeration(m, a, 1);
	}));
	CHECK(fx.errorsText() == "");
	CHECK(fx.var("n0").toString() == "x");
	CHECK(fx.var("end").isUndefined());

	// a string naming nothing resolves to undefined: a reported stop, never a silent empty list
	Fx other;
	REQUIRE(other.run([&](TestMovie &, Asm &a) { a.pushString("noSuchVariable").op(APT_OP_ENUMERATE2).op(APT_OP_POP); }));
	REQUIRE(other.vm.errors().size() == 1);
	CHECK(other.vm.errors()[0].find("non-object") != std::string::npos);
}

TEST_CASE("object model: __proto__ assigned a non-object keeps the value (BFME2 0x00ADB36E setter, 0x00B0B3F4 getter)")
{
	Fx fx;
	AptObject *o = fx.vm.newObject();
	fx.root->setMember("o", AptValue::object(o));
	REQUIRE(fx.run([&](TestMovie &, Asm &a) {
		a.getStringVar("o").pushString("__proto__").pushByte(5).op(APT_OP_SETMEMBER);
		setVar(a, "back", [&] { a.getStringVar("o").getStringMember("__proto__"); });
	}));
	CHECK(fx.errorsText() == "");
	CHECK(fx.var("back").isInteger());
	CHECK(fx.var("back").asInteger() == 5);
	// the chain does not continue through a non-object link
	AptValue ignored;
	CHECK_FALSE(o->getMember("anything", ignored));
	// an object assigned afterwards replaces it and continues the chain
	AptObject *proto = fx.vm.newObject();
	proto->setMember("inherited", AptValue::integer(7));
	o->setMember("__proto__", AptValue::object(proto));
	REQUIRE(o->getMember("inherited", ignored));
	CHECK(ignored.asInteger() == 7);
	REQUIRE(o->getOwn("__proto__", ignored));
	CHECK(ignored.asObject() == proto);
	// for-in cannot walk a non-object link: reported, not guessed
	AptObject *p = fx.vm.newObject();
	p->setMember("__proto__", AptValue::string("text"));
	std::vector<std::string> names;
	std::string error;
	CHECK_FALSE(p->enumerateForIn(names, error));
	CHECK(error.find("non-object __proto__") != std::string::npos);
	AptValue back;
	REQUIRE(p->getOwn("__proto__", back));
	CHECK(back.asString() == "text");
	CHECK(p->deleteOwn("__proto__"));
	CHECK_FALSE(p->getOwn("__proto__", back));
}

TEST_CASE("VM: storing a property over a native member of the same object is a reported acceptance stop, never a silent order")
{
	Fx fx;
	// Math is a native member of the global object; toString a native member of Object.prototype
	fx.vm.setMember(AptValue::object(fx.vm.global()), "Math", AptValue::integer(1));
	REQUIRE(fx.vm.errors().size() == 1);
	CHECK(fx.vm.errors()[0].find("built-in native member") != std::string::npos);
	CHECK(fx.vm.errors()[0].find("acceptance stop") != std::string::npos);
	fx.vm.clearErrors();
	fx.vm.setMember(AptValue::object(fx.vm.objectPrototype()), "toString", AptValue::integer(1));
	CHECK(fx.vm.errors().size() == 1);
	fx.vm.clearErrors();
	// a script object that merely inherits the native does not collide; neither does an unrelated name
	AptObject *o = fx.vm.newObject();
	fx.vm.setMember(AptValue::object(o), "toString", AptValue::integer(1));
	fx.vm.setMember(AptValue::object(fx.vm.global()), "SomethingNew", AptValue::integer(1));
	CHECK(fx.vm.errors().empty());
	// the same through the SetVariable opcode (the interpreter's variable stores report too)
	REQUIRE(fx.run([&](TestMovie &, Asm &a) { a.pushString("Math").pushByte(2).op(APT_OP_SETVARIABLE); }));
	CHECK(fx.vm.errors().size() <= 1);
}

TEST_CASE("Array natives respect the array bound (resource bound): constructor, push, unshift, concat and splice")
{
	Fx fx;
	AptValue arrayClass = fx.vm.getMember(AptValue::object(fx.vm.global()), "Array");
	REQUIRE(arrayClass.isObject());
	// new Array(999999999)
	AptValue huge = fx.vm.callFunction(arrayClass, AptValue(), { AptValue::integer(999999999) }, true);
	CHECK(huge.isUndefined());
	REQUIRE(fx.vm.errors().size() == 1);
	CHECK(fx.vm.errors()[0].find("resource bound") != std::string::npos);
	fx.vm.clearErrors();
	// within the bound still works
	AptValue ok = fx.vm.callFunction(arrayClass, AptValue(), { AptValue::integer(10) }, true);
	REQUIRE(ok.isObject());
	CHECK(static_cast<AptArray *>(ok.asObject())->items.size() == 10);

	AptArray *full = fx.vm.newArray();
	full->items.resize(AptArray::kMaxLength);
	AptValue fullValue = AptValue::object(full);
	AptArray *spare = fx.vm.newArray();
	spare->items.resize(2);
	std::size_t calls = 0;
	for (const char *method : { "push", "unshift", "concat", "splice" })
	{
		fx.vm.clearErrors();
		std::vector<AptValue> args = { AptValue::integer(1) };
		if (std::string(method) == "splice")
		{
			args = { AptValue::integer(0), AptValue::integer(0), AptValue::integer(1) }; // insert one element at 0
		}
		else if (std::string(method) == "concat")
		{
			args = { AptValue::object(spare) };
		}
		fx.vm.callMethod(fullValue, method, args);
		REQUIRE_MESSAGE(fx.vm.errors().size() == 1, method);
		CHECK_MESSAGE(fx.vm.errors()[0].find("resource bound") != std::string::npos, method);
		CHECK_MESSAGE(full->items.size() == AptArray::kMaxLength, method); // unchanged
		++calls;
	}
	CHECK(calls == 4);
}

TEST_CASE("Boolean() follows the BFME2 native at 0x00AFF850 (Boolean(true) is false in retail)")
{
	Fx fx;
	AptValue boolean = fx.vm.getMember(AptValue::object(fx.vm.global()), "Boolean");
	REQUIRE(boolean.isObject());
	auto call = [&](std::vector<AptValue> args) { return fx.vm.callFunction(boolean, AptValue(), args); };
	AptObject *o = fx.vm.newObject();
	float nan = std::numeric_limits<float>::quiet_NaN();
	CHECK(call({}).isUndefined()); // argc == 0: the undefined singleton (0x00AFF880)
	CHECK(call({ AptValue::object(o) }).asBool()); // objects: true (0x00ADC580 types 12..19)
	CHECK(call({ AptValue::object(fx.vm.newArray()) }).asBool());
	CHECK_FALSE(call({ AptValue() }).asBool()); // the undefined singleton: false (0x00AFF8E1)
	CHECK_FALSE(call({ AptValue::integer(0) }).asBool());
	CHECK(call({ AptValue::integer(-3) }).asBool());
	CHECK_FALSE(call({ AptValue::number(0.0f) }).asBool());
	CHECK(call({ AptValue::number(2.5f) }).asBool());
	CHECK(call({ AptValue::number(nan) }).asBool()); // fucompp unordered -> true (test ah,0x44 / jp at 0x00AFF920)
	CHECK(call({ AptValue::string("5") }).asBool()); // numeric string: toNumber != 0
	CHECK_FALSE(call({ AptValue::string("0") }).asBool());
	CHECK_FALSE(call({ AptValue::string("abc") }).asBool()); // isNonNumeric -> false (0x00AFF908)
	CHECK_FALSE(call({ AptValue::string("") }).asBool());
	// booleans reach isNonNumeric, which is true for them: Boolean(true) is false (the isBoolean arm at
	// 0x00AFF92E is only reachable from integers and floats)
	CHECK_FALSE(call({ AptValue::boolean(true) }).asBool());
	CHECK_FALSE(call({ AptValue::boolean(false) }).asBool());
	CHECK_FALSE(call({ AptValue::externValue() }).asBool());
	CHECK(call({ AptValue::integer(1), AptValue::integer(0) }).asBool()); // only the first argument (the stack top) is read
	CHECK(fx.errorsText() == "");
}

TEST_CASE("String.split respects the array bound (resource bound S-009)")
{
	Fx fx;
	// 262,145 characters split by "" would make 2^18 + 1 elements
	AptValue refused = fx.vm.callMethod(AptValue::string(std::string(AptArray::kMaxLength + 1, 'a')), "split", { AptValue::string("") });
	CHECK(refused.isUndefined());
	REQUIRE(fx.vm.errors().size() == 1);
	CHECK(fx.vm.errors()[0].find("Array.split") != std::string::npos);
	CHECK(fx.vm.errors()[0].find("resource bound") != std::string::npos);
	// the separator path counts pieces the same way
	fx.vm.clearErrors();
	std::string commas;
	for (std::size_t i = 0; i < AptArray::kMaxLength; ++i)
	{
		commas += ',';
	}
	CHECK(fx.vm.callMethod(AptValue::string(commas), "split", { AptValue::string(",") }).isUndefined()); // 2^18 + 1 pieces
	CHECK(fx.vm.errors().size() == 1);
	// exactly at the bound is allowed
	fx.vm.clearErrors();
	AptValue ok = fx.vm.callMethod(AptValue::string(std::string(AptArray::kMaxLength, 'a')), "split", { AptValue::string("") });
	REQUIRE(ok.isObject());
	CHECK(static_cast<AptArray *>(ok.asObject())->items.size() == AptArray::kMaxLength);
	CHECK(fx.vm.errors().empty());
}

TEST_CASE("VM: constants with no decoded runtime meaning are reported acceptance stops (S-013)")
{
	for (std::uint32_t type : { (std::uint32_t)APT_CONST_NONE, (std::uint32_t)APT_CONST_LOOKUP, (std::uint32_t)APT_CONST_PROPERTY, (std::uint32_t)APT_CONST_UNDEFINED })
	{
		Fx fx;
		std::string expected;
		REQUIRE(fx.run([&](TestMovie &m, Asm &a) {
			std::uint32_t c = m.constTyped(type, type == APT_CONST_NONE ? 0 : 3); // the .const parser requires raw 0 for None
			setVar(a, "v", [&] { a.pushDataIdx({ c }); });
		}));
		REQUIRE_MESSAGE(fx.vm.errors().size() == 1, "constant type " << type);
		CHECK_MESSAGE(fx.vm.errors()[0].find("has no verified runtime meaning (acceptance stop S-013)") != std::string::npos, fx.vm.errors()[0]);
		CHECK(fx.var("v").isUndefined()); // the stop pushes undefined after reporting
		CHECK(fx.vm.faultCount() == 0);
	}
}

TEST_CASE("VM: ToNumber of a string in the subnormal double range is a reported acceptance stop (S-017)")
{
	Fx fx;
	REQUIRE(fx.run([&](TestMovie &, Asm &a) {
		setVar(a, "tiny", [&] { a.pushString("4.94065645841246544177e-324").op(APT_OP_TONUMBER); });
		setVar(a, "ok", [&] { a.pushString("1.5").op(APT_OP_TONUMBER); });
	}));
	REQUIRE(fx.vm.errors().size() == 1);
	CHECK(fx.vm.errors()[0].find("S-017") != std::string::npos);
	CHECK(fx.vm.faultCount() == 0);
	CHECK(fx.var("ok").toNumber() == 1.5f);
}

TEST_CASE("VM: DefineFunction2 PreloadSuper is a reported acceptance stop (S-014)")
{
	Fx fx;
	REQUIRE(fx.run([&](TestMovie &, Asm &a) {
		a.defineFunction2("usesSuper", 3, APT_FN2_PRELOAD_SUPER | APT_FN2_SUPPRESS_THIS | APT_FN2_SUPPRESS_ARGUMENTS, {}, [&](Asm &f) { f.op(APT_OP_EA_PUSHUNDEFINED).op(APT_OP_RETURN); });
		a.pushByte(0).pushString("usesSuper").op(APT_OP_CALLFUNCTION).op(APT_OP_POP);
	}));
	REQUIRE(fx.vm.errors().size() == 1);
	CHECK(fx.vm.errors()[0].find("PreloadSuper has no decoded behaviour (acceptance stop S-014; function 'usesSuper')") != std::string::npos);
	CHECK(fx.vm.faultCount() == 0);
}

TEST_CASE("VM: a prototype chain longer than 64 objects reports its limit instead of reading as a miss (S-015)")
{
	Fx fx;
	// chain[0] is the object read from; chain[n-1] holds x.  A chain of 64 objects is fully searched.
	auto build = [&](int length) {
		std::vector<AptObject *> chain;
		for (int i = 0; i < length; ++i)
		{
			chain.push_back(fx.vm.newObject());
			chain.back()->setProto(nullptr);
		}
		for (int i = 0; i + 1 < length; ++i)
		{
			chain[(std::size_t)i]->setProto(chain[(std::size_t)i + 1]);
		}
		chain.back()->setMember("x", AptValue::integer(42));
		return chain.front();
	};
	AptObject *ok = build(64);
	AptValue v = fx.vm.getMember(AptValue::object(ok), "x");
	CHECK(v.asInteger() == 42);
	CHECK(fx.vm.errors().empty());
	// 65 objects: the last one is not searched; that is reported, and is not the same as a missing property
	AptObject *tooLong = build(65);
	v = fx.vm.getMember(AptValue::object(tooLong), "x");
	CHECK(v.isUndefined());
	REQUIRE(fx.vm.errors().size() == 1);
	CHECK(fx.vm.errors()[0].find("exceeded 64 objects") != std::string::npos);
	CHECK(fx.vm.errors()[0].find("S-015") != std::string::npos);
	// a genuine miss on a short chain stays silent
	fx.vm.clearErrors();
	CHECK(fx.vm.getMember(AptValue::object(ok), "absent").isUndefined());
	CHECK(fx.vm.errors().empty());
}

// ---- malformed UTF-8 ---------------------------------------------------------------------------

TEST_CASE("String natives decode valid UTF-8 code points (Unicode standard) and report malformed text instead of reading past it")
{
	Fx fx;
	auto code = [&](const std::string &text, int index) { return fx.vm.callMethod(AptValue::string(text), "charCodeAt", { AptValue::integer(index) }); };
	CHECK(code("A", 0).asString() == "65");
	CHECK(code("\xC3\xA9", 0).asString() == "233");               // U+00E9
	CHECK(code("\xE2\x82\xAC", 0).asString() == "8364");           // U+20AC
	CHECK(code("\xF0\x9F\x98\x80", 0).asString() == "128512");     // U+1F600
	CHECK(code("a\xC3\xA9z", 2).asString() == "122");              // code point indexing, not bytes
	CHECK(fx.errorsText() == "");

	// the review's case: a lead byte F0 followed by one continuation byte and the terminator
	CHECK(code(std::string("\xF0\x80", 2), 0).isUndefined());
	REQUIRE(fx.vm.errors().size() == 1);
	CHECK(fx.vm.errors()[0].find("malformed UTF-8") != std::string::npos);
	CHECK(fx.vm.errors()[0].find("truncated") != std::string::npos);
	fx.vm.clearErrors();
	// a stray continuation byte, a missing continuation byte and an invalid lead byte
	CHECK(code("\x80", 0).isUndefined());
	CHECK(code("\xC3z", 0).isUndefined());
	CHECK(code("\xFF", 0).isUndefined());
	CHECK(fx.vm.errors().size() == 3);
	// every code point native reports instead of crashing
	fx.vm.clearErrors();
	for (const char *method : { "charAt", "slice", "substr", "substring" })
	{
		fx.vm.callMethod(AptValue::string(std::string("\xF0\x80", 2)), method, { AptValue::integer(0), AptValue::integer(1) });
	}
	fx.vm.callMethod(AptValue::string(std::string("\xF0\x80", 2)), "split", { AptValue::string("") }); // empty separator splits code points
	CHECK(fx.vm.errors().size() == 5);
	// length counts code points for valid text and reports for malformed text
	fx.vm.clearErrors();
	CHECK(fx.vm.getMember(AptValue::string("a\xC3\xA9z"), "length").asInteger() == 3);
	CHECK(fx.vm.getMember(AptValue::string(std::string("\xF0\x80", 2)), "length").isUndefined());
	CHECK(fx.vm.errors().size() == 1);
}

// ---- natives -----------------------------------------------------------------------------------

TEST_CASE("VM natives: String methods follow the EA bodies (substr, substring, slice, indexOf, charAt, charCodeAt as a string)")
{
	Fx fx;
	auto call = [&](const char *method, std::vector<AptValue> args, const char *text = "Hello World") {
		return fx.vm.callMethod(AptValue::string(text), method, args);
	};
	CHECK(call("substr", { AptValue::integer(6) }).asString() == "World");
	CHECK(call("substr", { AptValue::integer(-5), AptValue::integer(3) }).asString() == "Wor");
	CHECK(call("substr", { AptValue::integer(0), AptValue::integer(5) }).asString() == "Hello");
	CHECK(call("substring", { AptValue::integer(6), AptValue::integer(0) }).asString() == "Hello "); // bounds are swapped
	CHECK(call("substring", { AptValue::integer(-3), AptValue::integer(5) }).asString() == "Hello");
	CHECK(call("slice", { AptValue::integer(-5) }).asString() == "World");
	CHECK(call("slice", { AptValue::integer(0), AptValue::integer(-6) }).asString() == "Hello");
	CHECK(call("indexOf", { AptValue::string("o") }).asInteger() == 4);
	CHECK(call("indexOf", { AptValue::string("o"), AptValue::integer(5) }).asInteger() == 7);
	CHECK(call("indexOf", { AptValue::string("zz") }).asInteger() == -1);
	CHECK(call("charAt", { AptValue::integer(1) }).asString() == "e");
	CHECK(call("charAt", { AptValue::integer(99) }).isUndefined());
	AptValue code = call("charCodeAt", { AptValue::integer(0) });
	CHECK(code.isString());                  // the EA body returns the decimal text, not a number
	CHECK(code.asString() == "72");
	CHECK(call("toUpperCase", {}).asString() == "HELLO WORLD");
	CHECK(call("toLowerCase", {}).asString() == "hello world");
	AptValue parts = call("split", { AptValue::string(" ") });
	REQUIRE(parts.isObject());
	REQUIRE(static_cast<const AptArray *>(parts.asObject())->items.size() == 2);
	CHECK(static_cast<const AptArray *>(parts.asObject())->items[1].asString() == "World");
	// length counts UTF-8 code points
	CHECK(fx.vm.getMember(AptValue::string("h\xC3\xA9llo"), "length").asInteger() == 5);
	CHECK(call("substr", { AptValue::integer(1), AptValue::integer(1) }, "h\xC3\xA9llo").asString() == "\xC3\xA9");
	CHECK(fx.errorsText() == "");
}

TEST_CASE("VM natives: Array methods and Math (aptMath*.cpp: round and abs return integers, min/max by behaviour)")
{
	Fx fx;
	AptArray *arr = fx.vm.newArray();
	AptValue av = AptValue::object(arr);
	auto call = [&](const char *method, std::vector<AptValue> args) { return fx.vm.callMethod(av, method, args); };
	CHECK(call("push", { AptValue::integer(3), AptValue::integer(1), AptValue::integer(2) }).asInteger() == 3);
	CHECK(fx.vm.getMember(av, "length").asInteger() == 3);
	CHECK(call("join", { AptValue::string("-") }).asString() == "3-1-2");
	CHECK(call("join", {}).asString() == "3,1,2");
	CHECK(call("pop", {}).asInteger() == 2);
	call("unshift", { AptValue::integer(9) });
	CHECK(call("shift", {}).asInteger() == 9);
	AptValue sliced = call("slice", { AptValue::integer(1) });
	CHECK(static_cast<const AptArray *>(sliced.asObject())->items.size() == 1);
	AptValue cat = call("concat", { sliced, AptValue::integer(5) });
	CHECK(static_cast<const AptArray *>(cat.asObject())->items.size() == 4); // arrays flatten
	call("push", { AptValue::integer(0) });
	call("sort", {});
	CHECK(call("join", {}).asString() == "0,1,3");
	call("reverse", {});
	CHECK(call("join", {}).asString() == "3,1,0");
	AptValue removed = call("splice", { AptValue::integer(1), AptValue::integer(1) });
	CHECK(static_cast<const AptArray *>(removed.asObject())->items.size() == 1);
	CHECK(call("toString", {}).asString() == "3,0");

	AptValue math = fx.vm.getMember(AptValue::object(fx.vm.global()), "Math");
	auto m = [&](const char *fn, std::vector<AptValue> args) { return fx.vm.callMethod(math, fn, args); };
	CHECK(m("round", { AptValue::number(2.5f) }).isInteger());
	CHECK(m("round", { AptValue::number(2.5f) }).asInteger() == 3);
	CHECK(m("round", { AptValue::number(-2.5f) }).asInteger() == -3);
	CHECK(m("round", { AptValue::number(2.4f) }).asInteger() == 2);
	CHECK(m("abs", { AptValue::number(-3.7f) }).asInteger() == 3); // abs(toInteger(x))
	CHECK(m("max", { AptValue::integer(2), AptValue::integer(9) }).asFloat() == 9.0f);
	CHECK(m("min", { AptValue::integer(2), AptValue::integer(9) }).asFloat() == 2.0f);
	CHECK(m("floor", { AptValue::number(2.9f) }).asFloat() == 2.0f);
	CHECK(m("ceil", { AptValue::number(2.1f) }).asFloat() == 3.0f);
	CHECK(m("pow", { AptValue::integer(2), AptValue::integer(10) }).asFloat() == 1024.0f);
	CHECK(m("sqrt", { AptValue::integer(9) }).asFloat() == 3.0f);
	CHECK(std::fabs(m("atan2", { AptValue::integer(1), AptValue::integer(1) }).asFloat() - 0.7853982f) < 1e-6f);
	CHECK(m("sin", { AptValue::integer(0) }).asFloat() == 0.0f);
	CHECK(m("max", { AptValue::integer(2) }).isUndefined()); // argc < 2 -> fallback
	CHECK(fx.errorsText() == "");
}

// ---- host routing ------------------------------------------------------------------------------

TEST_CASE("VM: getURL and getURL2 route FSCommand:, .swf, empty and other urls to the host")
{
	Fx fx;
	REQUIRE(fx.run([&](TestMovie &, Asm &a) {
		a.getURL("FSCommand:AptMainMenu::Skirmish", "1");
		a.getURL("FSCommand:", "empty-command");
		a.getURL("SkirmishOpenPlay.swf", "_level1");
		a.getURL("UPPER.SWF", "x");
		a.getURL("", "_level2");
		a.getURL("http://example.invalid/", "_blank");
		a.getURL("fscommand:lowercase", "no"); // the prefix test is case-sensitive
		a.pushString("FSCommand:PlaySound").pushString("Gui_Click").op(APT_OP_GETURL2); // url under the target
		a.pushString("FSCommand:").pushString("a").op(APT_OP_STRINGCONCAT).pushString("arg").op(APT_OP_GETURL2);
	}));
	REQUIRE(fx.host.fscommands.size() == 4);
	CHECK(fx.host.fscommands[0].command == "AptMainMenu::Skirmish");
	CHECK(fx.host.fscommands[0].argument == "1");
	CHECK(fx.host.fscommands[1].command == "");
	CHECK(fx.host.fscommands[1].argument == "empty-command");
	CHECK(fx.host.fscommands[2].command == "PlaySound");
	CHECK(fx.host.fscommands[2].argument == "Gui_Click");
	CHECK(fx.host.fscommands[3].command == "a");
	CHECK(fx.host.fscommands[3].argument == "arg");
	REQUIRE(fx.host.loads.size() == 3);
	CHECK(fx.host.loads[0].movie == "SkirmishOpenPlay");
	CHECK(fx.host.loads[0].target == "_level1");
	CHECK(fx.host.loads[1].movie == "UPPER");
	CHECK(fx.host.loads[2].movie == "");        // empty url: unload
	CHECK(fx.host.loads[2].target == "_level2");
	REQUIRE(fx.host.urls.size() == 2);
	CHECK(fx.host.urls[0].command == "http://example.invalid/");
	CHECK(fx.host.urls[1].command == "fscommand:lowercase");
}

TEST_CASE("VM: Trace and Random go through the host")
{
	Fx fx;
	REQUIRE(fx.run([&](TestMovie &, Asm &a) {
		a.pushString("hello ").pushByte(3).op(APT_OP_ADD2).op(APT_OP_TRACE);
		setVar(a, "r", [&] { a.pushByte(10).op(APT_OP_RANDOM); });
		setVar(a, "u", [&] { a.op(APT_OP_EA_PUSHUNDEFINED).op(APT_OP_RANDOM); });
	}));
	REQUIRE(fx.host.traces.size() == 1);
	CHECK(fx.host.traces[0] == "hello 3");
	CHECK(fx.var("r").isInteger());
	CHECK(fx.var("r").asInteger() >= 0);
	CHECK(fx.var("r").asInteger() < 10);
	CHECK(fx.var("u").asInteger() == 0); // AptActionInterpreterRandom.cpp: undefined input yields zero
}

TEST_CASE("VM: the extern object exchanges strings with the host provider")
{
	Fx fx;
	fx.host.setExternValue("InGame", "1");
	fx.host.setExternValue("AptX::Count", "42");
	fx.host.setExternUndefined("Nope");      // a provider that answers "no value": reads as undefined, no error
	fx.host.setExternUndefined("AptX::Flag"); // providers that accept the writes below
	fx.host.setExternUndefined("AptX::Number");
	REQUIRE(fx.run([&](TestMovie &, Asm &a) {
		a.constantPool({ "extern", "InGame" });
		setVar(a, "ingame", [&] { a.pushValueOfVar(0).getNamedMember(1); });
		setVar(a, "count", [&] { a.pushValueOfVar(0).pushString("AptX::Count").op(APT_OP_GETMEMBER); });
		setVar(a, "absent", [&] { a.pushValueOfVar(0).pushString("Nope").op(APT_OP_GETMEMBER); });
		a.pushValueOfVar(0).pushString("AptX::Flag").op(APT_OP_EA_PUSHTRUE).op(APT_OP_SETMEMBER);
		a.pushValueOfVar(0).pushString("AptX::Number").pushFloat(1.5f).op(APT_OP_SETMEMBER);
	}));
	CHECK(fx.var("ingame").asString() == "1");
	CHECK(fx.var("count").asString() == "42");
	CHECK(fx.var("absent").isUndefined());
	REQUIRE(fx.host.externSets.size() == 2);
	CHECK(fx.host.externSets[0].command == "AptX::Flag");
	CHECK(fx.host.externSets[0].argument == "true");
	CHECK(fx.host.externSets[1].argument == "1.500000"); // getName of a float
	CHECK(fx.errorsText() == "");
}

TEST_CASE("VM: extern distinguishes a provider answering undefined from a name nobody registered")
{
	Fx fx;
	fx.host.setExternUndefined("Declared");
	REQUIRE(fx.run([&](TestMovie &, Asm &a) {
		a.constantPool({ "extern" });
		setVar(a, "declared", [&] { a.pushValueOfVar(0).pushString("Declared").op(APT_OP_GETMEMBER); });
		setVar(a, "missing", [&] { a.pushValueOfVar(0).pushString("Misspelled").op(APT_OP_GETMEMBER); });
		a.pushValueOfVar(0).pushString("AlsoMissing").pushByte(1).op(APT_OP_SETMEMBER);
		a.pushValueOfVar(0).pushString("Declared").pushByte(2).op(APT_OP_SETMEMBER);
	}));
	CHECK(fx.var("declared").isUndefined());
	CHECK(fx.var("missing").isUndefined());
	// exactly two errors: the unregistered read and the unregistered write; the declared name is silent
	REQUIRE(fx.vm.errors().size() == 2);
	CHECK(fx.vm.errors()[0].find("extern.Misspelled read: no extern provider is registered") != std::string::npos);
	CHECK(fx.vm.errors()[1].find("extern.AlsoMissing write: no extern provider is registered") != std::string::npos);
	REQUIRE(fx.host.externSets.size() == 1);
	CHECK(fx.host.externSets[0].command == "Declared");
	CHECK(fx.host.externSets[0].argument == "2");
	CHECK(fx.vm.faultCount() == 0); // survivable script errors, not VM faults
}

TEST_CASE("VM: typeof extern is the empty string and PushNull is the undefined singleton (BFME1 TypeOf.cpp:86-112, BFME2 0x00B05320)")
{
	Fx fx;
	REQUIRE(fx.run([&](TestMovie &, Asm &a) {
		a.constantPool({ "extern" });
		setVar(a, "tExtern", [&] { a.pushValueOfVar(0).op(APT_OP_TYPEOF); });
		setVar(a, "tNull", [&] { a.op(APT_OP_EA_PUSHNULL).op(APT_OP_TYPEOF); });
		setVar(a, "nullToString", [&] { a.op(APT_OP_EA_PUSHNULL).op(APT_OP_TOSTRING); });
		setVar(a, "nullEqUndef", [&] { a.op(APT_OP_EA_PUSHNULL).op(APT_OP_EA_PUSHUNDEFINED).op(APT_OP_EQUALS2); });
		setVar(a, "nullEqZero", [&] { a.op(APT_OP_EA_PUSHNULL).pushByte(0).op(APT_OP_EQUALS2); });
	}));
	CHECK(fx.errorsText() == "");
	CHECK(fx.var("tExtern").isString());
	CHECK(fx.var("tExtern").asString() == "");
	CHECK(fx.var("tNull").asString() == "undefined");
	CHECK(fx.var("nullToString").asString() == "undefined");
	CHECK(fx.var("nullEqUndef").asBool());
	CHECK_FALSE(fx.var("nullEqZero").asBool()); // SWF 7: an undefined operand is equal only to another undefined
}

TEST_CASE("VM: ToNumber, Increment and Decrement keep a SWF 7 undefined and use the integer path for hex text (BFME2 0x00B03730/0x00B03F40/0x00B04020)")
{
	for (int version : { 7, 6 })
	{
		Fx fx;
		REQUIRE(fx.run(version, [&](TestMovie &, Asm &a) {
			setVar(a, "toNumUndef", [&] { a.op(APT_OP_EA_PUSHUNDEFINED).op(APT_OP_TONUMBER); });
			setVar(a, "incUndef", [&] { a.op(APT_OP_EA_PUSHUNDEFINED).op(APT_OP_INCREMENT); });
			setVar(a, "decUndef", [&] { a.op(APT_OP_EA_PUSHUNDEFINED).op(APT_OP_DECREMENT); });
			setVar(a, "hex", [&] { a.pushString("0x10").op(APT_OP_TONUMBER); });
			setVar(a, "eqMixed", [&] { a.pushByte(1).pushFloat(1.0005f).op(APT_OP_EQUALS2); });
		}));
		CHECK_MESSAGE(fx.errorsText() == "", "version " << version);
		if (version == 7)
		{
			CHECK(fx.var("toNumUndef").isUndefined());
			CHECK(fx.var("incUndef").isUndefined());
			CHECK(fx.var("decUndef").isUndefined());
		}
		else
		{
			CHECK(fx.var("toNumUndef").isInteger()); // SWF 6: numeric, text "" -> integer 0
			CHECK(fx.var("toNumUndef").asInteger() == 0);
			CHECK(fx.var("incUndef").asFloat() == 1.0f);
			CHECK(fx.var("decUndef").asFloat() == -1.0f);
		}
		CHECK(fx.var("hex").isInteger());
		CHECK(fx.var("hex").asInteger() == 16);
		CHECK(fx.var("eqMixed").asBool()); // |1 - 1.0005| < 0.001 (BFME2 0x00B03690, constant 0x00BC28F8)
	}
}

TEST_CASE("VM: timeline opcodes are forwarded to the target clip")
{
	Fx fx;
	TestClip *clip = fx.gc.create<TestClip>();
	REQUIRE(fx.run([&](TestMovie &, Asm &a) {
		a.op(APT_OP_NEXTFRAME).op(APT_OP_PLAY).op(APT_OP_STOP);
		a.opAlignedI32(APT_OP_GOTOFRAME, 4);
		a.gotoLabel("_show");
		a.pushString("_hide").opAlignedI32(APT_OP_GOTOFRAME2, 1);  // play flag
		a.pushByte(3).opAlignedI32(APT_OP_GOTOFRAME2, 0);
		a.pushString("src").pushString("copy").pushByte(7).op(APT_OP_CLONESPRITE);
		a.pushString("victim").op(APT_OP_REMOVESPRITE);
	}, clip));
	CHECK(fx.errorsText() == "");
	std::vector<std::string> expected = { "nextFrame", "play", "stop", "gotoFrame:4", "gotoLabel:_show", "gotoFrame2:_hide:play", "gotoFrame2:3:stop",
		"clone:src:copy:7", "remove:victim" };
	CHECK(clip->log == expected);
}

TEST_CASE("VM: GetProperty/SetProperty map SWF property indices to clip members")
{
	Fx fx;
	AptObject *clip = fx.vm.newObject();
	clip->setMember("_x", AptValue::number(10));
	clip->setMember("_name", AptValue::string("hero"));
	AptObject *child = fx.vm.newObject();
	child->setMember("_alpha", AptValue::integer(50));
	clip->setMember("kid", AptValue::object(child));
	REQUIRE(fx.run([&](TestMovie &, Asm &a) {
		setVar(a, "x", [&] { a.pushString("").pushByte(0).op(APT_OP_GETPROPERTY); });          // "" = the current target
		setVar(a, "name", [&] { a.pushString("").pushByte(13).op(APT_OP_GETPROPERTY); });       // index 13 is _name
		setVar(a, "alpha", [&] { a.pushString("kid").pushByte(6).op(APT_OP_GETPROPERTY); });    // a child by name
		a.pushString("kid").pushByte(6).pushByte(25).op(APT_OP_SETPROPERTY);
		a.pushString("").pushByte(1).pushFloat(3.5f).op(APT_OP_SETPROPERTY);                    // _y
		setVar(a, "bad", [&] { a.pushString("").pushByte(99).op(APT_OP_GETPROPERTY); });
	}, clip));
	AptValue v;
	REQUIRE(clip->getMember("x", v));
	CHECK(v.asFloat() == 10.0f);
	CHECK(clip->getMember("name", v));
	CHECK(v.asString() == "hero");
	CHECK(clip->getMember("alpha", v));
	CHECK(v.asInteger() == 50);
	REQUIRE(child->getMember("_alpha", v));
	CHECK(v.asInteger() == 25);
	REQUIRE(clip->getMember("_y", v));
	CHECK(v.asFloat() == 3.5f);
	REQUIRE(fx.vm.errors().size() == 1);
	CHECK(fx.vm.errors()[0].find("unknown property index") != std::string::npos);
}

// ---- failures are loud ---------------------------------------------------------------------------

namespace
{
// A host that keeps the calls without a function apart from the script errors (the player's split, S-380)
struct CallSplitHost : AptRecordingHost
{
	std::vector<std::string> calls;
	void scriptCallWithoutFunction(const std::string &message) override { calls.push_back(message); }
};

struct CallFx
{
	AptGC gc;
	CallSplitHost host;
	AptActionInterpreter vm;
	AptObject *root;
	CallFx() : vm(gc, host) { root = vm.newObject(); }
	bool run(const std::function<void(Asm &)> &build)
	{
		TestMovie m(7);
		Asm a = m.program();
		// o = {}; o.obj = {}; o.arr = []; notFn = {}; r = "set"
		setVar(a, "o", [&] { a.pushByte(0).op(APT_OP_INITOBJECT); });
		a.getStringVar("o").pushString("obj").pushByte(0).op(APT_OP_INITOBJECT).op(APT_OP_SETMEMBER);
		a.getStringVar("o").pushString("arr").pushByte(0).op(APT_OP_INITARRAY).op(APT_OP_SETMEMBER);
		setVar(a, "notFn", [&] { a.pushByte(0).op(APT_OP_INITOBJECT); });
		setVar(a, "r", [&] { a.pushString("set"); });
		build(a);
		a.op(APT_OP_END);
		std::uint32_t off = m.commit(a);
		m.setRootFrames({ { m.addActionItem(off) } });
		AptFile f;
		std::string err;
		REQUIRE_MESSAGE(m.parse(f, &err), err);
		std::shared_ptr<const AptCodeBlock> block = f.codeAt(off, &err);
		REQUIRE_MESSAGE(block, err);
		return vm.execute(*block, root, root);
	}
	AptValue var(const char *name)
	{
		AptValue v;
		root->getMember(name, v);
		return v;
	}
};
} // namespace

TEST_CASE("stops S-380: an object or array callee is a call without a function, by method, by name and unnamed; a constructor of a non-class stays an error")
{
	struct Case
	{
		const char *what;
		std::function<void(Asm &)> call; // leaves the call's result on the stack
		const char *message;
	};
	const Case cases[] = {
		{ "method on an object member", [](Asm &a) { a.pushByte(0).getStringVar("o").pushString("obj").op(APT_OP_CALLMETHOD); }, "method 'obj' is not a function on a value of type object" },
		{ "method on an array member", [](Asm &a) { a.pushByte(0).getStringVar("o").pushString("arr").op(APT_OP_CALLMETHOD); }, "method 'arr' is not a function on a value of type object" },
		{ "named function holding an object", [](Asm &a) { a.pushByte(0).pushString("notFn").op(APT_OP_CALLFUNCTION); }, "call of undefined function 'notFn'" },
		{ "unnamed call of an object", [](Asm &a) { a.pushByte(0).getStringVar("o").pushString("obj").op(APT_OP_GETMEMBER).pushString("").op(APT_OP_CALLMETHOD); }, "call of a non-function value (object)" },
		{ "unnamed call of an array", [](Asm &a) { a.pushByte(0).getStringVar("o").pushString("arr").op(APT_OP_GETMEMBER).op(APT_OP_EA_PUSHUNDEFINED).op(APT_OP_CALLMETHOD); }, "call of a non-function value (object)" },
	};
	for (const Case &c : cases)
	{
		INFO(c.what);
		CallFx fx;
		CHECK(fx.run([&](Asm &a) {
			a.pushString("r");
			c.call(a);
			a.op(APT_OP_SETVARIABLE);
		}));
		CHECK(fx.var("r").isUndefined()); // the result of the skipped call is undefined
		CHECK(fx.host.errors.empty());
		REQUIRE(fx.host.calls.size() == 1);
		CHECK(fx.host.calls[0].rfind(c.message, 0) == 0);
		CHECK(fx.vm.faultCount() == 0);
	}
	CallFx fx;
	CHECK(fx.run([&](Asm &a) { setVar(a, "made", [&] { a.pushByte(0).pushString("notFn").op(APT_OP_NEWOBJECT); }); }));
	CHECK(fx.host.calls.empty());
	CHECK(fx.host.errors.size() == 1); // constructing from a non-class is the port's reported error, not a skipped call
}

TEST_CASE("stops S-380: a skipped call consumes its operands like retail: argc + 3 for an undefined receiver, argc then undefined for a non-function; the Pop variant discards it")
{
	// RotWK CallMethod: an undefined receiver pops argc + 3 values and pushes undefined (0x00B1C955); a non-function member pops the argc arguments
	// and pushes undefined (0x00B1AA5F, the value at 0x00DFDDD0); CallNamedMethodPop discards the result (0x00B1D406)
	auto twoArgs = [](Asm &a) { a.pushString("a1").pushString("a2").pushByte(2); };
	{
		CallFx fx; // undefined receiver, CallMethod: "keep" "r" | undefined -> r = undefined, "keep" left
		CHECK(fx.run([&](Asm &a) {
			a.pushString("keep").pushString("r");
			twoArgs(a);
			a.getStringVar("nothing").pushString("m").op(APT_OP_CALLMETHOD).op(APT_OP_SETVARIABLE);
		}));
		CHECK(fx.var("r").isUndefined());
		CHECK(fx.vm.lastStackDepth() == 1);
		CHECK(fx.host.calls.size() == 1);
	}
	{
		CallFx fx; // non-function member of a defined object
		CHECK(fx.run([&](Asm &a) {
			a.pushString("keep").pushString("r");
			twoArgs(a);
			a.getStringVar("o").pushString("obj").op(APT_OP_CALLMETHOD).op(APT_OP_SETVARIABLE);
		}));
		CHECK(fx.var("r").isUndefined());
		CHECK(fx.vm.lastStackDepth() == 1);
		CHECK(fx.host.calls.size() == 1);
	}
	{
		CallFx fx; // the Pop variants leave nothing
		CHECK(fx.run([&](Asm &a) {
			a.pushString("keep");
			twoArgs(a);
			a.getStringVar("nothing").pushString("m").op(APT_OP_EA_CALLMETHODPOP);
			twoArgs(a);
			a.getStringVar("o").pushString("obj").op(APT_OP_EA_CALLMETHODPOP);
			twoArgs(a);
			a.pushString("notFn").op(APT_OP_EA_CALLFUNCPOP);
		}));
		CHECK(fx.vm.lastStackDepth() == 1);
		CHECK(fx.host.calls.size() == 3);
		CHECK(fx.host.errors.empty());
	}
}

TEST_CASE("VM: faults and skipped operations are reported with the opcode and file offset")
{
	{
		Fx fx;
		CHECK_FALSE(fx.run([&](TestMovie &, Asm &a) { a.op(APT_OP_ADD2); })); // nothing on the stack
		REQUIRE(fx.vm.errors().size() == 1);
		CHECK(fx.vm.errors()[0].find("value stack underflow") != std::string::npos);
		CHECK(fx.vm.errors()[0].find("Add2") != std::string::npos);
		CHECK(fx.vm.errors()[0].find("file offset") != std::string::npos);
		CHECK(fx.host.errors.size() == 1); // the host sees it too
	}
	{
		Fx fx;
		CHECK(fx.run([&](TestMovie &, Asm &a) { a.pushByte(0).pushString("missingFunction").op(APT_OP_CALLFUNCTION); })); // survivable
		REQUIRE(fx.vm.errors().size() == 1);
		CHECK(fx.vm.errors()[0].find("call of undefined function 'missingFunction'") != std::string::npos);
	}
	{
		Fx fx;
		CHECK(fx.run([&](TestMovie &, Asm &a) {
			setVar(a, "o", [&] { a.pushByte(0).op(APT_OP_INITOBJECT); });
			a.pushByte(0).getStringVar("o").pushString("nope").op(APT_OP_CALLMETHOD).op(APT_OP_POP);
		}));
		REQUIRE(fx.vm.errors().size() == 1);
		CHECK(fx.vm.errors()[0].find("method 'nope' is not a function") != std::string::npos);
	}
	{
		Fx fx;
		CHECK(fx.run([&](TestMovie &, Asm &a) { a.op(APT_OP_PLAY); })); // a plain object has no timeline
		REQUIRE(fx.vm.errors().size() == 1);
		CHECK(fx.vm.errors()[0].find("no timeline for Play") != std::string::npos);
	}
	{
		Fx fx;
		CHECK_FALSE(fx.run([&](TestMovie &, Asm &a) { a.pushByte(1).setRegister(300); })); // outside the register file
		CHECK(fx.vm.errors()[0].find("register 300") != std::string::npos);
	}
	{
		Fx fx;
		CHECK(fx.run([&](TestMovie &, Asm &a) { a.pushConstByte(5); })); // no constant pool active
		CHECK(fx.vm.errors()[0].find("constant pool index 5") != std::string::npos);
	}
	{
		Fx fx;
		fx.vm.setInstructionBudget(1000);
		CHECK_FALSE(fx.run([&](TestMovie &, Asm &a) { a.label("spin").branchAlways("spin"); }));
		CHECK(fx.vm.errors()[0].find("instruction budget") != std::string::npos);
	}
	{
		Fx fx;
		CHECK(fx.run([&](TestMovie &m, Asm &a) {
			std::uint32_t lookup = m.constTyped(APT_CONST_LOOKUP, 3);
			a.pushDataIdx({ lookup });
		}));
		CHECK(fx.vm.errors()[0].find("no verified runtime meaning") != std::string::npos);
	}
	{
		// runaway recursion is cut off
		Fx fx;
		CHECK(fx.run([&](TestMovie &, Asm &a) {
			a.defineFunction("loop", {}, [&](Asm &f) { f.pushByte(0).pushString("loop").op(APT_OP_CALLFUNCTION).op(APT_OP_RETURN); });
			a.pushByte(0).pushString("loop").op(APT_OP_CALLFUNCTION).op(APT_OP_POP);
		}));
		bool overflow = false;
		for (const std::string &e : fx.vm.errors())
		{
			overflow = overflow || e.find("call stack overflow") != std::string::npos;
		}
		CHECK(overflow);
	}
}

TEST_CASE("VM: the instruction budget applies to each outermost call, not cumulatively")
{
	Fx fx;
	REQUIRE(fx.run([&](TestMovie &, Asm &a) {
		// function spin(): counts to 100
		a.defineFunction("spin", {}, [&](Asm &f) {
			f.pushString("i").pushByte(0).op(APT_OP_SETVARIABLE);
			f.label("top");
			f.pushString("i").getStringVar("i").op(APT_OP_INCREMENT).op(APT_OP_SETVARIABLE);
			f.getStringVar("i").pushByte(100).op(APT_OP_LESS2).branchIfTrue("top");
		});
	}));
	fx.vm.setInstructionBudget(5000);
	for (int i = 0; i < 20; ++i)
	{
		fx.vm.callFunction(fx.var("spin"), AptValue(), {}); // ~700 instructions each, 14,000 in total
	}
	CHECK(fx.errorsText() == "");
}

TEST_CASE("VM: the interpreter's roots keep globals alive across a collection")
{
	Fx fx;
	REQUIRE(fx.run([&](TestMovie &, Asm &a) {
		a.pushString("kept").pushByte(0).op(APT_OP_INITOBJECT).op(APT_OP_SETVARIABLE);
		a.op(APT_OP_EA_PUSHGLOBALVAR).pushString("g").pushByte(0).op(APT_OP_INITOBJECT).op(APT_OP_SETMEMBER);
	}));
	AptObject *kept = fx.var("kept").asObject();
	REQUIRE(kept);
	AptObject *orphan = fx.vm.newObject();
	(void)orphan;
	std::size_t before = fx.gc.objectCount();
	std::size_t freed = fx.gc.collect([&](AptGC &g) {
		fx.vm.markRoots(g);
		g.mark(fx.root);
	});
	CHECK(freed == 1);
	CHECK(fx.gc.objectCount() == before - 1);
	CHECK(fx.var("kept").asObject() == kept);
	AptValue g;
	CHECK(fx.vm.global()->getMember("g", g));
}

// ---- retail bytecode ---------------------------------------------------------------------------

namespace
{
std::shared_ptr<const AptFile> loadRetailMovie(AptRetail &mount, const char *name)
{
	static std::unique_ptr<AptArchiveFileSource> source;
	static std::unique_ptr<AptLoader> loader;
	if (!loader)
	{
		source = std::make_unique<AptArchiveFileSource>(mount.fs);
		loader = std::make_unique<AptLoader>(*source);
	}
	std::string err;
	std::shared_ptr<const AptFile> f = loader->loadMovie(name, &err);
	REQUIRE_MESSAGE(f, err);
	return f;
}
} // namespace

TEST_CASE("retail MainMenu root frame 0 defines GameCode, GetExtern, SetExtern and PlaySound and sets CodePrefix")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	std::shared_ptr<const AptFile> mm = loadRetailMovie(mount, "MainMenu");
	Fx fx;
	// The startup path reads these three externs; declaring them as providers that answer "no value" keeps the
	// authoring path (extern.InGame not truthy) the expectations below were read from the disassembly for.
	fx.host.setExternUndefined("InGame");
	fx.host.setExternUndefined("InBetaDemo");
	fx.host.setExternUndefined("InDreamMachineDemo");
	std::string err;
	REQUIRE(!mm->frames.empty());
	int ran = 0;
	for (const AptFrameItem &it : mm->frames[0].items)
	{
		if (it.type != APT_ITEM_ACTION)
		{
			continue;
		}
		std::shared_ptr<const AptCodeBlock> block = mm->codeAt(it.codeOffset, &err);
		REQUIRE_MESSAGE(block, err);
		CHECK(fx.vm.execute(*block, fx.root, fx.root));
		++ran;
	}
	CHECK(ran == 2); // the disassembly lists two root frame-0 programs (offsets 67100 and 71696)
	// Frame 0 runs the retail startup path: InitialSetup() calls _root.InitFunc(), which sends
	// GameCode('OnInitialized'); Startup() then fades the background out.  Nothing else reaches the engine.
	REQUIRE(fx.host.fscommands.size() == 2);
	CHECK(fx.host.fscommands[0].command == "AptMainMenu::OnInitialized");
	CHECK(fx.host.fscommands[0].argument == "");
	CHECK(fx.host.fscommands[1].command == "SetBackground");
	CHECK(fx.host.fscommands[1].argument == "fadeout");
	CHECK(fx.host.traces == std::vector<std::string>{ "Call AptMainMenu::OnInitialized()", "Startup!", "AptMainMenu:RevealAllButtons" });
	// The only script errors are the clips (QuitMainMenu, SoloPlayNav, ...) and the setInterval native that step A2
	// provides; nothing else is missing and nothing faulted.
	CHECK(fx.vm.errors().size() == 13);
	for (const std::string &e : fx.vm.errors())
	{
		bool known = e.find("is not a function on a value of type") != std::string::npos || e.find("call of undefined function 'setInterval'") != std::string::npos;
		CHECK_MESSAGE(known, e);
	}
	fx.vm.clearErrors();
	fx.host.fscommands.clear();
	for (const char *name : { "GameCode", "GameCodeNoPrefix", "GetExtern", "SetExtern", "PlaySound", "EnableComponents", "DisableComponents" })
	{
		AptValue fn = fx.var(name);
		CHECK_MESSAGE(fn.isObject(), name);
		if (fn.isObject())
		{
			CHECK(fn.asObject()->kind() == AptObjectKind::Function);
		}
	}
	CHECK(fx.var("CodePrefix").asString() == "AptMainMenu");
	CHECK(fx.var("colorAngmarCore").asString() == "80B4FF");
	CHECK(fx.var("colorFrameGlobal").asString() == "0x74808E"); // "0x" + colorAngmarLines through StringConcat/Add2

	// GameCode(func, params) -> FSCommand:AptMainMenu::<func>
	fx.vm.callFunction(fx.var("GameCode"), AptValue::object(fx.root), { AptValue::string("Skirmish"), AptValue::integer(1) });
	REQUIRE(fx.host.fscommands.size() == 1);
	CHECK(fx.host.fscommands[0].command == "AptMainMenu::Skirmish");
	CHECK(fx.host.fscommands[0].argument == "1");
	fx.vm.callFunction(fx.var("GameCodeNoPrefix"), AptValue::object(fx.root), { AptValue::string("Bare"), AptValue::string("x") });
	REQUIRE(fx.host.fscommands.size() == 2);
	CHECK(fx.host.fscommands[1].command == "Bare");
	CHECK(fx.host.fscommands[1].argument == "x");

	// InitialSetup() marked the movie initialised during frame 0, so PlaySound reaches the engine; with
	// _root.Initialized cleared it returns without a command.
	CHECK(fx.var("Initialized").toBoolean(6));
	fx.vm.callFunction(fx.var("PlaySound"), AptValue::object(fx.root), { AptValue::string("Gui_Click") });
	REQUIRE(fx.host.fscommands.size() == 3);
	CHECK(fx.host.fscommands[2].command == "PlaySound");
	CHECK(fx.host.fscommands[2].argument == "Gui_Click");
	fx.root->setMember("Initialized", AptValue::boolean(false));
	fx.vm.callFunction(fx.var("PlaySound"), AptValue::object(fx.root), { AptValue::string("Gui_Click") });
	CHECK(fx.host.fscommands.size() == 3);

	// GetExtern/SetExtern: without _global.InGame they use _root; with it they go through extern
	fx.root->setMember("SomeValue", AptValue::string("from root"));
	CHECK(fx.vm.callFunction(fx.var("GetExtern"), AptValue::object(fx.root), { AptValue::string("SomeValue") }).toString() == "from root");
	fx.vm.global()->setMember("InGame", AptValue::boolean(true));
	fx.host.setExternValue("AptX::Level", "7");
	fx.host.setExternUndefined("AptX::Name");
	CHECK(fx.vm.callFunction(fx.var("GetExtern"), AptValue::object(fx.root), { AptValue::string("AptX::Level") }).toString() == "7");
	// SetExtern(name, value): callers push the value, then the name, then the count (campaignmenu f0 @9504:
	// "push-true; push 'CampaignMenu:Victorious'; push-byte 2; call-named-method-pop SetExtern").  The spec's
	// "SetExtern(value, name)" has the order reversed.
	fx.vm.callFunction(fx.var("SetExtern"), AptValue::object(fx.root), { AptValue::string("AptX::Name"), AptValue::string("new") });
	REQUIRE(fx.host.externSets.size() == 1);
	CHECK(fx.host.externSets[0].command == "AptX::Name");
	CHECK(fx.host.externSets[0].argument == "new");
	CHECK(fx.errorsText() == "");
}

namespace
{
// Every script function reachable from `start`: own members, array items, the `prototype` field, the
// __proto__ chain and the members of functions themselves.  Each function is paired with the object it was
// found on (used as `this`).
void collectFunctions(AptObject *start, std::set<AptObject *> &seen, std::vector<std::pair<AptFunction *, AptObject *>> &out)
{
	std::vector<std::pair<AptObject *, AptObject *>> work{ { start, nullptr } };
	while (!work.empty())
	{
		AptObject *o = work.back().first;
		AptObject *holder = work.back().second;
		work.pop_back();
		if (!o || !seen.insert(o).second)
		{
			continue;
		}
		if (o->kind() == AptObjectKind::Function && !static_cast<AptFunction *>(o)->isNative())
		{
			out.push_back({ static_cast<AptFunction *>(o), holder });
		}
		auto visit = [&](const AptValue &v) {
			if (v.isObject() && v.asObject())
			{
				work.push_back({ v.asObject(), o });
			}
		};
		// every stored value: hash entries, the `prototype` field and the native table (_global.Array.extra,
		// _global.Math.extra ...), with the object it was found on as the holder
		o->props.forEachValue(visit);
		if (o->proto())
		{
			work.push_back({ o->proto(), o });
		}
		if (o->kind() == AptObjectKind::Array)
		{
			for (const AptValue &item : static_cast<const AptArray *>(o)->items)
			{
				visit(item);
			}
		}
	}
}

// The survivable errors the stub environment is KNOWN to cause, with the step that removes each kind.
//   A2 = spec menus-apt.md step A2 "Display list, timeline and natives" (movie clips and their members, Color,
//        setInterval/attachMovie, the clip tree scripts call into).
//   host = the WindowManager extern provider map (spec 3.3), which the stub host does not register.
// A kind alone is not accepted: each occurrence is identified (the missing class, function, method or extern
// name) and compared with the reviewed baseline engine/tests/data/apt/corpus_error_baseline.tsv, so a new
// identity or a changed count fails the gate (docs/STOPS.md S-010).
struct ErrorKind
{
	const char *key;
	const char *removedBy;
};

const ErrorKind kKinds[] = {
	{ "native-class", "A2: Color and MovieClip natives" },
	{ "undefined-function", "A2: clip tree and timeline natives (setInterval, attachMovie, sibling clips' functions)" },
	{ "method-not-function", "A2: clip tree and MovieClip methods" },
	{ "non-function-undefined", "A2: clip tree and MovieClip members" },
	{ "extern-read", "host: WindowManager extern providers" },
	{ "extern-write", "host: WindowManager extern providers" },
	{ "for-in-undefined", "A2: clip tree and MovieClip members" },
};

// False for an error that belongs to no kind.  `identity` names the missing thing.
bool classifyError(const std::string &e, std::string &kind, std::string &identity)
{
	auto starts = [&](const char *prefix) { return e.rfind(prefix, 0) == 0; };
	auto between = [&](const char *open, const char *close) {
		std::size_t from = std::string(open).size();
		std::size_t to = e.find(close, from);
		return to == std::string::npos ? std::string() : e.substr(from, to - from);
	};
	if (starts("NewObject: class '") && e.size() > 30 && e.compare(e.size() - 16, 16, "' is not defined") == 0)
	{
		kind = "native-class";
		identity = between("NewObject: class '", "' is not defined");
		return true;
	}
	if (starts("call of undefined function '"))
	{
		kind = "undefined-function";
		identity = between("call of undefined function '", "'");
		return true;
	}
	const char *methodTail = "' is not a function on a value of type ";
	if (starts("method '") && e.find(methodTail) != std::string::npos)
	{
		kind = "method-not-function";
		identity = between("method '", methodTail) + " on " + e.substr(e.find(methodTail) + std::string(methodTail).size());
		return true;
	}
	if (starts("call of a non-function value (undefined)"))
	{
		kind = "non-function-undefined";
		identity = "undefined";
		return true;
	}
	if (starts("for-in over a non-object value (undefined) is an acceptance stop"))
	{
		kind = "for-in-undefined"; // the operand is a clip member the stub timeline does not have
		identity = "undefined";
		return true;
	}
	if (starts("extern."))
	{
		std::size_t r = e.find(" read: no extern provider is registered under that name");
		std::size_t w = e.find(" write: no extern provider is registered under that name");
		if (r != std::string::npos)
		{
			kind = "extern-read";
			identity = e.substr(7, r - 7);
			return true;
		}
		if (w != std::string::npos)
		{
			kind = "extern-write";
			identity = e.substr(7, w - 7);
			return true;
		}
	}
	return false;
}

std::string baselinePath()
{
	return std::string(OPENBFME_APT_TEST_DATA_DIR) + "/corpus_error_baseline.tsv";
}

// "kind<TAB>identity<TAB>count" lines; '#' lines are comments.
bool loadBaseline(std::map<std::string, std::size_t> &out, std::string &error)
{
	std::ifstream in(baselinePath(), std::ios::binary);
	if (!in)
	{
		error = "cannot open " + baselinePath();
		return false;
	}
	std::string line;
	while (std::getline(in, line))
	{
		if (!line.empty() && line.back() == '\r')
		{
			line.pop_back();
		}
		if (line.empty() || line[0] == '#')
		{
			continue;
		}
		std::size_t last = line.rfind('\t');
		if (last == std::string::npos || line.find('\t') == last)
		{
			error = "malformed baseline line: " + line;
			return false;
		}
		out[line.substr(0, last)] = (std::size_t)std::stoull(line.substr(last + 1));
	}
	return true;
}
} // namespace

// What this gate establishes, and no more: every one of the 4,767 retail programs runs on a stub scope, and
// every script function reachable afterwards from the stub timeline, _root and _global object graphs is called
// once with NO arguments, without a VM fault (stack underflow/overflow, runaway budget, bad operand, call stack
// overflow: AptActionInterpreter::faultCount) and with a balanced value stack.  It does not claim correct
// behaviour.  Survivable script errors are compared occurrence by occurrence with a reviewed baseline (a kind,
// the identity of the missing thing, a count); a new identity or a changed count fails the gate.  Functions
// reachable only through closures or arguments are not called.
TEST_CASE("retail corpus: every program runs, and every reachable function is called with no arguments, without a VM fault")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	static std::unique_ptr<AptArchiveFileSource> source;
	source = std::make_unique<AptArchiveFileSource>(mount.fs);
	AptLoader loader(*source);
	std::size_t programs = 0;
	std::size_t programFaults = 0;
	std::size_t unbalanced = 0;
	std::size_t functionsCalled = 0;
	std::size_t functionFaults = 0;
	std::size_t functionResidue = 0;
	std::size_t survivableErrors = 0;
	std::string firstFault;
	std::string firstUnbalanced;
	std::string firstFunctionFault;
	std::string firstFunctionResidue;
	std::size_t unclassified = 0;
	std::string firstUnclassified;
	std::map<std::string, std::size_t> occurrences; // "kind<TAB>identity" -> count
	for (const std::string &name : source->listMovies())
	{
		std::string err;
		std::shared_ptr<const AptFile> f = loader.loadMovie(name, &err);
		REQUIRE_MESSAGE(f, err);
		for (std::uint32_t off : f->programOffsets())
		{
			std::shared_ptr<const AptCodeBlock> block = f->codeAt(off, &err);
			REQUIRE_MESSAGE(block, err);
			// Fresh state per program: a stub timeline that accepts every timeline op, a tiny instruction budget
			Fx fx;
			fx.vm.setInstructionBudget(200000);
			TestClip *clip = fx.gc.create<TestClip>();
			AptObject *root = fx.vm.newObject();
			++programs;
			std::uint64_t faultsBefore = fx.vm.faultCount();
			if (!fx.vm.execute(*block, clip, root) || fx.vm.faultCount() != faultsBefore)
			{
				++programFaults;
				if (firstFault.empty())
				{
					firstFault = name + " @" + std::to_string(off) + ": " + (fx.vm.errors().empty() ? std::string("?") : fx.vm.errors().back());
				}
			}
			else if (fx.vm.lastStackDepth() != 0)
			{
				++unbalanced;
				if (firstUnbalanced.empty())
				{
					firstUnbalanced = name + " @" + std::to_string(off) + " left " + std::to_string(fx.vm.lastStackDepth()) + " values";
				}
			}
			// Call every script function reachable from the timeline, _root and _global.
			std::set<AptObject *> seen;
			std::vector<std::pair<AptFunction *, AptObject *>> reachable;
			collectFunctions(clip, seen, reachable);
			collectFunctions(root, seen, reachable);
			collectFunctions(fx.vm.global(), seen, reachable);
			for (const auto &entry : reachable)
			{
				++functionsCalled;
				std::uint64_t before = fx.vm.faultCount();
				fx.vm.callFunction(AptValue::object(entry.first), AptValue::object(entry.second), {});
				if (fx.vm.faultCount() != before)
				{
					++functionFaults;
					if (firstFunctionFault.empty())
					{
						firstFunctionFault = name + " @" + std::to_string(off) + ": " + fx.vm.errors().back();
					}
				}
				else if (fx.vm.lastStackDepth() != 0)
				{
					++functionResidue;
					if (firstFunctionResidue.empty())
					{
						firstFunctionResidue = name + " @" + std::to_string(off) + " left " + std::to_string(fx.vm.lastStackDepth());
					}
				}
			}
			for (const std::string &e : fx.vm.errors())
			{
				++survivableErrors;
				std::string kind, identity;
				if (classifyError(e, kind, identity))
				{
					++occurrences[kind + "\t" + identity];
				}
				else
				{
					++unclassified;
					if (firstUnclassified.empty())
					{
						firstUnclassified = name + " @" + std::to_string(off) + ": " + e;
					}
				}
			}
		}
	}
	CHECK(programs == 4767);
	CHECK_MESSAGE(programFaults == 0, programFaults << " programs faulted, first: " << firstFault);
	// Compiled scripts end with an empty value stack; a residue would mean an opcode's stack effect is wrong
	// (this is what validates the fused call forms 0x5E/0xB1/0xB3 that no EA source describes).
	CHECK_MESSAGE(unbalanced == 0, unbalanced << " programs ended with values on the stack, first: " << firstUnbalanced);
	CHECK(functionsCalled > 1000);
	CHECK_MESSAGE(functionFaults == 0, functionFaults << " function calls faulted, first: " << firstFunctionFault);
	CHECK_MESSAGE(functionResidue == 0, functionResidue << " function calls ended with values on their stack, first: " << firstFunctionResidue);
	// every error belongs to a known kind
	CHECK_MESSAGE(unclassified == 0, unclassified << " errors outside every known kind, first: " << firstUnclassified);
	// Regenerate the baseline (then review the diff) with OPENBFME_APT_BASELINE_WRITE=<file>.
	if (const char *writeTo = std::getenv("OPENBFME_APT_BASELINE_WRITE"))
	{
		std::ofstream out(writeTo, std::ios::binary);
		out << "# Reviewed baseline of the survivable script errors the stub scope causes while the 4,767 retail programs run\n"
			   "# and every reachable function is called with no arguments.  kind<TAB>identity<TAB>count.\n"
			   "# Every kind is removed by menus-apt.md step A2 or the host extern providers (docs/STOPS.md S-010).\n";
		for (const auto &o : occurrences)
		{
			out << o.first << "\t" << o.second << "\n";
		}
		MESSAGE("baseline written to " << std::string(writeTo));
		return;
	}
	std::map<std::string, std::size_t> baseline;
	std::string baselineError;
	REQUIRE_MESSAGE(loadBaseline(baseline, baselineError), baselineError);
	std::string differences;
	std::size_t differenceCount = 0;
	for (const auto &o : occurrences)
	{
		auto b = baseline.find(o.first);
		if (b == baseline.end() || b->second != o.second)
		{
			++differenceCount;
			if (differenceCount <= 15)
			{
				differences += "\n  " + o.first + ": " + std::to_string(o.second) + (b == baseline.end() ? " (new identity)" : " (baseline " + std::to_string(b->second) + ")");
			}
		}
	}
	for (const auto &b : baseline)
	{
		if (!occurrences.count(b.first))
		{
			++differenceCount;
			if (differenceCount <= 15)
			{
				differences += "\n  " + b.first + ": gone (baseline " + std::to_string(b.second) + ")";
			}
		}
	}
	CHECK_MESSAGE(differenceCount == 0, differenceCount << " occurrence differences from the reviewed baseline:" << differences);
	// The missing native classes are checked against the raw bytecode: an independent census of the 4,767
	// programs finds 231 `new Color` and 2 `new MovieClip` construction sites (Sol review r2), each of which the
	// stub reports once.
	CHECK(occurrences["native-class\tColor"] == 231);
	CHECK(occurrences["native-class\tMovieClip"] == 2);
	std::map<std::string, std::size_t> perKind;
	for (const auto &o : occurrences)
	{
		perKind[o.first.substr(0, o.first.find('\t'))] += o.second;
	}
	std::string table;
	for (const ErrorKind &k : kKinds)
	{
		std::size_t identities = 0;
		for (const auto &o : occurrences)
		{
			identities += o.first.compare(0, std::string(k.key).size() + 1, std::string(k.key) + "\t") == 0 ? 1 : 0;
		}
		table += "\n  " + std::to_string(perKind[k.key]) + " x " + k.key + " (" + std::to_string(identities) + " identities)  [removed by " + k.removedBy + "]";
	}
	MESSAGE(programs << " programs, " << functionsCalled << " functions called, " << survivableErrors << " survivable errors (" << unclassified << " unclassified):" << table);
}
