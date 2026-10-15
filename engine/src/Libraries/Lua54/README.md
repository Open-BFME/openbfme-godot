# Lua 5.4 companion runtime

Pristine upstream source: https://www.lua.org/ftp/lua-5.4.9.tar.gz

SHA-256: `2335b6c582a52654f94612bf10d2f4672805d05329aa6568b1d8cd9e5c6fb8e6`

Upstream copyright and MIT license are in `engine/thirdparty/lua-5.4.9/doc/readme.html` and source headers.
No Second Age implementation code is included; its public documentation informs the compatibility API.

`prefix.h` renames Lua 5.4 identifiers at compilation so its public and internal symbols cannot bind to the existing EA Lua 4 VM.
Both the library and its C++ adapter include the prefix before Lua headers. Do not include either Lua ABI's headers in the other's translation unit.
The companion is console-driven and does not replace the deterministic retail VM. Its mutations are not sent over the synchronized command stream.

Regenerate the prefix header after an upstream update with `python3 tools/lua54_prefix.py`.
