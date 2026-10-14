// Copyright (c) 2018-present The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <consensus/tx_verify.h>

#include <amount.h>
#include <chain.h>
#include <coins.h>
#include <consensus/activation.h>
#include <consensus/blockticks.h>
#include <consensus/consensus.h>
#include <consensus/params.h>
#include <consensus/validation.h>
#include <node/blockheighttotickmapper.h>
#include <primitives/transaction.h>
#include <script/script_flags.h>
#include <util/check.h>
#include <util/moneystr.h> // For FormatMoney
#include <version.h>       // For PROTOCOL_VERSION

static bool IsFinalTx(const CTransaction &tx, const BlockTicks &blockTicks, const int64_t nBlockTime) {
    if (tx.nLockTime == 0) {
        return true;
    }

    if (tx.nLockTime < LOCKTIME_THRESHOLD) {
        // Height-based locktime -- as per the FABLOUS CHIP, we must translate txn locktime into ticks for this
        // comparison. Note that this change is backward compatible with legacy 10-min blocks.
        const BlockTicks lockTimeAsTicks = legacy::POW_TARGET_SPACING_TICKS * tx.nLockTime;
        if (lockTimeAsTicks < blockTicks) {
            return true;
        }
    } else {
        // Time-based locktime -- no translation necessary, compare with block time.
        if (static_cast<int64_t>(tx.nLockTime) < nBlockTime) {
            return true;
        }
    }

    for (const auto &txin : tx.vin) {
        if (txin.nSequence != CTxIn::SEQUENCE_FINAL) {
            return false;
        }
    }
    return true;
}

static uint64_t GetMinimumTxSize(const Consensus::Params &params, int nHeightPrev) {
    if (IsUpgrade9EnabledForHeightPrev(params, nHeightPrev)) {
        return MIN_TX_SIZE_UPGRADE9;
    }
    if (IsMagneticAnomalyEnabled(params, nHeightPrev)) {
        return MIN_TX_SIZE_MAGNETIC_ANOMALY;
    }
    return 0;
}

uint64_t GetMinimumTxSize(const Consensus::Params &params, const CBlockIndex *pindexPrev) {
    if (!pindexPrev) return 0;
    return GetMinimumTxSize(params, pindexPrev->nHeight);
}

bool ContextualCheckTransaction(const Consensus::Params &params,
                                const CTransaction &tx, CValidationState &state,
                                const int nHeight, const int64_t nLockTimeCutoff,
                                const int64_t nMedianTimePastPrev,
                                const BlockTicks &blockTicks) {
    if (!IsFinalTx(tx, blockTicks, nLockTimeCutoff)) {
        // While this is only one transaction, we use txns in the error to
        // ensure continuity with other clients.
        return state.DoS(10, false, REJECT_INVALID, "bad-txns-nonfinal", false,
                         "non-final transaction");
    }

    // Enforce minimum tx size, if any
    // Note: nHeight is height of *this* block (and nMedianTimePastPrev is MTP of *prev* block),
    // but Is*Enabled() expects nHeight and MTP of *prev* block.
    const uint64_t minTxSize = GetMinimumTxSize(params, nHeight - 1);
    if (minTxSize && ::GetSerializeSize(tx, PROTOCOL_VERSION) < minTxSize) {
        return state.DoS(100, false, REJECT_INVALID, "bad-txns-undersize");
    }

    if (IsUpgrade9EnabledForHeightPrev(params, nHeight - 1)) {
        // CHIP 2021-01 Restrict Transaction Version
        if (tx.nVersion > CTransaction::MAX_CONSENSUS_VERSION || tx.nVersion < CTransaction::MIN_CONSENSUS_VERSION) {
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-version");
        }
    }

    if (tx.nVersion == 1 && tx.nLockTime == 0 /* do the cheap integer checks first for speed */
        && IsUpgrade2027EnabledForMTPPrev(params, nMedianTimePastPrev) && tx.IsCoinBase()) {
        // CHIP-2025-03 Faster Blocks for Bitcoin Cash:
        // To prevent collision with pre-BIP34 coinbase txns, the following rule was added after the upgrade:
        // "A coinbase transaction is invalid if its version is 1 AND its locktime is 0 AND the sum of its outputs is
        //  at least 25 BCH."
        if (/*tx.nVersion == 1 && tx.nLockTime == 0 &&*/ tx.GetValueOut() >= 25 * COIN) {
            return state.DoS(100, false, REJECT_INVALID, "bad-cb-shape", false,
                             "A coinbase transaction may not have the following combination:"
                             " nVersion==1 && nLockTime==0 && nValueOut >= 25 " + CURRENCY_UNIT);
        }
    }

    return true;
}

