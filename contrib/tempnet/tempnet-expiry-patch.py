#!/usr/bin/env python3
"""Give the tempnet chain its own software-expiry time (2026-11-15 12:00 UTC, the chipnet activation),
so BCHN's software-outdated warning and RPC shutdown no longer key off tempnet's activation time, which
is already in the past (that made `expire=0` mandatory). Apply on a tree that already has the tempnet
chain (tempnet-patch.py). Usage: tempnet-expiry-patch.py <source tree root>."""
import sys, pathlib
root = pathlib.Path(sys.argv[1])
def edit(rel, old, new):
    p = root / rel; s = p.read_text(); n = s.count(old)
    if n != 1: sys.exit(f"ANCHOR MISMATCH in {rel}: found {n}: {old[:70]!r}")
    p.write_text(s.replace(old, new)); print(f"patched {rel}: {old.strip().splitlines()[0][:60]!r}")
edit("src/chainparams.h",
     "    const Consensus::Params &GetConsensus() const { return consensus; }\n",
     "    const Consensus::Params &GetConsensus() const { return consensus; }\n"
     "    /** Time after which this software considers itself outdated/expired (see software_outdated).\n"
     "     *  Defaults to the next upgrade's activation time; a chain whose activation time is already in\n"
     "     *  the past (tempnet) sets its own. */\n"
     "    int64_t SoftwareExpiryTime() const {\n"
     "        return nSoftwareExpiryTime > 0 ? nSoftwareExpiryTime : consensus.upgrade2027ActivationTime;\n"
     "    }\n")
edit("src/chainparams.h",
     "    Consensus::Params consensus;\n",
     "    Consensus::Params consensus;\n    int64_t nSoftwareExpiryTime = 0;\n")
edit("src/chainparams.cpp",
     "        consensus.upgrade2027ActivationTime = 1792065600; // Oct 15 2026 12:00:00 UTC\n",
     "        consensus.upgrade2027ActivationTime = 1792065600; // Oct 15 2026 12:00:00 UTC\n"
     "        // Tempnets expire when the real chipnet activates the upgrade (Nov 15 2026 12:00:00 UTC): the\n"
     "        // software-outdated warning and the RPC shutdown key off this, not the (past) activation time.\n"
     "        nSoftwareExpiryTime = 1794744000;\n")
edit("src/init.cpp",
     "        // The software outdated warning will start to happen 30 days before May 15th, 2027;\n"
     "        // on -chipnet this date is 30 days before Nov. 15th, 2026.\n"
     "        software_outdated::nTime = gArgs.GetArg(\"-upgrade2027activationtime\",\n"
     "                                                chainparams.GetConsensus().upgrade2027ActivationTime);\n",
     "        // The software outdated warning will start to happen 30 days before May 15th, 2027;\n"
     "        // on -chipnet this date is 30 days before Nov. 15th, 2026, and -tempnet has its own\n"
     "        // expiry (chainparams), since its activation time is already in the past.\n"
     "        software_outdated::nTime = gArgs.GetArg(\"-upgrade2027activationtime\",\n"
     "                                                chainparams.SoftwareExpiryTime());\n")
print("OK: tempnet expiry patch applied")
