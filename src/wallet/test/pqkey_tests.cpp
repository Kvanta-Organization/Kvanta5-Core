// Copyright (c) 2026 The Kvanta5 Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include <wallet/pqkey.h>

#include <addresstype.h>
#include <crypto/pq/mldsa87/mldsa87.h>
#include <hash.h>
#include <key_io.h>
#include <script/solver.h>
#include <test/util/setup_common.h>

#include <boost/test/unit_test.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <set>
#include <string>
#include <utility>
#include <vector>

BOOST_FIXTURE_TEST_SUITE(pqkey_tests, BasicTestingSetup)

namespace {

std::vector<unsigned char> MakePatternSeed(unsigned char value)
{
    return std::vector<unsigned char>(MLDSA87_SEED_SIZE, value);
}

std::vector<std::vector<unsigned char>> AdversarialSeeds()
{
    std::vector<std::vector<unsigned char>> seeds;

    seeds.push_back(MakePatternSeed(0x00));
    seeds.push_back(MakePatternSeed(0xff));
    seeds.push_back(MakePatternSeed(0xaa));
    seeds.push_back(MakePatternSeed(0x55));

    std::vector<unsigned char> ascending(MLDSA87_SEED_SIZE);
    std::vector<unsigned char> descending(MLDSA87_SEED_SIZE);

    for (size_t i = 0; i < MLDSA87_SEED_SIZE; ++i) {
        ascending[i] = static_cast<unsigned char>(i);
        descending[i] = static_cast<unsigned char>(0xffU - i);
    }

    seeds.push_back(std::move(ascending));
    seeds.push_back(std::move(descending));
    return seeds;
}

Span<const unsigned char> ByteSpan(const std::vector<unsigned char>& bytes)
{
    return Span<const unsigned char>(bytes.data(), bytes.size());
}

} // namespace

BOOST_AUTO_TEST_CASE(cpqkey_default_state)
{
    CPQKey key;

    BOOST_CHECK(!key.IsValid());
    BOOST_CHECK(!key.HasSeed());
    BOOST_CHECK(key.GetSeedBytes().empty());
    BOOST_CHECK(key.GetPubKeyBytes().empty());
    BOOST_CHECK(key.GetSecretKeyBytes().empty());

    CPQPubKey pubkey;
    BOOST_CHECK(!pubkey.IsValid());
    BOOST_CHECK(pubkey.Raw().empty());
}

