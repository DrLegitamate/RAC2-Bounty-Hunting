// SPDX-License-Identifier: GPL-3.0-or-later
//
// The wanted target's paint: one moby alone, cleared at every ending, never left on a reused slot.

#include "check.h"
#include "rules.h"

using namespace bounty_hunter;

namespace {

constexpr int kSlot = 40;
constexpr int kClass = 2921;
constexpr std::uint32_t kAddress = 0x00300000u;

std::uint32_t paint() {
    return target_material(34, 8);
}

void material_choice() {
    CHECK_EQ(target_material(36, 8), kMaterialPaint | 8u << 8);  // Paint keeps shading.
    CHECK_EQ(target_material(34, 99), kMaterialPaint | 15u << 8);
    CHECK_EQ(target_material(34, -3), kMaterialPaint);
    CHECK_EQ(target_material(29, 8), kMaterialTint);  // Older port: tint fallback.
    CHECK_EQ(target_material(20, 8), kMaterialNone);
}

void target_alone() {
    TargetPaint p;
    p.aim(kSlot, kClass, paint(), 0x3399FF);
    // Another enemy of the same class, another slot: never painted.
    CHECK_EQ(p.seen(0x00300100u, kSlot + 1, kClass).address, 0u);
    // Another class in the target's slot (a stale or reused slot): never painted.
    CHECK_EQ(p.seen(kAddress, kSlot, kClass + 1).address, 0u);
    // The target itself.
    const PaintOp op = p.seen(kAddress, kSlot, kClass);
    CHECK_EQ(op.address, kAddress);
    CHECK_EQ(op.material, paint());
    CHECK_EQ(op.rgb, 0x3399FFu);
    CHECK(p.painted());
    // Seen again: nothing more to do.
    CHECK_EQ(p.seen(kAddress, kSlot, kClass).address, 0u);
}

void cleared_on_ending() {
    // Completion, failure and disappearance all go through release().
    TargetPaint p;
    p.aim(kSlot, kClass, paint(), 0xFF0000);
    p.seen(kAddress, kSlot, kClass);
    const PaintOp clear = p.release();
    CHECK_EQ(clear.address, kAddress);
    CHECK_EQ(clear.material, kMaterialNone);
    CHECK(!p.painted());
    CHECK(!p.aiming());
    // Nothing is painted after the ending, even if the slot is seen again with that class (a new
    // moby of the same class reusing the slot).
    CHECK_EQ(p.seen(kAddress, kSlot, kClass).address, 0u);
    // A second release asks for nothing.
    CHECK_EQ(p.release().address, 0u);
}

void level_change() {
    TargetPaint p;
    p.aim(kSlot, kClass, paint(), 0xFF0000);
    p.seen(kAddress, kSlot, kClass);
    p.level_changed();  // The port dropped it with the level.
    CHECK(!p.painted());
    CHECK_EQ(p.release().address, 0u);  // So no stale clear on a new level's moby.
}

void reload_repaints() {
    TargetPaint p;
    p.aim(kSlot, kClass, paint(), 0xFF0000);
    p.seen(kAddress, kSlot, kClass);
    p.level_changed();
    p.moved(kSlot + 5);  // The same key in another slot after the reload.
    CHECK_EQ(p.seen(kAddress, kSlot, kClass).address, 0u);
    CHECK_EQ(p.seen(0x00300500u, kSlot + 5, kClass).address, 0x00300500u);
}

void refused_retries() {
    TargetPaint p;
    p.aim(kSlot, kClass, paint(), 0xFF0000);
    p.seen(kAddress, kSlot, kClass);
    p.refused();
    CHECK(!p.painted());
    CHECK_EQ(p.seen(kAddress, kSlot, kClass).address, kAddress);
}

void highlight_off() {
    TargetPaint p;
    p.aim(kSlot, kClass, kMaterialNone, 0xFF0000);
    CHECK_EQ(p.seen(kAddress, kSlot, kClass).address, 0u);
}

void new_target_clears_old() {
    TargetPaint p;
    p.aim(kSlot, kClass, paint(), 0xFF0000);
    p.seen(kAddress, kSlot, kClass);
    const PaintOp old = p.aim(kSlot + 1, kClass, paint(), 0x00FF00);
    CHECK_EQ(old.address, kAddress);
    CHECK_EQ(old.material, kMaterialNone);
}

}  // namespace

int main() {
    material_choice();
    target_alone();
    cleared_on_ending();
    level_change();
    reload_repaints();
    refused_retries();
    highlight_off();
    new_target_clears_old();
    return check::finish("highlight_test");
}
