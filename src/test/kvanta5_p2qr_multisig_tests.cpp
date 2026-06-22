// Copyright (c) 2015-2026 The Bitcoin Core developers
// Copyright (c) 2026 The Kvanta5 Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <script/kvanta5_p2qr.h>
#include <test/util/setup_common.h>

#include <boost/test/unit_test.hpp>

#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <vector>

BOOST_FIXTURE_TEST_SUITE(kvanta5_p2qr_multisig_tests, BasicTestingSetup)

static std::vector<unsigned char> FakePubKey(unsigned int seed)
{
    std::vector<unsigned char> pubkey(KVANTA5_P2QR_PUBKEY_SIZE, 0x00);

    pubkey[0] = static_cast<unsigned char>((seed >> 24) & 0xff);
    pubkey[1] = static_cast<unsigned char>((seed >> 16) & 0xff);
    pubkey[2] = static_cast<unsigned char>((seed >> 8) & 0xff);
    pubkey[3] = static_cast<unsigned char>(seed & 0xff);

    for (size_t i = 4; i < pubkey.size(); ++i) {
        pubkey[i] = static_cast<unsigned char>((seed + i) & 0xff);
    }

    return pubkey;
}

static std::vector<unsigned char> FakeSignature(unsigned int seed)
{
    std::vector<unsigned char> sig(KVANTA5_P2QR_SIGNATURE_SIZE);

    for (size_t i = 0; i < sig.size(); ++i) {
        sig[i] = static_cast<unsigned char>((seed + i) & 0xff);
    }

    return sig;
}

static std::vector<unsigned char> SmallInt(unsigned int value)
{
    if (value <= 0xff) {
        return std::vector<unsigned char>{static_cast<unsigned char>(value)};
    }

    return std::vector<unsigned char>{
        static_cast<unsigned char>(value & 0xff),
        static_cast<unsigned char>((value >> 8) & 0xff),
    };
}

static std::vector<unsigned char> ProgramPush(const uint256& program)
{
    return std::vector<unsigned char>(program.begin(), program.end());
}

struct FakeSigner
{
    std::vector<unsigned char> pubkey;
    uint256 program;
};

static FakeSigner MakeFakeSigner(unsigned int seed)
{
    FakeSigner signer;
    signer.pubkey = FakePubKey(seed);
    signer.program = Kvanta5P2QRSingleProgram(signer.pubkey);
    return signer;
}

static std::vector<FakeSigner> MakeFakeSigners(std::initializer_list<unsigned int> seeds)
{
    std::vector<FakeSigner> signers;
    signers.reserve(seeds.size());

    for (const unsigned int seed : seeds) {
        signers.push_back(MakeFakeSigner(seed));
    }

    std::sort(signers.begin(), signers.end(),
        [](const FakeSigner& a, const FakeSigner& b) {
            return a.program < b.program;
        });

    return signers;
}

static std::vector<FakeSigner> MakeFakeSigners(unsigned int first_seed, unsigned int count)
{
    std::vector<FakeSigner> signers;
    signers.reserve(count);

    for (unsigned int i = 0; i < count; ++i) {
        signers.push_back(MakeFakeSigner(first_seed + i));
    }

    std::sort(signers.begin(), signers.end(),
        [](const FakeSigner& a, const FakeSigner& b) {
            return a.program < b.program;
        });

    return signers;
}

static std::vector<uint256> SignerPrograms(const std::vector<FakeSigner>& signers)
{
    std::vector<uint256> programs;
    programs.reserve(signers.size());

    for (const FakeSigner& signer : signers) {
        programs.push_back(signer.program);
    }

    return programs;
}

