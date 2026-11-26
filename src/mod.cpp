// SPDX-License-Identifier: GPL-3.0-or-later
//
// Bounty Hunter for Ratchet & Clank: Going Commando on OpenRAC (README.md).
//
// The game reaches this file only through the SDK: hook points, callbacks and the openrac_api
// table. What it reads of game memory is listed in discs.h, each read named by the SDK's own
// documentation or by the per-disc globals the openrac-mods NG++ mod uses; it writes none.
//
//   Moby_RecordDeath (rac2.moby.record_death)  a death the game records: the bounty and the
//                                               contract's outcome
//   Moby_CountKill   (rac2.combat.count_kill)   inside it, the game's own "this was an enemy",
//                                               where the port has it (taken at run time)
//   Moby_PostHit     (rac2.combat.post_hit)     every hit: which weapon dealt it (rules.h)
//   Moby_GetHitMessage (rac2.combat.get_hit)    an enemy polling its hits: the target's address,
//                                               for its paint
//   Level_Load       (rac2.level.load)          a level loaded or resumed: the contract
//   on_weapon_fire, on_shot_end                 the shots and their weapons
//   on_frame, on_overlay                        checks, notices, the HUD and the marker

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <random>
#include <string>
#include <unordered_set>
#include <vector>

#include "discs.h"
#include "enemy_names.h"
#include "openrac/games/rac2.hpp"
#include "openrac/mod.hpp"
#include "rules.h"
#include "weapons.h"

