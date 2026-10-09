// OpenBFME unit tests: ThingTemplate, ModuleFactory and parseObjectDefinition (INI port steps 7-10). GPL-3.0.
//
// Every expected value comes from the RotWK disassembly or the spec sections cited on the case
// (spec = ini-and-object-model.md; RW = RotWK game.dat, caveat S-001), not from running this code.
// The synthetic INI uses real class names and real field names of the binary's registry.

#include "doctest.h"
#include "ObjectTestUtil.h"

#include "GameLogic/ArmorSet.h"
#include "GameLogic/WeaponSet.h"

#include <algorithm>
#include <map>
#include <set>

using namespace objtest;

namespace
{
// A default template like retail's Data\INI\Default\Object.ini, trimmed: a dummy body and one die module,
// an inheritable update module, and an armor set / weapon set.
const char kDefault[] =
	"Object DefaultThingTemplate\n"
	"  Body = InactiveBody ModuleTag_DefaultBody\n"
	"  End\n"
	"  Behavior = DestroyDie ModuleTag_DefaultDie\n"
	"  End\n"
	"  InheritableModule\n"
	"    Behavior = KeepObjectDie ModuleTag_DefaultKeep\n"
	"    End\n"
	"  End\n"
	"  Draw = W3DDefaultDraw ModuleTag_DefaultDraw\n"
	"  End\n"
	"  ArmorSet\n"
	"    Conditions = None\n"
	"    Armor = NoArmor\n"
	"  End\n"
	"End\n";

Mods M(std::initializer_list<std::pair<const char *, const char *>> l)
{
	Mods m;
	for (auto &p : l)
	{
		m.emplace_back(p.first, p.second);
	}
	return m;
}
}

// ---------------------------------------------------------------------------------------------
// templates, ids, names
// ---------------------------------------------------------------------------------------------
TEST_CASE("object: a template gets the next id from 1 and names are case sensitive (RW 0x6D1ACD, 0x6D2848, 0x4065AA)")
{
	World w;
	CHECK(w.load("Object Foo\nEnd\nObject foo\nEnd\nObject Bar\nEnd\n").empty());
	REQUIRE(w.get("Foo"));
	REQUIRE(w.get("foo"));
	CHECK(w.get("Foo") != w.get("foo"));
	CHECK(w.get("Foo")->getTemplateID() == 1);
	CHECK(w.get("foo")->getTemplateID() == 2);
	CHECK(w.get("Bar")->getTemplateID() == 3);
	CHECK(w.things.templateCount() == 3);
	CHECK(w.get("FOO") == nullptr);
	CHECK(w.get("Foo")->getName() == "Foo");
}

TEST_CASE("object: extra tokens on a block header are ignored, a missing name is an error (B1 INIObject.cpp)")
{
	World w;
	CHECK(w.load("Object Foo Extra Tokens\nEnd\n").empty());
	CHECK(w.get("Foo"));
	int code = 0;
	const std::string err = w.load("Object\nEnd\n", INI_LOAD_OVERWRITE, "x.ini", &code);
	CHECK(contains(err, "Expected additional data"));
	CHECK(code == 3);
	CHECK(contains(w.load("ChildObject Lonely\nEnd\n"), "Expected additional data"));
}

TEST_CASE("object: an unknown field is a loud error naming field, block and line (spec 1.9, 4.3)")
{
	World w;
	int code = 0;
	const std::string err = w.load("Object Foo\n  NoSuchField = 3\nEnd\n", INI_LOAD_OVERWRITE, "obj.ini", &code);
	CHECK(code == 5);
	CHECK(contains(err, "Unknown field 'NoSuchField' in block 'Object'"));
	CHECK(contains(err, "obj.ini"));
	CHECK(contains(err, "line 2"));
}

TEST_CASE("object: field names match case sensitively, so 'buildcost' is unknown (spec 1.6)")
{
	World w;
	CHECK(contains(w.load("Object Foo\n  buildcost = 5\nEnd\n"), "Unknown field 'buildcost'"));
}

TEST_CASE("object: a missing End is reported (spec 1.7)")
{
	World w;
	int code = 0;
	CHECK(contains(w.load("Object Foo\n  BuildCost = 5\n", INI_LOAD_OVERWRITE, "obj.ini", &code), "Missing 'END' token"));
	CHECK(code == 4);
}

// ---------------------------------------------------------------------------------------------
// typed object fields
// ---------------------------------------------------------------------------------------------
TEST_CASE("fields: rows whose parse function is a standard INI parser store the parsed value")
{
	World w;
	CHECK(w.load(
		"Object Foo\n"
		"  BuildCost = 300\n"            // RW 0x42EC11 parseUnsignedShort
		"  BountyValue = -12\n"          // 0x42EC5E parseInt
		"  VisionRange = 2.5\n"          // 0x42ED00 parseReal
		"  IsBridge = Yes\n"             // 0x42E558 parseBool
		"  Side = Mordor\n"              // 0x42EE5E parseAsciiString
		"  EquivalentTo = A B C\n"       // 0x42EED6 vector
		"  VisionSide = 50%\n"           // 0x42EEFA percent to real
		"  BuildTime = 15\n"
		"End\n").empty());
	const ThingTemplate *t = w.get("Foo");
	REQUIRE(t);
	CHECK(std::get<long long>(*t->findField("BuildCost")) == 300);
	CHECK(std::get<long long>(*t->findField("BountyValue")) == -12);
	CHECK(std::get<float>(*t->findField("VisionRange")) == doctest::Approx(2.5f));
	CHECK(std::get<bool>(*t->findField("IsBridge")) == true);
	CHECK(std::get<std::string>(*t->findField("Side")) == "Mordor");
	CHECK(std::get<std::vector<std::string>>(*t->findField("EquivalentTo")) == std::vector<std::string>{ "A", "B", "C" });
	CHECK(std::get<float>(*t->findField("VisionSide")) == doctest::Approx(0.5f));
	CHECK(t->findField("RefundValue") == nullptr); // never set: absent (stop S-072)
}

TEST_CASE("fields: velocity and 16-bit bit-string rows (RW 0x73A4B6, 0x42EF5A)")
{
	World w;
	// CrushKnockback / RamPower: scanReal * 0.2f (the logic-frame global at RW 0xD9F61C)
	CHECK(w.load("Object Foo\n  CrushKnockback = 5\n  RamPower = 2.5\n  Shadow = SHADOW_DECAL SHADOW_VOLUME\nEnd\n").empty());
	const ThingTemplate *t = w.get("Foo");
	CHECK(std::get<float>(*t->findField("CrushKnockback")) == doctest::Approx(1.0f));
	CHECK(std::get<float>(*t->findField("RamPower")) == doctest::Approx(0.5f));
	CHECK(std::get<long long>(*t->findField("Shadow")) == 3); // bit 0 and bit 1 of the shadow-type names
	// the 17th name needs bit 16: a word cannot hold it (retail throws the plain int 1)
	int code = 0;
	CHECK(contains(w.load("Object Bar\n  Shadow = SHADOW_SUBTRACT_DECAL_DYNAMIC\nEnd\n", INI_LOAD_OVERWRITE, "obj.ini", &code), "Unknown error parsing field 'Shadow'"));
	CHECK(code == 8);
	CHECK(contains(w.load("Object Baz\n  Shadow = SHADOW_NOPE\nEnd\n"), "is not a valid member of the index list"));
}

TEST_CASE("fields: range-checked parsers reject out-of-range values like retail (RW 0x42EC11 throws the plain int 1)")
{
	World w;
	int code = 0;
	const std::string err = w.load("Object Foo\n  BuildCost = 70000\nEnd\n", INI_LOAD_OVERWRITE, "obj.ini", &code);
	CHECK(code == 8); // initFromINIMulti wraps a non-INIException as 'Unknown error parsing field'
	CHECK(contains(err, "Unknown error parsing field 'BuildCost'"));
}

TEST_CASE("fields: parseUnsignedInt with a maximum userData (RW 0x42ECB2)")
{
	World w;
	// the table row Flammability.Fuel has userData 0xFFFF in the lazily built table; the object table's
	// own rows with fn 0x42ECB2 have userData 0 (no limit)
	CHECK(w.load("Object Foo\n  CampnessValue = 4000000000\nEnd\n").empty());
	CHECK(std::get<long long>(*w.get("Foo")->findField("CampnessValue")) == 4000000000LL);
}

TEST_CASE("fields: a later occurrence overwrites an earlier one (the INI loop calls the parser again)")
{
	World w;
	CHECK(w.load("Object Foo\n  BuildCost = 1\n  BuildCost = 2\nEnd\n").empty());
	CHECK(std::get<long long>(*w.get("Foo")->findField("BuildCost")) == 2);
}

TEST_CASE("fields: rows without a typed port keep their tokens raw (stop S-072)")
{
	World w;
	CHECK(w.load("Object Foo\n  KindOf = STRUCTURE SELECTABLE\n  VoiceSelect = SomeEvent\nEnd\n").empty());
	const FieldValue *kind = w.get("Foo")->findField("KindOf");
	REQUIRE(kind);
	const RawTokens &raw = std::get<RawTokens>(*kind);
	CHECK(raw.tokens == std::vector<std::string>{ "STRUCTURE", "SELECTABLE" });
	CHECK(raw.line.sourceLine == 2);
	CHECK(raw.line.file == "obj.ini");
	CHECK(w.things.rawFieldCounts().at("KindOf") == 1);
	CHECK(w.things.rawFieldCounts().at("VoiceSelect") == 1);
}

