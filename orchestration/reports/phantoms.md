
## Phantom References (5 Runners Appearing in Gates but NOT Executable)

These runners are referenced in gate scripts but are NOT actual executable registrations:

| Runner | References | Status | Reason |
|--------|-----------|--------|--------|
| banner_castle_silent_playtest_runner | tools/export-scan.ps1:611 (NOT WIRED comment) | File exists | Deliberately marked NOT WIRED; appears only in export-scan.ps1 as informational |
| diagnostics_log_runner | tools/export-scan.ps1:170 | File exists | Appears only in export-scan.ps1; diagnostic utility not an actual test |
| lan_discovery_runner | tools/export-scan.ps1:161 | File exists | Appears only in export-scan.ps1; network diagnostic, not a functional test |
| menu_match_cycle_runner | tools/export-scan.ps1:156 | File exists | Appears only in export-scan.ps1; internal diagnostic |
| retail_mp_menu_runner | tools/export-scan.ps1:155 | File exists | Appears only in export-scan.ps1; not in active gate registration |

**Interpretation**: These runners are real files but not tracked by actual execution gates (tools/gate-retail.ps1, tools/gate-rotwk-systems.ps1, etc.). They appear ONLY in export-scan.ps1, which is an informational/diagnostic tool, not an execution gate. Therefore they are phantoms: they inflate the lexical reference count (106) but are not part of the true wired count (101).

