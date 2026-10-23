// Copyright (c) 2011-2017 The Bitcoin Core developers
// Copyright (c) 2020-present The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <bench/bench.h>
#include <config.h>
#include <consensus/validation.h>
#include <script/standard.h>
#include <test/util.h>
#include <txmempool.h>
#include <validation.h>

#include <vector>

static void AssembleBlock(benchmark::State &state) {
    const Config &config = GetConfig();

    const CScript redeemScript = CScript() << OP_DROP << OP_TRUE;
    const CScript SCRIPT_PUB = GetScriptForDestination(ScriptID(redeemScript, false /* p2sh_32 = false */));

    const CScript scriptSig = CScript() << std::vector<uint8_t>(100, 0xff)
                                        << ByteView{redeemScript};

    // Collect some loose transactions that spend the coinbases of our mined blocks
    constexpr size_t NUM_TXS_DESIRED{101};
    std::vector<CTransactionRef> txs;
    txs.reserve(NUM_TXS_DESIRED);
    // The below loop mines NUM_TXS_DESIRED coinbases
    while (txs.size() < NUM_TXS_DESIRED) {
        const auto utxo = MineBlock(config, SCRIPT_PUB);
        CMutableTransaction tx;
        tx.vin.emplace_back(utxo.first, scriptSig);
        tx.vout.emplace_back(utxo.second - 1337 * SATOSHI, SCRIPT_PUB);
        txs.push_back(MakeTransactionRef(std::move(tx)));
    }
    // Now, mature the coinbases
    MatureCurrentTip(config);

    {
        // Required for ::AcceptToMemoryPool.
        LOCK(::cs_main);

        for (const auto &txr : txs) {
            CValidationState vstate;
            bool ret{::AcceptToMemoryPool(config, ::g_mempool, vstate, txr,
                                          nullptr /* pfMissingInputs */,
                                          false /* bypass_limits */,
                                          /* nAbsurdFee */ Amount::zero())};
            assert(ret);
        }
    }

    BENCHMARK_LOOP {
        PrepareBlock(config, SCRIPT_PUB);
    }
}

BENCHMARK(AssembleBlock, 700);
