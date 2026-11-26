# Design

## Layout

| File | What |
|---|---|
| [src/rules.h](../src/rules.h), [src/rules.cpp](../src/rules.cpp) | All decisions, free of the SDK and the game: tiers, streak, payouts, duplicate guard, shot tracker, hit classifier, fatal-hit ledger, target eligibility and pick, weapon pool, colours, the contract state machine, the paint state machine |
| [src/weapons.h](../src/weapons.h) | Weapons, names, trackability, hit-source classes |
| [src/enemy_names.h](../src/enemy_names.h) | Cited class labels (generated) |
| [src/discs.h](../src/discs.h) | Per-disc inventory addresses; SDK-documented offsets |
| [src/mod.cpp](../src/mod.cpp) | The glue: hooks, callbacks, HUD, overlay |
| [tests/](../tests) | Unit tests of the rules, and a load test of the built library against a simulated port |

## Hooks and callbacks

| SDK entry | Since | Used for |
|---|---|---|
| `rac2.moby.record_death` | hook | Every recorded death: bounty, contract outcome, paint cleared |
| `rac2.combat.count_kill` | hook, optional | Inside it: the game's enemy verdict |
| `rac2.combat.post_hit` | hook | Every hit: attribution; the target's address |
| `rac2.combat.get_hit` | hook | Enemies polling their hits: the target's address |
| `rac2.level.load` | hook | New visit or reload: choose or restore the contract |
| `on_weapon_fire`, `on_shot_end` | 1.29 | Shot slot to weapon |
| `moby_list2`, `moby_slot`, `moby_slot_of`, `moby_copy_check` | 1.14 / 1.9 / 1.25 | Slots, keys, classes, eligibility |
| `figure_slot(-1)`, `hero_held` | 1.9 | The hero's slot and class; the wrench moby's class; the hand (only to refuse a guess) |
| `weapon_record` | 1.29 | Ammo capacity |
| `hook_entry("rac2.item.equip")` | 1.21 | Auto-equip |
| `moby_material` | 1.23 / 1.34 | Tint / paint |
| `bolts` | 1.9 | Paying |
| `hud_text_set`, `on_overlay` | 1.12 / 1.14 | Banners, indicator, marker |
| `play_status` | 1.8 | Phase, level, load counter |

The manifest's `api` is 1.29: everything above exists there. Paint is used when `api_minor >= 34`.

## Flow

**Level load** (`rac2.level.load` returns): clear the shot tracker, hit ledger, paid set, streak
and paint address. Read the moby list and judge every placed moby with `moby_copy_check`. Then
`ContractBook::level_loaded(level, resume)`:

- *Continue* (the same level as the last load, after a death for example): re-find the target by
  key (`moby_slot`), point the paint at its new slot, and keep everything else.
- *New visit*: pick a target, pick a weapon from the inventory, pick a hue (random at least 60°
  from the last, or the fixed one). Start the contract, or record that this visit has none.

**Each frame:** read the moby list, the hero's slot and class, and the hand. Check the target is
still in its slot, re-find it by key if not, and declare it lost after 30 ticks missing. Announce
and equip once the player is in control. Redraw the HUD lines.

**Each hit:** read the request's source and mask, resolve the source slot and class, classify it,
and record it against the victim's slot. If the victim is the target, note its address and paint.

**Each death:** note the moby and run the original, during which `count_kill` may mark it an
enemy. Then the fatal blow comes from the ledger. An enemy not yet paid is paid tier × streak
(× wrench if the blow is a verified wrench blow). If it was the target: clear the paint and settle
the contract, paying `multiplier × tier bounty` once on success.

## Why these choices

- **No address arithmetic.** Every game address the mod uses was handed to it by a hook point and
  confirmed by `moby_slot_of`, or comes from an SDK call (`weapon_record`, `relocate_data`). Raw
  reads are limited to fields the SDK headers document (request `+0x10`/`+0x14`, moby `+0xB4`,
  record `+0x8E`) and the two inventory arrays in `discs.h`.
- **No writes to game memory.** Bolts go through `bolts()`, the paint is render-only, and the equip
  is the game's own request.
- **Fail closed.** When attribution is uncertain, the contract fails as *not verified* rather than
  paying. The bounty system never depends on contracts, and contracts never depend on paint.
