// SPDX-License-Identifier: GPL-3.0-or-later
//
// Going Commando: the two inventory arrays Bounty Hunter reads to choose a contract's weapon, per
// disc. The SDK has no inventory call, so these are the game's own globals, read only (never
// written), through relocate_data.
//
//   owned  u8 per item, non-zero when owned   (g_inventoryOwned; D_001A7AF8 on NTSC-U v1.01)
//   ammo   s32 per item, the rounds it holds  (g_weaponAmmo;     D_00139688 on NTSC-U v1.01)
//
// Sources: rac2-decomp's globals and symbol_addrs for each disc, as tabulated and used by the
// openrac-mods NG++ mod (ng-plus-plus/src/discs.h, kDiscs: the same three discs, the same columns),
// and OpenRAC's own port (port/game/rac2/host/weapon_shots.c reads the ammo array as AMMO + item * 4
// to report OPENRAC_WEAPON_SHOT_ROUNDS). The capacity is read through the SDK (weapon_record, +0x8E
// as openrac_mod.h documents it), not from here.

#pragma once

#include <cstdint>
#include <string_view>

namespace bounty_hunter {

struct Disc {
    const char* version;     // openrac_api.version
    std::uint32_t owned;     // g_inventoryOwned
    std::uint32_t ammo;      // g_weaponAmmo
    int tick_rate;           // The game's ticks a second.
};

// clang-format off
inline constexpr Disc kDiscs[] = {
    {"ntsc",  0x001A7AF8u, 0x00139688u, 60},
    {"pal",   0x001A7B80u, 0x00139708u, 50},
    {"ntsc2", 0x001A7B00u, 0x00139688u, 60},
};
// clang-format on

// The items each array holds.
inline constexpr int kItems = 64;

// The weapon record's u16 at this offset: the most ammo the weapon holds (openrac_mod.h,
// weapon_record: "+0x8E the ammo it holds"); 0 for a weapon with endless ammo.
inline constexpr std::uint32_t kRecordAmmoMax = 0x8Eu;

// A hit request's fields (openrac/games/rac2.h, Moby_PostHit: "+0x10 the attacker, +0x14 the weapon
// or attack mask").
inline constexpr std::uint32_t kRequestSource = 0x10u;
inline constexpr std::uint32_t kRequestMask = 0x14u;

// A moby's own bolt drop, s16 (openrac/games/rac2.h, Moby_RecordDeath: "its bolt drop (the moby's
// s16 at +0xB4)").
inline constexpr std::uint32_t kMobyOwnBolts = 0xB4u;

// The PlayStation 2's main memory: a game address past it is no moby or request.
inline constexpr std::uint32_t kGuestMemory = 0x02000000u;

inline const Disc* disc_of(std::string_view version) {
    for (const Disc& disc : kDiscs) {
        if (version == disc.version) {
            return &disc;
        }
    }
    return nullptr;
}

}  // namespace bounty_hunter