namespace {
/**
 * Calculates the block ticks and previous block's median time past at
 * which the transaction will be considered final in the context of BIP 68.
 * Also removes from the vector of input heights any entries which did not
 * correspond to sequence locked inputs as they do not affect the calculation.
 */
std::pair<std::optional<BlockTicks>, int64_t>
CalculateSequenceLocks(const CTransaction &tx, int flags, std::vector<int> &prevheights, const CBlockIndex &block,
                       const BlockHeightToTickMapper &mapper) {
    assert(prevheights.size() == tx.vin.size());

    // tx.nVersion is signed integer so requires cast to unsigned otherwise
    // we would be doing a signed comparison and half the range of nVersion
    // wouldn't support BIP 68.
    const bool fEnforceBIP68 = static_cast<uint32_t>(tx.nVersion) >= 2 && flags & LOCKTIME_VERIFY_SEQUENCE;

    // Do not enforce sequence numbers as a relative lock time
    // unless we have been instructed to
    if (!fEnforceBIP68) {
        return {std::nullopt, -1};
    }

    // Will be set to the equivalent ticks- and time-based nLockTime
    // values that would be necessary to satisfy all relative lock-
    // time constraints given our view of block chain history.
    // The semantics of nLockTime are the last invalid ticks/time, so
    // -1 to have the effect of any ticks or time being valid.
    int64_t minTicks = -1;
    int64_t nMinTime = -1;

    for (size_t txinIndex = 0; txinIndex < tx.vin.size(); ++txinIndex) {
        const CTxIn &txin = tx.vin[txinIndex];

        // Sequence numbers with the most significant bit set are not
        // treated as relative lock-times, nor are they given any
        // consensus-enforced meaning at this point.
        if (txin.nSequence & CTxIn::SEQUENCE_LOCKTIME_DISABLE_FLAG) {
            // The height of this input is not relevant for sequence locks
            prevheights[txinIndex] = 0;
            continue;
        }

        const int nCoinHeight = std::max(prevheights[txinIndex], 0); // Negative heights here make no sense; force min 0

        if (txin.nSequence & CTxIn::SEQUENCE_LOCKTIME_TYPE_FLAG) {
            const int64_t nCoinTime = Assert(block.GetAncestor(std::max(nCoinHeight - 1, 0)))->GetMedianTimePast();
            // NOTE: Subtract 1 to maintain nLockTime semantics.
            // BIP 68 relative lock times have the semantics of calculating the
            // first block or time at which the transaction would be valid. When
            // calculating the effective block time or height for the entire
            // transaction, we switch to using the semantics of nLockTime which
            // is the last invalid block time or height. Thus we subtract 1 from
            // the calculated time or height.

            // Time-based relative lock-times are measured from the smallest
            // allowed timestamp of the block containing the txout being spent,
            // which is the median time past of the block prior.
            nMinTime = std::max(
                nMinTime,
                nCoinTime +
                    int64_t((txin.nSequence & CTxIn::SEQUENCE_LOCKTIME_MASK)
                            << CTxIn::SEQUENCE_LOCKTIME_GRANULARITY) -
                    1);
        } else {
            // We need to scale the raw locktime "as if" we were still on 10-mins blocks in order to calculate in terms
            // of ticks.
            const int rawLockTime = static_cast<int>(txin.nSequence & CTxIn::SEQUENCE_LOCKTIME_MASK);
            const int64_t locktimeScaled = rawLockTime * legacy::POW_TARGET_SPACING_TICKS.toSecs();
            const BlockTicks coinTicks = mapper.HeightToTick(nCoinHeight);

            // We subtract 1 in the below expression to transform the failure threshold into the `>=` relationship
            // that `EvaluateSequenceLocks()` expects (rather than keeping it as `>` which is what it would be without
            // the below -1).
            minTicks = std::max(minTicks, coinTicks.toSecs() + locktimeScaled - 1);
        }
    }

    std::optional<BlockTicks> optMinTicks;
    if (minTicks >= 0) {
        optMinTicks = BlockTicks{static_cast<uint64_t>(minTicks)};
    }
    return {optMinTicks, nMinTime};
}

bool EvaluateSequenceLocks(const CBlockIndex &block, const std::pair<std::optional<BlockTicks>, int64_t> &lockPair) {
    assert(block.pprev);
    const int64_t nBlockTime = block.pprev->GetMedianTimePast();
    if (lockPair.first >= block.GetBlockTicks() || lockPair.second >= nBlockTime) {
        return false;
    }

    return true;
}
} // namespace

