// OpenBFME unit tests: the ScriptEvents.xml reader and the event model (lane LUA-1; spec lua-scripting.md 1.2, 1.3, 1.7, 4.1, 4.4).
// Expected values: the disassembly of the retail reader (RW 0x949147 / 0x9491D1 / 0x948E05 / 0x9490D2 / 0x94907C / 0x94901C and the parse
// functions 0x739ABD, 0x738AED, 0x7397A3, 0x73459A, 0x7384DD, 0x738556), traced by hand for each synthetic case below; for the retail file,
// the counts of the spec (made by independent scripts over the archive text) and a second count by a regular expression over the same text.

#include "doctest.h"

#include "LuaTestUtil.h"
#include "RetailTestMount.h"

#include "Common/MD5.h"
#include "Common/ModelState.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"
#include "GameLogic/ScriptEngine/LuaScriptEvents.h"
#include "GameLogic/ScriptEngine/LuaXml.h"

#include <regex>

using luatest::run;

namespace
{
std::string tokens(const std::string &xml)
{
	EaXmlLexer x(xml);
	std::string out;
	for (int i = 0; i < 40; ++i)
	{
		const int t = x.next();
		out += std::to_string(t);
		if (t == EaXmlLexer::START || t == EaXmlLexer::FINISH || t == EaXmlLexer::TEXT)
		{
			out += "(" + x.tail() + ")";
		}
		out += " ";
		if (t == EaXmlLexer::END || t == EaXmlLexer::ERROR_TOKEN)
		{
			break;
		}
	}
	return out;
}

const char *kDecl = "<?xml version=\"1.0\"?>";

struct Reg
{
	NameKeyGenerator keys;
	LuaReportSink sink;
	std::unique_ptr<LuaEventRegistry> reg;
	std::map<std::string, int> globals; // lua_type of a global by name for the handler check (5 = function)
	Reg()
	{
		keys.init();
		reg.reset(new LuaEventRegistry(keys));
	}
	void parse(const std::string &xml, bool keepOpen = false)
	{
		reg->parse(xml, keepOpen, [this](const std::string &n) { auto it = globals.find(n); return it == globals.end() ? 1 : it->second; }, sink);
	}
	std::vector<std::string> handlerKeys(const std::string &list)
	{
		std::vector<std::string> out;
		const LuaEventList *l = reg->findEventList(list);
		if (l)
		{
			for (const LuaEventHandler &h : l->handlers)
			{
				out.push_back(keys.keyToName(h.key) + "=" + h.function);
			}
		}
		return out;
	}
};
std::string join(const std::vector<std::string> &v)
{
	std::string s;
	for (const std::string &x : v)
	{
		s += (s.empty() ? "" : ",") + x;
	}
	return s;
}
} // namespace

// ---------------------------------------------------------------------------------------------------------------------
// the lexer
// ---------------------------------------------------------------------------------------------------------------------
TEST_CASE("xml lexer: tokens: start 1, end 2, text 3, end of text 0; a self closing tag is a start then an end")
{
	CHECK(tokens("<A x=\"1\" y='2'><B/>text</A>") == "1(A) 1(B) 2(B) 3(text) 2(A) 0 ");
	CHECK(tokens("  <A >  </A  >  ") == "1(A) 2(A) 0 ");
	CHECK(tokens("") == "0 ");
	CHECK(tokens("   \n\t ") == "0 ");
	// attributes are available after the start token
	EaXmlLexer x("<A p=\"1\" q='two words'/>");
	REQUIRE(x.next() == EaXmlLexer::START);
	REQUIRE(x.attributeCount() == 2);
	CHECK(x.attributeName(0) == "p");
	CHECK(x.attributeValue(0) == "1");
	CHECK(x.attributeName(1) == "q");
	CHECK(x.attributeValue(1) == "two words");
}

