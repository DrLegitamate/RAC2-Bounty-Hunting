# SDK limitation: material on one moby, by slot

## The limitation

`moby_material(mod, moby, material, rgb)` (since 1.23) draws **one** moby with a material, but it
takes the moby's **game address**. The SDK's moby lists (`moby_list`, `moby_list2`) and lookups
(`moby_slot`) give **slots and keys**, never addresses. `moby_slot_of` goes the other way, from
address to slot. There is no `moby_address_of(slot)`.

`moby_material_class` (1.34) takes a class, not a moby. It paints every moby of that class,
including the menus' pictures of it, so it cannot mark one enemy among several of its kind.

The SDK's own guidance for `moby_material` is to take the address from a hook point that returns
or receives the moby (Going Commando's `rac2.bolt.spawn_pickup`). That works for mobys a mod sees
being made. It does not work for a **placed** enemy picked from the level's list, which is what a
contract targets.

## What Bounty Hunter does about it

It does not derive an address from a slot; the moby table's base and stride are port internals,
not SDK. It waits for a hook point that is handed the moby:

- `rac2.combat.get_hit` (Moby_GetHitMessage), which an enemy's damage path calls as it updates,
- `rac2.combat.post_hit` (its victim), when it is hit,
- `rac2.moby.record_death`, as it dies (used to clear the paint).

It paints that address only when `moby_slot_of(address)` equals the target's slot and the slot
holds the target's class and key in this level load. Until then a `WANTED` marker is drawn over
the target's head by the overlay. The class-wide material is never used to stand in for the one
target.

Weaknesses of this workaround:

- An enemy that never polls its hits until the player is close stays unpainted until then. The
  marker covers that gap.
- It depends on `Moby_GetHitMessage` being polled each update by ordinary enemies. The decomp
  shows ~200 call sites, but whether every enemy kind polls every tick has not been verified in
  play.

## The minimal SDK change

Either of these would make the highlight direct and immediate:

**A. Material by slot** (preferred: no address leaves the port)

```c
/* Draws the live moby in `slot` (as moby_list names it) with `material` and `rgb`, exactly as
 * moby_material does for its address: render-only, until it leaves the level's list of live mobys,
 * its slot holds a moby of another class, the level changes, the mod sets NONE or is unloaded.
 * Returns OPENRAC_HOOK_OK, or OPENRAC_HOOK_UNKNOWN for no live moby in the slot, an unknown
 * material, rgb over 0xFFFFFF, or OPENRAC_MOBY_MATERIALS_MAX reached. Since 1.41. */
int (*moby_material_slot)(openrac_mod* mod, int32_t slot, uint32_t material, uint32_t rgb);
```

The port already has the mapping (`moby_address(slot)` in `port/game/common/mod_api/world.cpp`,
used by `moby_hit`), so the implementation is a few lines over `moby_material`.

A stronger variant also takes the expected `key`. The port refuses when the slot's moby has
another key, which closes the slot-reuse window entirely.

**B. Address by slot**

```c
/* The game address of the live moby in `slot`, 0 for none. Valid until the moby leaves the level.
 * Since 1.41. */
openrac_gaddr (*moby_address)(int32_t slot);
```

This is more general, since it helps any API that takes an address, but it exposes more.

With A, Bounty Hunter would paint the target the moment the contract starts, and keep the marker
only for ports older than the feature (`api_minor` check).

## Related: `on_moby_hit` on Going Commando

`on_moby_hit` (1.9) is documented generically, but on RAC2 nothing calls it: the port's
`game_moby_hit` dispatcher has callers only in `port/game/rac1/host`. Mods have to hook
`rac2.combat.post_hit` and read the request's documented `+0x10` and `+0x14` themselves. Either
wiring `Moby_PostHit` to `on_moby_hit` on RAC2, or documenting that it is RAC1-only, would help.
