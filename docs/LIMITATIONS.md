# Known limitations

## Not verified in play

Nothing here has run inside OpenRAC with the game. The automated tests check the rules and the
glue against a simulated port that behaves as the SDK documents. These assumptions need a real
playthrough:

| Assumption | If wrong | What to look for |
|---|---|---|
| Ordinary enemies call `Moby_GetHitMessage` as they update | Paint appears only on first hit; the marker shows until then | Marker staying over an enemy that is visibly moving |
| A weapon's damage posts with the source mobys in [ATTRIBUTION.md](ATTRIBUTION.md) | Contracts for that weapon fail as *not verified* | Log line `contract failed: the killing blow could not be laid to one weapon` after a clean kill |
| Hero-sourced `0x10000` hits are wrench strikes | A charge-boots ram or another hero move might count as a wrench kill | Wrench bonus on non-wrench kills |
| Enemies die within 900 ticks (15 s) of their last hit | Very slow deaths fail as *not verified* | Big enemies with long death animations |
| The tier thresholds (10 / 30 / 75 own bolts) suit the game's values | Tiers skewed (all Basic, say) | The bounty popup's amounts |
| Placed enemies leave the list only by dying | False *TARGET LOST* | That banner without a kill |

The mod logs its decisions (contract chosen, kept, completed, failed and why, fallbacks in use)
to OpenRAC's session log, prefixed with the mod's id.

## Attribution is strict by design

- Nine weapons can never be required ([ATTRIBUTION.md](ATTRIBUTION.md)).
- A kill whose last two ticks of hits mix weapons, or include a hit of unknown source, is
  *not verified*. The contract fails and pays nothing, though the ordinary bounty is still paid.
  Spraying the target with two weapons at the end can therefore lose a contract.
- Splash damage from the required weapon counts only if its blast class is in the table. Blasts
  of unlisted classes (the Hoverbomb's, for instance) count for nobody.

## Kill counting

- With `rac2.combat.count_kill` (OpenRAC builds after 2026-11-25), "enemy" is the game's own
  verdict. On older ports the fallback misses enemy classes that only ever appear by spawning.
- Spawned enemies that the game never records (`uid < 0`) give no bounty. That matches the game,
  which gives them no bolt drop either.
- No Boss tier. `moby_copy_check`'s `LISTED` covers bosses, turrets and spawners alike, and there
  is no reliable boss table. Bosses pay by their own drop's tier.

## Contracts

- One contract per **visit**. Leaving a map and coming back later starts a new contract, while a
  death's reload keeps the current one. Contracts are not saved between sessions.
- Bosses, turrets, set-piece and spawned enemies are never targets. A map where every enemy is one
  of those has no contract, and says so.
- Enemy names come from community class labels for 198 classes ([../src/enemy_names.h](../src/enemy_names.h)).
  An enemy without one is shown as `Unidentified hostile (class N)` rather than given an invented
  name. Named enemies are preferred as targets.
- The reward is announced as `30X BOUNTY` until the target's tier is known, which happens the
  first time a hook hands over its address. It is then shown in bolts.
- Auto-equip is skipped while the hero is Clank, and if the port has no `rac2.item.equip` entry
  (logged).
- The inventory is read from two per-disc globals ([../src/discs.h](../src/discs.h)), because the
  SDK has no inventory call.

## Highlighting

See [SDK_REQUEST.md](SDK_REQUEST.md). Paint needs OpenRAC 1.34; 1.29 to 1.33 tint; the marker
covers the time before the target's address has been seen. The marker is drawn through walls.

## Multiplayer

The manifest says `solo`. Bounties and contracts are local to one game and have not been designed
for a party's shared world.
