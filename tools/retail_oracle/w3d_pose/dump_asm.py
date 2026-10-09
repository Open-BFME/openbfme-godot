"""Dump the instructions of one function from a running ghidrasql HTTP server (the BFME2 game.dat project) to the "addr disasm" text
file that emu_arms.py reads.

    python dump_asm.py <port> <function address, decimal or 0x hex> <out file>
"""
import json
import subprocess
import sys

port, addr, out = sys.argv[1], int(sys.argv[2], 0), sys.argv[3]
query = "SELECT addr, disasm FROM instructions WHERE func_addr=%d ORDER BY addr" % addr
raw = subprocess.run(["curl", "-s", "-X", "POST", "http://127.0.0.1:%s/query" % port, "--data", query], capture_output=True, check=True).stdout
result = json.loads(raw.decode("utf-8-sig"))["results"][0]
if result.get("error"):
    sys.exit(result["error"])
with open(out, "w") as f:
    f.write("\n".join("%x %s" % (int(a), d) for a, d in result["rows"]))
print(len(result["rows"]), "instructions")
