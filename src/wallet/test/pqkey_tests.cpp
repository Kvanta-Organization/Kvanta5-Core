// Copyright (c) 2026 The Kvanta5 Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include <wallet/pqkey.h>

#include <addresstype.h>
#include <key_io.h>
#include <script/solver.h>
#include <test/util/setup_common.h>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(pqkey_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(cpqkey_seed_key_address_sign_verify)
{
    CPQKey key;
    BOOST_CHECK(key.MakeNewKey());
    BOOST_CHECK(key.IsValid());
    BOOST_CHECK(key.HasSeed());

    BOOST_CHECK_EQUAL(key.GetSeedBytes().size(), MLDSA87_SEED_SIZE);
    BOOST_CHECK_EQUAL(key.GetPubKeyBytes().size(), MLDSA87_PUBLIC_KEY_SIZE);
    BOOST_CHECK_EQUAL(key.GetSecretKeyBytes().size(), MLDSA87_SECRET_KEY_SIZE);

    CPQPubKey pubkey = key.GetPubKey();
    BOOST_CHECK(pubkey.IsValid());
    BOOST_CHECK_EQUAL(pubkey.Raw().size(), MLDSA87_PUBLIC_KEY_SIZE);

    const Kvanta5P2QRDestination dest = key.GetDestination();
    BOOST_CHECK(IsValidDestination(dest));

    const CScript script = GetScriptForDestination(dest);
    BOOST_CHECK_EQUAL(script.size(), 2 + KVANTA5_P2QR_PROGRAM_SIZE);
    BOOST_CHECK_EQUAL(script[0], OP_KVANTA5_P2QR);
    BOOST_CHECK_EQUAL(script[1], KVANTA5_P2QR_PROGRAM_SIZE);

    std::vector<std::vector<unsigned char>> solutions;
    BOOST_CHECK(Solver(script, solutions) == TxoutType::KVANTA5_P2QR);
    BOOST_REQUIRE_EQUAL(solutions.size(), 1);
    BOOST_CHECK_EQUAL(solutions[0].size(), KVANTA5_P2QR_PROGRAM_SIZE);

    CTxDestination extracted;
    BOOST_CHECK(ExtractDestination(script, extracted));
    BOOST_CHECK(std::holds_alternative<Kvanta5P2QRDestination>(extracted));
    BOOST_CHECK(std::get<Kvanta5P2QRDestination>(extracted) == dest);

    const std::string address = EncodeDestination(dest);
    BOOST_CHECK(!address.empty());

    std::string error_msg;
    CTxDestination decoded = DecodeDestination(address, error_msg);
    BOOST_CHECK_MESSAGE(IsValidDestination(decoded), error_msg);
    BOOST_CHECK(std::holds_alternative<Kvanta5P2QRDestination>(decoded));
    BOOST_CHECK(std::get<Kvanta5P2QRDestination>(decoded) == dest);

    const uint256 msg_hash = Hash(std::string{"KV5 CPQKey signing test"});

    std::vector<unsigned char> sig;
    BOOST_CHECK(key.Sign(Span<const unsigned char>(msg_hash.begin(), msg_hash.size()), sig));
    BOOST_CHECK_EQUAL(sig.size(), MLDSA87_SIGNATURE_SIZE);

    BOOST_CHECK(MLDSA87Verify(
        Span<const unsigned char>(key.GetPubKeyBytes().data(), key.GetPubKeyBytes().size()),
        Span<const unsigned char>(msg_hash.begin(), msg_hash.size()),
        Span<const unsigned char>(sig.data(), sig.size())
    ));

    CPQKey imported;
    BOOST_CHECK(imported.SetSeedBytes(key.GetSeedBytes()));
    BOOST_CHECK(imported.IsValid());
    BOOST_CHECK(imported.HasSeed());
    BOOST_CHECK(imported.GetPubKeyBytes() == key.GetPubKeyBytes());
    BOOST_CHECK(imported.GetDestination() == key.GetDestination());
}

BOOST_AUTO_TEST_SUITE_END()
