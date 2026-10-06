// Copyright (c) 2026-present The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include <consensus/blockticks.h>
#include <consensus/consensus.h>

#include <cstdint>
#include <initializer_list>
#include <map>
#include <stdexcept>
#include <span>
#include <tuple>
#include <utility>

// Provides a mechanism to map block heights -> BlockTicks given a "schedule" of block time interval changes.
// Each entry in the table is a height for which the new schedule takes effect, so the blocks mined after the given
// entry will have the new interval.
struct BlockHeightToTickMapper {
    using BlockHeight = uint32_t;
    using HeightToIntervalPair = std::pair<BlockHeight, BlockTicks>;

    // The default interval used for block at height = 0 (legacy 600 second interval), if none is supplied in c'tor.
    static constexpr BlockTicks DEFAULT_BLOCK0_INTERVAL = legacy::POW_TARGET_SPACING_TICKS;

    // Thrown by: constructor on bad usage, as well as PushLast().
    struct BadArgs : std::invalid_argument { using std::invalid_argument::invalid_argument; };
    // Thrown by: PopLast() if the schedule only contains 1 entry
    struct CannotPopLastElement : std::runtime_error { using std::runtime_error::runtime_error; };

    // If schedule is empty, we do default 600 ticks per block height. If schedule contains any 0-tick intervals,
    // will throw `BadArgs`
    explicit BlockHeightToTickMapper(const std::span<const HeightToIntervalPair> &intervalSchedule = {});

    // Support initializer_list syntax
    BlockHeightToTickMapper(std::initializer_list<HeightToIntervalPair> intervalSchedule)
        : BlockHeightToTickMapper(std::span<const HeightToIntervalPair>{intervalSchedule}) {}

    // Based on configured schedule, convert height to tick. Returns the number of ticks elapsed for the given height,
    // taking into account all intervening block time interval changes.
    BlockTicks HeightToTick(BlockHeight height) const noexcept;

    // Based on configured schedule, convert tick to height, taking into account all configured block time interval changes.
    // Returns nearest height less-than-or-equal-to the given ticks, so for example with the default setup,
    // tick count 600 and 601 both return height 1, tick count 1200 and 1201 return height 2, and tick 0 through 599
    // return height 0.
    BlockHeight TickToHeight(BlockTicks ticks) const noexcept;

    // Returns the number of ticks for the block interval [fromHeight, toHeight]:
    // Shorthand for: `this->HeightToTick(toHeight) - this->HeightToTick(fromHeight)`
    BlockTicks TicksElapsed(BlockHeight fromHeight, BlockHeight toHeight) const noexcept;

    // Get the interval for block `height`; the next block after `height` will be mined at this interval.
    BlockTicks GetBlockTickIntervalForHeight(BlockHeight height) const noexcept;

    // Get the latest entry (greatest height) from the schedule
    HeightToIntervalPair LastEntry() const noexcept;
    // Get the first entry (height == 0) from the schedule (used by tests)
    HeightToIntervalPair FirstEntry() const noexcept;

    // Pop the latest entry from the schedule. You cannot pop the height == 0 entry from the schedule. Attempting to do
    // so will result in `CannotPopLastElement` being thrown.
    HeightToIntervalPair PopLast();

    // Push a new entry to the schedule. Note that `height` must be > any existing height in the schedule otherwise
    // this will throw `BadArgs`. The interval for `p` must also be > 0 otherwise `BadArgs` is thrown as well.
    void PushLast(HeightToIntervalPair p);

    // Return the number of entries in the schedule. This is always >= 1.
    size_t Size() const noexcept { return schedule.size(); }

    bool operator==(const BlockHeightToTickMapper &o) const noexcept {
        return this == &o || std::tie(schedule, revMap) == std::tie(o.schedule, o.revMap);
    }

 private:
    struct Mapped {
        BlockTicks interval; ///< the block time interval (in ticks) starting at the given height
        BlockTicks cumulative; ///< the number of cumulative ticks so far for the given height
        bool operator==(const Mapped &o) const noexcept {
            return this == &o || std::tie(interval, cumulative) == std::tie(o.interval, o.cumulative);
        }
    };
    std::map<BlockHeight, Mapped> schedule; ///< may not be empty
    std::map<BlockTicks, BlockHeight> revMap; ///< map of cumulative ticks -> block height
};