TEST_CASE("xml lexer: the declaration must be exactly <?xml version=\"1.0\"?> (RW 0x9491A1); another one faults retail and is reported by crashesRetail")
{
	CHECK(tokens(std::string(kDecl) + "\n<A/>") == "1(A) 2(A) 0 ");
	EaXmlLexer bad("<?xml version=\"1.0\" encoding=\"utf-8\"?><A/>");
	CHECK(bad.crashesRetail());
	CHECK(bad.next() == EaXmlLexer::ERROR_TOKEN);
	EaXmlLexer none("<A/>");
	CHECK(!none.crashesRetail());
	// a processing instruction elsewhere is not a declaration: "<?" is a tag start whose name is empty, which retail rejects
	CHECK(tokens("<A/><?pi?>") == "1(A) 2(A) -1 ");
}

TEST_CASE("xml lexer: tag and attribute names are runs of letters, digits and '_' only; anything else is an error and the lexer stays failed")
{
	CHECK(tokens("<a_b1 c_2=\"x\"/>") == "1(a_b1) 2(a_b1) 0 ");
	CHECK(tokens("<a-b/>") == "-1 ");
	CHECK(tokens("<a b:c=\"1\"/>") == "-1 ");
	CHECK(tokens("<a b=1/>") == "-1 ");        // an unquoted value
	CHECK(tokens("<a b \"1\"/>") == "-1 ");    // no '='
	CHECK(tokens("<a b=\"1/>") == "-1 ");      // an unterminated value
	CHECK(tokens("<a") == "-1 ");
	CHECK(tokens("<a b=\"1\"") == "-1 ");
	CHECK(tokens("<") == "-1 ");
	EaXmlLexer x("<a-b/><c/>");
	CHECK(x.next() == -1);
	CHECK(x.next() == -1); // after an error every call is -1 (the cursor is NULL)
	CHECK(x.next() == -1);
}

TEST_CASE("xml lexer: only the first 4 attributes are kept, names are cut at 32 and values at 63 characters, values are not entity decoded")
{
	EaXmlLexer x("<A a=\"1\" b=\"2\" c=\"3\" d=\"4\" e=\"5\"/>");
	REQUIRE(x.next() == EaXmlLexer::START);
	CHECK(x.attributeCount() == 4);
	CHECK(x.attributeName(3) == "d");
	const std::string name40(40, 'n'), value70(70, 'v');
	EaXmlLexer y("<A " + name40 + "=\"" + value70 + "\"/>");
	REQUIRE(y.next() == EaXmlLexer::START);
	CHECK(y.attributeName(0) == std::string(32, 'n'));
	CHECK(y.attributeValue(0) == std::string(63, 'v'));
	EaXmlLexer z("<A v=\"&amp;\"/>");
	REQUIRE(z.next() == EaXmlLexer::START);
	CHECK(z.attributeValue(0) == "&amp;");
}

TEST_CASE("xml lexer: comments: <!-- ... --> is skipped, \"--->\" does not close one (RW 0x949099 restarts after the third dash), other <! is an error")
{
	CHECK(tokens("<!-- hi --><A/>") == "1(A) 2(A) 0 ");
	CHECK(tokens("<A/><!-- one --><!-- two -->  <B/>") == "1(A) 2(A) 1(B) 2(B) 0 ");
	CHECK(tokens("<!-- a - b -- c --><A/>") == "1(A) 2(A) 0 ");
	CHECK(tokens("<!-- a ---><A/>") == "-1 "); // retail never finds the end
	CHECK(tokens("<!-- a ---> --><A/>") == "1(A) 2(A) 0 ");
	CHECK(tokens("<!DOCTYPE x><A/>") == "-1 ");
	CHECK(tokens("<!- x --><A/>") == "-1 ");
	CHECK(tokens("<!-- never closed <A/>") == "-1 ");
}

TEST_CASE("xml lexer: text: leading white space is skipped, the rest kept; the five entities decode; an unknown one, a missing ';' or an end of input is -1")
{
	CHECK(tokens("<A>  one  two \n</A>") == "1(A) 3(one  two \n) 2(A) 0 ");
	CHECK(tokens("<A>a &amp; b &lt;c&gt; &quot;d&quot; &apos;e&apos;</A>") == "1(A) 3(a & b <c> \"d\" 'e') 2(A) 0 ");
	CHECK(tokens("<A>&bogus;</A>") == "1(A) -1 ");
	CHECK(tokens("<A>&amp</A>") == "1(A) -1 ");
	CHECK(tokens("<A>text") == "1(A) -1 ");   // text that reaches the end of the input without a '<'
	CHECK(tokens("<A/>text") == "1(A) 2(A) -1 ");
	// the buffer holds 0x1000 characters: the rest is dropped without an error
	const std::string big(5000, 'x');
	EaXmlLexer x("<A>" + big + "</A>");
	REQUIRE(x.next() == EaXmlLexer::START);
	REQUIRE(x.next() == EaXmlLexer::TEXT);
	CHECK(x.tail().size() == 0x1000);
}

