// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2017 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>
#include <consensus/merkle.h>
#include <consensus/validation.h>
#include <validation.h>
#include <pow.h>
#include <prime/prime.h>

#include <tinyformat.h>
#include <util.h>
#include <utilstrencodings.h>

#include <assert.h>
#include <memory>

#include <chainparamsseeds.h>

static CBlock CreateGenesisBlock(const char* pszStartTopic, const CScript& genesisOutputScript, uint32_t nTime, uint32_t nNonce, uint32_t nBits, int32_t nVersion, CBigNum bnPrimeChainMultiplier, const CAmount& genesisReward)
{
    CMutableTransaction txNew;
    txNew.nVersion = 1;
    txNew.vin.resize(1);
    txNew.vout.resize(1);
    txNew.vin[0].scriptSig = CScript() << 0 << CScriptNum(999) << std::vector<unsigned char>((const unsigned char*)pszStartTopic, (const unsigned char*)pszStartTopic + strlen(pszStartTopic));
    txNew.vout[0].nValue = genesisReward;
    txNew.vout[0].scriptPubKey = genesisOutputScript;

    CBlock genesis;
    genesis.nTime    = nTime;
    genesis.nBits    = nBits;
    genesis.nNonce   = nNonce;
    genesis.nVersion = nVersion;
	genesis.bnPrimeChainMultiplier = bnPrimeChainMultiplier;
    genesis.vtx.push_back(MakeTransactionRef(std::move(txNew)));
    genesis.hashPrevBlock.SetNull();
    genesis.hashMerkleRoot = BlockMerkleRoot(genesis);

    return genesis;
}

/**
 * Build the genesis block. Note that the output of its generation
 * transaction cannot be spent since it did not originally exist in the
 * database.
 *
 * CBlock(hash=000000000019d6, ver=1, hashPrevBlock=00000000000000, hashMerkleRoot=4a5e1e, nTime=1231006505, nBits=1d00ffff, nNonce=2083236893, vtx=1)
 *   CTransaction(hash=4a5e1e, ver=1, vin.size=1, vout.size=1, nLockTime=0)
 *     CTxIn(COutPoint(000000, -1), coinbase 04ffff001d0104455468652054696d65732030332f4a616e2f32303039204368616e63656c6c6f72206f6e206272696e6b206f66207365636f6e64206261696c6f757420666f722062616e6b73)
 *     CTxOut(nValue=50.00000000, scriptPubKey=0x5F1DF16B2B704C8A578D0B)
 *   vMerkleTree: 4a5e1e
 */
static CBlock CreateGenesisBlock(uint32_t nTime, uint32_t nNonce, uint32_t nBits, int32_t nVersion, CBigNum bnPrimeChainMultiplier, const CAmount& genesisReward)
{
    const char* pszStartTopic = "https://bitcointalk.org/index.php?topic=325735.0";
    const CScript genesisOutputScript = CScript();
    return CreateGenesisBlock(pszStartTopic, genesisOutputScript, nTime, nNonce, nBits, nVersion, bnPrimeChainMultiplier, genesisReward);
}

void CChainParams::UpdateVersionBitsParameters(Consensus::DeploymentPos d, int64_t nStartTime, int64_t nTimeout)
{
    consensus.vDeployments[d].nStartTime = nStartTime;
    consensus.vDeployments[d].nTimeout = nTimeout;
}

/**
 * Main network
 */
/**
 * What makes a good checkpoint block?
 * + Is surrounded by blocks with reasonable timestamps
 *   (no blocks before with a timestamp after, none after with
 *    timestamp before)
 * + Contains no strange transactions
 */

