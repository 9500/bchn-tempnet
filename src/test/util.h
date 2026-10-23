// Copyright (c) 2019 The Bitcoin Core developers
// Copyright (c) 2020-present The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include <primitives/transaction.h>
#include <span.h>

#include <any>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

class CBlock;
class Config;

// Lower-level utils //

/** Returns the generated coin */
/** Returns the generated coinbase coin */
std::pair<COutPoint, Amount> MineBlock(const Config &config, const CScript &coinbase_scriptPubKey,
                                       const std::vector<CTransactionRef> &extraTxns = {});
/** Prepare a block to be mined */
std::shared_ptr<CBlock> PrepareBlock(const Config &config,
                                     const CScript &coinbase_scriptPubKey,
                                     const std::vector<CTransactionRef> &extraTxns = {});
/** Mines until the current tip's coinbase is mature, paying out to *scriptPubKey or OP_TRUE if !scriptPubKey.
    Returns number of blocks mined on top of tip */
size_t MatureCurrentTip(const Config &config, const CScript *scriptPubKey = nullptr);

/** If fCheckBlockIndex is enabled, temporarily disables it and returns an opaque RAII type that will auto-re-enable
 *  fCheckBlockIndex when its lifetime ends. Additionally, on lifetime end of the returned value, CheckBlockIndex()
 *  is run once for good measure. Warning: param `config` *must* outlive the returned RAII type! */
[[nodiscard]] std::any TemporarilyDisableCheckBlockIndex(const Config &config);

// Use zlib to uncompress to a byte buffer. Throws on error, such as if uncompressedByteSize is too small, otherwise
// returns the uncompressed bytes. The returned vector's size is always <= uncompressedByteSize.
std::vector<uint8_t> Uncompress(Span<const uint8_t> compressedBytes, size_t uncompressedByteSize);
// Like the above but return the data as a string. Intended to be used for large JSON data strings embedded in the app,
// hence the std::string return type. The returned string is always <= uncompressedByteSize long.
std::string UncompressStr(Span<const uint8_t> compressedBytes, size_t uncompressedByteSize);
// Use zlib to uncompress to a generic byte buffer in-place. Throws on error such as if outputBuf does not have enough
// space to hold the uncompressed data. Returns a subspan of outputBuf that enecompasses the uncompressed data actually
// written.
Span<uint8_t> UncompressInPlace(Span<uint8_t> outputBuf, Span<const uint8_t> compressedBytes);

// An RAII class that disables upgrade 2027. Can be used as the first base class of a derived TestChainSetup class,
// or can be used stand-alone.
class Upgrade2027Disabler {
    const std::optional<int64_t> saved_g_Upgrade2027TimeOverride;
public:
    Upgrade2027Disabler();
    ~Upgrade2027Disabler();

    // not copyable or movable
    Upgrade2027Disabler(const Upgrade2027Disabler &) = delete;
    Upgrade2027Disabler(Upgrade2027Disabler &&) = delete;
    Upgrade2027Disabler & operator=(const Upgrade2027Disabler &) = delete;
    Upgrade2027Disabler & operator=(Upgrade2027Disabler &&) = delete;
};
