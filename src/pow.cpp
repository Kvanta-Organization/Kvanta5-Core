// Copyright (c) 2015-2026 The Bitcoin Core developers
// Copyright (c) 2026 The Kvanta5 Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <pow.h>

#include <arith_uint256.h>
#include <chain.h>
#include <primitives/block.h>
#include <uint256.h>

#include <assert.h>

static constexpr int64_t nPastBlocks = 24;

static inline uint32_t PowLimitBits(const Consensus::Params& params)
{
    return UintToArith256(params.powLimit).GetCompact();
}

static inline int64_t BlockTimeForDA(const CBlockIndex* pindex)
{
    return pindex->GetMedianTimePast();
}

static unsigned int Kvanta5DarkGravityWave(const CBlockIndex* pindexLast,
                                           const Consensus::Params& params)
{
    assert(pindexLast != nullptr);

    const arith_uint256 bnPowLimit = UintToArith256(params.powLimit);

    if (params.fPowNoRetargeting) {
        return pindexLast->nBits;
    }

    if (pindexLast->nHeight < nPastBlocks) {
        return bnPowLimit.GetCompact();
    }

    const CBlockIndex* pindex = pindexLast;

    arith_uint256 bnPastTargetAvg;
    arith_uint256 bnTarget;
    bool fNegative = false;
    bool fOverflow = false;

    int64_t nActualTimespan = 0;
    int64_t nBlockCount = 0;
    int64_t nLastBlockTime = 0;

    while (pindex != nullptr && nBlockCount < nPastBlocks) {
        bnTarget.SetCompact(pindex->nBits, &fNegative, &fOverflow);

        if (fNegative || fOverflow || bnTarget == 0) {
            return bnPowLimit.GetCompact();
        }

        ++nBlockCount;

        if (nBlockCount == 1) {
            bnPastTargetAvg = bnTarget;
        } else {
            bnPastTargetAvg = ((bnPastTargetAvg * (nBlockCount - 1)) + bnTarget) / nBlockCount;
        }

        const int64_t nThisBlockTime = BlockTimeForDA(pindex);

        if (nLastBlockTime > 0) {
            int64_t nDiff = nLastBlockTime - nThisBlockTime;

            if (nDiff < 0) {
                nDiff = 0;
            }

            nActualTimespan += nDiff;
        }

        nLastBlockTime = nThisBlockTime;
        pindex = pindex->pprev;
    }

    if (nBlockCount < nPastBlocks) {
        return bnPowLimit.GetCompact();
    }

    const int64_t nTargetTimespan = nPastBlocks * params.nPowTargetSpacing;

    if (nActualTimespan < nTargetTimespan / 4) {
        nActualTimespan = nTargetTimespan / 4;
    }

    if (nActualTimespan > nTargetTimespan * 4) {
        nActualTimespan = nTargetTimespan * 4;
    }

    arith_uint256 bnNew = bnPastTargetAvg;
    bnNew *= nActualTimespan;
    bnNew /= nTargetTimespan;

    if (bnNew == 0 || bnNew > bnPowLimit) {
        bnNew = bnPowLimit;
    }

    return bnNew.GetCompact();
}

unsigned int CalculateNextWorkRequired(const CBlockIndex* pindexLast,
                                       int64_t nFirstBlockTime,
                                       const Consensus::Params& params)
{
    (void)nFirstBlockTime;

    assert(pindexLast != nullptr);

    if (params.fPowNoRetargeting) {
        return pindexLast->nBits;
    }

    return Kvanta5DarkGravityWave(pindexLast, params);
}

unsigned int GetNextWorkRequired(const CBlockIndex* pindexLast,
                                 const CBlockHeader* pblock,
                                 const Consensus::Params& params)
{
    (void)pblock;

    const arith_uint256 bnPowLimit = UintToArith256(params.powLimit);

    if (pindexLast == nullptr) {
        return bnPowLimit.GetCompact();
    }

    if (params.fPowNoRetargeting) {
        return pindexLast->nBits;
    }

    return Kvanta5DarkGravityWave(pindexLast, params);
}

std::optional<arith_uint256> DeriveTarget(unsigned int nBits, const uint256 pow_limit)
{
    bool fNegative = false;
    bool fOverflow = false;

    arith_uint256 bnTarget;
    bnTarget.SetCompact(nBits, &fNegative, &fOverflow);

    if (fNegative || bnTarget == 0 || fOverflow ||
        bnTarget > UintToArith256(pow_limit)) {
        return {};
    }

    return bnTarget;
}

bool CheckProofOfWork(uint256 hash,
                      unsigned int nBits,
                      const Consensus::Params& params)
{
    bool fNegative = false;
    bool fOverflow = false;
    arith_uint256 bnTarget;

    bnTarget.SetCompact(nBits, &fNegative, &fOverflow);

    if (fNegative || bnTarget == 0 || fOverflow ||
        bnTarget > UintToArith256(params.powLimit)) {
        return false;
    }

    if (UintToArith256(hash) > bnTarget) {
        return false;
    }

    return true;
}

bool CheckProofOfWork(const CBlockHeader& block,
                      const Consensus::Params& params)
{
    return CheckProofOfWork(block.GetHash(), block.nBits, params);
}

bool PermittedDifficultyTransition(const Consensus::Params& params,
                                   int64_t height,
                                   uint32_t old_nbits,
                                   uint32_t new_nbits)
{
    if (params.fPowNoRetargeting) {
        return true;
    }

    bool fNegative = false;
    bool fOverflow = false;

    arith_uint256 old_target;
    old_target.SetCompact(old_nbits, &fNegative, &fOverflow);

    if (fNegative || fOverflow || old_target == 0 ||
        old_target > UintToArith256(params.powLimit)) {
        return false;
    }

    fNegative = false;
    fOverflow = false;

    arith_uint256 new_target;
    new_target.SetCompact(new_nbits, &fNegative, &fOverflow);

    if (fNegative || fOverflow || new_target == 0 ||
        new_target > UintToArith256(params.powLimit)) {
        return false;
    }

    if (height <= nPastBlocks) {
        return new_nbits == PowLimitBits(params);
    }

    if (new_target > old_target * 4) {
        return false;
    }

    if (new_target < old_target / 4) {
        return false;
    }

    return true;
}