class CMainParams : public CChainParams {
public:
    CMainParams() {
        strNetworkID = "main";
        consensus.nSubsidyHalvingInterval = 210000;
        consensus.BIP16Height = 0; // always enforce P2SH BIP16
        consensus.BIP34Height = 950;
        consensus.BIP34Hash = uint256S("0x22596accbbde801463d46b802343c915010bcadf1c098119a252a0f17664b466");
        consensus.BIP65Height = -1;
        consensus.BIP66Height = -1;
        consensus.powLimit = uint256S("00000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.nPowTargetTimespan = 7 * 24 * 60 * 60; // a weeks
        consensus.nPowTargetSpacing = 60;
        consensus.fPowAllowMinDifficultyBlocks = false;
        consensus.fPowNoRetargeting = false;
        consensus.nRuleChangeActivationThreshold = 1026; // 95% of 1080
        consensus.nMinerConfirmationWindow = 1080; // nPowTargetTimespan / nPowTargetSpacing
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 1199145601; // January 1, 2008
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = 1230767999; // December 31, 2008

        // Deployment of BIP68, BIP112, and BIP113.
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].bit = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nStartTime = 1462060800; // May 1st, 2016
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nTimeout = 1493596800; // May 1st, 2017

        // Deployment of SegWit (BIP141, BIP143, and BIP147)
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].bit = 1;
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].nStartTime = 999999999999ULL; //DATACOIN SEGWIT отключаем SEGWIT = 1479168000; // November 15th, 2016.
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].nTimeout = 999999999999ULL; //DATACOIN SEGWIT отключаем SEGWIT =1510704000; // November 15th, 2017.

        // The best chain should have at least this much work.
        //DATACOIN CHANGED Минимальная работа активной цепи.
        //Загрузка блоков не начнется пока заголовки не достигнут этой работы
        consensus.nMinimumChainWork = uint256S("0x000000000000000000000000000000000000000000000000000031d4a178b250");

        // By default assume that the signatures in ancestors of this block are valid.
        consensus.defaultAssumeValid = uint256S("0xb7183015dc5e4f2e1be353329e7fd9c0eb32efef412e0a0c5c970a9aacde8d8d"); //3128684

        consensus.nTargetInitialLength = 7; // primecoin: initial prime chain target
        consensus.nTargetMinLength = 6;     // primecoin: minimum prime chain target

        /**
         * The message start string is designed to be unlikely to occur in normal data.
         * The characters are rarely used upper ASCII, not valid as UTF-8, and produce
         * a large 32-bit integer with any alignment.
         */
        pchMessageStart[0] = 0xda;
        pchMessageStart[1] = 0xdc;
        pchMessageStart[2] = 0xdd;
        pchMessageStart[3] = 0xed;
        nDefaultPort = 4777;
        nPruneAfterHeight = 100000;

        genesis = CreateGenesisBlock(1384627170, 49030125, TargetFromInt(6), 2, (uint64_t) 5651310, COIN);
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256S("0x1d724e874ee9ea571563239bde095911f128db47c7612fb1968c08c9f95cabe8"));
        assert(genesis.hashMerkleRoot == uint256S("0xfe5d7082c24c53362f6b82211913d536677aaffafde0dcec6ff7b348ff6265f8"));

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,30);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,90);
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,128+30);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x88, 0xB2, 0x1E};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x88, 0xAD, 0xE4};

        bech32_hrp = "bc";

        vFixedSeeds = std::vector<SeedSpec6>(pnSeed6_main, pnSeed6_main + ARRAYLEN(pnSeed6_main));

        fDefaultConsistencyChecks = false;
        fRequireStandard = true;
        fMineBlocksOnDemand = false;

	//DATACOIN CHANGED Добавить чекпоинты
        checkpointData = {
            {
                {   72204, uint256S("0x661b85bab200d0b1f72c6909c5b2602af8227459ae72b7afbff75d16c8e2b703")},
                { 2000000, uint256S("0x4e49f85b69f68d6f58b2b18473c4bf17d88e6dd7c79d7d416e22522da17bc91a")},
                { 3128684, uint256S("0xb7183015dc5e4f2e1be353329e7fd9c0eb32efef412e0a0c5c970a9aacde8d8d")},
            }
        };

        chainTxData = ChainTxData{
            // Data as of block b7183015dc5e4f2e1be353329e7fd9c0eb32efef412e0a0c5c970a9aacde8d8d (height 3128684).
            1569522270, // * UNIX timestamp of last known number of transactions
            3538876,    // * total number of transactions between genesis and that timestamp
                        //   (the tx=... number in the SetBestChain debug.log lines)
            2140497552, // * total data size
            0.020,      // * estimated number of transactions per second after that timestamp
	    0.083       // * estimated data rate (bytes per sec)
        };
    }
};

/**
 * Testnet (v3)
 */
