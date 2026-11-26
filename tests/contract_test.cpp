// SPDX-License-Identifier: GPL-3.0-or-later
//
// Contracts: targets, weapons, colours and the life of a contract through reloads.

#include <set>

#include "check.h"
#include "enemy_names.h"
#include "rules.h"
#include "weapons.h"

using namespace bounty_hunter;

namespace {

Candidate make(int slot, int key, int moby_class, int answer) {
    Candidate c;
    c.slot = slot;
    c.key = key;
    c.moby_class = moby_class;
    c.copy_answer = answer;
    return c;
}

auto never_named = [](int) { return false; };

void eligibility() {
    CHECK(eligible_target(make(1, 1, 2921, kCopyOk)));
    CHECK(eligible_target(make(1, 1, 2921, kCopyNoRoom)));  // Only too large to copy.
    CHECK(!eligible_target(make(1, 1, 2921, kCopyNotEnemy)));   // Friendly NPC, vendor, crate.
    CHECK(!eligible_target(make(1, 1, 2921, kCopyScripted)));   // Wave, arena, door group.
    CHECK(!eligible_target(make(1, 1, 2921, kCopyListed)));     // Boss, turret, spawner.
    CHECK(!eligible_target(make(1, 1, 2921, kCopyNotPlaced)));  // Spawned: no stable key.
    CHECK(!eligible_target(make(1, -1, 2921, kCopyOk)));        // No key.
    CHECK(!eligible_target(make(1, 1, 2921, -1)));              // The port would not say.
    Candidate dead = make(1, 1, 2921, kCopyOk);
    dead.dead = true;
    CHECK(!eligible_target(dead));
    Candidate actor = make(1, 1, 2921, kCopyOk);
    actor.scene = true;
    CHECK(!eligible_target(actor));
}

void none_eligible() {
    std::mt19937 rng(1);
    std::vector<Candidate> level = {
        make(1, 1, 10, kCopyNotEnemy), make(2, 2, 11, kCopyScripted), make(3, 3, 12, kCopyListed)
    };
    CHECK_EQ(pick_target(level, never_named, rng), -1);
    CHECK_EQ(pick_target(std::vector<Candidate>{}, never_named, rng), -1);
}

void random_selection_covers_all() {
    std::vector<Candidate> level = {
        make(1, 1, 10, kCopyOk), make(2, 2, 11, kCopyNotEnemy), make(3, 3, 12, kCopyOk),
        make(4, 4, 13, kCopyNoRoom)
    };
    std::set<int> seen;
    std::mt19937 rng(7);
    for (int i = 0; i < 200; ++i) {
        const int pick = pick_target(level, never_named, rng);
        CHECK(pick == 0 || pick == 2 || pick == 3);
        seen.insert(pick);
    }
    CHECK_EQ(seen.size(), 3u);
}

void named_preferred() {
    std::vector<Candidate> level = {make(1, 1, 999999, kCopyOk), make(2, 2, 2921, kCopyOk)};
    std::mt19937 rng(3);
    for (int i = 0; i < 50; ++i) {
        CHECK_EQ(pick_target(level, [](int c) { return enemy_name(c) != nullptr; }, rng), 1);
    }
    CHECK(enemy_name(2921) != nullptr);  // Protopet, cited in the table.
    CHECK(enemy_name(999999) == nullptr);
}

std::vector<OwnedWeapon> inventory() {
    return {
        {kWrenchItem, true, 0, 0},
        {30, true, 200, 200},   // Lancer, loaded.
        {26, true, 0, 30},      // Blitz Gun, empty.
        {27, false, 0, 8},      // Minirocket Tube, not owned.
        {43, true, 1, 1},       // Zodiac: owned but untrackable.
        {5, true, 0, 0},        // A gadget.
        {24, true, 0, -1},      // Seeker Gun, capacity unknown and empty.
    };
}

void weapon_pool_rules() {
    const auto pool = weapon_pool(inventory(), false, true);
    CHECK_EQ(pool.size(), 1u);
    CHECK_EQ(pool[0], 30);

    const auto no_ammo_rule = weapon_pool(inventory(), false, false);
    CHECK_EQ(no_ammo_rule.size(), 3u);  // Lancer, Blitz, Seeker; never the unowned or untracked.
    for (int item : no_ammo_rule) {
        CHECK(item != 27 && item != 43 && item != 5 && item != kWrenchItem);
    }

    const auto with_wrench = weapon_pool(inventory(), true, true);
    CHECK_EQ(with_wrench.size(), 2u);
    CHECK(with_wrench[0] == kWrenchItem || with_wrench[1] == kWrenchItem);

    // Endless ammo counts as available.
    std::vector<OwnedWeapon> endless = {{18, true, 0, 0}};
    CHECK_EQ(weapon_pool(endless, false, true).size(), 1u);
}

void hues() {
    CHECK_EQ(hsv_to_rgb(0, 1.0f, 1.0f), 0xFF0000u);
    CHECK_EQ(hsv_to_rgb(120, 1.0f, 1.0f), 0x00FF00u);
    CHECK_EQ(hsv_to_rgb(240, 1.0f, 1.0f), 0x0000FFu);
    CHECK_EQ(hsv_to_rgb(360, 1.0f, 1.0f), 0xFF0000u);
    CHECK_EQ(hsv_to_rgb(210, 0.0f, 1.0f), 0xFFFFFFu);
    CHECK_EQ(hue_distance(350, 10), 20);
    CHECK_EQ(hue_distance(0, 180), 180);

    std::mt19937 rng(11);
    int previous = -1;
    std::set<int> distinct;
    for (int map = 0; map < 100; ++map) {
        const int hue = pick_hue(rng, previous);
        CHECK(hue >= 0 && hue < 360);
        if (previous >= 0) {
            CHECK(hue_distance(hue, previous) >= 60);
        }
        distinct.insert(hue);
        previous = hue;
    }
    CHECK(distinct.size() > 20);
}

Contract sample(int level) {
    Contract c;
    c.level = level;
    c.key = 17;
    c.slot = 40;
    c.moby_class = 2921;
    c.weapon = 30;
    c.hue = 210;
    c.reward = 300;
    return c;
}

void persists_through_death_reload() {
    ContractBook book;
    CHECK(book.level_loaded(4, false) == VisitChange::NewVisit);
    CHECK(book.start(sample(4)));
    // The hero dies: the level is resumed.
    CHECK(book.level_loaded(4, true) == VisitChange::Continue);
    CHECK(book.contract().status == ContractStatus::Active);
    CHECK_EQ(book.contract().key, 17);
    CHECK_EQ(book.contract().weapon, 30);
    CHECK_EQ(book.contract().hue, 210);
    CHECK(!book.start(sample(4)));  // No reroll on the same visit.
}

void new_map_new_contract() {
    ContractBook book;
    book.level_loaded(4, false);
    book.start(sample(4));
    CHECK(book.level_loaded(5, false) == VisitChange::NewVisit);
    CHECK(book.contract().status == ContractStatus::None);
    Contract next = sample(5);
    next.key = 99;
    CHECK(book.start(next));
    CHECK_EQ(book.contract().key, 99);
}

void completion_pays_once() {
    ContractBook book;
    book.level_loaded(4, false);
    book.start(sample(4));
    FatalResult blow;
    blow.kind = Fatal::Verified;
    blow.item = 30;
    CHECK(book.target_died(blow) == ContractStatus::Completed);
    CHECK_EQ(book.take_reward(), 300);
    CHECK_EQ(book.take_reward(), 0);
    // A second death report changes nothing.
    CHECK(book.target_died(blow) == ContractStatus::Completed);
    CHECK_EQ(book.take_reward(), 0);
    // A reload of the map does not bring it back.
    CHECK(book.level_loaded(4, true) == VisitChange::Continue);
    CHECK(!book.start(sample(4)));
    CHECK_EQ(book.take_reward(), 0);
}

void wrong_weapon_fails() {
    ContractBook book;
    book.level_loaded(4, false);
    book.start(sample(4));
    FatalResult blow;
    blow.kind = Fatal::Verified;
    blow.item = 26;
    CHECK(book.target_died(blow) == ContractStatus::Failed);
    CHECK(book.contract().reason == FailReason::WrongWeapon);
    CHECK_EQ(book.take_reward(), 0);
    book.level_loaded(4, true);
    CHECK(!book.start(sample(4)));  // No reroll after a failure.
    CHECK(book.contract().status == ContractStatus::Failed);
}

void unverified_never_pays() {
    for (Fatal kind : {Fatal::Ambiguous, Fatal::None}) {
        ContractBook book;
        book.level_loaded(4, false);
        book.start(sample(4));
        FatalResult blow;
        blow.kind = kind;
        blow.item = 30;  // Even naming the right weapon, an unverified blow does not pay.
        CHECK(book.target_died(blow) == ContractStatus::Failed);
        CHECK(book.contract().reason == FailReason::Unverified);
        CHECK_EQ(book.take_reward(), 0);
    }
}

void target_lost() {
    ContractBook book;
    book.level_loaded(4, false);
    book.start(sample(4));
    CHECK(book.target_lost() == ContractStatus::Failed);
    CHECK(book.contract().reason == FailReason::TargetLost);
}

void no_targets_no_fake_contract() {
    ContractBook book;
    book.level_loaded(8, false);
    book.none_available(8);
    CHECK(book.contract().status == ContractStatus::None);
    CHECK(book.decided());
    CHECK(!book.start(sample(8)));  // Not on this visit, even after a reload.
    book.level_loaded(8, true);
    CHECK(!book.start(sample(8)));
    book.level_loaded(9, false);
    CHECK(book.start(sample(9)));
}

}  // namespace

int main() {
    eligibility();
    none_eligible();
    random_selection_covers_all();
    named_preferred();
    weapon_pool_rules();
    hues();
    persists_through_death_reload();
    new_map_new_contract();
    completion_pays_once();
    wrong_weapon_fails();
    unverified_never_pays();
    target_lost();
    no_targets_no_fake_contract();
    return check::finish("contract_test");
}
