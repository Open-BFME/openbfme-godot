@echo off
cd c:\Users\Jonathan\Desktop\open-bfme
git commit -m "feat: map object.DamageCreationList end-to-end, add runtime validation" ^
  -m "Debris/damage object spawning on destruction now has a compiled consumer and runner." ^
  -m "Add game/src/retail_slice/damage_creation.gd: static spawn_on_death() executor" ^
  -m "Add game/tests/test_damage_creation_list.gd: validates row matching, debris count, position inheritance" ^
  -m "Registry entry: DamageCreationList -^> (damage_creation.gd, test_damage_creation_list.gd)" ^
  -m "Test: 5/5 pass (damage type match, model condition match, debris count, position, wrong-type refusal)" ^
  -m "Evidence: 665 RotWK sites, 605 BFME2 sites per retail-ini-coverage ledger" ^
  -m "Co-Authored-By: Codex Sol ^<noreply@openai.com^>"
