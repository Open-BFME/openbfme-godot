#!/usr/bin/env python3
"""Run lua-gdextension's own C++ code generator (tools/code_generation/generate_cpp_code.py of the vendored tree) for OpenBFME's build.

Upstream reads lib/godot-cpp/gdextension/extension_api.json of its own checkout and writes into its src/generated; this build
uses OpenBFME's godot-cpp and its Godot API version, and writes the generated headers into the build folder.

usage: luagd_generate.py <lua-gdextension tree> <extension_api.json> <output folder>"""
import importlib.util
import os
import sys

tree, api_json, dest = (os.path.abspath(a) for a in sys.argv[1:4])
spec = importlib.util.spec_from_file_location("generate_cpp_code", os.path.join(tree, "tools", "code_generation", "generate_cpp_code.py"))
gen = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gen)
gen.SRC_DIR = os.path.join(tree, "src")
gen.DEST_DIR = dest
gen.API_JSON_PATH = api_json
gen.PACKAGE_SEARCHER_SRC = os.path.join(gen.SRC_DIR, "luaopen", "package_searcher.lua")
gen.LUA_SCRIPT_GLOBALS_SRC = os.path.join(gen.SRC_DIR, "script-language", "globals.lua")
gen.main()