static std::vector<std::vector<unsigned char>> BuildSpendStack(
    const std::vector<FakeSigner>& signers,
    const std::vector<uint16_t>& signer_indexes)
{
    const uint16_t required = static_cast<uint16_t>(signer_indexes.size());
    const uint16_t total = static_cast<uint16_t>(signers.size());

    std::vector<std::vector<unsigned char>> stack;
    stack.reserve(required + required + required + total + 2);

    for (uint16_t i = 0; i < required; ++i) {
        stack.push_back(FakeSignature(1000 + i));
    }

    for (const uint16_t signer_index : signer_indexes) {
        stack.push_back(SmallInt(signer_index));
    }

    for (const uint16_t signer_index : signer_indexes) {
        if (signer_index < signers.size()) {
            stack.push_back(signers[signer_index].pubkey);
        } else {
            stack.push_back(FakePubKey(900000 + signer_index));
        }
    }

    for (const FakeSigner& signer : signers) {
        stack.push_back(ProgramPush(signer.program));
    }

    stack.push_back(SmallInt(required));
    stack.push_back(SmallInt(total));

    return stack;
}

BOOST_AUTO_TEST_CASE(kvanta5_p2qr_multisig_parse_valid_2_of_3)
{
    const std::vector<FakeSigner> signers = MakeFakeSigners({1, 2, 3});
    const std::vector<uint256> signer_programs = SignerPrograms(signers);

    const std::vector<std::vector<unsigned char>> stack = BuildSpendStack(signers, {0, 2});

    Kvanta5P2QRMultisigSpend spend;
    std::string error;

    BOOST_CHECK_MESSAGE(Kvanta5ParseP2QRMultisigSpend(stack, spend, &error), error);

    BOOST_CHECK_EQUAL(spend.required, 2);
    BOOST_CHECK_EQUAL(spend.total, 3);
    BOOST_CHECK_EQUAL(spend.signatures.size(), 2);
    BOOST_CHECK_EQUAL(spend.signer_indexes.size(), 2);
    BOOST_CHECK_EQUAL(spend.signing_pubkeys.size(), 2);
    BOOST_CHECK_EQUAL(spend.signer_programs.size(), 3);

    BOOST_CHECK_EQUAL(spend.signer_indexes[0], 0);
    BOOST_CHECK_EQUAL(spend.signer_indexes[1], 2);

    BOOST_CHECK(spend.signing_pubkeys[0] == signers[0].pubkey);
    BOOST_CHECK(spend.signing_pubkeys[1] == signers[2].pubkey);

    for (size_t i = 0; i < signer_programs.size(); ++i) {
        BOOST_CHECK(spend.signer_programs[i] == signer_programs[i]);
    }

    const uint256 program1 = Kvanta5P2QRMultisigProgram(spend.required, spend.signer_programs);
    const uint256 program2 = Kvanta5P2QRMultisigProgram(2, signer_programs);

    BOOST_CHECK(program1 == program2);
}

BOOST_AUTO_TEST_CASE(kvanta5_p2qr_multisig_parse_valid_1024_of_1024)
{
    const std::vector<FakeSigner> signers = MakeFakeSigners(20000, 1024);

    std::vector<uint16_t> signer_indexes;
    signer_indexes.reserve(1024);

    for (uint16_t i = 0; i < 1024; ++i) {
        signer_indexes.push_back(i);
    }

    const std::vector<std::vector<unsigned char>> stack = BuildSpendStack(signers, signer_indexes);

    Kvanta5P2QRMultisigSpend spend;
    std::string error;

    BOOST_CHECK_MESSAGE(Kvanta5ParseP2QRMultisigSpend(stack, spend, &error), error);

    BOOST_CHECK_EQUAL(spend.required, 1024);
    BOOST_CHECK_EQUAL(spend.total, 1024);
    BOOST_CHECK_EQUAL(spend.signatures.size(), 1024);
    BOOST_CHECK_EQUAL(spend.signer_indexes.size(), 1024);
    BOOST_CHECK_EQUAL(spend.signing_pubkeys.size(), 1024);
    BOOST_CHECK_EQUAL(spend.signer_programs.size(), 1024);
}

