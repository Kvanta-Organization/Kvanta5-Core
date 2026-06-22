// Copyright (c) 2015-2026 The Bitcoin Core developers
// Copyright (c) 2026 The Kvanta5 Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <key_io.h>
#include <script/descriptor.h>
#include <script/script.h>
#include <script/solver.h>
#include <test/util/setup_common.h>
#include <util/strencodings.h>

#include <boost/test/unit_test.hpp>

#include <memory>
#include <string>
#include <vector>

namespace {

static CScript Kvanta5TestP2QRScript(uint8_t tag)
{
    std::vector<unsigned char> program(32);
    for (size_t i = 0; i < program.size(); ++i) {
        program[i] = static_cast<unsigned char>(tag + i);
    }

    return CScript() << OP_RESERVED << program;
}

static std::unique_ptr<Descriptor> ParseOne(const std::string& desc, FlatSigningProvider& provider)
{
    std::string error;
    auto parsed = Parse(desc, provider, error);
    BOOST_REQUIRE_MESSAGE(parsed.size() == 1, "Parse failed for `" + desc + "`: " + error);
    return std::move(parsed.at(0));
}

static void CheckDescriptorExpandsToScript(const std::string& desc, const CScript& expected_script)
{
    FlatSigningProvider provider;
    auto parsed = ParseOne(desc, provider);

    std::vector<CScript> scripts;
    FlatSigningProvider out;
    BOOST_REQUIRE(parsed->Expand(/*pos=*/0, provider, scripts, out));
    BOOST_REQUIRE_EQUAL(scripts.size(), 1U);
    BOOST_CHECK_EQUAL(HexStr(scripts.at(0)), HexStr(expected_script));

    DescriptorCache cache;
    std::vector<CScript> cached_scripts;
    FlatSigningProvider cached_out;
    BOOST_REQUIRE(parsed->Expand(/*pos=*/0, provider, scripts, out, &cache));
    BOOST_REQUIRE(parsed->ExpandFromCache(/*pos=*/0, cache, cached_scripts, cached_out));
    BOOST_REQUIRE_EQUAL(cached_scripts.size(), 1U);
    BOOST_CHECK_EQUAL(HexStr(cached_scripts.at(0)), HexStr(expected_script));
}

static std::string AddressForScript(const CScript& script)
{
    CTxDestination dest;
    BOOST_REQUIRE(ExtractDestination(script, dest));
    BOOST_REQUIRE(IsValidDestination(dest));

    const std::string address = EncodeDestination(dest);
    BOOST_REQUIRE(!address.empty());

    const CTxDestination decoded = DecodeDestination(address);
    BOOST_REQUIRE(IsValidDestination(decoded));
    BOOST_CHECK_EQUAL(HexStr(GetScriptForDestination(decoded)), HexStr(script));

    return address;
}

static void CheckUnparsable(const std::string& desc)
{
    FlatSigningProvider provider;
    std::string error;
    auto parsed = Parse(desc, provider, error);
    BOOST_CHECK_MESSAGE(parsed.empty(), "Descriptor unexpectedly parsed: " + desc);
}

} // namespace

BOOST_FIXTURE_TEST_SUITE(descriptor_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(descriptor_test)
{
    // Kvanta5 wallet reality:
    //   - native P2QR scriptPubKey: OP_RESERVED + 32-byte program
    //   - wrapped P2SH-P2QR compatibility scriptPubKey
    //
    // Do not preserve Bitcoin wallet descriptor assumptions here:
    // combo(), pk(), pkh(), wpkh(), sh(wpkh()), wsh(), tr(), and BTC WIF
    // descriptor vectors are not Kvanta5 wallet identity tests.

    const CScript native_p2qr = Kvanta5TestP2QRScript(0xC5);
    const std::string native_addr = AddressForScript(native_p2qr);

    CheckDescriptorExpandsToScript("raw(" + HexStr(native_p2qr) + ")", native_p2qr);
    CheckDescriptorExpandsToScript("addr(" + native_addr + ")", native_p2qr);

    const CScript redeem_script = Kvanta5TestP2QRScript(0xD5);
    const CScript wrapped_p2sh_p2qr = GetScriptForDestination(ScriptHash(redeem_script));
    const std::string wrapped_addr = AddressForScript(wrapped_p2sh_p2qr);

    CheckDescriptorExpandsToScript("raw(" + HexStr(wrapped_p2sh_p2qr) + ")", wrapped_p2sh_p2qr);
    CheckDescriptorExpandsToScript("addr(" + wrapped_addr + ")", wrapped_p2sh_p2qr);

    // BTC private-key descriptors must not be treated as valid Kvanta5 wallet
    // descriptors. This specifically guards the old upstream failure point.
    CheckUnparsable("combo(L4rK1yDtCWekvXuE6oXD9jCYfFNV2cWRpVuPLBcCU2z8TrisoyY1)");
}

BOOST_AUTO_TEST_CASE(descriptor_checksum)
{
    const CScript native_p2qr = Kvanta5TestP2QRScript(0xC5);
    const std::string desc = "raw(" + HexStr(native_p2qr) + ")";
    const std::string checksum = GetDescriptorChecksum(desc);

    BOOST_CHECK_EQUAL(checksum.size(), 8U);

    FlatSigningProvider provider;
    std::string error;
    auto parsed = Parse(desc + "#" + checksum, provider, error);
    BOOST_CHECK_MESSAGE(parsed.size() == 1, error);
}

BOOST_AUTO_TEST_SUITE_END()
