// SPDX-License-Identifier: GPL-3.0-or-later
//
// The general bounty: tiers, streaks, the wrench bonus and duplicate payouts.

#include "check.h"
#include "rules.h"
#include "weapons.h"

using namespace bounty_hunter;

namespace {

BountySettings defaults() {
    BountySettings s;
    s.base = 10;
    s.streak_ticks = 5 * 60;
    s.max_streak = 5;
    s.wrench_multiplier = 2;
    s.tiers = true;
    return s;
}

void normal_kill() {
    const BountySettings s = defaults();
    Streak streak;
    const int times = streak.kill(1000, s);
    const Payout p = compute_payout(s, Tier::Standard, times, false);
    CHECK_EQ(times, 1);
    CHECK_EQ(p.total, 10);
}

void tiers() {
    BountySettings s = defaults();
    CHECK_EQ(tier_bounty(s, Tier::Basic), 5);
    CHECK_EQ(tier_bounty(s, Tier::Standard), 10);
    CHECK_EQ(tier_bounty(s, Tier::Advanced), 20);
    CHECK_EQ(tier_bounty(s, Tier::Elite), 40);
    CHECK(tier_of_bolts(5) == Tier::Basic);
    CHECK(tier_of_bolts(kBasicMaxBolts) == Tier::Basic);
    CHECK(tier_of_bolts(kBasicMaxBolts + 1) == Tier::Standard);
    CHECK(tier_of_bolts(50) == Tier::Advanced);
    CHECK(tier_of_bolts(500) == Tier::Elite);
    CHECK(tier_of_bolts(-1) == Tier::Standard);  // Unknown drop: Standard, never a guess upward.
    s.tiers = false;
    CHECK_EQ(tier_bounty(s, Tier::Elite), 10);
    CHECK_EQ(tier_bounty(s, Tier::Basic), 10);
    s.tiers = true;
    s.base = 1;
    CHECK_EQ(tier_bounty(s, Tier::Basic), 1);  // Never rounds a paid bounty to nothing.
    s.base = 0;
    CHECK_EQ(tier_bounty(s, Tier::Basic), 0);
}

void wrench_kill() {
    const BountySettings s = defaults();
    CHECK_EQ(compute_payout(s, Tier::Standard, 1, true).total, 20);
    // Stacks with the streak.
    CHECK_EQ(compute_payout(s, Tier::Standard, 3, true).total, 60);
    CHECK_EQ(compute_payout(s, Tier::Elite, 5, true).total, 400);
}

void streak_grows_and_caps() {
    const BountySettings s = defaults();
    Streak streak;
    std::uint32_t tick = 100;
    int expected[] = {1, 2, 3, 4, 5, 5, 5};
    for (int want : expected) {
        CHECK_EQ(streak.kill(tick, s), want);
        tick += 60;  // A kill a second.
    }
    CHECK(streak.active(tick, s));
}

void streak_expires() {
    const BountySettings s = defaults();
    Streak streak;
    CHECK_EQ(streak.kill(0, s), 1);
    CHECK_EQ(streak.kill(s.streak_ticks, s), 2);           // Exactly on the edge still counts.
    CHECK_EQ(streak.current(s.streak_ticks * 2 + 1, s), 1);  // Lapsed.
    CHECK(!streak.active(s.streak_ticks * 2 + 1, s));
    CHECK_EQ(streak.kill(s.streak_ticks * 2 + 1, s), 1);   // Starts again.
}

void streak_handles_tick_wrap() {
    const BountySettings s = defaults();
    Streak streak;
    CHECK_EQ(streak.kill(0xFFFFFFF0u, s), 1);
    CHECK_EQ(streak.kill(0x10u, s), 2);  // 32 ticks later across the wrap.
}

void no_duplicate_payouts() {
    PaidKills paid;
    CHECK(paid.first(42));
    CHECK(!paid.first(42));
    CHECK(paid.first(43));
    paid.reset();  // A new level load.
    CHECK(paid.first(42));
}

void crates_are_not_enemies() {
    // A crate is never a contract target: moby_copy_check calls it NOT_ENEMY (its class is not of
    // the bestiary's kind). The bounty itself is paid only on Moby_CountKill's enemy kill, which a
    // crate's death never reaches (rac2-decomp SetDeathBits: kind 5 only), so this is the
    // classification test that the mod's kill path keys on.
    Candidate crate;
    crate.slot = 3;
    crate.key = 7;
    crate.moby_class = 500;
    crate.copy_answer = kCopyNotEnemy;
    CHECK(!eligible_target(crate));
}

}  // namespace

int main() {
    normal_kill();
    tiers();
    wrench_kill();
    streak_grows_and_caps();
    streak_expires();
    streak_handles_tick_wrap();
    no_duplicate_payouts();
    crates_are_not_enemies();
    return check::finish("bounty_test");
}
