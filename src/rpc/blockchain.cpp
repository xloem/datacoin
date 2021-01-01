// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2017 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <rpc/blockchain.h>

#include <amount.h>
#include <chain.h>
#include <chainparams.h>
#include <checkpoints.h>
#include <coins.h>
#include <consensus/validation.h>
#include <validation.h>
#include <core_io.h>
#include <policy/feerate.h>
#include <policy/policy.h>
#include <primitives/transaction.h>
#include <rpc/server.h>
#include <streams.h>
#include <sync.h>
#include <txdb.h>
#include <txmempool.h>
#include <util.h>
#include <utilstrencodings.h>
#include <hash.h>
#include <validationinterface.h>
#include <warnings.h>
#include <prime/prime.h>
#include <wallet/wallet.h>
#include <init.h>

#include <stdint.h>

#include <univalue.h>

#include <boost/thread/thread.hpp> // boost::thread::interrupt

#include <memory>
#include <mutex>
#include <condition_variable>

struct CUpdatedBlock
{
    uint256 hash;
    int height;
};

static std::mutex cs_blockchange;
static std::condition_variable cond_blockchange;
static CUpdatedBlock latestblock;

extern void TxToJSON(const CTransaction& tx, const uint256 hashBlock, UniValue& entry);

/* Calculate the difficulty for a given block index,
 * or the block index of the given chain.
 */
double GetDifficulty(const CChain& chain, const CBlockIndex* blockindex)
{
    // Floating point number that is approximate log scale of prime target,
    // minimum difficulty = 256, maximum difficulty = 2039
    if (blockindex == nullptr)
    {
        if (chain.Tip() == nullptr)
            return 256.0;
        else
            blockindex = chain.Tip();
    }

    double dDiff = GetPrimeDifficulty(blockindex->nBits);

    return dDiff;
}

double GetDifficulty(const CBlockIndex* blockindex)
{
    return GetDifficulty(chainActive, blockindex);
}

UniValue blockheaderToJSON(const CBlockIndex* blockindex)
{
    AssertLockHeld(cs_main);
    UniValue result(UniValue::VOBJ);
    result.push_back(Pair("hash", blockindex->GetBlockHash().GetHex()));
    int confirmations = -1;
    // Only report confirmations if the block is on the main chain
    if (chainActive.Contains(blockindex))
        confirmations = chainActive.Height() - blockindex->nHeight + 1;
    result.push_back(Pair("confirmations", confirmations));
    result.push_back(Pair("height", blockindex->nHeight));
    result.push_back(Pair("version", blockindex->nVersion));
    result.push_back(Pair("versionHex", strprintf("%08x", blockindex->nVersion)));
    result.push_back(Pair("merkleroot", blockindex->hashMerkleRoot.GetHex()));
    result.push_back(Pair("time", (int64_t)blockindex->nTime));
    result.push_back(Pair("mediantime", (int64_t)blockindex->GetMedianTimePast()));
    result.push_back(Pair("nonce", (uint64_t)blockindex->nNonce));
    result.push_back(Pair("primechainmultiplier", blockindex->bnPrimeChainMultiplier.ToString())); // NOTE: DATACOIN added
    result.push_back(Pair("bits", strprintf("%08x", blockindex->nBits)));
    result.push_back(Pair("difficulty", GetPrimeDifficulty(blockindex->nBits)));
    result.push_back(Pair("chainwork", blockindex->nChainWork.GetHex())); 
    result.push_back(Pair("transition", GetPrimeDifficulty(blockindex->nWorkTransition)));
    result.push_back(Pair("primechain", GetPrimeChainName(blockindex->nPrimeChainType, blockindex->nPrimeChainLength)));
    // auto& hHash=blockindex->GetHeaderHash();
    // if (hHash) {
    //     CBigNum bnPrimeChainOrigin = CBigNum(hHash) * blockindex->bnPrimeChainMultiplier;
    //     result.push_back(Pair("primeorigin", bnPrimeChainOrigin.ToString()));       
    // } else
    //     result.push_back(Pair("primeorigin", "unknown"));   

    if (blockindex->pprev) {
        result.push_back(Pair("previousblockhash", blockindex->pprev->GetBlockHash().GetHex()));
        CBigNum bnPrimeChainOrigin = CBigNum(blockindex->GetHeaderHash()) * blockindex->bnPrimeChainMultiplier;
        result.push_back(Pair("primeorigin", bnPrimeChainOrigin.ToString()));
    }
    CBlockIndex *pnext = chainActive.Next(blockindex);
    if (pnext)
        result.push_back(Pair("nextblockhash", pnext->GetBlockHash().GetHex()));
    return result;
}

UniValue blockToJSON(const CBlock& block, const CBlockIndex* blockindex, bool txDetails)
{
    AssertLockHeld(cs_main);
    UniValue result(UniValue::VOBJ);
    result.push_back(Pair("hash", blockindex->GetBlockHash().GetHex()));
    int confirmations = -1;
    // Only report confirmations if the block is on the main chain
    if (chainActive.Contains(blockindex))
        confirmations = chainActive.Height() - blockindex->nHeight + 1;
    result.push_back(Pair("confirmations", confirmations));
    result.push_back(Pair("strippedsize", (int)::GetSerializeSize(block, SER_NETWORK, PROTOCOL_VERSION | SERIALIZE_TRANSACTION_NO_WITNESS)));
    result.push_back(Pair("size", (int)::GetSerializeSize(block, SER_NETWORK, PROTOCOL_VERSION)));
    result.push_back(Pair("weight", (int)::GetBlockWeight(block)));
    result.push_back(Pair("height", blockindex->nHeight));
    result.push_back(Pair("version", block.nVersion));
    result.push_back(Pair("headerhash", block.GetHeaderHash().GetHex()));
    result.push_back(Pair("versionHex", strprintf("%08x", block.nVersion)));
    result.push_back(Pair("merkleroot", block.hashMerkleRoot.GetHex()));
    UniValue txs(UniValue::VARR);
    for(const auto& tx : block.vtx)
    {
        if(txDetails)
        {
            UniValue objTx(UniValue::VOBJ);
            TxToUniv(*tx, uint256(), objTx, true, RPCSerializationFlags());
            txs.push_back(objTx);
        }
        else
            txs.push_back(tx->GetHash().GetHex());
    }
    result.push_back(Pair("tx", txs));
    result.push_back(Pair("time", block.GetBlockTime()));
    result.push_back(Pair("mediantime", (int64_t)blockindex->GetMedianTimePast()));
    result.push_back(Pair("nonce", (uint64_t)block.nNonce));
    result.push_back(Pair("primechainmultiplier", block.bnPrimeChainMultiplier.ToString())); // NOTE: DATACOIN added
    result.push_back(Pair("bits", strprintf("%08x", block.nBits)));
    result.push_back(Pair("difficulty", GetDifficulty(blockindex)));
    result.push_back(Pair("chainwork", blockindex->nChainWork.GetHex()));
    result.push_back(Pair("nTx", (uint64_t)blockindex->nTx));
    result.push_back(Pair("transition", GetPrimeDifficulty(blockindex->nWorkTransition)));
    CBigNum bnPrimeChainOrigin = CBigNum(block.GetHeaderHash()) * block.bnPrimeChainMultiplier;
    result.push_back(Pair("primechain", GetPrimeChainName(blockindex->nPrimeChainType, blockindex->nPrimeChainLength)));
    result.push_back(Pair("primeorigin", bnPrimeChainOrigin.ToString()));

    if (blockindex->pprev)
        result.push_back(Pair("previousblockhash", blockindex->pprev->GetBlockHash().GetHex()));
    CBlockIndex *pnext = chainActive.Next(blockindex);
    if (pnext)
        result.push_back(Pair("nextblockhash", pnext->GetBlockHash().GetHex()));
    return result;
}

UniValue getblockcount(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 0)
        throw std::runtime_error(
            "getblockcount\n"
            "\nReturns the number of blocks in the longest blockchain.\n"
            "\nResult:\n"
            "n    (numeric) The current block count\n"
            "\nExamples:\n"
            + HelpExampleCli("getblockcount", "")
            + HelpExampleRpc("getblockcount", "")
        );

    LOCK(cs_main);
    return chainActive.Height();
}

UniValue getbestblockhash(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 0)
        throw std::runtime_error(
            "getbestblockhash\n"
            "\nReturns the hash of the best (tip) block in the longest blockchain.\n"
            "\nResult:\n"
            "\"hex\"      (string) the block hash hex encoded\n"
            "\nExamples:\n"
            + HelpExampleCli("getbestblockhash", "")
            + HelpExampleRpc("getbestblockhash", "")
        );

    LOCK(cs_main);
    return chainActive.Tip()->GetBlockHash().GetHex();
}

void RPCNotifyBlockChange(bool ibd, const CBlockIndex * pindex)
{
    if(pindex) {
        std::lock_guard<std::mutex> lock(cs_blockchange);
        latestblock.hash = pindex->GetBlockHash();
        latestblock.height = pindex->nHeight;
    }
    cond_blockchange.notify_all();
}

UniValue waitfornewblock(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 1)
        throw std::runtime_error(
            "waitfornewblock (timeout)\n"
            "\nWaits for a specific new block and returns useful info about it.\n"
            "\nReturns the current block on timeout or exit.\n"
            "\nArguments:\n"
            "1. timeout (int, optional, default=0) Time in milliseconds to wait for a response. 0 indicates no timeout.\n"
            "\nResult:\n"
            "{                           (json object)\n"
            "  \"hash\" : {       (string) The blockhash\n"
            "  \"height\" : {     (int) Block height\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("waitfornewblock", "1000")
            + HelpExampleRpc("waitfornewblock", "1000")
        );
    int timeout = 0;
    if (!request.params[0].isNull())
        timeout = request.params[0].get_int();

    CUpdatedBlock block;
    {
        std::unique_lock<std::mutex> lock(cs_blockchange);
        block = latestblock;
        if(timeout)
            cond_blockchange.wait_for(lock, std::chrono::milliseconds(timeout), [&block]{return latestblock.height != block.height || latestblock.hash != block.hash || !IsRPCRunning(); });
        else
            cond_blockchange.wait(lock, [&block]{return latestblock.height != block.height || latestblock.hash != block.hash || !IsRPCRunning(); });
        block = latestblock;
    }
    UniValue ret(UniValue::VOBJ);
    ret.push_back(Pair("hash", block.hash.GetHex()));
    ret.push_back(Pair("height", block.height));
    return ret;
}

UniValue waitforblock(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 2)
        throw std::runtime_error(
            "waitforblock <blockhash> (timeout)\n"
            "\nWaits for a specific new block and returns useful info about it.\n"
            "\nReturns the current block on timeout or exit.\n"
            "\nArguments:\n"
            "1. \"blockhash\" (required, string) Block hash to wait for.\n"
            "2. timeout       (int, optional, default=0) Time in milliseconds to wait for a response. 0 indicates no timeout.\n"
            "\nResult:\n"
            "{                           (json object)\n"
            "  \"hash\" : {       (string) The blockhash\n"
            "  \"height\" : {     (int) Block height\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("waitforblock", "\"0000000000079f8ef3d2c688c244eb7a4570b24c9ed7b4a8c619eb02596f8862\", 1000")
            + HelpExampleRpc("waitforblock", "\"0000000000079f8ef3d2c688c244eb7a4570b24c9ed7b4a8c619eb02596f8862\", 1000")
        );
    int timeout = 0;

    uint256 hash = uint256S(request.params[0].get_str());

    if (!request.params[1].isNull())
        timeout = request.params[1].get_int();

    CUpdatedBlock block;
    {
        std::unique_lock<std::mutex> lock(cs_blockchange);
        if(timeout)
            cond_blockchange.wait_for(lock, std::chrono::milliseconds(timeout), [&hash]{return latestblock.hash == hash || !IsRPCRunning();});
        else
            cond_blockchange.wait(lock, [&hash]{return latestblock.hash == hash || !IsRPCRunning(); });
        block = latestblock;
    }

    UniValue ret(UniValue::VOBJ);
    ret.push_back(Pair("hash", block.hash.GetHex()));
    ret.push_back(Pair("height", block.height));
    return ret;
}

UniValue waitforblockheight(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 2)
        throw std::runtime_error(
            "waitforblockheight <height> (timeout)\n"
            "\nWaits for (at least) block height and returns the height and hash\n"
            "of the current tip.\n"
            "\nReturns the current block on timeout or exit.\n"
            "\nArguments:\n"
            "1. height  (required, int) Block height to wait for (int)\n"
            "2. timeout (int, optional, default=0) Time in milliseconds to wait for a response. 0 indicates no timeout.\n"
            "\nResult:\n"
            "{                           (json object)\n"
            "  \"hash\" : {       (string) The blockhash\n"
            "  \"height\" : {     (int) Block height\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("waitforblockheight", "\"100\", 1000")
            + HelpExampleRpc("waitforblockheight", "\"100\", 1000")
        );
    int timeout = 0;

    int height = request.params[0].get_int();

    if (!request.params[1].isNull())
        timeout = request.params[1].get_int();

    CUpdatedBlock block;
    {
        std::unique_lock<std::mutex> lock(cs_blockchange);
        if(timeout)
            cond_blockchange.wait_for(lock, std::chrono::milliseconds(timeout), [&height]{return latestblock.height >= height || !IsRPCRunning();});
        else
            cond_blockchange.wait(lock, [&height]{return latestblock.height >= height || !IsRPCRunning(); });
        block = latestblock;
    }
    UniValue ret(UniValue::VOBJ);
    ret.push_back(Pair("hash", block.hash.GetHex()));
    ret.push_back(Pair("height", block.height));
    return ret;
}

