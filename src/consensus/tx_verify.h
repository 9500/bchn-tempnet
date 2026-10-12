// Copyright (c) 2018-present The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include <cstdint>
#include <vector>

struct Amount;
class BlockTicks;
struct BlockHeightToTickMapper;
class CBlockIndex;
class CCoinsViewCache;
class CTransaction;
class CValidationState;

namespace Consensus {
struct Params;

/**
 * Check whether all inputs of this transaction are valid (no double spends and
 * amounts). This does not modify the UTXO set. This does not check scripts and
 * sigs.
 * @param[out] txfee Set to the transaction fee if successful.
 * Preconditions: tx.IsCoinBase() is false.
 */
bool CheckTxInputs(const CTransaction &tx, CValidationState &state,
                   const CCoinsViewCache &inputs,
                   const BlockTicks &spendTicks,
                   const BlockHeightToTickMapper &mapper,
                   Amount &txfee);

} // namespace Consensus

/**
 * Context-dependent validity checks for transactions. This doesn't check the
 * validity of the transaction against the UTXO set, but simply characteristics
 * that are susceptible to change over time such as feature
 * activation/deactivation and CLTV.
 *
 * Note that while `nHeight` and `blockTicks` are the height and tick count of the
 * current block for the transaction, `nMedianTimePastPrev` is the MTP of the
 * previous block.
 */
bool ContextualCheckTransaction(const Consensus::Params &params,
                                const CTransaction &tx, CValidationState &state,
                                int nHeight,
                                int64_t nLockTimeCutoff,
                                int64_t nMedianTimePastPrev,
                                const BlockTicks &blockTicks);

/**
 * Check if transaction is final per BIP 68 sequence numbers and can be included
 * in a block. Consensus critical. Takes as input a list of heights at which
 * tx's inputs (in order) confirmed.
 */
bool SequenceLocks(const CTransaction &tx, int flags,
                   std::vector<int> *prevHeights, const CBlockIndex &block);

/// Returns the minimum transaction size (100 for post-MagneticAnomaly, 65 for post-Upgrade9), or 0 if before those
/// two upgrades have activated (no enforced minimum).
uint64_t GetMinimumTxSize(const Consensus::Params &params, const CBlockIndex *pindexPrev);
