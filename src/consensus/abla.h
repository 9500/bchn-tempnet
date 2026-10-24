// Copyright (c) 2023-present The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include <consensus/consensus.h>
#include <serialize.h>

#include <cassert>
#include <compare> // for comparison <=>
#include <cstdint>
#include <string>
#include <tuple>

/// ABLA, the Adaptive Blocksize-Limit Algorithm
///
/// Algorithm and data types for dynamically adjusting the block size limit permitted on the Bitcoin Cash
/// network.
///
/// Originally written by bitcoincashautist but adapted to C++ and to fit into the BCHN sources by Calin Culianu.
namespace abla {

// Base block interval, 10 minutes
inline constexpr BlockTicks BASE_BLOCK_INTERVAL = legacy::POW_TARGET_SPACING_TICKS;
// Maximum block interval, same as BASE_BLOCK_INTERVAL since we don't anticipate *increasing* the blocktime
inline constexpr BlockTicks MAX_BLOCK_INTERVAL = BASE_BLOCK_INTERVAL;
// Minimum block interval (ticks = seconds), 1 == 1 second
inline constexpr BlockTicks MIN_BLOCK_INTERVAL{1u};

/// Algorithm configuration -- this should be a part of a chain's consensus params.
struct Config {
    /// Initial control block size value, also used as floor value
    uint64_t epsilon0{};
    /// Initial elastic buffer size value, also used as floor value
    uint64_t beta0{};
    /// Reciprocal of control function "forget factor" value
    uint64_t gammaReciprocal{};
    /// Control function "asymmetry factor" value
    uint64_t zeta_xB7{};
    /// Reciprocal of elastic buffer decay rate
    uint64_t thetaReciprocal{};
    /// Elastic buffer "gear factor"
    uint64_t delta{};
    /// Maximum control block size value
    uint64_t epsilonMax{};
    /// Maximum elastic buffer size value
    uint64_t betaMax{};

    /// Set epsilonMax and betaMax such that algo's internal arithmetic ops can't overflow UINT64_MAX
    void SetMax();

    /// Returns true if the configuration is valid and/or sane. On false return optional out `*err` is set to point to
    /// a constant string explaining the reason that it is invalid.
    [[nodiscard]] bool IsValid(const char **err = nullptr) const;

    /// Returns true if the configuration renders the algorithm as a 'no-op' that will always return a fixed size.
    /// This can only be true iff `epsilon0 == epsilonMax && beta0 == betaMax` (testnets 3 & 4 have this as true).
    [[nodiscard]] bool IsFixedSize() const;

    /// Used for debug purposes -- print all of this instance's variables to a string.
    std::string ToString() const;

    /// Returns a default configuration for mainnet, etc as suggested in the ABLA spec: https://gitlab.com/0353F40E/ebaa
    /// @param fixedSize - if `true`, set `epsilonMax = epsilon0`, `betaMax = beta0`, thus making the ABLA algorithm
    ///        a no-op that always returns `defaultBlockSize` as the static max block size. This is normally set to
    ///        `true` for testnet3 and testnet4 (where we do not want the max block size to grow over time).
    static Config MakeDefault(uint64_t defaultBlockSize = DEFAULT_CONSENSUS_BLOCK_SIZE,
                              bool fixedSize = false);
};

/// Algorithm's internal state -- this is what gets persisted to disk via CDiskBlockIndex
///
/// Intended to be used such that this State is associated with block N, and the block size limit for block N is to
/// be given by `state_N.GetBlockSizeLimit()`. Thus, when checking the limit for the next block N + 1, given the state
/// for N, one must do: `state_N.NextBlockState().GetBlockSizeLimit()`
class StateBase {
protected:
    /// Saved with the state -- the actual block size in bytes for this block
    uint64_t blockSize{};

    /// Control function state
    uint64_t controlBlockSize{};
    /// Elastic buffer function state
    uint64_t elasticBufferSize{};

    constexpr StateBase() noexcept = default;

    constexpr StateBase(uint64_t blockSizeIn, uint64_t cbsIn, uint64_t ebsIn) noexcept
        : blockSize{blockSizeIn}, controlBlockSize{cbsIn}, elasticBufferSize{ebsIn}
    {}

    /// Get the block size limit for this block -- note this is capped at MAX_CONSENSUS_BLOCK_SIZE == 2GB unless
    /// `disable2GBCap` is set to true.
    uint64_t GetBlockSizeLimitBase(BlockTicks thisBlockInterval, bool disable2GBCap) const;

    /// Advance the algorithm's state to the next block (N + 1), given the next block (N + 1) block size,
    /// algorithm state for this block (N), and global algorithm configuration.
    StateBase NextBlockStateBase(const Config &config, BlockTicks thisBlockInterval, uint64_t nextBlockSize) const;

public:
    /// Query this block's size
    constexpr uint64_t GetBlockSize() const noexcept { return blockSize; }
    /// Query this state's "control block size"
    constexpr uint64_t GetControlBlockSize() const noexcept { return controlBlockSize; }
    /// Query this state's "elastic buffer size"
    constexpr uint64_t GetElasticBufferSize() const noexcept { return elasticBufferSize; }

    /// Note we only serialize these three properties to the block index db; see CBlockIndex which fills-in the
    /// `State::blockInterval` property at on-demand via the AblaStateMixin CRTP class.
    SERIALIZE_METHODS(StateBase, obj) {
        READWRITE(VARINT(obj.blockSize), VARINT(obj.controlBlockSize), VARINT(obj.elasticBufferSize));
    }
};

/// In-memory ABLA state -- this is the working state that's constructed on-the-fly by CBlockIndex
/// as client code works with ABLA to advance the state.
class State : public StateBase {
    BlockTicks blockInterval = BASE_BLOCK_INTERVAL; ///< In-memory only, not serialized, this block's pow target spacing

