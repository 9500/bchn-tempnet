// Copyright (c) 2023-present The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifdef HAVE_CONFIG_H
#include <config/bitcoin-config.h>
#endif

#if HAVE_INT128
namespace { using u128 = unsigned __int128; }
#else
// On arm32 or other platforms lacking the __int128 type we emulate this type using our codebase's arith_uint256
// type. This is slow-ish but it works.
#include <arith_uint256.h>
namespace {
struct u128 : arith_uint256 {
    using arith_uint256::arith_uint256;
    explicit operator uint64_t() const { return GetLow64(); }
};
} // namespace
#endif

#include <consensus/abla.h>

#include <logging.h>
#include <tinyformat.h>
#include <util/defer.h>

#include <algorithm>
#include <cassert>
#include <limits>

namespace abla {

namespace {

/// Utility function for fixed-point math. Multiplies x by y as uint128's, then divides by z, returning the result.
/// Precondition: z must not be 0; the expression x * y / z must not exceed the 2^64 - 1.
inline uint64_t muldiv(uint64_t x, uint64_t y, uint64_t z) {
    assert(z != 0);
    const u128 res = (static_cast<u128>(x) * static_cast<u128>(y)) / static_cast<u128>(z);
    assert(res <= static_cast<u128>(std::numeric_limits<uint64_t>::max()));
    return static_cast<uint64_t>(res);
}

} // namespace

void Config::SetMax() {
    const uint64_t maxSafeBlocksizeLimit = std::numeric_limits<uint64_t>::max() / zeta_xB7 * B7;

    // elastic_buffer_ratio_max = (delta * gamma / theta * (zeta - 1)) / (gamma / theta * (zeta - 1) + 1)
    const uint64_t maxElasticBufferRatioNumerator = delta * ((zeta_xB7 - B7) * thetaReciprocal / gammaReciprocal);
    const uint64_t maxElasticBufferRatioDenominator = (zeta_xB7 - B7) * thetaReciprocal / gammaReciprocal + B7;

    epsilonMax = maxSafeBlocksizeLimit / (maxElasticBufferRatioNumerator + maxElasticBufferRatioDenominator) * maxElasticBufferRatioDenominator;
    betaMax = maxSafeBlocksizeLimit - epsilonMax;

    LogPrint(BCLog::ABLA, "[ABLA] Auto-configured epsilonMax: %u, betaMax: %u\n", epsilonMax, betaMax);
}

bool Config::IsValid(const char **err) const {
    const char *errStr = nullptr;
    Defer d([this, &errStr, &err]{
        // on function return, print to debug log if there was an error, and/or optionally set `err` out param.
        if (errStr) {
            LogPrint(BCLog::ABLA, "[ABLA] Config::IsValid: %s - %s\n", errStr, ToString());
            if (err) { *err = errStr; }
        } else if (err) {
            *err = "";
        }
    });
    if (epsilon0 > epsilonMax) {
        errStr = "Error, initial control block size limit sanity check failed (epsilonMax)";
        return false;
    }
    if (beta0 > betaMax) {
        errStr = "Error, initial elastic buffer size sanity check failed (betaMax)";
        return false;
    }
    if (zeta_xB7 < MIN_ZETA_XB7 || zeta_xB7 > MAX_ZETA_XB7) {
        errStr = "Error, zeta sanity check failed";
        return false;
    }
    if (gammaReciprocal < MIN_GAMMA_RECIPROCAL || gammaReciprocal > MAX_GAMMA_RECIPROCAL) {
        errStr = "Error, gammaReciprocal sanity check failed";
        return false;
    }
    if (delta + 1u <= MIN_DELTA || delta > MAX_DELTA) {
        errStr = "Error, delta sanity check failed";
        return false;
    }
    if (thetaReciprocal < MIN_THETA_RECIPROCAL || thetaReciprocal > MAX_THETA_RECIPROCAL) {
        errStr = "Error, thetaReciprocal sanity check failed";
        return false;
    }
    if (epsilon0 < muldiv(gammaReciprocal, B7, zeta_xB7 - B7)) {
        // Required due to truncation of integer ops.
        // With this we ensure that the control size can be adjusted for at least 1 byte.
        // Also, with this we ensure that divisor bytesMax in State::NextBlockState() can't be 0.
        errStr = "Error, epsilon0 sanity check failed. Too low relative to gamma and zeta.";
        return false;
    }
    return true;
}

bool Config::IsFixedSize() const {
    return epsilon0 == epsilonMax && beta0 == betaMax;
}


std::string Config::ToString() const {
    return strprintf("abla::Config(epsilon0=%u, beta0=%u, gammaReciprocal=%u, zeta_xB7=%u, thetaReciprocal=%u"
                     ", delta=%u, epsilonMax=%u, betaMax=%u)",
                     epsilon0, beta0, gammaReciprocal, zeta_xB7, thetaReciprocal, delta, epsilonMax, betaMax);
}

/* static */
Config Config::MakeDefault(uint64_t defaultBlockSize, bool fixedSize) {
    Config ret;
    ret.epsilon0 = defaultBlockSize / 2u;
    ret.beta0 = defaultBlockSize / 2u;
    ret.gammaReciprocal = 37938;
    ret.zeta_xB7 = 192;
    ret.thetaReciprocal = 37938;
    ret.delta = 10;
    if ( ! fixedSize) {
        // Auto-set epsilonMax and betaMax to huge, 64-bit safe values
        ret.SetMax();
    } else {
        // Fixed-size, rendering this EBAA algorithm a no-op that always returns `defaultBlockSize` (testnets 3 & 4)
        ret.epsilonMax = ret.epsilon0;
        ret.betaMax = ret.beta0;
    }
    return ret;
}

uint64_t StateBase::GetBlockSizeLimitBase(BlockTicks thisBlockInterval, bool disable2GBCap) const {
    uint64_t val = muldiv(controlBlockSize + elasticBufferSize, thisBlockInterval.value(), BASE_BLOCK_INTERVAL.value());
    if (!disable2GBCap) {
        val = std::min<uint64_t>(val, MAX_CONSENSUS_BLOCK_SIZE);
    }
    return val;
}

StateBase StateBase::NextBlockStateBase(const Config &config, BlockTicks thisBlockInterval, uint64_t nextBlockSize) const {
    StateBase ret;

    ret.blockSize = nextBlockSize; // save the blocksize for the next block to its State

    // control function

    // For safety: we clamp this current block's blocksize to the maximum value this algorithm expects. Normally this
    // won't happen unless the node is run with some -forceblocksize parameter that permits larger blocks than this
    // algo's current state expects.
    const uint64_t clampedBlockSize = std::min(this->blockSize,
                                               muldiv(this->controlBlockSize + this->elasticBufferSize,
                                                      thisBlockInterval.value(), BASE_BLOCK_INTERVAL.value()));

    // zeta * x_{n-1}
    // Note: We determine the amplified block size from `clampedBlockSize`, not from `nextBlockSize`.
    const uint64_t amplifiedCurrentBlockSize =
        muldiv(config.zeta_xB7, muldiv(clampedBlockSize, BASE_BLOCK_INTERVAL.value(), thisBlockInterval.value()), B7);

    // if zeta * x_{n-1} > epsilon_{n-1} then increase
    if (amplifiedCurrentBlockSize > this->controlBlockSize) {
        // zeta * x_{n-1} - epsilon_{n-1}
        const uint64_t bytesToAdd = amplifiedCurrentBlockSize - this->controlBlockSize;

        // zeta * y_{n-1}
        const uint64_t amplifiedBlockSizeLimit = muldiv(config.zeta_xB7, this->controlBlockSize + this->elasticBufferSize, B7);

        // zeta * y_{n-1} - epsilon_{n-1}
        const uint64_t bytesMax = amplifiedBlockSizeLimit - this->controlBlockSize;

        // zeta * beta_{n-1} * (zeta * x_{n-1} - epsilon_{n-1}) / (zeta * y_{n-1} - epsilon_{n-1})
        const uint64_t scalingOffset = muldiv(muldiv(config.zeta_xB7, this->elasticBufferSize, B7),
                                              bytesToAdd, bytesMax);

        // epsilon_n = epsilon_{n-1} + gamma * (zeta * x_{n-1} - epsilon_{n-1} - zeta * beta_{n-1} * (zeta * x_{n-1} - epsilon_{n-1}) / (zeta * y_{n-1} - epsilon_{n-1}))
        ret.controlBlockSize = this->controlBlockSize + muldiv((bytesToAdd - scalingOffset), thisBlockInterval.value(),
                                                               config.gammaReciprocal * BASE_BLOCK_INTERVAL.value());
    }
    // if zeta * x_{n-1} <= epsilon_{n-1} then decrease or no change
    else {
        // epsilon_{n-1} - zeta * x_{n-1}
        const uint64_t bytesToRemove = this->controlBlockSize - amplifiedCurrentBlockSize;

        // epsilon_{n-1} + gamma * (zeta * x_{n-1} - epsilon_{n-1})
        // rearranged to:
        // epsilon_{n-1} - gamma * (epsilon_{n-1} - zeta * x_{n-1})
        ret.controlBlockSize = this->controlBlockSize - muldiv(bytesToRemove, thisBlockInterval.value(),
                                                               config.gammaReciprocal * BASE_BLOCK_INTERVAL.value());

        // epsilon_n = max(epsilon_{n-1} + gamma * (zeta * x_{n-1} - epsilon_{n-1}), epsilon_0)
        ret.controlBlockSize = std::max(ret.controlBlockSize, config.epsilon0);
    }

    // elastic buffer function

    // beta_{n-1} * theta
    const uint64_t bufferDecay = muldiv(this->elasticBufferSize, thisBlockInterval.value(),
                                        config.thetaReciprocal * BASE_BLOCK_INTERVAL.value());

    // if zeta * x_{n-1} > epsilon_{n-1} then increase
    if (amplifiedCurrentBlockSize > this->controlBlockSize) {
        // (epsilon_{n} - epsilon_{n-1}) * delta
        const uint64_t bytesToAdd = (ret.controlBlockSize - this->controlBlockSize) * config.delta;

        // beta_{n-1} - beta_{n-1} * theta + (epsilon_{n} - epsilon_{n-1}) * delta
        ret.elasticBufferSize = this->elasticBufferSize - bufferDecay + bytesToAdd;
    }
    // if zeta * x_{n-1} <= epsilon_{n-1} then decrease or no change
    else {
        // beta_{n-1} - beta_{n-1} * theta
        ret.elasticBufferSize = this->elasticBufferSize - bufferDecay;
    }
    // max(beta_{n-1} - beta_{n-1} * theta + (epsilon_{n} - epsilon_{n-1}) * delta, beta_0) , if zeta * x_{n-1} > epsilon_{n-1}
    // max(beta_{n-1} - beta_{n-1} * theta, beta_0) , if zeta * x_{n-1} <= epsilon_{n-1}
    ret.elasticBufferSize = std::max(ret.elasticBufferSize, config.beta0);

    // clip controlBlockSize to epsilonMax to avoid integer overflow for extreme sizes
    ret.controlBlockSize = std::min(ret.controlBlockSize, config.epsilonMax);
    // clip elasticBufferSize to betaMax to avoid integer overflow for extreme sizes
    ret.elasticBufferSize = std::min(ret.elasticBufferSize, config.betaMax);

    return ret;
}

namespace {

/// Closed-form upper bound on `controlBlockSize + elasticBufferSize` after `k` further transitions, starting from a
/// state whose own block is full (blockSize == its limit) with interval `tau`, where every following block is also
/// full and also has interval `tau`. Exact for k == 0. (CHIP-2025-03, "Block download".)
///
/// For a full block the amplified size equals the amplified limit, so the scaling offset cancels and the control step
/// is d = floor(eps * G / D), with G = tau * (zeta - B7) and D = 600 * B7 * gammaReciprocal. Hence
///     eps_{n+1} <= eps_n * (1 + G/D)
///     s_{n+1}   <= s_n + (1 + delta) * eps_n * G/D        (elastic buffer decay dropped)
/// where s = eps + beta. The epsilonMax / betaMax clips only lower a component, so the bound survives them. Summing
/// the geometric series and applying AM-GM, (1 + g)^k * (1 - k*g) <= 1, gives
///     s_k <= s_0 + (1 + delta) * eps_0 * k*G / (D - k*G),    for k*G < D.
/// Every integer step of the real drive truncates downward, so flooring the right-hand side stays an upper bound.
uint64_t BoundSumAfterFullBlocks(const Config &config, uint64_t tau, uint64_t s0, uint64_t eps0, uint64_t k) {
    if (k == 0) {
        return s0;
    }

    const uint64_t G = tau * (config.zeta_xB7 - B7); // >= 1, since zeta_xB7 >= MIN_ZETA_XB7 > B7 and tau >= 1
    const uint64_t D = BASE_BLOCK_INTERVAL.value() * B7 * config.gammaReciprocal;
    const u128 sat = static_cast<u128>(config.epsilonMax) + static_cast<u128>(config.betaMax);

    // Past the radius of the AM-GM form the bound is vacuous; fall back to the trivial maximum (a clip only lowers a
    // component, so eps + beta can never exceed epsilonMax + betaMax). This also keeps D - k*G positive.
    // For the mainnet parameters at tau = 600, D / G = 75876.
    if (k >= D / G) {
        return static_cast<uint64_t>(sat);
    }

    const uint64_t kG = k * G; // < D after the guard above, so no overflow
    const u128 inc = static_cast<u128>(1 + config.delta) * static_cast<u128>(eps0) * static_cast<u128>(kG)
                     / static_cast<u128>(D - kG);
    return static_cast<uint64_t>(std::min<u128>(static_cast<u128>(s0) + inc, sat));
}

} // namespace

uint64_t State::CalcLookaheadBlockSizeLimitWorstCase(const Config &config, size_t count, const BlockTicks tau,
                                                     bool disable2GBCap) const {
    if (count == 0) {
        return GetBlockSizeLimit(disable2GBCap);
    }

    assert(tau >= MIN_BLOCK_INTERVAL && tau <= MAX_BLOCK_INTERVAL);

    // The first transition is done exactly: it responds to this state's own stored block size and interval, and its
    // result (a full block with interval `tau`) is the state the remaining `count - 1` blocks start from.
    const State s = NextBlockState(config, tau, GetNextBlockSizeLimit(config, tau, disable2GBCap));

    const uint64_t eps = s.GetControlBlockSize();
    const uint64_t bound = BoundSumAfterFullBlocks(config, tau.value(), eps + s.GetElasticBufferSize(), eps,
                                                   static_cast<uint64_t>(count - 1));

    // Same readback as GetBlockSizeLimitBase(): scale by the interval, then apply the 2GB cap (if enabled).
    uint64_t limit = muldiv(bound, tau.value(), BASE_BLOCK_INTERVAL.value());
    if (!disable2GBCap) {
        limit = std::min<uint64_t>(limit, MAX_CONSENSUS_BLOCK_SIZE);
    }
    return limit;
}

bool State::IsValid(const Config &config, const char **err) const {
    const char *errStr = nullptr;
    Defer d([this, &errStr, &err]{
        // on function return, print to debug log if there was an error, and/or optionally set `err` out param.
        if (errStr) {
            LogPrint(BCLog::ABLA, "[ABLA] State::IsValid: %s - %s\n", errStr, ToString());
            if (err) { *err = errStr; }
        } else if (err) {
            *err = "";
        }
    });
    if (blockInterval < MIN_BLOCK_INTERVAL || blockInterval > MAX_BLOCK_INTERVAL) {
        errStr = "Error, invalid blockInterval state. Must be in allowed range [MIN_BLOCK_INTERVAL, MAX_BLOCK_INTERVAL].";
        return false;
    }
    if (controlBlockSize < config.epsilon0 || controlBlockSize > config.epsilonMax) {
        errStr = "Error, invalid controlBlockSize state. Can't be below initialization value nor above epsilonMax.";
        return false;
    }
    if (elasticBufferSize < config.beta0 || elasticBufferSize > config.betaMax) {
        errStr = "Error, invalid elasticBufferSize state. Can't be below initialization value nor above betaMax.";
        return false;
    }
    return true;
}

std::string State::ToString() const {
    return strprintf("abla::State(blockSize=%u, controlBlockSize=%u, elasticBufferSize=%u, blockInterval=%i)",
                     blockSize, controlBlockSize, elasticBufferSize, blockInterval.toSecs());
}

} // namespace abla
