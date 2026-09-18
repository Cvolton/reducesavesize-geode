# Cloud Save Optimizations
It makes cloud saves smaller, which makes them faster and easier to upload without problems.

Specifically the following changes are made:
* All created levels are recompressed
  * The underlying level data is not modified
* Unused Daily and Gauntlet level data is removed
* The save files themselves are compressed with a stronger algorithm

This results in savings of about 15% while keeping everything fully compatible with the vanilla game.

## Important Recommendation
Geometry Dash is notorious for <cr>randomly wiping players' saves</c> even without any mods installed. It is therefore recommended to also install HJfod's <cg>Backups mod</c> to minimize data loss.

<mod:hjfod.backups>