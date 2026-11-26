// SPDX-License-Identifier: GPL-3.0-or-later
//
// Bounty Hunter's rules, free of the game and the SDK so the tests can run them alone: what a kill
// pays (its tier, the streak, the wrench), which hit dealt a killing blow, which enemy and weapon a
// contract names, its colour, and how a contract lives through a level's reloads.

#pragma once

#include <cstdint>
#include <random>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace bounty_hunter {

// ---- General bounty ------------------------------------------------------------------------

// The launcher's bounty settings, with the game's tick rate already applied.
struct BountySettings {
    int base = 10;               // Bolts for a Standard enemy.
    std::uint32_t streak_ticks = 300;  // How long a streak waits for the next kill.
    int max_streak = 5;          // The highest streak multiplier.
    int wrench_multiplier = 2;   // Times the bounty for a verified wrench kill.
    bool tiers = true;           // Scale the bounty by the enemy's tier.
};

// How tough an enemy is, judged from the bolts the level gives it (its own drop, the s16 the SDK
// documents at +0xB4 of the moby in Moby_RecordDeath's description).
enum class Tier : std::uint8_t { Basic, Standard, Advanced, Elite };

// The drop at or below which an enemy is that tier (Elite above the last).
inline constexpr int kBasicMaxBolts = 10;
inline constexpr int kStandardMaxBolts = 30;
inline constexpr int kAdvancedMaxBolts = 75;

// Returns the tier of an enemy whose own drop is `bolts` (-1: unknown, read as Standard).
Tier tier_of_bolts(int bolts);

// Returns the tier's name ("Basic", ...).
const char* tier_name(Tier tier);

// Returns the bounty of a kill of `tier`, before the streak and the wrench: half the base for
// Basic (at least 1 when the base is), the base for Standard, twice it Advanced, four times Elite.
// With tiers off, always the base.
int tier_bounty(const BountySettings& settings, Tier tier);

// The kill streak: each kill within the window of the one before raises the multiplier by one, up
// to the most; a kill after the window starts again at x1.
class Streak {
public:
    // A kill at `tick`: returns its multiplier.
    int kill(std::uint32_t tick, const BountySettings& settings);

    // Returns the multiplier a kill at `tick` would get now without counting it (x1 when lapsed).
    int current(std::uint32_t tick, const BountySettings& settings) const;

    // Returns whether a streak is running at `tick` (more than one kill and still in its window).
    bool active(std::uint32_t tick, const BountySettings& settings) const;

    // Forgets the streak.
    void reset() { m_count = 0; }

private:
    int m_count = 0;
    std::uint32_t m_last = 0;
};

// What a kill pays: the tier's bounty, times the streak, times the wrench multiplier for a
// verified wrench kill.
struct Payout {
    int tier_bounty = 0;
    int streak = 1;
    bool wrench = false;
    int total = 0;
};

Payout compute_payout(const BountySettings& settings, Tier tier, int streak, bool wrench);

// The kills already paid in one level load, by the moby's stable key (its record in the level's
// file), so a death the game records twice is paid once.
class PaidKills {
public:
    // Returns true the first time `key` is seen since the last reset; false after.
    bool first(int key) { return m_paid.insert(key).second; }

    void reset() { m_paid.clear(); }

private:
    std::unordered_set<int> m_paid;
};

// ---- Weapon attribution ---------------------------------------------------------------------

// The shots on_weapon_fire reported and on_shot_end has not ended, by their moby's slot.
class ShotTracker {
public:
    void fired(int slot, int moby_class, int item);
    void ended(int slot);
    void clear() { m_shots.clear(); }

    // Returns the item of the live shot in `slot` of class `moby_class`, or -1 (none, or the slot
    // now holds another class: the shot ended unseen and the slot went to another moby).
    int item_of(int slot, int moby_class) const;

    std::size_t size() const { return m_shots.size(); }

private:
    struct Shot {
        int moby_class;
        int item;
    };
    std::unordered_map<int, Shot> m_shots;
};

// What the classifier knows of a hit's source.
struct HitSource {
    int slot = -1;              // The source moby's slot, -1 for none.
    int moby_class = -1;        // Its class, -1 unknown.
    std::uint32_t mask = 0;     // The hit's weapon or attack mask.
    int hero_slot = -1;         // The hero's moby slot.
    int hero_class = -1;        // The hero's moby class (Clank's is kClankHeroClass).
    int held_item = -1;         // The item in the hero's hand (only ever used to refuse a guess).
    int wrench_class = -1;      // The wrench moby's class, -1 not seen yet.
};

// Returns the weapon item that dealt a hit, or -1 when it cannot be told for certain. In order: a
// shot on_weapon_fire reported in that slot; a melee strike from the hero's own moby (Ratchet, not
// Clank, with no Walloper in hand); the wrench moby; a class that only one weapon makes. Never the
// weapon in hand alone.
int classify_hit(const HitSource& source, const ShotTracker& shots);

