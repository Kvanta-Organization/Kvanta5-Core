// Copyright (c) 2015-2026 The Bitcoin Core developers
// Copyright (c) 2026 The Kvanta5 Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <interfaces/mining.h>
#include <node/miner.h>
#include <script/script.h>
#include <script/solver.h>
#include <test/util/setup_common.h>
#include <txmempool.h>
#include <util/strencodings.h>
#include <validation.h>

#include <boost/test/unit_test.hpp>

#include <memory>
#include <vector>

using interfaces::BlockTemplate;
using interfaces::Mining;
using node::BlockAssembler;

namespace miner_tests {

struct MinerTestingSetup : public TestChain100Setup {
    std::unique_ptr<Mining> MakeMining()
    {
        return interfaces::MakeMining(m_node);
    }
};

static CScript Kvanta5TestP2QRScript(uint8_t tag = 0xC5)
{
    std::vector<unsigned char> program(32);
    for (size_t i = 0; i < program.size(); ++i) {
        program[i] = static_cast<unsigned char>(tag + i);
    }
    return CScript() << OP_RESERVED << program;
}

static bool IsKvanta5NativeP2QR(const CScript& script_pub_key)
{
    std::vector<std::vector<unsigned char>> solutions;
    const TxoutType type{Solver(script_pub_key, solutions)};
    return type == TxoutType::KVANTA5_P2QR
        && solutions.size() == 1
        && solutions[0].size() == 32;
}

static bool IsKvanta5AllowedMinerOutput(const CScript& script_pub_key)
{
    std::vector<std::vector<unsigned char>> solutions;
    const TxoutType type{Solver(script_pub_key, solutions)};

    // Kvanta5 miner-created outputs must stay in the QR-safe output universe.
    // Native P2QR is the normal miner payout. Wrapped P2SH-P2QR may exist for
    // compatibility/mining infrastructure.
    return type == TxoutType::KVANTA5_P2QR || type == TxoutType::SCRIPTHASH;
}

} // namespace miner_tests

BOOST_FIXTURE_TEST_SUITE(miner_tests, miner_tests::MinerTestingSetup)

BOOST_AUTO_TEST_CASE(CreateNewBlock_validity)
{
    auto mining{MakeMining()};
    BOOST_REQUIRE(mining);

    // TestChain100Setup gives this test a valid Kvanta5 chain. Do not import
    // Bitcoin's old BLOCKINFO fixture or mutate coinbase scripts/nonces.
    BOOST_REQUIRE_GE(m_coinbase_txns.size(), 4U);

    const CScript miner_script{miner_tests::Kvanta5TestP2QRScript(0xC5)};
    BOOST_REQUIRE(miner_tests::IsKvanta5NativeP2QR(miner_script));

    BlockAssembler::Options options;
    options.coinbase_output_script = miner_script;

    std::unique_ptr<BlockTemplate> block_template{mining->createNewBlock(options)};
    BOOST_REQUIRE(block_template);

    CBlock block{block_template->getBlock()};
    BOOST_REQUIRE(!block.vtx.empty());
    BOOST_REQUIRE(block.vtx[0]->IsCoinBase());
    BOOST_REQUIRE(!block.vtx[0]->vout.empty());

    bool found_miner_payout{false};
    for (const CTxOut& out : block.vtx[0]->vout) {
        BOOST_CHECK_MESSAGE(
            miner_tests::IsKvanta5AllowedMinerOutput(out.scriptPubKey),
            "coinbase created forbidden output script: " + HexStr(out.scriptPubKey));

        if (out.scriptPubKey == miner_script) {
            found_miner_payout = true;
        }
    }

    BOOST_CHECK_MESSAGE(found_miner_payout, "coinbase did not contain requested native P2QR miner payout");

    // A second template should build cleanly on the same valid Kvanta5 chain.
    // This catches broken template state without relying on Bitcoin legacy
    // transaction fixtures.
    std::unique_ptr<BlockTemplate> next_template{mining->createNewBlock(options)};
    BOOST_REQUIRE(next_template);

    CBlock next_block{next_template->getBlock()};
    BOOST_REQUIRE(!next_block.vtx.empty());
    BOOST_REQUIRE(next_block.vtx[0]->IsCoinBase());
    BOOST_REQUIRE(!next_block.vtx[0]->vout.empty());

    for (const CTxOut& out : next_block.vtx[0]->vout) {
        BOOST_CHECK_MESSAGE(
            miner_tests::IsKvanta5AllowedMinerOutput(out.scriptPubKey),
            "next coinbase created forbidden output script: " + HexStr(out.scriptPubKey));
    }
}

BOOST_AUTO_TEST_SUITE_END()
