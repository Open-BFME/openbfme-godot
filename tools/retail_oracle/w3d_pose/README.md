# W3D pose vectors from the retail binary

Expected values of `engine/tests/test_w3d_pose.cpp` that come from BFME2 1.06's own code, not from the engine:

| script | what it produces | how |
|---|---|---|
| `oracle_nlerp.py` | `BFME2_Nlerp is bit-identical ...` rows | calls the real function 0xB17550 through `tools/retail_oracle` |
| `oracle_base.py` | `Base_Update is bit-identical ...` rows | calls the real `Base_Update` 0x5628A0 on a hand-built hierarchy object |
| `emu_arms.py` | `Anim_Update's two generic arms ...` rows | runs the SSE instructions of 0x562BB0 in `emu.py`, a scalar float32 emulator |

`emu.py` is validated against the real function: it reproduces all `oracle_base.py` rows bit for bit (`Base_Update`'s loop body
0x562930-0x562B7A, run per pivot). `emu_arms.py` then runs the same way over the arms' segments (listed in the test's comment).
`Anim_Update` itself needs vtable objects, which the oracle cannot call without hand-assembled stubs, hence the emulator.

The live `oracle_*.py` scripts require Windows and the built retail helper with
BFME2 installed. The emulator scripts require Python 3 and NumPy; dumping also
needs curl and a running local ghidrasql server on the BFME2 `game.dat` project.
From the repository root on Linux/macOS (POSIX shell), with `PORT` set to that
local server's port and the ignored `workspace` directory created:

```sh
python3 tools/retail_oracle/w3d_pose/dump_asm.py "$PORT" 0x562BB0 workspace/arm_asm.txt
ARM_ASM=workspace/arm_asm.txt python3 tools/retail_oracle/w3d_pose/emu_arms.py
```

`emu_arms.py` defaults to `arm_asm.txt` relative to the working directory; `ARM_ASM`
selects the private dump above. The dump is derived from retail bytes and is not committed.

The finding behind all three: the Ghidra decompile writes `T + a + b` for what the compiled code computes as `T + (a + b)` (the two
product pairs are added before the translation); only the instructions, or a call to the real function, settle the grouping.
