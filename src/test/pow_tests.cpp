// Copyright (c) 2015-2026 The Bitcoin Core developers
// Copyright (c) 2026 The Kvanta5 Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chain.h>
#include <chainparams.h>
#include <pow.h>
#include <test/util/random.h>
#include <test/util/setup_common.h>
#include <util/chaintype.h>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(pow_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(CheckProofOfWork_test_negative_target)
{
    const auto consensus = CreateChainParams(*m_node.args, ChainType::MAIN)->GetConsensus();

    uint256 hash{1};
    const unsigned int nBits = UintToArith256(consensus.powLimit).GetCompact(true);

    BOOST_CHECK(!CheckProofOfWork(hash, nBits, consensus));
}

BOOST_AUTO_TEST_CASE(CheckProofOfWork_test_overflow_target)
{
    const auto consensus = CreateChainParams(*m_node.args, ChainType::MAIN)->GetConsensus();

    uint256 hash{1};
    const unsigned int nBits{~0x00800000U};

    BOOST_CHECK(!CheckProofOfWork(hash, nBits, consensus));
}

BOOST_AUTO_TEST_CASE(CheckProofOfWork_test_too_easy_target)
{
    const auto consensus = CreateChainParams(*m_node.args, ChainType::MAIN)->GetConsensus();

    arith_uint256 target = UintToArith256(consensus.powLimit);
    target *= 2;

    const unsigned int nBits = target.GetCompact();
    const uint256 hash{1};

    BOOST_CHECK(!CheckProofOfWork(hash, nBits, consensus));
}

BOOST_AUTO_TEST_CASE(CheckProofOfWork_test_bigger_hash_than_target)
{
    const auto consensus = CreateChainParams(*m_node.args, ChainType::MAIN)->GetConsensus();

    arith_uint256 target = UintToArith256(consensus.powLimit);
    const unsigned int nBits = target.GetCompact();

    target *= 2;
    const uint256 hash = ArithToUint256(target);

    BOOST_CHECK(!CheckProofOfWork(hash, nBits, consensus));
}

BOOST_AUTO_TEST_CASE(CheckProofOfWork_test_zero_target)
{
    const auto consensus = CreateChainParams(*m_node.args, ChainType::MAIN)->GetConsensus();

    const arith_uint256 zero{0};
    const unsigned int nBits = zero.GetCompact();
    const uint256 hash = ArithToUint256(zero);

    BOOST_CHECK(!CheckProofOfWork(hash, nBits, consensus));
}

BOOST_AUTO_TEST_CASE(GetBlockProofEquivalentTime_test)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    const auto& consensus = chainParams->GetConsensus();

    std::vector<CBlockIndex> blocks(10000);

    const int64_t start_time = chainParams->GenesisBlock().GetBlockTime() + consensus.nPowTargetSpacing;

    for (int i = 0; i < 10000; ++i) {
        blocks[i].pprev = i ? &blocks[i - 1] : nullptr;
        blocks[i].nHeight = i;
        blocks[i].nTime = start_time + i * consensus.nPowTargetSpacing;

        // Easy regtest-style target used only for equivalent-time arithmetic.
        blocks[i].nBits = 0x207fffff;

        blocks[i].nChainWork = i ? blocks[i - 1].nChainWork + GetBlockProof(blocks[i - 1]) : arith_uint256{0};
    }

    for (int j = 0; j < 1000; ++j) {
        CBlockIndex* p1 = &blocks[m_rng.randrange(10000)];
        CBlockIndex* p2 = &blocks[m_rng.randrange(10000)];
        CBlockIndex* p3 = &blocks[m_rng.randrange(10000)];

        const int64_t tdiff = GetBlockProofEquivalentTime(*p1, *p2, *p3, consensus);
        BOOST_CHECK_EQUAL(tdiff, p1->GetBlockTime() - p2->GetBlockTime());
    }
}

void sanity_check_chainparams(const ArgsManager& args, ChainType chain_type)
{
    const auto chainParams = CreateChainParams(args, chain_type);
    const auto& consensus = chainParams->GetConsensus();

    // Genesis hash must match chainparams consensus.
    BOOST_CHECK_EQUAL(consensus.hashGenesisBlock, chainParams->GenesisBlock().GetHash());

    // Target spacing must be sane.
    BOOST_CHECK_GT(consensus.nPowTargetSpacing, 0);

    // Target timespan must be sane and divisible by spacing.
    BOOST_CHECK_GT(consensus.nPowTargetTimespan, 0);
    BOOST_CHECK_EQUAL(consensus.nPowTargetTimespan % consensus.nPowTargetSpacing, 0);

    // powLimit must be nonzero and representable.
    BOOST_CHECK(UintToArith256(consensus.powLimit) > 0);

    // Genesis nBits must be positive, non-overflowing, and no easier than powLimit.
    arith_uint256 genesis_target;
    bool neg{false};
    bool over{false};

    genesis_target.SetCompact(chainParams->GenesisBlock().nBits, &neg, &over);

    BOOST_CHECK(!neg);
    BOOST_CHECK(!over);
    BOOST_CHECK(genesis_target > 0);
    BOOST_CHECK(UintToArith256(consensus.powLimit) >= genesis_target);
}

BOOST_AUTO_TEST_CASE(ChainParams_MAIN_sanity)
{
    sanity_check_chainparams(*m_node.args, ChainType::MAIN);
}

BOOST_AUTO_TEST_CASE(ChainParams_TESTNET5_sanity)
{
    sanity_check_chainparams(*m_node.args, ChainType::TESTNET5);
}

BOOST_AUTO_TEST_CASE(ChainParams_REGTEST_sanity)
{
    sanity_check_chainparams(*m_node.args, ChainType::REGTEST);
}

BOOST_AUTO_TEST_CASE(ChainParams_SIGNET_sanity)
{
    sanity_check_chainparams(*m_node.args, ChainType::SIGNET);
}

BOOST_AUTO_TEST_SUITE_END()