BOOST_AUTO_TEST_CASE(cpqkey_seed_key_address_sign_verify)
{
    CPQKey key;
    BOOST_REQUIRE(key.MakeNewKey());
    BOOST_REQUIRE(key.IsValid());
    BOOST_REQUIRE(key.HasSeed());

    BOOST_CHECK_EQUAL(key.GetSeedBytes().size(), MLDSA87_SEED_SIZE);
    BOOST_CHECK_EQUAL(key.GetPubKeyBytes().size(), MLDSA87_PUBLIC_KEY_SIZE);
    BOOST_CHECK_EQUAL(key.GetSecretKeyBytes().size(), MLDSA87_SECRET_KEY_SIZE);

    CPQPubKey pubkey = key.GetPubKey();
    BOOST_REQUIRE(pubkey.IsValid());
    BOOST_CHECK_EQUAL(pubkey.Raw().size(), MLDSA87_PUBLIC_KEY_SIZE);
    BOOST_CHECK(pubkey.GetHash() == key.GetHash());
    BOOST_CHECK(pubkey.GetDestination() == key.GetDestination());

    const Kvanta5P2QRDestination dest = key.GetDestination();
    BOOST_REQUIRE(IsValidDestination(dest));

    const CScript script = GetScriptForDestination(dest);
    BOOST_CHECK_EQUAL(script.size(), 2 + KVANTA5_P2QR_PROGRAM_SIZE);
    BOOST_CHECK_EQUAL(script[0], OP_KVANTA5_P2QR);
    BOOST_CHECK_EQUAL(script[1], KVANTA5_P2QR_PROGRAM_SIZE);

    std::vector<std::vector<unsigned char>> solutions;
    BOOST_CHECK(Solver(script, solutions) == TxoutType::KVANTA5_P2QR);
    BOOST_REQUIRE_EQUAL(solutions.size(), 1);
    BOOST_CHECK_EQUAL(solutions[0].size(), KVANTA5_P2QR_PROGRAM_SIZE);

    CTxDestination extracted;
    BOOST_REQUIRE(ExtractDestination(script, extracted));
    BOOST_REQUIRE(std::holds_alternative<Kvanta5P2QRDestination>(extracted));
    BOOST_CHECK(std::get<Kvanta5P2QRDestination>(extracted) == dest);

    const std::string address = EncodeDestination(dest);
    BOOST_REQUIRE(!address.empty());

    std::string error_msg;
    const CTxDestination decoded = DecodeDestination(address, error_msg);
    BOOST_CHECK_MESSAGE(IsValidDestination(decoded), error_msg);
    BOOST_REQUIRE(std::holds_alternative<Kvanta5P2QRDestination>(decoded));
    BOOST_CHECK(std::get<Kvanta5P2QRDestination>(decoded) == dest);

    const uint256 msg_hash = Hash(std::string{"KV5 CPQKey signing test"});
    std::vector<unsigned char> sig;

    BOOST_REQUIRE(key.Sign(
        Span<const unsigned char>(msg_hash.begin(), msg_hash.size()), sig));
    BOOST_REQUIRE_EQUAL(sig.size(), MLDSA87_SIGNATURE_SIZE);

    BOOST_CHECK(MLDSA87Verify(
        ByteSpan(key.GetPubKeyBytes()),
        Span<const unsigned char>(msg_hash.begin(), msg_hash.size()),
        ByteSpan(sig)));

    CPQKey imported;
    BOOST_REQUIRE(imported.SetSeedBytes(key.GetSeedBytes()));
    BOOST_REQUIRE(imported.IsValid());
    BOOST_REQUIRE(imported.HasSeed());
    BOOST_CHECK(imported.GetPubKeyBytes() == key.GetPubKeyBytes());
    BOOST_CHECK(imported.GetSecretKeyBytes() == key.GetSecretKeyBytes());
    BOOST_CHECK(imported.GetDestination() == key.GetDestination());
}

BOOST_AUTO_TEST_CASE(mldsa87_wrapper_size_contracts)
{
    std::vector<unsigned char> pubkey;
    std::vector<unsigned char> seckey;

    BOOST_CHECK(!MLDSA87GenerateKeypairFromSeed(
        ByteSpan(std::vector<unsigned char>(MLDSA87_SEED_SIZE - 1, 0x42)),
        pubkey,
        seckey));
    BOOST_CHECK(pubkey.empty());
    BOOST_CHECK(seckey.empty());

    BOOST_CHECK(!MLDSA87GenerateKeypairFromSeed(
        ByteSpan(std::vector<unsigned char>(MLDSA87_SEED_SIZE + 1, 0x42)),
        pubkey,
        seckey));
    BOOST_CHECK(pubkey.empty());
    BOOST_CHECK(seckey.empty());

    CPQKey key;
    BOOST_REQUIRE(key.SetSeedBytes(MakePatternSeed(0x13)));

    const std::vector<unsigned char> message{'K', 'V', '5'};
    std::vector<unsigned char> sig;

    BOOST_CHECK(!MLDSA87Sign(
        ByteSpan(std::vector<unsigned char>(MLDSA87_SECRET_KEY_SIZE - 1, 0x00)),
        ByteSpan(message),
        sig));
    BOOST_CHECK(sig.empty());

    BOOST_REQUIRE(key.Sign(ByteSpan(message), sig));
    BOOST_REQUIRE_EQUAL(sig.size(), MLDSA87_SIGNATURE_SIZE);

    BOOST_CHECK(!MLDSA87Verify(
        ByteSpan(std::vector<unsigned char>(MLDSA87_PUBLIC_KEY_SIZE - 1, 0x00)),
        ByteSpan(message),
        ByteSpan(sig)));

    std::vector<unsigned char> short_sig(sig.begin(), sig.end() - 1);
    BOOST_CHECK(!MLDSA87Verify(
        ByteSpan(key.GetPubKeyBytes()),
        ByteSpan(message),
        ByteSpan(short_sig)));

    std::vector<unsigned char> long_sig = sig;
    long_sig.push_back(0x00);
    BOOST_CHECK(!MLDSA87Verify(
        ByteSpan(key.GetPubKeyBytes()),
        ByteSpan(message),
        ByteSpan(long_sig)));
}

