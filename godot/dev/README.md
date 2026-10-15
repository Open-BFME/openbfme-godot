# Reloadable console scripts

Put `.lua` entry files directly in this directory, then press **Ctrl+Shift+F5**,
click **Reload scripts**, or run `console.reload()` (`bfme.reloadScripts()` also works).
All files are reread from disk on every reload, in filename order. No restart is required.

Files run in the persistent console VM. Define globals/functions here and call them from
the console; functions with the same names are replaced when reloaded. Existing unrelated
globals remain. Entry-script return values are ignored; automatic event callbacks are not provided.

The included helper provides `setGold(10000)` after reloading. It returns a mutation ticket.

Exported games also read `.lua` files in `user://dev` after the bundled scripts.
On macOS that is `~/Library/Application Support/Godot/app_userdata/OpenBFME/dev/`.
Use that directory for editable scripts in a packaged game. `require()` still only loads the built-in `bfme` module.

All entries are compiled before any run. A syntax error prevents the reload; a runtime
error stops at that file, after any earlier files have already run. Edits are not transactional.