BOOST_AUTO_TEST_CASE(kvanta5_p2qr_multisig_rejects_1025_signer_programs)
{
    const std::vector<FakeSigner> signers = MakeFakeSigners(30000, 1025);

    std::vector<std::vector<unsigned char>> stack;
    stack.reserve(1 + 1 + 1 + 1025 + 2);

    stack.push_back(FakeSignature(11));
    stack.push_back(SmallInt(0));
    stack.push_back(signers[0].pubkey);

    for (const FakeSigner& signer : signers) {
        stack.push_back(ProgramPush(signer.program));
    }

    stack.push_back(SmallInt(1));
    stack.push_back(SmallInt(1025));

    Kvanta5P2QRMultisigSpend spend;
    std::string error;

    BOOST_CHECK(!Kvanta5ParseP2QRMultisigSpend(stack, spend, &error));
    BOOST_CHECK(!error.empty());
}

BOOST_AUTO_TEST_CASE(kvanta5_p2qr_multisig_rejects_arbitrary_extra_push)
{
    const std::vector<FakeSigner> signers = MakeFakeSigners({1, 2, 3});

    std::vector<std::vector<unsigned char>> stack = BuildSpendStack(signers, {0, 2});
    stack.insert(stack.end() - 2, std::vector<unsigned char>{'N', 'O', 'N', '_', 'M', 'O', 'N', 'E', 'T', 'A', 'R', 'Y'});

    Kvanta5P2QRMultisigSpend spend;
    std::string error;

    BOOST_CHECK(!Kvanta5ParseP2QRMultisigSpend(stack, spend, &error));
    BOOST_CHECK_EQUAL(error, "p2qr-multisig-extra-or-missing-stack-items");
}

BOOST_AUTO_TEST_CASE(kvanta5_p2qr_multisig_rejects_missing_signature)
{
    const std::vector<FakeSigner> signers = MakeFakeSigners({1, 2, 3});

    std::vector<std::vector<unsigned char>> stack;
    stack.reserve(1 + 2 + 2 + 3 + 2);

    stack.push_back(FakeSignature(11));

    stack.push_back(SmallInt(0));
    stack.push_back(SmallInt(2));

    stack.push_back(signers[0].pubkey);
    stack.push_back(signers[2].pubkey);

    for (const FakeSigner& signer : signers) {
        stack.push_back(ProgramPush(signer.program));
    }

    stack.push_back(SmallInt(2));
    stack.push_back(SmallInt(3));

    Kvanta5P2QRMultisigSpend spend;
    std::string error;

    BOOST_CHECK(!Kvanta5ParseP2QRMultisigSpend(stack, spend, &error));
    BOOST_CHECK_EQUAL(error, "p2qr-multisig-extra-or-missing-stack-items");
}

BOOST_AUTO_TEST_CASE(kvanta5_p2qr_multisig_rejects_bad_signature_size)
{
    const std::vector<FakeSigner> signers = MakeFakeSigners({1, 2, 3});

    std::vector<std::vector<unsigned char>> stack = BuildSpendStack(signers, {0, 2});
    stack[0].push_back(0x00);

    Kvanta5P2QRMultisigSpend spend;
    std::string error;

    BOOST_CHECK(!Kvanta5ParseP2QRMultisigSpend(stack, spend, &error));
    BOOST_CHECK(!error.empty());
}

BOOST_AUTO_TEST_CASE(kvanta5_p2qr_multisig_rejects_bad_signing_pubkey_size)
{
    const std::vector<FakeSigner> signers = MakeFakeSigners({1, 2, 3});

    std::vector<std::vector<unsigned char>> stack = BuildSpendStack(signers, {0, 2});
    const size_t first_signing_pubkey_pos = 2 + 2;
    stack[first_signing_pubkey_pos].push_back(0x00);

    Kvanta5P2QRMultisigSpend spend;
    std::string error;

    BOOST_CHECK(!Kvanta5ParseP2QRMultisigSpend(stack, spend, &error));
    BOOST_CHECK(!error.empty());
}

BOOST_AUTO_TEST_CASE(kvanta5_p2qr_multisig_rejects_bad_signer_program_size)
{
    const std::vector<FakeSigner> signers = MakeFakeSigners({1, 2, 3});

    std::vector<std::vector<unsigned char>> stack = BuildSpendStack(signers, {0, 2});
    const size_t first_signer_program_pos = 2 + 2 + 2;
    stack[first_signer_program_pos].push_back(0x00);

    Kvanta5P2QRMultisigSpend spend;
    std::string error;

    BOOST_CHECK(!Kvanta5ParseP2QRMultisigSpend(stack, spend, &error));
    BOOST_CHECK(!error.empty());
}