UniValue syncwithvalidationinterfacequeue(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 0) {
        throw std::runtime_error(
            "syncwithvalidationinterfacequeue\n"
            "\nWaits for the validation interface queue to catch up on everything that was there when we entered this function.\n"
            "\nExamples:\n"
            + HelpExampleCli("syncwithvalidationinterfacequeue","")
            + HelpExampleRpc("syncwithvalidationinterfacequeue","")
        );
    }
    SyncWithValidationInterfaceQueue();
    return NullUniValue;
}

UniValue getdifficulty(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 0)
        throw std::runtime_error(
            "getdifficulty\n"
            "\nReturns the proof-of-work difficulty in prime chain length.\n"
            "\nResult:\n"
            "n.nnn       (numeric) the proof-of-work difficulty in prime chain length.\n"
            "\nExamples:\n"
            + HelpExampleCli("getdifficulty", "")
            + HelpExampleRpc("getdifficulty", "")
        );

    LOCK(cs_main);
    return GetDifficulty();
}

std::string EntryDescriptionString()
{
    return "    \"size\" : n,             (numeric) virtual transaction size as defined in BIP 141. This is different from actual serialized size for witness transactions as witness data is discounted.\n"
           "    \"fee\" : n,              (numeric) transaction fee in " + CURRENCY_UNIT + "\n"
           "    \"modifiedfee\" : n,      (numeric) transaction fee with fee deltas used for mining priority\n"
           "    \"time\" : n,             (numeric) local time transaction entered pool in seconds since 1 Jan 1970 GMT\n"
           "    \"height\" : n,           (numeric) block height when transaction entered pool\n"
           "    \"descendantcount\" : n,  (numeric) number of in-mempool descendant transactions (including this one)\n"
           "    \"descendantsize\" : n,   (numeric) virtual transaction size of in-mempool descendants (including this one)\n"
           "    \"descendantfees\" : n,   (numeric) modified fees (see above) of in-mempool descendants (including this one)\n"
           "    \"ancestorcount\" : n,    (numeric) number of in-mempool ancestor transactions (including this one)\n"
           "    \"ancestorsize\" : n,     (numeric) virtual transaction size of in-mempool ancestors (including this one)\n"
           "    \"ancestorfees\" : n,     (numeric) modified fees (see above) of in-mempool ancestors (including this one)\n"
           "    \"wtxid\" : hash,         (string) hash of serialized transaction, including witness data\n"
           "    \"depends\" : [           (array) unconfirmed transactions used as inputs for this transaction\n"
           "        \"transactionid\",    (string) parent transaction id\n"
           "       ... ]\n";
}

void entryToJSON(UniValue &info, const CTxMemPoolEntry &e)
{
    AssertLockHeld(mempool.cs);

    info.push_back(Pair("size", (int)e.GetTxSize()));
    info.push_back(Pair("fee", ValueFromAmount(e.GetFee())));
    info.push_back(Pair("modifiedfee", ValueFromAmount(e.GetModifiedFee())));
    info.push_back(Pair("time", e.GetTime()));
    info.push_back(Pair("height", (int)e.GetHeight()));
    info.push_back(Pair("descendantcount", e.GetCountWithDescendants()));
    info.push_back(Pair("descendantsize", e.GetSizeWithDescendants()));
    info.push_back(Pair("descendantfees", e.GetModFeesWithDescendants()));
    info.push_back(Pair("ancestorcount", e.GetCountWithAncestors()));
    info.push_back(Pair("ancestorsize", e.GetSizeWithAncestors()));
    info.push_back(Pair("ancestorfees", e.GetModFeesWithAncestors()));
    info.push_back(Pair("wtxid", mempool.vTxHashes[e.vTxHashesIdx].first.ToString()));
    const CTransaction& tx = e.GetTx();
    std::set<std::string> setDepends;
    for (const CTxIn& txin : tx.vin)
    {
        if (mempool.exists(txin.prevout.hash))
            setDepends.insert(txin.prevout.hash.ToString());
    }

    UniValue depends(UniValue::VARR);
    for (const std::string& dep : setDepends)
    {
        depends.push_back(dep);
    }

    info.push_back(Pair("depends", depends));
}

UniValue mempoolToJSON(bool fVerbose)
{
    if (fVerbose)
    {
        LOCK(mempool.cs);
        UniValue o(UniValue::VOBJ);
        for (const CTxMemPoolEntry& e : mempool.mapTx)
        {
            const uint256& hash = e.GetTx().GetHash();
            UniValue info(UniValue::VOBJ);
            entryToJSON(info, e);
            o.push_back(Pair(hash.ToString(), info));
        }
        return o;
    }
    else
    {
        std::vector<uint256> vtxid;
        mempool.queryHashes(vtxid);

        UniValue a(UniValue::VARR);
        for (const uint256& hash : vtxid)
            a.push_back(hash.ToString());

        return a;
    }
}

UniValue getrawmempool(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 1)
        throw std::runtime_error(
            "getrawmempool ( verbose )\n"
            "\nReturns all transaction ids in memory pool as a json array of string transaction ids.\n"
            "\nHint: use getmempoolentry to fetch a specific transaction from the mempool.\n"
            "\nArguments:\n"
            "1. verbose (boolean, optional, default=false) True for a json object, false for array of transaction ids\n"
            "\nResult: (for verbose = false):\n"
            "[                     (json array of string)\n"
            "  \"transactionid\"     (string) The transaction id\n"
            "  ,...\n"
            "]\n"
            "\nResult: (for verbose = true):\n"
            "{                           (json object)\n"
            "  \"transactionid\" : {       (json object)\n"
            + EntryDescriptionString()
            + "  }, ...\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getrawmempool", "true")
            + HelpExampleRpc("getrawmempool", "true")
        );

    bool fVerbose = false;
    if (!request.params[0].isNull())
        fVerbose = request.params[0].get_bool();

    return mempoolToJSON(fVerbose);
}

UniValue getmempoolancestors(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 2) {
        throw std::runtime_error(
            "getmempoolancestors txid (verbose)\n"
            "\nIf txid is in the mempool, returns all in-mempool ancestors.\n"
            "\nArguments:\n"
            "1. \"txid\"                 (string, required) The transaction id (must be in mempool)\n"
            "2. verbose                  (boolean, optional, default=false) True for a json object, false for array of transaction ids\n"
            "\nResult (for verbose=false):\n"
            "[                       (json array of strings)\n"
            "  \"transactionid\"           (string) The transaction id of an in-mempool ancestor transaction\n"
            "  ,...\n"
            "]\n"
            "\nResult (for verbose=true):\n"
            "{                           (json object)\n"
            "  \"transactionid\" : {       (json object)\n"
            + EntryDescriptionString()
            + "  }, ...\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getmempoolancestors", "\"mytxid\"")
            + HelpExampleRpc("getmempoolancestors", "\"mytxid\"")
            );
    }

    bool fVerbose = false;
    if (!request.params[1].isNull())
        fVerbose = request.params[1].get_bool();

    uint256 hash = ParseHashV(request.params[0], "parameter 1");

    LOCK(mempool.cs);

    CTxMemPool::txiter it = mempool.mapTx.find(hash);
    if (it == mempool.mapTx.end()) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Transaction not in mempool");
    }

    CTxMemPool::setEntries setAncestors;
    uint64_t noLimit = std::numeric_limits<uint64_t>::max();
    std::string dummy;
    mempool.CalculateMemPoolAncestors(*it, setAncestors, noLimit, noLimit, noLimit, noLimit, dummy, false);

    if (!fVerbose) {
        UniValue o(UniValue::VARR);
        for (CTxMemPool::txiter ancestorIt : setAncestors) {
            o.push_back(ancestorIt->GetTx().GetHash().ToString());
        }

        return o;
    } else {
        UniValue o(UniValue::VOBJ);
        for (CTxMemPool::txiter ancestorIt : setAncestors) {
            const CTxMemPoolEntry &e = *ancestorIt;
            const uint256& _hash = e.GetTx().GetHash();
            UniValue info(UniValue::VOBJ);
            entryToJSON(info, e);
            o.push_back(Pair(_hash.ToString(), info));
        }
        return o;
    }
}

UniValue getmempooldescendants(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 2) {
        throw std::runtime_error(
            "getmempooldescendants txid (verbose)\n"
            "\nIf txid is in the mempool, returns all in-mempool descendants.\n"
            "\nArguments:\n"
            "1. \"txid\"                 (string, required) The transaction id (must be in mempool)\n"
            "2. verbose                  (boolean, optional, default=false) True for a json object, false for array of transaction ids\n"
            "\nResult (for verbose=false):\n"
            "[                       (json array of strings)\n"
            "  \"transactionid\"           (string) The transaction id of an in-mempool descendant transaction\n"
            "  ,...\n"
            "]\n"
            "\nResult (for verbose=true):\n"
            "{                           (json object)\n"
            "  \"transactionid\" : {       (json object)\n"
            + EntryDescriptionString()
            + "  }, ...\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getmempooldescendants", "\"mytxid\"")
            + HelpExampleRpc("getmempooldescendants", "\"mytxid\"")
            );
    }

    bool fVerbose = false;
    if (!request.params[1].isNull())
        fVerbose = request.params[1].get_bool();

    uint256 hash = ParseHashV(request.params[0], "parameter 1");

    LOCK(mempool.cs);

    CTxMemPool::txiter it = mempool.mapTx.find(hash);
    if (it == mempool.mapTx.end()) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Transaction not in mempool");
    }

    CTxMemPool::setEntries setDescendants;
    mempool.CalculateDescendants(it, setDescendants);
    // CTxMemPool::CalculateDescendants will include the given tx
    setDescendants.erase(it);

    if (!fVerbose) {
        UniValue o(UniValue::VARR);
        for (CTxMemPool::txiter descendantIt : setDescendants) {
            o.push_back(descendantIt->GetTx().GetHash().ToString());
        }

        return o;
    } else {
        UniValue o(UniValue::VOBJ);
        for (CTxMemPool::txiter descendantIt : setDescendants) {
            const CTxMemPoolEntry &e = *descendantIt;
            const uint256& _hash = e.GetTx().GetHash();
            UniValue info(UniValue::VOBJ);
            entryToJSON(info, e);
            o.push_back(Pair(_hash.ToString(), info));
        }
        return o;
    }
}

UniValue getmempoolentry(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1) {
        throw std::runtime_error(
            "getmempoolentry txid\n"
            "\nReturns mempool data for given transaction\n"
            "\nArguments:\n"
            "1. \"txid\"                   (string, required) The transaction id (must be in mempool)\n"
            "\nResult:\n"
            "{                           (json object)\n"
            + EntryDescriptionString()
            + "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getmempoolentry", "\"mytxid\"")
            + HelpExampleRpc("getmempoolentry", "\"mytxid\"")
        );
    }

    uint256 hash = ParseHashV(request.params[0], "parameter 1");

    LOCK(mempool.cs);

    CTxMemPool::txiter it = mempool.mapTx.find(hash);
    if (it == mempool.mapTx.end()) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Transaction not in mempool");
    }

    const CTxMemPoolEntry &e = *it;
    UniValue info(UniValue::VOBJ);
    entryToJSON(info, e);
    return info;
}

UniValue getblockhash(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "getblockhash height\n"
            "\nReturns hash of block in best-block-chain at height provided.\n"
            "\nArguments:\n"
            "1. height         (numeric, required) The height index\n"
            "\nResult:\n"
            "\"hash\"         (string) The block hash\n"
            "\nExamples:\n"
            + HelpExampleCli("getblockhash", "1000")
            + HelpExampleRpc("getblockhash", "1000")
        );

    LOCK(cs_main);

    int nHeight = request.params[0].get_int();
    if (nHeight < 0 || nHeight > chainActive.Height())
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Block height out of range");

    CBlockIndex* pblockindex = chainActive[nHeight];
    return pblockindex->GetBlockHash().GetHex();
}

