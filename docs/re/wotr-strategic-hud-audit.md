# Strategic HUD audit

Edition scope: **RotWK 2.01 presentation**. The shared cursor renderer keeps
its existing path when no strategic control supplies a tooltip. This change
does not implement BFME2 simulation rules or change battle simulation.

## Current code and stale claims

S-1334's older milestones are a history, not an inventory of current code.
Before this change, the retinue/hero details panels, build queue, region
pages, command-button help, region-stat help, map-rollover help, unit swapper,
conflict results and war score screen already existed. In particular, the
region armies page already wrote an army CP line. Its host supplied the
player's world limit rather than that army's tactical limit. The separate
Palantir selection CP widget still had only load logging.

The open-PR audit found no substantially similar strategic change. PR #318's
actual file list concerns the tactical HUD, weather and shell backdrop; it
does not change AptStrategicHUD or the Living World score screen.

## Restored controls

- Checklist scrollbar: bind its loaded clip, forward page/line sizes and
  visibility, handle position changes, reflow after row-height changes, and
  return to the top when END PHASE flags required tasks.
- The seven stat help entries, Options/Objectives help, radial command help,
  retinue archetype help, back-button tooltips, army CP tooltip and build-queue
  archetype/turns tooltips use the existing retail help/cursor movies.
- Palantir selected-army CP display, including clearing it with selection;
  the region page also uses the army's own limit.
- StrategicPlayerStatus: player sides, colours, faction icons, owned
  territories, army-summary quantities, status and scenario/objective text.
  Its close callback releases the screen. The modal screen owns map input.
- Battle-prompt ally/enemy pages: player tabs, colours, names, page fades,
  garrison/hero army portraits and category counts. Army-order swap controls
  remain disabled and report S-1336; accepting inert clicks would be misleading.

## Evidence and limits

The installed RotWK executable has the extra sections described by S-001.
Its observations below retain that caveat; they are not clean-binary proof.
The original APT and INI files are loaded from the retail mount at runtime.
No extracted data or captures are included in the repository.

| Contract | RotWK target | BFME2 reading aid |
|---|---|---|
| Checklist ratios and offset | RW 0x985205, 0x984D27, 0x985591; total-height reader 0x984C68 | ChecklistUIImplRva0057B16D.cpp; AptWotrPanelCallbacks.cpp, RVA 0x57B16D / 0x57AC8F / 0x57B4F9 |
| Stat help | RW 0x983BA3 / 0x983C22; row parser 0x983611 accepts seven rows | Common/StrategicHUD.cpp, RVA 0x579B81 / 0x579C00; BFME2 row parser RVA 0x579676 accepts six |
| Archetype tooltips | RW table 0xC916F0 adds Siege (7) and Support (8); type 4 has no entry | StrategicInGameUIGetTypeImage.cpp, RVA 0x5F027D; table RVA 0x878C3C has six entries |
| Selection CP | RW 0x9E2F5E / 0x9E2FE2 / 0x9E304D | Rva005D38C8Fetch.cpp, Rva005D39EACpState.cpp, RVA 0x5D3966 / 0x5D39EA / 0x5D3A55 |
| Status screen | RW bindings 0x92A37E..0x92A81F; close 0x929F5F | AptStrategicPlayerStatus.cpp, RVA 0x523825; StrategicPlayerStatusCallbacks.cpp, RVA 0x523481 |
| Prompt pages/tabs | RW 0xA0D4D3 / 0xA0D5EA, 0xA11E67, 0xA125EF | StrategicHUDBattlePromptMovieClip.cpp, Rva005FE047Visibility.cpp, Rva005FFCCBAppend.cpp |

The prompt's exclusion of unfinished buildings from the fortress category is
an inference from the donor's plot flag. The source's six-category display
limit is preserved. More than three hero armies in one player page is
reported rather than silently omitted. These limits are not proof of retail
battle-order or reinforcement parity.

## Still open

These requested gaps remain open; S-1334 is not closed by this restoration.

| Gap | Owning prerequisite and acceptance evidence still needed |
|---|---|
| StrategicVeterancy | The movie exists; its per-hero battle/war kill and battle counters are not supplied by the current result model. Port the data collection and result-to-veterancy transition before showing fabricated zeroes. Donor StrategicVeterancy.cpp / StrategicVeterancyAutoResolveCtor.cpp. |
| StrategicRegionAward | The movie exists, but allied ownership disputes and phase 3 are still S-1324. Restore the dispute state/messages, then verify eligible players, voting and the award before enabling its UI. |
| StrategicDynamicAutoResolve | The movie exists, but the current resolver returns the entire battle at once. Restore the sourced per-step/round observer and pacing messages (S-1336), then verify loss updates and continuation against the original executable. Merely animating the final aggregate is not equivalent. |
| Unified-region notice | Region-unified score marks exist. The notice's observer-slot-1 dispatcher/timing is still unidentified. Trace it before inserting a notice during end-of-turn score collection. |
| Defeated-local-player camera flight | The existing end-war splash is not that flight. Its target, trigger and completion/exit sequence still need a target trace; this change does not substitute a generic camera movement. |
| Real-time battle statistics merge | RW 0x8F33A0 merges frames and template-count maps at tactical +0x1F0, +0x2EC and all twenty destroyed maps. The current strategic result API does not carry those maps; the war score still lacks their contributions. Donor LivingWorldScoreKeeper::rva004EE950, RVA 0x4EE950. Preserve template classification, save/resume and one-time collection when implementing it. |
| Profile strategic statistics | StrategicStatsPreferences reads the file but has no strategic reporter/writer. Port StatsReporter RW 0x9CF16C (donor StatsReporterStrategicResults.cpp), including guards and counter semantics, and verify with an external expected profile file. Do not turn the score-screen display totals into guessed profile keys. |
| Remaining selection/help detail | Palantir portrait/region selection state, full unit/building help content, upgrade icons/tooltips and army-retinue scrolling remain incomplete. The new cursor-label path does not claim to implement those data providers. |

## Local validation

- Built the macOS arm64 extension and C++ tests in an independent checkout.
- With the original RotWK/BFME2 mounts, the strategic/hover group passed 16
  cases and 6,884 assertions. After the input-discovered fixes, the focused
  strategic controls/cursor tests passed 2 cases and 59 assertions.
- The original-APT GPU test passed at 1024x768, 1280x720 and 1680x720, using
  real mouse input for checklist scrolling, ally-tab selection and status
  screen tabs/Resume. It also replaces an open battle prompt with different
  player sides, checks that status blocks END PHASE input, and destroys a shell
  with the status screen open.
- The existing HUD viewport/input test passed its 4:3, 16:9 and wider checks.
- The original-data smoke test passed on the combined RotWK/BFME2 mount;
  the BFME1 startup test passed separately with BFME1 data present.
- The stop-count and tolerance-gate Python tests passed (40 tests).

Before/after captures are kept outside the repository for attachment to the pull request.
The baseline capture run intentionally demonstrates the three missing checklist
arrow targets. These checks exercise the original assets in OpenBFME; they are
not an original-executable comparison or proof that S-1334 is complete.