// ---------------------------------------------------------------------------------------------
// modules: declaration
// ---------------------------------------------------------------------------------------------
TEST_CASE("modules: Behavior / Body / Draw / ClientUpdate / ClientBehavior fill the four lists in order (spec 4.3, 4.4)")
{
	World w;
	CHECK(w.load(
		"Object Foo\n"
		"  Behavior = DestroyDie ModuleTag_Die\n"
		"  End\n"
		"  Body = ActiveBody ModuleTag_Body\n"
		"    MaxHealth = 100\n"
		"  End\n"
		"  Draw = W3DDefaultDraw ModuleTag_Draw\n"
		"  End\n"
		"  ClientBehavior = TerrainResourceClientBehavior ModuleTag_CB\n"
		"  End\n"
		"End\n").empty());
	const ThingTemplate *t = w.get("Foo");
	REQUIRE(t);
	CHECK(modulesOf(t) == M({ { "DestroyDie", "ModuleTag_Die" }, { "ActiveBody", "ModuleTag_Body" }, { "W3DDefaultDraw", "ModuleTag_Draw" }, { "TerrainResourceClientBehavior", "ModuleTag_CB" } }));
	CHECK(t->behaviorModules().size() == 2);
	CHECK(t->drawModules().size() == 1);
	CHECK(t->clientBehaviorModules().size() == 1);
	CHECK(t->clientUpdateModules().size() == 0);
	const ThingTemplate::Nugget &body = t->behaviorModules().nuggets()[1];
	CHECK(body.interfaceMask == MODULEINTERFACE_BODY);
	CHECK(!body.copiedFromDefault);
	CHECK(body.data->getModuleTagNameKey() == w.keys.findKey("ModuleTag_Body"));
}

TEST_CASE("modules: ClientUpdate modules fill the third list (mask 0x800, module type 2)")
{
	World w;
	CHECK(w.load("Object Foo\n  ClientUpdate = BeaconClientUpdate ModuleTag_CU\n    RadarPulseFrequency = 5\n  End\n  ClientUpdate = SwayClientUpdate ModuleTag_Sway\n  End\nEnd\n").empty());
	const ThingTemplate *t = w.get("Foo");
	CHECK(t->clientUpdateModules().size() == 2);
	CHECK(t->clientUpdateModules().nuggets()[0].interfaceMask == MODULEINTERFACE_CLIENT_UPDATE);
	CHECK(modulesOf(t) == M({ { "BeaconClientUpdate", "ModuleTag_CU" }, { "SwayClientUpdate", "ModuleTag_Sway" } }));
	// a client-update class declared as a Behavior is another decorated key and does not exist
	World w2;
	CHECK(contains(w2.load("Object Foo\n  Behavior = SwayClientUpdate T\n  End\nEnd\n"), "Unknown module class 'SwayClientUpdate'"));
	// a client-update tag clashes with the other lists too (update module in the message, RW 0xC26D80)
	World w3;
	CHECK(contains(w3.load("Object Foo\n  ClientUpdate = SwayClientUpdate T\n  End\n  Behavior = DestroyDie T\n  End\nEnd\n"), "also already on update module 'SwayClientUpdate'"));
}

TEST_CASE("modules: the module tag is required (RW 0x73F27E, spec 4.4 step 1)")
{
	World w;
	int code = 0;
	CHECK(contains(w.load("Object Foo\n  Behavior = DestroyDie\n  End\nEnd\n", INI_LOAD_OVERWRITE, "obj.ini", &code), "Expected additional data"));
	CHECK(code == 3);
}

TEST_CASE("modules: Body must name a body class, other types must not (RW 0xC27254, 0xC2723C)")
{
	World w;
	CHECK(contains(w.load("Object Foo\n  Body = DestroyDie ModuleTag_X\n  End\nEnd\n"), "Only Body allowed here"));
	World w2;
	CHECK(contains(w2.load("Object Foo\n  Behavior = ActiveBody ModuleTag_X\n  End\nEnd\n"), "No Body allowed here"));
	World w3;
	CHECK(contains(w3.load("Object Foo\n  Body = NotAClass ModuleTag_X\n  End\nEnd\n"), "Only Body allowed here")); // unknown: mask 0 (RW 0x656C5E)
}

TEST_CASE("modules: an unknown class is a loud error (RW dereferences NULL here, stop S-075)")
{
	World w;
	int code = 0;
	const std::string err = w.load("Object Foo\n  Behavior = NoSuchBehavior ModuleTag_X\n  End\nEnd\n", INI_LOAD_OVERWRITE, "obj.ini", &code);
	CHECK(code == 3);
	CHECK(contains(err, "Unknown module class 'NoSuchBehavior'"));
	CHECK(contains(err, "S-075"));
}

TEST_CASE("modules: the registry is case sensitive: 'activebody' is not 'ActiveBody' (decorated NAMEKEY, RW 0x655A0C)")
{
	World w;
	CHECK(contains(w.load("Object Foo\n  Body = activebody ModuleTag_X\n  End\nEnd\n"), "Only Body allowed here"));
}

TEST_CASE("modules: a module class registered for another type is not found (key is '0'+type+name)")
{
	World w;
	// W3DDefaultDraw is a DRAW module (type 1): declared as Behavior its mask is 0 and it is unknown
	const std::string err = w.load("Object Foo\n  Behavior = W3DDefaultDraw ModuleTag_X\n  End\nEnd\n");
	CHECK(contains(err, "Unknown module class 'W3DDefaultDraw'"));
}

TEST_CASE("modules: tags are unique across all lists of one template (RW 0x73EF3B, spec 4.4 step 8)")
{
	World w;
	const std::string err = w.load(
		"Object Foo\n"
		"  Behavior = DestroyDie ModuleTag_Same\n"
		"  End\n"
		"  Draw = W3DDefaultDraw ModuleTag_Same\n"
		"  End\n"
		"End\n");
	CHECK(contains(err, "addModuleInfo - ERROR defining module 'W3DDefaultDraw' on thing template 'Foo'"));
	CHECK(contains(err, "has the tag 'ModuleTag_Same' which must be unique among all modules for this object"));
	CHECK(contains(err, "also already on behavior module 'DestroyDie'"));
	CHECK(contains(err, "Please make unique tag names within an object definition."));
	// the message names the kind of the list that already holds the tag
	World w2;
	CHECK(contains(w2.load("Object Foo\n  Draw = W3DDefaultDraw T\n  End\n  Behavior = DestroyDie T\n  End\nEnd\n"), "also already on draw module 'W3DDefaultDraw'"));
	World w3;
	CHECK(contains(w3.load("Object Foo\n  ClientBehavior = TerrainResourceClientBehavior T\n  End\n  Behavior = DestroyDie T\n  End\nEnd\n"), "also already on client behavior module"));
}

TEST_CASE("modules: tags compare case sensitively (RW 0x4065AA is strcmp)")
{
	World w;
	CHECK(w.load("Object Foo\n  Behavior = DestroyDie Tag\n  End\n  Behavior = KeepObjectDie tag\n  End\nEnd\n").empty());
	CHECK(w.get("Foo")->behaviorModules().size() == 2);
}

TEST_CASE("modules: a nested End inside a module body does not close the object (extent comes from the class table)")
{
	World w;
	CHECK(w.load(
		"Object Foo\n"
		"  Draw = W3DScriptedModelDraw ModuleTag_Draw\n"
		"    DefaultModelConditionState\n"
		"      Model = SomeModel\n"
		"    End\n"
		"    AnimationState = MOVING\n"
		"      Animation = Run\n"
		"        AnimationName = X\n"
		"      End\n"
		"    End\n"
		"  End\n"
		"  BuildCost = 7\n"
		"End\n").empty());
	const ThingTemplate *t = w.get("Foo");
	CHECK(t->drawModules().size() == 1);
	CHECK(std::get<long long>(*t->findField("BuildCost")) == 7); // the object continued after the draw module
	const RawModuleData *raw = dynamic_cast<const RawModuleData *>(t->drawModules().nuggets()[0].data.get());
	REQUIRE(raw);
	CHECK(raw->className() == "W3DScriptedModelDraw");
	CHECK(raw->lines().size() == 9); // 8 body lines + the module's End
	CHECK(raw->lines().front().text.find("DefaultModelConditionState") != std::string::npos);
	CHECK(raw->lines().front().sourceLine == 3);
	CHECK(raw->lines().back().sourceLine == 11);
	CHECK(raw->lines().back().file == "obj.ini");
}

TEST_CASE("modules: a BeginScript body ends at ENDSCRIPT; lines in it that look like End do not close anything (RW 0x42D400)")
{
	World w;
	CHECK(w.load(
		"Object Foo\n"
		"  Draw = W3DScriptedModelDraw ModuleTag_Draw\n"
		"    IdleAnimationState\n"
		"      BeginScript\n"
		"        if Prev == \"A\" then x() end\n"
		"        End\n"
		"        end\n"
		"      EndScript\n"
		"    End\n"
		"  End\n"
		"  BuildCost = 9\n"
		"End\n").empty());
	CHECK(std::get<long long>(*w.get("Foo")->findField("BuildCost")) == 9);
	// the terminator is matched case-insensitively
	World w2;
	CHECK(w2.load("Object Foo\n  Draw = W3DScriptedModelDraw T\n    IdleAnimationState\n      BeginScript\n      endscript\n    End\n  End\nEnd\n").empty());
	// no ENDSCRIPT: retail never leaves its read loop (stop S-076); this port reports it
	World w3;
	CHECK(contains(w3.load("Object Foo\n  Draw = W3DScriptedModelDraw T\n    IdleAnimationState\n      BeginScript\n      x\n"), "Missing 'ENDSCRIPT' token"));
}

