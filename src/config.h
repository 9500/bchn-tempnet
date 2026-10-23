// Copyright (c) 2017 Amaury SÉCHET
// Copyright (c) 2020-present The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include <amount.h>
#include <consensus/blockticks.h>
#include <consensus/consensus.h>
#include <feerate.h>
#include <policy/policy.h>
#include <util/noncopyable.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <variant>

/** Default for -usecashaddr */
static constexpr bool DEFAULT_USE_CASHADDR = true;
/** Default for -signschnorr */
static constexpr bool DEFAULT_SIGN_SCHNORR = true;

class CChainParams;

class Config : public NonCopyable {
public:
    /** Convenience. Returns the consensus max block size as configured by Consensus::Params for the current chain.
        The returned value is scaled based on `blockInterval`.
        Note: If ABLA is enabled, this is the max block size floor for ABLA.
        Note2: May return the value (if any) set by SetBlockSizeOverride(), scaled for `blockInterval` (functional tests only). */
    virtual uint64_t GetDefaultConsensusBlockSize(BlockTicks blockInterval) const = 0;

    /** Functional tests only: corresponds to the hidden CLI arg: -forceblocksize */
    virtual bool SetBlockSizeOverride(std::optional<uint64_t> overrideBlockSize10Mins) = 0;

    /** Look-ahead "guess" for the max blocksize (actual blocksize limit is guaranteed to be <= this value for
        blocks within the block download window). This value gets updated by validation.cpp when the tip changes.
        Used by net code and some pre-checks on blocks to discard blocks that are definitely oversized.
        The returned value is in terms of a 10-min blocktime's blocksize and should be used as such as a worst-case
        upper threshold. */
    virtual uint64_t GetMaxBlockSizeLookAheadGuess() const = 0;
    /** Sets the max block size lookahead guess, in terms of a 10-min blocktime blocksize. */
    virtual void NotifyMaxBlockSizeLookAheadGuessChanged(uint64_t blockSize10Mins) const = 0;

    /** Set the largest block size this node will generate (mine) in bytes.
        Note on block time intervals: This value is interpreted "as-if" the blocktime were always 10 mins.
        For 1 minute block times, the actual value used will be 1/10th of this value.
        See: GetGeneratedBlockSize() and GetDefaultConsensusBlockSize() both of which take a blocktime interval arg. */
    virtual void SetGeneratedBlockSizeBytes(uint64_t blockSize10Mins) = 0;
    /** Set the largest block size this node will generate (mine), in terms of
        percentage of the current block size as determined by ABLA.
        Returns false if `percent` is not in the range [0.0, 100.0]. */
    virtual bool SetGeneratedBlockSizePercent(double percent) = 0;
    /** Returns the maximum mined block size in bytes.
        Note that the returned value is scaled to the target blockInterval. So for 10 mins it's a 1:1 scale, for 1-min
        block times, the returned value is 1/10th of that, etc. */
    virtual uint64_t GetGeneratedBlockSize(BlockTicks blockInterval, std::optional<uint64_t> currentMaxBlockSizeAlreadyScaled) const = 0;
    /** The maximum amount of RAM to be used in the mempool before TrimToSize is called. */
    virtual void SetMaxMemPoolSize(uint64_t maxMemPoolSize) = 0;
    virtual uint64_t GetMaxMemPoolSize() const = 0;
    /** Set the TPS per MB per 10Min block. Note that to read this value back you need to specify a block interval
     *  since it scales itself up if the blocksize is smaller due to shorter block intervals.*/
    virtual bool SetInvBroadcastRatePerMbFor10MinBlocks(uint64_t ratePerMbPer10MinBlock) = 0;
    /** Read back the TPS rate for this block interval. Shorter times return a higher rate -- this value is only
     *  used in 1 place in the codebase in net_processing.cpp:SendMessages(), and so the scaling is needed there. */
    virtual uint64_t GetInvBroadcastRatePerMbForBlockInterval(BlockTicks blockInterval) const = 0;
    virtual bool SetInvBroadcastInterval(uint64_t interval) = 0;
    virtual uint64_t GetInvBroadcastInterval() const = 0;
    virtual const CChainParams &GetChainParams() const = 0;
    virtual void SetCashAddrEncoding(bool) = 0;
    virtual bool UseCashAddrEncoding() const = 0;

    virtual void SetExcessUTXOCharge(Amount amt) = 0;
    virtual Amount GetExcessUTXOCharge() const = 0;

    virtual void SetRejectSubVersions(const std::set<std::string> &reject) = 0;
    virtual const std::set<std::string> & GetRejectSubVersions() const = 0;

    virtual void SetGBTCheckValidity(bool) = 0;
    virtual bool GetGBTCheckValidity() const = 0;

    virtual void SetAllowUnconnectedMining(bool) = 0;
    virtual bool GetAllowUnconnectedMining() const = 0;

    /** Whether to always sign transactions using Schnorr signatures */
    virtual void SetSignSchnorr(bool) = 0;
    virtual bool IsSignSchnorr() const = 0;
};