// How a death's killing blow is known.
enum class Fatal : std::uint8_t {
    Verified,   // Every hit of the final burst came from one weapon.
    Ambiguous,  // The final burst mixed weapons, or held a hit of unknown source.
    None,       // No hit seen in the window before the death.
};

struct FatalResult {
    Fatal kind = Fatal::None;
    int item = -1;  // The weapon, when Verified.
};

// The hits posted to each moby, by slot, kept long enough to name a death's killing blow.
class HitLedger {
public:
    // The game keeps a moby's pending hit until its update reads it, and a weaker one posted
    // meanwhile only adds its mask: any hit within this many ticks of the last one may be the one
    // the victim reads, so all of them must agree.
    static constexpr std::uint32_t kBurstTicks = 2;

    // A death is laid to a hit at most this many ticks old (an enemy's death animation can run
    // several seconds before the game records it).
    static constexpr std::uint32_t kWindowTicks = 900;

    void record(int victim_slot, int item, std::uint32_t tick);

    // The killing blow of the moby in `victim_slot`, dead at `tick`.
    FatalResult fatal(int victim_slot, std::uint32_t tick) const;

    // Forgets a slot's hits (its moby died) or every hit (another level load).
    void forget(int victim_slot) { m_hits.erase(victim_slot); }
    void clear() { m_hits.clear(); }

    // Drops hits older than the window.
    void prune(std::uint32_t tick);

private:
    struct Hit {
        std::uint32_t tick;
        int item;
    };
    std::unordered_map<int, std::vector<Hit>> m_hits;
};

// ---- Contracts --------------------------------------------------------------------------------

// What OpenRAC's moby_copy_check answers (openrac_mod.h's OPENRAC_COPY_*), repeated so the rules
// need no SDK header.
enum CopyAnswer : int {
    kCopyOk = 0,
    kCopyNotEnemy = 1,
    kCopyNotPlaced = 2,
    kCopyScripted = 3,
    kCopyListed = 4,
    kCopyNoRoom = 5,
};

// A placed moby of a level being judged as a target.
struct Candidate {
    int slot = -1;
    int key = -1;
    int moby_class = -1;
    int copy_answer = -1;  // moby_copy_check's answer, -1 when it refused to say.
    bool dead = false;     // OPENRAC_MOBY_DEAD.
    bool scene = false;    // OPENRAC_MOBY_SCENE.
};

// Returns whether a candidate may be a contract's target: a live, placed, rewarded enemy that the
// level's scripts do not need (moby_copy_check OK, or NO_ROOM, whose only fault is that a copy
// would be too large). Bosses, turrets and spawners (LISTED), set pieces (SCRIPTED), spawned or
// rewardless mobys (NOT_PLACED) and non-enemies are not.
bool eligible_target(const Candidate& candidate);

// Picks a target among `candidates`: an eligible one with a known name if any (`named`), else any
// eligible one, uniformly. Returns its index, or -1 for none eligible.
template <class Named>
int pick_target(const std::vector<Candidate>& candidates, Named named, std::mt19937& rng);

// A weapon of the player's, as the inventory says.
struct OwnedWeapon {
    int item = -1;
    bool owned = false;
    int ammo = 0;
    int capacity = -1;  // The most ammo it holds: 0 endless, -1 unknown.
};

// The weapons a contract may require: owned, trackable (contract_trackable), the wrench only when
// `include_wrench`, and with `require_ammo` only those with rounds left or endless ammo.
std::vector<int> weapon_pool(
    const std::vector<OwnedWeapon>& owned, bool include_wrench, bool require_ammo
);

// HSV (hue in degrees, saturation and value 0 to 1) to 0xRRGGBB.
std::uint32_t hsv_to_rgb(int hue, float saturation, float value);

// Returns the angle between two hues, 0 to 180.
int hue_distance(int a, int b);

// A random hue at least `min_distance` from `previous` (-1: none) where a few tries find one.
int pick_hue(std::mt19937& rng, int previous, int min_distance = 60);

// The wanted target's paint: saturated and bright.
inline constexpr float kTargetSaturation = 0.9f;
inline constexpr float kTargetValue = 1.0f;

enum class ContractStatus : std::uint8_t {
    None,       // No contract on this visit (contracts off, or nothing eligible).
    Active,
    Completed,
    Failed,
};

enum class FailReason : std::uint8_t {
    None,
    WrongWeapon,
    Unverified,  // The killing blow could not be laid to one weapon.
    TargetLost,  // The target left the level without a recorded death.
};

// A level visit's contract.
struct Contract {
    int level = -1;
    ContractStatus status = ContractStatus::None;
    FailReason reason = FailReason::None;
    int key = -1;          // The target's stable key.
    int slot = -1;         // Its slot in the current level load.
    int moby_class = -1;
    int weapon = -1;       // The required weapon's item.
    int hue = 0;
    std::uint32_t rgb = 0;
    Tier tier = Tier::Standard;
    int reward = 0;        // Bolts on completion.
    bool paid = false;
    bool announced = false;
    bool equipped = false;
};

