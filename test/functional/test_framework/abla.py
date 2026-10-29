#!/usr/bin/env python3
# Copyright (c) 2023 The Bitcoin developers

"""The contents of this file mainly match what's on the C++ side in src/consensus/abla.*."""

from .cdefs import DEFAULT_CONSENSUS_BLOCK_SIZE, MAX_CONSENSUS_BLOCK_SIZE, ONE_MEGABYTE

B7 = 1 << 7
MAX_UINT64 = 2**64 - 1
BASE_BLOCK_INTERVAL = 600
MIN_BLOCK_INTERVAL = 1
MAX_BLOCK_INTERVAL = 600

def _muldiv(x, y, z):
    assert z != 0
    res = (x * y) // z
    assert res <= MAX_UINT64
    return res


# noinspection PyPep8Naming
class AblaConfig:
    __slots__ = ("epsilon0", "beta0", "gammaReciprocal", "zeta_xB7", "thetaReciprocal", "delta", "epsilonMax",
                 "betaMax")

    def __init__(self, maxBlockSize=DEFAULT_CONSENSUS_BLOCK_SIZE, fixedSize=False):
        self.epsilon0 = maxBlockSize // 2
        self.beta0 = maxBlockSize // 2
        self.gammaReciprocal = 37938
        self.zeta_xB7 = 192
        self.thetaReciprocal = 37938
        self.delta = 10
        if not fixedSize:
            self.setMax()
        else:
            self.epsilonMax = self.epsilon0
            self.betaMax = self.beta0

    def setMax(self):
        maxSafeBlocksizeLimit = MAX_UINT64 // self.zeta_xB7 * B7
        maxElasticBufferRatioNumerator = self.delta * ((self.zeta_xB7 - B7)
                                                           * self.thetaReciprocal // self.gammaReciprocal)
        maxElasticBufferRatioDenominator = (self.zeta_xB7 - B7) * self.thetaReciprocal // self.gammaReciprocal + B7
        self.epsilonMax = (maxSafeBlocksizeLimit
                           // (maxElasticBufferRatioNumerator + maxElasticBufferRatioDenominator)
                           * maxElasticBufferRatioDenominator)
        self.betaMax = maxSafeBlocksizeLimit - self.epsilonMax

    def __repr__(self):
        return f"<class 'AblaConfig' epsilon0={self.epsilon0} beta0={self.beta0}" \
               f" gammaReciprocal={self.gammaReciprocal} zeta_xB7={self.zeta_xB7}" \
               f" thetaReciprocal={self.thetaReciprocal} delta={self.delta} epsilonMax={self.epsilonMax}" \
               f" betaMax={self.betaMax}>"


# Regtest default
DEFAULT_ABLA_CONFIG = AblaConfig()


# noinspection PyPep8Naming
class AblaState:
    __slots__ = ("blockSize", "controlBlockSize", "elasticBufferSize", "blockInterval")

    def __init__(self, config=DEFAULT_ABLA_CONFIG, blockSize=None, *, controlBlockSize=None, elasticBufferSize=None,
                 blockInterval=None):
        self.blockSize = blockSize or 0
        self.controlBlockSize = config.epsilon0 if controlBlockSize is None else controlBlockSize
        self.elasticBufferSize = config.beta0 if elasticBufferSize is None else elasticBufferSize
        self.blockInterval = BASE_BLOCK_INTERVAL if blockInterval is None else blockInterval

    @classmethod
    def FromRpcDict(cls, d, config=DEFAULT_ABLA_CONFIG):
        if 'ablastate' in d:
            # Was a header, drill-down into the ablastate dict
            return cls.FromRpcDict(d['ablastate'], config=config)
        return cls(config=config, blockSize=d['blocksize'], controlBlockSize=d['epsilon'], elasticBufferSize=d['beta'],
                   blockInterval=d['blockinterval'])

    def GetBlockSizeLimit(self):
        val = _muldiv(self.controlBlockSize + self.elasticBufferSize, self.blockInterval, BASE_BLOCK_INTERVAL)
        return min(val, MAX_CONSENSUS_BLOCK_SIZE)

    def GetNextBlockSizeLimit(self, nextBlockInterval, config=DEFAULT_ABLA_CONFIG):
        return self.NextBlockState(config=config, nextBlockInterval=nextBlockInterval, nextBlockSize=0).GetBlockSizeLimit()

    def NextBlockState(self, nextBlockSize, nextBlockInterval, config=DEFAULT_ABLA_CONFIG):
        ret = AblaState(config=config, blockSize=nextBlockSize, blockInterval=nextBlockInterval)

        tau = self.blockInterval
        clampedBlockSize = min(self.blockSize, _muldiv(self.controlBlockSize + self.elasticBufferSize, tau,
                                                       BASE_BLOCK_INTERVAL))
        amplifiedCurrentBlockSize = _muldiv(config.zeta_xB7, _muldiv(clampedBlockSize, BASE_BLOCK_INTERVAL, tau), B7)

        # control buffer function
        if amplifiedCurrentBlockSize > self.controlBlockSize:
            bytesToAdd = amplifiedCurrentBlockSize - self.controlBlockSize
            amplifiedBlockSizeLimit = _muldiv(config.zeta_xB7, self.controlBlockSize + self.elasticBufferSize, B7)
            bytesMax = amplifiedBlockSizeLimit - self.controlBlockSize
            scalingOffset = _muldiv(_muldiv(config.zeta_xB7, self.elasticBufferSize, B7), bytesToAdd, bytesMax)
            ret.controlBlockSize = self.controlBlockSize + _muldiv(bytesToAdd - scalingOffset, tau,
                                                                   config.gammaReciprocal * BASE_BLOCK_INTERVAL)
        else:
            bytesToRemove = self.controlBlockSize - amplifiedCurrentBlockSize
            ret.controlBlockSize = self.controlBlockSize - _muldiv(bytesToRemove, tau,
                                                                   config.gammaReciprocal * BASE_BLOCK_INTERVAL)
            ret.controlBlockSize = max(ret.controlBlockSize, config.epsilon0)

        # elastic buffer function
        bufferDecay = _muldiv(self.elasticBufferSize, tau, config.thetaReciprocal * BASE_BLOCK_INTERVAL)
        if amplifiedCurrentBlockSize > self.controlBlockSize:
            bytesToAdd = (ret.controlBlockSize - self.controlBlockSize) * config.delta
            ret.elasticBufferSize = self.elasticBufferSize - bufferDecay + bytesToAdd
        else:
            ret.elasticBufferSize = self.elasticBufferSize - bufferDecay

        ret.elasticBufferSize = max(ret.elasticBufferSize, config.beta0)
        ret.controlBlockSize = min(ret.controlBlockSize, config.epsilonMax)
        ret.elasticBufferSize = min(ret.elasticBufferSize, config.betaMax)

        return ret

    # This is the old iterative method to calculate this, changed recently to the closed-form
    def CalcLookaheadBlockSizeLimit_old_iterative(self, count, config=DEFAULT_ABLA_CONFIG):
        lookaheadState = self
        for i in range(count):
            maxSize = lookaheadState.GetNextBlockSizeLimit(config=config, nextBlockInterval=BASE_BLOCK_INTERVAL)
            lookaheadState = lookaheadState.NextBlockState(config=config, nextBlockSize=maxSize,
                                                           nextBlockInterval=BASE_BLOCK_INTERVAL)
        return lookaheadState.GetBlockSizeLimit()

    @staticmethod
    def _BoundSumAfterFullBlocks(config, tau: int, s0: int, eps0: int, k: int):
        baseBlockInterval = 600
        G = tau * (config.zeta_xB7 - B7)
        D = baseBlockInterval * B7 * config.gammaReciprocal
        sat = config.epsilonMax + config.betaMax

        if k >= (D // G):
            return sat

        kG = k * G
        inc = (1 + config.delta) * eps0 * kG // (D - kG)
        return min(s0 + inc, sat)

    def CalcLookaheadBlockSizeLimit(self, count: int, config=DEFAULT_ABLA_CONFIG):
        lookaheadState = self
        if count == 0:
            return lookaheadState.GetBlockSizeLimit()
        # Every block is at the base interval of 600 seconds.
        tau = BASE_BLOCK_INTERVAL
        # The first transition is done exactly: it responds to this state's own stored block size, and its
        # result (a full block) is the state the remaining `count - 1` blocks start from.
        nextBlockSize = lookaheadState.GetNextBlockSizeLimit(config=config, nextBlockInterval=tau)
        lookaheadState = lookaheadState.NextBlockState(config=config, nextBlockSize=nextBlockSize,
                                                       nextBlockInterval=tau)
        eps = lookaheadState.controlBlockSize
        return self._BoundSumAfterFullBlocks(config, tau, eps + lookaheadState.elasticBufferSize, eps, count - 1)

    def __repr__(self):
        return f"<class 'AblaState' blockSize={self.blockSize} controlBlockSize={self.controlBlockSize}" \
               f" elasticBufferSize={self.elasticBufferSize} blockInterval={self.blockInterval}>"

    def __str__(self):
        return repr(self)

    def as_tup(self):
        return self.blockSize, self.controlBlockSize, self.elasticBufferSize, self.blockInterval

    def __eq__(self, other):
        return self.as_tup() == other.as_tup()

    def __ne__(self, other):
        return self.as_tup() != other.as_tup()

    def __hash__(self):
        return hash(self.as_tup())


if __name__ == "__main__":
    # 2MB blocksize limit
    conf = AblaConfig(maxBlockSize=2*ONE_MEGABYTE)
    state = AblaState(blockSize=1129, config=conf)
    print(repr(conf))
    print(repr(state))
    assert state.CalcLookaheadBlockSizeLimit(2048, config=conf) == 2234156

    # 32MB blocksize limit, already-advanced state
    conf = DEFAULT_ABLA_CONFIG
    state = AblaState(blockSize=403438931, config=conf, controlBlockSize=93377312, elasticBufferSize=310061619)
    print(repr(conf))
    print(repr(state))
    assert state.CalcLookaheadBlockSizeLimit(500, config=conf) == 406127292
