// Copyright (c) 2017-present The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <config.h>

#include <chainparams.h>
#include <policy/policy.h> // MAX_INV_BROADCAST_*
#include <util/overloaded.h>

#include <algorithm>

GlobalConfig::GlobalConfig()
    : useCashAddr(DEFAULT_USE_CASHADDR), gbtCheckValidity(DEFAULT_GBT_CHECK_VALIDITY),
      allowUnconnectedMining(DEFAULT_ALLOW_UNCONNECTED_MINING),
      // NB: The generated block size is normally set in init.cpp to use chain-specific
      //     defaults which are often smaller than the DEFAULT_CONSENSUS_BLOCK_SIZE.
      varGeneratedBlockSizeParam(100.0),
      nMaxMemPoolSize(DEFAULT_CONSENSUS_BLOCK_SIZE * DEFAULT_MAX_MEMPOOL_SIZE_PER_MB) {}

bool GlobalConfig::SetBlockSizeOverride(std::optional<uint64_t> optBlockSize10Mins) {
    if (!optBlockSize10Mins) {
        blockSize10MinsOverride.reset();
        return true;
    }
    const uint64_t &blockSize10Mins = *optBlockSize10Mins;

    // Do not allow maxBlockSize to be set below historic 1MB limit
    // It cannot be equal either because of the "must be big" UAHF rule.
    if (blockSize10Mins <= LEGACY_MAX_BLOCK_SIZE) {
        return false;
    }

    // We limit this block size parameter to what the machine can physically address on 32-bit (2GB)
    if (blockSize10Mins > MAX_CONSENSUS_BLOCK_SIZE) {
        return false;
    }

    blockSize10MinsOverride = blockSize10Mins;

    return true;
}

void GlobalConfig::NotifyMaxBlockSizeLookAheadGuessChanged(uint64_t nSize10Mins) const {
    nMaxBlockSizeWorstCaseGuess10Mins = nSize10Mins;
}

uint64_t GlobalConfig::GetMaxBlockSizeLookAheadGuess() const {
    return std::clamp(nMaxBlockSizeWorstCaseGuess10Mins.load(), GetDefaultConsensusBlockSize(legacy::POW_TARGET_SPACING_TICKS),
                      MAX_CONSENSUS_BLOCK_SIZE);
}

uint64_t GlobalConfig::GetDefaultConsensusBlockSize(const BlockTicks blockInterval) const {
    const uint64_t nSize10Mins = blockSize10MinsOverride.value_or(GetChainParams().GetConsensus()
                                                                      .nDefaultConsensusBlockSize10Mins);
    // Scale based on blockInterval
    const uint64_t nSizeScaled = nSize10Mins * blockInterval / legacy::POW_TARGET_SPACING_TICKS;
    return std::min(nSizeScaled, MAX_CONSENSUS_BLOCK_SIZE);
}

void  GlobalConfig::SetGeneratedBlockSizeBytes(uint64_t blockSize10Mins) {
    varGeneratedBlockSizeParam = blockSize10Mins;
}

bool GlobalConfig::SetGeneratedBlockSizePercent(double percent) {
    if (percent < 0.0 || percent > 100.0) {
        return false;
    }

    varGeneratedBlockSizeParam = percent;
    return true;
}

bool GlobalConfig::SetInvBroadcastRate(uint64_t rate) {
    if (rate > MAX_INV_BROADCAST_RATE) return false;
    nInvBroadcastRate = rate;
    return true;
}

bool GlobalConfig::SetInvBroadcastInterval(uint64_t interval) {
    if (interval > MAX_INV_BROADCAST_INTERVAL) return false;
    nInvBroadcastInterval = interval;
    return true;
}

uint64_t GlobalConfig::GetGeneratedBlockSize(const BlockTicks blockInterval,
                                             std::optional<uint64_t> currentMaxBlockSizeScaled) const {
    uint64_t blockSizeScaled;
    const uint64_t maxBlockSizeScaled = currentMaxBlockSizeScaled ? *currentMaxBlockSizeScaled
                                                                  : GetDefaultConsensusBlockSize(blockInterval);

    std::visit(
        util::Overloaded{
            [&blockSizeScaled, blockInterval](uint64_t val10Mins) {
                blockSizeScaled = val10Mins * blockInterval / legacy::POW_TARGET_SPACING_TICKS;
            },
            [&blockSizeScaled, maxBlockSizeScaled](double percent) {
                blockSizeScaled = static_cast<uint64_t>(maxBlockSizeScaled * (percent / 100.0));
            }
        }, varGeneratedBlockSizeParam);

    // Maintain invariant: ensure that blockSize <= maxBlockSize
    blockSizeScaled = std::min(maxBlockSizeScaled, blockSizeScaled);

    return blockSizeScaled;
}

const CChainParams &GlobalConfig::GetChainParams() const {
    return Params();
}

static GlobalConfig gConfig;

const Config &GetConfig() {
    return gConfig;
}

Config &GetMutableConfig() {
    return gConfig;
}

void GlobalConfig::SetCashAddrEncoding(bool c) {
    useCashAddr = c;
}
bool GlobalConfig::UseCashAddrEncoding() const {
    return useCashAddr;
}

DummyConfig::DummyConfig()
    : chainParams(CreateChainParams(CBaseChainParams::REGTEST)) {}

DummyConfig::DummyConfig(const std::string &net)
    : chainParams(CreateChainParams(net)) {}

DummyConfig::DummyConfig(std::unique_ptr<CChainParams> chainParamsIn)
    : chainParams(std::move(chainParamsIn)) {}

void DummyConfig::SetChainParams(const std::string &net) {
    chainParams = CreateChainParams(net);
}

void GlobalConfig::SetExcessUTXOCharge(Amount fee) {
    excessUTXOCharge = fee;
}

Amount GlobalConfig::GetExcessUTXOCharge() const {
    return excessUTXOCharge;
}
