// SPDX-License-Identifier: GPL-3.0-or-later
//
// The built Bounty Hunter library loaded as OpenRAC loads it, against a fake port made here: an
// openrac_api table whose calls answer from a small made-up level (no game data), and the hook
// points' replacements called as the game would call them. It checks the glue end to end: the
// contract chosen at a level load, the weapon equipped, the target alone painted when a hook point
// hands its address over, bounties and contract rewards paid through bolts(), the wrong weapon
// failing, the contract kept through a death's reload, crates paying nothing.
//
// This is not the game. It shows the mod does what it means to with the calls the SDK documents; it
// cannot show the real game hands them over as documented. Run: load_test <bounty_hunter.so|...>.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "openrac/openrac_mod.h"

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#define REQUIRE(condition)                                                                       \
    do {                                                                                         \
        if (!(condition)) {                                                                      \
            std::fprintf(stderr, "%s:%d: REQUIRE failed: %s\n", __FILE__, __LINE__, #condition);  \
            std::exit(1);                                                                        \
        }                                                                                        \
    } while (0)

namespace {

// The made-up level's mobys: address = kMobyBase + slot * 0x100.
constexpr std::uint32_t kMobyBase = 0x00400000u;
constexpr std::uint32_t kRecordBase = 0x00600000u;  // weapon_record's rows.
constexpr std::uint32_t kRequest = 0x00700000u;     // A hit request.
constexpr std::uint32_t kOwned = 0x001A7AF8u;       // NTSC-U v1.01's g_inventoryOwned.
constexpr std::uint32_t kAmmo = 0x00139688u;        // NTSC-U v1.01's g_weaponAmmo.

constexpr int kHeroSlot = 0;
constexpr int kLancerGunSlot = 1;  // The Lancer's gun moby, class 1490.
constexpr int kBlitzGunSlot = 2;   // The Blitz Gun's, class 1489.
constexpr int kEnemyClass = 2921;  // A class the name table knows.

struct FakeMoby {
    int slot;
    int key;
    int oclass;
    int copy_answer;
    bool dead = false;
};

struct Host {
    std::vector<std::uint8_t> memory = std::vector<std::uint8_t>(0x02000000u);
    std::vector<FakeMoby> mobys;
    std::map<std::string, void*> hooks;
    std::map<std::string, bool> bools;
    std::map<std::string, std::int64_t> ints;
    std::map<std::string, std::string> strings;
    openrac_frame_fn frame = nullptr;
    void* frame_user = nullptr;
    openrac_weapon_fire_fn fire = nullptr;
    void* fire_user = nullptr;
    int phase = OPENRAC_PLAY_PLAYING;
    int level = 4;
    std::uint32_t loads = 1;
    int bolts_paid = 0;
    std::vector<int> bolt_calls;
    std::vector<int> equipped;
    struct Material {
        std::uint32_t moby;
        std::uint32_t material;
        std::uint32_t rgb;
    };
    std::vector<Material> materials;
    std::vector<std::string> hud;
};

Host* g_host = nullptr;

std::uint32_t address_of(int slot) {
    return kMobyBase + static_cast<std::uint32_t>(slot) * 0x100u;
}

FakeMoby* moby_in(int slot) {
    for (FakeMoby& m : g_host->mobys) {
        if (m.slot == slot && !m.dead) {
            return &m;
        }
    }
    return nullptr;
}

template <class T>
void poke(std::uint32_t address, T value) {
    std::memcpy(g_host->memory.data() + address, &value, sizeof value);
}

// ---- The port's calls --------------------------------------------------------------------------

void api_log(openrac_mod*, int, const char* text) {
    std::printf("  [mod] %s\n", text);
}
void api_fail(openrac_mod*, const char* reason) {
    std::printf("  [fail] %s\n", reason);
}
int api_setting_bool(openrac_mod*, const char* key) {
    return g_host->bools.count(key) ? g_host->bools[key] : 0;
}
std::int64_t api_setting_int(openrac_mod*, const char* key) {
    return g_host->ints.count(key) ? g_host->ints[key] : 0;
}
double api_setting_float(openrac_mod*, const char*) {
    return 0.0;
}
const char* api_setting_string(openrac_mod*, const char* key) {
    return g_host->strings.count(key) ? g_host->strings[key].c_str() : "";
}
int api_hook(openrac_mod*, const char* name, const char*, void* replacement, void** original);
int api_on_frame(openrac_mod*, openrac_frame_fn fn, void* user) {
    g_host->frame = fn;
    g_host->frame_user = user;
    return 0;
}
openrac_gaddr api_relocate(openrac_gaddr address) {
    return address;
}
int api_loaded_level() {
    return g_host->level;
}
int api_play_status(openrac_play_status* out) {
    out->phase = g_host->phase;
    out->level = g_host->level;
    out->levels = 27;
    out->loads = g_host->loads;
    return OPENRAC_HOOK_OK;
}
int api_moby_list2(openrac_moby2* out, int max) {
    int n = 0;
    for (const FakeMoby& m : g_host->mobys) {
        if (n >= max) {
            break;
        }
        openrac_moby2& o = out[n++];
        std::memset(&o, 0, sizeof o);
        o.size = sizeof o;
        o.slot = m.slot;
        o.key = m.key;
        o.oclass = m.oclass;
        o.flags = m.dead ? OPENRAC_MOBY_DEAD : 0u;
        o.mission = -1;
    }
    return n;
}
int api_moby_slot(int key) {
    for (const FakeMoby& m : g_host->mobys) {
        if (m.key == key && !m.dead) {
            return m.slot;
        }
    }
    return -1;
}
int api_moby_slot_of(openrac_gaddr moby) {
    if (moby < kMobyBase || (moby - kMobyBase) % 0x100u != 0) {
        return -1;
    }
    const int slot = static_cast<int>((moby - kMobyBase) / 0x100u);
    return moby_in(slot) != nullptr ? slot : -1;
}
int api_figure_slot(openrac_mod*, int) {
    return kHeroSlot;
}
int api_hero_held(openrac_held* out) {
    out->count = 1;
    std::memset(&out->pieces[0], 0, sizeof out->pieces[0]);
    out->pieces[0].item = 30;
    out->pieces[0].oclass = 1490;
    return OPENRAC_HOOK_OK;
}
int api_bolts(int add) {
    g_host->bolts_paid += add;
    if (add != 0) {
        g_host->bolt_calls.push_back(add);
    }
    return g_host->bolts_paid;
}
int api_hud_text_set(openrac_mod*, const openrac_hud_text* lines, int count) {
    g_host->hud.clear();
    for (int i = 0; i < count; ++i) {
        g_host->hud.emplace_back(lines[i].text);
    }
    return OPENRAC_HOOK_OK;
}
int api_on_overlay(openrac_mod*, openrac_overlay_fn, void*) {
    return OPENRAC_HOOK_OK;
}
int api_moby_material(openrac_mod*, openrac_gaddr moby, std::uint32_t material, std::uint32_t rgb) {
    g_host->materials.push_back({moby, material, rgb});
    return OPENRAC_HOOK_OK;
}
int api_moby_copy_check(openrac_mod*, int slot) {
    const FakeMoby* m = moby_in(slot);
    return m != nullptr ? m->copy_answer : OPENRAC_HOOK_UNKNOWN;
}
int api_on_weapon_fire(openrac_mod*, openrac_weapon_fire_fn fn, void* user) {
    g_host->fire = fn;
    g_host->fire_user = user;
    return OPENRAC_HOOK_OK;
}
int api_on_shot_end(openrac_mod*, openrac_shot_end_fn, void*) {
    return OPENRAC_HOOK_OK;
}
openrac_gaddr api_weapon_record(openrac_mod*, std::int32_t item) {
    return kRecordBase + static_cast<std::uint32_t>(item) * 0xE0u;
}
std::int32_t fake_equip(std::int32_t item) {
    g_host->equipped.push_back(item);
    return 1;
}
void* api_hook_entry(const char* name, const char*) {
    return std::strcmp(name, "rac2.item.equip") == 0 ? reinterpret_cast<void*>(&fake_equip)
                                                     : nullptr;
}

// ---- The game's own functions under the hook points ------------------------------------------

using CountKillFn = void (*)(std::uint32_t, std::int32_t);
using RecordDeathFn = void (*)(std::uint32_t, std::int32_t, std::int32_t);
using PostHitFn = void (*)(std::uint32_t, std::uint32_t);
using GetHitFn = std::uint32_t (*)(std::uint32_t, std::uint32_t, std::int32_t);
using LevelLoadFn = std::int32_t (*)(std::int32_t, std::int32_t);

void game_count_kill(std::uint32_t, std::int32_t) {}

// SetDeathBits: an enemy's class (the test's: every class but the crate's) is counted.
void game_record_death(std::uint32_t moby, std::int32_t, std::int32_t) {
    const int slot = api_moby_slot_of(moby);
    const FakeMoby* m = moby_in(slot);
    if (m != nullptr && m->copy_answer != 1) {
        reinterpret_cast<CountKillFn>(g_host->hooks["rac2.combat.count_kill"])(moby, 0);
    }
}
void game_post_hit(std::uint32_t, std::uint32_t) {}
std::uint32_t game_get_hit(std::uint32_t, std::uint32_t, std::int32_t) {
    return 0;
}
std::int32_t game_level_load(std::int32_t, std::int32_t) {
    return 0;
}

int api_hook(openrac_mod*, const char* name, const char*, void* replacement, void** original) {
    const std::string id = name;
    void* game = nullptr;
    if (id == "rac2.combat.count_kill") {
        game = reinterpret_cast<void*>(&game_count_kill);
    } else if (id == "rac2.moby.record_death") {
        game = reinterpret_cast<void*>(&game_record_death);
    } else if (id == "rac2.combat.post_hit") {
        game = reinterpret_cast<void*>(&game_post_hit);
    } else if (id == "rac2.combat.get_hit") {
        game = reinterpret_cast<void*>(&game_get_hit);
    } else if (id == "rac2.level.load") {
        game = reinterpret_cast<void*>(&game_level_load);
    } else {
        return OPENRAC_HOOK_UNKNOWN;
    }
    g_host->hooks[id] = replacement;
    *original = game;
    return OPENRAC_HOOK_OK;
}

// ---- Driving the mod ---------------------------------------------------------------------------

void tick(int n = 1) {
    for (int i = 0; i < n; ++i) {
        g_host->frame(g_host->frame_user);
    }
}

void level_load(int level, bool resume) {
    g_host->level = level;
    ++g_host->loads;
    reinterpret_cast<LevelLoadFn>(g_host->hooks["rac2.level.load"])(level, resume ? 1 : 0);
}

void hit(int victim, int source_slot, std::uint32_t mask = 0x20000u) {
    poke<std::uint32_t>(kRequest + 0x10, address_of(source_slot));
    poke<std::uint32_t>(kRequest + 0x14, mask);
    reinterpret_cast<PostHitFn>(g_host->hooks["rac2.combat.post_hit"])(address_of(victim), kRequest);
}

void poll(int slot) {
    reinterpret_cast<GetHitFn>(g_host->hooks["rac2.combat.get_hit"])(address_of(slot), 0xFFFFFFFFu, 0);
}

void die(int slot) {
    reinterpret_cast<RecordDeathFn>(g_host->hooks["rac2.moby.record_death"])(address_of(slot), 0, 0);
    moby_in(slot)->dead = true;
}

// A level: the hero, two gun mobys, one ordinary enemy (the only eligible target), a boss, a set
// piece, a crate and a spawned enemy.
void lay_out_level(int enemy_slot, int enemy_key) {
    g_host->mobys = {
        {kHeroSlot, -1, 0, 1},
        {kLancerGunSlot, -1, 1490, 1},
        {kBlitzGunSlot, -1, 1489, 1},
        {enemy_slot, enemy_key, kEnemyClass, 0},
        {20, 200, 3000, 4},  // LISTED: a boss.
        {21, 201, 3001, 3},  // SCRIPTED.
        {22, 202, 500, 1},   // A crate: NOT_ENEMY.
        {23, -1, 3002, 2},   // Spawned: NOT_PLACED.
    };
    // Its own bolt drop: 20, a Standard enemy.
    poke<std::int16_t>(address_of(enemy_slot) + 0xB4, 20);
}

}  // namespace

int main(int argc, char** argv) {
    REQUIRE(argc == 2);
#if defined(_WIN32)
    HMODULE library = LoadLibraryA(argv[1]);
    REQUIRE(library != nullptr);
    auto query = reinterpret_cast<openrac_mod_query_fn>(GetProcAddress(library, "openrac_mod_query"));
    auto load = reinterpret_cast<int (*)(const openrac_api*, openrac_mod*)>(
        GetProcAddress(library, "openrac_mod_load")
    );
#else
    void* library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (library == nullptr) {
        std::fprintf(stderr, "%s\n", dlerror());
    }
    REQUIRE(library != nullptr);
    auto query = reinterpret_cast<openrac_mod_query_fn>(dlsym(library, "openrac_mod_query"));
    auto load =
        reinterpret_cast<int (*)(const openrac_api*, openrac_mod*)>(dlsym(library, "openrac_mod_load"));
#endif
    REQUIRE(query != nullptr && load != nullptr);

    openrac_mod_info info{};
    info.size = sizeof info;
    query(&info);
    REQUIRE(std::strcmp(info.id, "bounty-hunter") == 0);
    REQUIRE(info.api_minor == 29);

    Host host;
    g_host = &host;
    host.bools = {{"enabled", true},         {"enemy_tiers", true}, {"bounty_notifications", true},
                  {"contracts", true},       {"auto_equip", true},  {"wrench_in_pool", false},
                  {"require_ammo", true},    {"highlight", true},   {"target_notifications", true}};
    host.ints = {{"base_bounty", 10},  {"streak_seconds", 5},         {"max_streak", 5},
                 {"wrench_multiplier", 2}, {"contract_multiplier", 30}, {"fixed_hue", 210},
                 {"glow", 8}};
    host.strings = {{"hue_mode", "fixed"}};

    // The inventory: the Lancer (ammo 40 of 200) and the Blitz Gun (empty); the Zodiac untracked.
    poke<std::uint8_t>(kOwned + 30, 1);
    poke<std::int32_t>(kAmmo + 30 * 4, 40);
    poke<std::uint16_t>(kRecordBase + 30 * 0xE0 + 0x8E, 200);
    poke<std::uint8_t>(kOwned + 26, 1);
    poke<std::int32_t>(kAmmo + 26 * 4, 0);
    poke<std::uint16_t>(kRecordBase + 26 * 0xE0 + 0x8E, 30);
    poke<std::uint8_t>(kOwned + 43, 1);
    poke<std::int32_t>(kAmmo + 43 * 4, 5);

    openrac_api api{};
    api.size = sizeof api;
    api.api_major = OPENRAC_MOD_API_MAJOR;
    api.api_minor = 36;
    api.game = "rac2";
    api.version = "ntsc";
    api.openrac_version = "test";
    api.platform = "test";
    api.memory = host.memory.data();
    api.log = api_log;
    api.fail = api_fail;
    api.setting_bool = api_setting_bool;
    api.setting_int = api_setting_int;
    api.setting_float = api_setting_float;
    api.setting_string = api_setting_string;
    api.hook = api_hook;
    api.on_frame = api_on_frame;
    api.relocate_data = api_relocate;
    api.loaded_level = api_loaded_level;
    api.play_status = api_play_status;
    api.moby_list2 = api_moby_list2;
    api.moby_slot = api_moby_slot;
    api.moby_slot_of = api_moby_slot_of;
    api.figure_slot = api_figure_slot;
    api.hero_held = api_hero_held;
    api.bolts = api_bolts;
    api.hud_text_set = api_hud_text_set;
    api.on_overlay = api_on_overlay;
    api.moby_material = api_moby_material;
    api.moby_copy_check = api_moby_copy_check;
    api.on_weapon_fire = api_on_weapon_fire;
    api.on_shot_end = api_on_shot_end;
    api.weapon_record = api_weapon_record;
    api.hook_entry = api_hook_entry;

    openrac_mod* handle = reinterpret_cast<openrac_mod*>(&host);
    REQUIRE(load(&api, handle) == 0);
    REQUIRE(host.hooks.count("rac2.moby.record_death") == 1);
    REQUIRE(host.hooks.count("rac2.combat.count_kill") == 1);
    REQUIRE(host.frame != nullptr);

    // ---- Level 4: a contract on the one eligible enemy, for the Lancer ----
    std::puts("level 4: contract, paint, completion");
    lay_out_level(10, 100);
    level_load(4, false);
    tick();
    REQUIRE(host.equipped.size() == 1 && host.equipped[0] == 30);  // The Lancer, once.
    bool announced = false;
    for (const std::string& l : host.hud) {
        announced = announced || l.find("REQUIRED WEAPON: LANCER") != std::string::npos;
    }
    REQUIRE(announced);

    // Another enemy polls its hits: nothing painted. The target polls: painted, alone.
    poll(20);
    REQUIRE(host.materials.empty());
    poll(10);
    REQUIRE(host.materials.size() == 1);
    REQUIRE(host.materials[0].moby == address_of(10));
    REQUIRE(host.materials[0].material == (OPENRAC_MOBY_MATERIAL_PAINT | 8u << 8));
    REQUIRE(host.materials[0].rgb != 0);
    const std::uint32_t colour = host.materials[0].rgb;

    // The Lancer's gun moby hits it, then it dies: Standard bounty 10, contract 30 x 10.
    tick(10);
    hit(10, kLancerGunSlot);
    tick(10);
    die(10);
    REQUIRE(host.bolt_calls.size() == 2);
    REQUIRE(host.bolt_calls[0] == 10);
    REQUIRE(host.bolt_calls[1] == 300);
    REQUIRE(host.materials.back().moby == address_of(10));
    REQUIRE(host.materials.back().material == OPENRAC_MOBY_MATERIAL_NONE);  // Paint removed.

    // The death recorded again: no second payout of either.
    moby_in(10);  // (dead now)
    host.mobys[3].dead = false;
    die(10);
    REQUIRE(host.bolt_calls.size() == 2);

    // The crate and the boss die: the crate is not an enemy; the boss is, and pays its bounty.
    tick(400);  // Past the streak.
    die(22);
    REQUIRE(host.bolt_calls.size() == 2);
    die(20);
    REQUIRE(host.bolt_calls.size() == 3);
    REQUIRE(host.bolt_calls[2] == 5);  // Its drop is 0 here: Basic, 10 / 2.

    // A death's reload of the completed map: no new contract, no new equip.
    level_load(4, true);
    tick();
    REQUIRE(host.equipped.size() == 1);

    // ---- Level 5: wrong weapon ----
    std::puts("level 5: wrong weapon");
    host.materials.clear();
    host.bolt_calls.clear();
    lay_out_level(11, 110);
    level_load(5, false);
    tick();
    REQUIRE(host.equipped.size() == 2 && host.equipped[1] == 30);
    hit(11, kLancerGunSlot);
    tick(5);
    hit(11, kBlitzGunSlot);  // The last blow: the Blitz Gun's.
    tick(5);
    die(11);
    REQUIRE(host.bolt_calls.size() == 1);  // The bounty, no contract reward.
    REQUIRE(host.bolt_calls[0] == 10);
    tick();  // The HUD shows what happened at the next frame.
    bool failed = false;
    for (const std::string& l : host.hud) {
        failed = failed || l.find("WRONG WEAPON") != std::string::npos;
    }
    REQUIRE(failed);

    // ---- Level 6: kept through a death's reload, then an unverified kill ----
    std::puts("level 6: death reload keeps the contract; unattributed kill fails safe");
    host.materials.clear();
    host.bolt_calls.clear();
    lay_out_level(12, 120);
    level_load(6, false);
    tick();
    poll(12);
    REQUIRE(host.materials.size() == 1);
    // Ratchet dies; the level is made again with the target in another slot.
    lay_out_level(14, 120);
    level_load(6, true);
    tick();
    REQUIRE(host.equipped.size() == 3);  // Not equipped again.
    poll(14);
    REQUIRE(host.materials.size() == 2);
    REQUIRE(host.materials[1].moby == address_of(14));
    REQUIRE(host.materials[1].rgb == host.materials[0].rgb);  // Same colour: no reroll.
    hit(14, 23);  // The spawned enemy's attack: no weapon.
    tick(3);
    die(14);
    REQUIRE(host.bolt_calls.size() == 1 && host.bolt_calls[0] == 10);  // Bounty only.
    tick();
    failed = false;
    for (const std::string& l : host.hud) {
        failed = failed || l.find("NOT VERIFIED") != std::string::npos;
    }
    REQUIRE(failed);
    (void)colour;

    // ---- Level 7: nothing eligible ----
    std::puts("level 7: no eligible target");
    host.mobys = {{kHeroSlot, -1, 0, 1}, {22, 202, 500, 1}, {20, 200, 3000, 4}};
    level_load(7, false);
    tick();
    bool none = false;
    for (const std::string& l : host.hud) {
        none = none || l.find("NO BOUNTY CONTRACT") != std::string::npos;
    }
    REQUIRE(none);

    // ---- Level 8: the pause menu does not run a streak out ----
    std::puts("level 8: streak survives the pause menu");
    host.bolt_calls.clear();
    lay_out_level(10, 100);
    level_load(8, false);
    tick();
    die(20);  // The boss (drop 0: Basic, 5).
    host.phase = OPENRAC_PLAY_MENU;
    tick(1000);  // Far past the 5 s window, all of it paused.
    host.phase = OPENRAC_PLAY_PLAYING;
    tick();
    die(21);  // The set piece: the second kill of the streak.
    REQUIRE(host.bolt_calls.size() == 2);
    REQUIRE(host.bolt_calls[0] == 5);
    REQUIRE(host.bolt_calls[1] == 10);  // x2.

    std::puts("load_test: all passed");
    return 0;
}
