# lua-gdextension: altered source versions

lua-gdextension 0.8.2 (https://github.com/gilzoide/lua-gdextension, tag `0.8.2`, commit `776b9fb0984ae86c1c324e9de3ec71dad0143806`,
MIT, Copyright (C) 2026 Gil Barbosa Reis) is vendored unedited in `engine/thirdparty/lua-gdextension-0.8.2/` (`src/`, `doc_classes/`,
`tools/code_generation/`, `LICENSE`, `README.md`, `CHANGELOG.md`). Its dependencies at the commits 0.8.2 pins: `engine/thirdparty/sol2`
(`ab874eb0e8ef8aea4c10074a89efa25f62a29d9a` of https://github.com/Assumeru/sol2, `include/` and `LICENSE.txt`),
`engine/thirdparty/tree-sitter` (`0bb43f7afb5f0d83579007037a13d1fede636dbd`, `lib/` and `LICENSE`), `engine/thirdparty/tree-sitter-lua`
(`d76023017f7485eae629cb60d406c7a1ca0f40c9` of https://github.com/tree-sitter-grammars/tree-sitter-lua, `src/` and `LICENSE.md`) and
LuaJIT (`engine/src/Libraries/LuaJIT/README.md`).

`cmake/LuaGDExtension.cmake` builds it into the openbfme GDExtension from a staging folder: the pristine `src/`, then the files of
this folder's `src/` copied over them by path. Each altered file starts with `ALTERED SOURCE VERSION` and says why. The changes
(the godot-cpp ones: upstream 0.8.2 is written against godot-cpp's 4.6 branch, and in godot-cpp 10 / Godot 4.7 `memnew` of a
`RefCounted` class gives a `Ref<T>`):

- `src/LuaObject.hpp`: Lua object references are anchored to the main thread rather than the temporary coroutine that created them; callbacks and tables remain valid after the coroutine is collected. Light userdata retains its upstream representation.
- `src/LuaCoroutine.hpp`, `src/LuaCoroutine.cpp`: `LuaCoroutine::create` returns `Ref<LuaCoroutine>` (and its binding).
- `src/script-language/LuaScriptMethod.cpp` (not godot-cpp: LuaJIT): a script method's info says it returns a Variant, and, when its
  parameter list is unknown (LuaJIT reports Lua 5.1, whose debug API has no parameter count; release builds), that it takes any
  arguments. Upstream's empty, non-vararg list makes GDScript's analyzer refuse every call with arguments (`lua_console.lua`).
- `src/luaopen/variant.cpp` (not godot-cpp: an upstream binding bug): `Variant:call` / `Variant:pcall` (`node:call("set_name", "x")`,
  `vector:call("length")`) were bound with a `const VariantArguments&` parameter, which sol2 cannot get from the stack: it read a
  usertype that is not there and crashed (sol2's safety checks, which would report it, are off in Release builds). They now take
  `sol::variadic_args`. On a Callable, `call` is `Callable.call` (`cb:call(41)`), which upstream's helper shadowed.
- `src/script-language/LuaScriptLanguage.cpp`: `_create_script` (a deprecated virtual that returns `Object *`, editor only) keeps
  the new script's reference for the caller.

Not built: upstream's `src/main.cpp` (its entry point; `engine/src/GodotDevice/LuaGDExtensionRegister.cpp` registers the same
classes from the openbfme extension) and the editor documentation data. The generated headers come from upstream's own generator,
run against this build's godot-cpp API by `tools/luagd_generate.py`.