BOOST_AUTO_TEST_CASE(cpqkey_adversarial_seed_determinism)
{
    std::set<std::vector<unsigned char>> public_keys;

    for (const auto& seed : AdversarialSeeds()) {
        CPQKey a;
        CPQKey b;

        BOOST_REQUIRE(a.SetSeedBytes(seed));
        BOOST_REQUIRE(b.SetSeedBytes(seed));
        BOOST_REQUIRE(a.IsValid());
        BOOST_REQUIRE(b.IsValid());

        BOOST_CHECK(a.GetSeedBytes() == seed);
        BOOST_CHECK(a.GetPubKeyBytes() == b.GetPubKeyBytes());
        BOOST_CHECK(a.GetSecretKeyBytes() == b.GetSecretKeyBytes());
        BOOST_CHECK(a.GetDestination() == b.GetDestination());

        BOOST_CHECK(public_keys.insert(a.GetPubKeyBytes()).second);

        std::vector<unsigned char> direct_pubkey;
        std::vector<unsigned char> direct_seckey;

        BOOST_REQUIRE(MLDSA87GenerateKeypairFromSeed(
            ByteSpan(seed), direct_pubkey, direct_seckey));

        BOOST_CHECK(direct_pubkey == a.GetPubKeyBytes());
        BOOST_CHECK(direct_seckey == a.GetSecretKeyBytes());
    }
}

BOOST_AUTO_TEST_CASE(cpqkey_rejected_seed_preserves_valid_state)
{
    CPQKey key;
    BOOST_REQUIRE(key.SetSeedBytes(MakePatternSeed(0x5a)));

    const auto original_seed = key.GetSeedBytes();
    const auto original_pubkey = key.GetPubKeyBytes();
    const auto original_seckey = key.GetSecretKeyBytes();
    const auto original_dest = key.GetDestination();

    BOOST_CHECK(!key.SetSeedBytes(
        std::vector<unsigned char>(MLDSA87_SEED_SIZE - 1, 0x42)));
    BOOST_CHECK(key.IsValid());
    BOOST_CHECK(key.HasSeed());
    BOOST_CHECK(key.GetSeedBytes() == original_seed);
    BOOST_CHECK(key.GetPubKeyBytes() == original_pubkey);
    BOOST_CHECK(key.GetSecretKeyBytes() == original_seckey);
    BOOST_CHECK(key.GetDestination() == original_dest);

    BOOST_CHECK(!key.SetSeedBytes(
        std::vector<unsigned char>(MLDSA87_SEED_SIZE + 1, 0x42)));
    BOOST_CHECK(key.IsValid());
    BOOST_CHECK(key.GetSeedBytes() == original_seed);
    BOOST_CHECK(key.GetPubKeyBytes() == original_pubkey);
    BOOST_CHECK(key.GetSecretKeyBytes() == original_seckey);
    BOOST_CHECK(key.GetDestination() == original_dest);
}