UniValue getblockheader(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 2)
        throw std::runtime_error(
            "getblockheader \"hash\" ( verbose )\n"
            "\nIf verbose is false, returns a string that is serialized, hex-encoded data for blockheader 'hash'.\n"
            "If verbose is true, returns an Object with information about blockheader <hash>.\n"
            "\nArguments:\n"
            "1. \"hash\"          (string, required) The block hash\n"
            "2. verbose           (boolean, optional, default=true) true for a json object, false for the hex encoded data\n"
            "\nResult (for verbose = true):\n"
            "{\n"
            "  \"hash\" : \"hash\",     (string) the block hash (same as provided)\n"
            "  \"confirmations\" : n,   (numeric) The number of confirmations, or -1 if the block is not on the main chain\n"
            "  \"height\" : n,          (numeric) The block height or index\n"
            "  \"version\" : n,         (numeric) The block version\n"
            "  \"versionHex\" : \"00000000\", (string) The block version formatted in hexadecimal\n"
            "  \"merkleroot\" : \"xxxx\", (string) The merkle root\n"
            "  \"time\" : ttt,          (numeric) The block time in seconds since epoch (Jan 1 1970 GMT)\n"
            "  \"mediantime\" : ttt,    (numeric) The median block time in seconds since epoch (Jan 1 1970 GMT)\n"
            "  \"nonce\" : n,           (numeric) The nonce\n"
            "  \"bits\" : \"1d00ffff\", (string) The bits\n"
            "  \"difficulty\" : x.xxx,  (numeric) The difficulty\n"
            "  \"chainwork\" : \"0000...1f3\"     (string) Expected number of hashes required to produce the current chain (in hex)\n"
            "  \"nTx\" : n,             (numeric) The number of transactions in the block.\n"
            "  \"previousblockhash\" : \"hash\",  (string) The hash of the previous block\n"
            "  \"nextblockhash\" : \"hash\",      (string) The hash of the next block\n"
            "}\n"
            "\nResult (for verbose=false):\n"
            "\"data\"             (string) A string that is serialized, hex-encoded data for block 'hash'.\n"
            "\nExamples:\n"
            + HelpExampleCli("getblockheader", "\"00000000c937983704a73af28acdec37b049d214adbda81d7e2a3dd146f6ed09\"")
            + HelpExampleRpc("getblockheader", "\"00000000c937983704a73af28acdec37b049d214adbda81d7e2a3dd146f6ed09\"")
        );

    LOCK(cs_main);

    std::string strHash = request.params[0].get_str();
    uint256 hash(uint256S(strHash));

    bool fVerbose = true;
    if (!request.params[1].isNull())
        fVerbose = request.params[1].get_bool();

    if (mapBlockIndex.count(hash) == 0)
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");

    CBlockIndex* pblockindex = mapBlockIndex[hash];

    if (!fVerbose)
    {
        CDataStream ssBlock(SER_NETWORK, PROTOCOL_VERSION);
        ssBlock << pblockindex->GetFullBlockHeader();
        std::string strHex = HexStr(ssBlock.begin(), ssBlock.end());
        return strHex;
    }

    return blockheaderToJSON(pblockindex);
}

UniValue getblock(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 2)
        throw std::runtime_error(
            "getblock \"blockhash\" ( verbosity ) \n"
            "\nIf verbosity is 0, returns a string that is serialized, hex-encoded data for block 'hash'.\n"
            "If verbosity is 1, returns an Object with information about block <hash>.\n"
            "If verbosity is 2, returns an Object with information about block <hash> and information about each transaction. \n"
            "\nArguments:\n"
            "1. \"blockhash\"          (string, required) The block hash\n"
            "2. verbosity              (numeric, optional, default=1) 0 for hex encoded data, 1 for a json object, and 2 for json object with transaction data\n"
            "\nResult (for verbosity = 0):\n"
            "\"data\"             (string) A string that is serialized, hex-encoded data for block 'hash'.\n"
            "\nResult (for verbosity = 1):\n"
            "{\n"
            "  \"hash\" : \"hash\",     (string) the block hash (same as provided)\n"
            "  \"confirmations\" : n,   (numeric) The number of confirmations, or -1 if the block is not on the main chain\n"
            "  \"size\" : n,            (numeric) The block size\n"
            "  \"strippedsize\" : n,    (numeric) The block size excluding witness data\n"
            "  \"weight\" : n           (numeric) The block weight as defined in BIP 141\n"
            "  \"height\" : n,          (numeric) The block height or index\n"
            "  \"version\" : n,         (numeric) The block version\n"
            "  \"versionHex\" : \"00000000\", (string) The block version formatted in hexadecimal\n"
            "  \"merkleroot\" : \"xxxx\", (string) The merkle root\n"
            "  \"tx\" : [               (array of string) The transaction ids\n"
            "     \"transactionid\"     (string) The transaction id\n"
            "     ,...\n"
            "  ],\n"
            "  \"time\" : ttt,          (numeric) The block time in seconds since epoch (Jan 1 1970 GMT)\n"
            "  \"mediantime\" : ttt,    (numeric) The median block time in seconds since epoch (Jan 1 1970 GMT)\n"
            "  \"nonce\" : n,           (numeric) The nonce\n"
            "  \"bits\" : \"1d00ffff\", (string) The bits\n"
            "  \"difficulty\" : x.xxx,  (numeric) The difficulty\n"
            "  \"chainwork\" : \"xxxx\",  (string) Expected number of hashes required to produce the chain up to this block (in hex)\n"
            "  \"nTx\" : n,             (numeric) The number of transactions in the block.\n"
            "  \"previousblockhash\" : \"hash\",  (string) The hash of the previous block\n"
            "  \"nextblockhash\" : \"hash\"       (string) The hash of the next block\n"
            "}\n"
            "\nResult (for verbosity = 2):\n"
            "{\n"
            "  ...,                     Same output as verbosity = 1.\n"
            "  \"tx\" : [               (array of Objects) The transactions in the format of the getrawtransaction RPC. Different from verbosity = 1 \"tx\" result.\n"
            "         ,...\n"
            "  ],\n"
            "  ,...                     Same output as verbosity = 1.\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getblock", "\"00000000c937983704a73af28acdec37b049d214adbda81d7e2a3dd146f6ed09\"")
            + HelpExampleRpc("getblock", "\"00000000c937983704a73af28acdec37b049d214adbda81d7e2a3dd146f6ed09\"")
        );

    LOCK(cs_main);

    std::string strHash = request.params[0].get_str();
    uint256 hash(uint256S(strHash));

    int verbosity = 1;
    if (!request.params[1].isNull()) {
        if(request.params[1].isNum())
            verbosity = request.params[1].get_int();
        else
            verbosity = request.params[1].get_bool() ? 1 : 0;
    }

    if (mapBlockIndex.count(hash) == 0)
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");

    CBlock block;
    CBlockIndex* pblockindex = mapBlockIndex[hash];

    if (fHavePruned && !(pblockindex->nStatus & BLOCK_HAVE_DATA) && pblockindex->nTx > 0)
        throw JSONRPCError(RPC_MISC_ERROR, "Block not available (pruned data)");

    if (!ReadBlockFromDisk(block, pblockindex, Params().GetConsensus()))
        // Block not found on disk. This could be because we have the block
        // header in our index but don't have the block (for example if a
        // non-whitelisted node sends us an unrequested long chain of valid
        // blocks, we add the headers to our index, but don't accept the
        // block).
        throw JSONRPCError(RPC_MISC_ERROR, "Block not found on disk");

    if (verbosity <= 0)
    {
        CDataStream ssBlock(SER_NETWORK, PROTOCOL_VERSION | RPCSerializationFlags());
        ssBlock << block;
        std::string strHex = HexStr(ssBlock.begin(), ssBlock.end());
        return strHex;
    }

    return blockToJSON(block, pblockindex, verbosity >= 2);
}

struct CCoinsStats
{
    int nHeight;
    uint256 hashBlock;
    uint64_t nTransactions;
    uint64_t nTransactionOutputs;
    uint64_t nBogoSize;
    uint256 hashSerialized;
    uint64_t nDiskSize;
    CAmount nTotalAmount;

    CCoinsStats() : nHeight(0), nTransactions(0), nTransactionOutputs(0), nBogoSize(0), nDiskSize(0), nTotalAmount(0) {}
};

static void ApplyStats(CCoinsStats &stats, CHashWriter& ss, const uint256& hash, const std::map<uint32_t, Coin>& outputs)
{
    assert(!outputs.empty());
    ss << hash;
    ss << VARINT(outputs.begin()->second.nHeight * 2 + outputs.begin()->second.fCoinBase);
    stats.nTransactions++;
    for (const auto output : outputs) {
        ss << VARINT(output.first + 1);
        ss << output.second.out.scriptPubKey;
        ss << VARINT(output.second.out.nValue);
        stats.nTransactionOutputs++;
        stats.nTotalAmount += output.second.out.nValue;
        stats.nBogoSize += 32 /* txid */ + 4 /* vout index */ + 4 /* height + coinbase */ + 8 /* amount */ +
                           2 /* scriptPubKey len */ + output.second.out.scriptPubKey.size() /* scriptPubKey */;
    }
    ss << VARINT(0);
}

//! Calculate statistics about the unspent transaction output set
static bool GetUTXOStats(CCoinsView *view, CCoinsStats &stats)
{
    std::unique_ptr<CCoinsViewCursor> pcursor(view->Cursor());
    assert(pcursor);

    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    stats.hashBlock = pcursor->GetBestBlock();
    {
        LOCK(cs_main);
        stats.nHeight = mapBlockIndex.find(stats.hashBlock)->second->nHeight;
    }
    ss << stats.hashBlock;
    uint256 prevkey;
    std::map<uint32_t, Coin> outputs;
    while (pcursor->Valid()) {
        boost::this_thread::interruption_point();
        COutPoint key;
        Coin coin;
        if (pcursor->GetKey(key) && pcursor->GetValue(coin)) {
            if (!outputs.empty() && key.hash != prevkey) {
                ApplyStats(stats, ss, prevkey, outputs);
                outputs.clear();
            }
            prevkey = key.hash;
            outputs[key.n] = std::move(coin);
        } else {
            return error("%s: unable to read value", __func__);
        }
        pcursor->Next();
    }
    if (!outputs.empty()) {
        ApplyStats(stats, ss, prevkey, outputs);
    }
    stats.hashSerialized = ss.GetHash();
    stats.nDiskSize = view->EstimateSize();
    return true;
}

UniValue pruneblockchain(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "pruneblockchain\n"
            "\nArguments:\n"
            "1. \"height\"       (numeric, required) The block height to prune up to. May be set to a discrete height, or a unix timestamp\n"
            "                  to prune blocks whose block time is at least 2 hours older than the provided timestamp.\n"
            "\nResult:\n"
            "n    (numeric) Height of the last block pruned.\n"
            "\nExamples:\n"
            + HelpExampleCli("pruneblockchain", "1000")
            + HelpExampleRpc("pruneblockchain", "1000"));

    if (!fPruneMode)
        throw JSONRPCError(RPC_MISC_ERROR, "Cannot prune blocks because node is not in prune mode.");

    LOCK(cs_main);

    int heightParam = request.params[0].get_int();
    if (heightParam < 0)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Negative block height.");

    // Height value more than a billion is too high to be a block height, and
    // too low to be a block time (corresponds to timestamp from Sep 2001).
    if (heightParam > 1000000000) {
        // Add a 2 hour buffer to include blocks which might have had old timestamps
        CBlockIndex* pindex = chainActive.FindEarliestAtLeast(heightParam - TIMESTAMP_WINDOW);
        if (!pindex) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Could not find block with at least the specified timestamp.");
        }
        heightParam = pindex->nHeight;
    }

    unsigned int height = (unsigned int) heightParam;
    unsigned int chainHeight = (unsigned int) chainActive.Height();
    if (chainHeight < Params().PruneAfterHeight())
        throw JSONRPCError(RPC_MISC_ERROR, "Blockchain is too short for pruning.");
    else if (height > chainHeight)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Blockchain is shorter than the attempted prune height.");
    else if (height > chainHeight - MIN_BLOCKS_TO_KEEP) {
        LogPrint(BCLog::RPC, "Attempt to prune blocks close to the tip.  Retaining the minimum number of blocks.");
        height = chainHeight - MIN_BLOCKS_TO_KEEP;
    }

    PruneBlockFilesManual(height);
    return uint64_t(height);
}

UniValue gettxoutsetinfo(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 0)
        throw std::runtime_error(
            "gettxoutsetinfo\n"
            "\nReturns statistics about the unspent transaction output set.\n"
            "Note this call may take some time.\n"
            "\nResult:\n"
            "{\n"
            "  \"height\":n,     (numeric) The current block height (index)\n"
            "  \"bestblock\": \"hex\",   (string) The hash of the block at the tip of the chain\n"
            "  \"transactions\": n,      (numeric) The number of transactions with unspent outputs\n"
            "  \"txouts\": n,            (numeric) The number of unspent transaction outputs\n"
            "  \"bogosize\": n,          (numeric) A meaningless metric for UTXO set size\n"
            "  \"hash_serialized_2\": \"hash\", (string) The serialized hash\n"
            "  \"disk_size\": n,         (numeric) The estimated size of the chainstate on disk\n"
            "  \"total_amount\": x.xxx          (numeric) The total amount\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("gettxoutsetinfo", "")
            + HelpExampleRpc("gettxoutsetinfo", "")
        );

    UniValue ret(UniValue::VOBJ);

    CCoinsStats stats;
    FlushStateToDisk();
    if (GetUTXOStats(pcoinsdbview.get(), stats)) {
        ret.push_back(Pair("height", (int64_t)stats.nHeight));
        ret.push_back(Pair("bestblock", stats.hashBlock.GetHex()));
        ret.push_back(Pair("transactions", (int64_t)stats.nTransactions));
        ret.push_back(Pair("txouts", (int64_t)stats.nTransactionOutputs));
        ret.push_back(Pair("bogosize", (int64_t)stats.nBogoSize));
        ret.push_back(Pair("hash_serialized_2", stats.hashSerialized.GetHex()));
        ret.push_back(Pair("disk_size", stats.nDiskSize));
        ret.push_back(Pair("total_amount", ValueFromAmount(stats.nTotalAmount)));
    } else {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Unable to read UTXO set");
    }
    return ret;
}

