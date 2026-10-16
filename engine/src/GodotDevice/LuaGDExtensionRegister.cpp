// OpenBFME. GPL-3.0. Lane LUAGD-1: lua-gdextension's classes, registered by the openbfme extension (cmake/LuaGDExtension.cmake).
// Port of upstream's src/main.cpp (engine/thirdparty/lua-gdextension-0.8.2/src/main.cpp, MIT, Gil Barbosa Reis): the same
// registration at the scene level, without its own entry point (the addon is compiled into this extension).
#include "GodotDevice/LuaGDExtensionRegister.h"

#include "LuaAST.hpp"
#include "LuaASTNode.hpp"
#include "LuaASTQuery.hpp"
#include "LuaCoroutine.hpp"
#include "LuaDebug.hpp"
#include "LuaError.hpp"
#include "LuaFunction.hpp"
#include "LuaLightUserdata.hpp"
#include "LuaObject.hpp"
#include "LuaParser.hpp"
#include "LuaState.hpp"
#include "LuaTable.hpp"
#include "LuaThread.hpp"
#include "LuaUserdata.hpp"
#include "script-language/LuaCodeEdit.hpp"
#include "script-language/LuaScript.hpp"
#include "script-language/LuaScriptImportBehaviorManager.hpp"
#include "script-language/LuaScriptLanguage.hpp"
#include "script-language/LuaScriptResourceFormatLoader.hpp"
#include "script-language/LuaScriptResourceFormatSaver.hpp"
#include "script-language/LuaSyntaxHighlighter.hpp"
#include "utils/project_settings.hpp"
#include "utils/string_names.hpp"

#include <godot_cpp/core/class_db.hpp>

using namespace godot;
using namespace luagdextension;

void LuaGDExtension::initialize()
{
	string_names = memnew(struct string_names());

	// Lua object wrappers
	ClassDB::register_abstract_class<LuaObject>();
	ClassDB::register_abstract_class<LuaThread>();
	ClassDB::register_abstract_class<LuaCoroutine>();
	ClassDB::register_abstract_class<LuaFunction>();
	ClassDB::register_abstract_class<LuaLightUserdata>();
	ClassDB::register_abstract_class<LuaTable>();
	ClassDB::register_abstract_class<LuaUserdata>();

	// Godot classes for interacting with Lua States
	ClassDB::register_abstract_class<LuaDebug>();
	ClassDB::register_class<LuaError>();
	ClassDB::register_class<LuaState>();

	// Parser
	ClassDB::register_abstract_class<LuaASTNode>();
	ClassDB::register_abstract_class<LuaAST>();
	ClassDB::register_class<LuaASTQuery>();
	ClassDB::register_class<LuaParser>();
	LuaParser::setup_tree_sitter_allocator();

	// Lua Script Language
	register_project_settings();
	ClassDB::register_abstract_class<LuaScript>();
	ClassDB::register_abstract_class<LuaScriptLanguage>();
	ClassDB::register_abstract_class<LuaScriptResourceFormatLoader>();
	ClassDB::register_abstract_class<LuaScriptResourceFormatSaver>();
	LuaScriptImportBehaviorManager::get_or_create_singleton();
	LuaScriptLanguage::get_or_create_singleton();
	LuaScriptResourceFormatLoader::register_in_godot();
	LuaScriptResourceFormatSaver::register_in_godot();

	// Lua code editing
	ClassDB::register_class<LuaCodeEdit>();
	ClassDB::register_class<LuaSyntaxHighlighter>();
}

void LuaGDExtension::uninitialize()
{
	LuaScriptResourceFormatSaver::unregister_in_godot();
	LuaScriptResourceFormatLoader::unregister_in_godot();
	LuaScriptLanguage::delete_singleton();
	LuaScriptImportBehaviorManager::delete_singleton();
	memdelete(string_names);
}

godot::Ref<godot::RefCounted> LuaGDExtension::newBfmeState(void **luaState)
{
	Ref<LuaState> state = memnew(LuaState);
	state->open_libraries(LuaState::GODOT_VARIANT | LuaState::GODOT_ENUMS);
	*luaState = state->get_lua_state().lua_state();
	return state;
}