TEST_CASE("modules: field names inside a module body come from the class's own table and are strcmp-matched")
{
	World w;
	CHECK(contains(w.load("Object Foo\n  Body = ActiveBody T\n    NotAField = 1\n  End\nEnd\n"), "Unknown field 'NotAField' in block 'Object'"));
	World w2;
	CHECK(w2.load("Object Foo\n  Body = ActiveBody T\n    MaxHealth = 10\n    MaxHealthDamaged = 5\n  End\nEnd\n").empty());
	// a class with no table at all (base ModuleData, table NULL) accepts only End
	World w3;
	CHECK(w3.load("Object Foo\n  Body = InactiveBody T\n  End\nEnd\n").empty());
	World w4;
	CHECK(contains(w4.load("Object Foo\n  Body = InactiveBody T\n    Anything = 1\n  End\nEnd\n"), "Unknown field 'Anything'"));
}

// ---------------------------------------------------------------------------------------------
// modules: inheritance
// ---------------------------------------------------------------------------------------------
TEST_CASE("inherit: a new Object copies the default and a declaration drops the default modules that share an interface bit (spec 4.4 step 4, 4.6)")
{
	World w;
	REQUIRE(w.load(kDefault).empty());
	CHECK(modulesOf(w.get("DefaultThingTemplate")) == M({ { "InactiveBody", "ModuleTag_DefaultBody" }, { "DestroyDie", "ModuleTag_DefaultDie" }, { "KeepObjectDie", "ModuleTag_DefaultKeep" }, { "W3DDefaultDraw", "ModuleTag_DefaultDraw" } }));
	// no modules declared: everything is inherited, flagged copied-from-default
	REQUIRE(w.load("Object Plain\nEnd\n").empty());
	CHECK(modulesOf(w.get("Plain")) == modulesOf(w.get("DefaultThingTemplate")));
	for (const auto &n : w.get("Plain")->behaviorModules().nuggets())
	{
		CHECK(n.copiedFromDefault);
	}
	// Body = ActiveBody (mask BODY) drops InactiveBody; SlowDeathBehavior (mask DIE|UPDATE) drops DestroyDie
	// but NOT the inheritable KeepObjectDie
	REQUIRE(w.load(
		"Object Own\n"
		"  Body = ActiveBody ModuleTag_Body\n"
		"  End\n"
		"  Behavior = SlowDeathBehavior ModuleTag_Slow\n"
		"  End\n"
		"End\n").empty());
	CHECK(modulesOf(w.get("Own")) == M({ { "KeepObjectDie", "ModuleTag_DefaultKeep" }, { "ActiveBody", "ModuleTag_Body" }, { "SlowDeathBehavior", "ModuleTag_Slow" }, { "W3DDefaultDraw", "ModuleTag_DefaultDraw" } }));
	const ThingTemplate::Nugget &keep = w.get("Own")->behaviorModules().nuggets()[0];
	CHECK(keep.inheritable);
	CHECK(keep.copiedFromDefault);
}

TEST_CASE("inherit: the default's own InheritableModule marks the nugget inheritable (RW 0x73F4C1)")
{
	World w;
	REQUIRE(w.load(kDefault).empty());
	for (const auto &n : w.get("DefaultThingTemplate")->behaviorModules().nuggets())
	{
		CHECK(n.inheritable == (n.tag == "ModuleTag_DefaultKeep"));
	}
}

TEST_CASE("inherit: re-declaring a default's tag in a plain Object is an error unless the default module was cleared first")
{
	World w;
	REQUIRE(w.load(kDefault).empty());
	// DestroyDie has mask DIE; declaring another DIE module with the SAME tag clears the default (copiedFromDefault) first, so it is fine
	CHECK(w.load("Object A\n  Behavior = SlowDeathBehavior ModuleTag_DefaultDie\n  End\nEnd\n").empty());
	// the inheritable keep-module survives the clearing, so reusing ITS tag is a duplicate
	CHECK(contains(w.load("Object B\n  Behavior = SlowDeathBehavior ModuleTag_DefaultKeep\n  End\nEnd\n"), "must be unique"));
}

TEST_CASE("inherit: ChildObject copies the parent, the same tag replaces, a new tag adds, nothing is cleared (spec 4.6, RW 0x73F33D)")
{
	World w;
	REQUIRE(w.load(
		"Object Parent\n"
		"  Body = ActiveBody ModuleTag_Body\n    MaxHealth = 100\n  End\n"
		"  Behavior = DestroyDie ModuleTag_Die\n  End\n"
		"  Draw = W3DDefaultDraw ModuleTag_Draw\n  End\n"
		"End\n").empty());
	REQUIRE(w.load(
		"ChildObject Kid Parent\n"
		"  Behavior = SlowDeathBehavior ModuleTag_Die\n  End\n"   // same tag: replaces DestroyDie even though classes differ
		"  Behavior = KeepObjectDie ModuleTag_New\n  End\n"       // new tag: added
		"End\n").empty());
	CHECK(modulesOf(w.get("Kid")) == M({ { "ActiveBody", "ModuleTag_Body" }, { "SlowDeathBehavior", "ModuleTag_Die" }, { "KeepObjectDie", "ModuleTag_New" }, { "W3DDefaultDraw", "ModuleTag_Draw" } }));
	// the parent is untouched, and the child's module data are separate objects
	CHECK(modulesOf(w.get("Parent")) == M({ { "ActiveBody", "ModuleTag_Body" }, { "DestroyDie", "ModuleTag_Die" }, { "W3DDefaultDraw", "ModuleTag_Draw" } }));
	CHECK(w.get("Kid")->getTemplateID() != w.get("Parent")->getTemplateID());
	// nothing was cleared: a die module of another class with a NEW tag keeps the parent's die module
	REQUIRE(w.load("ChildObject Kid2 Parent\n  Behavior = SlowDeathBehavior ModuleTag_Other\n  End\nEnd\n").empty());
	CHECK(modulesOf(w.get("Kid2")) == M({ { "ActiveBody", "ModuleTag_Body" }, { "DestroyDie", "ModuleTag_Die" }, { "SlowDeathBehavior", "ModuleTag_Other" }, { "W3DDefaultDraw", "ModuleTag_Draw" } }));
}

TEST_CASE("inherit: a ChildObject draw module with the tag of another CLASS is an error, the same class replaces (RW 0x73EFA2-0x73EFB8)")
{
	World w;
	REQUIRE(w.load("Object P\n  Draw = W3DDefaultDraw ModuleTag_Draw\n  End\nEnd\n").empty());
	CHECK(contains(w.load("ChildObject C1 P\n  Draw = W3DScriptedModelDraw ModuleTag_Draw\n  End\nEnd\n"), "also already on draw module 'W3DDefaultDraw'"));
	CHECK(w.load("ChildObject C2 P\n  Draw = W3DDefaultDraw ModuleTag_Draw\n  End\nEnd\n").empty());
	CHECK(w.get("C2")->drawModules().size() == 1);
}

TEST_CASE("inherit: a tag may move between lists in a ChildObject (removed from ALL lists, RW 0x73E2B3)")
{
	World w;
	REQUIRE(w.load("Object P\n  Behavior = DestroyDie ModuleTag_X\n  End\nEnd\n").empty());
	// the same tag declared as a CLIENT behavior: the parent's behavior module with that tag is replaced
	REQUIRE(w.load("ChildObject C P\n  ClientBehavior = TerrainResourceClientBehavior ModuleTag_X\n  End\nEnd\n").empty());
	CHECK(modulesOf(w.get("C")) == M({ { "TerrainResourceClientBehavior", "ModuleTag_X" } }));
}

TEST_CASE("inherit: a ChildObject AI module clears the parent's AI modules, other modules stay (RW 0x73F49B, vtable slot 4)")
{
	World w;
	REQUIRE(w.load("Object P\n  Behavior = AIUpdateInterface ModuleTag_AI\n  End\n  Behavior = DestroyDie ModuleTag_Die\n  End\nEnd\n").empty());
	REQUIRE(w.load("ChildObject C P\n  Behavior = HordeAIUpdate ModuleTag_NewAI\n  End\nEnd\n").empty());
	CHECK(modulesOf(w.get("C")) == M({ { "DestroyDie", "ModuleTag_Die" }, { "HordeAIUpdate", "ModuleTag_NewAI" } }));
	// the same clearing applies to a plain Object that declares an AI module twice in a row: its own earlier AI module goes
	REQUIRE(w.load("Object Q\n  Behavior = AIUpdateInterface ModuleTag_AI1\n  End\n  Behavior = HordeAIUpdate ModuleTag_AI2\n  End\nEnd\n").empty());
	CHECK(modulesOf(w.get("Q")) == M({ { "HordeAIUpdate", "ModuleTag_AI2" } }));
}

