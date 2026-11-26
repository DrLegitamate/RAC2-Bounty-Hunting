# Bounty Hunter

A native [OpenRAC](https://github.com/OpenRAC/OpenRAC) mod for **Ratchet & Clank: Going Commando**.
Combat pays better, and every map has a wanted enemy.

- **Bounties.** Every enemy kill pays extra bolts on top of the game's own drop. Kills in quick
  succession build a streak (x2, x3… up to x5), and a kill finished with the OmniWrench pays double.
  Tougher enemies pay more.
- **Contracts.** When you land on a map, one ordinary enemy is chosen at random and painted in a
  colour of its own. A weapon is picked at random from what you own. Kill the wanted enemy with
  that weapon and the contract pays **30 times** its normal bounty. Kill it with anything else and
  the contract fails, though you still get the ordinary bounty.

> **Status: built and unit-tested, not yet played.** The library builds against the released SDK
> and passes its automated tests, including a test that loads it the way OpenRAC does against a
> simulated port. It has **not** been run inside OpenRAC with the game yet, so treat 1.0.0 as a
> release for testers. See [Testing](#testing).

## Installing

1. Build it (below), or take `bounty-hunter-<version>.zip` from this repository's Releases.
2. In the OpenRAC launcher open **Mods**, choose **Add a mod**, and pick the `.zip`. You can also
   choose **Add from a link** and paste this repository's address; the launcher installs from
   the latest release.
3. Switch it on, set its options under its row, and press Play. Option changes apply from the
   game's next start.

You need an OpenRAC whose mod interface is **1.29 or newer**. **1.34 or newer** gives the painted
target; older ports fall back as described under [Highlighting](#highlighting-the-wanted-enemy).

### Supported discs

| Disc | Version id | Supported |
|---|---|---|
| NTSC-U v1.01 | `ntsc` | Yes |
| PAL | `pal` | Yes (streak timing uses PAL's 50 ticks a second) |
| NTSC-U v2.00 | `ntsc2` | Yes |

Every hook point the mod needs exists on all three discs. The only disc-specific data is two
inventory addresses ([src/discs.h](src/discs.h)).

The mod is marked `"multiplayer": "solo"`, so the launcher keeps it out of co-op parties.

## Settings

### General bounty

| Setting | Type | Default | Range | What it does |
|---|---|---|---|---|
| Enable Bounty Hunter | switch | On | | Master switch. Off means the mod takes no hooks at all. |
| Base Enemy Bounty | number | 10 | 0–1000 | Extra bolts for a Standard enemy. 0 turns the kill bounty off and leaves contracts. |
| Streak Duration (seconds) | number | 5 | 1–30 | How long a streak waits for the next kill. |
| Maximum Streak Multiplier | number | 5 | 1–10 | Each kill in a streak adds one to the multiplier, up to this. |
| Wrench Bounty Multiplier | number | 2 | 1–10 | Applies when the killing blow was the OmniWrench; stacks with the streak. |
| Enemy Bounty Tiers | switch | On | | Scales the bounty by tier (below). Off pays the base for everything. |
| Show Bounty Notifications | switch | On | | A short line above the HUD for each bounty: `BOUNTY +30 BOLTS  X3 STREAK`. |

### Contracts

| Setting | Type | Default | Range | What it does |
|---|---|---|---|---|
| Enable Map Contracts | switch | On | | One contract per map visit. |
| Contract Reward Multiplier | number | 30 | 1–100 | The contract pays this many times the target's tier bounty, before streak and wrench. A Standard target at the default base pays 300 bolts. |
| Automatically Equip Required Weapon | switch | On | | Puts the required weapon in Ratchet's hand once, when the contract is announced. You can switch freely afterwards. |
| Include OmniWrench in Required Weapon Pool | switch | Off | | Lets a contract demand a wrench kill. |
| Require Available Ammunition | switch | On | | Only weapons with rounds left, or endless ammo, can be required. |
| Highlight Wanted Enemy | switch | On | | Paints the target, or marks it until the paint can go on. |
| Target Hue Mode | choice | Random Each Map | Random Each Map / Fixed Custom Hue | A new colour per map (never within 60° of the last), or always the hue below. |
| Fixed Target Hue | number | 210 | 0–359 | Degrees on the colour wheel: 0 red, 120 green, 210 azure, 300 magenta. |
| Target Glow Level | number | 8 | 0–15 | Strength of the painted enemy's rim glow (OpenRAC 1.34+). |
| Show Target Notifications | switch | On | | The contract banners and the corner indicator (wanted enemy, weapon, reward, status). |

The spec's original *Contract Reward: 500 bolts* was replaced by the multiplier, at the owner's
request, so a contract on a tough enemy is worth more than one on a weak enemy.

### Enemy tiers

The tier comes from the enemy's own bolt drop, which the level sets per enemy. This is the game's
own measure of what an enemy is worth.

| Tier | Own drop | Bounty (base 10) |
|---|---|---|
| Basic | 0–10 | 5 |
| Standard | 11–30, or unknown | 10 |
| Advanced | 31–75 | 20 |
| Elite | 76+ | 40 |

The thresholds are a first guess and have not been checked against real drop values in play.
There is no Boss tier, because nothing reliable tells a boss apart from a turret or a spawner
(see [docs/LIMITATIONS.md](docs/LIMITATIONS.md)).

## How it works

[docs/DESIGN.md](docs/DESIGN.md) has the full picture. In short:

- **Kills** come from the game's own death recorder (`rac2.moby.record_death`, SetDeathBits), the
  same call that drops an enemy's bolts. Inside it, `rac2.combat.count_kill` is the game saying
  "this was an enemy" (bestiary kind 5). Crates, pickups and scripted rewards never reach it. On
  ports without that hook, the mod falls back to the enemy classes the level's
  `moby_copy_check` answers named at load. Each placed enemy pays once per level load.
- **Weapon attribution** follows every hit through `rac2.combat.post_hit` to the moby that dealt
  it. Shots reported by `on_weapon_fire` are matched by slot and class, then known weapon classes
  are tried, then wrench strikes. The weapon in Ratchet's hand is **never** used to decide.
  [docs/ATTRIBUTION.md](docs/ATTRIBUTION.md) lists every weapon and whether a contract may require it.
- **Contracts** are picked when `rac2.level.load` returns. Candidates are placed, live enemies
  that `moby_copy_check` calls `OK`, or `NO_ROOM` (only too big to copy). Bosses, turrets,
  spawners, set-piece enemies, spawned enemies and non-enemies are left out. Enemies with a known
  name are preferred. The contract is held by the level and the enemy's stable key, so a death and
  reload bring back the same target, weapon and colour.
- **Highlighting** paints the one target through `moby_material`, as below.

### Highlighting the wanted enemy

`moby_material` needs a moby's game address, and the SDK's moby list gives slots, not addresses.
The mod does not compute an address from a slot. Instead it waits for a hook point that is
*handed* the moby: `rac2.combat.get_hit` (an enemy polling its hits as it updates),
`rac2.combat.post_hit` (it was hit) or `rac2.moby.record_death`. It paints that address only when
`moby_slot_of` confirms it is the target's slot, holding the target's class and key.

- OpenRAC 1.34+: `OPENRAC_MOBY_MATERIAL_PAINT_AT(glow)`, the contract colour over the model's own
  shading, with a glowing rim.
- 1.29–1.33: `OPENRAC_MOBY_MATERIAL_TINT` in the contract colour.
- Until the address has been seen (an enemy far away that has not updated yet), a small `WANTED`
  marker in the contract colour is drawn over the target's head through the overlay.

The paint is cleared through the same address on completion, failure, or loss of the target, and
the port clears it at level changes. The class-wide material is never used. The SDK change that
would make all this direct is in [docs/SDK_REQUEST.md](docs/SDK_REQUEST.md).

## Building

You need CMake 3.20+, a C++17 compiler, Python 3 and optionally Ninja. Nothing from OpenRAC
itself is required.

```bash
python3 tools/fetch_sdk.py
```

```bash
cmake -S . -B build -G Ninja
```

```bash
cmake --build build
```

```bash
ctest --test-dir build --output-on-failure
```

```bash
python3 .sdk/openrac-mod-sdk-1.36.0/tools/package_mod.py build/package/bounty_hunter -o dist
```

`sdk.lock` pins **SDK 1.36.0**, the newest one published as a release, with its SHA-256. To try
the build without packaging, add `build/package/bounty_hunter` in the launcher's Mods tab.

Pushing a tag `v<version>` runs [.github/workflows/build.yml](.github/workflows/build.yml) (the
SDK's own workflow). It builds and tests on Windows, Linux and macOS, joins the platforms into
`bounty-hunter-<version>.zip`, and publishes the release the launcher installs from.

`src/enemy_names.h` is generated from rac2-decomp's cited class labels by
`tools/gen_enemy_names.py`.

## Testing

| What | Status |
|---|---|
| Compiles (GCC, Clang; Linux x86-64) | Yes, no warnings with `-Wall -Wextra -Wpedantic -Wshadow` |
| Packaging (`package_mod.py`, which also checks the manifest's hooks) | Yes |
| Unit tests: bounty, attribution, contracts, highlighting | All pass |
| Load test: the built library under a simulated port | Passes; also clean under AddressSanitizer and UBSan |
| Windows and macOS builds | Not run here; the release workflow builds them |
| **In-game in OpenRAC, any disc** | **Not done** |

The load test drives the real library through four maps. It checks the contract choice, the
equip, painting of the target alone, a verified Lancer kill paying 10 + 300, the double record
paying nothing, a crate paying nothing, a Blitz Gun finishing blow failing the contract, the
contract surviving a death reload into another slot with the same colour, an unattributed kill
failing safe, and a map with nothing eligible. It proves the mod handles the SDK's documented
calls correctly. It cannot prove the game makes those calls as documented, which only play will
show. [docs/LIMITATIONS.md](docs/LIMITATIONS.md) lists what to watch for.

## Known limitations

See [docs/LIMITATIONS.md](docs/LIMITATIONS.md). The main ones:

- Not yet verified in play.
- Contracts only require weapons whose damage can be traced (16 of 25, wrench included; the rest are listed with
  reasons in [docs/ATTRIBUTION.md](docs/ATTRIBUTION.md)).
- A kill is credited to a weapon only when every hit in the final two ticks came from it.
  Otherwise the contract fails as *not verified* rather than guessing.
- Enemy names come from community class labels (198 classes). An enemy with none is shown as
  `Unidentified hostile (class N)`, and named enemies are preferred as targets.

## Licence

GPL-3.0-or-later ([LICENSE](LICENSE)). `tools/fetch_sdk.py` and `.github/workflows/build.yml` are
the OpenRAC SDK's files and keep their MIT notice. The mod contains no game data: no code, text,
models or bytes from the discs.
