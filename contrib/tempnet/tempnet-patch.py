#!/usr/bin/env python3
"""Add a 'tempnet' chain (bitjson's 2026-10-15 tempnet for CHIP-2025-03) to a BCHN tree that already
contains the faster-blocks code (cculianu blocktime/wip2). Usage: tempnet-patch.py <source tree root>.
Every edit is anchored on exact text and fails loudly if an anchor is missing or ambiguous."""
import sys, pathlib
root = pathlib.Path(sys.argv[1])
def edit(rel, old, new, count=1):
    p = root / rel; s = p.read_text()
    n = s.count(old)
    if n != count: sys.exit(f"ANCHOR MISMATCH in {rel}: expected {count}, found {n}: {old[:70]!r}")
    p.write_text(s.replace(old, new)); print(f"patched {rel}: {old.strip().splitlines()[0][:60]!r}")

# 1. chainparamsbase.h / .cpp: the network id, the -tempnet option, RPC/onion ports (same as chipnet)
edit("src/chainparamsbase.h",
     "    static const std::string CHIPNET;\n",
     "    static const std::string CHIPNET;\n    static const std::string TEMPNET;\n")
edit("src/chainparamsbase.cpp",
     'const std::string CBaseChainParams::CHIPNET = "chip";\n',
     'const std::string CBaseChainParams::CHIPNET = "chip";\nconst std::string CBaseChainParams::TEMPNET = "temp";\n')
edit("src/chainparamsbase.cpp",
     '    gArgs.AddArg("-chipnet", "Use the upcoming upgrade activation chain", ArgsManager::ALLOW_ANY,\n'
     '                 OptionsCategory::CHAINPARAMS);\n',
     '    gArgs.AddArg("-chipnet", "Use the upcoming upgrade activation chain", ArgsManager::ALLOW_ANY,\n'
     '                 OptionsCategory::CHAINPARAMS);\n'
     '    gArgs.AddArg("-tempnet", "Use the temporary test chain for upcoming chipnet rules (tempnet-20261015)",\n'
     '                 ArgsManager::ALLOW_ANY, OptionsCategory::CHAINPARAMS);\n')
edit("src/chainparamsbase.cpp",
     '        return std::make_unique<CBaseChainParams>("chipnet", 48332, 48334);\n    }\n',
     '        return std::make_unique<CBaseChainParams>("chipnet", 48332, 48334);\n    }\n\n'
     '    if (chain == CBaseChainParams::TEMPNET) {\n'
     '        return std::make_unique<CBaseChainParams>("tempnet", 48332, 48334);\n    }\n')

# 2. chainparams.cpp: CTempNetParams = chipnet with the 2027 rules activated on 2026-10-15 12:00 UTC
s = (root / "src/chainparams.cpp").read_text()
i = s.index("class CChipNetParams : public CChainParams {")
j = s.index("\n};\n", i) + len("\n};\n")
tempnet_class = '''
/**
 * Tempnet (bitjson's temporary chipnet fork, 2026-10-15 .. 2026-11-15): chipnet with the
 * May 2027 rules (CHIP-2025-03 Faster Blocks) activated at MTP 1792065600 (2026-10-15 12:00 UTC).
 * Same network magic and P2P port as chipnet; the chains diverge at height 327989.
 */
class CTempNetParams : public CChipNetParams {
public:
    CTempNetParams() {
        strNetworkID = CBaseChainParams::TEMPNET;
        // Tempnets activate the next consensus changes ahead of chipnet.
        consensus.upgrade2027ActivationTime = 1792065600; // Oct 15 2026 12:00:00 UTC
        // First block mined under the new rules (from tempnet.chaingraph.cash); 327988 is the last common block.
        checkpointData.mapCheckpoints[327989] =
            BlockHash::fromHex("00000000e18af2c66d44a1bbfe165acc4b87a625d211a09e33d5ee61d35dcb66");
        vSeeds.clear();
        vFixedSeeds.clear();
        vSeeds.emplace_back("tempnet.bitjson.com");
        vSeeds.emplace_back("chipnet.bitjson.com");
    }
};
'''
s = s[:j] + tempnet_class + s[j:]
old = "        return std::make_unique<CChipNetParams>();\n    }\n"
assert s.count(old) == 1, "CreateChainParams anchor"
s = s.replace(old, old + "\n    if (chain == CBaseChainParams::TEMPNET) {\n        return std::make_unique<CTempNetParams>();\n    }\n")
(root / "src/chainparams.cpp").write_text(s); print("patched src/chainparams.cpp: CTempNetParams + CreateChainParams")

# 3. util/system.cpp: config-file section name and -tempnet chain selection
edit("src/util/system.cpp",
     "        CBaseChainParams::TESTNET, CBaseChainParams::TESTNET4, CBaseChainParams::SCALENET, CBaseChainParams::CHIPNET,\n",
     "        CBaseChainParams::TESTNET, CBaseChainParams::TESTNET4, CBaseChainParams::SCALENET, CBaseChainParams::CHIPNET,\n"
     "        CBaseChainParams::TEMPNET,\n")
edit("src/util/system.cpp",
     '    bool fChipNet = ArgsManagerHelper::GetNetBoolArg(*this, "-chipnet");\n',
     '    bool fChipNet = ArgsManagerHelper::GetNetBoolArg(*this, "-chipnet");\n'
     '    bool fTempNet = ArgsManagerHelper::GetNetBoolArg(*this, "-tempnet");\n')
edit("src/util/system.cpp",
     "    if (fTestNet + fTestNet4 + fScaleNet + fRegTest + fChipNet > 1) {\n",
     "    if (fTestNet + fTestNet4 + fScaleNet + fRegTest + fChipNet + fTempNet > 1) {\n")
edit("src/util/system.cpp",
     '            "Invalid combination of -regtest, -testnet, -testnet4, -scalenet, and -chipnet.");\n',
     '            "Invalid combination of -regtest, -testnet, -testnet4, -scalenet, -chipnet, and -tempnet.");\n')
edit("src/util/system.cpp",
     "        return CBaseChainParams::CHIPNET;\n    }\n",
     "        return CBaseChainParams::CHIPNET;\n    }\n    if (fTempNet) {\n        return CBaseChainParams::TEMPNET;\n    }\n")
print("OK: tempnet patch applied")
