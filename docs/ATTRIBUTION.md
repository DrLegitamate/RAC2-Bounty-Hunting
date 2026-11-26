# Enemy classification and weapon attribution

## Which deaths are enemy kills

A death counts when the game records it **and** counts it as an enemy.

1. **Recorded.** `rac2.moby.record_death` (Moby_RecordDeath, rac2-decomp `SetDeathBits`) is the
   game's death recorder. It gives experience, updates the bestiary and drops the bolts. Enemies
   and crates both call it. Scripted reward bursts do not; they go straight to
   `Bolt_SpawnPickups`. It also returns early, without counting, for a moby the level did not
   place (`uid < 0`) or one flagged as giving no reward.
2. **An enemy.** Inside it, for a moby whose class record is of kind 5 (the bestiary's enemy
   kind), `SetDeathBits` calls `moby_count_kill_or_event(moby, 0)`, which OpenRAC exposes as
   `rac2.combat.count_kill`. The mod notes a type-0 call for the moby being recorded. That is the
   game's own verdict, so crates, pickups, NPCs and props are never paid.

`rac2.combat.count_kill` was added to OpenRAC's binding table after SDK 1.36 (OpenRAC commit
`e140714`, 2026-11-25). The mod therefore asks for it at run time and does not list it in the
manifest, whose hooks `package_mod.py` checks against the 1.36 table. On a port without it:

- **Fallback:** an enemy is a placed moby (it has a key) whose class `moby_copy_check` judged at
  this level's load to be of the enemy kind. The port's judge (`port/game/rac2/host/enemy_copies.c`,
  `judge`) checks the class's kind before anything but a dead moby, so every answer other than
  `NOT_ENEMY`, `NOT_PLACED` or an error means kind 5. An enemy class that appears only by spawning
  after load is missed by the fallback. The log says which path is in use.

Each kill is paid once per level load, keyed by the placed moby's record and slot, so a death
recorded twice pays once.

## Choosing a contract target

At `rac2.level.load`'s return, the mod reads every placed moby (`moby_list2`) and asks
`moby_copy_check` about each one:

| Answer | Meaning (openrac_mod.h) | Target? | Why |
|---|---|---|---|
| `OK` | A placed, rewarding enemy the level's data does not need on its own | **Yes** | The ordinary case |
| `NO_ROOM` | As `OK`, but its data is too large for a copy | **Yes** | Only copying is limited, not tracking |
| `NOT_ENEMY` | Not the bestiary's kind: NPCs, vendors, crates, pickups, props | No | Not an enemy |
| `NOT_PLACED` | Spawned by the game, or gives no reward | No | No stable key to persist by, and its death is never recorded |
| `SCRIPTED` | Part of a set piece: a wave, an arena, a door's group | No | May be absent or hidden until a trigger, and its death drives scripts |
| `LISTED` | Bosses, turrets, spawners, Tabora/Grelbin crystal-field creatures | No | Fights with their own rules; turrets and spawners are not huntable enemies |
| error | The port would not say | No | Fail safe |

Dead and scene-actor mobys are excluded too. Among the eligible ones, a class with a cited name
([src/enemy_names.h](../src/enemy_names.h)) is preferred, then the pick is uniform.

`SCRIPTED` and `LISTED` enemies are not unsafe in themselves. They are left out of version 1
because their lifetime is driven by level scripts, which would make *target lost* failures likely.
Allowing `SCRIPTED` targets behind an option is a reasonable next step once play has shown how
they behave.

## Which weapon dealt a hit

Every hit the game posts goes through `rac2.combat.post_hit` with a request whose `+0x10` is the
source moby and `+0x14` the attack mask (both documented in `openrac/games/rac2.h`). On RAC2 the
SDK's `on_moby_hit` is not called by the port at all: it is wired to RAC1's hit path only
(`port/game/common/mod_play.cpp` `game_moby_hit` has no RAC2 caller), so the hook point is the
only supported route.

The source moby is classified in this order (`classify_hit`, [src/rules.cpp](../src/rules.cpp)):

1. **A reported shot.** `on_weapon_fire` gave this slot and class for item *n*, and
   `on_shot_end` has not ended it. Delayed projectiles count for the weapon that fired them,
   whatever is in hand now.
2. **The hero's own moby, mask `0x10000`.** A melee strike, credited to the OmniWrench, except
   when the hero is Clank (class 87) or the Walloper is in hand, whose punches could look the
   same. Then the hit is credited to nobody.
3. **The wrench moby** (its class as `hero_held` reports it while item 10 is in hand): a throw or
   a swing's reach.
4. **A class that only one weapon makes** (`kHitSourceClasses`, [src/weapons.h](../src/weapons.h)):
   the gun mobys of weapons with no shot moby of their own (`OPENRAC_WEAPON_SHOT_ROUNDS`, slot
   -1), and the blasts, blobs and beams weapons leave behind.
