// Copyright (c) 2018-present The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <consensus/activation.h>

#include <chain.h>
#include <consensus/params.h>
#include <util/system.h>

static bool IsUAHFenabled(const Consensus::Params &params, int nHeight) {
    return nHeight >= params.uahfHeight;
}

bool IsUAHFenabled(const Consensus::Params &params,
                   const CBlockIndex *pindexPrev) {
    if (pindexPrev == nullptr) {
        return false;
    }

    return IsUAHFenabled(params, pindexPrev->nHeight);
}

static bool IsDAAEnabled(const Consensus::Params &params, int nHeight) {
    return nHeight >= params.daaHeight;
}

bool IsDAAEnabled(const Consensus::Params &params,
                  const CBlockIndex *pindexPrev) {
    if (pindexPrev == nullptr) {
        return false;
    }

    return IsDAAEnabled(params, pindexPrev->nHeight);
}

bool IsMagneticAnomalyEnabled(const Consensus::Params &params,
                              int32_t nHeight) {
    return nHeight >= params.magneticAnomalyHeight;
}

bool IsMagneticAnomalyEnabled(const Consensus::Params &params,
                              const CBlockIndex *pindexPrev) {
    if (pindexPrev == nullptr) {
        return false;
    }

    return IsMagneticAnomalyEnabled(params, pindexPrev->nHeight);
}

static bool IsGravitonEnabled(const Consensus::Params &params,
                              int32_t nHeight) {
    return nHeight >= params.gravitonHeight;
}

bool IsGravitonEnabled(const Consensus::Params &params,
                       const CBlockIndex *pindexPrev) {
    if (pindexPrev == nullptr) {
        return false;
    }

    return IsGravitonEnabled(params, pindexPrev->nHeight);
}

static bool IsPhononEnabled(const Consensus::Params &params, int32_t nHeight) {
    return nHeight >= params.phononHeight;
}

bool IsPhononEnabled(const Consensus::Params &params,
                     const CBlockIndex *pindexPrev) {
    if (pindexPrev == nullptr) {
        return false;
    }

    return IsPhononEnabled(params, pindexPrev->nHeight);
}

bool IsAxionEnabled(const Consensus::Params &params,
                    const CBlockIndex *pindexPrev) {
    if (pindexPrev == nullptr) {
        return false;
    }

    if (params.asertAnchorParams) {
        // This chain has a checkpointed anchor block, do simple height check
        return pindexPrev->nHeight >= params.asertAnchorParams->nHeight;
    }

    // Otherwise, do the MTP check
    return pindexPrev->GetMedianTimePast() >=
           gArgs.GetArg("-axionactivationtime", params.axionActivationTime);
}

// Upgrade 9
std::optional<int32_t> g_Upgrade9HeightOverride;

int32_t GetUpgrade9ActivationHeight(const Consensus::Params &params) {
    return g_Upgrade9HeightOverride.value_or(params.upgrade9Height);
}

bool IsUpgrade9EnabledForHeightPrev(const Consensus::Params &params, const int32_t nHeightPrev) {
    return nHeightPrev >= GetUpgrade9ActivationHeight(params);
}

bool IsUpgrade9Enabled(const Consensus::Params &params, const CBlockIndex *pindexPrev) {
    if (pindexPrev == nullptr) {
        return false;
    }

    return IsUpgrade9EnabledForHeightPrev(params, pindexPrev->nHeight);
}

// Upgrade 10
std::optional<int32_t> g_Upgrade10HeightOverride;

int32_t GetUpgrade10ActivationHeight(const Consensus::Params &params) {
    return g_Upgrade10HeightOverride.value_or(params.upgrade10Height);
}

static bool IsUpgrade10EnabledForHeightPrev(const Consensus::Params &params, const int32_t nHeightPrev) {
    return nHeightPrev >= GetUpgrade10ActivationHeight(params);
}

bool IsUpgrade10Enabled(const Consensus::Params &params, const CBlockIndex *pindexPrev) {
    if (pindexPrev == nullptr) {
        return false;
    }

    return IsUpgrade10EnabledForHeightPrev(params, pindexPrev->nHeight);
}

// Upgrade 11
std::optional<int32_t> g_Upgrade11HeightOverride;

int32_t GetUpgrade11ActivationHeight(const Consensus::Params &params) {
    return g_Upgrade11HeightOverride.value_or(params.upgrade11Height);
}

static bool IsUpgrade11EnabledForHeightPrev(const Consensus::Params &params, const int32_t nHeightPrev) {
    return nHeightPrev >= GetUpgrade11ActivationHeight(params);
}

bool IsUpgrade11Enabled(const Consensus::Params &params, const CBlockIndex *pindexPrev) {
    if (pindexPrev == nullptr) {
        return false;
    }

    return IsUpgrade11EnabledForHeightPrev(params, pindexPrev->nHeight);
}

// Upgrade 12
std::optional<int32_t> g_Upgrade12HeightOverride;

int32_t GetUpgrade12ActivationHeight(const Consensus::Params &params) {
    return g_Upgrade12HeightOverride.value_or(params.upgrade12Height);
}

static bool IsUpgrade12EnabledForHeightPrev(const Consensus::Params &params, const int32_t nHeightPrev) {
    return nHeightPrev >= GetUpgrade12ActivationHeight(params);
}

bool IsUpgrade12Enabled(const Consensus::Params &params, const CBlockIndex *pindexPrev) {
    if (pindexPrev == nullptr) {
        return false;
    }

    return IsUpgrade12EnabledForHeightPrev(params, pindexPrev->nHeight);
}

// Upgrade 2027
std::optional<int64_t> g_Upgrade2027TimeOverride;

static bool IsUpgrade2027EnabledForMTPPrev(const Consensus::Params &params, const int64_t nMedianTimePastPrev) {
    // Implementation note: This upgrade shortens the block time from 10 mins -> 1 min. In the future,
    // any upgrade that wants to shorten or lengthen the block time must satisfy the following
    // constraints for the upgrade block and for the new interval chosen:
    //
    // - Let I equal the new interval (in seconds) and let pindexPrevUpgradeBlock be the activation block for the new
    //   interval I; such that the next block after pindexPrevUpgradeBlock is mined with the new interval I. The
    //   following criteria must be met:
    //
    //    - 1 <= I <= 600
    //    - 0 == 600 % I
    //    - 0 == pindexPrevUpgradeBlock->GetBlockTicks().toSecs() % 600
    //
    // The above is basically a set of constraints to ensure that any block time changes happen on what would have been
    // a historical 10-minute-block boundary, and that any new block time chosen is an integer fraction of 600 seconds.
    //
    // If the above constraints are not satisfied when changing the block time either up or down, then economic
    // properties of the chain (as they stood under the original 10-minute block paradigm), such as block subsidy
    // halving schedule, may end up being violated.
    return nMedianTimePastPrev >= g_Upgrade2027TimeOverride.value_or(params.upgrade2027ActivationTime);
}

bool IsUpgrade2027Enabled(const Consensus::Params &params, const CBlockIndex *pindexPrev) {
    if (pindexPrev == nullptr) {
        return false;
    }

    return IsUpgrade2027EnabledForMTPPrev(params, pindexPrev->GetMedianTimePast());
}