BOOST_AUTO_TEST_CASE(cpqkey_seed_single_bit_influence)
{
    const std::vector<unsigned char> baseline_seed(MLDSA87_SEED_SIZE, 0x00);

    CPQKey baseline;
    BOOST_REQUIRE(baseline.SetSeedBytes(baseline_seed));

    const auto baseline_pub = baseline.GetPubKeyBytes();
    BOOST_REQUIRE_EQUAL(baseline_pub.size(), MLDSA87_PUBLIC_KEY_SIZE);

    size_t minimum_rho_hamming = std::numeric_limits<size_t>::max();
    size_t maximum_rho_hamming = 0;
    uint64_t total_rho_hamming = 0;

    std::set<std::array<unsigned char, MLDSA87_SEED_SIZE>> rho_values;

    for (size_t bit = 0; bit < MLDSA87_SEED_SIZE * 8; ++bit) {
        std::vector<unsigned char> mutated = baseline_seed;
        mutated[bit / 8] ^= static_cast<unsigned char>(1U << (bit % 8));

        CPQKey key;
        BOOST_REQUIRE(key.SetSeedBytes(mutated));

        const auto& pub = key.GetPubKeyBytes();
        BOOST_CHECK(pub != baseline_pub);

        size_t hamming = 0;
        std::array<unsigned char, MLDSA87_SEED_SIZE> rho{};

        for (size_t i = 0; i < MLDSA87_SEED_SIZE; ++i) {
            rho[i] = pub[i];

            unsigned char x =
                static_cast<unsigned char>(pub[i] ^ baseline_pub[i]);

            while (x != 0) {
                hamming += x & 1U;
                x >>= 1;
            }
        }

        BOOST_CHECK(rho_values.insert(rho).second);

        minimum_rho_hamming = std::min(minimum_rho_hamming, hamming);
        maximum_rho_hamming = std::max(maximum_rho_hamming, hamming);
        total_rho_hamming += hamming;
    }

    const double average =
        static_cast<double>(total_rho_hamming) /
        static_cast<double>(MLDSA87_SEED_SIZE * 8);

    BOOST_TEST_MESSAGE(
        "P2QR seed avalanche: rho Hamming min="
        << minimum_rho_hamming
        << " max=" << maximum_rho_hamming
        << " avg=" << average);

    BOOST_CHECK(minimum_rho_hamming > 64);
    BOOST_CHECK(maximum_rho_hamming < 192);
    BOOST_CHECK(average > 112.0);
    BOOST_CHECK(average < 144.0);
}

BOOST_AUTO_TEST_CASE(mldsa87_signature_negative_matrix)
{
    CPQKey key_a;
    CPQKey key_b;

    BOOST_REQUIRE(key_a.SetSeedBytes(MakePatternSeed(0x11)));
    BOOST_REQUIRE(key_b.SetSeedBytes(MakePatternSeed(0x22)));

    const std::vector<unsigned char> message{
        'K', 'v', 'a', 'n', 't', 'a', '5', ' ', 's', 'i', 'g', 'n', 'a', 't', 'u', 'r', 'e'};
    std::vector<unsigned char> wrong_message = message;
    wrong_message.back() ^= 0x01;

    std::vector<unsigned char> sig;
    BOOST_REQUIRE(key_a.Sign(ByteSpan(message), sig));

    BOOST_CHECK(MLDSA87Verify(
        ByteSpan(key_a.GetPubKeyBytes()), ByteSpan(message), ByteSpan(sig)));

    BOOST_CHECK(!MLDSA87Verify(
        ByteSpan(key_b.GetPubKeyBytes()), ByteSpan(message), ByteSpan(sig)));

    BOOST_CHECK(!MLDSA87Verify(
        ByteSpan(key_a.GetPubKeyBytes()), ByteSpan(wrong_message), ByteSpan(sig)));

    const std::array<size_t, 12> mutation_offsets{
        0,
        1,
        31,
        32,
        255,
        511,
        1023,
        MLDSA87_SIGNATURE_SIZE / 2,
        MLDSA87_SIGNATURE_SIZE - 3,
        MLDSA87_SIGNATURE_SIZE - 2,
        MLDSA87_SIGNATURE_SIZE - 1,
        MLDSA87_SIGNATURE_SIZE / 3,
    };

    for (const size_t offset : mutation_offsets) {
        std::vector<unsigned char> mutated = sig;
        mutated[offset] ^= 0x01;

        BOOST_CHECK_MESSAGE(
            !MLDSA87Verify(
                ByteSpan(key_a.GetPubKeyBytes()),
                ByteSpan(message),
                ByteSpan(mutated)),
            "Mutated ML-DSA-87 signature unexpectedly verified at byte "
                << offset);
    }

    const std::array<size_t, 8> pubkey_offsets{
        0,
        1,
        31,
        32,
        255,
        MLDSA87_PUBLIC_KEY_SIZE / 2,
        MLDSA87_PUBLIC_KEY_SIZE - 2,
        MLDSA87_PUBLIC_KEY_SIZE - 1,
    };

    for (const size_t offset : pubkey_offsets) {
        std::vector<unsigned char> mutated_pubkey = key_a.GetPubKeyBytes();
        mutated_pubkey[offset] ^= 0x01;

        BOOST_CHECK_MESSAGE(
            !MLDSA87Verify(
                ByteSpan(mutated_pubkey),
                ByteSpan(message),
                ByteSpan(sig)),
            "Mutated ML-DSA-87 public key unexpectedly verified at byte "
                << offset);
    }
}