UniValue gettxout(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 2 || request.params.size() > 3)
        throw std::runtime_error(
            "gettxout \"txid\" n ( include_mempool )\n"
            "\nReturns details about an unspent transaction output.\n"
            "\nArguments:\n"
            "1. \"txid\"             (string, required) The transaction id\n"
            "2. \"n\"                (numeric, required) vout number\n"
            "3. \"include_mempool\"  (boolean, optional) Whether to include the mempool. Default: true."
            "     Note that an unspent output that is spent in the mempool won't appear.\n"
            "\nResult:\n"
            "{\n"
            "  \"bestblock\":  \"hash\",    (string) The hash of the block at the tip of the chain\n"
            "  \"confirmations\" : n,       (numeric) The number of confirmations\n"
            "  \"value\" : x.xxx,           (numeric) The transaction value in " + CURRENCY_UNIT + "\n"
            "  \"scriptPubKey\" : {         (json object)\n"
            "     \"asm\" : \"code\",       (string) \n"
            "     \"hex\" : \"hex\",        (string) \n"
            "     \"reqSigs\" : n,          (numeric) Number of required signatures\n"
            "     \"type\" : \"pubkeyhash\", (string) The type, eg pubkeyhash\n"
            "     \"addresses\" : [          (array of string) array of datacoin addresses\n"
            "        \"address\"     (string) datacoin address\n"
            "        ,...\n"
            "     ]\n"
            "  },\n"
            "  \"coinbase\" : true|false   (boolean) Coinbase or not\n"
            "}\n"

            "\nExamples:\n"
            "\nGet unspent transactions\n"
            + HelpExampleCli("listunspent", "") +
            "\nView the details\n"
            + HelpExampleCli("gettxout", "\"txid\" 1") +
            "\nAs a json rpc call\n"
            + HelpExampleRpc("gettxout", "\"txid\", 1")
        );

    LOCK(cs_main);

    UniValue ret(UniValue::VOBJ);

    std::string strHash = request.params[0].get_str();
    uint256 hash(uint256S(strHash));
    int n = request.params[1].get_int();
    COutPoint out(hash, n);
    bool fMempool = true;
    if (!request.params[2].isNull())
        fMempool = request.params[2].get_bool();

    Coin coin;
    if (fMempool) {
        LOCK(mempool.cs);
        CCoinsViewMemPool view(pcoinsTip.get(), mempool);
        if (!view.GetCoin(out, coin) || mempool.isSpent(out)) {
            return NullUniValue;
        }
    } else {
        if (!pcoinsTip->GetCoin(out, coin)) {
            return NullUniValue;
        }
    }

    BlockMap::iterator it = mapBlockIndex.find(pcoinsTip->GetBestBlock());
    CBlockIndex *pindex = it->second;
    ret.push_back(Pair("bestblock", pindex->GetBlockHash().GetHex()));
    if (coin.nHeight == MEMPOOL_HEIGHT) {
        ret.push_back(Pair("confirmations", 0));
    } else {
        ret.push_back(Pair("confirmations", (int64_t)(pindex->nHeight - coin.nHeight + 1)));
    }
    ret.push_back(Pair("value", ValueFromAmount(coin.out.nValue)));
    UniValue o(UniValue::VOBJ);
    ScriptPubKeyToUniv(coin.out.scriptPubKey, o, true);
    ret.push_back(Pair("scriptPubKey", o));
    ret.push_back(Pair("coinbase", (bool)coin.fCoinBase));

    return ret;
}

UniValue verifychain(const JSONRPCRequest& request)
{
    int nCheckLevel = gArgs.GetArg("-checklevel", DEFAULT_CHECKLEVEL);
    int nCheckDepth = gArgs.GetArg("-checkblocks", DEFAULT_CHECKBLOCKS);
    if (request.fHelp || request.params.size() > 2)
        throw std::runtime_error(
            "verifychain ( checklevel nblocks )\n"
            "\nVerifies blockchain database.\n"
            "\nArguments:\n"
            "1. checklevel   (numeric, optional, 0-4, default=" + strprintf("%d", nCheckLevel) + ") How thorough the block verification is.\n"
            "2. nblocks      (numeric, optional, default=" + strprintf("%d", nCheckDepth) + ", 0=all) The number of blocks to check.\n"
            "\nResult:\n"
            "true|false       (boolean) Verified or not\n"
            "\nExamples:\n"
            + HelpExampleCli("verifychain", "")
            + HelpExampleRpc("verifychain", "")
        );

    LOCK(cs_main);

    if (!request.params[0].isNull())
        nCheckLevel = request.params[0].get_int();
    if (!request.params[1].isNull())
        nCheckDepth = request.params[1].get_int();

    return CVerifyDB().VerifyDB(Params(), pcoinsTip.get(), nCheckLevel, nCheckDepth);
}

/** Implementation of IsSuperMajority with better feedback */
static UniValue SoftForkMajorityDesc(int version, CBlockIndex* pindex, const Consensus::Params& consensusParams)
{
    UniValue rv(UniValue::VOBJ);
    bool activated = false;
    switch(version)
    {
        case 2:
            activated = pindex->nHeight >= consensusParams.BIP34Height;
            break;
        case 3:
            activated = pindex->nHeight >= consensusParams.BIP66Height;
            break;
        case 4:
            activated = pindex->nHeight >= consensusParams.BIP65Height;
            break;
    }
    rv.push_back(Pair("status", activated));
    return rv;
}

static UniValue SoftForkDesc(const std::string &name, int version, CBlockIndex* pindex, const Consensus::Params& consensusParams)
{
    UniValue rv(UniValue::VOBJ);
    rv.push_back(Pair("id", name));
    rv.push_back(Pair("version", version));
    rv.push_back(Pair("reject", SoftForkMajorityDesc(version, pindex, consensusParams)));
    return rv;
}

static UniValue BIP9SoftForkDesc(const Consensus::Params& consensusParams, Consensus::DeploymentPos id)
{
    UniValue rv(UniValue::VOBJ);
    const ThresholdState thresholdState = VersionBitsTipState(consensusParams, id);
    switch (thresholdState) {
    case THRESHOLD_DEFINED: rv.push_back(Pair("status", "defined")); break;
    case THRESHOLD_STARTED: rv.push_back(Pair("status", "started")); break;
    case THRESHOLD_LOCKED_IN: rv.push_back(Pair("status", "locked_in")); break;
    case THRESHOLD_ACTIVE: rv.push_back(Pair("status", "active")); break;
    case THRESHOLD_FAILED: rv.push_back(Pair("status", "failed")); break;
    }
    if (THRESHOLD_STARTED == thresholdState)
    {
        rv.push_back(Pair("bit", consensusParams.vDeployments[id].bit));
    }
    rv.push_back(Pair("startTime", consensusParams.vDeployments[id].nStartTime));
    rv.push_back(Pair("timeout", consensusParams.vDeployments[id].nTimeout));
    rv.push_back(Pair("since", VersionBitsTipStateSinceHeight(consensusParams, id)));
    if (THRESHOLD_STARTED == thresholdState)
    {
        UniValue statsUV(UniValue::VOBJ);
        BIP9Stats statsStruct = VersionBitsTipStatistics(consensusParams, id);
        statsUV.push_back(Pair("period", statsStruct.period));
        statsUV.push_back(Pair("threshold", statsStruct.threshold));
        statsUV.push_back(Pair("elapsed", statsStruct.elapsed));
        statsUV.push_back(Pair("count", statsStruct.count));
        statsUV.push_back(Pair("possible", statsStruct.possible));
        rv.push_back(Pair("statistics", statsUV));
    }
    return rv;
}

void BIP9SoftForkDescPushBack(UniValue& bip9_softforks, const Consensus::Params& consensusParams, Consensus::DeploymentPos id)
{
    // Deployments with timeout value of 0 are hidden.
    // A timeout value of 0 guarantees a softfork will never be activated.
    // This is used when softfork codes are merged without specifying the deployment schedule.
    if (consensusParams.vDeployments[id].nTimeout > 0)
        bip9_softforks.push_back(Pair(VersionBitsDeploymentInfo[id].name, BIP9SoftForkDesc(consensusParams, id)));
}

UniValue getblockchaininfo(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 0)
        throw std::runtime_error(
            "getblockchaininfo\n"
            "Returns an object containing various state info regarding blockchain processing.\n"
            "\nResult:\n"
            "{\n"
            "  \"chain\": \"xxxx\",              (string) current network name as defined in BIP70 (main, test, regtest)\n"
            "  \"blocks\": xxxxxx,             (numeric) the current number of blocks processed in the server\n"
            "  \"headers\": xxxxxx,            (numeric) the current number of headers we have validated\n"
            "  \"bestblockhash\": \"...\",       (string) the hash of the currently best block\n"
            "  \"difficulty\": xxxxxx,         (numeric) the current difficulty\n"
            "  \"mediantime\": xxxxxx,         (numeric) median time for the current best block\n"
            "  \"verificationprogress\": xxxx, (numeric) estimate of verification progress [0..1]\n"
            "  \"initialblockdownload\": xxxx, (bool) (debug information) estimate of whether this node is in Initial Block Download mode.\n"
            "  \"chainwork\": \"xxxx\"           (string) total amount of work in active chain, in hexadecimal\n"
            "  \"size_on_disk\": xxxxxx,       (numeric) the estimated size of the block and undo files on disk\n"
            "  \"pruned\": xx,                 (boolean) if the blocks are subject to pruning\n"
            "  \"pruneheight\": xxxxxx,        (numeric) lowest-height complete block stored (only present if pruning is enabled)\n"
            "  \"automatic_pruning\": xx,      (boolean) whether automatic pruning is enabled (only present if pruning is enabled)\n"
            "  \"prune_target_size\": xxxxxx,  (numeric) the target size used by pruning (only present if automatic pruning is enabled)\n"
            "  \"softforks\": [                (array) status of softforks in progress\n"
            "     {\n"
            "        \"id\": \"xxxx\",           (string) name of softfork\n"
            "        \"version\": xx,          (numeric) block version\n"
            "        \"reject\": {             (object) progress toward rejecting pre-softfork blocks\n"
            "           \"status\": xx,        (boolean) true if threshold reached\n"
            "        },\n"
            "     }, ...\n"
            "  ],\n"
            "  \"bip9_softforks\": {           (object) status of BIP9 softforks in progress\n"
            "     \"xxxx\" : {                 (string) name of the softfork\n"
            "        \"status\": \"xxxx\",       (string) one of \"defined\", \"started\", \"locked_in\", \"active\", \"failed\"\n"
            "        \"bit\": xx,              (numeric) the bit (0-28) in the block version field used to signal this softfork (only for \"started\" status)\n"
            "        \"startTime\": xx,        (numeric) the minimum median time past of a block at which the bit gains its meaning\n"
            "        \"timeout\": xx,          (numeric) the median time past of a block at which the deployment is considered failed if not yet locked in\n"
            "        \"since\": xx,            (numeric) height of the first block to which the status applies\n"
            "        \"statistics\": {         (object) numeric statistics about BIP9 signalling for a softfork (only for \"started\" status)\n"
            "           \"period\": xx,        (numeric) the length in blocks of the BIP9 signalling period \n"
            "           \"threshold\": xx,     (numeric) the number of blocks with the version bit set required to activate the feature \n"
            "           \"elapsed\": xx,       (numeric) the number of blocks elapsed since the beginning of the current period \n"
            "           \"count\": xx,         (numeric) the number of blocks with the version bit set in the current period \n"
            "           \"possible\": xx       (boolean) returns false if there are not enough blocks left in this period to pass activation threshold \n"
            "        }\n"
            "     }\n"
            "  }\n"
            "  \"warnings\" : \"...\",           (string) any network and blockchain warnings.\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getblockchaininfo", "")
            + HelpExampleRpc("getblockchaininfo", "")
        );

    LOCK(cs_main);

    UniValue obj(UniValue::VOBJ);
    obj.push_back(Pair("chain",                 Params().NetworkIDString()));
    obj.push_back(Pair("blocks",                (int)chainActive.Height()));
    obj.push_back(Pair("headers",               pindexBestHeader ? pindexBestHeader->nHeight : -1));
    obj.push_back(Pair("bestblockhash",         chainActive.Tip()->GetBlockHash().GetHex()));
    obj.push_back(Pair("difficulty",            (double)GetDifficulty()));
    obj.push_back(Pair("mediantime",            (int64_t)chainActive.Tip()->GetMedianTimePast()));
    obj.push_back(Pair("verificationprogress",  GuessVerificationProgress(Params().TxData(), chainActive.Tip())));
    obj.push_back(Pair("initialblockdownload",  IsInitialBlockDownload()));
    obj.push_back(Pair("chainwork",             chainActive.Tip()->nChainWork.GetHex()));
    obj.push_back(Pair("size_on_disk",          CalculateCurrentUsage()));
    obj.push_back(Pair("pruned",                fPruneMode));
    if (fPruneMode) {
        CBlockIndex* block = chainActive.Tip();
        assert(block);
        while (block->pprev && (block->pprev->nStatus & BLOCK_HAVE_DATA)) {
            block = block->pprev;
        }

        obj.push_back(Pair("pruneheight",        block->nHeight));

        // if 0, execution bypasses the whole if block.
        bool automatic_pruning = (gArgs.GetArg("-prune", 0) != 1);
        obj.push_back(Pair("automatic_pruning",  automatic_pruning));
        if (automatic_pruning) {
            obj.push_back(Pair("prune_target_size",  nPruneTarget));
        }
    }

    const Consensus::Params& consensusParams = Params().GetConsensus();
    CBlockIndex* tip = chainActive.Tip();
    UniValue softforks(UniValue::VARR);
    UniValue bip9_softforks(UniValue::VOBJ);
    softforks.push_back(SoftForkDesc("bip34", 2, tip, consensusParams));
    softforks.push_back(SoftForkDesc("bip66", 3, tip, consensusParams));
    softforks.push_back(SoftForkDesc("bip65", 4, tip, consensusParams));
    for (int pos = Consensus::DEPLOYMENT_CSV; pos != Consensus::MAX_VERSION_BITS_DEPLOYMENTS; ++pos) {
        BIP9SoftForkDescPushBack(bip9_softforks, consensusParams, static_cast<Consensus::DeploymentPos>(pos));
    }
    obj.push_back(Pair("softforks",             softforks));
    obj.push_back(Pair("bip9_softforks", bip9_softforks));

    obj.push_back(Pair("warnings", GetWarnings("statusbar")));
    return obj;
}

