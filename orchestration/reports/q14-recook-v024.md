# Q14 — full RotWK recook → v0.2.4 alpha

Lane: grok-q14. Absorbs Q3 (EVA recompose) and Q4 (Men republish). Exclusive
tree access. Selection changes only via one `apply-selection-transaction`.
No `--select` on the recook. Nothing under `workspace/` or `dist/` committed.

Claim commit: `f3c81475b784f861d81296af35d45be7dbb5a7ba`
(`chore(queue): claim Q14 (absorbs Q3 EVA, Q4 Men) as grok-q14`).

## Step 0 — baseline

| item | value |
|---|---|
| pre-claim HEAD | `d06729c1a74822ca50346e3a9bf03e9e152256c8` |
| HEAD after claim | `f3c81475b784f861d81296af35d45be7dbb5a7ba` |
| `check_pack_addresses.py` | PASS packs=200 roots=2 |
| selection sha | `04229f763fd6f41d3637d6747a73fe5a2b8e198dd6b3325907de90e1b08fa41e` |
| activePack | `rotwk-men-vslice/4f92c8a486861100c29f20d1287f01990bc835a2622c53e911cfd2fb024a147e` |
| supplements | 99 (7 factions + 7 EVA + neutrals/music/cursors/maps + 79 missing-physical) |
| VERSION | 0.2.2 (0.2.3 was never cut) |

Live Men pack already carries some `projectileSpeed` / `projectileObjectId`
rows (pre-Q13 compiler shape). It has **no** `damageComponents`. Recook is
required for Q13 radius/taper component rows and a full seven-faction refresh.

## Step 1 — recook

Command (no `--select`):

```
python -u tools\rotwk_full_content.py
  --install workspace\retail-work\editions\rotwk\layered-install\layer-0-rotwk
  --bfme2-install workspace\retail-work\editions\rotwk\layered-install\layer-1-bfme2
  --state-root workspace\retail-work
  --map-limit 10
```

First Start-Process launch (pid recycled) was killed with the agent Job
Object. Relunched via WMI `Win32_Process.Create` as cmd 28180, log
`workspace/logs/q14-recook.txt`, convert run `32030a07b5844c86b562740b972763ad`.
Aborted first attempt saved as `workspace/logs/q14-recook-aborted-jobobject.txt`.

IN PROGRESS — this report will be completed after the remaining steps.

## Digests

(pending recook)

## Selection

| moment | sha256 | active | supplements | address check |
|---|---|---|---|---|
| Step 0 | `04229f763fd6f41d3637d6747a73fe5a2b8e198dd6b3325907de90e1b08fa41e` | men `4f92c8a4…` | 99 | PASS packs=200 roots=2 |

## Runner table

(pending checkpoint)

## Dist

(pending ship)

## Left undone

- Recook still running.
- EVA recompose, selection transaction, checkpoint, re-pin, v0.2.4 publish
  not started.
