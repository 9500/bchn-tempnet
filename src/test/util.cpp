// Copyright (c) 2019 The Bitcoin Core developers
// Copyright (c) 2020-present The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/util.h>

#include <chain.h>
#include <chainparams.h>
#include <config.h>
#include <consensus/activation.h>
#include <consensus/consensus.h>
#include <consensus/merkle.h>
#include <miner.h>
#include <primitives/transaction.h>
#include <pow.h>
#include <util/defer.h>
#include <validation.h>

#include <zlib.h>

#include <algorithm>
#include <cassert>
#include <stdexcept>
#include <optional>

std::pair<COutPoint, Amount>
MineBlock(const Config &config, const CScript &coinbase_scriptPubKey, const std::vector<CTransactionRef> &extraTxns) {
    auto block = PrepareBlock(config, coinbase_scriptPubKey, extraTxns);

    while (!CheckProofOfWork(block->GetHash(), block->nBits, config.GetChainParams().GetConsensus())) {
        ++block->nNonce;
        assert(block->nNonce);
    }

    bool processed{ProcessNewBlock(config, block, true, nullptr)};
    assert(processed);

    const auto &tx = *block->vtx.at(0);
    return {std::piecewise_construct,
            std::forward_as_tuple(tx.GetId(), 0), std::forward_as_tuple(tx.vout.at(0).nValue)};
}

std::shared_ptr<CBlock> PrepareBlock(const Config &config,
                                     const CScript &coinbase_scriptPubKey,
                                     const std::vector<CTransactionRef> &extraTxns) {
    const CBlockIndex *tip{};
    auto block = std::make_shared<CBlock>(std::move(
        BlockAssembler{config, ::g_mempool}.CreateNewBlock(coinbase_scriptPubKey, 0, true, &tip)->block
    ));
    assert(tip != nullptr);
    assert(tip->GetBlockHash() == block->hashPrevBlock);
    // Set nTime based on tip + 1
    block->nTime = tip->GetMedianTimePast() + 1;

    // Add any extra txns specified
    if (!extraTxns.empty()) {
        block->vtx.reserve(block->vtx.size() + extraTxns.size());
        block->vtx.insert(block->vtx.end(), extraTxns.begin(), extraTxns.end());
        // Sort if we added txns and we have magnetic anomaly enabled
        if (IsMagneticAnomalyEnabled(config.GetChainParams().GetConsensus(), tip)) {
            std::sort(block->vtx.begin() + 1, block->vtx.end(), [](const auto &a, const auto &b){
                return a->GetId() < b->GetId();
            });
        }
    }

    // Recalculate merkle root since we touched header and/or possibly txns
    block->hashMerkleRoot = BlockMerkleRoot(*block);

    return block;
}

size_t MatureCurrentTip(const Config &config, const CScript *scriptPubKey) {
    // we must force-disable block index checking while we mine blocks, since it slows this process down for 1000-block chains somewhat
    const auto raiiReEnabler = TemporarilyDisableCheckBlockIndex(config);

    std::optional<CScript> tmpSPK;
    if (!scriptPubKey) {
        scriptPubKey = &(tmpSPK.emplace() << OP_TRUE);
    }
    auto GetTicks = [](bool next = false) {
        LOCK(cs_main);
        const CBlockIndex * const tip = ::ChainActive().Tip();
        return tip ? tip->GetBlockTicks() + (next ? tip->GetTickDeltaForNextBlock() : BlockTicks{0}) : BlockTicks{0};
    };
    const auto maturity = GetTicks() + kCoinbaseMaturityTicks;
    size_t ct{};
    while (GetTicks(true) < maturity) {
        MineBlock(config, *scriptPubKey);
        ++ct;
    }
    return ct;
}

[[nodiscard]] std::any TemporarilyDisableCheckBlockIndex(const Config &config) {
    if (!fCheckBlockIndex) {
        // was already disabled, just bail without doing anything
        return {};
    }
    // was enabled, disable and return an RAII functor that will re-enable and run CheckBlockIndex on lifetime end
    fCheckBlockIndex = false;
    auto ReEnabler = [&params = config.GetChainParams().GetConsensus()]{
        fCheckBlockIndex = true;
        CheckBlockIndex(params);
    };
    // We use the "Defer" type here that auto-calls a functor on destruction.
    return std::make_shared<const Defer<decltype(ReEnabler)>>(std::move(ReEnabler));
}

Span<uint8_t> UncompressInPlace(Span<uint8_t> outputBuf, Span<const uint8_t> compressedBytes) {
    const size_t uncompBufSpace = outputBuf.size();
    Span<const Bytef> compressed(reinterpret_cast<const Bytef *>(compressedBytes.data()), compressedBytes.size());
    Bytef *outb = reinterpret_cast<Bytef *>(outputBuf.data());
    unsigned long uncompSzResult = uncompBufSpace;
    const auto r = uncompress(outb, &uncompSzResult, compressed.data(), compressed.size());
    if (r != Z_OK) {
        throw std::runtime_error(strprintf("zlib uncompress returned %i", r));
    }
    assert(uncompSzResult <= uncompBufSpace);
    return outputBuf.subspan(0, uncompSzResult);
}

namespace {
template <typename Container>
Container UncompressGeneric(Span<const uint8_t> compressedBytes, const size_t uncompSz) {
    Container uncompressedData;
    uncompressedData.resize(uncompSz);
    auto span = MakeUInt8Span(uncompressedData);
    span = UncompressInPlace(span, compressedBytes);
    uncompressedData.resize(span.size());
    return uncompressedData;
}
}

std::string UncompressStr(Span<const uint8_t> compressedBytes, size_t uncompressedByteSize) {
    return UncompressGeneric<std::string>(compressedBytes, uncompressedByteSize);
}

std::vector<uint8_t> Uncompress(Span<const uint8_t> compressedBytes, size_t uncompressedByteSize) {
    return UncompressGeneric<std::vector<uint8_t>>(compressedBytes, uncompressedByteSize);
}

Upgrade2027Disabler::Upgrade2027Disabler() : saved_g_Upgrade2027TimeOverride{::g_Upgrade2027TimeOverride} {
    /* Note: a time ridiculously far into the future -- we don't use INT64_MAX here to avoid potential overlflow in
       software_outdated.cpp */
    ::g_Upgrade2027TimeOverride = (int64_t{1} << 48) - 1;
}

Upgrade2027Disabler::~Upgrade2027Disabler() {
    ::g_Upgrade2027TimeOverride = saved_g_Upgrade2027TimeOverride;
}