/** Comparison function for sorting the getchaintips heads.  */
struct CompareBlocksByHeight
{
    bool operator()(const CBlockIndex* a, const CBlockIndex* b) const
    {
        /* Make sure that unequal blocks with the same height do not compare
           equal. Use the pointers themselves to make a distinction. */

        if (a->nHeight != b->nHeight)
          return (a->nHeight > b->nHeight);

        return a < b;
    }
};

UniValue getchaintips(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 0)
        throw std::runtime_error(
            "getchaintips\n"
            "Return information about all known tips in the block tree,"
            " including the main chain as well as orphaned branches.\n"
            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"height\": xxxx,         (numeric) height of the chain tip\n"
            "    \"hash\": \"xxxx\",         (string) block hash of the tip\n"
            "    \"branchlen\": 0          (numeric) zero for main chain\n"
            "    \"status\": \"active\"      (string) \"active\" for the main chain\n"
            "  },\n"
            "  {\n"
            "    \"height\": xxxx,\n"
            "    \"hash\": \"xxxx\",\n"
            "    \"branchlen\": 1          (numeric) length of branch connecting the tip to the main chain\n"
            "    \"status\": \"xxxx\"        (string) status of the chain (active, valid-fork, valid-headers, headers-only, invalid)\n"
            "  }\n"
            "]\n"
            "Possible values for status:\n"
            "1.  \"invalid\"               This branch contains at least one invalid block\n"
            "2.  \"headers-only\"          Not all blocks for this branch are available, but the headers are valid\n"
            "3.  \"valid-headers\"         All blocks are available for this branch, but they were never fully validated\n"
            "4.  \"valid-fork\"            This branch is not part of the active chain, but is fully validated\n"
            "5.  \"active\"                This is the tip of the active main chain, which is certainly valid\n"
            "\nExamples:\n"
            + HelpExampleCli("getchaintips", "")
            + HelpExampleRpc("getchaintips", "")
        );

    LOCK(cs_main);

    /*
     * Idea:  the set of chain tips is chainActive.tip, plus orphan blocks which do not have another orphan building off of them.
     * Algorithm:
     *  - Make one pass through mapBlockIndex, picking out the orphan blocks, and also storing a set of the orphan block's pprev pointers.
     *  - Iterate through the orphan blocks. If the block isn't pointed to by another orphan, it is a chain tip.
     *  - add chainActive.Tip()
     */
    std::set<const CBlockIndex*, CompareBlocksByHeight> setTips;
    std::set<const CBlockIndex*> setOrphans;
    std::set<const CBlockIndex*> setPrevs;

    for (const std::pair<const uint256, CBlockIndex*>& item : mapBlockIndex)
    {
        if (!chainActive.Contains(item.second)) {
            setOrphans.insert(item.second);
            setPrevs.insert(item.second->pprev);
        }
    }

    for (std::set<const CBlockIndex*>::iterator it = setOrphans.begin(); it != setOrphans.end(); ++it)
    {
        if (setPrevs.erase(*it) == 0) {
            setTips.insert(*it);
        }
    }

    // Always report the currently active tip.
    setTips.insert(chainActive.Tip());

    /* Construct the output array.  */
    UniValue res(UniValue::VARR);
    for (const CBlockIndex* block : setTips)
    {
        UniValue obj(UniValue::VOBJ);
        obj.push_back(Pair("height", block->nHeight));
        obj.push_back(Pair("hash", block->phashBlock->GetHex()));

        const int branchLen = block->nHeight - chainActive.FindFork(block)->nHeight;
        obj.push_back(Pair("branchlen", branchLen));

        std::string status;
        if (chainActive.Contains(block)) {
            // This block is part of the currently active chain.
            status = "active";
        } else if (block->nStatus & BLOCK_FAILED_MASK) {
            // This block or one of its ancestors is invalid.
            status = "invalid";
        } else if (block->nChainTx == 0) {
            // This block cannot be connected because full block data for it or one of its parents is missing.
            status = "headers-only";
        } else if (block->IsValid(BLOCK_VALID_SCRIPTS)) {
            // This block is fully validated, but no longer part of the active chain. It was probably the active block once, but was reorganized.
            status = "valid-fork";
        } else if (block->IsValid(BLOCK_VALID_TREE)) {
            // The headers for this block are valid, but it has not been validated. It was probably never part of the most-work chain.
            status = "valid-headers";
        } else {
            // No clue.
            status = "unknown";
        }
        obj.push_back(Pair("status", status));

        res.push_back(obj);
    }

    return res;
}

UniValue mempoolInfoToJSON()
{
    UniValue ret(UniValue::VOBJ);
    ret.push_back(Pair("size", (int64_t) mempool.size()));
    ret.push_back(Pair("bytes", (int64_t) mempool.GetTotalTxSize()));
    ret.push_back(Pair("usage", (int64_t) mempool.DynamicMemoryUsage()));
    size_t maxmempool = gArgs.GetArg("-maxmempool", DEFAULT_MAX_MEMPOOL_SIZE) * 1000000;
    ret.push_back(Pair("maxmempool", (int64_t) maxmempool));
    ret.push_back(Pair("mempoolminfee", ValueFromAmount(std::max(mempool.GetMinFee(maxmempool), ::minRelayTxFee).GetFeePerK())));
    ret.push_back(Pair("minrelaytxfee", ValueFromAmount(::minRelayTxFee.GetFeePerK())));

    return ret;
}

UniValue getmempoolinfo(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 0)
        throw std::runtime_error(
            "getmempoolinfo\n"
            "\nReturns details on the active state of the TX memory pool.\n"
            "\nResult:\n"
            "{\n"
            "  \"size\": xxxxx,               (numeric) Current tx count\n"
            "  \"bytes\": xxxxx,              (numeric) Sum of all virtual transaction sizes as defined in BIP 141. Differs from actual serialized size because witness data is discounted\n"
            "  \"usage\": xxxxx,              (numeric) Total memory usage for the mempool\n"
            "  \"maxmempool\": xxxxx,         (numeric) Maximum memory usage for the mempool\n"
            "  \"mempoolminfee\": xxxxx       (numeric) Minimum fee rate in " + CURRENCY_UNIT + "/kB for tx to be accepted. Is the maximum of minrelaytxfee and minimum mempool fee\n"
            "  \"minrelaytxfee\": xxxxx       (numeric) Current minimum relay fee for transactions\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getmempoolinfo", "")
            + HelpExampleRpc("getmempoolinfo", "")
        );

    return mempoolInfoToJSON();
}

UniValue preciousblock(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "preciousblock \"blockhash\"\n"
            "\nTreats a block as if it were received before others with the same work.\n"
            "\nA later preciousblock call can override the effect of an earlier one.\n"
            "\nThe effects of preciousblock are not retained across restarts.\n"
            "\nArguments:\n"
            "1. \"blockhash\"   (string, required) the hash of the block to mark as precious\n"
            "\nResult:\n"
            "\nExamples:\n"
            + HelpExampleCli("preciousblock", "\"blockhash\"")
            + HelpExampleRpc("preciousblock", "\"blockhash\"")
        );

    std::string strHash = request.params[0].get_str();
    uint256 hash(uint256S(strHash));
    CBlockIndex* pblockindex;

    {
        LOCK(cs_main);
        if (mapBlockIndex.count(hash) == 0)
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");

        pblockindex = mapBlockIndex[hash];
    }

    CValidationState state;
    PreciousBlock(state, Params(), pblockindex);

    if (!state.IsValid()) {
        throw JSONRPCError(RPC_DATABASE_ERROR, state.GetRejectReason());
    }

    return NullUniValue;
}

UniValue invalidateblock(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "invalidateblock \"blockhash\"\n"
            "\nPermanently marks a block as invalid, as if it violated a consensus rule.\n"
            "\nArguments:\n"
            "1. \"blockhash\"   (string, required) the hash of the block to mark as invalid\n"
            "\nResult:\n"
            "\nExamples:\n"
            + HelpExampleCli("invalidateblock", "\"blockhash\"")
            + HelpExampleRpc("invalidateblock", "\"blockhash\"")
        );

    std::string strHash = request.params[0].get_str();
    uint256 hash(uint256S(strHash));
    CValidationState state;

    {
        LOCK(cs_main);
        if (mapBlockIndex.count(hash) == 0)
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");

        CBlockIndex* pblockindex = mapBlockIndex[hash];
        InvalidateBlock(state, Params(), pblockindex);
    }

    if (state.IsValid()) {
        ActivateBestChain(state, Params());
    }

    if (!state.IsValid()) {
        throw JSONRPCError(RPC_DATABASE_ERROR, state.GetRejectReason());
    }

    return NullUniValue;
}

UniValue reconsiderblock(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "reconsiderblock \"blockhash\"\n"
            "\nRemoves invalidity status of a block and its descendants, reconsider them for activation.\n"
            "This can be used to undo the effects of invalidateblock.\n"
            "\nArguments:\n"
            "1. \"blockhash\"   (string, required) the hash of the block to reconsider\n"
            "\nResult:\n"
            "\nExamples:\n"
            + HelpExampleCli("reconsiderblock", "\"blockhash\"")
            + HelpExampleRpc("reconsiderblock", "\"blockhash\"")
        );

    std::string strHash = request.params[0].get_str();
    uint256 hash(uint256S(strHash));

    {
        LOCK(cs_main);
        if (mapBlockIndex.count(hash) == 0)
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");

        CBlockIndex* pblockindex = mapBlockIndex[hash];
        ResetBlockFailureFlags(pblockindex);
    }

    CValidationState state;
    ActivateBestChain(state, Params());

    if (!state.IsValid()) {
        throw JSONRPCError(RPC_DATABASE_ERROR, state.GetRejectReason());
    }

    return NullUniValue;
}

UniValue getchaintxstats(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 2)
        throw std::runtime_error(
            "getchaintxstats ( nblocks blockhash )\n"
            "\nCompute statistics about the total number and rate of transactions in the chain.\n"
            "\nArguments:\n"
            "1. nblocks      (numeric, optional) Size of the window in number of blocks (default: one month).\n"
            "2. \"blockhash\"  (string, optional) The hash of the block that ends the window.\n"
            "\nResult:\n"
            "{\n"
            "  \"time\": xxxxx,                (numeric) The timestamp for the final block in the window in UNIX format.\n"
            "  \"txcount\": xxxxx,             (numeric) The total number of transactions in the chain up to that point.\n"
            "  \"window_block_count\": xxxxx,  (numeric) Size of the window in number of blocks.\n"
            "  \"window_tx_count\": xxxxx,     (numeric) The number of transactions in the window. Only returned if \"window_block_count\" is > 0.\n"
            "  \"window_interval\": xxxxx,     (numeric) The elapsed time in the window in seconds. Only returned if \"window_block_count\" is > 0.\n"
            "  \"txrate\": x.xx,               (numeric) The average rate of transactions per second in the window. Only returned if \"window_interval\" is > 0.\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getchaintxstats", "")
            + HelpExampleRpc("getchaintxstats", "2016")
        );

    const CBlockIndex* pindex;
    int blockcount = 30 * 24 * 60 * 60 / Params().GetConsensus().nPowTargetSpacing; // By default: 1 month

    bool havehash = !request.params[1].isNull();
    uint256 hash;
    if (havehash) {
        hash = uint256S(request.params[1].get_str());
    }

    {
        LOCK(cs_main);
        if (havehash) {
            auto it = mapBlockIndex.find(hash);
            if (it == mapBlockIndex.end()) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");
            }
            pindex = it->second;
            if (!chainActive.Contains(pindex)) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Block is not in main chain");
            }
        } else {
            pindex = chainActive.Tip();
        }
    }
    
    assert(pindex != nullptr);

    if (request.params[0].isNull()) {
        blockcount = std::max(0, std::min(blockcount, pindex->nHeight - 1));
    } else {
        blockcount = request.params[0].get_int();

        if (blockcount < 0 || (blockcount > 0 && blockcount >= pindex->nHeight)) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid block count: should be between 0 and the block's height - 1");
        }
    }

    const CBlockIndex* pindexPast = pindex->GetAncestor(pindex->nHeight - blockcount);
    int nTimeDiff = pindex->GetMedianTimePast() - pindexPast->GetMedianTimePast();
    int nTxDiff = pindex->nChainTx - pindexPast->nChainTx;
    long long int nDataSizeDiff = pindex->nChainDataSize - pindexPast->nChainDataSize;

    UniValue ret(UniValue::VOBJ);
    ret.push_back(Pair("time", (int64_t)pindex->nTime));
    ret.push_back(Pair("txcount", (int64_t)pindex->nChainTx));
    ret.push_back(Pair("datasize", (int64_t)pindex->nChainDataSize));
    ret.push_back(Pair("window_block_count", blockcount));
    if (blockcount > 0) {
        ret.push_back(Pair("window_tx_count", nTxDiff));
        ret.push_back(Pair("window_data_size", (int64_t)nDataSizeDiff));
        ret.push_back(Pair("window_interval", nTimeDiff));
        if (nTimeDiff > 0) {
            ret.push_back(Pair("txrate", ((double)nTxDiff) / nTimeDiff));
            ret.push_back(Pair("datarate", ((double)nDataSizeDiff) / nTimeDiff));
        }
    }

    return ret;
}