TEST_CASE("xml lexer: the self closing flag survives until the next tag: text that starts with '/' right after <a/> is misread (RW 0x9491D7)")
{
	CHECK(tokens("<a/>/x<b/>") == "1(a) 2(a) -1 ");
	CHECK(tokens("<a/> /x<b/>") == "1(a) 2(a) 3(/x) 1(b) 2(b) 0 "); // white space first: the cursor is not at '/'
}

// ---------------------------------------------------------------------------------------------------------------------
// the registry
// ---------------------------------------------------------------------------------------------------------------------
TEST_CASE("events: the 17 internal slots, their order and the cached OnDestroyed key (RW 0x73449A)")
{
	Reg r;
	const char *expected[17] = { "OnDamaged", "OnDestroyed", "OnArrived", "OnUnitEntered", "OnTeamEntered", "OnUnitExited", "OnTeamExited",
		"OnTeamDestroyed", "BeScary", "DamageIncoming", "OnAflame", "OnQuenched", "OnCreated", "OnBuildingComplete", "OnSlaughtered",
		"OnGenericEvent", "OnBuildVariation" };
	for (int i = 0; i < 17; ++i)
	{
		CHECK(r.keys.keyToName(r.reg->internalKey(i)) == expected[i]);
	}
	CHECK(r.reg->onDestroyedKey() == r.reg->internalKey(LUAEVENT_OnDestroyed));
	CHECK(LUAEVENT_OnUnitExited == 5);  // the engine's code order, not the name order
	CHECK(LUAEVENT_OnTeamEntered == 4);
	CHECK(LUAEVENT_OnCreated == 12);
}

TEST_CASE("events: Events and EventList parse: handlers, Inherit copies the earlier list first, an own handler replaces an inherited one of the same event")
{
	Reg r;
	r.globals = { { "A", 5 }, { "B", 5 }, { "C", 5 }, { "D", 5 } };
	r.parse(std::string(kDecl) +
		"<SageLuaScriptSection>"
		"<Events><InternalEvent Name=\"OnCreated\"/><ScriptedEvent Name=\"Chant\"/></Events>"
		"<EventList Name=\"Base\"><EventHandler EventName=\"OnCreated\" ScriptFunctionName=\"A\"/>"
		"<EventHandler EventName=\"OnDamaged\" ScriptFunctionName=\"B\"/></EventList>"
		"<EventList Name=\"Child\" Inherit=\"Base\"><EventHandler EventName=\"OnCreated\" ScriptFunctionName=\"C\"/>"
		"<EventHandler EventName=\"OnDestroyed\" ScriptFunctionName=\"D\"/></EventList>"
		"</SageLuaScriptSection>");
	CHECK(r.reg->eventListCount() == 2);
	CHECK(join(r.handlerKeys("Base")) == "OnCreated=A,OnDamaged=B");
	// the inherited OnCreated (A) is replaced in place by the child's (C); the slot keeps its position
	CHECK(join(r.handlerKeys("Child")) == "OnCreated=C,OnDamaged=B,OnDestroyed=D");
	CHECK(r.reg->internalEnabled(LUAEVENT_OnCreated));
	CHECK(!r.reg->internalEnabled(LUAEVENT_OnDamaged));
	CHECK(r.reg->scriptedEvents().size() == 1);
	CHECK(r.reg->findEvent("Chant").kind == LuaEventRef::SCRIPTED);
	CHECK(r.reg->findEvent("OnCreated").kind == LuaEventRef::INTERNAL);
	CHECK(r.reg->findEvent("OnCreated").index == 12);
	CHECK(!r.reg->findEvent("Nothing").valid());
	CHECK(r.reg->notes().empty());
}

