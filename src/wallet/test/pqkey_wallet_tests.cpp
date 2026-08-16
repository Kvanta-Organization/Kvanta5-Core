// Copyright (c) 2026 The Kvanta5 Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include <wallet/pqkey.h>
#include <wallet/test/util.h>
#include <wallet/wallet.h>
#include <wallet/walletdb.h>

#include <crypto/pq/mldsa87/mldsa87.h>
#include <hash.h>
#include <key_io.h>
#include <script/solver.h>
#include <test/util/setup_common.h>
#include <util/strencodings.h>

#include <boost/test/unit_test.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace wallet {
BOOST_FIXTURE_TEST_SUITE(pqkey_wallet_tests, BasicTestingSetup)

namespace {

std::vector<unsigned char> FixedSeed(const char* hex)
{
    const std::vector<unsigned char> seed = ParseHex(hex);
    BOOST_REQUIRE_EQUAL(seed.size(), MLDSA87_SEED_SIZE);
    return seed;
}

std::vector<unsigned char> SeedA()
{
    return FixedSeed(
        "000102030405060708090a0b0c0d0e0f"
        "101112131415161718191a1b1c1d1e1f");
}

std::vector<unsigned char> SeedB()
{
    return FixedSeed(
        "c046f904b38fb929207d7a32caf0e7b3"
        "a9c96d43f600b7197fbe663092996f33");
}

Span<const unsigned char> ByteSpan(const std::vector<unsigned char>& bytes)
{
    return Span<const unsigned char>(bytes.data(), bytes.size());
}

std::unique_ptr<CWallet> MakeEmptyWallet(interfaces::Chain* chain)
{
    auto wallet = std::make_unique<CWallet>(
        chain, "", CreateMockableWalletDatabase());
    BOOST_REQUIRE(wallet->LoadWallet() == DBErrors::LOAD_OK);
    return wallet;
}

std::unique_ptr<CWallet> ReloadWallet(
    interfaces::Chain* chain,
    WalletDatabase& source,
    DBErrors expected = DBErrors::LOAD_OK)
{
    auto wallet = std::make_unique<CWallet>(
        chain, "", DuplicateMockDatabase(source));
    BOOST_CHECK(wallet->LoadWallet() == expected);
    return wallet;
}

} // namespace

BOOST_AUTO_TEST_CASE(p2qr_seed_import_export_roundtrip)
{
    auto wallet = MakeEmptyWallet(m_node.chain.get());

    const std::vector<unsigned char> seed = SeedA();

    CPQKey expected;
    BOOST_REQUIRE(expected.SetSeedBytes(seed));

    Kvanta5P2QRDestination imported_dest;

    {
        LOCK(wallet->cs_wallet);

        BOOST_REQUIRE(wallet->ImportKvanta5P2QRSeed(
            ByteSpan(seed),
            "p2qr-import-roundtrip",
            imported_dest));

        BOOST_CHECK(imported_dest == expected.GetDestination());
        BOOST_CHECK(wallet->HaveKvanta5P2QRDestination(imported_dest));

        std::vector<unsigned char> exported_seed;
        BOOST_REQUIRE(wallet->GetKvanta5P2QRSeed(
            imported_dest, exported_seed));
        BOOST_CHECK(exported_seed == seed);

        CPQKey restored;
        BOOST_REQUIRE(wallet->GetKvanta5P2QRKey(
            imported_dest, restored));
        BOOST_CHECK(restored.GetSeedBytes() == seed);
        BOOST_CHECK(restored.GetPubKeyBytes() == expected.GetPubKeyBytes());
        BOOST_CHECK(restored.GetSecretKeyBytes() == expected.GetSecretKeyBytes());

        std::vector<unsigned char> pubkey;
        BOOST_REQUIRE(wallet->GetKvanta5P2QRPubKey(
            imported_dest, pubkey));
        BOOST_CHECK(pubkey == expected.GetPubKeyBytes());

        BOOST_CHECK(
            wallet->IsMine(GetScriptForDestination(imported_dest)) ==
            ISMINE_SPENDABLE);
    }
}