UniValue savemempool(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 0) {
        throw std::runtime_error(
            "savemempool\n"
            "\nDumps the mempool to disk.\n"
            "\nExamples:\n"
            + HelpExampleCli("savemempool", "")
            + HelpExampleRpc("savemempool", "")
        );
    }

    if (!DumpMempool()) {
        throw JSONRPCError(RPC_MISC_ERROR, "Unable to dump mempool to disk");
    }

    return NullUniValue;
}

// Primecoin: list prime chain records within primecoin network
UniValue listprimerecords(const JSONRPCRequest& request)
{   
    auto& fHelp = request.fHelp;
    auto& params = request.params;

    if (fHelp || params.size() < 1 || params.size() > 2)
        throw std::runtime_error(
            "listprimerecords <primechain length> [primechain type]\n"
            "Returns the list of record prime chains in datacoin network.\n"
            "<primechain length> is integer like 10, 11, 12 etc.\n"
            "[primechain type] is optional type, among 1CC, 2CC and TWN");

    int nPrimeChainLength = params[0].get_int();
    unsigned int nPrimeChainType = 0;
    if (params.size() > 1)
    {
        std::string strPrimeChainType = params[1].get_str();
        if (strPrimeChainType.compare("1CC") == 0)
            nPrimeChainType = PRIME_CHAIN_CUNNINGHAM1;
        else if (strPrimeChainType.compare("2CC") == 0)
            nPrimeChainType = PRIME_CHAIN_CUNNINGHAM2;
        else if (strPrimeChainType.compare("TWN") == 0)
            nPrimeChainType = PRIME_CHAIN_BI_TWIN;
        else
            throw std::runtime_error("Prime chain type must be 1CC, 2CC or TWN.");
    }

    UniValue ret(UniValue::VOBJ);

    CBigNum bnPrimeRecord = 0;

    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    bool fwavail = EnsureWalletIsAvailable(pwallet, request.fHelp);
                
    uint32_t nHeight=0;
    for (CBlockIndex* pindex = chainActive[nHeight]; pindex; nHeight++)
    {
        if (nPrimeChainLength != (int) TargetGetLength(pindex->nPrimeChainLength))
            continue; // length not matching, next block
        if (nPrimeChainType && nPrimeChainType != pindex->nPrimeChainType)
            continue; // type not matching, next block

        CBlock block;
        if (!ReadBlockFromDisk(block, pindex, Params().GetConsensus())) continue;

        CBigNum bnPrimeChainOrigin = CBigNum(block.GetHeaderHash()) * block.bnPrimeChainMultiplier; // compute prime chain origin

        if (bnPrimeChainOrigin > bnPrimeRecord)
        {
            bnPrimeRecord = bnPrimeChainOrigin; // new record in primecoin
            ret.push_back(Pair("time", DateTimeStrFormat("%Y-%m-%d %H:%M:%S UTC", pindex->GetBlockTime())));
            ret.push_back(Pair("epoch", (boost::int64_t) pindex->GetBlockTime()));
            ret.push_back(Pair("height", pindex->nHeight));
            if (fwavail) ret.push_back(Pair("ismine", pwallet->IsMine(*(block.vtx[0]))));
            CTxDestination address;
            ret.push_back(Pair("mineraddress", (block.vtx[0]->vout.size() > 1)? "multiple" : ExtractDestination(block.vtx[0]->vout[0].scriptPubKey, address)? EncodeDestination(address) : "invalid"));
            ret.push_back(Pair("primedigit", (int) bnPrimeChainOrigin.ToString().length()));
            ret.push_back(Pair("primechain", GetPrimeChainName(pindex->nPrimeChainType, pindex->nPrimeChainLength)));
            ret.push_back(Pair("primeorigin", bnPrimeChainOrigin.ToString()));
            ret.push_back(Pair("primorialform", GetPrimeOriginPrimorialForm(bnPrimeChainOrigin)));
        }
    }

    return ret;
}

// Primecoin: list top prime chain within primecoin network
UniValue listtopprimes(const JSONRPCRequest& request)
{   
    auto& fHelp = request.fHelp;
    auto& params = request.params;

    if (fHelp || params.size() < 1 || params.size() > 2)
        throw std::runtime_error(
            "listtopprimes <primechain length> [primechain type]\n"
            "Returns the list of top prime chains in datacoin network.\n"
            "<primechain length> is integer like 10, 11, 12 etc.\n"
            "[primechain type] is optional type, among 1CC, 2CC and TWN");

    int nPrimeChainLength = params[0].get_int();
    unsigned int nPrimeChainType = 0;
    if (params.size() > 1)
    {
        std::string strPrimeChainType = params[1].get_str();
        if (strPrimeChainType.compare("1CC") == 0)
            nPrimeChainType = PRIME_CHAIN_CUNNINGHAM1;
        else if (strPrimeChainType.compare("2CC") == 0)
            nPrimeChainType = PRIME_CHAIN_CUNNINGHAM2;
        else if (strPrimeChainType.compare("TWN") == 0)
            nPrimeChainType = PRIME_CHAIN_BI_TWIN;
        else
            throw std::runtime_error("Prime chain type must be 1CC, 2CC or TWN.");
    }

    // Search for top prime chains
    unsigned int nRankingSize = 10; // ranking list size
    unsigned int nSortVectorSize = 64; // vector size for sort operation
    CBigNum bnPrimeQualify = 0; // minimum qualify value for ranking list
    std::vector<std::pair<CBigNum, uint256> > vSortedByOrigin;
    
    uint32_t nHeight=0;
    for (CBlockIndex* pindex = chainActive[nHeight]; pindex; nHeight++)
    {
        if (nPrimeChainLength != (int) TargetGetLength(pindex->nPrimeChainLength))
            continue; // length not matching, next block
        if (nPrimeChainType && nPrimeChainType != pindex->nPrimeChainType)
            continue; // type not matching, next block

        CBlock block;
        if (!ReadBlockFromDisk(block, pindex, Params().GetConsensus())) continue;
        CBigNum bnPrimeChainOrigin = CBigNum(block.GetHeaderHash()) * block.bnPrimeChainMultiplier; // compute prime chain origin

        if (bnPrimeChainOrigin > bnPrimeQualify)
            vSortedByOrigin.push_back(std::make_pair(bnPrimeChainOrigin, block.GetHash()));

        if (vSortedByOrigin.size() >= nSortVectorSize)
        {
            // Sort prime chain candidates
            sort(vSortedByOrigin.begin(), vSortedByOrigin.end());
            reverse(vSortedByOrigin.begin(), vSortedByOrigin.end());
            // Truncate candidate list
            while (vSortedByOrigin.size() > nRankingSize)
                vSortedByOrigin.pop_back();
            // Update minimum qualify value for top prime chains
            bnPrimeQualify = vSortedByOrigin.back().first;
        }
    }

    // Final sort of prime chain candidates
    sort(vSortedByOrigin.begin(), vSortedByOrigin.end());
    reverse(vSortedByOrigin.begin(), vSortedByOrigin.end());
    // Truncate candidate list
    while (vSortedByOrigin.size() > nRankingSize) {
        vSortedByOrigin.pop_back();
    }

    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    bool fwavail = EnsureWalletIsAvailable(pwallet, request.fHelp);
    
    // Output top prime chains
    UniValue ret(UniValue::VOBJ);
    for(const auto& item : vSortedByOrigin)
    {
        CBigNum bnPrimeChainOrigin = item.first;
        CBlockIndex* pindex = mapBlockIndex[item.second];
        CBlock block;
        if (!ReadBlockFromDisk(block, pindex, Params().GetConsensus())) continue;
        ret.push_back(Pair("time", DateTimeStrFormat("%Y-%m-%d %H:%M:%S UTC", pindex->GetBlockTime())));
        ret.push_back(Pair("epoch", (boost::int64_t) pindex->GetBlockTime()));
        ret.push_back(Pair("height", pindex->nHeight));
        if (fwavail) ret.push_back(Pair("ismine", pwallet->IsMine(*(block.vtx[0]))));
        CTxDestination address;
        ret.push_back(Pair("mineraddress", (block.vtx[0]->vout.size() > 1)? "multiple" : ExtractDestination(block.vtx[0]->vout[0].scriptPubKey, address)? EncodeDestination(address) : "invalid"));
        ret.push_back(Pair("primedigit", (int) bnPrimeChainOrigin.ToString().length()));
        ret.push_back(Pair("primechain", GetPrimeChainName(pindex->nPrimeChainType, pindex->nPrimeChainLength)));
        ret.push_back(Pair("primeorigin", bnPrimeChainOrigin.ToString()));
        ret.push_back(Pair("primorialform", GetPrimeOriginPrimorialForm(bnPrimeChainOrigin)));
    }

    return ret;
}



