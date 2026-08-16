// Copyright (c) 2026 The Kvanta5 Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include <wallet/pqkey.h>

#include <crypto/pq/mldsa87/mldsa87.h>
#include <hash.h>
#include <key_io.h>
#include <test/util/setup_common.h>
#include <util/strencodings.h>

#include <boost/test/unit_test.hpp>

#include <cstddef>
#include <string>
#include <vector>

BOOST_FIXTURE_TEST_SUITE(pqkey_kat_tests, BasicTestingSetup)

namespace {

struct KatVector
{
    const char* name;
    const char* seed_hex;
    const char* pub_hash256_hex;
    const char* sec_hash256_hex;
    const char* key_hash256_hex;
    const char* address;
};

const std::vector<KatVector>& KatVectors()
{
    static const std::vector<KatVector> vectors{
        {
            "all_zero",
            "0000000000000000000000000000000000000000000000000000000000000000",
            "d60e58ca1b51fe494ae96633f981d92638378a6491f638e8ca3965e0e805ade0",
            "e475572b88916b26a7548614b87d2b1f2fd04a115390f9abc3afaa1f03c39ec2",
            "d60e58ca1b51fe494ae96633f981d92638378a6491f638e8ca3965e0e805ade0",
            "kvqr1uzkst68qv5uu46pc76gkfz3h8qndnq0exdnwjjjfleg3hjjcpmtqpyywfc",
        },
        {
            "all_ff",
            "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff",
            "8124ad42017690a808456a4ca327779d3c5766de7fe11fbddc0af0d10670cc60",
            "7327d3af1ea9b05fddff3184382cc647a56b650df94e3f233658cfdb1d01b633",
            "8124ad42017690a808456a4ca327779d3c5766de7fe11fbddc0af0d10670cc60",
            "kvqr1vrx8qpk37q9de0glu9lauejh8jwhwfarf34y2z9gjpmqzs4dyjqsk3y0ju",
        },
        {
            "ascending_00_1f",
            "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f",
            "25b6c23f4b621ea8c12661be540de6da9119e6b3b3d09ce8acea285dd9c8e035",
            "fd22ec5da5a441f767660e65039b78894ac8b02a4425b5eb872f10c7672aeddd",
            "25b6c23f4b621ea8c12661be540de6da9119e6b3b3d09ce8acea285dd9c8e035",
            "kvqr1xhsv3k2a9r42e6yu6zem8esej8dwvr25hesjdsdgre3yk07zkcjs4d4kut",
        },
        {
            "descending_1f_00",
            "1f1e1d1c1b1a191817161514131211100f0e0d0c0b0a09080706050403020100",
            "b48a8845473b16889a17152aa4cfe482630b31e3f647748f1fcde68debebc121",
            "ee485856a4230f07a58819c3eb8168e15e5e22ce95cff99ec1e0435c5b02c897",
            "b48a8845473b16889a17152aa4cfe482630b31e3f647748f1fcde68debebc121",
            "kvqr1y8q7h6udumx3lrm5glmwxvgtvwpwfnay9g230x5gzca5w3vg326qkeeuzg",
        },
        {
            "alternating_aa55",
            "aa55aa55aa55aa55aa55aa55aa55aa55aa55aa55aa55aa55aa55aa55aa55aa55",
            "791461efbb601f8c79e2f15e9dff85cc86ea0c338d51f409c6d61dd95b186593",
            "1fc69c7d8e28b04a6f2b38ce5960ad76da6b9b753c7e6135d5af5aad9f7da838",
            "791461efbb601f8c79e2f15e9dff85cc86ea0c338d51f409c6d61dd95b186593",
            "kvqr1jdj3sk7erhtvvz052xxnxr82smxgtluatmc7y7vvrasthmmpz3ustjxc6s",
        },
        {
            "frozen_random_01",
            "c046f904b38fb929207d7a32caf0e7b3a9c96d43f600b7197fbe663092996f33",
            "2d36577d2547c834eaaa019c48910cfcd2d7f5f9c147091de0c53c078fcc5631",
            "f6a575e0770e53bb273b58f14f60ebb509585ff324bdbe440f8368bef35419a8",
            "2d36577d2547c834eaaa019c48910cfcd2d7f5f9c147091de0c53c078fcc5631",
            "kvqr1x9tverc88nz7q8gfglqlnawh6t7qey2gnsq64635eprj2l2hxckszf8lk0",
        },
    };

    return vectors;
}

Span<const unsigned char> ByteSpan(const std::vector<unsigned char>& bytes)
{
    return Span<const unsigned char>(bytes.data(), bytes.size());
}

} // namespace

