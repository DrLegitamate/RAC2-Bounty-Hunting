// SPDX-License-Identifier: GPL-3.0-or-later
//
// Bounty Hunter's rules (rules.h).

#include "rules.h"

#include <algorithm>
#include <cmath>

#include "weapons.h"

namespace bounty_hunter {

// ---- General bounty ------------------------------------------------------------------------

Tier tier_of_bolts(int bolts) {
    if (bolts < 0) {
        return Tier::Standard;
    }
    if (bolts <= kBasicMaxBolts) {
        return Tier::Basic;
    }
    if (bolts <= kStandardMaxBolts) {
        return Tier::Standard;
    }
    if (bolts <= kAdvancedMaxBolts) {
        return Tier::Advanced;
    }
    return Tier::Elite;
}

const char* tier_name(Tier tier) {
    switch (tier) {
        case Tier::Basic:
            return "Basic";
        case Tier::Standard:
            return "Standard";
        case Tier::Advanced:
            return "Advanced";
        case Tier::Elite:
            return "Elite";
    }
    return "Standard";
}

int tier_bounty(const BountySettings& settings, Tier tier) {
    const int base = std::max(0, settings.base);
    if (!settings.tiers) {
        return base;
    }
    switch (tier) {
        case Tier::Basic:
            return base > 0 ? std::max(1, base / 2) : 0;
        case Tier::Standard:
            return base;
        case Tier::Advanced:
            return base * 2;
        case Tier::Elite:
            return base * 4;
    }
    return base;
}

int Streak::kill(std::uint32_t tick, const BountySettings& settings) {
    const int max = std::max(1, settings.max_streak);
    if (m_count > 0 && tick - m_last <= settings.streak_ticks) {
        m_count = std::min(m_count + 1, max);
    } else {
        m_count = 1;
    }
    m_last = tick;
    return m_count;
}

int Streak::current(std::uint32_t tick, const BountySettings& settings) const {
    if (m_count > 0 && tick - m_last <= settings.streak_ticks) {
        return m_count;
    }
    return 1;
}

bool Streak::active(std::uint32_t tick, const BountySettings& settings) const {
    return m_count > 1 && tick - m_last <= settings.streak_ticks;
}

Payout compute_payout(const BountySettings& settings, Tier tier, int streak, bool wrench) {
    Payout payout;
    payout.tier_bounty = tier_bounty(settings, tier);
    payout.streak = std::max(1, streak);
    payout.wrench = wrench;
    const int wrench_times = wrench ? std::max(1, settings.wrench_multiplier) : 1;
    payout.total = payout.tier_bounty * payout.streak * wrench_times;
    return payout;
}

// ---- Weapon attribution ---------------------------------------------------------------------

void ShotTracker::fired(int slot, int moby_class, int item) {
    if (slot < 0) {
        return;
    }
    m_shots[slot] = Shot{moby_class, item};
}

void ShotTracker::ended(int slot) {
    m_shots.erase(slot);
}

int ShotTracker::item_of(int slot, int moby_class) const {
    const auto found = m_shots.find(slot);
    if (found == m_shots.end()) {
        return -1;
    }
    // A class we could not read matches nothing: never a guess.
    if (moby_class < 0 || found->second.moby_class != moby_class) {
        return -1;
    }
    return found->second.item;
}

int classify_hit(const HitSource& source, const ShotTracker& shots) {
    if (source.slot < 0) {
        return -1;
    }

    // A shot the port told us of, still in its slot.
    const int shot = shots.item_of(source.slot, source.moby_class);
    if (shot >= 0) {
        return shot;
    }

    // The hero's own moby: a melee strike is the wrench's, unless he is Clank or holds the
    // Walloper (whose punches cannot be told apart). Anything else he posts is no weapon's.
    if (source.slot == source.hero_slot) {
        if (source.hero_class == kClankHeroClass || (source.mask & kMeleeMask) == 0
            || source.held_item == kWalloperItem) {
            return -1;
        }
        return kWrenchItem;
    }

    // The wrench moby itself (a swing's reach, a throw).
    if (source.wrench_class >= 0 && source.moby_class == source.wrench_class) {
        return kWrenchItem;
    }

    // A class only one weapon makes.
    if (source.moby_class >= 0) {
        return item_of_source_class(source.moby_class);
    }
    return -1;
}

void HitLedger::record(int victim_slot, int item, std::uint32_t tick) {
    if (victim_slot < 0) {
        return;
    }
    m_hits[victim_slot].push_back(Hit{tick, item});
}

FatalResult HitLedger::fatal(int victim_slot, std::uint32_t tick) const {
    FatalResult result;
    const auto found = m_hits.find(victim_slot);
    if (found == m_hits.end() || found->second.empty()) {
        return result;
    }
    const std::vector<Hit>& hits = found->second;
    const std::uint32_t last = hits.back().tick;
    if (tick - last > kWindowTicks) {
        return result;
    }

    // Every hit of the final burst must name one weapon.
    int item = -2;
    for (auto it = hits.rbegin(); it != hits.rend(); ++it) {
        if (last - it->tick > kBurstTicks) {
            break;
        }
        if (it->item < 0 || (item != -2 && it->item != item)) {
            result.kind = Fatal::Ambiguous;
            return result;
        }
        item = it->item;
    }
    result.kind = Fatal::Verified;
    result.item = item;
    return result;
}

void HitLedger::prune(std::uint32_t tick) {
    for (auto it = m_hits.begin(); it != m_hits.end();) {
        std::vector<Hit>& hits = it->second;
        hits.erase(
            std::remove_if(
                hits.begin(),
                hits.end(),
                [tick](const Hit& hit) { return tick - hit.tick > kWindowTicks; }
            ),
            hits.end()
        );
        it = hits.empty() ? m_hits.erase(it) : std::next(it);
    }
}

// ---- Contracts --------------------------------------------------------------------------------

bool eligible_target(const Candidate& candidate) {
    if (candidate.slot < 0 || candidate.key < 0 || candidate.dead || candidate.scene) {
        return false;
    }
    return candidate.copy_answer == kCopyOk || candidate.copy_answer == kCopyNoRoom;
}

std::vector<int> weapon_pool(
    const std::vector<OwnedWeapon>& owned, bool include_wrench, bool require_ammo
) {
    std::vector<int> pool;
    for (const OwnedWeapon& weapon : owned) {
        if (!weapon.owned || !contract_trackable(weapon.item)) {
            continue;
        }
        if (weapon.item == kWrenchItem) {
            if (include_wrench) {
                pool.push_back(weapon.item);
            }
            continue;
        }
        if (require_ammo && !(weapon.ammo > 0 || weapon.capacity == 0)) {
            continue;
        }
        if (std::find(pool.begin(), pool.end(), weapon.item) == pool.end()) {
            pool.push_back(weapon.item);
        }
    }
    return pool;
}

std::uint32_t hsv_to_rgb(int hue, float saturation, float value) {
    hue = ((hue % 360) + 360) % 360;
    const float c = value * saturation;
    const float h = static_cast<float>(hue) / 60.0f;
    const float x = c * (1.0f - std::fabs(std::fmod(h, 2.0f) - 1.0f));
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    switch (hue / 60) {
        case 0:
            r = c, g = x;
            break;
        case 1:
            r = x, g = c;
            break;
        case 2:
            g = c, b = x;
            break;
        case 3:
            g = x, b = c;
            break;
        case 4:
            r = x, b = c;
            break;
        default:
            r = c, b = x;
            break;
    }
    const float m = value - c;
    auto byte = [m](float channel) {
        const float scaled = std::round((channel + m) * 255.0f);
        return static_cast<std::uint32_t>(std::clamp(scaled, 0.0f, 255.0f));
    };
    return byte(r) << 16 | byte(g) << 8 | byte(b);
}

int hue_distance(int a, int b) {
    const int d = std::abs((((a - b) % 360) + 360) % 360);
    return d > 180 ? 360 - d : d;
}

int pick_hue(std::mt19937& rng, int previous, int min_distance) {
    std::uniform_int_distribution<int> any(0, 359);
    int hue = any(rng);
    for (int tries = 0; previous >= 0 && tries < 16 && hue_distance(hue, previous) < min_distance;
         ++tries) {
        hue = any(rng);
    }
    return hue;
}

VisitChange visit_change(int last_level, int level, bool resume) {
    if (last_level >= 0 && level == last_level) {
        return VisitChange::Continue;
    }
    (void)resume;  // A resume is always of the level last loaded; another level is a new visit.
    return VisitChange::NewVisit;
}

VisitChange ContractBook::level_loaded(int level, bool resume) {
    const VisitChange change = visit_change(m_last_level, level, resume);
    m_last_level = level;
    if (change == VisitChange::NewVisit) {
        m_contract = Contract{};
        m_contract.level = level;
        m_decided = false;
    }
    return change;
}

bool ContractBook::start(const Contract& contract) {
    if (m_decided) {
        return false;
    }
    m_contract = contract;
    m_contract.status = ContractStatus::Active;
    m_contract.reason = FailReason::None;
    m_contract.paid = false;
    m_decided = true;
    return true;
}

void ContractBook::none_available(int level) {
    if (m_decided) {
        return;
    }
    m_contract = Contract{};
    m_contract.level = level;
    m_decided = true;
}

ContractStatus ContractBook::target_died(const FatalResult& fatal) {
    if (m_contract.status != ContractStatus::Active) {
        return m_contract.status;
    }
    if (fatal.kind == Fatal::Verified && fatal.item == m_contract.weapon) {
        m_contract.status = ContractStatus::Completed;
    } else {
        m_contract.status = ContractStatus::Failed;
        m_contract.reason =
            fatal.kind == Fatal::Verified ? FailReason::WrongWeapon : FailReason::Unverified;
    }
    return m_contract.status;
}

ContractStatus ContractBook::target_lost() {
    if (m_contract.status == ContractStatus::Active) {
        m_contract.status = ContractStatus::Failed;
        m_contract.reason = FailReason::TargetLost;
    }
    return m_contract.status;
}

int ContractBook::take_reward() {
    if (m_contract.status != ContractStatus::Completed || m_contract.paid) {
        return 0;
    }
    m_contract.paid = true;
    return m_contract.reward;
}

// ---- The wanted target's paint ------------------------------------------------------------

std::uint32_t target_material(int api_minor, int glow) {
    if (api_minor >= 34) {
        const int level = std::clamp(glow, 0, kPaintGlowTop);
        return kMaterialPaint | (static_cast<std::uint32_t>(level) & 0xFu) << 8;
    }
    if (api_minor >= 23) {
        return kMaterialTint;
    }
    return kMaterialNone;
}

PaintOp TargetPaint::aim(int slot, int moby_class, std::uint32_t material, std::uint32_t rgb) {
    const PaintOp cleared = release();
    m_slot = slot;
    m_class = moby_class;
    m_material = material;
    m_rgb = rgb & 0xFFFFFFu;
    return cleared;
}

void TargetPaint::moved(int slot) {
    m_slot = slot;
    m_address = 0;
}

PaintOp TargetPaint::seen(std::uint32_t address, int slot, int moby_class) {
    PaintOp op;
    if (address == 0 || m_slot < 0 || m_material == kMaterialNone || m_address != 0) {
        return op;
    }
    if (slot != m_slot || moby_class != m_class) {
        return op;
    }
    m_address = address;
    op.address = address;
    op.material = m_material;
    op.rgb = m_rgb;
    return op;
}

PaintOp TargetPaint::release() {
    PaintOp op;
    if (m_address != 0) {
        op.address = m_address;
        op.material = kMaterialNone;
        op.rgb = 0xFFFFFFu;
    }
    m_address = 0;
    m_slot = -1;
    m_class = -1;
    return op;
}

void TargetPaint::level_changed() {
    m_address = 0;
}

}  // namespace bounty_hunter
