@echo off
cd c:\Users\Jonathan\Desktop\open-bfme
git commit -m "fix(combat): close structure armor fallback defect, add runtime validation" ^
  -m "Remove STRUCTURE_ARMOR_PROVISIONAL_SCALAR; structure kinds without compiled armor tables now refuse damage rather than silently using provisional 0.25 scalar." ^
  -m "Test: test_structure_armor_tables.gd validates all selected kinds have compiled tables." ^
  -m "Co-Authored-By: Codex Sol ^<noreply@openai.com^>"
