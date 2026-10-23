// Copyright (c) 2021-present The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <amount.h>
#include <bench/bench.h>
#include <config.h>
#include <consensus/validation.h>
#include <miner.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <script/standard.h>
#include <test/setup_common.h>
#include <test/util.h>
#include <txmempool.h>
#include <util/defer.h>
#include <util/system.h>
#include <validation.h>

#include <cassert>
#include <cstdio>
#include <list>
#include <memory>
#include <utility>
#include <vector>

/// This file contains benchmarks focusing on removeForBlock in CTxMemPool

static const CScript REDEEM_SCRIPT = CScript()
    << OP_DROP << OP_TRUE;

static const CScript SCRIPT_PUB_KEY = GetScriptForDestination(ScriptID(REDEEM_SCRIPT, false /* p2sh_20 */));

static const CScript SCRIPT_SIG = CScript()
    << std::vector<uint8_t>(100, 0xff)
    << ByteView(REDEEM_SCRIPT);

/// Mine a new utxo that has nValue == desiredAmount (may create coinbase txns and mature them to achieve this)
using UTXO = std::pair<COutPoint, Amount>;
static UTXO createUTXO(const Config& config, const Amount desiredAmount) {
    assert(MoneyRange(desiredAmount));
    std::vector<UTXO> utxos;
    Amount total;
    // Mine enough coinbases to meet `desiredAmount`
    do {
        utxos.push_back(MineBlock(config, SCRIPT_PUB_KEY));
        total += utxos.back().second;
    } while (total < desiredAmount);

    // Mature the above utxos
    MatureCurrentTip(config);

    // Merge all coins into a single coin that is == desiredAmount
    CMutableTransaction tx;
    tx.vin.reserve(utxos.size());
    tx.vout.reserve(1);
    for (const auto & [outpt, _] : utxos) {
        tx.vin.emplace_back(outpt, SCRIPT_SIG);
    }
    tx.vout.push_back(CTxOut{desiredAmount, SCRIPT_PUB_KEY});
    auto ptx = MakeTransactionRef(std::move(tx));

    // Mine the above txn
    MineBlock(config, SCRIPT_PUB_KEY, {{ptx}});

    // Return the UTXO
    return {COutPoint(ptx->GetId(), 0), ptx->vout[0].nValue};
}