TEST_CASE("inherit: a ChildObject body clears the parent's body through the slot 7 predicate, but InactiveBody is not cleared (RW 0x73F4A9)")
{
	World w;
	REQUIRE(w.load("Object P\n  Body = ActiveBody ModuleTag_Body\n  End\nEnd\nObject P2\n  Body = InactiveBody ModuleTag_Body\n  End\nEnd\n").empty());
	// child body replaces the real body even under a different tag
	REQUIRE(w.load("ChildObject C P\n  Body = StructureBody ModuleTag_Other\n  End\nEnd\n").empty());
	CHECK(modulesOf(w.get("C")) == M({ { "StructureBody", "ModuleTag_Other" } }));
	// InactiveBody (slot 7 false) is not cleared: both bodies are present
	REQUIRE(w.load("ChildObject C2 P2\n  Body = StructureBody ModuleTag_Other\n  End\nEnd\n").empty());
	CHECK(modulesOf(w.get("C2")) == M({ { "InactiveBody", "ModuleTag_Body" }, { "StructureBody", "ModuleTag_Other" } }));
}

TEST_CASE("inherit: ObjectReskin clears like Object, not like ChildObject, and records its source (spec 4.6; RW 0x73FA52)")
{
	World w;
	REQUIRE(w.load(kDefault).empty());
	REQUIRE(w.load("Object Orig\n  Behavior = DestroyDie ModuleTag_Die\n  End\n  Draw = W3DDefaultDraw ModuleTag_Draw\n  End\nEnd\n").empty());
	REQUIRE(w.load("ObjectReskin Skin Orig\n  Draw = W3DScriptedModelDraw ModuleTag_Skin\n  End\nEnd\n").empty());
	// the reskin's draw module has mask DRAW: the copied W3DDefaultDraw is dropped (copied-from-default is set by the
	// copy); the body, the die module and the inheritable keep-module are copied as they are
	CHECK(modulesOf(w.get("Skin")) == M({ { "InactiveBody", "ModuleTag_DefaultBody" }, { "KeepObjectDie", "ModuleTag_DefaultKeep" }, { "DestroyDie", "ModuleTag_Die" }, { "W3DScriptedModelDraw", "ModuleTag_Skin" } }));
	// a ChildObject with the same declaration would have kept the draw module
	REQUIRE(w.load("ChildObject KidOfOrig Orig\n  Draw = W3DScriptedModelDraw ModuleTag_Skin\n  End\nEnd\n").empty());
	CHECK(modulesOf(w.get("KidOfOrig")).size() == 5);
	CHECK(w.get("Skin")->reskinnedFrom() == std::vector<std::string>{ "Orig" });
	// the full object table applies (not ZH's visual-only table)
	REQUIRE(w.load("ObjectReskin Skin2 Orig\n  BuildCost = 5\nEnd\n").empty());
	CHECK(std::get<long long>(*w.get("Skin2")->findField("BuildCost")) == 5);
	// a reskin of a reskin accumulates the names
	REQUIRE(w.load("ObjectReskin Skin3 Skin\nEnd\n").empty());
	CHECK(w.get("Skin3")->reskinnedFrom() == std::vector<std::string>{ "Orig", "Skin" });
}

TEST_CASE("inherit: ChildObject / ObjectReskin must follow their original (RW 0xC186EC, 0xC186B0)")
{
	World w;
	int code = 0;
	std::string err = w.load("ChildObject Kid Missing\nEnd\n", INI_LOAD_OVERWRITE, "obj.ini", &code);
	CHECK(contains(err, "ChildObject must come after the original Object (Missing, Kid)."));
	CHECK(code == 3);
	err = w.load("ObjectReskin Skin Missing\nEnd\n");
	CHECK(contains(err, "ObjectReskin must come after the original Object (Missing, Skin)."));
}

TEST_CASE("inherit: ChildObject parses with load type 4 and the type is restored afterwards (RW 0x6D29D9, 0x6D29E5)")
{
	World w;
	// observable through the parse: a ChildObject's fields are parsed with load type 4, which is why modules are
	// not cleared; the next block of the same file is parsed with the file's own type again
	REQUIRE(w.load(
		"Object P\n  Behavior = DestroyDie ModuleTag_Die\n  End\nEnd\n"
		"ChildObject C P\n  Behavior = SlowDeathBehavior ModuleTag_New\n  End\nEnd\n"
		"Object Q\n  Behavior = DestroyDie ModuleTag_Die\n  End\n  Behavior = SlowDeathBehavior ModuleTag_Die2\n  End\nEnd\n").empty());
	CHECK(modulesOf(w.get("C")) == M({ { "DestroyDie", "ModuleTag_Die" }, { "SlowDeathBehavior", "ModuleTag_New" } }));
	// Q is a plain Object: copied-from-default clearing applies to its own declarations... but nothing was copied,
	// so both modules stay
	CHECK(modulesOf(w.get("Q")) == M({ { "DestroyDie", "ModuleTag_Die" }, { "SlowDeathBehavior", "ModuleTag_Die2" } }));
}

TEST_CASE("inherit: a duplicate Object block (load type 1) parses into the existing template (RW 0x6D28DC, spec 4.2)")
{
	World w;
	REQUIRE(w.load("Object Foo\n  BuildCost = 1\n  BountyValue = 2\nEnd\n").empty());
	REQUIRE(w.load("Object Foo\n  BuildCost = 9\nEnd\n").empty());
	CHECK(w.things.templateCount() == 1);
	CHECK(std::get<long long>(*w.get("Foo")->findField("BuildCost")) == 9);
	CHECK(std::get<long long>(*w.get("Foo")->findField("BountyValue")) == 2); // merged
}

TEST_CASE("inherit: the developer reload (load type 5) makes a new template of the same name (RW 0x6D28E5)")
{
	World w;
	REQUIRE(w.load("Object Foo\n  BuildCost = 1\nEnd\n").empty());
	const ThingTemplate *first = w.get("Foo");
	REQUIRE(w.load("Object Foo\n  BuildCost = 2\nEnd\n", INI_LOAD_RELOAD).empty());
	CHECK(w.get("Foo") != first);
	CHECK(std::get<long long>(*w.get("Foo")->findField("BuildCost")) == 2);
}

// ---------------------------------------------------------------------------------------------
// AddModule / RemoveModule / ReplaceModule / InheritableModule (spec 4.7)
// ---------------------------------------------------------------------------------------------
TEST_CASE("edit: AddModule adds modules that still go through the mask clearing in load type 1 (spec 4.7)")
{
	World w;
	REQUIRE(w.load(kDefault).empty());
	REQUIRE(w.load("Object A\n  AddModule\n    Behavior = SlowDeathBehavior ModuleTag_Slow\n    End\n  End\nEnd\n").empty());
	// AddModule is a mode, not an exemption: the declaration still clears the copied die module
	CHECK(modulesOf(w.get("A")) == M({ { "InactiveBody", "ModuleTag_DefaultBody" }, { "KeepObjectDie", "ModuleTag_DefaultKeep" }, { "SlowDeathBehavior", "ModuleTag_Slow" }, { "W3DDefaultDraw", "ModuleTag_DefaultDraw" } }));
}

TEST_CASE("edit: RemoveModule removes by tag and errors when nothing matches (B1 ThingTemplateModuleRemovalParsers.cpp, RW 0xC26BF0)")
{
	World w;
	REQUIRE(w.load("Object P\n  Behavior = DestroyDie ModuleTag_A\n  End\n  Draw = W3DDefaultDraw ModuleTag_B\n  End\nEnd\n").empty());
	REQUIRE(w.load("ChildObject C P\n  RemoveModule ModuleTag_A\nEnd\n").empty());
	CHECK(modulesOf(w.get("C")) == M({ { "W3DDefaultDraw", "ModuleTag_B" } }));
	int code = 0;
	const std::string err = w.load("ChildObject D P\n  RemoveModule ModuleTag_Nope\nEnd\n", INI_LOAD_OVERWRITE, "obj.ini", &code);
	CHECK(code == 3);
	CHECK(contains(err, "RemoveModule ModuleTag_Nope was not found for D."));
}

TEST_CASE("edit: RemoveModule removes the tag from every list (RW 0x73E2B3)")
{
	World w;
	REQUIRE(w.load("Object P\n  ClientBehavior = TerrainResourceClientBehavior ModuleTag_A\n  End\nEnd\n").empty());
	REQUIRE(w.load("ChildObject C P\n  RemoveModule ModuleTag_A\nEnd\n").empty());
	CHECK(modulesOf(w.get("C")).empty());
}

TEST_CASE("edit: ReplaceModule replaces a module with another of the same class and a NEW tag (RW 0xC27170, 0xC270F0)")
{
	World w;
	REQUIRE(w.load("Object P\n  Behavior = DestroyDie ModuleTag_Old\n  End\nEnd\n").empty());
	REQUIRE(w.load("ChildObject C P\n  ReplaceModule ModuleTag_Old\n    Behavior = DestroyDie ModuleTag_New\n    End\n  End\nEnd\n").empty());
	CHECK(modulesOf(w.get("C")) == M({ { "DestroyDie", "ModuleTag_New" } }));
	// not found
	CHECK(contains(w.load("ChildObject D P\n  ReplaceModule ModuleTag_Nope\n    Behavior = DestroyDie X\n    End\n  End\nEnd\n"), "ReplaceModule ModuleTag_Nope was not found for D, cannot continue."));
	// different class
	CHECK(contains(w.load("ChildObject E P\n  ReplaceModule ModuleTag_Old\n    Behavior = SlowDeathBehavior ModuleTag_New\n    End\n  End\nEnd\n"),
		"ReplaceModule must replace modules with another module of the same type, but you are attempting to replace a DestroyDie with a SlowDeathBehavior for object E."));
	// same tag
	CHECK(contains(w.load("ChildObject F P\n  ReplaceModule ModuleTag_Old\n    Behavior = DestroyDie ModuleTag_Old\n    End\n  End\nEnd\n"),
		"ReplaceModule must specify a new unique tag for the replaced module, but you are not doing so for DestroyDie (ModuleTag_Old) for object F."));
}