TEST_CASE("events: a handler naming a missing function is accepted (Q1), an EventName that is not declared is accepted, and both are only noted")
{
	Reg r;
	r.parse("<SageLuaScriptSection><EventList Name=\"L\">"
		"<EventHandler EventName=\"OnCreated\" ScriptFunctionName=\"Missing\"/>"
		"<EventHandler EventName=\"NotAnEvent\" ScriptFunctionName=\"AlsoMissing\"/>"
		"</EventList></SageLuaScriptSection>");
	CHECK(join(r.handlerKeys("L")) == "OnCreated=Missing,NotAnEvent=AlsoMissing");
	REQUIRE(r.reg->notes().size() == 2);
	CHECK(r.reg->notes()[0].find("Missing is not defined.") != std::string::npos);
	r.globals["NotAFunction"] = 2;
	Reg q;
	q.globals["F"] = 2;
	q.parse("<SageLuaScriptSection><EventList Name=\"L\"><EventHandler EventName=\"OnCreated\" ScriptFunctionName=\"F\"/></EventList></SageLuaScriptSection>");
	REQUIRE(q.reg->notes().size() == 1);
	CHECK(q.reg->notes()[0].find("is not a lua function.") != std::string::npos);
}

TEST_CASE("events: an EventHandler without both names is skipped; DebugSingleStep is true only for the text \"true\"")
{
	Reg r;
	r.globals["F"] = 5;
	r.parse("<SageLuaScriptSection><EventList Name=\"L\">"
		"<EventHandler EventName=\"OnCreated\"/>"
		"<EventHandler ScriptFunctionName=\"F\"/>"
		"<EventHandler EventName=\"\" ScriptFunctionName=\"F\"/>"
		"<EventHandler EventName=\"OnDamaged\" ScriptFunctionName=\"F\" DebugSingleStep=\"true\"/>"
		"<EventHandler EventName=\"OnDestroyed\" ScriptFunctionName=\"F\" DebugSingleStep=\"True\"/>"
		"</EventList></SageLuaScriptSection>");
	const LuaEventList *l = r.reg->findEventList("L");
	REQUIRE(l);
	REQUIRE(l->handlers.size() == 2);
	CHECK(l->handlers[0].debugSingleStep);
	CHECK(!l->handlers[1].debugSingleStep);
}

TEST_CASE("events: Inherit resolves while parsing, in file order: a base defined later is silently not inherited (retail data bug EvilPorterFunctions)")
{
	Reg r;
	r.globals = { { "A", 5 }, { "B", 5 } };
	r.parse("<SageLuaScriptSection>"
		"<EventList Name=\"EvilPorter\" Inherit=\"Infantry\"><EventHandler EventName=\"OnCreated\" ScriptFunctionName=\"A\"/></EventList>"
		"<EventList Name=\"Infantry\"><EventHandler EventName=\"BeTerrified\" ScriptFunctionName=\"B\"/></EventList>"
		"</SageLuaScriptSection>");
	CHECK(join(r.handlerKeys("EvilPorter")) == "OnCreated=A");
	CHECK(join(r.handlerKeys("Infantry")) == "BeTerrified=B");
	REQUIRE(r.reg->notes().size() == 1);
	CHECK(r.reg->notes()[0].find("Inherit=\"Infantry\"") != std::string::npos);
}

