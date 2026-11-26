// SPDX-License-Identifier: GPL-3.0-or-later
//
// Weapon attribution: which weapon dealt a hit, and which hit killed.

#include "check.h"
#include "rules.h"
#include "weapons.h"

using namespace bounty_hunter;

namespace {

constexpr int kHero = 0;
constexpr int kRatchetClass = 0;
constexpr int kWrenchClass = 4000;  // Whatever hero_held reports; the test picks one.

HitSource source(int slot, int moby_class, std::uint32_t mask = 0x20000u) {
    HitSource s;
    s.slot = slot;
    s.moby_class = moby_class;
    s.mask = mask;
    s.hero_slot = kHero;
    s.hero_class = kRatchetClass;
    s.held_item = 30;
    s.wrench_class = kWrenchClass;
    return s;
}

void projectile_from_tracker() {
    ShotTracker shots;
    shots.fired(120, 1498, 27);  // A Minirocket Tube rocket.
    CHECK_EQ(classify_hit(source(120, 1498), shots), 27);
}

void delayed_projectile_after_switch() {
    // The player fired a seeker, switched to the Lancer: the hit is still the Seeker Gun's,
    // whatever is in hand.
    ShotTracker shots;
    shots.fired(200, 3329, 24);
    HitSource s = source(200, 3329);
    s.held_item = 30;
    CHECK_EQ(classify_hit(s, shots), 24);
}

void slot_reused_by_another_class() {
    ShotTracker shots;
    shots.fired(50, 1498, 27);
    // The rocket ended unseen and the slot holds an enemy's spit now: neither the rocket's weapon
    // nor any weapon.
    CHECK_EQ(classify_hit(source(50, 9999), shots), -1);
    shots.ended(50, 1498, 27);
    CHECK_EQ(shots.size(), 0u);
}

void end_does_not_drop_a_newer_shot() {
    // A rocket in slot 60 ends, and a seeker fired the same tick took slot 60 and was told first:
    // the rocket's end must not forget the seeker.
    ShotTracker shots;
    shots.fired(60, 1498, 27);
    shots.fired(60, 3329, 24);
    shots.ended(60, 1498, 27);
    CHECK_EQ(classify_hit(source(60, 3329), shots), 24);
    shots.ended(60, 3329, 24);
    CHECK_EQ(shots.size(), 0u);
}

void rounds_weapons_by_class() {
    // Weapons with no shot moby (OPENRAC_WEAPON_SHOT_ROUNDS, slot -1) post their hits from the
    // gun's own moby: its class names it.
    ShotTracker shots;
    CHECK_EQ(classify_hit(source(9, 1490), shots), 30);  // Lancer.
    CHECK_EQ(classify_hit(source(9, 1489), shots), 26);  // Blitz Gun.
    CHECK_EQ(classify_hit(source(9, 1494), shots), 23);  // Pulse Rifle's beam.
    // An explosion of a known weapon's.
    CHECK_EQ(classify_hit(source(77, 3301), shots), 42);  // Gravity Bomb's blast.
}

void wrench_hits() {
    ShotTracker shots;
    // A swing from the hero himself.
    CHECK_EQ(classify_hit(source(kHero, kRatchetClass, kMeleeMask), shots), kWrenchItem);
    // A thrown wrench: its own moby.
    CHECK_EQ(classify_hit(source(31, kWrenchClass, kMeleeMask), shots), kWrenchItem);
    // Clank's punch is not the wrench.
    HitSource clank = source(kHero, kClankHeroClass, kMeleeMask);
    clank.hero_class = kClankHeroClass;
    CHECK_EQ(classify_hit(clank, shots), -1);
    // The Walloper in hand: a hero-sourced punch may be its, so no attribution.
    HitSource walloper = source(kHero, kRatchetClass, kMeleeMask);
    walloper.held_item = kWalloperItem;
    CHECK_EQ(classify_hit(walloper, shots), -1);
    // A hero-sourced hit that is not melee is nobody's.
    CHECK_EQ(classify_hit(source(kHero, kRatchetClass, 0x20000u), shots), -1);
}

void unknown_sources_fail_safe() {
    ShotTracker shots;
    CHECK_EQ(classify_hit(source(-1, -1), shots), -1);   // No source moby.
    CHECK_EQ(classify_hit(source(12, -1), shots), -1);   // Class unknown.
    CHECK_EQ(classify_hit(source(12, 2222), shots), -1);  // An enemy's attack, a hazard.
    // The weapon in hand is never used to fill the gap.
    HitSource s = source(12, 2222);
    s.held_item = 27;
    CHECK_EQ(classify_hit(s, shots), -1);
}

void fatal_verified() {
    HitLedger ledger;
    ledger.record(5, 30, 100);
    ledger.record(5, 30, 110);
    ledger.record(5, 24, 115);  // The last burst: the Seeker Gun alone.
    ledger.record(5, 24, 116);
    const FatalResult r = ledger.fatal(5, 140);
    CHECK(r.kind == Fatal::Verified);
    CHECK_EQ(r.item, 24);

    // A Lancer hit only kBurstTicks before the last is inside the burst: not verified.
    HitLedger edge;
    edge.record(5, 30, 110);
    edge.record(5, 24, 110 + HitLedger::kBurstTicks);
    CHECK(edge.fatal(5, 140).kind == Fatal::Ambiguous);
}

void fatal_mixed_burst_is_ambiguous() {
    HitLedger ledger;
    ledger.record(5, 30, 200);
    ledger.record(5, 24, 201);  // Within the burst: the game may have read either.
    CHECK(ledger.fatal(5, 210).kind == Fatal::Ambiguous);
}

void fatal_unknown_hit_is_ambiguous() {
    HitLedger ledger;
    ledger.record(5, 30, 300);
    ledger.record(5, -1, 300);  // An environmental hit in the final burst.
    CHECK(ledger.fatal(5, 305).kind == Fatal::Ambiguous);
}

void fatal_too_old_is_none() {
    HitLedger ledger;
    ledger.record(5, 30, 0);
    CHECK(ledger.fatal(5, HitLedger::kWindowTicks + 1).kind == Fatal::None);
    CHECK(ledger.fatal(6, 10).kind == Fatal::None);  // Never hit at all (a scripted death).
}

void fatal_forget_and_prune() {
    HitLedger ledger;
    ledger.record(5, 30, 0);
    ledger.forget(5);
    CHECK(ledger.fatal(5, 1).kind == Fatal::None);
    ledger.record(6, 30, 0);
    ledger.record(6, 26, 1000);
    ledger.prune(1000);
    const FatalResult r = ledger.fatal(6, 1001);
    CHECK(r.kind == Fatal::Verified);
    CHECK_EQ(r.item, 26);
}

void trackable_weapons() {
    CHECK(contract_trackable(30));
    CHECK(contract_trackable(kWrenchItem));
    CHECK(!contract_trackable(43));  // Zodiac posts no hit.
    CHECK(!contract_trackable(25));  // Hoverbomb blast unverified.
    CHECK(!contract_trackable(5));   // A gadget.
    // Every reliable weapon but the wrench has at least one hit-source class.
    for (const WeaponInfo& w : kWeapons) {
        if (w.tracking != Tracking::Reliable || w.item == kWrenchItem) {
            continue;
        }
        bool has_source = false;
        for (const HitSourceClass& c : kHitSourceClasses) {
            has_source = has_source || c.item == w.item;
        }
        CHECK(has_source);
    }
    // And every hit-source class names a reliable weapon.
    for (const HitSourceClass& c : kHitSourceClasses) {
        CHECK(contract_trackable(c.item));
    }
}

}  // namespace

int main() {
    projectile_from_tracker();
    delayed_projectile_after_switch();
    slot_reused_by_another_class();
    end_does_not_drop_a_newer_shot();
    rounds_weapons_by_class();
    wrench_hits();
    unknown_sources_fail_safe();
    fatal_verified();
    fatal_mixed_burst_is_ambiguous();
    fatal_unknown_hit_is_ambiguous();
    fatal_too_old_is_none();
    fatal_forget_and_prune();
    trackable_weapons();
    return check::finish("attribution_test");
}