class GlobalConfig final : public Config {
public:
    GlobalConfig();
    uint64_t GetDefaultConsensusBlockSize(BlockTicks blockInterval) const override;
    bool SetBlockSizeOverride(std::optional<uint64_t> blockSize10Mins) override;
    uint64_t GetMaxBlockSizeLookAheadGuess() const override;
    void NotifyMaxBlockSizeLookAheadGuessChanged(uint64_t) const override;
    void SetGeneratedBlockSizeBytes(uint64_t blockSize) override;
    bool SetGeneratedBlockSizePercent(double percent) override;
    uint64_t GetGeneratedBlockSize(BlockTicks blockInterval, std::optional<uint64_t> currentMaxBlockSizeAlreadyScaled) const override;
    void SetMaxMemPoolSize(uint64_t maxMemPoolSize) override { nMaxMemPoolSize = maxMemPoolSize; }
    uint64_t GetMaxMemPoolSize() const override { return nMaxMemPoolSize; }
    //! Note: `rate` may not exceed MAX_INV_BROADCAST_RATE (1 million)
    bool SetInvBroadcastRatePerMbFor10MinBlocks(uint64_t ratePerMbPer10MinBlock) override;
    uint64_t GetInvBroadcastRatePerMbForBlockInterval(BlockTicks blockInterval) const override;
    //! Note: `interval` may not exceed MAX_INV_BROADCAST_INTERVAL (1 million)
    bool SetInvBroadcastInterval(uint64_t interval) override;
    uint64_t GetInvBroadcastInterval() const override { return nInvBroadcastInterval; }
    const CChainParams &GetChainParams() const override;
    void SetCashAddrEncoding(bool) override;
    bool UseCashAddrEncoding() const override;

    void SetExcessUTXOCharge(Amount) override;
    Amount GetExcessUTXOCharge() const override;

    void SetRejectSubVersions(const std::set<std::string> &reject) override { rejectSubVersions = reject; }
    const std::set<std::string> & GetRejectSubVersions() const override { return rejectSubVersions; }

    void SetGBTCheckValidity(bool b) override { gbtCheckValidity = b; }
    bool GetGBTCheckValidity() const override { return gbtCheckValidity; }

    //! "Unconnected mining" (default false). If true, getblocktemplate[light] works even if not connected to any peers.
    void SetAllowUnconnectedMining(bool b) override { allowUnconnectedMining = b; }
    bool GetAllowUnconnectedMining() const override { return allowUnconnectedMining; }

    //! Whether to always sign transactions using Schnorr signatures (default: true)
    void SetSignSchnorr(bool b) override { signSchnorr = b; }
    bool IsSignSchnorr() const override { return signSchnorr; }

private:
    bool useCashAddr{};
    bool gbtCheckValidity{};
    bool allowUnconnectedMining{};
    bool signSchnorr = DEFAULT_SIGN_SCHNORR;
    Amount excessUTXOCharge;
    uint64_t nInvBroadcastRatePerMbPer10MinBlock{};
    uint64_t nInvBroadcastInterval{};

    /** Override for the blockSize for functional tests (size is stored "as if" blocktime is 10 mins and is scaled by getter) */
    std::optional<uint64_t> blockSize10MinsOverride;

    /** The largest block size this node will generate. Stores either a size in bytes (scaled to 10-min block times),
        or a percentage (double). */
    std::variant<uint64_t, double> varGeneratedBlockSizeParam;

    /** The maximum amount of RAM to be used in the mempool before TrimToSize is called. */
    uint64_t nMaxMemPoolSize{};

    std::set<std::string> rejectSubVersions;

    /** Temporarily set to mutable because consensus code only gets passed a `const Config &`, but it does need to
        modify this value as the chain tip is updated. Note that this value is in terms of 10-min block times, and
        we never re-scale it since our worst-case guess will always be "as if" the blocktime is 10-mins. */
    mutable std::atomic<uint64_t> nMaxBlockSizeWorstCaseGuess10Mins{0u};
};

// Dummy for subclassing in unittests
class DummyConfig : public Config {
public:
    DummyConfig();
    DummyConfig(const std::string &net);
    DummyConfig(std::unique_ptr<CChainParams> chainParamsIn);
    uint64_t GetDefaultConsensusBlockSize(BlockTicks) const override { return 0; }
    bool SetBlockSizeOverride(std::optional<uint64_t>) override { return false; }
    uint64_t GetMaxBlockSizeLookAheadGuess() const override { return 0; }
    void NotifyMaxBlockSizeLookAheadGuessChanged(uint64_t) const override {}
    void SetGeneratedBlockSizeBytes(uint64_t) override {}
    bool SetGeneratedBlockSizePercent(double) override { return false; }
    uint64_t GetGeneratedBlockSize(BlockTicks, std::optional<uint64_t>) const override { return 0; }
    void SetMaxMemPoolSize(uint64_t) override {}
    uint64_t GetMaxMemPoolSize() const override { return 0; }
    bool SetInvBroadcastRatePerMbFor10MinBlocks(uint64_t) override { return false; }
    uint64_t GetInvBroadcastRatePerMbForBlockInterval(BlockTicks) const override { return 0; }
    bool SetInvBroadcastInterval(uint64_t) override { return false; }
    uint64_t GetInvBroadcastInterval() const override {return 0; }

    void SetChainParams(const std::string &net);
    const CChainParams &GetChainParams() const override { return *chainParams; }

    void SetCashAddrEncoding(bool) override {}
    bool UseCashAddrEncoding() const override { return false; }

    void SetExcessUTXOCharge(Amount) override {}
    Amount GetExcessUTXOCharge() const override { return Amount::zero(); }

    void SetRejectSubVersions(const std::set<std::string> &) override {}
    const std::set<std::string> & GetRejectSubVersions() const override {
        static const std::set<std::string> dummy;
        return dummy;
    }

    void SetGBTCheckValidity(bool) override {}
    bool GetGBTCheckValidity() const override { return false; }

    void SetAllowUnconnectedMining(bool) override {}
    bool GetAllowUnconnectedMining() const override { return false; }

    void SetSignSchnorr(bool) override {}
    // This getter always just returns the default (true), unlike the other getters; we enforce Schnorr even trivially.
    bool IsSignSchnorr() const override { return DEFAULT_SIGN_SCHNORR; }

private:
    std::unique_ptr<CChainParams> chainParams;
};

// Temporary woraround.
const Config &GetConfig();
Config &GetMutableConfig();