// How a level load relates to the contract held.
enum class VisitChange : std::uint8_t {
    Continue,  // A death's reload or another load of the same level: keep the contract.
    NewVisit,  // Another level, or the first: a fresh contract.
};

// Returns how a load of `level` (resumed after a death when `resume`) relates to the last level
// loaded (`last_level`, -1 none).
VisitChange visit_change(int last_level, int level, bool resume);

// The contract's state machine. One contract a visit; a death's reload keeps it, target, weapon
// and colour; a finished one stays finished until the next visit.
class ContractBook {
public:
    // A load of `level`: returns whether the contract continues (the caller then re-finds its
    // target by key) or the visit is new (the contract is forgotten; the caller may start one).
    VisitChange level_loaded(int level, bool resume);

    // Starts the visit's contract. Refused (false) when one was already made on this visit.
    bool start(const Contract& contract);

    // Marks the visit as having none to give (nothing eligible): no later start on this visit.
    void none_available(int level);

    // The target died with killing blow `fatal`. Returns the new status (Completed only when the
    // blow was the required weapon's, verified), or the old one when the contract was not active.
    ContractStatus target_died(const FatalResult& fatal);

    // The target is gone without a recorded death.
    ContractStatus target_lost();

    // Returns the reward to pay now, once: the contract's reward after it completes, else 0.
    int take_reward();

    const Contract& contract() const { return m_contract; }
    Contract& contract() { return m_contract; }
    bool decided() const { return m_decided; }

private:
    Contract m_contract;
    int m_last_level = -1;
    bool m_decided = false;  // A contract was made, or none could be, on this visit.
};

// ---- The wanted target's paint ------------------------------------------------------------

// moby_material's materials (openrac_mod.h's OPENRAC_MOBY_MATERIAL_*), repeated so the rules need
// no SDK header; mod.cpp checks they agree.
inline constexpr std::uint32_t kMaterialNone = 0;
inline constexpr std::uint32_t kMaterialTint = 1;
inline constexpr std::uint32_t kMaterialPaint = 5;
inline constexpr int kPaintGlowTop = 15;

// The material the port can draw the target with: paint at `glow` from interface 1.34, a tint
// from 1.23, else none (kMaterialNone: the overlay marker alone).
std::uint32_t target_material(int api_minor, int glow);

// One call to moby_material the paint asks for.
struct PaintOp {
    std::uint32_t address = 0;
    std::uint32_t material = kMaterialNone;
    std::uint32_t rgb = 0xFFFFFFu;
};

// The paint on the one wanted moby. moby_material takes a moby's game address, which the SDK's
// moby lists do not give; the hook points that are handed a moby (Moby_GetHitMessage as an
// enemy's damage path polls it, Moby_PostHit's victim, Moby_RecordDeath) are, and the port's
// moby_slot_of turns that address into the slot the contract knows. So an address is painted only
// when the port says it is the target's slot, holding the target's class, this level load; never
// an address worked out from a slot.
class TargetPaint {
public:
    // Paints the target in `slot` of `moby_class` with `material` and `rgb` once its address is
    // seen; `material` kMaterialNone paints nothing. Clears any earlier target first (returned).
    PaintOp aim(int slot, int moby_class, std::uint32_t material, std::uint32_t rgb);

    // The target's slot after a reload (the same key, maybe another slot). Forgets the address:
    // the level load cleared the port's material.
    void moved(int slot);

    // A moby at `address`, in `slot` of `moby_class` as the port says: the op that paints it when
    // it is the target not painted yet, else an op with address 0.
    PaintOp seen(std::uint32_t address, int slot, int moby_class);

    // The port refused the paint (returned not OK): try again at the next sighting.
    void refused() { m_address = 0; }

    // The target died, was lost, or the contract ended: the op that clears it, if painted, and no
    // target from now on.
    PaintOp release();

    // The level is gone (the port drops every material with it): forget, nothing to clear.
    void level_changed();

    bool painted() const { return m_address != 0; }
    bool aiming() const { return m_slot >= 0; }
    int slot() const { return m_slot; }
    std::uint32_t address() const { return m_address; }

private:
    int m_slot = -1;
    int m_class = -1;
    std::uint32_t m_material = kMaterialNone;
    std::uint32_t m_rgb = 0xFFFFFFu;
    std::uint32_t m_address = 0;
};

// ---- Template definitions ------------------------------------------------------------------

template <class Named>
int pick_target(const std::vector<Candidate>& candidates, Named named, std::mt19937& rng) {
    std::vector<int> any;
    std::vector<int> with_name;
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        if (!eligible_target(candidates[i])) {
            continue;
        }
        any.push_back(static_cast<int>(i));
        if (named(candidates[i].moby_class)) {
            with_name.push_back(static_cast<int>(i));
        }
    }
    const std::vector<int>& pool = with_name.empty() ? any : with_name;
    if (pool.empty()) {
        return -1;
    }
    std::uniform_int_distribution<std::size_t> pick(0, pool.size() - 1);
    return pool[pick(rng)];
}

}  // namespace bounty_hunter
