<!-- Thanks for helping! Please read CONTRIBUTING.md first. Keep what applies, delete the rest. -->

## What this changes

<!-- One or two sentences: what a player (or a developer) will notice. Link the issue it fixes, if any: "Fixes #123". -->

## Editions

<!-- Which of BFME1, BFME2 and RotWK does this change affect? How did you check the others? -->

## How it was checked

<!-- What you ran: the unit tests, a game you played, a screenshot of the change. The pull request checks run by themselves.
     Logic, data, state and protocol fixes: name the test that fails without the fix.
     Visual fixes: attach before and after captures to this pull request; never commit them.
     Say which retail tests or games you ran with the game files present, since CI skips those. -->

## Checklist

- [ ] Ran the commit gate (`python3 tools/precommit.py`) on my staged changes.
- [ ] Updated the stop count (`python3 tools/stops_count.py --write`) if `docs/STOPS.md` changed.
- [ ] No game files: nothing from the game's install (.big, .w3d, .dds, .tga, .wav, .mp3, .map, .str, .csf, game.dat ...), nothing extracted
      from them and nothing converted from them (the commit gate refuses them by name and by content).
- [ ] No personal data: no paths of my own machine, no e-mail addresses, no logs of my install.
- [ ] I wrote this change myself (or say below where it comes from and under which licence) and it may be published under the GPL-3.0.
- [ ] For game behaviour: the source it follows (the retail game, a BFME decompile, Zero Hour) is named in a comment.
- [ ] If this fixes a regression, the description says what introduced it (`Regression from <sha or #PR>`).