bool SequenceLocks(const CTransaction &tx, int flags, std::vector<int> &prevheights,
                   const CBlockIndex &block, const BlockHeightToTickMapper &mapper) {
    return EvaluateSequenceLocks(block, CalculateSequenceLocks(tx, flags, prevheights, block, mapper));
}

namespace Consensus {
bool CheckTxInputs(const CTransaction &tx, CValidationState &state, const CCoinsViewCache &inputs,
                   const BlockTicks &spendTicks, const BlockHeightToTickMapper &mapper, Amount &txfee) {
    assert(!tx.IsCoinBase()); // precondition of this function

    Amount nValueIn = Amount::zero();
    for (const auto &in : tx.vin) {
        const COutPoint &prevout = in.prevout;
        const Coin &coin = inputs.AccessCoin(prevout);

        // Is the actual input available?
        if (coin.IsSpent()) {
            return state.DoS(100, false, REJECT_INVALID,
                             "bad-txns-inputs-missingorspent", false,
                             strprintf("%s: inputs missing/spent", __func__));
        }

        // If prev is coinbase, check that it's matured
        if (coin.IsCoinBase()) {
            const auto coinTicks = mapper.HeightToTick(coin.GetHeight());
            const auto depthTicks = spendTicks > coinTicks ? spendTicks - coinTicks : BlockTicks{0};
            if (depthTicks < kCoinbaseMaturityTicks) {
                return state.Invalid(false, REJECT_INVALID, "bad-txns-premature-spend-of-coinbase",
                                     strprintf("tried to spend coinbase at tick depth %u, required depth: %u "
                                               "(coinTicks: %u, spendTicks: %u, coinHeight: %i)",
                                               depthTicks.value(), kCoinbaseMaturityTicks.value(),
                                               coinTicks.value(), spendTicks.value(), coin.GetHeight()));
            }
        }

        // Fail now if the locking script is guaranteed to fail evaluation later on in the pipeline
        if (coin.GetTxOut().scriptPubKey.IsUnspendable()) {
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-input-scriptpubkey-unspendable", false,
                             strprintf("%s: input scriptPubKey is unspendable", __func__));
        }

        // Check for negative or overflow input values
        nValueIn += coin.GetTxOut().nValue;
        if (!MoneyRange(coin.GetTxOut().nValue) || !MoneyRange(nValueIn)) {
            return state.DoS(100, false, REJECT_INVALID,
                             "bad-txns-inputvalues-outofrange");
        }
    }

    const Amount value_out = tx.GetValueOut();
    if (nValueIn < value_out) {
        return state.DoS(
            100, false, REJECT_INVALID, "bad-txns-in-belowout", false,
            strprintf("value in (%s) < value out (%s)", FormatMoney(nValueIn),
                      FormatMoney(value_out)));
    }

    // Tally transaction fees
    const Amount txfee_aux = nValueIn - value_out;
    if (!MoneyRange(txfee_aux)) {
        return state.DoS(100, false, REJECT_INVALID, "bad-txns-fee-outofrange");
    }

    txfee = txfee_aux;
    return true;
}
} // namespace Consensus
