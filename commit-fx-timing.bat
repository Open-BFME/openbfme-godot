@echo off
cd c:\Users\Jonathan\Desktop\open-bfme
git commit -m "feat: map fxparticlesystem.BurstDelay/InitialDelay end-to-end, add runtime validation" ^
  -m "FX timing delays (authored frame-based initial and repeat delays) now have compiled consumer and runner." ^
  -m "Add game/src/retail_slice/fx_timing.gd: deterministic burst scheduler with seeded RNG sampling" ^
  -m "Add game/tests/test_fx_timing_delays.gd: validates delay suppression, exact emission timing, cadence" ^
  -m "Registry entries: FxTiming -^> (fx_timing.gd, test_fx_timing_delays.gd)" ^
  -m "Test: 6/6 pass (config accept, initial suppress, initial emit, burst suppress, burst emit, authored fields)" ^
  -m "Evidence: BurstDelay=751 RotWK sites, InitialDelay=404 sites per retail-ini-coverage ledger" ^
  -m "Co-Authored-By: Codex Sol ^<noreply@openai.com^>"