static void benchRemoveForBlock(const Config& config, benchmark::State& state,
                                const size_t nTx, const double blockMB, const bool allowUnconfChains) {
    assert(nTx > 0 && blockMB > 0.);

    // save initial state
    const auto origBlockMinTxFee = gArgs.GetArg("-blockmintxfee", "");
    gArgs.ForceSetArg("-blockmintxfee", "0");

    // undo above on scope end
    const Defer d([&origBlockMinTxFee] {
        gArgs.ForceSetArg("-blockmintxfee", origBlockMinTxFee);
        g_mempool.clear();
    });

    const auto utxo = createUTXO(config, 50 * COIN);
    assert(utxo.second == 50 * COIN);
    using UTXOList = std::list<UTXO>;
    UTXOList ins;
    ins.push_back(utxo);

    auto SpendTxInToMempool = [&config](const COutPoint &outpoint, const Amount val, const size_t fanoutSize,
                                        const bool unchecked = false) -> UTXOList {
        UTXOList ret;
        assert(fanoutSize > 0);
        CMutableTransaction tx;
        tx.vin.emplace_back(outpoint, SCRIPT_SIG);
        while (tx.vout.size() < fanoutSize) {
            tx.vout.emplace_back(int64_t((val / SATOSHI) / fanoutSize) * SATOSHI, SCRIPT_PUB_KEY);
        }
        const CTransactionRef rtx = MakeTransactionRef(tx);
        const auto &txId = rtx->GetId();
        unsigned outN = 0;
        for (const auto &out : rtx->vout) {
            ret.emplace_back(COutPoint(txId, outN++), out.nValue);
        }
        LOCK(cs_main);
        if (unchecked) {
            LOCK(g_mempool.cs);
            g_mempool.addUnchecked(TestMemPoolEntryHelper{}.FromTx(rtx));
        } else {
            CValidationState vstate;
            bool missingInputs{};
            const bool ok = AcceptToMemoryPool(config, g_mempool, vstate, rtx, &missingInputs, true, Amount::zero());
            assert(ok && vstate.IsValid());
        }
        return ret;
    };

    while (ins.size() < nTx) {
        const auto [outpoint, val] = ins.front();
        ins.pop_front();
        auto inList = SpendTxInToMempool(outpoint, val, 100 /* fanoutSize */);
        // add the utxos we just generated to the input list at the end
        ins.splice(ins.end(), inList);
    }

    if (!allowUnconfChains) {
        // next, mine a block to commit the above chained tx's, then use the ins again as 1-to-1 tx's into the mempool
        LogPrint(BCLog::MEMPOOL, "(%s 1) mempool size: %i\n", __func__, g_mempool.size());
        MineBlock(config, SCRIPT_PUB_KEY);
        LogPrint(BCLog::MEMPOOL, "(%s 2) mempool size: %i\n", __func__,  g_mempool.size());
    }

    for (const auto & [outpoint, val] : ins) {
        SpendTxInToMempool(outpoint, val, 1 /* fanoutSize */, true /* unchecked */);
    }
    ins.clear(); // we don't need these inputs anymore

    assert(g_mempool.size() >= nTx);

    LogPrint(BCLog::MEMPOOL, "(%s 3) mempool size: %i\n",  __func__, g_mempool.size());

    BlockAssembler::Options opts;
    opts.blockMinFeeRate = CFeeRate{Amount::zero()};
    opts.nConsensusCurrentBlockSizeLimit = config.GetDefaultConsensusBlockSize(WITH_LOCK(cs_main, return ::ChainActive().Tip()->GetTickDeltaForNextBlock()));
    opts.nMaxGeneratedBlockSize = blockMB * ONE_MEGABYTE;

    // Below assert ensures that we satisfied the caller's demand for blockMB. This is a check to ensure that we do not
    // run this bench with Upgrade2027 activated. If we do, we cannot satisfy the blocksize requested (3.2 MB max blocks
    // in the 1-min regime vs 32 MB max in the 10-min regime).
    assert(opts.nConsensusCurrentBlockSizeLimit >= opts.nMaxGeneratedBlockSize);

    const auto pblktemplate = BlockAssembler{config, ::g_mempool, opts}.CreateNewBlock(SCRIPT_PUB_KEY);
    const auto &block = pblktemplate->block;

    std::list<CTxMemPool> pools;

    // Note: in order to isolate how long removeForBlock takes, we are
    // forced to pre-create all the pools we will be needing up front,
    // copying the entries from g_mempool into them.
    for (uint64_t i = 0; i < state.m_num_iters + 1; ++i) {
        LOCK2(cs_main, g_mempool.cs);
        const auto &index = g_mempool.mapTx.get<entry_id>(); // iterate by entry id
        pools.emplace_back();
        auto &pool = pools.back();
        for (auto it = index.begin(); it != index.end(); ++it) {
            LOCK(pool.cs);
            pool.addUnchecked(*it);
        }
        assert(pool.size() == g_mempool.size());
    }

    auto it = pools.begin();

    BENCHMARK_LOOP {
        assert(it != pools.end());
        auto &pool = *it++;
        pool.removeForBlock(block.vtx);
    }
}

/// Fill a mempool with 450k txs, then repeatedly test removeForBlock with a 32MB block of ~170k small-sized txs
static void RemoveForBlock32MB(benchmark::State& state) {
    const Config& config = GetConfig();
    benchRemoveForBlock(config, state, 450'000, 32, false);
}

/// Fill a mempool with 450k txs, then repeatedly test removeForBlock with an 8MB block of ~43k small-sized txs
static void RemoveForBlock8MB(benchmark::State& state) {
    const Config& config = GetConfig();
    benchRemoveForBlock(config, state, 450'000, 8, false);
}

/// Fill a mempool with ~450k txs, then repeatedly test removeForBlock with a 32MB block of ~93k mixed-sized txs,
/// leaving some unconfirmed chains in mempool too so that the parentSet for some txs is larger
static void RemoveForBlock32MB_UnconfChains(benchmark::State& state) {
    const Config& config = GetConfig();
    benchRemoveForBlock(config, state, 450'000, 32, true);
}

/// Fill a mempool with 450k txs, then repeatedly test removeForBlock with an 8MB block of ~8k mixed-sized txs,
/// leaving some unconfirmed chains in mempool too so that the parentSet for some txs is larger
static void RemoveForBlock8MB_UnconfChains(benchmark::State& state) {
    const Config& config = GetConfig();
    benchRemoveForBlock(config, state, 450'000, 8, true);
}

// Called each time the benchmark is evaluated; explicitly disable upgrade 2027 (since we need 32MB blocks).
static void DisableUpgrade2027(benchmark::State &state) {
    if (!state.anyData.has_value()) {
        state.anyData = std::make_shared<Upgrade2027Disabler>();
    }
}

BENCHMARK(RemoveForBlock32MB, 1, {}, false, DisableUpgrade2027);
BENCHMARK(RemoveForBlock8MB, 1, {}, false, DisableUpgrade2027);
BENCHMARK(RemoveForBlock32MB_UnconfChains, 1, {}, false, DisableUpgrade2027);
BENCHMARK(RemoveForBlock8MB_UnconfChains, 1, {}, false, DisableUpgrade2027);