static std::string blockgraph(const CBlockIndex* pblockindex)
{

    bool withTypes = false;

    std::map<std::string,std::string> blockkeys = {
        std::make_pair("difficulty", "decimal"),
        std::make_pair("height", "integer"),
        std::make_pair("mediantime", "integer"),
        std::make_pair("primechain", "string"),
        std::make_pair("primechainmultiplier", "integer"),
        std::make_pair("primeorigin", "integer"),
        std::make_pair("size", "integer"),
        std::make_pair("time", "integer"),
        std::make_pair("transition", "decimal"),
    };

    // std::string chainid = "<http://purl.org/net/bel-epa/ccy#C324fff4a-c492-4e8b-94f4-2f599efd7ba1> ";
    std::string rdfs = "<http://www.w3.org/1999/02/22-rdf-syntax-ns#";
    std::string ccy = "<http://purl.org/net/bel-epa/ccy#";
    std::string ccyC = ccy + "C";
    std::string doacc = "<http://purl.org/net/bel-epa/doacc#";

    std::stringstream stream;

    CBlock block;
    auto& consensus_params = Params().GetConsensus();
    UniValue data;
    {
        LOCK(cs_main);
        ReadBlockFromDisk(block, pblockindex, consensus_params);
        data = blockToJSON(block, pblockindex, true);
    }

    std::string blockid = "<http://purl.org/net/bel-epa/ccy#C" + data["hash"].getValStr() + "> ";
    if (withTypes)
        stream << blockid << rdfs << "type> " << ccy << "Block> ." << std::endl;
    if (!data["nextblockhash"].isNull())
        stream << blockid << ccy << "next> " << ccyC << data["nextblockhash"].getValStr() << "> ." << std::endl;
    if (pblockindex->nHeight > 0)
        stream << blockid << ccy << "prev> " << ccyC << data["previousblockhash"].getValStr() << "> ." << std::endl;
    stream << blockid << ccy << "time> \"" << DateTimeStrFormat("%Y-%m-%dT%H:%M:%SZ", std::stoll(data["time"].getValStr())) << "\"^^<http://www.w3.org/2001/XMLSchema#dateTime> ." << std::endl;

    for (auto it = blockkeys.begin(); it != blockkeys.end(); ++it)
    {
        stream << blockid << ccy << it->first << "> \"" << data[it->first].getValStr() << "\"^^<http://www.w3.org/2001/XMLSchema#" << it->second << "> ." << std::endl;
    }

    UniValue tx(UniValue::VARR);
    for (size_t i=0; i<data["tx"].size(); i++) {
        UniValue txi(UniValue::VARR);
        UniValue txo(UniValue::VARR);
        UniValue script(UniValue::VARR);

        tx = data["tx"][i];

        std::string txid = ccyC + tx["txid"].getValStr() + "> ";
        if (withTypes)
            stream << txid << rdfs << "type> " << ccy << "Transaction> ." << std::endl;
        stream << blockid << ccy << "transaction> " << txid << " ." << std::endl;
        stream << txid << ccy << "time> \"" << DateTimeStrFormat("%Y-%m-%dT%H:%M:%SZ", std::stoll(data["time"].getValStr())) << "\"^^<http://www.w3.org/2001/XMLSchema#dateTime> ." << std::endl;
        if (tx["locktime"].getValStr() != "0")
            stream << txid << ccy << "locktime> \"" << tx["txid"].getValStr() << "\"^^<http://www.w3.org/2001/XMLSchema#integer> ." << std::endl; 

        if (i == 0) {
            txi = tx["vin"][0];
            std::string coinbasetxinput = ccyC + tx["txid"].getValStr() + "-0-0> ";
            if (withTypes)
                stream << coinbasetxinput << rdfs << "type> " << ccy << "TransactionInput> ." << std::endl;
            stream << txid << ccy << "input> " << coinbasetxinput << "." << std::endl;
            stream << coinbasetxinput << ccy + "coinbase> \"" << txi["coinbase"].getValStr() << "\"^^<http://www.w3.org/2001/XMLSchema#string> ." << std::endl;
            stream << coinbasetxinput << ccy + "sequence> \"" << txi["sequence"].getValStr() << "\"^^<http://www.w3.org/2001/XMLSchema#integer> ." << std::endl;

            txo = tx["vout"][0];
            script = txo["scriptPubKey"];
            std::string coinbasetxoutput = ccyC + tx["txid"].getValStr() + "-1-0> ";
            if (withTypes)
                stream << coinbasetxoutput << rdfs << "type> " << ccy << "TransactionOutput> ." << std::endl;
            stream << txid << ccy << "output> " << coinbasetxoutput << "." << std::endl;
            stream << coinbasetxoutput << ccy + "value> \"" << txo["value"].getValStr() << "\"^^<http://www.w3.org/2001/XMLSchema#decimal> ." << std::endl;
            stream << coinbasetxoutput << ccy + "n> \"" << txo["n"].getValStr() << "\"^^<http://www.w3.org/2001/XMLSchema#integer> ." << std::endl;
            stream << coinbasetxoutput << ccy + "pkasm> \"" << script["asm"].getValStr() << "\"^^<http://www.w3.org/2001/XMLSchema#string> ." << std::endl;
            stream << coinbasetxoutput << ccy + "type> \"" << script["type"].getValStr() << "\"^^<http://www.w3.org/2001/XMLSchema#string> ." << std::endl;
            if (script["type"].getValStr() != "nulldata" && script["type"].getValStr() != "nonstandard") {
                stream << coinbasetxoutput << ccy + "reqSigs> \"" << script["reqSigs"].getValStr() << "\"^^<http://www.w3.org/2001/XMLSchema#string> ." << std::endl;
                std::string coinbasetxoutputaddresses = ccy + "OA" + tx["txid"].getValStr() + strprintf("-0-0> ");
                for (size_t m=0; m<script["addresses"].size();m++) {
                    std::string coinbasetxoutputaddress = ccy + script["addresses"][m].getValStr() + "> ";
                    if (withTypes)
                        stream << coinbasetxoutputaddress << rdfs << "type> " << ccy << "Address> " << "." << std::endl;
                    stream << coinbasetxoutput << ccy << "address> " << coinbasetxoutputaddress << "." << std::endl;
                }
            }
        } else {

            // create TransactionInput
            for (size_t j=0;j<tx["vin"].size();j++) {
                txi = tx["vin"][j];
                std::string txinputid = ccyC + tx["txid"].getValStr() + "-0-" + std::to_string((int)j) + "> ";
                if (withTypes)
                    stream << txinputid << rdfs << "type> " << ccy << "TransactionInput> ." << std::endl;
                stream << txid << ccy << "input> " << txinputid << "." << std::endl;
                stream << txinputid << ccy + "txid> " << ccyC << txi["txid"].getValStr() << "> ." << std::endl;
                stream << txinputid << ccy + "nvout> \"" << txi["vout"].getValStr() << "\"^^<http://www.w3.org/2001/XMLSchema#integer> ." << std::endl;
                stream << txinputid << ccy + "ssasm> \"" << txi["scriptSig"]["asm"].getValStr() << "\"^^<http://www.w3.org/2001/XMLSchema#string> ." << std::endl;
                stream << txinputid << ccy + "sequence> \"" << txi["sequence"].getValStr() << "\"^^<http://www.w3.org/2001/XMLSchema#integer> ." << std::endl;
                if (!txi["txinwitness"].isNull()) {
                    for(size_t w=0; w<txi["txinwitness"].size();w++) {
                        std::string txinputwitness = ccy + txi["txinwitness"][w].getValStr() + "> ";
                        stream << txinputid << ccy << "witness> " << txinputwitness << " ." << std::endl;
                    }
                }
                // Mark the txins as spent
                std::string txspentid = ccyC + tx["txid"].getValStr() + "-1-" + txi["vout"].getValStr() + "> ";
                stream << txspentid << ccy << "spent> \"true\"^^<http://www.w3.org/2001/XMLSchema#boolean> ." << std::endl;
            }

            // create TransactionOutput
            for (size_t k=0;k<tx["vout"].size();k++) {
                txo = tx["vout"][k];
                script = txo["scriptPubKey"];
                std::string txoutputid = ccyC + tx["txid"].getValStr() + "-1-" + std::to_string((int)k) + "> ";
                if (withTypes)
                    stream << txoutputid << rdfs << "type> " << ccy << "TransactionOutput> ." << std::endl;
                stream << txid << ccy << "output> " << txoutputid << "." << std::endl;
                stream << txoutputid << ccy << "spent> \"false\"^^<http://www.w3.org/2001/XMLSchema#boolean> ." << std::endl;
                stream << txoutputid << ccy + "value> \"" << txo["value"].getValStr() << "\"^^<http://www.w3.org/2001/XMLSchema#decimal> ." << std::endl;
                stream << txoutputid << ccy + "n> \"" << txo["n"].getValStr() << "\"^^<http://www.w3.org/2001/XMLSchema#integer> ." << std::endl;
                stream << txoutputid << ccy + "pkasm> \"" << script["asm"].getValStr() << "\"^^<http://www.w3.org/2001/XMLSchema#string> ." << std::endl;
                stream << txoutputid << ccy + "type> \"" << script["type"].getValStr() << "\"^^<http://www.w3.org/2001/XMLSchema#string> ." << std::endl;

                // nonstandard, pubkey, pubkeyhash, scripthash, multisig, nulldata, 
                // witness_v0_keyhash, witness_v0_scripthash, witness_unknown"

                if (script["type"].getValStr() != "nulldata" && script["type"].getValStr() != "nonstandard") {
                    stream << txoutputid << ccy + "reqSigs> \"" << script["reqSigs"].getValStr() << "\"^^<http://www.w3.org/2001/XMLSchema#string> ." << std::endl;
                    for (size_t n=0; n<script["addresses"].size();n++) {
                        std::string txoutputaddress = ccy + script["addresses"][n].getValStr() + "> ";
                        if (withTypes)
                            stream << txoutputid << rdfs << "type> " << ccy << "Address> " << "." << std::endl;
                        stream << txoutputid << ccy << "address> " << txoutputaddress << "." << std::endl;
                    }
                }
            }
        }
    }
    return stream.str();
}

UniValue renderblock(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 1)
        throw std::runtime_error(
            "renderblock block\n"
            "\nReturns an RDF serialization of the block.\n"
            "\nArguments:\n"
            "1. block    (numeric) block number to render (default last block).\n"
            "\nResult:\n"
            "RDF graph serialized as N-triples\n"
            "\nExamples:\n"
            + HelpExampleCli("renderblock", "\"100\"")
            + HelpExampleRpc("renderblock", "\"100\"")
        );


    LOCK(cs_main);

    CBlock block;
    CBlockIndex* pblockindex = chainActive.Tip();
    int blocktorender = pblockindex->nHeight;

    if (!request.params[0].isNull()) {
        blocktorender = request.params[0].get_int();
        if (blocktorender < 0 || (blocktorender > 0 && blocktorender > pblockindex->nHeight)) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid block count: should be between 0 and the latest block height");
        }
        else {
            pblockindex = chainActive[blocktorender];
        }
    }

    if (fHavePruned && !(pblockindex->nStatus & BLOCK_HAVE_DATA) && pblockindex->nTx > 0)
        throw JSONRPCError(RPC_MISC_ERROR, "Block not available (pruned data)");

    if (!ReadBlockFromDisk(block, pblockindex, Params().GetConsensus()))
        // Block not found on disk. This could be because we have the block
        // header in our index but don't have the block (for example if a
        // non-whitelisted node sends us an unrequested long chain of valid
        // blocks, we add the headers to our index, but don't accept the
        // block).
        throw JSONRPCError(RPC_MISC_ERROR, "Block not found on disk");

    UniValue result(UniValue::VSTR);
    result.setStr(blockgraph(pblockindex));
    return result;
}

UniValue renderblockhash(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 1)
        throw std::runtime_error(
            "renderblockhash \"blockhash\""
            "\nReturns a string that is serialized, RDF-encoded data for block 'hash'.\n"
            "\nArguments:\n"
            "1. \"blockhash\"          (string, required) The block hash\n"
            "\nResult:\n"
            "RDF graph serialized as N-triples\n"
            "\nExamples:\n"
            + HelpExampleCli("renderblockhash", "\"e798f3ae4f57adcf25740fe43100d95ec4fd5d43a1568bc89e2b25df89ff6cb0\"")
            + HelpExampleRpc("renderblockhash", "\"e798f3ae4f57adcf25740fe43100d95ec4fd5d43a1568bc89e2b25df89ff6cb0\"")
        );

    LOCK(cs_main);

    std::string strHash = request.params[0].get_str();
    const uint256 hash(uint256S(strHash));

    if (mapBlockIndex.count(hash) == 0)
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");

    CBlock block;
    CBlockIndex* pblockindex = mapBlockIndex[hash];

    if (fHavePruned && !(pblockindex->nStatus & BLOCK_HAVE_DATA) && pblockindex->nTx > 0)
        throw JSONRPCError(RPC_MISC_ERROR, "Block not available (pruned data)");

    if (!ReadBlockFromDisk(block, pblockindex, Params().GetConsensus()))
        // Block not found on disk. This could be because we have the block
        // header in our index but don't have the block (for example if a
        // non-whitelisted node sends us an unrequested long chain of valid
        // blocks, we add the headers to our index, but don't accept the
        // block).
        throw JSONRPCError(RPC_MISC_ERROR, "Block not found on disk");

    UniValue result(UniValue::VSTR);
    result.setStr(blockgraph(pblockindex));
    return result;
}