5. Anything else is **nobody's**: enemy attacks, hazards, unknown classes. The weapon in hand is
   **never** used to fill the gap.

The source's class is read from `moby_list2`. If the slot is new since the last read (a blast made
this tick), the list is read again, at most twice a tick.

## Which hit was fatal

The game keeps one pending hit per moby until its update reads it, and a weaker hit posted
meanwhile only adds its mask (rac2.h, Moby_PostHit). So the mod cannot know which of several
same-tick hits the enemy acted on. The rule (`HitLedger::fatal`):

- Take the last hit posted to the victim. If it is more than 900 ticks before the death, there
  is **no** known killing blow. Death animations can run for seconds before the game records the
  death.
- Take every hit within 2 ticks of that last one. If they **all** name the same weapon, that
  weapon is **verified**. If any differ, or any is nobody's, the blow is **ambiguous**.

Contracts pay only on a verified blow from the required weapon. A verified blow from another
weapon is `WRONG WEAPON`. Ambiguous or none is `KILL NOT VERIFIED`, which fails without paying.
The wrench bounty bonus also needs a verified wrench blow.

## The weapons

| Item | Weapon | Contract may require it | How its hits are known, or why not |
|---|---|---|---|
| 9 | Clank Zapper | Yes | Its beam, class 3800 |
| 10 | OmniWrench | Only with *Include OmniWrench* | Melee mask from the hero or the wrench moby |
| 12 | Bomb Glove | Yes | Its bomb, class 121 (also reported by `on_weapon_fire`) |
| 14 | Visibomb Gun | No | Its guided missile is not reported by `on_weapon_fire` and has no verified class |
| 16 | Sheepinator | No | It transforms rather than kills; no known hit source |
| 17 | Decoy Glove | No | The decoy deals no damage |
| 18 | Tesla Claw | Yes | Its tether, class 177 |
| 22 | Chopper | No | Its blades are not reported (OpenRAC leaves them out of `on_weapon_fire`) |
| 23 | Pulse Rifle | Yes | Its beam, class 1494 |
| 24 | Seeker Gun | Yes | The gun (1486) and its seekers (1497, 3329) |
| 25 | Hoverbomb Gun | No | The bomb is reported, but whether its blast posts as the bomb is unverified |
| 26 | Blitz Gun | Yes | The gun (1489) and the Blitz Cannon (2801) |
| 27 | Minirocket Tube | Yes | Its rockets (1498), the Megarocket Cannon's too |
| 28 | Plasma Coil | Yes | Its shots (2715), the Plasma Storm's (2473) |
| 29 | Lava Gun | Yes | The gun (1481), its blobs (4406) and the Meteor Gun's meteors (2862) |
| 30 | Lancer | Yes | The gun (1490) and the Heavy Lancer (3077) |
| 31 | Synthenoid | No | Its drones are not reported by `on_weapon_fire` |
| 32 | Spiderbot Glove | Yes | The bot's exploding trail (3755) |
| 37 | Bouncer | Yes | Its bomblets (3684) and bouncing blasts (3790) |
| 41 | Miniturret Glove | No | The turrets are reported, but their own shots are not tied to the glove |
| 42 | Gravity Bomb | Yes | The bomb (3300) and its blasts (3070, 3301) |
| 43 | Zodiac | No | Its pulse posts no hit at all |
| 44 | RYNO II | Yes | Its rockets (3799) |
| 45 | Shield Charger | Yes | The shield (2652) and the Tesla Barrier (4498) |
| 53 | Walloper | No | Its punches cannot be told from the wrench's |

Gadgets (Swingshot, Dynamo, Thermanator and the rest) are not weapons and are never chosen.
The item ids and source classes are rac2-decomp's, as tabulated in the openrac-mods NG++ mod
(`ng-plus-plus/src/discs.h` `kHitSources`, `tiers.cpp` `kind_of`), which names the update
function behind each class. That the OmniWrench is item 10 and strikes with mask `0x10000` is from
`wrench_update` (rac2-decomp `src/levels/shared/L00_0038BF18-g8.c`).

## The required weapon

At the contract's start the mod reads, for each weapon above:

- **owned:** `g_inventoryOwned[item]` (u8)
- **ammo:** `g_weaponAmmo[item]` (s32)
- **capacity:** `weapon_record(item) + 0x8E` (u16, documented by the SDK), where 0 means endless

The two arrays are per disc ([src/discs.h](../src/discs.h)), at the addresses NG++ uses and
OpenRAC's port reads for the same purpose. They are read and never written. The pool is the
owned, contract-trackable weapons, with the wrench only if the setting includes it. With
*Require Available Ammunition*, a weapon must also have rounds or endless ammo. The pick is
uniform, so ammo, upgrades, experience and unlocks are untouched.

*Automatically Equip* calls `rac2.item.equip`'s entry (`hook_entry`), which is the game's own equip
request, once per contract. It is not repeated on a death reload.