    constexpr State(const StateBase &b, BlockTicks blkTicks) : StateBase(b), blockInterval{blkTicks} {}

public:
    /// Constructs an "invalid" state (used by tests)
    constexpr State() noexcept = default;

    /// Construct a state using defaults from Config (suitable for all blocks before ABLA activation)
    constexpr State(const Config &config, uint64_t blkSize) noexcept
        : StateBase(blkSize, config.epsilon0, config.beta0) {}

    /// Convenience c'tor
    constexpr State(uint64_t blockSizeIn, uint64_t cbsIn, uint64_t ebsIn, BlockTicks thisBlockInterval) noexcept
        : StateBase(blockSizeIn, cbsIn, ebsIn), blockInterval{thisBlockInterval} {}

    /// Returns true if this state is valid relative to `config`. On false return, optional out `err` is set
    /// to point to a constant string explaining the reason that this state is invalid.
    [[nodiscard]] bool IsValid(const Config &config, const char **err = nullptr) const;

    /// Used for debug purposes -- print all of this instance's variables to a string.
    std::string ToString() const;

    /// Get the block size limit for this block -- note this is capped at MAX_CONSENSUS_BLOCK_SIZE == 2GB unless
    /// `disable2GBCap` is set to true.
    uint64_t GetBlockSizeLimit(bool disable2GBCap = false) const {
        return GetBlockSizeLimitBase(blockInterval, disable2GBCap);
    }

    /// Return the block size limit for the next block after this one. This is a utility method for consensus code
    /// to quickly know the limit to apply to the next block, given the current tip's defined State.
    uint64_t GetNextBlockSizeLimit(const Config &config, BlockTicks nextBlockInterval, bool disable2GBCap = false) const {
        return NextBlockState(config, nextBlockInterval, 0).GetBlockSizeLimit(disable2GBCap);
    }

    /// Advance the algorithm's state to the next block (N + 1), given the next block (N + 1) block size,
    /// algorithm state for this block (N), and global algorithm configuration.
    State NextBlockState(const Config &config, BlockTicks nextBlockInterval, uint64_t nextBlockSize) const {
        State ret{NextBlockStateBase(config, blockInterval, nextBlockSize), nextBlockInterval};
        assert(ret.IsValid(config));
        return ret;
    }

    /// Calculate algorithm's look-ahead block size limit, for a block N blocks ahead of current one. This is a
    /// "worst-case" calculation for the block size limit N blocks ahead. In other words, this function
    /// returns the limit for block with current+N height, assuming all blocks 100% full.
    ///
    /// Also note that since this is a worst-case calculation, assumption is that all future blocks use
    /// `paranoidWorseCaseBlockInterval` (default: BASE_BLOCK_INTERVAL = 10 mins) as their block time.
    ///
    /// This is computed in O(1) with a closed-form bound (CHIP-2025-03, "Block download"), not by iterating the
    /// algorithm `count` times. The result is therefore an UPPER BOUND on the exact iterated value: equal to it for
    /// `count <= 1`, and never below it. Since the result is only used to size buffers / as a "guess" of a future
    /// limit, a slightly loose upper bound is safe; callers must not rely on exact equality with the iterated value.
    uint64_t CalcLookaheadBlockSizeLimitWorstCase(const Config &config, size_t count,
                                                  BlockTicks paranoidWorseCaseBlockInterval = BASE_BLOCK_INTERVAL,
                                                  bool disable2GBCap = false) const;

    // -- Getter for this block's interval
    constexpr BlockTicks GetBlockInterval() const noexcept { return blockInterval; }

    // -- To/From a tuple (mainly used by tests) --

    constexpr std::tuple<uint64_t, uint64_t, uint64_t, BlockTicks> ToTuple() const noexcept {
        return std::tuple(blockSize, controlBlockSize, elasticBufferSize, blockInterval);
    }
    static constexpr State FromTuple(const std::tuple<uint64_t, uint64_t, uint64_t, BlockTicks> &tup) noexcept {
        const auto & [bs, cbs, ebs, bi] = tup;
        return State{bs, cbs, ebs, bi};
    }

    // -- Comparison ops --

    constexpr auto operator<=>(const State &o) const noexcept { return ToTuple() <=> o.ToTuple(); }
    constexpr bool operator==(const State &o) const noexcept { return ToTuple() == o.ToTuple(); }
};

// --- Some constants ---
// These are exposed here for unit tests, normally client code shouldn't need these.

/**
 * Constant 2^7, used as fixed precision for algorithm's "asymmetry factor" configuration value, e.g. we will
 * store the real number 1.5 as integer 192 so when we want to multiply or divide an integer with value of 1.5,
 * we will do muldiv(value, 192, B7) or muldiv(value, B7, 192).
 */
inline constexpr uint64_t B7 = 1u << 7u;

// Sanity ranges for configuration values
inline constexpr uint64_t MIN_ZETA_XB7 = 129u; ///< zeta real value of 1.0078125
inline constexpr uint64_t MAX_ZETA_XB7 = 256u; ///< zeta real value of 2.0000000
inline constexpr uint64_t MIN_GAMMA_RECIPROCAL = 9484u;
inline constexpr uint64_t MAX_GAMMA_RECIPROCAL = 151744u;
inline constexpr uint64_t MIN_DELTA = 0u;
inline constexpr uint64_t MAX_DELTA = 32u;
inline constexpr uint64_t MIN_THETA_RECIPROCAL = 9484u;
inline constexpr uint64_t MAX_THETA_RECIPROCAL = 151744u;

} // namespace abla