TEST_CASE("edit: the module-editing keywords do not nest (RW 0xC26ACC)")
{
	World w;
	REQUIRE(w.load("Object P\n  Behavior = DestroyDie ModuleTag_A\n  End\nEnd\n").empty());
	int code = 0;
	std::string err = w.load("ChildObject C P\n  AddModule\n    AddModule\n    End\n  End\nEnd\n", INI_LOAD_OVERWRITE, "obj.ini", &code);
	CHECK(contains(err, "Expected oldMode to be MODULEPARSE_NORMAL"));
	CHECK(code == 3);
	CHECK(contains(w.load("ChildObject D P\n  InheritableModule\n    RemoveModule ModuleTag_A\n  End\nEnd\n"), "Expected oldMode to be MODULEPARSE_NORMAL"));
	CHECK(contains(w.load("ChildObject E P\n  ReplaceModule ModuleTag_A\n    InheritableModule\n    End\n  End\nEnd\n"), "Expected oldMode to be MODULEPARSE_NORMAL"));
}

TEST_CASE("edit: inside AddModule the OBJECT table applies, not the audio table (RW 0x73BEC3 pushes 0xDA3DF8)")
{
	World w;
	// VoiceSelect is an audio-table field: fine at object level, an unknown field inside AddModule
	REQUIRE(w.load("Object P\n  VoiceSelect = X\nEnd\n").empty());
	const std::string err = w.load("Object Q\n  AddModule\n    VoiceSelect = X\n  End\nEnd\n");
	CHECK(contains(err, "Unknown field 'VoiceSelect'"));
	// ordinary object fields still parse inside the mode block
	REQUIRE(w.load("Object R\n  AddModule\n    BuildCost = 4\n  End\nEnd\n").empty());
	CHECK(std::get<long long>(*w.get("R")->findField("BuildCost")) == 4);
}

TEST_CASE("edit: OverrideableByLikeKind does not exist in RotWK (spec 4.7)")
{
	World w;
	CHECK(contains(w.load("Object P\n  OverrideableByLikeKind\n    Behavior = DestroyDie T\n    End\n  End\nEnd\n"), "Unknown field 'OverrideableByLikeKind'"));
}

TEST_CASE("edit: a mode that is open when an error is thrown is not restored (retail leaves the template as is)")
{
	World w;
	REQUIRE(w.load("Object P\n  Behavior = DestroyDie ModuleTag_A\n  End\nEnd\n").empty());
	CHECK(!w.load("ChildObject C P\n  ReplaceModule ModuleTag_A\n    Behavior = SlowDeathBehavior ModuleTag_B\n    End\n  End\nEnd\n").empty());
	CHECK(w.get("C")->moduleParsingMode() == MODULEPARSE_ADD_REMOVE_REPLACE);
}

// ---------------------------------------------------------------------------------------------
// overrides (map.ini / solo.ini: load type 2) and reset (spec 4.8)
// ---------------------------------------------------------------------------------------------
TEST_CASE("override: in load type 2 a module declaration must sit inside AddModule or ReplaceModule (RW 0x73F30C, 0xC271FC)")
{
	World w;
	REQUIRE(w.load(kDefault).empty());
	REQUIRE(w.load("Object Orig\n  Behavior = DestroyDie ModuleTag_Die\n  End\nEnd\n").empty());
	int code = 0;
	const std::string err = w.load("Object Orig\n  Behavior = SlowDeathBehavior ModuleTag_Slow\n  End\nEnd\n", INI_LOAD_CREATE_OVERRIDES, "map.ini", &code);
	CHECK(code == 3);
	CHECK(contains(err, "You must use AddModule to add modules in override INI files."));
	// inside AddModule it works and nothing is cleared in an override
	World w2;
	REQUIRE(w2.load(kDefault).empty());
	REQUIRE(w2.load("Object Orig\n  Behavior = DestroyDie ModuleTag_Die\n  End\nEnd\n").empty());
	REQUIRE(w2.load("Object Orig\n  AddModule\n    Behavior = SlowDeathBehavior ModuleTag_Slow\n    End\n  End\nEnd\n", INI_LOAD_CREATE_OVERRIDES, "map.ini").empty());
	const ThingTemplate *master = w2.get("Orig");
	const ThingTemplate *fin = master->getFinalOverride();
	CHECK(fin != master);
	CHECK(master->getNextOverride() == fin);
	CHECK(fin->isOverride());
	CHECK(modulesOf(master) == modulesOf(w2.get("Orig")));
	CHECK(modulesOf(master).size() == 4); // the master keeps its modules (default's body, die x2, draw...)
	bool hasSlow = false, masterHasSlow = false;
	for (const auto &m : modulesOf(fin))
	{
		hasSlow |= (m.second == "ModuleTag_Slow");
	}
	for (const auto &m : modulesOf(master))
	{
		masterHasSlow |= (m.second == "ModuleTag_Slow");
	}
	CHECK(hasSlow);
	CHECK(!masterHasSlow);
}

TEST_CASE("override: InheritableModule is refused in load type 2 (the mode byte must be ADD_REMOVE_REPLACE, RW 0x73F317)")
{
	World w;
	REQUIRE(w.load("Object Orig\n  Behavior = DestroyDie ModuleTag_Die\n  End\nEnd\n").empty());
	CHECK(contains(w.load("Object Orig\n  InheritableModule\n    Behavior = SlowDeathBehavior T\n    End\n  End\nEnd\n", INI_LOAD_CREATE_OVERRIDES, "map.ini"), "You must use AddModule"));
}

TEST_CASE("override: ReplaceModule works under load type 2")
{
	World w;
	REQUIRE(w.load("Object Orig\n  Behavior = DestroyDie ModuleTag_Die\n  End\nEnd\n").empty());
	REQUIRE(w.load("Object Orig\n  ReplaceModule ModuleTag_Die\n    Behavior = DestroyDie ModuleTag_Die2\n    End\n  End\nEnd\n", INI_LOAD_CREATE_OVERRIDES, "map.ini").empty());
	CHECK(modulesOf(w.get("Orig")->getFinalOverride()) == M({ { "DestroyDie", "ModuleTag_Die2" } }));
	CHECK(modulesOf(w.get("Orig")) == M({ { "DestroyDie", "ModuleTag_Die" } }));
}

TEST_CASE("override: a second override chains behind the first (the final override is copied, ZH Overridable.h)")
{
	World w;
	REQUIRE(w.load("Object Orig\n  BuildCost = 1\nEnd\n").empty());
	REQUIRE(w.load("Object Orig\n  BuildCost = 2\nEnd\n", INI_LOAD_CREATE_OVERRIDES, "map.ini").empty());
	REQUIRE(w.load("Object Orig\n  BountyValue = 3\nEnd\n", INI_LOAD_CREATE_OVERRIDES, "map.ini").empty());
	const ThingTemplate *master = w.get("Orig");
	const ThingTemplate *o1 = master->getNextOverride();
	REQUIRE(o1);
	const ThingTemplate *o2 = o1->getNextOverride();
	REQUIRE(o2);
	CHECK(o2->getNextOverride() == nullptr);
	CHECK(master->getFinalOverride() == o2);
	CHECK(std::get<long long>(*o2->findField("BuildCost")) == 2); // copied from the final override
	CHECK(std::get<long long>(*o2->findField("BountyValue")) == 3);
	CHECK(std::get<long long>(*master->findField("BuildCost")) == 1);
	CHECK(master->findField("BountyValue") == nullptr);
	CHECK(w.things.templateCount() == 1); // overrides are not in the master list
}

TEST_CASE("override: a template created in map.ini is an override and reset() drops it with every chain (ZH ThingFactory.cpp:209-238)")
{
	World w;
	REQUIRE(w.load("Object Keep\n  BuildCost = 1\nEnd\n").empty());
	REQUIRE(w.load("Object Keep\n  BuildCost = 2\nEnd\n", INI_LOAD_CREATE_OVERRIDES, "map.ini").empty());
	REQUIRE(w.load("Object MapOnly\n  BuildCost = 3\nEnd\n", INI_LOAD_CREATE_OVERRIDES, "map.ini").empty());
	REQUIRE(w.get("MapOnly"));
	CHECK(w.get("MapOnly")->isOverride());
	CHECK(w.things.templateCount() == 2);
	w.things.reset();
	CHECK(w.get("MapOnly") == nullptr);
	CHECK(w.things.templateCount() == 1);
	REQUIRE(w.get("Keep"));
	CHECK(w.get("Keep")->getNextOverride() == nullptr);
	CHECK(w.get("Keep")->getFinalOverride() == w.get("Keep"));
	CHECK(std::get<long long>(*w.get("Keep")->findField("BuildCost")) == 1);
	// a fresh map can define the name again
	REQUIRE(w.load("Object MapOnly\n  BuildCost = 4\nEnd\n", INI_LOAD_CREATE_OVERRIDES, "map.ini").empty());
	CHECK(w.get("MapOnly"));
}