class CTestNetParams : public CChainParams {
public:
    CTestNetParams() {
        strNetworkID = "test";
        consensus.nSubsidyHalvingInterval = 210000;
        consensus.BIP16Height = 0; // always enforce P2SH BIP16
        consensus.BIP34Height = 750;
        consensus.BIP34Hash = uint256S("0x8af7eb332ff63e1ff919043fbe87c9cfa2a168903e88c1e34850151a3aed2be0");
        consensus.BIP65Height = -1;
        consensus.BIP66Height = -1;
        consensus.powLimit = uint256S("00000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.nPowTargetTimespan = 7 * 24 * 60 * 60; // a weeks
        consensus.nPowTargetSpacing = 60;
        consensus.fPowAllowMinDifficultyBlocks = true;
        consensus.fPowNoRetargeting = false;
        consensus.nRuleChangeActivationThreshold = 810; // 75% for testchains
        consensus.nMinerConfirmationWindow = 1080; // nPowTargetTimespan / nPowTargetSpacing
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 1199145601; // January 1, 2008
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = 1230767999; // December 31, 2008

        // Deployment of BIP68, BIP112, and BIP113.
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].bit = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nStartTime = 1456790400; // March 1st, 2016
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nTimeout = 1493596800; // May 1st, 2017

        // Deployment of SegWit (BIP141, BIP143, and BIP147)
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].bit = 1;
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].nStartTime = 999999999999ULL; //DATACOIN SEGWIT отключаем SEGWIT = 1462060800; // May 1st 2016
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].nTimeout = 999999999999ULL; //DATACOIN SEGWIT отключаем SEGWIT = 1493596800; // May 1st 2017

        // The best chain should have at least this much work.
        consensus.nMinimumChainWork = uint256S("0x0000000000000000000000000000000000000000000000000001beed3c6966e0");

        // By default assume that the signatures in ancestors of this block are valid.
        consensus.defaultAssumeValid = uint256S("0xe0fd3ea6ef46adabd8f4a58d8b957e9909cd1006eaf23712761f8e72d595c676"); //442608

	consensus.nTargetInitialLength = 4; // primecoin: initial prime chain target
        consensus.nTargetMinLength = 2;     // primecoin: minimum prime chain target

        pchMessageStart[0] = 0xdb;
        pchMessageStart[1] = 0xde;
        pchMessageStart[2] = 0xdb;
        pchMessageStart[3] = 0xd3;
        nDefaultPort = 4776;
        nPruneAfterHeight = 1000;

        genesis = CreateGenesisBlock(1385686192, 46032, TargetFromInt(4), 2, (uint64_t) 211890, COIN);
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256S("0x26ee5563233ed8cbdd8af5f16bc55b73d9d8cc727392d507292ca959fd08c03f"));
        assert(genesis.hashMerkleRoot == uint256S("0xfe5d7082c24c53362f6b82211913d536677aaffafde0dcec6ff7b348ff6265f8"));

        vFixedSeeds.clear();
        vSeeds.clear();

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,70);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,132);
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,128+70);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};

        bech32_hrp = "tb";

        vFixedSeeds = std::vector<SeedSpec6>(pnSeed6_test, pnSeed6_test + ARRAYLEN(pnSeed6_test));

        fDefaultConsistencyChecks = false;
        fRequireStandard = false;
        fMineBlocksOnDemand = false;


        checkpointData = {
            {
                {442608, uint256S("e0fd3ea6ef46adabd8f4a58d8b957e9909cd1006eaf23712761f8e72d595c676")},
            }
        };

        chainTxData = ChainTxData{
            // Data as of block e0fd3ea6ef46adabd8f4a58d8b957e9909cd1006eaf23712761f8e72d595c676 (height 442608)
            1569518411,
            444925,
            246583384,
            0.020,
            0.0001
        };

    }
};

/**
 * Regression test
 */