BOOST_AUTO_TEST_CASE(cpqkey_cross_platform_known_answer_vectors)
{
    const auto& vectors = KatVectors();
    BOOST_REQUIRE_EQUAL(vectors.size(), 6);

    for (const auto& vector : vectors) {
        const std::vector<unsigned char> seed = ParseHex(vector.seed_hex);
        BOOST_REQUIRE_MESSAGE(
            seed.size() == MLDSA87_SEED_SIZE,
            "Invalid frozen KAT seed length for " << vector.name);

        CPQKey key;
        BOOST_REQUIRE_MESSAGE(
            key.SetSeedBytes(seed),
            "CPQKey failed to derive frozen KAT " << vector.name);
        BOOST_REQUIRE(key.IsValid());
        BOOST_REQUIRE(key.HasSeed());

        BOOST_CHECK_MESSAGE(
            key.GetSeedBytes() == seed,
            "CPQKey did not preserve the exact seed for " << vector.name);

        BOOST_REQUIRE_EQUAL(key.GetPubKeyBytes().size(), MLDSA87_PUBLIC_KEY_SIZE);
        BOOST_REQUIRE_EQUAL(key.GetSecretKeyBytes().size(), MLDSA87_SECRET_KEY_SIZE);

        const std::string pub_hash256 = Hash(key.GetPubKeyBytes()).GetHex();
        const std::string sec_hash256 = Hash(key.GetSecretKeyBytes()).GetHex();
        const std::string key_hash256 = key.GetHash().GetHex();
        const std::string address = EncodeDestination(key.GetDestination());

        BOOST_CHECK_EQUAL(pub_hash256, vector.pub_hash256_hex);
        BOOST_CHECK_EQUAL(sec_hash256, vector.sec_hash256_hex);
        BOOST_CHECK_EQUAL(key_hash256, vector.key_hash256_hex);
        BOOST_CHECK_EQUAL(address, vector.address);

        /*
         * CPQKey::GetHash() is the destination-binding hash of the complete
         * ML-DSA-87 public key. Keep that relationship frozen too.
         */
        BOOST_CHECK_EQUAL(pub_hash256, key_hash256);

        /*
         * Verify the lower-level deterministic wrapper independently from
         * CPQKey. This catches divergence between the wallet wrapper and the
         * ML-DSA-87 seed-expansion interface.
         */
        std::vector<unsigned char> direct_pubkey;
        std::vector<unsigned char> direct_seckey;

        BOOST_REQUIRE_MESSAGE(
            MLDSA87GenerateKeypairFromSeed(
                ByteSpan(seed), direct_pubkey, direct_seckey),
            "Direct ML-DSA-87 derivation failed for " << vector.name);

        BOOST_REQUIRE_EQUAL(direct_pubkey.size(), MLDSA87_PUBLIC_KEY_SIZE);
        BOOST_REQUIRE_EQUAL(direct_seckey.size(), MLDSA87_SECRET_KEY_SIZE);

        BOOST_CHECK_MESSAGE(
            direct_pubkey == key.GetPubKeyBytes(),
            "Direct/wallet public-key derivation mismatch for " << vector.name);
        BOOST_CHECK_MESSAGE(
            direct_seckey == key.GetSecretKeyBytes(),
            "Direct/wallet secret-key derivation mismatch for " << vector.name);

        BOOST_CHECK_EQUAL(Hash(direct_pubkey).GetHex(), vector.pub_hash256_hex);
        BOOST_CHECK_EQUAL(Hash(direct_seckey).GetHex(), vector.sec_hash256_hex);

        /*
         * Re-derive a second time in the same process. This is redundant with
         * the frozen vector, but makes accidental statefulness immediately
         * visible in the failure location.
         */
        CPQKey repeated;
        BOOST_REQUIRE(repeated.SetSeedBytes(seed));

        BOOST_CHECK_MESSAGE(
            repeated.GetPubKeyBytes() == key.GetPubKeyBytes(),
            "Repeated public-key derivation changed for " << vector.name);
        BOOST_CHECK_MESSAGE(
            repeated.GetSecretKeyBytes() == key.GetSecretKeyBytes(),
            "Repeated secret-key derivation changed for " << vector.name);
        BOOST_CHECK_MESSAGE(
            repeated.GetDestination() == key.GetDestination(),
            "Repeated destination derivation changed for " << vector.name);
    }
}

BOOST_AUTO_TEST_SUITE_END()
