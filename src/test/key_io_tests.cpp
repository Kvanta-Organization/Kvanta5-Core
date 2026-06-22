// Copyright (c) 2015-2026 The Bitcoin Core developers
// Copyright (c) 2026 The Kvanta5 Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/data/key_io_invalid.json.h>

#include <key.h>
#include <key_io.h>
#include <script/script.h>
#include <script/solver.h>
#include <test/util/json.h>
#include <test/util/setup_common.h>
#include <univalue.h>
#include <util/chaintype.h>
#include <util/strencodings.h>

#include <boost/test/unit_test.hpp>

#include <vector>

BOOST_FIXTURE_TEST_SUITE(key_io_tests, BasicTestingSetup)

static CScript Kvanta5TestP2QRScript(uint8_t tag)
{
    std::vector<unsigned char> program(32);
    for (size_t i = 0; i < program.size(); ++i) {
        program[i] = static_cast<unsigned char>(tag + i);
    }

    return CScript() << OP_RESERVED << program;
}

static void CheckDestinationRoundTrip(const CScript& expected_script)
{
    CTxDestination dest;
    BOOST_REQUIRE(ExtractDestination(expected_script, dest));
    BOOST_REQUIRE(IsValidDestination(dest));

    const std::string address = EncodeDestination(dest);
    BOOST_REQUIRE(!address.empty());

    const CTxDestination decoded = DecodeDestination(address);
    BOOST_REQUIRE(IsValidDestination(decoded));

    const CScript decoded_script = GetScriptForDestination(decoded);
    BOOST_CHECK_EQUAL(HexStr(decoded_script), HexStr(expected_script));

    CKey privkey = DecodeSecret(address);
    BOOST_CHECK_MESSAGE(!privkey.IsValid(), "address decoded as private key: " + address);

    std::string flipped = address;
    for (char& c : flipped) {
        if (c >= 'a' && c <= 'z') {
            c = (c - 'a') + 'A';
        } else if (c >= 'A' && c <= 'Z') {
            c = (c - 'A') + 'a';
        }
    }

    const CTxDestination flipped_dest = DecodeDestination(flipped);

    // Bech32/Bech32m-style native P2QR addresses are valid as all-lowercase
    // or all-uppercase. Base58 wrapped P2SH-P2QR addresses are case-sensitive.
    const bool is_native_p2qr = address.rfind("kvqr1", 0) == 0 || address.rfind("KVQR1", 0) == 0;
    if (is_native_p2qr) {
        BOOST_CHECK_MESSAGE(IsValidDestination(flipped_dest), "uppercase native P2QR address became invalid: " + flipped);
        BOOST_CHECK_EQUAL(HexStr(GetScriptForDestination(flipped_dest)), HexStr(expected_script));
    } else {
        BOOST_CHECK_MESSAGE(!IsValidDestination(flipped_dest), "case-flipped Base58 address remained valid: " + flipped);
    }
}

BOOST_AUTO_TEST_CASE(key_io_valid_parse)
{
    SelectParams(ChainType::MAIN);

    // Native Kvanta5 P2QR: OP_RESERVED + 32-byte program.
    CheckDestinationRoundTrip(Kvanta5TestP2QRScript(0xC5));

    // Wrapped P2SH-P2QR compatibility address:
    // address encodes the P2SH scriptPubKey for redeemScript = native P2QR script.
    const CScript redeem_script = Kvanta5TestP2QRScript(0xD5);
    const CScript wrapped_script = GetScriptForDestination(ScriptHash(redeem_script));
    CheckDestinationRoundTrip(wrapped_script);
}

BOOST_AUTO_TEST_CASE(key_io_valid_gen)
{
    SelectParams(ChainType::MAIN);

    {
        const CScript script = Kvanta5TestP2QRScript(0xC5);
        CTxDestination dest;
        BOOST_REQUIRE(ExtractDestination(script, dest));

        const std::string address = EncodeDestination(dest);
        BOOST_REQUIRE(IsValidDestination(DecodeDestination(address)));
        BOOST_CHECK_EQUAL(HexStr(GetScriptForDestination(DecodeDestination(address))), HexStr(script));
    }

    {
        const CScript redeem_script = Kvanta5TestP2QRScript(0xD5);
        const CScript script = GetScriptForDestination(ScriptHash(redeem_script));
        CTxDestination dest;
        BOOST_REQUIRE(ExtractDestination(script, dest));

        const std::string address = EncodeDestination(dest);
        BOOST_REQUIRE(IsValidDestination(DecodeDestination(address)));
        BOOST_CHECK_EQUAL(HexStr(GetScriptForDestination(DecodeDestination(address))), HexStr(script));
    }
}

// Goal: check that parsing code is robust against a variety of corrupted data.
BOOST_AUTO_TEST_CASE(key_io_invalid)
{
    UniValue tests = read_json(json_tests::key_io_invalid);
    CKey privkey;
    CTxDestination destination;

    for (unsigned int idx = 0; idx < tests.size(); idx++) {
        const UniValue& test = tests[idx];
        std::string strTest = test.write();
        if (test.size() < 1) {
            BOOST_ERROR("Bad test: " << strTest);
            continue;
        }

        std::string exp_base58string = test[0].get_str();

        for (const auto& chain : {ChainType::MAIN, ChainType::SIGNET, ChainType::REGTEST}) {
            SelectParams(chain);

            destination = DecodeDestination(exp_base58string);
            BOOST_CHECK_MESSAGE(!IsValidDestination(destination), "IsValid pubkey: " + strTest);

            privkey = DecodeSecret(exp_base58string);
            BOOST_CHECK_MESSAGE(!privkey.IsValid(), "IsValid privkey: " + strTest);
        }
    }

    SelectParams(ChainType::MAIN);
}

BOOST_AUTO_TEST_SUITE_END()