TEST_CASE("events: control flow of the retail loops: an unknown element abandons what it is in and everything after it is skipped")
{
	{
		// unknown child of EventList: the list is NOT registered (RW 0x739A7B); the loop in ParseToken then reads the synthetic end of
		// <Bogus/> as status 2 and returns, and ParseTokenFile ignores every later start tag (the root name no longer matches)
		Reg r;
		r.parse("<SageLuaScriptSection>"
			"<EventList Name=\"One\"><EventHandler EventName=\"OnCreated\" ScriptFunctionName=\"F\"/><Bogus/></EventList>"
			"<EventList Name=\"Two\"><EventHandler EventName=\"OnCreated\" ScriptFunctionName=\"F\"/></EventList>"
			"</SageLuaScriptSection>");
		CHECK(r.reg->eventListCount() == 0);
	}
	{
		// unknown child of Events: the rest of the document is lost, including the following EventList
		Reg r;
		r.parse("<SageLuaScriptSection><Events><Bogus/><InternalEvent Name=\"OnCreated\"/></Events>"
			"<EventList Name=\"L\"><EventHandler EventName=\"OnCreated\" ScriptFunctionName=\"F\"/></EventList></SageLuaScriptSection>");
		CHECK(r.reg->eventListCount() == 0);
		CHECK(!r.reg->internalEnabled(LUAEVENT_OnCreated));
	}
	{
		// an unknown top level element: the same
		Reg r;
		r.parse("<SageLuaScriptSection><Other/><EventList Name=\"L\"/></SageLuaScriptSection>");
		CHECK(r.reg->eventListCount() == 0);
	}
	{
		// a document whose root has another name reads nothing
		Reg r;
		r.parse("<Root><EventList Name=\"L\"/></Root>");
		CHECK(r.reg->eventListCount() == 0);
	}
	{
		// an EventHandler that is not self closing: its end tag is consumed by the first finish(), the second reads the next sibling
		Reg r;
		r.parse("<SageLuaScriptSection><EventList Name=\"L\">"
			"<EventHandler EventName=\"OnCreated\" ScriptFunctionName=\"F\"></EventHandler>"
			"<EventHandler EventName=\"OnDamaged\" ScriptFunctionName=\"G\"></EventHandler>"
			"</EventList></SageLuaScriptSection>");
		CHECK(join(r.handlerKeys("L")) == "OnCreated=F,OnDamaged=G");
	}
	{
		// an EventHandler with text content: the first finish() reads the text (3), the second the end tag (2): the list is registered at that
		// point, with the handlers so far; the later siblings are never read
		Reg r;
		r.parse("<SageLuaScriptSection><EventList Name=\"L\">"
			"<EventHandler EventName=\"OnCreated\" ScriptFunctionName=\"F\">words</EventHandler>"
			"<EventHandler EventName=\"OnDamaged\" ScriptFunctionName=\"G\"/>"
			"</EventList></SageLuaScriptSection>");
		CHECK(join(r.handlerKeys("L")) == "OnCreated=F");
	}
	{
		// an EventList closed by the end of the text is registered
		Reg r;
		r.parse("<SageLuaScriptSection><EventList Name=\"L\"><EventHandler EventName=\"OnCreated\" ScriptFunctionName=\"F\"/>");
		CHECK(r.reg->eventListCount() == 1);
	}
	{
		// a malformed document stops the parse where the lexer fails; what came before stays
		Reg r;
		r.parse("<SageLuaScriptSection><EventList Name=\"A\"/><EventList Name=\"B\"><EventHandler EventName=\"OnCreated\" ScriptFunctionName=\"F\" !/></EventList></SageLuaScriptSection>");
		CHECK(r.reg->eventListCount() == 1);
		CHECK(r.reg->findEventList("A") != nullptr);
	}
}

TEST_CASE("events: duplicate EventList names are both appended (noted), the map overlay replaces a list of the same name in place and keeps its address")
{
	Reg r;
	r.globals = { { "A", 5 }, { "B", 5 } };
	const char *one = "<SageLuaScriptSection><EventList Name=\"L\"><EventHandler EventName=\"OnCreated\" ScriptFunctionName=\"A\"/></EventList></SageLuaScriptSection>";
	r.parse(one);
	const LuaEventList *first = r.reg->findEventList("L");
	r.parse(one);
	CHECK(r.reg->eventListCount() == 2);
	CHECK(r.reg->findEventList("L") == first);
	REQUIRE(!r.reg->notes().empty());
	CHECK(r.reg->notes().back().find("defined twice") != std::string::npos);
	// the acceptance stop reaches the report sink (not only the notes): exactly one entry for the one duplicate
	REQUIRE(r.sink.entries().size() == 1);
	CHECK(r.sink.entries()[0].stop == "S-127");
	CHECK(r.sink.entries()[0].text.find("EventList L is defined more than once") != std::string::npos);
	CHECK(r.sink.count("S-127") == 1);
	// overlay: keepOpen replaces the first list of that name
	r.parse("<SageLuaScriptSection><EventList Name=\"L\"><EventHandler EventName=\"OnDamaged\" ScriptFunctionName=\"B\"/></EventList>"
		"<EventList Name=\"New\"/></SageLuaScriptSection>", true);
	CHECK(r.reg->eventListCount() == 3);
	CHECK(r.reg->findEventList("L") == first);
	CHECK(join(r.handlerKeys("L")) == "OnDamaged=B");
}

