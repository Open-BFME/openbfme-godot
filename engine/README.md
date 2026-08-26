# OpenBfme.Sim — FROZEN (owner decision 2026-08-25, queue Q89)

This C# deterministic sim kernel is **parked, not deleted**. No new lanes, no
new behaviors, no maintenance beyond keeping CI's existing test job green.

Why: the shipping simulation is the GDScript retail slice, whose lockstep
determinism is proven continuously — per-tick state hashes bit-identical
across Windows and Linux in CI, plus the retail_state_pin /
retail_projectile_pin / retail_pathing_pin family. The cross-platform
float-drift risk this kernel was insurance against has not materialized.

The doorway back stays open: `game/tests/retail_dualrun_trace_runner.gd`
replays GDScript traces against this kernel. Revive this project only if the
determinism pins ever catch cross-platform drift, and only after the
trace-replay boundary DIRECTION.md describes is an executable contract.
See DESIGN.md for the original architecture intent.