TEST_CASE("override: copyFrom keeps the destination's Overridable fields, because RW's Overridable assignment copies nothing (RW 0x92BB7C, 0x7405B1)")
{
	World w;
	REQUIRE(w.load("Object P\n  BuildCost = 1\nEnd\n").empty());
	REQUIRE(w.load("Object P\n  BuildCost = 2\nEnd\n", INI_LOAD_CREATE_OVERRIDES, "map.ini").empty());
	REQUIRE(w.get("P")->getNextOverride());
	// a ChildObject of an overridden parent copies the MASTER's values and does not inherit its override chain
	REQUIRE(w.load("ChildObject C P\nEnd\n").empty());
	CHECK(w.get("C")->getNextOverride() == nullptr);
	CHECK(!w.get("C")->isOverride());
	CHECK(std::get<long long>(*w.get("C")->findField("BuildCost")) == 1);
	// a ChildObject declared in map.ini is created as an override (parseObjectDefinition marks it BEFORE the copy) and stays one:
	// ZH's implicit assignment would have overwritten the flag with the parent's false, leaving it behind after reset()
	REQUIRE(w.load("ChildObject MapKid P\n  BountyValue = 5\nEnd\n", INI_LOAD_CREATE_OVERRIDES, "map.ini").empty());
	REQUIRE(w.get("MapKid"));
	CHECK(w.get("MapKid")->isOverride());
	w.things.reset();
	CHECK(w.get("MapKid") == nullptr);
	CHECK(w.get("C") != nullptr);
}

TEST_CASE("override: macros are banned in map.ini (spec 0.1 fact 9, 2.2)")
{
	World w;
	const std::string err = w.load("#define X 5\nObject Foo\nEnd\n", INI_LOAD_CREATE_OVERRIDES, "map.ini");
	CHECK(!err.empty());
	CHECK(w.get("Foo") == nullptr);
}

// ---------------------------------------------------------------------------------------------
// nested object-level blocks (stop S-071): exact extents, template-level clearing
// ---------------------------------------------------------------------------------------------
TEST_CASE("blocks: ArmorSet / WeaponSet (typed) / LocomotorSet / Prerequisites are consumed to their End, all four also kept raw")
{
	World w;
	CHECK(w.load(
		"Object Foo\n"
		"  ArmorSet\n    Conditions = None\n    Armor = HeroArmor\n    DamageFX = SomeFX\n  End\n"
		"  WeaponSet\n    Conditions = None\n    Weapon = PRIMARY Sword\n  End\n"
		"  LocomotorSet\n    Locomotor = Walk\n    Condition = NORMAL\n    Speed = 30\n  End\n"
		"  Prerequisites\n    Object = A B\n    Science = X\n  End\n"
		"  BuildCost = 11\n"
		"End\n").empty());
	const ThingTemplate *t = w.get("Foo");
	CHECK(std::get<long long>(*t->findField("BuildCost")) == 11);
	REQUIRE(t->rawBlocks().size() == 4);
	CHECK(t->rawBlocks()[0].field == "ArmorSet");
	CHECK(t->rawBlocks()[0].lines.size() == 5); // header + 3 fields + End
	CHECK(t->rawBlocks()[0].lines.front().text.find("ArmorSet") != std::string::npos);
	CHECK(t->rawBlocks()[0].lines.front().sourceLine == 2);
	CHECK(t->rawBlocks()[0].lines.back().sourceLine == 6);
	CHECK(t->rawBlocks()[1].field == "WeaponSet");
	CHECK(t->rawBlocks()[2].field == "LocomotorSet");
	CHECK(t->rawBlocks()[3].field == "Prerequisites");
	CHECK(w.things.rawFieldCounts().at("ArmorSet") == 1);
	// WEAPON-1: the typed WeaponSet / ArmorSet blocks (a null TheWeaponStore reports the weapon name as unresolved)
	REQUIRE(t->armorTemplateSets().size() == 1);
	CHECK(t->armorTemplateSets()[0].m_flags == 0u);
	CHECK(t->armorTemplateSets()[0].m_armorName == "HeroArmor");
	CHECK(t->armorTemplateSets()[0].m_damageFXName == "SomeFX");
	REQUIRE(t->weaponTemplateSets().size() == 1);
	CHECK(t->weaponTemplateSets()[0].unresolvedWeapons == std::vector<std::string>{ "Sword" });
}

TEST_CASE("blocks: an unknown field inside a nested block is an error from the block's own table")
{
	World w;
	CHECK(contains(w.load("Object Foo\n  ArmorSet\n    Bogus = 1\n  End\nEnd\n"), "Unknown field 'Bogus'"));
	World w2;
	CHECK(contains(w2.load("Object Foo\n  WeaponSet\n    Armor = X\n  End\nEnd\n"), "Unknown field 'Armor'"));
}

TEST_CASE("blocks: ArmorSet and WeaponSet clear the copied sets once, then accumulate (RW 0x73FAC4, 0x73ED19)")
{
	World w;
	REQUIRE(w.load(kDefault).empty());
	// the default's ArmorSet is copied; the first ArmorSet of the new object clears it, the second appends
	REQUIRE(w.load("Object Foo\n  ArmorSet\n    Conditions = VETERAN\n  End\n  ArmorSet\n    Conditions = ELITE\n  End\nEnd\n").empty());
	const ThingTemplate *t = w.get("Foo");
	REQUIRE(t->rawBlocks().size() == 2);
	CHECK(t->rawBlocks()[0].lines[1].text.find("VETERAN") != std::string::npos);
	REQUIRE(t->armorTemplateSets().size() == 2); // WEAPON-1: the typed sets: the copied one was cleared, both new ones accumulate
	CHECK(t->armorTemplateSets()[0].m_flags == 1u);
	CHECK(t->armorTemplateSets()[1].m_flags == 2u);
	CHECK(!t->armorCopiedFromDefault());
	CHECK(t->weaponsCopiedFromDefault()); // no WeaponSet declared: still the copied (empty) state
	// no ArmorSet declared: the default's is inherited
	REQUIRE(w.load("Object Bar\nEnd\n").empty());
	CHECK(w.get("Bar")->rawBlocks().size() == 1);
	CHECK(w.get("Bar")->armorCopiedFromDefault());
}

TEST_CASE("blocks: a ChildObject's first ArmorSet clears the parent's too (the flag is set by setCopiedFromDefault; RW 0x73FAC4 has no load type test)")
{
	World w;
	REQUIRE(w.load("Object P\n  ArmorSet\n    Conditions = HERO\n  End\nEnd\n").empty());
	REQUIRE(w.load("ChildObject C P\n  ArmorSet\n    Conditions = PLAYER_UPGRADE\n  End\nEnd\n").empty());
	REQUIRE(w.get("C")->rawBlocks().size() == 1);
	CHECK(w.get("C")->rawBlocks()[0].lines[1].text.find("PLAYER_UPGRADE") != std::string::npos);
	REQUIRE(w.get("C")->armorTemplateSets().size() == 1);
	CHECK(w.get("C")->armorTemplateSets()[0].m_flags == 8u);
	REQUIRE(w.load("ChildObject D P\nEnd\n").empty());
	REQUIRE(w.get("D")->rawBlocks().size() == 1);
	CHECK(w.get("D")->rawBlocks()[0].lines[1].text.find("HERO") != std::string::npos);
	REQUIRE(w.get("D")->armorTemplateSets().size() == 1);
	CHECK(w.get("D")->armorTemplateSets()[0].m_flags == 4u);
}

TEST_CASE("blocks: UnitSpecificSounds / UnitSpecificFX take any field name and replace the earlier block (RW 0x73F684 clears the map)")
{
	World w;
	REQUIRE(w.load("Object Foo\n  UnitSpecificSounds\n    TankTurretMove = Snd1\n    Anything = Snd2\n  End\n  UnitSpecificSounds\n    Other = Snd3\n  End\n  UnitSpecificFX\n    HitFX = FX_A\n  End\nEnd\n").empty());
	const ThingTemplate *t = w.get("Foo");
	REQUIRE(t->rawBlocks().size() == 2);
	CHECK(t->rawBlocks()[0].field == "UnitSpecificSounds");
	CHECK(t->rawBlocks()[0].lines.size() == 3); // header, Other, End
	CHECK(t->rawBlocks()[1].field == "UnitSpecificFX");
}

TEST_CASE("blocks: Prerequisites accumulates and a load type 2 declaration clears first (RW 0x74057B)")
{
	World w;
	REQUIRE(w.load("Object Foo\n  Prerequisites\n    Object = A\n  End\n  Prerequisites\n    Object = B\n  End\nEnd\n").empty());
	CHECK(w.get("Foo")->rawBlocks().size() == 2);
	REQUIRE(w.load("Object Foo\n  Prerequisites\n    Object = C\n  End\nEnd\n", INI_LOAD_CREATE_OVERRIDES, "map.ini").empty());
	const ThingTemplate *fin = w.get("Foo")->getFinalOverride();
	REQUIRE(fin->rawBlocks().size() == 1);
	CHECK(fin->rawBlocks()[0].lines[1].text.find("C") != std::string::npos);
	CHECK(w.get("Foo")->rawBlocks().size() == 2);
}