TEST_CASE("events: ModelConditionEvent: required and excluded sets from +FLAG / -FLAG and from plain lists; the match rule is RW 0x7330DD")
{
	Reg r;
	r.parse("<SageLuaScriptSection><Events>"
		"<ModelConditionEvent Name=\"Moving\"><Conditions>+MOVING</Conditions></ModelConditionEvent>"
		"<ModelConditionEvent Name=\"OnFire\"><Conditions>-DYING +AFLAME</Conditions></ModelConditionEvent>"
		"<ModelConditionEvent Name=\"Exact\"><Conditions>MOVING DAMAGED</Conditions></ModelConditionEvent>"
		"</Events></SageLuaScriptSection>");
	const auto &recs = r.reg->modelConditionEvents();
	REQUIRE(recs.size() == 3);
	auto flags = [](std::initializer_list<const char *> names) {
		ModelConditionFlags f;
		for (const char *n : names)
		{
			const int bit = ModelCondition::indexOf(n);
			REQUIRE(bit >= 0);
			f.set(bit);
		}
		return f;
	};
	// +MOVING: required {MOVING}, nothing excluded
	CHECK(recs[0].matches(flags({ "MOVING" })));
	CHECK(recs[0].matches(flags({ "MOVING", "DAMAGED", "AFLAME" })));
	CHECK(!recs[0].matches(flags({ "DAMAGED" })));
	// -DYING +AFLAME: required {AFLAME}, excluded {DYING}
	CHECK(recs[1].matches(flags({ "AFLAME" })));
	CHECK(!recs[1].matches(flags({ "AFLAME", "DYING" })));
	CHECK(!recs[1].matches(flags({ "MOVING" })));
	// a plain list is an exact match: required {MOVING, DAMAGED}, excluded everything else
	CHECK(recs[2].matches(flags({ "MOVING", "DAMAGED" })));
	CHECK(!recs[2].matches(flags({ "MOVING", "DAMAGED", "AFLAME" })));
	CHECK(!recs[2].matches(flags({ "MOVING" })));
	CHECK(r.reg->findEvent("Moving").kind == LuaEventRef::MODEL_CONDITION);
}

TEST_CASE("events: ModelConditionEvent: an unknown flag is a reported parse error and the element still ends normally; an identical record stops the reader")
{
	{
		Reg r;
		r.parse("<SageLuaScriptSection><Events>"
			"<ModelConditionEvent Name=\"Bad\"><Conditions>+NOT_A_FLAG</Conditions></ModelConditionEvent>"
			"<ModelConditionEvent Name=\"Good\"><Conditions>+MOVING</Conditions></ModelConditionEvent>"
			"</Events></SageLuaScriptSection>");
		CHECK(r.reg->modelConditionEvents().size() == 1);
		CHECK(r.sink.has("S-127", "Bad"));
	}
	{
		Reg r;
		r.parse("<SageLuaScriptSection><Events>"
			"<ModelConditionEvent Name=\"A\"><Conditions>+MOVING</Conditions></ModelConditionEvent>"
			"<ModelConditionEvent Name=\"A\"><Conditions>+MOVING</Conditions></ModelConditionEvent>"
			"<ScriptedEvent Name=\"AfterTheDuplicate\"/>"
			"</Events></SageLuaScriptSection>");
		CHECK(r.reg->modelConditionEvents().size() == 1);
		CHECK(r.reg->scriptedEvents().empty()); // retail returns without consuming the duplicate's closing tags: the rest is lost
	}
	{
		Reg r;
		r.parse("<SageLuaScriptSection><Events>"
			"<ModelConditionEvent Name=\"NoText\"><Conditions></Conditions></ModelConditionEvent>"
			"<ScriptedEvent Name=\"After\"/></Events></SageLuaScriptSection>");
		CHECK(r.reg->modelConditionEvents().empty()); // finish() is not 3: nothing is recorded and the rest of the document is lost
		CHECK(r.reg->scriptedEvents().empty());
	}
	{
		Reg r;
		r.parse("<SageLuaScriptSection><Events>"
			"<ModelConditionEvent Name=\"Mixed\"><Conditions>MOVING +DAMAGED</Conditions></ModelConditionEvent></Events></SageLuaScriptSection>");
		CHECK(r.reg->modelConditionEvents().empty());
		CHECK(r.sink.has("S-127", "you may not mix normal and +- ops in bitstring lists"));
	}
}

