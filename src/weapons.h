// SPDX-License-Identifier: GPL-3.0-or-later
//
// Going Commando's combat weapons as Bounty Hunter knows them: their item ids, their names, which
// moby classes post hits on a weapon's behalf, and whether a contract may ask for the weapon.
//
// Sources (docs/ATTRIBUTION.md has the reasoning for each row):
//   - item ids: rac2-decomp's level code (each weapon's update reads its damage from
//     g_weapon_table[g_item_slot_to_record[item]] with a literal item), as tabulated by the
//     openrac-mods NG++ mod (ng-plus-plus/src/tiers.cpp kind_of, discs.h kHitSources);
//   - the OmniWrench is item 10: wrench_update (rac2-decomp src/levels/shared/L00_0038BF18-g8.c)
//     reads the thrown wrench's damage from item 0xA's record and posts its hits with mask 0x10000;
//   - the hit-source classes: the moby that a weapon's update passes as the hit's source to
//     Moby_PostHit (NG++ discs.h kHitSources, each row naming the decomp function), plus the shot
//     classes OpenRAC's on_weapon_fire reports (openrac_mod.h, moby_spawn_live's list).

#pragma once

#include <cstdint>

namespace bounty_hunter {

// The OmniWrench's item id.
inline constexpr int kWrenchItem = 10;

// The Walloper's item id: a glove whose punches may come from the hero's own moby, as the
// wrench's swings do, so a hero-sourced melee hit while it is in hand is never called a wrench's.
inline constexpr int kWalloperItem = 53;

// The hit mask of a melee strike (the wrench's swings, throws and slams).
inline constexpr std::uint32_t kMeleeMask = 0x10000u;

// The hero moby's class while the player is Clank (Aranos Prison and the other Clank sections).
inline constexpr int kClankHeroClass = 87;

// How far Bounty Hunter can follow a weapon's damage to its item.
enum class Tracking : std::uint8_t {
    Reliable,    // Every damaging moby it makes is known: a contract may require it.
    Unreliable,  // Some of its damage comes from a source not tied to it: never required.
};

// One combat weapon.
struct WeaponInfo {
    int item;
    const char* name;
    Tracking tracking;
    const char* note;  // Why it is unreliable, or what carries its hits.
};

// clang-format off: one weapon per row.
inline constexpr WeaponInfo kWeapons[] = {
    {9,  "Clank Zapper",     Tracking::Reliable,   "its beam (class 3800) posts the hit"},
    {10, "OmniWrench",       Tracking::Reliable,   "melee mask 0x10000 from the hero or the wrench moby"},
    {12, "Bomb Glove",       Tracking::Reliable,   "its bomb (class 121) posts the hit"},
    {14, "Visibomb Gun",     Tracking::Unreliable, "its guided missile is not reported by on_weapon_fire"},
    {16, "Sheepinator",      Tracking::Unreliable, "transforms rather than kills; no known hit source"},
    {17, "Decoy Glove",      Tracking::Unreliable, "the decoy deals no damage"},
    {18, "Tesla Claw",       Tracking::Reliable,   "its tether (class 177) posts the hit"},
    {22, "Chopper",          Tracking::Unreliable, "its blades are not reported by on_weapon_fire"},
    {23, "Pulse Rifle",      Tracking::Reliable,   "its beam (class 1494) posts the hit"},
    {24, "Seeker Gun",       Tracking::Reliable,   "the gun (1486) and its seekers (1497, 3329)"},
    {25, "Hoverbomb Gun",    Tracking::Unreliable, "the bomb is reported, but its blast's source is unverified"},
    {26, "Blitz Gun",        Tracking::Reliable,   "the gun (1489) and the Blitz Cannon (2801)"},
    {27, "Minirocket Tube",  Tracking::Reliable,   "its rockets (1498)"},
    {28, "Plasma Coil",      Tracking::Reliable,   "its shots (2715) and the Plasma Storm's (2473)"},
    {29, "Lava Gun",         Tracking::Reliable,   "the gun (1481), its blobs (4406) and meteors (2862)"},
    {30, "Lancer",           Tracking::Reliable,   "the gun (1490) and the Heavy Lancer (3077)"},
    {31, "Synthenoid",       Tracking::Unreliable, "its drones are not reported by on_weapon_fire"},
    {32, "Spiderbot Glove",  Tracking::Reliable,   "the bot's exploding trail (3755)"},
    {37, "Bouncer",          Tracking::Reliable,   "its bomblets (3684) and bouncing blasts (3790)"},
    {41, "Miniturret Glove", Tracking::Unreliable, "the turrets' own shots are not tied to the glove"},
    {42, "Gravity Bomb",     Tracking::Reliable,   "the bomb (3300) and its blasts (3070, 3301)"},
    {43, "Zodiac",           Tracking::Unreliable, "its pulse posts no hit at all"},
    {44, "RYNO II",          Tracking::Reliable,   "its rockets (3799)"},
    {45, "Shield Charger",   Tracking::Reliable,   "the shield (2652) and the Tesla Barrier (4498)"},
    {53, "Walloper",         Tracking::Unreliable, "its punches cannot be told from the wrench's"},
};
// clang-format on

// A moby class whose hits are one weapon's.
struct HitSourceClass {
    int moby_class;
    int item;
};

// clang-format off: one class per row.
inline constexpr HitSourceClass kHitSourceClasses[] = {
    {1490, 30}, {3077, 30},                    // Lancer, Heavy Lancer
    {1489, 26}, {2801, 26},                    // Blitz Gun, Blitz Cannon
    {1481, 29}, {4406, 29}, {2862, 29},        // Lava Gun, its blobs, the Meteor Gun's meteor
    {1486, 24}, {1497, 24}, {3329, 24},        // Seeker Gun, its seekers
    {1498, 27},                                // Minirocket Tube's rocket
    {3799, 44},                                // RYNO II's rocket
    {3300, 42}, {3070, 42}, {3301, 42},        // Gravity Bomb, its blasts
    {121, 12},                                 // Bomb Glove's bomb
    {3684, 37}, {3790, 37},                    // Bouncer's bomblet and blast
    {3755, 32},                                // Spiderbot's exploding trail
    {2652, 45}, {4498, 45},                    // Shield Charger, Tesla Barrier
    {177, 18},                                 // Tesla Claw's tether
    {2715, 28}, {2473, 28},                    // Plasma Coil's and Plasma Storm's shots
    {1494, 23},                                // Pulse Rifle's beam
    {3800, 9},                                 // Clank Zapper's beam
};
// clang-format on

// Returns the weapon of item `item`, or nullptr for an item that is not a combat weapon.
inline const WeaponInfo* weapon_info(int item) {
    for (const WeaponInfo& weapon : kWeapons) {
        if (weapon.item == item) {
            return &weapon;
        }
    }
    return nullptr;
}

// Returns the name of weapon `item`, or "Unknown weapon".
inline const char* weapon_name(int item) {
    const WeaponInfo* weapon = weapon_info(item);
    return weapon != nullptr ? weapon->name : "Unknown weapon";
}

// Returns whether a contract may require weapon `item`.
inline bool contract_trackable(int item) {
    const WeaponInfo* weapon = weapon_info(item);
    return weapon != nullptr && weapon->tracking == Tracking::Reliable;
}

// Returns the item whose damage a moby of `moby_class` deals, or -1.
inline int item_of_source_class(int moby_class) {
    for (const HitSourceClass& source : kHitSourceClasses) {
        if (source.moby_class == moby_class) {
            return source.item;
        }
    }
    return -1;
}

}  // namespace bounty_hunter
