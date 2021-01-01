// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2017 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <pow.h>
#include <prime/prime.h>

#include <arith_uint256.h>
#include <chain.h>
#include <chainparams.h>
#include <chainparamsbase.h>
#include <primitives/block.h>
#include <uint256.h>
#include <util.h>

bool TestNet()
{
    if (ChainNameFromCommandLine() == CBaseChainParams::TESTNET)
        return true;
    return false;
}

unsigned int GetNextWorkRequired(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params& params)
{
	// TODO(gjh): DATACOIN optimize?
    // In tests with unrealistic input, (pindexPrev->nBits == 0) and (nActualSpacing == 0) the
    // function returns a value below TargetGetLimit(). Change to always return at least TargetGetLimit() ???
    unsigned int nBits = TargetGetLimit();

    // Genesis block
    if (pindexLast == NULL)
        return nBits;

    const CBlockIndex* pindexPrev = pindexLast;
    if (pindexPrev->pprev == NULL)
        return TargetGetInitial(); // first block
    const CBlockIndex* pindexPrevPrev = pindexPrev->pprev;
    if (pindexPrevPrev->pprev == NULL)
        return TargetGetInitial(); // second block

    // NOTE: PRIMECOIN continuous target adjustment on every block
    int64_t nInterval = params.nPowTargetTimespan / params.nPowTargetSpacing;
    int64_t nActualSpacing = pindexPrev->GetBlockTime() - pindexPrevPrev->GetBlockTime();
    if (!TargetGetNext(pindexPrev->nBits, nInterval, params.nPowTargetSpacing, nActualSpacing, nBits))
        return error("GetNextWorkRequired() : failed to get next target");

    if ((gArgs.IsArgSet("-debug")) && gArgs.GetBoolArg("-printtarget", false))
        LogPrintf("GetNextWorkRequired() : lastindex=%u prev=0x%08x new=0x%08x\n",
            pindexLast->nHeight, pindexPrev->nBits, nBits);
    return nBits;
}

unsigned int CalculateNextWorkRequired(const CBlockIndex* pindexLast, int64_t nFirstBlockTime, const Consensus::Params& params)
{
    if (params.fPowNoRetargeting)
        return pindexLast->nBits;

    // Limit adjustment step
    int64_t nActualTimespan = pindexLast->GetBlockTime() - nFirstBlockTime;
    if (nActualTimespan < params.nPowTargetTimespan/4)
        nActualTimespan = params.nPowTargetTimespan/4;
    if (nActualTimespan > params.nPowTargetTimespan*4)
        nActualTimespan = params.nPowTargetTimespan*4;

    // Retarget
    const arith_uint256 bnPowLimit = UintToArith256(params.powLimit);
    arith_uint256 bnNew;
    bnNew.SetCompact(pindexLast->nBits);
    bnNew *= nActualTimespan;
    bnNew /= params.nPowTargetTimespan;

    if (bnNew > bnPowLimit)
        bnNew = bnPowLimit;

    return bnNew.GetCompact();
}

bool CheckProofOfWork(uint256 hashBlockHeader, unsigned int nBits, const Consensus::Params& params, const CBigNum& bnProbablePrime, unsigned int& nChainType, unsigned int& nChainLength, bool fSilent)
{
    if (!CheckPrimeProofOfWork(hashBlockHeader, nBits, bnProbablePrime, nChainType, nChainLength, fSilent))
        return fSilent ? false : error("CheckProofOfWork() : check failed for prime proof-of-work");

    return true;
}