TEST_CASE("events: reset clears the lists and event vectors, not the internal slot keys; a declaration other than the exact one is reported, nothing read")
{
	Reg r;
	r.parse("<SageLuaScriptSection><Events><InternalEvent Name=\"OnCreated\"/><ScriptedEvent Name=\"S\"/></Events><EventList Name=\"L\"/></SageLuaScriptSection>");
	CHECK(r.reg->eventListCount() == 1);
	const NameKeyType key = r.reg->internalKey(LUAEVENT_OnCreated);
	r.reg->reset();
	CHECK(r.reg->eventListCount() == 0);
	CHECK(r.reg->scriptedEvents().empty());
	CHECK(r.reg->internalKey(LUAEVENT_OnCreated) == key);
	Reg d;
	d.parse("<?xml version=\"1.0\" encoding=\"UTF-8\"?><SageLuaScriptSection><EventList Name=\"L\"/></SageLuaScriptSection>");
	CHECK(d.reg->eventListCount() == 0);
	CHECK(d.sink.has("S-127", "faults"));
}

// ---------------------------------------------------------------------------------------------------------------------
// the retail file
// ---------------------------------------------------------------------------------------------------------------------
namespace
{
std::string readRetailText(const char *path)
{
	retailtest::Mount *mount = retailtest::pureMount();
	if (!mount)
	{
		return std::string();
	}
	REQUIRE_MESSAGE(mount->fs, mount->error);
	std::vector<std::uint8_t> bytes;
	std::string err;
	REQUIRE_MESSAGE(mount->fs->readFile(path, bytes, &err), err);
	return std::string(bytes.begin(), bytes.end());
}
size_t countMatches(const std::string &text, const char *re)
{
	size_t n = 0;
	const std::regex rx(re);
	for (auto it = std::sregex_iterator(text.begin(), text.end(), rx); it != std::sregex_iterator(); ++it)
	{
		++n;
	}
	return n;
}
} // namespace

