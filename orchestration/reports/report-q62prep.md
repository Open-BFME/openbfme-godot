# Q62 b/c/d maps republish preparation

Date: 2026-08-21 · Owner: `agent-a0e02748f884b0e94` · Binding brief:
`workspace/orchestration/fable-wave/castle-lanes/brief-q62-republish-prep.md`

Baseline commit: `44de46d5f6edd436e3b54b0659feb4793d3f3448`.
Q56f scratch maps pack:
`rotwk-playable-maps-private/349a613a71567280590107610c2afbe4797f4c0eedb85e741815b1fb07daa37c`.
Helm's Deep control pack:
`rotwk-playable-maps-private/6f6be4dbbabceb7c78a8c3e6e56a5d955421320c592b7d5178b44fd532acc4d9`.
No selection, pack, publish, or sim file was changed.

## Result

Q62b/c/d are complete and ready for fresh-context verification. Ramp endpoint
derivation now restricts each ground search to a conservative grid bounding box
around the authored low edge instead of scanning the entire navigation grid for
every ramp. The exact distance filter and Q56f reach semantics are unchanged.
On the untouched Q56f scratch cook, the derived endpoint bytes remain exactly
665 bytes with SHA-256
`e8d7303ccc609aa894bb79b739ab235b16c0b861d94475ae29d3c948b0384a97`;
the portal count remains exactly 42.

The deleted Q56f proof is restored as
`game/tests/castle_wall_walk_census_runner.gd`. It loads an explicitly named
maps-pack digest, emits the map-data time and canonical endpoint hash, and
asserts Minas' proven state: 22 ground components, largest 89,391 cells,
Player 1 pocket 7,496 cells, Player 2 in the largest component, no ground or
layered route between the authored starts, and zero connected portal pairs.

## Performance

Both measurements use the same direct `RetailMapData.load_from_pack` path in
the restored census runner. The before and after runs use the exact pack
digests named above.

| Map | Before | After | Delta |
|---|---:|---:|---:|
| Minas Tirith (Q56f local cook) | 12,394 ms | 2,196 ms | -10,198 ms (-82.3%) |
| Helm's Deep (control) | 4,024 ms | 2,045 ms | -1,979 ms (-49.2%) |

The full Minas live-boot run independently reported `map_data delta_ms=1747`
and passed 8/0. Evidence:
`workspace/logs/q62prep-before-minas-profile2.txt`,
`q62prep-after-minas-census.txt`,
`q62prep-before-helms-profile.txt`,
`q62prep-after-helms-profile.txt`, and
`q62prep-minas-live-boot.txt`.

## Endpoint narrowing and authored reach

`castle_wall_walk_runner.gd` now pins both parts of the Q56f behavior:

- equal endpoint candidates narrow to exactly one deterministic, sorted pair;
- `authored_reach` admits the maximum horizontal low-edge-to-mesh distance,
  including the diagonal across a wide low edge.

That second envelope is generous: it can admit a ground cell farther than the
ramp's centerline run when the low edge is wide. It is retained deliberately
because Q62 requires the Q56f endpoint set to remain byte-identical, and the
Q56f retail proof established that its five refused endpoints are still beyond
the entire source mesh (nearest ground 5.3289–7.6656 local units versus authored
reach 2.5793–4.6945). The runtime does not treat the bounding box as authority;
every candidate still must pass the original squared-distance comparison.

Failing first: the new bounded-scan check was 24 passed / 1 failed before the
search-bounds helper existed. Final wall runner: 25 passed / 0 failed.
Evidence: `workspace/logs/q62prep-failing-first-bounded-scan.txt` and
`q62prep-wall-runner.txt`.

## Definition of Done

| Gate | Result | Evidence |
|---|---|---|
| Walk-surfaces pytest | PASS, 8 passed | `workspace/logs/q62prep-walk-surfaces-pytest.txt` |
| Castle wall-walk runner | PASS, 25/0 | `workspace/logs/q62prep-wall-runner.txt` |
| Restored Minas census | PASS, 6/0 | `workspace/logs/q62prep-after-minas-census.txt` |
| Q56f endpoint bytes / portals | PASS, same SHA-256 / 42 | before/after Minas logs above |
| Minas castle live boot | PASS, 8/0 | `workspace/logs/q62prep-minas-live-boot.txt` |
| Main deterministic state pin | PASS, `b025d16237ff644d66211a9cc26872f18b61520b9a377f11e9e99c6eceb43f58` | `workspace/logs/q62prep-state-pin.txt` |
| Pathing pin | PASS, `2e5ad58054d28dc93f37ef4728549bb538f6d4a1c22be922ec19b59fb2d1b12d` | `workspace/logs/q62prep-pathing-pin.txt` |

Failure-by-name delta: the only failing-first name,
`ground_endpoint_scan_is_bounded_to_authored_reach`, is green. The 22 prior
wall-runner checks remain green; the three new endpoint checks are green. No
existing gate lost a check or gained a failure.

## Not done

- No maps pack was recooked, selected, sealed, copied, or published.
- No `selection.json` was read-modify-written.
- No distribution build was produced. Maps republish and v0.2.9 remain the
  next owner-authorized lanes after fresh-context verification.