TEST_CASE("blocks: the lazily built Flammability table and the stack-built FormationPreviewDecal table are real tables")
{
	World w;
	CHECK(w.load("Object Foo\n  Flammability\n    Fuel = 10\n    FuelFactor = 1\n    MaxBurnRate = 2\n    Decay = 3\n    Resistance = 4\n  End\nEnd\n").empty());
	CHECK(contains(w.load("Object Bar\n  Flammability\n    Nope = 10\n  End\nEnd\n"), "Unknown field 'Nope'"));
	CHECK(w.load("Object Baz\n  FormationPreviewDecal\n    Texture = X\n    Width = 1\n    Height = 2\n  End\nEnd\n").empty());
}

TEST_CASE("blocks: unterminated retail tables stop at their last row (ThreatBreakdown has one field)")
{
	World w;
	CHECK(w.load("Object Foo\n  ThreatBreakdown\n    AIKindOf = INFANTRY\n  End\nEnd\n").empty());
	CHECK(contains(w.load("Object Bar\n  ThreatBreakdown\n    Other = 1\n  End\nEnd\n"), "Unknown field 'Other'"));
}

TEST_CASE("blocks: a dispatching block selects its table from the first token (MeleeBehavior, RW 0x86C30A: Swarm, WaitForLeader, HoldGround, Amoeba)")
{
	World w;
	// Amoeba: nine fields, all optional, and an empty block is fine
	CHECK(w.load("Object A\n  Behavior = HordeContain T\n    MeleeBehavior = Amoeba\n      FacingBonus = 30.0\n      OuterRange = 80\n      DelayUntilIdle = 100\n    End\n  End\nEnd\n").empty());
	CHECK(w.load("Object B\n  Behavior = HordeContain T\n    MeleeBehavior = Amoeba\n    End\n  End\nEnd\n").empty());
	// Swarm and HoldGround have no fields at all (their build proc adds nothing, RW 0x9F3A3C)
	CHECK(w.load("Object C\n  Behavior = HordeContain T\n    MeleeBehavior = Swarm\n    End\n    MeleeBehavior = HoldGround\n    End\n  End\nEnd\n").empty());
	CHECK(contains(w.load("Object D\n  Behavior = HordeContain T\n    MeleeBehavior = Swarm\n      FacingBonus = 1\n    End\n  End\nEnd\n"), "Unknown field 'FacingBonus'"));
	// WaitForLeader has its own three
	CHECK(w.load("Object E\n  Behavior = HordeContain T\n    MeleeBehavior = WaitForLeader\n      FollowLeader = Yes\n      DistanceToActiveLeader = 3\n    End\n  End\nEnd\n").empty());
	CHECK(contains(w.load("Object F\n  Behavior = HordeContain T\n    MeleeBehavior = WaitForLeader\n      FacingBonus = 1\n    End\n  End\nEnd\n"), "Unknown field 'FacingBonus'"));
	// the token is looked up case-insensitively in the index list (scanIndexList); an unknown one is an error from the index list
	CHECK(w.load("Object G\n  Behavior = HordeContain T\n    MeleeBehavior = amoeba\n    End\n  End\nEnd\n").empty());
	CHECK(contains(w.load("Object H\n  Behavior = HordeContain T\n    MeleeBehavior = Bogus\n    End\n  End\nEnd\n"), "Token 'Bogus' is not a valid member of the index list"));
	CHECK(contains(w.load("Object I\n  Behavior = HordeContain T\n    MeleeBehavior\n    End\n  End\nEnd\n"), "Expected additional data"));
}

TEST_CASE("blocks: AddEmotion opens a block only after OVERRIDE (RW 0x8B618C)")
{
	World w;
	CHECK(w.load(
		"Object A\n"
		"  Behavior = EmotionTrackerUpdate T\n"
		"    AddEmotion = Terror_Base\n"
		"    AddEmotion = OVERRIDE Taunt_Base\n"
		"      Duration = 1000\n"
		"      AttributeModifier = SomeModifier\n"
		"    End\n"
		"    AddEmotion = Point_Base\n"
		"  End\n"
		"  BuildCost = 3\n"
		"End\n").empty());
	CHECK(std::get<long long>(*w.get("A")->findField("BuildCost")) == 3);
	// the keyword matches case-insensitively, and the block's fields come from the emotion table
	CHECK(w.load("Object B\n  Behavior = EmotionTrackerUpdate T\n    AddEmotion = override X\n      Type = Foo\n    End\n  End\nEnd\n").empty());
	CHECK(contains(w.load("Object C\n  Behavior = EmotionTrackerUpdate T\n    AddEmotion = OVERRIDE X\n      Nope = 1\n    End\n  End\nEnd\n"), "Unknown field 'Nope'"));
	// without OVERRIDE the following line is a field of the module, not of a block
	CHECK(contains(w.load("Object D\n  Behavior = EmotionTrackerUpdate T\n    AddEmotion = X\n      Duration = 1000\n    End\n  End\nEnd\n"), "Unknown field 'Duration'"));
}

TEST_CASE("blocks: Radius / Opacity / Angle are a plain number or a keyframe block, decided by the first token (RW 0x73B723, sscanf %f)")
{
	World w;
	CHECK(w.load("Object A\n  Behavior = PartTheHeavensUpdate T\n    Radius = 5.0\n    Opacity = 0.5\n    Angle = 90\n  End\n  BuildCost = 1\nEnd\n").empty());
	CHECK(w.load("Object B\n  Behavior = PartTheHeavensUpdate T\n    Radius = Keys\n      InPadding = 1\n      Key = 0 5\n    End\n  End\nEnd\n").empty());
	CHECK(contains(w.load("Object C\n  Behavior = PartTheHeavensUpdate T\n    Radius = Keys\n      Bogus = 1\n    End\n  End\nEnd\n"), "Unknown field 'Bogus'"));
	// a number that scans as a float opens nothing, so a body line after it is a field of the module
	CHECK(contains(w.load("Object D\n  Behavior = PartTheHeavensUpdate T\n    Radius = 5.0\n      InPadding = 1\n    End\n  End\nEnd\n"), "Unknown field 'InPadding'"));
}

// ---------------------------------------------------------------------------------------------
// the module factory
// ---------------------------------------------------------------------------------------------
TEST_CASE("factory: init registers the binary's whole registry, 329 classes, all raw (RW 0x657A03-0x65BC64, stop S-070)")
{
	World w;
	CHECK(w.modules.classCount() == 329);
	CHECK(w.modules.typedCount() == 0);
	CHECK(w.modules.rawClassNames().size() == 329);
	CHECK(RwBinaryData::embedded().addModuleSites == 330); // WeaponBonusUpgrade registers twice with identical arguments
	// a class retail data never uses is registered anyway (PLAN rule 6)
	CHECK(w.modules.findModuleInterfaceMask("WargBehavior", MODULETYPE_BEHAVIOR) == 0);
	CHECK(w.modules.findModuleTemplate("WargBehavior", MODULETYPE_BEHAVIOR) != nullptr);
	CHECK(w.modules.findModuleTemplate("WallHubBehavior", MODULETYPE_BEHAVIOR) != nullptr);
}

TEST_CASE("factory: interface masks come from the registry (RW addModule sites; spec 4.4, 5.1)")
{
	World w;
	CHECK(w.modules.findModuleInterfaceMask("ActiveBody", MODULETYPE_BEHAVIOR) == MODULEINTERFACE_BODY);
	CHECK(w.modules.findModuleInterfaceMask("W3DScriptedModelDraw", MODULETYPE_DRAW) == MODULEINTERFACE_DRAW);
	CHECK(w.modules.findModuleInterfaceMask("WallHubBehavior", MODULETYPE_BEHAVIOR) == MODULEINTERFACE_UPDATE);
	CHECK(w.modules.findModuleInterfaceMask("CastleMemberBehavior", MODULETYPE_BEHAVIOR) == MODULEINTERFACE_DAMAGE);
	CHECK(w.modules.findModuleInterfaceMask("TerrainResourceClientBehavior", MODULETYPE_CLIENT_BEHAVIOR) == MODULEINTERFACE_CLIENT_BEHAVIOR);
	CHECK(w.modules.findModuleInterfaceMask("PillageModule", MODULETYPE_BEHAVIOR) == 0x2000);
	// unknown or empty: 0, not an error (RW 0x656C3D, 0x656C5E)
	CHECK(w.modules.findModuleInterfaceMask("", MODULETYPE_BEHAVIOR) == 0);
	CHECK(w.modules.findModuleInterfaceMask("Nope", MODULETYPE_BEHAVIOR) == 0);
	CHECK(w.modules.findModuleInterfaceMask("ActiveBody", MODULETYPE_DRAW) == 0); // wrong type: another decorated key
	CHECK(w.modules.findModuleInterfaceMask("activebody", MODULETYPE_BEHAVIOR) == 0);
}

TEST_CASE("factory: the decorated key is the NAMEKEY of '0'+type+name (RW 0x655A0C)")
{
	World w;
	CHECK(w.modules.makeDecoratedNameKey("ActiveBody", MODULETYPE_BEHAVIOR) == w.keys.findKey("0ActiveBody"));
	CHECK(w.modules.makeDecoratedNameKey("W3DDefaultDraw", MODULETYPE_DRAW) == w.keys.findKey("1W3DDefaultDraw"));
	CHECK(w.keys.findKey("3TerrainResourceClientBehavior") != NAMEKEY_INVALID);
	CHECK(w.keys.findKey("0W3DDefaultDraw") == NAMEKEY_INVALID);
}