TEST_CASE("retail: ScriptEvents.xml parses completely: 40 events (17 internal, 14 scripted, 9 model condition), 77 EventLists, 119 EventHandlers")
{
	if (!retailtest::pureMount())
	{
		retailtest::printSkip("retail: ScriptEvents.xml");
		return;
	}
	const std::string xml = readRetailText("data\\scripts\\scriptevents.xml");
	REQUIRE(!xml.empty());
	CHECK(xml.size() == 28970);
	CHECK(MD5::ofBytes(xml.data(), xml.size()) == "2ff14da66778eb7ee2874dfe70607d3f"); // spec 1.1
	// second, independent count over the same text, comments removed (two EventHandlers are commented out: DamageIncoming for Gandalf and Saruman)
	CHECK(countMatches(xml, "<EventHandler ") == 121);
	std::string text;
	for (size_t at = 0;;)
	{
		const size_t open = xml.find("<!--", at);
		text += xml.substr(at, open == std::string::npos ? std::string::npos : open - at);
		if (open == std::string::npos)
		{
			break;
		}
		at = xml.find("-->", open) + 3;
	}
	CHECK(countMatches(text, "<EventHandler ") == 119);
	CHECK(countMatches(xml, "<InternalEvent ") == 17);
	CHECK(countMatches(xml, "<ScriptedEvent ") == 14);
	CHECK(countMatches(xml, "<ModelConditionEvent ") == 9);
	CHECK(countMatches(xml, "<ObjectStatusEvent ") == 0);
	CHECK(countMatches(text, "<EventList ") == 77);
	Reg r;
	// the handler check needs Scripts.lua; here every function counts as defined
	r.reg->parse(xml, false, [](const std::string &) { return 5; }, r.sink);
	CHECK(r.reg->eventListCount() == 77);
	CHECK(r.reg->scriptedEvents().size() == 14);
	CHECK(r.reg->modelConditionEvents().size() == 9);
	CHECK(r.reg->objectStatusEvents().empty());
	int enabled = 0;
	for (int i = 0; i < 17; ++i)
	{
		enabled += r.reg->internalEnabled(i) ? 1 : 0;
	}
	CHECK(enabled == 17);
	// the one inherit that does not resolve: EvilPorterFunctions inherits InfantryFunctions defined 30 lines later (spec 1.7 item 2)
	size_t unresolved = 0;
	for (const std::string &n : r.reg->notes())
	{
		if (n.find("names no EventList defined earlier") != std::string::npos)
		{
			++unresolved;
			CHECK(n.find("EvilPorterFunctions") != std::string::npos);
		}
	}
	CHECK(unresolved == 1);
	// EvilPorter has only its own two handlers, not the infantry set; Fram's onCreated never matches OnCreated (spec 1.7 item 1)
	CHECK(r.handlerKeys("EvilPorterFunctions").size() == 2);
	CHECK(r.handlerKeys("InfantryFunctions").size() > 2);
	const LuaEventList *fram = r.reg->findEventList("FramFunctions");
	REQUIRE(fram);
	CHECK(fram->find(r.reg->internalKey(LUAEVENT_OnCreated)) == nullptr);
	CHECK(fram->find(r.keys.nameToKey("onCreated")) != nullptr);
	CHECK(r.reg->findEvent("onCreated").kind == LuaEventRef::NONE);
	// RotWK's addition: TrollSlingFunctions and OnMordorFighterCreated in MordorFighterFunctions
	const LuaEventList *troll = r.reg->findEventList("TrollFunctions");
	REQUIRE(troll);
	const LuaEventHandler *created = troll->find(r.reg->internalKey(LUAEVENT_OnCreated));
	REQUIRE(created);
	CHECK(created->function == "OnTrollCreated");
	CHECK(r.reg->findEventList("TrollSlingFunctions") != nullptr);
	const LuaEventList *mf = r.reg->findEventList("MordorFighterFunctions");
	REQUIRE(mf);
	const LuaEventHandler *mfc = mf->find(r.reg->internalKey(LUAEVENT_OnCreated));
	REQUIRE(mfc);
	CHECK(mfc->function == "OnMordorFighterCreated");
	// the model condition events and their flags
	std::map<std::string, size_t> byName;
	for (const LuaModelConditionEvent &m : r.reg->modelConditionEvents())
	{
		byName[r.keys.keyToName(m.key)] = (size_t)(&m - &r.reg->modelConditionEvents()[0]);
	}
	CHECK(byName.size() == 9);
	CHECK(byName.count("OnFire") == 1);
	CHECK(byName.count("UsingSpecialOne") == 1);
	CHECK(byName.count("CinematicRampageUser") == 1);
	// the lists with no handlers of their own (spec 1.3): those on BaseScriptFunctions (which has none) stay empty, those on InfantryFunctions /
	// GondorFighterFunctions have exactly their base's handlers
	for (const char *n : { "BaseScriptFunctions", "TheodenFunctions", "SarumanFunctions", "GwaihirFunctions" })
	{
		INFO(n);
		REQUIRE(r.reg->findEventList(n));
		CHECK(r.reg->findEventList(n)->handlers.empty());
	}
	CHECK(join(r.handlerKeys("ArcherFunctions")) == join(r.handlerKeys("InfantryFunctions")));
	CHECK(join(r.handlerKeys("HaradrimFunctions")) == join(r.handlerKeys("InfantryFunctions")));
	CHECK(join(r.handlerKeys("GondorFighterHordeFunctions")) == join(r.handlerKeys("GondorFighterFunctions")));
	// FindEvent order: internal, scripted, model condition
	CHECK(r.reg->findEvent("OnCreated").kind == LuaEventRef::INTERNAL);
	CHECK(r.reg->findEvent("BeAfraidOfBalrog").kind == LuaEventRef::SCRIPTED);
	CHECK(r.reg->findEvent("OnFire").kind == LuaEventRef::MODEL_CONDITION);
}
