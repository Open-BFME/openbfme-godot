# OpenBFME SDK samples

Each folder is a complete `-mod` folder that uses the modding SDK (docs/MODS.md, "Mod scripts in Luau"). Start the game with it:

```
openbfme -- -mod "<path>/tools/sdk/samples/income-banner"
```

- **income-banner:** a sim script that gives every player 250 resources every 10 seconds of game time, and a presentation script
  that shows a banner when the game starts and at each payday. Every player in a game needs the same sim script; the banner script
  may differ between players.