BOOST_AUTO_TEST_CASE(kvanta5_p2qr_multisig_rejects_unsorted_signer_programs)
{
    std::vector<FakeSigner> signers = MakeFakeSigners({1, 2, 3});
    std::swap(signers[0], signers[2]);

    const std::vector<std::vector<unsigned char>> stack = BuildSpendStack(signers, {0, 2});

    Kvanta5P2QRMultisigSpend spend;
    std::string error;

    BOOST_CHECK(!Kvanta5ParseP2QRMultisigSpend(stack, spend, &error));
    BOOST_CHECK(!error.empty());
}

BOOST_AUTO_TEST_CASE(kvanta5_p2qr_multisig_rejects_duplicate_signer_programs)
{
    std::vector<FakeSigner> signers = MakeFakeSigners({1, 2, 3});
    signers[1].program = signers[0].program;

    const std::vector<std::vector<unsigned char>> stack = BuildSpendStack(signers, {0, 2});

    Kvanta5P2QRMultisigSpend spend;
    std::string error;

    BOOST_CHECK(!Kvanta5ParseP2QRMultisigSpend(stack, spend, &error));
    BOOST_CHECK(!error.empty());
}

BOOST_AUTO_TEST_CASE(kvanta5_p2qr_multisig_rejects_zero_threshold)
{
    const std::vector<FakeSigner> signers = MakeFakeSigners({1});

    std::vector<std::vector<unsigned char>> stack;
    stack.reserve(3);

    stack.push_back(ProgramPush(signers[0].program));
    stack.push_back(std::vector<unsigned char>{0x00});
    stack.push_back(SmallInt(1));

    Kvanta5P2QRMultisigSpend spend;
    std::string error;

    BOOST_CHECK(!Kvanta5ParseP2QRMultisigSpend(stack, spend, &error));
    BOOST_CHECK(!error.empty());
}

BOOST_AUTO_TEST_CASE(kvanta5_p2qr_multisig_rejects_required_greater_than_total)
{
    const std::vector<FakeSigner> signers = MakeFakeSigners({1});

    std::vector<std::vector<unsigned char>> stack;
    stack.reserve(2 + 2 + 2 + 1 + 2);

    stack.push_back(FakeSignature(11));
    stack.push_back(FakeSignature(22));

    stack.push_back(SmallInt(0));
    stack.push_back(SmallInt(0));

    stack.push_back(signers[0].pubkey);
    stack.push_back(signers[0].pubkey);

    stack.push_back(ProgramPush(signers[0].program));

    stack.push_back(SmallInt(2));
    stack.push_back(SmallInt(1));

    Kvanta5P2QRMultisigSpend spend;
    std::string error;

    BOOST_CHECK(!Kvanta5ParseP2QRMultisigSpend(stack, spend, &error));
    BOOST_CHECK(!error.empty());
}

BOOST_AUTO_TEST_CASE(kvanta5_p2qr_multisig_rejects_duplicate_signer_index)
{
    const std::vector<FakeSigner> signers = MakeFakeSigners({1, 2, 3});

    const std::vector<std::vector<unsigned char>> stack = BuildSpendStack(signers, {0, 0});

    Kvanta5P2QRMultisigSpend spend;
    std::string error;

    BOOST_CHECK(!Kvanta5ParseP2QRMultisigSpend(stack, spend, &error));
    BOOST_CHECK_EQUAL(error, "p2qr-multisig-signer-indexes-not-strictly-increasing");
}

BOOST_AUTO_TEST_CASE(kvanta5_p2qr_multisig_rejects_unsorted_signer_indexes)
{
    const std::vector<FakeSigner> signers = MakeFakeSigners({1, 2, 3});

    const std::vector<std::vector<unsigned char>> stack = BuildSpendStack(signers, {2, 0});

    Kvanta5P2QRMultisigSpend spend;
    std::string error;

    BOOST_CHECK(!Kvanta5ParseP2QRMultisigSpend(stack, spend, &error));
    BOOST_CHECK_EQUAL(error, "p2qr-multisig-signer-indexes-not-strictly-increasing");
}

