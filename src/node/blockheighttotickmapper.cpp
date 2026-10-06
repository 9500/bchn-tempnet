// Copyright (c) 2026-present The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <node/blockheighttotickmapper.h>

#include <cassert>
#include <utility>

BlockHeightToTickMapper::BlockHeightToTickMapper(const std::span<const HeightToIntervalPair> &is) {
    for (const auto & [h, interval] : is) {
        schedule.try_emplace(h, Mapped{.interval = interval, .cumulative = BlockTicks{0}});
    }
    if (schedule.empty() || schedule.begin()->first != 0) {
        // enforce that first element *MUST* be height 0
        schedule.emplace(0, Mapped{.interval = DEFAULT_BLOCK0_INTERVAL, .cumulative = BlockTicks{0}});
    }
    auto itPrev = schedule.end();
    for (auto it = schedule.begin(); it != schedule.end(); itPrev = it++) {
        if (it->second.interval <= BlockTicks{0}) [[unlikely]] {
            // disallow 0-tick block intervals!
            throw BadArgs("The block interval schedule may not contain intervals of size 0!");
        }
        if (itPrev != schedule.end()) {
            const BlockHeight heightDiff = (it->first - itPrev->first);
            // accumulate: previous cumulative value + total tick count from previous to this height
            it->second.cumulative = itPrev->second.cumulative + itPrev->second.interval * heightDiff;
        }
    }
    for (const auto & [h, mapped] : schedule) {
        auto inserted = revMap.try_emplace(mapped.cumulative, h).second;
        assert(inserted); // enforce unique cumulative values (same effect as disallowing 0-tick intervals)
    }
}

BlockTicks BlockHeightToTickMapper::HeightToTick(const BlockHeight height) const noexcept {
    assert(!schedule.empty());
    auto it = schedule.upper_bound(height);
    if (it != schedule.begin()) {
        --it;
    }
    const BlockHeight baseHeight = it->first;
    const BlockHeight deltaHeight = height - baseHeight;
    return it->second.cumulative + deltaHeight * it->second.interval;
}

auto BlockHeightToTickMapper::TickToHeight(const BlockTicks tick) const noexcept -> BlockHeight {
    assert(!revMap.empty());
    auto it = revMap.upper_bound(tick);
    if (it != revMap.begin()) {
        --it;
    }
    assert(tick >= it->first);
    const BlockHeight baseHeight = it->second;
    auto it2 = schedule.find(baseHeight);
    assert(it2 != schedule.end());
    assert(it->first == it2->second.cumulative);
    const BlockTicks baseInterval = it2->second.interval;
    const BlockTicks deltaFromBase = tick - it->first;
    const BlockHeight height = baseHeight + (deltaFromBase / baseInterval);
    return height;
}

BlockTicks BlockHeightToTickMapper::TicksElapsed(BlockHeight fromHeight, BlockHeight toHeight) const noexcept {
    if (toHeight < fromHeight) {
        std::swap(toHeight, fromHeight);
    }
    return HeightToTick(toHeight) - HeightToTick(fromHeight);
}

BlockTicks BlockHeightToTickMapper::GetBlockTickIntervalForHeight(BlockHeight height) const noexcept {
    assert(!schedule.empty());
    auto it = schedule.upper_bound(height);
    if (it != schedule.begin()) {
        --it;
    }
    return it->second.interval;
}

auto BlockHeightToTickMapper::LastEntry() const noexcept -> HeightToIntervalPair {
    assert(!schedule.empty());
    auto last = schedule.rbegin();
    return HeightToIntervalPair{last->first, last->second.interval};
}

auto BlockHeightToTickMapper::FirstEntry() const noexcept -> HeightToIntervalPair {
    assert(!schedule.empty());
    auto it = schedule.begin();
    return HeightToIntervalPair{it->first, it->second.interval};
}

auto BlockHeightToTickMapper::PopLast() -> HeightToIntervalPair {
    if (schedule.size() <= 1) [[unlikely]] {
        // cannot pop the base height = 0 entry
        throw CannotPopLastElement("Attempted to pop the last element from the block interval schedule");
    }
    auto last = schedule.rbegin();
    HeightToIntervalPair ret{last->first, last->second.interval};
    revMap.erase(last->second.cumulative);
    schedule.erase(last->first); // `last` is invalidated here
    return ret;
}

void BlockHeightToTickMapper::PushLast(HeightToIntervalPair p) {
    const auto & [height, interval] = p;
    assert(!schedule.empty());
    if (schedule.lower_bound(height) != schedule.end()) [[unlikely]] {
        // ensure invariant, this must be LAST
        throw BadArgs("The specified height must be larger than all other heights in the existing interval schedule");
    } else if (interval <= BlockTicks{0}) [[unlikely]] {
        // disallow 0-tick block intervals!
        throw BadArgs("The block interval schedule may not contain intervals of size 0!");
    }
    auto && [it, inserted] = schedule.try_emplace(height, Mapped{.interval = interval, .cumulative = BlockTicks{}});
    assert(inserted);
    assert(it != schedule.begin()); // assertion must hold true otherwise expression `--it` below would be UB
    auto &mapped = it->second;
    const auto & [heightPrev, mappedPrev] = *--it;
    mapped.cumulative = mappedPrev.cumulative + ((height - heightPrev) * mappedPrev.interval);
    const bool inserted2 = revMap.try_emplace(mapped.cumulative, height).second;
    assert(inserted2);
}