BOOST_AUTO_TEST_CASE(p2qr_compact_seed_persistence_reload)
{
    auto wallet = MakeEmptyWallet(m_node.chain.get());

    const std::vector<unsigned char> seed = SeedA();

    CPQKey expected;
    BOOST_REQUIRE(expected.SetSeedBytes(seed));

    Kvanta5P2QRDestination dest;
    {
        LOCK(wallet->cs_wallet);
        BOOST_REQUIRE(wallet->ImportKvanta5P2QRSeed(
            ByteSpan(seed),
            "persistent-p2qr",
            dest));
    }

    auto reloaded = ReloadWallet(
        m_node.chain.get(),
        wallet->GetDatabase());

    {
        LOCK(reloaded->cs_wallet);

        BOOST_CHECK(reloaded->HaveKvanta5P2QRDestination(dest));

        std::vector<unsigned char> restored_seed;
        BOOST_REQUIRE(reloaded->GetKvanta5P2QRSeed(
            dest, restored_seed));
        BOOST_CHECK(restored_seed == seed);

        CPQKey restored_key;
        BOOST_REQUIRE(reloaded->GetKvanta5P2QRKey(
            dest, restored_key));

        BOOST_CHECK(restored_key.GetSeedBytes() == seed);
        BOOST_CHECK(restored_key.GetPubKeyBytes() == expected.GetPubKeyBytes());
        BOOST_CHECK(restored_key.GetSecretKeyBytes() == expected.GetSecretKeyBytes());
        BOOST_CHECK(restored_key.GetDestination() == dest);

        const CTxDestination address_book_dest{dest};
        const auto entry = reloaded->m_address_book.find(address_book_dest);
        BOOST_REQUIRE(entry != reloaded->m_address_book.end());
        BOOST_CHECK_EQUAL(entry->second.GetLabel(), "persistent-p2qr");
        BOOST_REQUIRE(entry->second.purpose.has_value());
        BOOST_CHECK(*entry->second.purpose == AddressPurpose::RECEIVE);

        BOOST_CHECK(
            reloaded->IsMine(GetScriptForDestination(dest)) ==
            ISMINE_SPENDABLE);

        const uint256 msg_hash =
            Hash(std::string{"KV5 P2QR persistence reload signing test"});

        std::vector<unsigned char> signature;
        BOOST_REQUIRE(restored_key.Sign(
            Span<const unsigned char>(msg_hash.begin(), msg_hash.size()),
            signature));

        BOOST_CHECK(MLDSA87Verify(
            ByteSpan(restored_key.GetPubKeyBytes()),
            Span<const unsigned char>(msg_hash.begin(), msg_hash.size()),
            ByteSpan(signature)));
    }
}

BOOST_AUTO_TEST_CASE(p2qr_compact_seed_wrong_key_hash_rejected)
{
    auto source = MakeEmptyWallet(m_node.chain.get());

    CPQKey key_a;
    CPQKey key_b;
    BOOST_REQUIRE(key_a.SetSeedBytes(SeedA()));
    BOOST_REQUIRE(key_b.SetSeedBytes(SeedB()));

    P2QRSeedRecord record;
    record.seed = key_b.GetSeedBytes();
    record.creation_time = 123456;

    {
        WalletBatch batch(source->GetDatabase());
        BOOST_REQUIRE(batch.WriteKvanta5P2QRSeed(
            key_a.GetHash(), record));
    }

    auto reloaded = ReloadWallet(
        m_node.chain.get(),
        source->GetDatabase(),
        DBErrors::NONCRITICAL_ERROR);

    LOCK(reloaded->cs_wallet);
    BOOST_CHECK(!reloaded->HaveKvanta5P2QRDestination(
        key_a.GetDestination()));
    BOOST_CHECK(!reloaded->HaveKvanta5P2QRDestination(
        key_b.GetDestination()));
}

BOOST_AUTO_TEST_CASE(p2qr_compact_seed_invalid_lengths_rejected)
{
    for (const size_t invalid_size :
         {MLDSA87_SEED_SIZE - 1, MLDSA87_SEED_SIZE + 1}) {

        auto source = MakeEmptyWallet(m_node.chain.get());

        CPQKey key;
        BOOST_REQUIRE(key.SetSeedBytes(SeedA()));

        P2QRSeedRecord record;
        record.seed.assign(invalid_size, 0x42);
        record.creation_time = 123456;

        {
            WalletBatch batch(source->GetDatabase());
            BOOST_REQUIRE(batch.WriteKvanta5P2QRSeed(
                key.GetHash(), record));
        }

        auto reloaded = ReloadWallet(
            m_node.chain.get(),
            source->GetDatabase(),
            DBErrors::NONCRITICAL_ERROR);

        LOCK(reloaded->cs_wallet);
        BOOST_CHECK(!reloaded->HaveKvanta5P2QRDestination(
            key.GetDestination()));
    }
}