TEST_CASE("factory: registration replays the binary's execution order, so name keys match retail's (RW 0x464AD2 runs 0x6579C9 first)")
{
	World w;
	const RwBinaryData &d = RwBinaryData::embedded();
	REQUIRE(d.registrationSequence.size() == 330);
	CHECK(d.registrationSequence.front() == std::make_pair(std::string("AutoHealBehavior"), 0));
	// fresh generator after init(): ids start at 1, so the first registered class owns key 1
	CHECK(w.keys.findKey("0AutoHealBehavior") == 1);
	CHECK(w.keys.findKey("0WallHubBehavior") == 2);
	// alphabetical order (the golden's class order) would have given key 1 to AIGateUpdate and key 24 to AutoHealBehavior
	CHECK(w.keys.findKey("0AIGateUpdate") != 1);
	// the 310 non-draw registrations (one of them WeaponBonusUpgrade twice) come first, then the 20 draw classes
	CHECK(w.keys.findKey("1W3DDefaultDraw") == 310);
	// every key follows the sequence: a repeated registration reuses its key, so 329 keys in all
	std::map<std::string, NameKeyType> expected;
	NameKeyType next = 1;
	for (const auto &step : d.registrationSequence)
	{
		const std::string key = std::string(1, (char)('0' + step.second)) + step.first;
		if (!expected.count(key))
		{
			expected[key] = next++;
		}
	}
	CHECK(next == 330);
	for (const auto &kv : expected)
	{
		CHECK_MESSAGE(w.keys.findKey(kv.first) == kv.second, kv.first);
	}
	CHECK(w.keys.nextId() == 330);
}

TEST_CASE("factory: raw grammar tables use zero storage offsets, so INI never computes an out-of-bounds pointer (retail offsets stay in the golden)")
{
	World w;
	const RwBinaryData &d = RwBinaryData::embedded();
	size_t nonZeroGolden = 0;
	for (const RwClass &cls : d.classes)
	{
		for (const RwTableRef &t : cls.tables)
		{
			nonZeroGolden += t.extra != 0;
		}
		MultiIniFieldParse p;
		w.grammar.buildClassFieldParse(cls, p);
		for (int i = 0; i < p.getCount(); ++i)
		{
			CHECK_MESSAGE(p.getNthExtraOffset(i) == 0, cls.name);
			for (const FieldParse *row = p.getNthFieldParse(i); row->token; ++row)
			{
				CHECK_MESSAGE(row->offset == 0, cls.name);
			}
		}
	}
	// the golden still records the retail extra offsets (e.g. the die-mux table at +8, AudioLoopUpgrade's audio table at +324)
	CHECK(nonZeroGolden > 0);
	size_t max = 0;
	for (const RwClass &cls : d.classes)
	{
		if (cls.name == "AudioLoopUpgrade")
		{
			for (const RwTableRef &t : cls.tables)
			{
				max = std::max<size_t>(max, t.extra);
			}
		}
	}
	CHECK(max == 324);
	// a body that really reaches such a table parses without touching memory outside the data object
	CHECK(w.load("Object Foo\n  Behavior = AudioLoopUpgrade T\n  End\nEnd\n").empty());
}

namespace
{
// a typed data class plugged into the factory the ZH way
struct TestWallData : public ModuleData
{
	int blastRadius = -1;
	static void buildFieldParse(MultiIniFieldParse &p)
	{
		static const FieldParse table[] = {
			{ "BlastRadius", INI::parseInt, nullptr, (int)offsetof(TestWallData, blastRadius) },
			{ nullptr, nullptr, nullptr, 0 },
		};
		p.add(table, 0);
	}
};
}

TEST_CASE("factory: a typed data class can be bound per class and replaces the raw one (the hook other lanes use)")
{
	World w;
	w.modules.bindTypedData<TestWallData>("WallHubBehavior", MODULETYPE_BEHAVIOR);
	CHECK(w.modules.typedCount() == 1);
	CHECK(w.modules.rawClassNames().size() == 328);
	REQUIRE(w.load("Object Foo\n  Behavior = WallHubBehavior ModuleTag_Wall\n    BlastRadius = 42\n  End\nEnd\n").empty());
	const auto &nugget = w.get("Foo")->behaviorModules().nuggets()[0];
	const TestWallData *typed = dynamic_cast<const TestWallData *>(nugget.data.get());
	REQUIRE(typed);
	CHECK(typed->blastRadius == 42);
	CHECK(nugget.interfaceMask == MODULEINTERFACE_UPDATE); // the registry's mask still applies
	// its fields are now the typed class's: an unknown field is an error from that table
	CHECK(contains(w.load("Object Bar\n  Behavior = WallHubBehavior T\n    Nope = 1\n  End\nEnd\n"), "Unknown field 'Nope'"));
	// binding a class the binary does not register is a programming error
	CHECK_THROWS_AS(w.modules.bindTypedData<TestWallData>("NoSuchClass", MODULETYPE_BEHAVIOR), std::logic_error);
	CHECK_THROWS_AS(w.modules.bindTypedData<TestWallData>("WallHubBehavior", MODULETYPE_DRAW), std::logic_error);
	// the S-070 report counts only the raw ones
	bool found = false;
	for (const std::string &s : w.modules.acceptanceStops())
	{
		if (s.rfind("S-070: 328 of 329", 0) == 0)
		{
			found = true;
		}
	}
	CHECK(found);
}

TEST_CASE("factory: typed data keeps the registry's AI and slot 7 predicates without overriding them")
{
	World w;
	w.modules.bindTypedData<TestWallData>("HordeAIUpdate", MODULETYPE_BEHAVIOR);
	REQUIRE(w.load("Object P\n  Behavior = AIUpdateInterface ModuleTag_AI\n  End\nEnd\n").empty());
	REQUIRE(w.load("ChildObject C P\n  Behavior = HordeAIUpdate ModuleTag_AI2\n    BlastRadius = 1\n  End\nEnd\n").empty());
	CHECK(modulesOf(w.get("C")) == M({ { "HordeAIUpdate", "ModuleTag_AI2" } })); // the typed class still clears the parent's AI module
}

TEST_CASE("factory: every module data made is kept by the factory (RW +0x1C list)")
{
	World w;
	REQUIRE(w.load("Object Foo\n  Behavior = DestroyDie A\n  End\n  Draw = W3DDefaultDraw B\n  End\nEnd\n").empty());
	CHECK(w.modules.moduleDataCount() == 2);
}

// ---------------------------------------------------------------------------------------------
// the stops
// ---------------------------------------------------------------------------------------------
TEST_CASE("stops: the object model reports every unported item with its S-0xx id")
{
	World w;
	std::set<std::string> ids;
	for (const std::string &s : w.things.acceptanceStops())
	{
		ids.insert(s.substr(0, 5));
	}
	CHECK(ids == std::set<std::string>{ "S-070", "S-071", "S-072", "S-073", "S-074", "S-075", "S-076", "S-077" });
	const std::vector<std::string> stops = w.things.acceptanceStops();
	CHECK(stops[0].find("S-070: 329 of 329 registered module classes keep their body as RawModuleData") == 0);
}

TEST_CASE("stops: the report is pinned line by line (docs/STOPS.md S-070 .. S-077)")
{
	World w;
	const std::vector<std::string> expected = {
		"S-070: 329 of 329 registered module classes keep their body as RawModuleData (field values not parsed; only the body extent and field names are checked)",
		"S-073: the name of the ModuleData vtable slot 7 predicate (ChildObject body replacement) and the meaning of interface mask 0x2000 (PillageModule) are unverified",
		"S-071: 10 object-level nested blocks (LocomotorSet, Prerequisites, UnitSpecificSounds/FX, AutoResolve*, ...) are kept as raw lines",
		"S-072: 101 object-table rows have parse functions with no typed port and are kept as raw tokens; field defaults of a fresh template are not ported (unset fields read as absent)",
		"S-074: nested-block extents come from static analysis of the binary (238 parse functions, 2 conditional, 1 dispatching, 3 null-guarded, reviewed by hand); an indirect-call block opener would read as a line",
		"S-075: a module declaration naming a class the binary does not register crashes RotWK (NULL dereference at RW 0x73F496); this port raises an INI error",
		"S-076: a BeginScript block without ENDSCRIPT never ends in RotWK (RW 0x42D400 has no end-of-file test); this port raises an INI error",
		"S-077: ThingTemplate::validate (RW 0x73CE7E: default shadow strings and audio event strings) is not ported",
	};
	CHECK(w.things.acceptanceStops() == expected);
}

TEST_CASE("stops: the raw object-table rows are exactly the ones without a typed port")
{
	World w;
	const std::vector<std::string> raw = w.things.rawFieldRowNames();
	std::set<std::string> rawSet(raw.begin(), raw.end());
	CHECK(rawSet.count("KindOf"));
	CHECK(rawSet.count("VoiceSelect"));
	CHECK(rawSet.count("Geometry"));
	CHECK(rawSet.count("ArmorSet"));
	CHECK(!rawSet.count("BuildCost"));
	CHECK(!rawSet.count("Behavior"));
	CHECK(!rawSet.count("AddModule"));
	CHECK(w.things.fieldSlotCount() == 191 + 56);
}