BOOST_AUTO_TEST_CASE(mldsa87_randomized_signatures)
{
    CPQKey key;
    BOOST_REQUIRE(key.SetSeedBytes(MakePatternSeed(0x7c)));

    const std::vector<unsigned char> message{
        'K', 'V', '5', '-', 'M', 'L', 'D', 'S', 'A', '-', 'R', 'N', 'G'};

    std::set<std::vector<unsigned char>> signatures;

    static constexpr size_t SIGNATURE_COUNT = 16;
    for (size_t i = 0; i < SIGNATURE_COUNT; ++i) {
        std::vector<unsigned char> sig;
        BOOST_REQUIRE(key.Sign(ByteSpan(message), sig));
        BOOST_REQUIRE_EQUAL(sig.size(), MLDSA87_SIGNATURE_SIZE);

        BOOST_CHECK(MLDSA87Verify(
            ByteSpan(key.GetPubKeyBytes()),
            ByteSpan(message),
            ByteSpan(sig)));

        BOOST_CHECK(signatures.insert(std::move(sig)).second);
    }

    BOOST_CHECK_EQUAL(signatures.size(), SIGNATURE_COUNT);
}

BOOST_AUTO_TEST_CASE(mldsa87_message_length_edges)
{
    CPQKey key;
    BOOST_REQUIRE(key.SetSeedBytes(MakePatternSeed(0x33)));

    const std::vector<std::vector<unsigned char>> messages{
        {},
        {0x00},
        {0xff},
        std::vector<unsigned char>(32, 0x00),
        std::vector<unsigned char>(32, 0xff),
        std::vector<unsigned char>(4096, 0x5a),
        std::vector<unsigned char>(65536, 0xa5),
    };

    for (const auto& message : messages) {
        std::vector<unsigned char> sig;
        BOOST_REQUIRE(key.Sign(ByteSpan(message), sig));
        BOOST_REQUIRE_EQUAL(sig.size(), MLDSA87_SIGNATURE_SIZE);

        BOOST_CHECK_MESSAGE(
            MLDSA87Verify(
                ByteSpan(key.GetPubKeyBytes()),
                ByteSpan(message),
                ByteSpan(sig)),
            "Failed ML-DSA-87 round-trip for message length "
                << message.size());
    }
}

BOOST_AUTO_TEST_CASE(cpqpubkey_validation_and_destination_binding)
{
    CPQKey key;
    BOOST_REQUIRE(key.SetSeedBytes(MakePatternSeed(0x91)));

    CPQPubKey correct{key.GetPubKeyBytes()};
    BOOST_REQUIRE(correct.IsValid());
    BOOST_CHECK(correct.GetHash() == key.GetHash());
    BOOST_CHECK(correct.GetDestination() == key.GetDestination());

    std::vector<unsigned char> short_bytes(MLDSA87_PUBLIC_KEY_SIZE - 1, 0x00);
    CPQPubKey short_pubkey{short_bytes};
    BOOST_CHECK(!short_pubkey.IsValid());

    std::vector<unsigned char> long_bytes(MLDSA87_PUBLIC_KEY_SIZE + 1, 0x00);
    CPQPubKey long_pubkey{long_bytes};
    BOOST_CHECK(!long_pubkey.IsValid());

    std::vector<unsigned char> mutated_bytes = key.GetPubKeyBytes();
    mutated_bytes.back() ^= 0x01;
    CPQPubKey mutated{mutated_bytes};

    BOOST_REQUIRE(mutated.IsValid());
    BOOST_CHECK(mutated.GetHash() != key.GetHash());
    BOOST_CHECK(mutated.GetDestination() != key.GetDestination());
}

BOOST_AUTO_TEST_SUITE_END()
