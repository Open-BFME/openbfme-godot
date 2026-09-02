# Fleet tools

Cross-platform Python, no dependencies beyond the standard library. These
are the only workflow tools; PowerShell stays for release packaging.

| Tool | Use |
|---|---|
| `work.py next [queue]` | serve one unit from the highest-priority non-empty queue (or the named one) with its oracle command |
| `work.py list <queue>` | list open units in a queue |
| `work.py done <unit-id>` | mark a manual unit done (oracle-derived queues mark themselves) |
| `work.py bank <unit-id> --note "..."` | bank a partial attempt to `workspace/attempts/` and `docs/lessons.md` |
| `progress.py` | print the product-bar table with deltas since the last run |
| `lock.py acquire|release|list <path>` | per-file locks for hubs in `hubs.txt` (uses `FLEET_AGENT`) |
| `audit_deps.py <path>...` | list tracked files that reference each candidate before deleting it |
| `precommit.py` | the fast gate; installed by `install_hooks.py` |
| `install_hooks.py` | write `.git/hooks/pre-commit` and `pre-push` |

## Queues

Two kinds. **Derived** queues compute their units from a denominator and
the current state, so finishing a unit removes it automatically:

- `core-modules`: census module types minus `engine/OpenBfme.Sim/Modules/<Name>.cs`.
- `red`: `RETAIL_SLICE FAIL` lines in `workspace/logs/latest-retail_slice_runner.txt`.
- `maps`, `assets`, `screens`: corpus census minus converted digests, when the census reports exist under `workspace/retail-work/reports/`.
- `bugs`: open GitHub issues labelled `playtest` (needs `gh`).

Generate the three private corpus-backed queues from the layered install and
the currently selected content packs (add `--kind assets|maps|screens` to
refresh only one):

```text
PYTHONPATH=importer python -m openbfme_importer.fleet_queues --install ROOT --content-root workspace/content-packs --out-dir workspace/retail-work/reports
```

**Manual** queues are directories of one JSON file per unit under
`queues/<name>/`. Adding a unit is adding a file; marking it done sets
`"status": "done"` in that file. No shared list to conflict on.

Priority order is the order in `work.py QUEUE_ORDER`.

For the `maps` queue, **boot** includes both the existing Godot cooked-map load
and the new-core C# path: load `openbfme.map.v1` through `MapDocument`, then
apply it through `MapWorldBuilder`. `verify_item --kind maps` performs the
Python-side conversion to map-v1 and required-key validation; the C# test suite
proves the strict engine load and world build.