class CRegTestParams : public CChainParams {
public:
    CRegTestParams() {
        strNetworkID = "regtest";
        consensus.nSubsidyHalvingInterval = 150;
        consensus.BIP16Height = 0; // always enforce P2SH BIP16 on regtest
        consensus.BIP34Height = 100000000; // BIP34 has not activated on regtest (far in the future so block v1 are not rejected in tests)
        consensus.BIP34Hash = uint256();
        consensus.BIP65Height = 1351; // BIP65 activated on regtest (Used in rpc activation tests)
        //DATACOIN CHANGED DTC COINBASE_MATURITY=3200. Для тестов трат приходится создавать длинную цепочку.
        //Но тест биткоина рассчитан на высоту (100) менее BIP66Height.
        //Поэтому увеличим BIP66Height чтобы было > 3200
        consensus.BIP66Height = 12510; // BIP66 activated on regtest (Used in rpc activation tests)
        consensus.powLimit = uint256S("7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.nPowTargetTimespan = 7 * 24 * 60 * 60; // a weeks
        consensus.nPowTargetSpacing = 60;
        consensus.fPowAllowMinDifficultyBlocks = true;
        consensus.fPowNoRetargeting = true;
        consensus.nRuleChangeActivationThreshold = 60; // 75% for testchains
        consensus.nMinerConfirmationWindow = 80; // Faster than normal for regtest (80 instead of 1080)
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = 999999999999ULL;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].bit = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nTimeout = 999999999999ULL;
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].bit = 1;
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].nStartTime = 999999999999ULL; //DATACOIN SEGWIT отключаем SEGWIT = 0ULL; // отключил сегвит на время тестов. потом нужно включить обратно
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].nTimeout = 999999999999ULL; //DATACOIN SEGWIT отключаем SEGWIT = 999999999999ULL;

        // The best chain should have at least this much work.
        consensus.nMinimumChainWork = uint256S("0x00");

        // By default assume that the signatures in ancestors of this block are valid.
        consensus.defaultAssumeValid = uint256S("0x00");

	consensus.nTargetInitialLength = 1; // primecoin: initial prime chain target
        consensus.nTargetMinLength = 1;     // primecoin: minimum prime chain target

        pchMessageStart[0] = 0xfa;
        pchMessageStart[1] = 0xbf;
        pchMessageStart[2] = 0xb5;
        pchMessageStart[3] = 0xda;
        nDefaultPort = 18444;
        nPruneAfterHeight = 1000;


        genesis = CreateGenesisBlock(1385686192, 46032, TargetFromInt(1), 2, (uint64_t) 211890, COIN); //DATACOIN CHANGED переделать под nTargetMinLength=2, nTargetInitialLength=4
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256S("0x3864a16a5e7c9f79f2ab2ebc41e943f342f6737b83649844f6b41334eb7e5ba8"));
        assert(genesis.hashMerkleRoot == uint256S("0xfe5d7082c24c53362f6b82211913d536677aaffafde0dcec6ff7b348ff6265f8"));

        vFixedSeeds.clear(); //!< Regtest mode doesn't have any fixed seeds.
        vSeeds.clear();      //!< Regtest mode doesn't have any DNS seeds.

        fDefaultConsistencyChecks = true;
        fRequireStandard = false;
        fMineBlocksOnDemand = true;

        checkpointData = {
            {
                {0, consensus.hashGenesisBlock},
            }
        };

        chainTxData = ChainTxData{
            0,
            0,
            0
        };

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,70);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,132);
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,128+70);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};

        bech32_hrp = "bcrt";
    }
};

static std::unique_ptr<CChainParams> globalChainParams;

const CChainParams &Params() {
    assert(globalChainParams);
    return *globalChainParams;
}

std::unique_ptr<CChainParams> CreateChainParams(const std::string& chain)
{
    if (chain == CBaseChainParams::MAIN)
        return std::unique_ptr<CChainParams>(new CMainParams());
    else if (chain == CBaseChainParams::TESTNET)
        return std::unique_ptr<CChainParams>(new CTestNetParams());
    else if (chain == CBaseChainParams::REGTEST)
        return std::unique_ptr<CChainParams>(new CRegTestParams());
    throw std::runtime_error(strprintf("%s: Unknown chain %s.", __func__, chain));
}

void SelectParams(const std::string& network)
{
    SelectBaseParams(network);
    globalChainParams = CreateChainParams(network);

    //DATACOIN CHANGED Биткоин тут ничего не проверяет. 
    //Вставка этой проверки дала нежелательную круговую зависимость библиотеки common от server
    //CValidationState state;
    //unsigned int tmpnChainType = 0, tmpnChainLength = 0;
    //assert(CheckBlock(Params().GenesisBlock(), state, Params().GetConsensus(), true, true));
    //assert(CheckProofOfWork(Params().GenesisBlock().GetHeaderHash(), Params().GenesisBlock().nBits,
    //    Params().GetConsensus(), Params().GenesisBlock().bnPrimeChainMultiplier,
    //    tmpnChainType, tmpnChainLength));

}

void UpdateVersionBitsParameters(Consensus::DeploymentPos d, int64_t nStartTime, int64_t nTimeout)
{
    globalChainParams->UpdateVersionBitsParameters(d, nStartTime, nTimeout);
}