namespace bounty_hunter {
namespace {

static_assert(kMaterialNone == OPENRAC_MOBY_MATERIAL_NONE, "material ids");
static_assert(kMaterialTint == OPENRAC_MOBY_MATERIAL_TINT, "material ids");
static_assert(kMaterialPaint == OPENRAC_MOBY_MATERIAL_PAINT, "material ids");
static_assert(OPENRAC_MOBY_MATERIAL_PAINT_AT(8) == (kMaterialPaint | 8u << 8), "paint levels");

// Moby_CountKill (rac2.combat.count_kill, void(u32 moby, i32 type)): SetDeathBits calls it with
// type 0 for a moby whose class is of the enemy kind, the bestiary's (rac2-decomp
// src/game/002A0430.c). OpenRAC lists it from SDK 1.37's binding table; this mod is built against
// the released 1.36 SDK, so the hook point is described here and asked for at run time: a port
// without it answers UNKNOWN and the mod falls back (the level's enemy classes, below).
struct CountKill {
    static constexpr const char* id = "rac2.combat.count_kill";
    static constexpr const char* name = "Moby_CountKill";
    static constexpr const char* signature = "void(u32,i32)";
    using fn = void (*)(std::uint32_t, std::int32_t);
};

constexpr int kMobySlots = 4096;
constexpr int kListRefreshesPerTick = 2;   // Extra moby list reads a hit may ask for each tick.
constexpr std::uint32_t kLostTicks = 30;   // Ticks a target must be missing to count as lost.
constexpr int kHueMinDistance = 60;

// ---- Settings ------------------------------------------------------------------------------

struct Settings {
    BountySettings bounty;
    bool bounty_notices = true;
    bool contracts = true;
    int contract_multiplier = 30;
    bool auto_equip = true;
    bool wrench_in_pool = false;
    bool require_ammo = true;
    bool highlight = true;
    bool fixed_hue = false;
    int hue = 210;
    int glow = 8;
    bool target_notices = true;
};

Settings read_settings(openrac::Mod& mod, int tick_rate) {
    Settings s;
    auto clamp_int = [&mod](const char* key, int lo, int hi) {
        return static_cast<int>(std::clamp<std::int64_t>(mod.setting<std::int64_t>(key), lo, hi));
    };
    s.bounty.base = clamp_int("base_bounty", 0, 1000);
    s.bounty.streak_ticks =
        static_cast<std::uint32_t>(clamp_int("streak_seconds", 1, 30) * tick_rate);
    s.bounty.max_streak = clamp_int("max_streak", 1, 10);
    s.bounty.wrench_multiplier = clamp_int("wrench_multiplier", 1, 10);
    s.bounty.tiers = mod.setting<bool>("enemy_tiers");
    s.bounty_notices = mod.setting<bool>("bounty_notifications");
    s.contracts = mod.setting<bool>("contracts");
    s.contract_multiplier = clamp_int("contract_multiplier", 1, 100);
    s.auto_equip = mod.setting<bool>("auto_equip");
    s.wrench_in_pool = mod.setting<bool>("wrench_in_pool");
    s.require_ammo = mod.setting<bool>("require_ammo");
    s.highlight = mod.setting<bool>("highlight");
    s.fixed_hue = mod.setting<std::string>("hue_mode") == "fixed";
    s.hue = clamp_int("fixed_hue", 0, 359);
    s.glow = clamp_int("glow", 0, kPaintGlowTop);
    s.target_notices = mod.setting<bool>("target_notifications");
    return s;
}

// ---- Text ----------------------------------------------------------------------------------

// The game's HUD font: printable ASCII less the glyphs it lacks or draws as button icons.
std::string hud_safe(const std::string& text) {
    std::string out;
    for (char c : text) {
        const bool bad = c < 32 || c > 126 || std::string_view("#%@&*=[]{}~").find(c)
                                                  != std::string_view::npos;
        out += bad ? ' ' : static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return out.substr(0, 63);
}

std::string enemy_label(int moby_class) {
    const char* name = enemy_name(moby_class);
    return name != nullptr ? std::string(name)
                           : "Unidentified hostile (class " + std::to_string(moby_class) + ")";
}

// ---- The mod's state -----------------------------------------------------------------------

struct Banner {
    std::vector<std::string> lines;
    std::uint32_t until = 0;
};

struct State {
    openrac::Mod* mod = nullptr;
    const openrac_api* api = nullptr;
    const Disc* disc = nullptr;
    Settings settings;
    std::mt19937 rng;

    std::uint32_t tick = 0;
    std::uint32_t loads = 0;
    int level = -1;
    int phase = OPENRAC_PLAY_NONE;

    // The level's mobys, read once a tick (and again for a hit whose source is new).
    std::vector<openrac_moby2> list;
    int list_count = 0;
    std::array<int, kMobySlots> class_of{};
    std::array<int, kMobySlots> key_of{};
    std::uint32_t list_tick = 0;
    int refreshes = 0;

    int hero_slot = -1;
    int hero_class = -1;
    int held_item = -1;
    int wrench_class = -1;

    // Kills and hits.
    Streak streak;
    PaidKills paid;
    ShotTracker shots;
    HitLedger ledger;
    bool have_count_kill = false;
    std::unordered_set<int> enemy_classes;  // The fallback without Moby_CountKill.
    std::uint32_t dying = 0;                // The moby Moby_RecordDeath is recording now.
    bool dying_counted = false;

    // The contract.
    ContractBook book;
    TargetPaint paint;
    std::uint32_t target_address = 0;  // Seen through a hook point, checked by moby_slot_of.
    bool tier_known = false;
    std::uint32_t missing_since = 0;
    bool missing = false;
    int last_hue = -1;
    int (*equip)(std::int32_t) = nullptr;
    std::uint32_t material = kMaterialNone;

    // Notices.
    Banner banner;
    std::string popup;
    std::uint32_t popup_until = 0;
};

State& state() {
    static State s;
    return s;
}

// ---- Game reads ----------------------------------------------------------------------------

bool guest_ok(std::uint32_t address, std::uint32_t bytes) {
    return address != 0 && address < kGuestMemory - bytes;
}

void refresh_list(State& s) {
    const int max = static_cast<int>(s.list.size());
    for (openrac_moby2& m : s.list) {
        m.size = sizeof(openrac_moby2);
    }
    const int count = std::min(s.api->moby_list2(s.list.data(), max), max);
    s.class_of.fill(-1);
    s.key_of.fill(-1);
    for (int i = 0; i < count; ++i) {
        const openrac_moby2& m = s.list[static_cast<std::size_t>(i)];
        if (m.slot < 0 || m.slot >= kMobySlots || (m.flags & OPENRAC_MOBY_DEAD) != 0) {
            continue;
        }
        s.class_of[static_cast<std::size_t>(m.slot)] = m.oclass;
        s.key_of[static_cast<std::size_t>(m.slot)] = m.key;
    }
    s.list_count = count;
}

int class_at(State& s, int slot) {
    if (slot < 0 || slot >= kMobySlots) {
        return -1;
    }
    int found = s.class_of[static_cast<std::size_t>(slot)];
    // Something made since the list was read (a blast, a shot this tick): read it again, a few
    // times a tick at most.
    if (found < 0 && s.refreshes < kListRefreshesPerTick) {
        ++s.refreshes;
        refresh_list(s);
        found = s.class_of[static_cast<std::size_t>(slot)];
    }
    return found;
}

int slot_of(const State& s, std::uint32_t address) {
    return guest_ok(address, 0x100u) ? s.api->moby_slot_of(address) : -1;
}

int own_bolts(const State& s, std::uint32_t moby) {
    if (!guest_ok(moby, kMobyOwnBolts + 2)) {
        return -1;
    }
    return s.mod->memory().read<std::int16_t>(moby + kMobyOwnBolts);
}

std::vector<OwnedWeapon> inventory(const State& s) {
    std::vector<OwnedWeapon> out;
    const openrac::Memory memory = s.mod->memory();
    const std::uint32_t owned = s.api->relocate_data(s.disc->owned);
    const std::uint32_t ammo = s.api->relocate_data(s.disc->ammo);
    for (const WeaponInfo& weapon : kWeapons) {
        OwnedWeapon w;
        w.item = weapon.item;
        w.owned = memory.read<std::uint8_t>(owned + static_cast<std::uint32_t>(weapon.item)) != 0;
        w.ammo = memory.read<std::int32_t>(ammo + 4u * static_cast<std::uint32_t>(weapon.item));
        const openrac_gaddr record = s.api->weapon_record(s.mod->handle(), weapon.item);
        w.capacity = guest_ok(record, kRecordAmmoMax + 2)
                         ? memory.read<std::uint16_t>(record + kRecordAmmoMax)
                         : -1;
        // The wrench is always Ratchet's, whatever its byte says.
        if (weapon.item == kWrenchItem) {
            w.owned = true;
            w.capacity = 0;
        }
        out.push_back(w);
    }
    return out;
}

// ---- Notices -------------------------------------------------------------------------------

void show_banner(State& s, std::vector<std::string> lines, int seconds) {
    if (!s.settings.target_notices) {
        return;
    }
    s.banner.lines = std::move(lines);
    s.banner.until = s.tick + static_cast<std::uint32_t>(seconds * s.disc->tick_rate);
}

void show_popup(State& s, const std::string& text) {
    if (!s.settings.bounty_notices) {
        return;
    }
    s.popup = text;
    s.popup_until = s.tick + static_cast<std::uint32_t>(2 * s.disc->tick_rate);
}

std::string reward_text(const State& s) {
    const Contract& c = s.book.contract();
    if (!s.settings.bounty.tiers || s.tier_known) {
        return std::to_string(c.reward) + " BOLTS";
    }
    return std::to_string(s.settings.contract_multiplier) + "X BOUNTY";
}

openrac_hud_text line(const std::string& text, float x, float y, float scale, std::uint32_t rgb) {
    openrac_hud_text out{};
    const std::string safe = hud_safe(text);
    std::copy(safe.begin(), safe.end(), out.text);
    out.x = x;
    out.y = y;
    out.scale = scale;
    // 128 a channel is the font's own shade: the hue at about the HUD's brightness.
    out.rgba[0] = static_cast<std::uint8_t>(64 + ((rgb >> 16) & 0xFF) / 2);
    out.rgba[1] = static_cast<std::uint8_t>(64 + ((rgb >> 8) & 0xFF) / 2);
    out.rgba[2] = static_cast<std::uint8_t>(64 + (rgb & 0xFF) / 2);
    out.rgba[3] = 255;
    out.flags = OPENRAC_HUD_SHADOW | (scale < 0.9f ? OPENRAC_HUD_SMALL : 0u);
    return out;
}

void update_hud(State& s) {
    std::vector<openrac_hud_text> lines;
    constexpr std::uint32_t kWhite = 0x808080u;  // The font's own shade.
    constexpr std::uint32_t kGold = 0xFFD040u;

    if (s.tick < s.banner.until) {
        float y = 0.30f;
        for (const std::string& text : s.banner.lines) {
            lines.push_back(line(text, 0.5f, y, 1.0f, s.book.contract().rgb ? s.book.contract().rgb
                                                                             : kGold));
            y += 0.05f;
        }
    }
    if (s.tick < s.popup_until) {
        lines.push_back(line(s.popup, 0.5f, 0.22f, 0.9f, kGold));
    }

    const Contract& c = s.book.contract();
    if (s.settings.target_notices && s.book.decided() && c.status != ContractStatus::None) {
        const char* status = c.status == ContractStatus::Active      ? "ACTIVE"
                             : c.status == ContractStatus::Completed ? "COMPLETE"
                                                                      : "FAILED";
        lines.push_back(line("WANTED: " + enemy_label(c.moby_class), 0.80f, 0.12f, 0.7f, c.rgb));
        lines.push_back(line(
            std::string(weapon_name(c.weapon)) + " - " + reward_text(s), 0.80f, 0.16f, 0.7f, kWhite
        ));
        lines.push_back(line(std::string("CONTRACT ") + status, 0.80f, 0.20f, 0.7f, kWhite));
    }
    s.api->hud_text_set(s.mod->handle(), lines.data(), static_cast<int>(lines.size()));
}

// ---- The contract --------------------------------------------------------------------------

void apply(State& s, const PaintOp& op) {
    if (op.address == 0) {
        return;
    }
    const int result = s.api->moby_material(s.mod->handle(), op.address, op.material, op.rgb);
    if (result != OPENRAC_HOOK_OK && op.material != kMaterialNone) {
        s.paint.refused();
    }
}

void set_reward(State& s, int bolts) {
    Contract& c = s.book.contract();
    c.tier = s.settings.bounty.tiers ? tier_of_bolts(bolts) : Tier::Standard;
    c.reward = s.settings.contract_multiplier * tier_bounty(s.settings.bounty, c.tier);
}

// Whether the contract still wants to see its target's address: to read its tier, or to paint it.
bool wants_sighting(const State& s) {
    if (s.book.contract().status != ContractStatus::Active) {
        return false;
    }
    const bool paint_due =
        s.settings.highlight && s.material != kMaterialNone && !s.paint.painted();
    return s.target_address == 0 || paint_due;
}

// A moby the hook points handed over, at `address`: when it is the target, keep its address and
// paint it.
void sighted(State& s, std::uint32_t address) {
    const Contract& c = s.book.contract();
    if (!wants_sighting(s)) {
        return;
    }
    const int slot = slot_of(s, address);
    if (slot < 0 || slot != c.slot || class_at(s, slot) != c.moby_class
        || s.key_of[static_cast<std::size_t>(slot)] != c.key) {
        return;
    }
    s.target_address = address;
    if (!s.tier_known) {
        set_reward(s, own_bolts(s, address));
        s.tier_known = true;
    }
    if (s.settings.highlight) {
        apply(s, s.paint.seen(address, slot, c.moby_class));
    }
}

void end_contract_visuals(State& s) {
    apply(s, s.paint.release());
    s.target_address = 0;
}

void start_contract(State& s, const std::vector<Candidate>& candidates) {
    const int pick =
        pick_target(candidates, [](int c) { return enemy_name(c) != nullptr; }, s.rng);
    if (pick < 0) {
        s.book.none_available(s.level);
        show_banner(s, {"NO BOUNTY CONTRACT ON THIS MAP", "NO SAFE TARGET FOUND"}, 4);
        s.mod->info("no eligible contract target in level " + std::to_string(s.level));
        return;
    }
    const std::vector<int> pool =
        weapon_pool(inventory(s), s.settings.wrench_in_pool, s.settings.require_ammo);
    if (pool.empty()) {
        s.book.none_available(s.level);
        show_banner(s, {"NO BOUNTY CONTRACT ON THIS MAP", "NO TRACKABLE WEAPON READY"}, 4);
        s.mod->info("no weapon for a contract in level " + std::to_string(s.level));
        return;
    }
    std::uniform_int_distribution<std::size_t> weapon_pick(0, pool.size() - 1);

    const Candidate& target = candidates[static_cast<std::size_t>(pick)];
    Contract c;
    c.level = s.level;
    c.key = target.key;
    c.slot = target.slot;
    c.moby_class = target.moby_class;
    c.weapon = pool[weapon_pick(s.rng)];
    c.hue = s.settings.fixed_hue ? s.settings.hue : pick_hue(s.rng, s.last_hue, kHueMinDistance);
    c.rgb = hsv_to_rgb(c.hue, kTargetSaturation, kTargetValue);
    s.last_hue = c.hue;
    s.book.start(c);
    s.tier_known = false;
    set_reward(s, -1);
    apply(
        s,
        s.paint.aim(c.slot, c.moby_class, s.settings.highlight ? s.material : kMaterialNone, c.rgb)
    );
    s.mod->info(
        "contract: level " + std::to_string(s.level) + ", key " + std::to_string(c.key)
        + ", class " + std::to_string(c.moby_class) + ", weapon " + weapon_name(c.weapon)
        + ", hue " + std::to_string(c.hue)
    );
}

void announce(State& s) {
    Contract& c = s.book.contract();
    if (c.announced || c.status != ContractStatus::Active) {
        return;
    }
    c.announced = true;
    show_banner(
        s,
        {"NEW BOUNTY CONTRACT",
         "WANTED: " + enemy_label(c.moby_class),
         std::string("REQUIRED WEAPON: ") + weapon_name(c.weapon),
         "CONTRACT REWARD: " + reward_text(s)},
        6
    );
    if (s.settings.auto_equip && !c.equipped && s.hero_class != kClankHeroClass) {
        c.equipped = true;
        if (s.equip != nullptr) {
            s.equip(c.weapon);
        }
    }
}

void target_died(State& s, std::uint32_t moby, const FatalResult& fatal) {
    Contract& c = s.book.contract();
    if (!s.tier_known) {
        set_reward(s, own_bolts(s, moby));
        s.tier_known = true;
    }
    // Clear the paint while the moby is still the target's.
    end_contract_visuals(s);
    const ContractStatus now = s.book.target_died(fatal);
    if (now == ContractStatus::Completed) {
        const int reward = s.book.take_reward();
        s.api->bolts(reward);
        show_banner(s, {"CONTRACT COMPLETE! +" + std::to_string(reward) + " BOLTS"}, 5);
        s.mod->info("contract complete: +" + std::to_string(reward) + " bolts");
    } else if (now == ContractStatus::Failed) {
        const bool wrong = c.reason == FailReason::WrongWeapon;
        show_banner(
            s,
            {wrong ? "CONTRACT FAILED - WRONG WEAPON" : "CONTRACT FAILED - KILL NOT VERIFIED"},
            5
        );
        s.mod->info(
            std::string("contract failed: ")
            + (wrong ? std::string("killed with ") + weapon_name(fatal.item)
                     : "the killing blow could not be laid to one weapon")
        );
    }
}

void level_loaded(State& s, bool resume) {
    openrac_play_status status{};
    status.size = sizeof status;
    s.api->play_status(&status);
    s.level = status.level >= 0 ? status.level : s.api->loaded_level();
    s.loads = status.loads;

    s.shots.clear();
    s.ledger.clear();
    s.paid.reset();
    s.streak.reset();
    s.paint.level_changed();
    s.target_address = 0;
    s.missing = false;
    s.refreshes = 0;
    refresh_list(s);

    // The level's placed mobys, judged as moby_copy_check judges them now, while the level's own
    // data is there (openrac_mod.h: ask as rac2.level.load returns).
    std::vector<Candidate> candidates;
    s.enemy_classes.clear();
    for (int i = 0; i < s.list_count; ++i) {
        const openrac_moby2& m = s.list[static_cast<std::size_t>(i)];
        if (m.key < 0) {
            continue;
        }
        Candidate c;
        c.slot = m.slot;
        c.key = m.key;
        c.moby_class = m.oclass;
        c.dead = (m.flags & OPENRAC_MOBY_DEAD) != 0;
        c.scene = (m.flags & OPENRAC_MOBY_SCENE) != 0;
        if (!c.dead) {
            const int answer = s.api->moby_copy_check(s.mod->handle(), m.slot);
            c.copy_answer = answer >= 0 ? answer : -1;
            // Every answer past NOT_ENEMY is of an enemy's class (the port's judge checks the
            // class's kind first, after a dead or free moby's NOT_PLACED).
            if (answer == kCopyOk || answer == kCopyScripted || answer == kCopyListed
                || answer == kCopyNoRoom) {
                s.enemy_classes.insert(m.oclass);
            }
        }
        candidates.push_back(c);
    }

    const VisitChange change = s.book.level_loaded(s.level, resume);
    Contract& c = s.book.contract();
    if (change == VisitChange::Continue) {
        // The same contract: find its target by key in this load.
        if (c.status == ContractStatus::Active) {
            c.slot = s.api->moby_slot(c.key);
            s.paint.moved(c.slot);
            s.mod->info("contract kept through the reload (key " + std::to_string(c.key) + ")");
        }
        return;
    }
    s.paint.release();  // The old level's paint went with it; forget the target.
    if (s.settings.contracts) {
        start_contract(s, candidates);
    }
}

// ---- Hook points and callbacks -------------------------------------------------------------

void on_fire(void* user, const openrac_weapon_shot* shot) {
    State& s = *static_cast<State*>(user);
    if (shot != nullptr && shot->slot >= 0) {
        s.shots.fired(shot->slot, shot->oclass, shot->item);
    }
}

void on_end(void* user, const openrac_shot_end* end) {
    State& s = *static_cast<State*>(user);
    if (end != nullptr && end->slot >= 0) {
        s.shots.ended(end->slot);
    }
}

void post_hit(State& s, std::uint32_t victim, std::uint32_t request) {
    if (!guest_ok(request, kRequestMask + 4)) {
        return;
    }
    const openrac::Memory memory = s.mod->memory();
    const std::uint32_t source = memory.read<std::uint32_t>(request + kRequestSource);
    const std::uint32_t mask = memory.read<std::uint32_t>(request + kRequestMask);
    const int victim_slot = slot_of(s, victim);
    if (victim_slot < 0 || victim_slot == s.hero_slot) {
        return;
    }
    sighted(s, victim);

    HitSource src;
    src.slot = slot_of(s, source);
    src.moby_class = class_at(s, src.slot);
    src.mask = mask;
    src.hero_slot = s.hero_slot;
    src.hero_class = s.hero_class;
    src.held_item = s.held_item;
    src.wrench_class = s.wrench_class;
    s.ledger.record(victim_slot, classify_hit(src, s.shots), s.tick);
}

void record_death(
    State& s,
    openrac::Original<void(std::uint32_t, std::int32_t, std::int32_t)> original,
    std::uint32_t moby,
    std::int32_t flags,
    std::int32_t arg
) {
    const int slot = slot_of(s, moby);
    const int key = slot >= 0 ? s.key_of[static_cast<std::size_t>(slot)] : -1;
    const int moby_class = class_at(s, slot);
    const int bolts = own_bolts(s, moby);

    s.dying = moby;
    s.dying_counted = false;
    original(moby, flags, arg);
    s.dying = 0;

    if (slot < 0) {
        return;
    }
    const bool enemy = s.have_count_kill
                           ? s.dying_counted
                           : key >= 0 && s.enemy_classes.count(moby_class) != 0;
    const FatalResult fatal = s.ledger.fatal(slot, s.tick);
    s.ledger.forget(slot);

    // The bounty: once per placed enemy and level load.
    // Keyed by the record and the slot: a death recorded twice is paid once, and an enemy copy
    // (another mod's moby_copy, which shares its original's record) is still its own kill.
    if (enemy && key >= 0 && s.paid.first(key * kMobySlots + slot)) {
        const bool wrench = fatal.kind == Fatal::Verified && fatal.item == kWrenchItem;
        const int times = s.streak.kill(s.tick, s.settings.bounty);
        const Payout pay = compute_payout(s.settings.bounty, tier_of_bolts(bolts), times, wrench);
        if (pay.total > 0) {
            s.api->bolts(pay.total);
            std::string text = "BOUNTY +" + std::to_string(pay.total) + " BOLTS";
            if (pay.streak > 1) {
                text += "  X" + std::to_string(pay.streak) + " STREAK";
            }
            if (wrench) {
                text += "  WRENCH BONUS";
            }
            show_popup(s, text);
        }
    }

    // The contract.
    const Contract& c = s.book.contract();
    if (c.status == ContractStatus::Active && slot == c.slot && key == c.key) {
        target_died(s, moby, fatal);
    }
}

void frame(State& s) {
    ++s.tick;
    s.refreshes = 0;

    openrac_play_status status{};
    status.size = sizeof status;
    s.api->play_status(&status);
    s.phase = status.phase;
    const bool in_level = status.phase >= OPENRAC_PLAY_PLAYING && status.level >= 0;
    if (!in_level) {
        s.api->hud_text_set(s.mod->handle(), nullptr, 0);
        return;
    }
    refresh_list(s);

    s.hero_slot = s.api->figure_slot(s.mod->handle(), -1);
    s.hero_class = s.hero_slot >= 0 && s.hero_slot < kMobySlots
                       ? s.class_of[static_cast<std::size_t>(s.hero_slot)]
                       : -1;
    openrac_held held{};
    held.size = sizeof held;
    s.held_item = -1;
    if (s.api->hero_held(&held) == OPENRAC_HOOK_OK && held.count > 0) {
        s.held_item = held.pieces[0].item;
        if (held.pieces[0].item == kWrenchItem && held.pieces[0].oclass >= 0) {
            s.wrench_class = held.pieces[0].oclass;
        }
    }
    if (s.tick % 60u == 0) {
        s.ledger.prune(s.tick);
    }

    // The target: still there, in the slot its key names?
    Contract& c = s.book.contract();
    if (c.status == ContractStatus::Active && status.loads == s.loads) {
        const bool there = c.slot >= 0 && s.key_of[static_cast<std::size_t>(c.slot)] == c.key
                           && s.class_of[static_cast<std::size_t>(c.slot)] == c.moby_class;
        if (!there) {
            const int slot = s.api->moby_slot(c.key);
            if (slot >= 0) {
                c.slot = slot;
                s.paint.moved(slot);
                s.target_address = 0;
                s.missing = false;
            } else if (!s.missing) {
                s.missing = true;
                s.missing_since = s.tick;
            } else if (s.tick - s.missing_since > kLostTicks) {
                end_contract_visuals(s);
                s.book.target_lost();
                show_banner(s, {"CONTRACT FAILED - TARGET LOST"}, 5);
                s.mod->info("contract failed: the target left the level without a recorded death");
            }
        } else {
            s.missing = false;
        }
    }
    if (status.phase == OPENRAC_PLAY_PLAYING) {
        announce(s);
    }
    update_hud(s);
}

void overlay(void* user, const openrac_overlay* overlay) {
    State& s = *static_cast<State*>(user);
    const Contract& c = s.book.contract();
    if (overlay == nullptr || !s.settings.highlight || c.status != ContractStatus::Active
        || s.paint.painted() || c.slot < 0) {
        return;
    }
    // The marker: only while the paint is not on (no port paint, or the address not seen yet).
    for (int i = 0; i < s.list_count; ++i) {
        const openrac_moby2& m = s.list[static_cast<std::size_t>(i)];
        if (m.slot != c.slot || m.key != c.key) {
            continue;
        }
        const float above[3] = {m.position[0], m.position[1], m.position[2] + 2.5f};
        float x = 0.0f;
        float y = 0.0f;
        if (overlay->project(overlay->context, above, &x, &y) == 0) {
            return;
        }
        const std::uint32_t rgba = c.rgb << 8 | 0xE0u;
        const char* text = "WANTED";
        const float height = 12.0f;
        const float width = overlay->text_width(overlay->context, height, text);
        overlay->rect(overlay->context, x - 3.0f, y - 3.0f, 6.0f, 6.0f, rgba);
        overlay->text(overlay->context, x - width / 2.0f, y - height - 6.0f, height, rgba, text);
        return;
    }
}

// ---- Load ------------------------------------------------------------------------------------

void load(openrac::Mod& mod) {
    if (mod.game() != "rac2") {
        throw openrac::Error("Bounty Hunter is for Ratchet & Clank: Going Commando");
    }
    State& s = state();
    s.mod = &mod;
    s.api = &mod.api();
    s.disc = disc_of(s.api->version);
    if (s.disc == nullptr) {
        throw openrac::Error(std::string("Bounty Hunter does not know disc ") + s.api->version);
    }
    if (s.api->api_minor < 29) {
        throw openrac::Error("Bounty Hunter needs OpenRAC's mod interface 1.29 or newer");
    }
    if (!mod.setting<bool>("enabled")) {
        mod.info("switched off in its settings");
        return;
    }
    s.settings = read_settings(mod, s.disc->tick_rate);
    s.list.resize(kMobySlots);
    s.class_of.fill(-1);
    s.key_of.fill(-1);
    s.rng.seed(static_cast<std::mt19937::result_type>(
        std::random_device{}() ^ static_cast<unsigned>(
            std::chrono::steady_clock::now().time_since_epoch().count()
        )
    ));
    s.material = target_material(s.api->api_minor, s.settings.glow);

    // The hook points every port of interface 1.29 has on the three discs.
    mod.hook<openrac::rac2::Moby_RecordDeath>(
        [](auto original, std::uint32_t moby, std::int32_t flags, std::int32_t arg) {
            record_death(state(), original, moby, flags, arg);
        }
    );
    mod.hook<openrac::rac2::Moby_PostHit>([](auto original, std::uint32_t victim,
                                             std::uint32_t request) {
        // Read the request's source before the original (a caller may reuse it after).
        post_hit(state(), victim, request);
        original(victim, request);
    });
    mod.hook<openrac::rac2::Moby_GetHitMessage>(
        [](auto original, std::uint32_t moby, std::uint32_t mask, std::int32_t keep) {
            State& st = state();
            if (wants_sighting(st)) {
                sighted(st, moby);
            }
            return original(moby, mask, keep);
        }
    );
    mod.hook<openrac::rac2::Level_Load>([](auto original, std::int32_t load, std::int32_t resume) {
        const std::int32_t next = original(load, resume);
        level_loaded(state(), resume != 0);
        return next;
    });

    // The game's own enemy test, where this port has it.
    try {
        mod.hook<CountKill>([](auto original, std::uint32_t moby, std::int32_t type) {
            State& st = state();
            if (st.dying != 0 && moby == st.dying && type == 0) {
                st.dying_counted = true;
            }
            original(moby, type);
        });
        s.have_count_kill = true;
    } catch (const openrac::Error& e) {
        mod.info(
            std::string("rac2.combat.count_kill unavailable (") + e.what()
            + "): enemies are the level's enemy classes instead"
        );
    }

    if (!mod.on_weapon_fire(&on_fire, &s) || !mod.on_shot_end(&on_end, &s)) {
        mod.warn("no weapon shot reports: projectile hits are told by their class alone");
    }
    s.equip = mod.hook_entry<openrac::rac2::Item_Equip>();
    if (s.equip == nullptr) {
        mod.info("rac2.item.equip has no entry here: the required weapon is not equipped for you");
    }
    mod.on_frame([] { frame(state()); });
    if (!mod.on_overlay(&overlay, &s)) {
        mod.info("no overlay: the wanted marker is not drawn");
    }

    mod.info(
        std::string("loaded on ") + s.api->version + ", interface 1."
        + std::to_string(s.api->api_minor) + ", target "
        + (s.material == kMaterialNone   ? "marker only"
           : s.material == kMaterialTint ? "tint"
                                         : "paint")
    );
}

}  // namespace
}  // namespace bounty_hunter

OPENRAC_MOD_CPP("bounty-hunter", "1.0.0", bounty_hunter::load)
