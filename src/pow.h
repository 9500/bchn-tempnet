// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2020-present The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include <arith_uint256.h>

#include <cstdint>

struct BlockHash;
class BlockTicks;
class CBlockHeader;
class CBlockIndex;
class uint256;

namespace Consensus {
struct Params;
}

uint32_t GetNextWorkRequired(const CBlockIndex *pindexPrev,
                             const CBlockHeader *pblock,
                             const Consensus::Params &params);
namespace legacy {
// Legacy work required calc, used by legacy Bitcoin chain and EDA DAA, but not new DAAs such as CW144 or ASERT.
uint32_t CalculateNextWorkRequired(const CBlockIndex *pindexPrev, int64_t nFirstBlockTime,
                                   const Consensus::Params &params);
} // namespace legacy

/**
 * Check whether a block hash satisfies the proof-of-work requirement specified
 * by nBits
 */
bool CheckProofOfWork(const BlockHash &hash, uint32_t nBits,
                      const Consensus::Params &params);

/**
 * Bitcoin cash's difficulty adjustment mechanism.
 */
uint32_t GetNextCashWorkRequired(const CBlockIndex *pindexPrev,
                                 const CBlockHeader *pblock,
                                 const Consensus::Params &params);

//! Was used when we based everything off of block height (pre-FABLOUS CHIP)
arith_uint256 CalculateASERT_HeightBased(const arith_uint256 &refTarget,
                                         const int64_t nPowTargetSpacing,
                                         const int64_t nTimeDiff,
                                         const int64_t nHeightDiff,
                                         const arith_uint256 &powLimit,
                                         const int64_t nHalfLife) noexcept;

//! New function used after we base everything off of BlockTicks (FABLOUS CHIP).
arith_uint256 CalculateASERT_TickBased(const arith_uint256 &refTarget,
                                       const int64_t nAnchorIdealBlockTime,
                                       const int64_t nNextIdealBlockTime,
                                       const int64_t nTimeDiff,
                                       const int64_t nTickDiff,
                                       const arith_uint256 &powLimit,
                                       const int64_t nHalfLife) noexcept;

uint32_t GetNextASERTWorkRequired(const CBlockIndex *pindexPrev,
                                  const CBlockHeader *pblock,
                                  const Consensus::Params &params,
                                  const CBlockIndex *pindexAnchorBlock) noexcept;

/**
 * ASERT caches a special block index for efficiency. If block indices are
 * freed then this needs to be called to ensure no dangling pointer when a new
 * block tree is created.
 * (this is temporary and will be removed after the ASERT constants are fixed)
 */
void ResetASERTAnchorBlockCache() noexcept;

/**
 * For testing purposes - get the current ASERT cache block.
 */
const CBlockIndex *GetASERTAnchorBlockCache() noexcept;

/**
 *  Get the target block time, in ticks. Always a positive value. This is 600 seconds by default until Upgrade2027
 *  is enabled, at which time it switches to 60 seconds.
 */
BlockTicks GetPowTargetSpacing(const Consensus::Params &params, const CBlockIndex *pindexPrev);