BOOST_AUTO_TEST_CASE(kvanta5_p2qr_multisig_rejects_signer_index_out_of_range)
{
    const std::vector<FakeSigner> signers = MakeFakeSigners({1, 2, 3});

    const std::vector<std::vector<unsigned char>> stack = BuildSpendStack(signers, {0, 3});

    Kvanta5P2QRMultisigSpend spend;
    std::string error;

    BOOST_CHECK(!Kvanta5ParseP2QRMultisigSpend(stack, spend, &error));
    BOOST_CHECK_EQUAL(error, "p2qr-multisig-signer-index-out-of-range");
}

BOOST_AUTO_TEST_CASE(kvanta5_p2qr_multisig_rejects_signing_pubkey_not_matching_signer_program)
{
    const std::vector<FakeSigner> signers = MakeFakeSigners({1, 2, 3});

    std::vector<std::vector<unsigned char>> stack = BuildSpendStack(signers, {0, 2});

    const size_t second_signing_pubkey_pos = 2 + 2 + 1;
    stack[second_signing_pubkey_pos] = FakePubKey(999999);

    Kvanta5P2QRMultisigSpend spend;
    std::string error;

    BOOST_CHECK(!Kvanta5ParseP2QRMultisigSpend(stack, spend, &error));
    BOOST_CHECK(!error.empty());
}

BOOST_AUTO_TEST_CASE(kvanta5_p2qr_multisig_rejects_nonminimal_large_integer)
{
    const std::vector<FakeSigner> signers = MakeFakeSigners({1});

    std::vector<unsigned char> nonminimal_one{0x01, 0x00};

    std::vector<std::vector<unsigned char>> stack;
    stack.reserve(1 + 1 + 1 + 1 + 2);

    stack.push_back(FakeSignature(11));
    stack.push_back(SmallInt(0));
    stack.push_back(signers[0].pubkey);
    stack.push_back(ProgramPush(signers[0].program));
    stack.push_back(nonminimal_one);
    stack.push_back(SmallInt(1));

    Kvanta5P2QRMultisigSpend spend;
    std::string error;

    BOOST_CHECK(!Kvanta5ParseP2QRMultisigSpend(stack, spend, &error));
    BOOST_CHECK(!error.empty());
}

BOOST_AUTO_TEST_CASE(kvanta5_p2qr_multisig_program_depends_on_threshold)
{
    const std::vector<FakeSigner> signers = MakeFakeSigners({1, 2, 3});
    const std::vector<uint256> signer_programs = SignerPrograms(signers);

    const uint256 one_of_three = Kvanta5P2QRMultisigProgram(1, signer_programs);
    const uint256 two_of_three = Kvanta5P2QRMultisigProgram(2, signer_programs);
    const uint256 three_of_three = Kvanta5P2QRMultisigProgram(3, signer_programs);

    BOOST_CHECK(one_of_three != two_of_three);
    BOOST_CHECK(two_of_three != three_of_three);
    BOOST_CHECK(one_of_three != three_of_three);
}

BOOST_AUTO_TEST_CASE(kvanta5_p2qr_multisig_program_depends_on_signer_program_set)
{
    const std::vector<FakeSigner> signers_a = MakeFakeSigners({1, 2, 3});
    const std::vector<FakeSigner> signers_b = MakeFakeSigners({1, 2, 4});

    const std::vector<uint256> signer_programs_a = SignerPrograms(signers_a);
    const std::vector<uint256> signer_programs_b = SignerPrograms(signers_b);

    const uint256 program_a = Kvanta5P2QRMultisigProgram(2, signer_programs_a);
    const uint256 program_b = Kvanta5P2QRMultisigProgram(2, signer_programs_b);

    BOOST_CHECK(program_a != program_b);
}

BOOST_AUTO_TEST_SUITE_END()