UniValue dumptriples(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 3)
        throw std::runtime_error(
            "dumptriples filename startblock endblock\n"
            "\nCreates an RDF serialization of the blockchain in destination, which can be a directory or a path with filename.\n"
            "\nArguments:\n"
            "1. filename      (string) optional filename with path (either absolute or relative)\n"
            "2. startblock    (numeric) optional first block number to dump (default 0).\n"
            "3. endblock      (numeric) optional last block number to dump (default 4000000).\n");

    fs::path filepath = request.params[0].get_str();
    filepath = fs::absolute(filepath);

    int nEndBlock = 4000000;
    int nStartBlock = 0;

    if (request.params.size() > 1)
        nStartBlock = request.params[1].get_int();

    if (request.params.size() > 2) {
        nEndBlock = request.params[2].get_int();
    }

    try {

        boost::filesystem::path filepath = request.params[0].get_str();
        filepath = boost::filesystem::absolute(filepath);

        /* Prevent arbitrary files from being overwritten. There have been reports
         * that users have overwritten wallet files this way:
         * https://github.com/bitcoin/bitcoin/issues/9934
         * It may also avoid other security issues.
         */
        if (boost::filesystem::exists(filepath)) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, filepath.string() + " already exists. If you are sure this is what you want, move it out of the way first");
        }

        std::ofstream file;
        file.open(filepath.string().c_str());
        if (!file.is_open())
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Cannot open triples dump file");

        CBlockIndex* pblockindex = chainActive[nStartBlock];
        auto& consensus_params = Params().GetConsensus();

        std::map<std::string,std::string> blockkeys = {
            std::make_pair("difficulty", "decimal"),
            std::make_pair("height", "integer"),
            std::make_pair("mediantime", "integer"),
            std::make_pair("primechain", "string"),
            std::make_pair("primechainmultiplier", "integer"),
            std::make_pair("primeorigin", "integer"),
            std::make_pair("size", "integer"),
            std::make_pair("time", "integer"),
            std::make_pair("transition", "decimal"),
        };
        std::string chainid = "<http://purl.org/net/bel-epa/ccy#Cc74ed816-06ae-4b2a-b51a-3ac190810b1e> ";
        std::string rdfs = "<http://www.w3.org/1999/02/22-rdf-syntax-ns#";
        std::string ccy = "<http://purl.org/net/bel-epa/ccy#";
        std::string ccyC = ccy + "C";
        std::string doacc = "<http://purl.org/net/bel-epa/doacc#";

        // file << chainid << rdfs << "type> " << doacc << "chain> ." << std::endl;
        // file << chainid << rdfs << "type> " << ccy << "Blockchain> ." << std::endl;
        // file << chainid << rdfs << "type> " << rdfs << "Seq> ." << std::endl;

        while (pblockindex->nHeight <= nEndBlock)
        {
            CBlock block;
            ReadBlockFromDisk(block, pblockindex, consensus_params);
            UniValue data;
            {
                LOCK(cs_main);
                data = blockToJSON(block, pblockindex, true);
            }

            std::string blockid = "<http://purl.org/net/bel-epa/ccy#C" + data["hash"].getValStr() + "> ";

            // file << chainid << rdfs << "_" << data["height"].getValStr()  << "> " << blockid << "." << std::endl;
            file << blockid << rdfs << "type> " << ccy << "Block> ." << std::endl;
            file << blockid << ccy << "next> " << ccyC << data["nextblockhash"].getValStr() << "> ." << std::endl;
            if (pblockindex->nHeight > 0)
                file << blockid << ccy << "prev> " << ccyC << data["previousblockhash"].getValStr() << "> ." << std::endl;

            for (auto it = blockkeys.begin(); it != blockkeys.end(); ++it)
            {
                file << blockid << ccy << it->first << "> \"" << data[it->first].getValStr() << "\"^^<http://www.w3.org/2001/XMLSchema#" << it->second << "> ." << std::endl;
            }

            UniValue tx(UniValue::VARR);
            for (size_t i=0; i<data["tx"].size(); i++) {
                UniValue txi(UniValue::VARR);
                UniValue txo(UniValue::VARR);
                UniValue script(UniValue::VARR);

                tx = data["tx"][i];

                std::string txid = ccy + tx["txid"].getValStr() + "> ";
                file << txid << rdfs << "type> " << ccy << "Transaction> ." << std::endl;
                if (tx["locktime"].getValStr() != "0")
                    file << txid << ccy << "locktime> \"" << tx["txid"].getValStr() << "\" ."; 
                if (tx["data"].getValStr() != "")
                    file << txid << ccy << "data> \"" << tx["txid"].getValStr().length() << "\" ."; 
                // file << blockid << ccy << "hasTransaction> " << txid << "." << std::endl;

                // std::string vinset = ccy + "VI" + tx["txid"].getValStr() + "> ";
                // file << txid << ccy << "vin> " << vinset << "." << std::endl;
                // file << vinset << rdfs << "type> " << ccy << "VIn> ." << std::endl;

                // std::string voutset = ccy + "VO" + tx["txid"].getValStr() + "> ";
                // file << txid << ccy << "vout> " << voutset << "." << std::endl;
                // file << voutset << rdfs << "type>" << ccy << "VOut> ." << std::endl;

                if (i == 0) {
                    // file << vinset << rdfs << "_1> " << txid << "." << std::endl;
                    // file << voutset << rdfs << "_1> " << txid << "." << std::endl;
                    // create coinbase TransactionInput and TransactionOutput
                    txi = tx["vin"][0];
                    std::string coinbasetxinput = ccy + tx["txid"].getValStr() + "-I-0> ";
                    file << txid << ccy << "txin> " << coinbasetxinput << "." << std::endl;
                    file << coinbasetxinput << rdfs << "type> " << ccy << "TransactionInput> ." << std::endl;
                    file << coinbasetxinput << ccy + "coinbase> \"" << txi["coinbase"].getValStr() << "\"^^<http://www.w3.org/2001/XMLSchema#string> ." << std::endl;
                    file << coinbasetxinput << ccy + "sequence> \"" << txi["sequence"].getValStr() << "\"^^<http://www.w3.org/2001/XMLSchema#integer> ." << std::endl;

                    txo = tx["vout"][0];
                    script = txo["scriptPubKey"];
                    std::string coinbasetxoutput = ccy + tx["txid"].getValStr() + "-O-0> ";
                    file << txid << ccy << "txout> " << coinbasetxoutput << "." << std::endl;
                    file << coinbasetxoutput << rdfs << "type> " << ccy << "TransactionOutput> ." << std::endl;
                    file << coinbasetxoutput << ccy + "value> \"" << txo["value"].getValStr() << "\"^^<http://www.w3.org/2001/XMLSchema#decimal> ." << std::endl;
                    file << coinbasetxoutput << ccy + "n> \"" << txo["n"].getValStr() << "\"^^<http://www.w3.org/2001/XMLSchema#integer> ." << std::endl;
                    file << coinbasetxoutput << ccy + "asm> \"" << script["asm"].getValStr() << "\"^^<http://www.w3.org/2001/XMLSchema#string> ." << std::endl;
                    file << coinbasetxoutput << ccy + "type> \"" << script["type"].getValStr() << "\"^^<http://www.w3.org/2001/XMLSchema#string> ." << std::endl;
                    if (script["type"].getValStr() != "pubkey") {
                        file << coinbasetxoutput << ccy + "reqSigs> \"" << script["reqSigs"].getValStr() << "\"^^<http://www.w3.org/2001/XMLSchema#string> ." << std::endl;
                        std::string coinbasetxoutputaddresses = ccy + "OA" + tx["txid"].getValStr() + strprintf("-0-0> ");
                        file << coinbasetxoutput << ccy + "addresses> " << coinbasetxoutputaddresses << "." << std::endl;
                        file << coinbasetxoutputaddresses << rdfs << "type> " << rdfs << "Bag> ." << std::endl;
                        for (size_t m=0; m<script["addresses"].size();m++) {
                            std::string coinbasetxoutputaddress = ccy + script["addresses"][m].getValStr() + "> ";
                            file << coinbasetxoutputaddress << rdfs << "type> " << ccy << "Address> " << "." << std::endl;
                            file << coinbasetxoutputaddresses << rdfs << "_" << m+1 << "> " << coinbasetxoutputaddress << "." << std::endl;
                        }
                    }
                } else {

                    // create TransactionInput
                    for (size_t j=0;j<tx["vin"].size();j++) {
                        txi = tx["vin"][j];
                        std::string txinputid = ccy + tx["txid"].getValStr() + "-I-" + std::to_string((int)j) + "> ";
                        file << txinputid << rdfs << "type> " << ccy << "TransactionInput> ." << std::endl;
                        file << txid << ccy << "txin> " << txinputid << "." << std::endl;
                        // file << vinset << rdfs << "_" << j+1 << "> " << txinputid << "." << std::endl;
                        file << txinputid << ccy + "txid> " << ccy << txi["txid"].getValStr() << "> ." << std::endl;
                        file << txinputid << ccy + "nvout> \"" << txi["vout"].getValStr() << "\"^^<http://www.w3.org/2001/XMLSchema#integer> ." << std::endl;
                        file << txinputid << ccy + "scriptSig> \"" << txi["scriptSig"]["asm"].getValStr() << "\"^^<http://www.w3.org/2001/XMLSchema#string> ." << std::endl;
                        file << txinputid << ccy + "sequence> \"" << txi["sequence"].getValStr() << "\"^^<http://www.w3.org/2001/XMLSchema#integer> ." << std::endl;
                        if (!txi["txinwitness"].isNull()) {
                            std::string txinputwitnesses = ccy + "WI" + tx["txid"].getValStr() + "> ";
                            file << txinputwitnesses << rdfs + "type> " << ccy << "Witness> ." << std::endl;
                            file << txinputwitnesses << rdfs + "type> " << ccy << "Bag> ." << std::endl;
                            file << txinputid << ccy + "haswitnesses> " << ccy << txinputwitnesses << "." << std::endl;
                            for(size_t w=0; w<txi["txinwitness"].size();w++) {
                                file << txinputwitnesses << rdfs << "_" << w+1 << "> \"" << txi["txinwitness"][w].getValStr() << "\"^^<http://www.w3.org/2001/XMLSchema#string> ." << std::endl;
                            }
                        }
                    }

                    // create TransactionOutput
                    for (size_t k=0;k<tx["vout"].size();k++) {
                        txo = tx["vout"][k];
                        script = txo["scriptPubKey"];
                        std::string txoutputid = ccy + tx["txid"].getValStr() + "-O-" + std::to_string((int)k) + "> ";
                        file << txoutputid << rdfs << "type> " << ccy << "TransactionOutput> ." << std::endl;
                        file << txid << ccy << "txout> " << txoutputid << "." << std::endl;
                        // file << voutset << rdfs << "_" << k+1 << "> " << txoutputid << "." << std::endl;
                        file << txoutputid << ccy + "value> \"" << txo["value"].getValStr() << "\"^^<http://www.w3.org/2001/XMLSchema#decimal> ." << std::endl;
                        file << txoutputid << ccy + "n> \"" << txo["n"].getValStr() << "\"^^<http://www.w3.org/2001/XMLSchema#integer> ." << std::endl;
                        file << txoutputid << ccy + "asm> \"" << script["asm"].getValStr() << "\"^^<http://www.w3.org/2001/XMLSchema#string> ." << std::endl;
                        file << txoutputid << ccy + "type> \"" << script["type"].getValStr() << "\"^^<http://www.w3.org/2001/XMLSchema#string> ." << std::endl;
                        if (script["type"].getValStr() != "pubkey") {
                            file << txoutputid << ccy + "reqSigs> \"" << script["reqSigs"].getValStr() << "\"^^<http://www.w3.org/2001/XMLSchema#string> ." << std::endl;
                            std::string txoutputidaddresses = ccy + "OA" + tx["txid"].getValStr() + strprintf("-%u-%u> ", i, k);
                            file << txoutputid << ccy << "addresses> " << txoutputidaddresses << "." << std::endl;
                            file << txoutputidaddresses << rdfs << "type> " << rdfs << "Bag> ." << std::endl;
                            for (size_t n=0; n<script["addresses"].size();n++) {
                                std::string txoutputaddress = ccy + script["addresses"][n].getValStr() + "> ";
                                file << txoutputaddress << rdfs << "type> " << ccy << "Address>" << "." << std::endl;
                                file << txoutputidaddresses << rdfs << "_" << n+1 << "> " << txoutputaddress << "." << std::endl;
                            }
                        }
                    }
                }
            }
            pblockindex = chainActive.Next(pblockindex);
        }

        file.close();

    } catch(const boost::filesystem::filesystem_error &e) {
        throw JSONRPCError(-1, "Error: Triples dump failed!");
    }

    UniValue reply(UniValue::VOBJ);
    reply.pushKV("filename", filepath.string());

    return reply;
}

static const CRPCCommand commands[] =
{ //  category              name                      actor (function)         argNames
  //  --------------------- ------------------------  -----------------------  ----------
    { "blockchain",         "getblockchaininfo",      &getblockchaininfo,      {} },
    { "blockchain",         "getchaintxstats",        &getchaintxstats,        {"nblocks", "blockhash"} },
    { "blockchain",         "getbestblockhash",       &getbestblockhash,       {} },
    { "blockchain",         "getblockcount",          &getblockcount,          {} },
    { "blockchain",         "getblock",               &getblock,               {"blockhash","verbosity|verbose"} },
    { "blockchain",         "getblockhash",           &getblockhash,           {"height"} },
    { "blockchain",         "getblockheader",         &getblockheader,         {"blockhash","verbose"} },
    { "blockchain",         "getchaintips",           &getchaintips,           {} },
    { "blockchain",         "getdifficulty",          &getdifficulty,          {} },
    { "blockchain",         "getmempoolancestors",    &getmempoolancestors,    {"txid","verbose"} },
    { "blockchain",         "getmempooldescendants",  &getmempooldescendants,  {"txid","verbose"} },
    { "blockchain",         "getmempoolentry",        &getmempoolentry,        {"txid"} },
    { "blockchain",         "getmempoolinfo",         &getmempoolinfo,         {} },
    { "blockchain",         "getrawmempool",          &getrawmempool,          {"verbose"} },
    { "blockchain",         "gettxout",               &gettxout,               {"txid","n","include_mempool"} },
    { "blockchain",         "gettxoutsetinfo",        &gettxoutsetinfo,        {} },
    { "blockchain",         "pruneblockchain",        &pruneblockchain,        {"height"} },
    { "blockchain",         "savemempool",            &savemempool,            {} },
    { "blockchain",         "verifychain",            &verifychain,            {"checklevel","nblocks"} },

    { "blockchain",         "listprimerecords",       &listprimerecords,       {"primechain_length","primechain_type"} },
    { "blockchain",         "listtopprimes",          &listtopprimes,          {"primechain_length","primechain_type"} },
    
    { "blockchain",         "preciousblock",          &preciousblock,          {"blockhash"} },

    /* Not shown in help */
    { "hidden",             "invalidateblock",        &invalidateblock,        {"blockhash"} },
    { "hidden",             "reconsiderblock",        &reconsiderblock,        {"blockhash"} },
    { "hidden",             "waitfornewblock",        &waitfornewblock,        {"timeout"} },
    { "hidden",             "waitforblock",           &waitforblock,           {"blockhash","timeout"} },
    { "hidden",             "waitforblockheight",     &waitforblockheight,     {"height","timeout"} },
    { "hidden",             "syncwithvalidationinterfacequeue", &syncwithvalidationinterfacequeue, {} },
    { "hidden",             "dumptriples",            &dumptriples,            {"filename", "start", "end"} },
    { "hidden",             "renderblock",            &renderblock,            {"block"} },
    { "hidden",             "renderblockhash",        &renderblockhash,        {"blockhash"} },
};

void RegisterBlockchainRPCCommands(CRPCTable &t)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        t.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
