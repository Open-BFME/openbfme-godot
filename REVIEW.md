# Review rules

These rules are for human and bot reviewers. A review judges the changed lines
only. Authors follow the same rules, which also appear in `AGENTS.md`,
`CLAUDE.md` and `docs/PLAN.md`.

## Report these

1. **Every edition.** A change to a shared INI parser, data table, module or
   startup path must work on BFME1, BFME2 and RotWK data, or name the edition
   it leaves out and how that edition fails. A typed parser that rejects
   another edition's fields stops that edition starting (#173, #177).
2. **No silent fallback.** Missing or unknown data is a named error, an
   `unverified` report line or a registered stop in `docs/STOPS.md`. It is
   never a default and never another edition's rule.
3. **Golden pins.** A changed `engine/tests/data/**/*.hashes` file must come
   with the gameplay change that explains it and a `// pin: <reason>` comment.
   Flag a re-measure with no cause, or a cause that does not fit the code
   change.
4. **Determinism.** Simulation code takes floating-point arithmetic, maths
   functions and float-to-integer conversions through `SimMath.h`. Integer
   arithmetic, float comparisons and copies are fine. Simulation results must
   not depend on wall clocks, render state or unordered container order.
   Network code may use clocks for pacing and timeouts. Work moved onto a job
   pool must produce the serial result, and a test must compare the two.
5. **Sources.** Game behaviour names the source it follows (`RW 0x...`,
   `BFME2 0x...`, a decomp file or Zero Hour). It keeps target facts, donor
   facts and inference apart, and a lower source never overrides an
   established difference.
6. **Proof.** A logic, data, state or protocol fix needs a test that fails
   without the fix. A purely visual fix needs before and after captures
   attached to the pull request. Assertions state exact values; a bound needs
   a `// tolerance: <reason>` comment.
7. **CI is not retail evidence.** Public CI has no game files, so retail tests
   print SKIP there and count as passed. It runs no Godot tests and only
   compiles Windows. A change to retail behaviour must say which retail tests
   or games were run with the files present.
8. **Game files.** Nothing from the games' installs enters the repository,
   including extracted or converted data.

## Do not report

- Style, naming or formatting that a formatter or the commit gate covers.
- Older code that the pull request does not change.
- Requests for more tests that would not catch a real regression.
- The existence of a stop that is properly reported, pinned and registered
  (`docs/PLAN.md`, "Acceptance stops"). Reviewers judge the reporting, not
  the stop's existence.