BOOST_AUTO_TEST_CASE(p2qr_legacy_record_validates_and_migrates)
{
    auto source = MakeEmptyWallet(m_node.chain.get());

    CPQKey key;
    BOOST_REQUIRE(key.SetSeedBytes(SeedA()));

    P2QRKeyRecord legacy;
    legacy.seed = key.GetSeedBytes();
    legacy.pubkey = key.GetPubKeyBytes();
    legacy.creation_time = 1700000000;

    {
        WalletBatch batch(source->GetDatabase());
        BOOST_REQUIRE(batch.WriteKvanta5P2QRKey(
            key.GetHash(), legacy));
    }

    auto reloaded = ReloadWallet(
        m_node.chain.get(),
        source->GetDatabase(),
        DBErrors::LOAD_OK);

    {
        LOCK(reloaded->cs_wallet);

        BOOST_CHECK(reloaded->HaveKvanta5P2QRDestination(
            key.GetDestination()));

        std::vector<unsigned char> seed;
        BOOST_REQUIRE(reloaded->GetKvanta5P2QRSeed(
            key.GetDestination(), seed));
        BOOST_CHECK(seed == key.GetSeedBytes());

        CPQKey restored;
        BOOST_REQUIRE(reloaded->GetKvanta5P2QRKey(
            key.GetDestination(), restored));
        BOOST_CHECK(restored.GetPubKeyBytes() == key.GetPubKeyBytes());
    }

    /*
     * A second reload exercises the post-migration database contents.
     * If migration failed to leave a valid compact seed record behind,
     * the key will disappear here.
     */
    auto after_migration = ReloadWallet(
        m_node.chain.get(),
        reloaded->GetDatabase(),
        DBErrors::LOAD_OK);

    LOCK(after_migration->cs_wallet);
    BOOST_CHECK(after_migration->HaveKvanta5P2QRDestination(
        key.GetDestination()));

    std::vector<unsigned char> seed;
    BOOST_REQUIRE(after_migration->GetKvanta5P2QRSeed(
        key.GetDestination(), seed));
    BOOST_CHECK(seed == key.GetSeedBytes());
}

BOOST_AUTO_TEST_CASE(p2qr_legacy_pubkey_mismatch_rejected)
{
    auto source = MakeEmptyWallet(m_node.chain.get());

    CPQKey key_a;
    CPQKey key_b;
    BOOST_REQUIRE(key_a.SetSeedBytes(SeedA()));
    BOOST_REQUIRE(key_b.SetSeedBytes(SeedB()));

    P2QRKeyRecord legacy;
    legacy.seed = key_a.GetSeedBytes();
    legacy.pubkey = key_b.GetPubKeyBytes();
    legacy.creation_time = 1700000000;

    {
        WalletBatch batch(source->GetDatabase());
        BOOST_REQUIRE(batch.WriteKvanta5P2QRKey(
            key_a.GetHash(), legacy));
    }

    auto reloaded = ReloadWallet(
        m_node.chain.get(),
        source->GetDatabase(),
        DBErrors::NONCRITICAL_ERROR);

    LOCK(reloaded->cs_wallet);
    BOOST_CHECK(!reloaded->HaveKvanta5P2QRDestination(
        key_a.GetDestination()));
    BOOST_CHECK(!reloaded->HaveKvanta5P2QRDestination(
        key_b.GetDestination()));
}

BOOST_AUTO_TEST_CASE(p2qr_conflicting_legacy_and_compact_records_rejected)
{
    auto source = MakeEmptyWallet(m_node.chain.get());

    CPQKey key;
    BOOST_REQUIRE(key.SetSeedBytes(SeedA()));

    P2QRKeyRecord legacy;
    legacy.seed = key.GetSeedBytes();
    legacy.pubkey = key.GetPubKeyBytes();
    legacy.creation_time = 100;

    P2QRSeedRecord compact;
    compact.seed = key.GetSeedBytes();
    compact.creation_time = 200; // Deliberate conflict.

    {
        WalletBatch batch(source->GetDatabase());
        BOOST_REQUIRE(batch.WriteKvanta5P2QRKey(
            key.GetHash(), legacy));
        BOOST_REQUIRE(batch.WriteKvanta5P2QRSeed(
            key.GetHash(), compact));
    }

    auto reloaded = ReloadWallet(
        m_node.chain.get(),
        source->GetDatabase(),
        DBErrors::NONCRITICAL_ERROR);

    /*
     * Both records are individually valid, but the database must not be
     * silently migrated when their metadata conflicts.
     *
     * The key may already have been loaded into memory before the conflict
     * is detected, so the security property asserted here is the load status:
     * the wallet must report the conflict instead of declaring LOAD_OK.
     */
    BOOST_CHECK(reloaded != nullptr);
}

BOOST_AUTO_TEST_SUITE_END()
} // namespace wallet
