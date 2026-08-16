// Copyright (c) 2026 The Kvanta5 Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include <wallet/pqkey.h>
#include <wallet/wallet.h>
#include <wallet/walletdb.h>

#include <crypto/pq/mldsa87/mldsa87.h>
#include <test/util/setup_common.h>
#include <util/fs.h>
#include <util/strencodings.h>

#include <boost/test/unit_test.hpp>

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace wallet {
BOOST_FIXTURE_TEST_SUITE(pqkey_wallet_sqlite_tests, BasicTestingSetup)

namespace {

constexpr DatabaseFormat WALLET_FORMAT{DatabaseFormat::SQLITE};

std::vector<unsigned char> DiskSeed()
{
    const auto seed = ParseHex(
        "7a0b77a62d8be8c701dc575f42fb80c4"
        "7ba37d80a3d108b276ca97ab0f73d4ea");
    BOOST_REQUIRE_EQUAL(seed.size(), MLDSA87_SEED_SIZE);
    return seed;
}

fs::path WalletPath(
    const fs::path& root,
    const std::string& test_name)
{
    const fs::path dir =
        root / fs::PathFromString(test_name);
    fs::create_directories(dir);
    return dir;
}

std::unique_ptr<WalletDatabase> OpenDatabase(
    const fs::path& path,
    bool create)
{
    DatabaseOptions options;
    options.require_format = WALLET_FORMAT;
    options.require_create = create;
    options.require_existing = !create;

    DatabaseStatus status;
    bilingual_str error;

    auto database = MakeDatabase(path, options, status, error);

    BOOST_REQUIRE_MESSAGE(
        status == DatabaseStatus::SUCCESS,
        "MakeDatabase failed for " << fs::PathToString(path)
        << " format=SQLite"
        << " error=" << error.original);

    BOOST_REQUIRE(database != nullptr);
    return database;
}

std::unique_ptr<CWallet> LoadDiskWallet(
    interfaces::Chain* chain,
    const fs::path& path,
    DBErrors expected)
{
    auto wallet = std::make_unique<CWallet>(
        chain,
        "",
        OpenDatabase(path, /*create=*/false));

    BOOST_CHECK(wallet->LoadWallet() == expected);
    return wallet;
}

} // namespace

BOOST_AUTO_TEST_CASE(p2qr_sqlite_compact_seed_reopen)
{
    const fs::path path =
        WalletPath(m_path_root, "p2qr_sqlite_reopen");

    const std::vector<unsigned char> seed = DiskSeed();

    CPQKey expected;
    BOOST_REQUIRE(expected.SetSeedBytes(seed));
    const Kvanta5P2QRDestination dest = expected.GetDestination();

    {
        auto database =
            OpenDatabase(path, /*create=*/true);

        auto wallet = std::make_unique<CWallet>(
            m_node.chain.get(), "", std::move(database));

        BOOST_REQUIRE(
            wallet->LoadWallet() == DBErrors::LOAD_OK);

        {
            LOCK(wallet->cs_wallet);

            Kvanta5P2QRDestination imported;
            BOOST_REQUIRE(wallet->ImportKvanta5P2QRSeed(
                Span<const unsigned char>(
                    seed.data(), seed.size()),
                "sqlite-reopen",
                imported));

            BOOST_CHECK(imported == dest);
        }

        wallet->Flush();
    }

    {
        auto reloaded = LoadDiskWallet(
            m_node.chain.get(),
            path,
            DBErrors::LOAD_OK);

        LOCK(reloaded->cs_wallet);

        BOOST_CHECK(
            reloaded->HaveKvanta5P2QRDestination(dest));

        std::vector<unsigned char> restored_seed;
        BOOST_REQUIRE(reloaded->GetKvanta5P2QRSeed(
            dest, restored_seed));
        BOOST_CHECK(restored_seed == seed);

        CPQKey restored;
        BOOST_REQUIRE(reloaded->GetKvanta5P2QRKey(
            dest, restored));

        BOOST_CHECK(
            restored.GetPubKeyBytes() ==
            expected.GetPubKeyBytes());
        BOOST_CHECK(
            restored.GetSecretKeyBytes() ==
            expected.GetSecretKeyBytes());
        BOOST_CHECK(restored.GetDestination() == dest);
    }
}

BOOST_AUTO_TEST_CASE(p2qr_sqlite_wrong_key_hash_rejected)
{
    const fs::path path =
        WalletPath(m_path_root, "p2qr_sqlite_wrong_hash");

    CPQKey key_a;
    CPQKey key_b;

    BOOST_REQUIRE(key_a.SetSeedBytes(DiskSeed()));

    std::vector<unsigned char> seed_b(
        MLDSA87_SEED_SIZE, 0x5c);
    BOOST_REQUIRE(key_b.SetSeedBytes(seed_b));

    {
        auto database =
            OpenDatabase(path, /*create=*/true);

        P2QRSeedRecord record;
        record.seed = key_b.GetSeedBytes();
        record.creation_time = 1700000001;

        WalletBatch batch(*database);
        BOOST_REQUIRE(batch.WriteKvanta5P2QRSeed(
            key_a.GetHash(), record));

        database->Flush();
    }

    auto reloaded = LoadDiskWallet(
        m_node.chain.get(),
        path,
        DBErrors::NONCRITICAL_ERROR);

    LOCK(reloaded->cs_wallet);

    BOOST_CHECK(
        !reloaded->HaveKvanta5P2QRDestination(
            key_a.GetDestination()));
    BOOST_CHECK(
        !reloaded->HaveKvanta5P2QRDestination(
            key_b.GetDestination()));
}

BOOST_AUTO_TEST_CASE(p2qr_sqlite_truncated_compact_record_is_corrupt)
{
    const fs::path path =
        WalletPath(m_path_root, "p2qr_sqlite_truncated");

    CPQKey key;
    BOOST_REQUIRE(key.SetSeedBytes(DiskSeed()));

    {
        auto database =
            OpenDatabase(path, /*create=*/true);

        /*
         * Write only the serialized seed vector under the real
         * kvanta5p2qrseed key. P2QRSeedRecord also requires the
         * creation_time field, so wallet load must treat this as a
         * truncated/corrupt serialized value rather than silently
         * manufacturing metadata.
         */
        auto batch = database->MakeBatch();
        BOOST_REQUIRE(batch != nullptr);

        BOOST_REQUIRE(batch->Write(
            std::make_pair(
                DBKeys::KVANTA5_P2QR_SEED,
                key.GetHash()),
            key.GetSeedBytes()));

        batch->Flush();
        batch->Close();
        database->Flush();
    }

    auto reloaded = LoadDiskWallet(
        m_node.chain.get(),
        path,
        DBErrors::CORRUPT);

    LOCK(reloaded->cs_wallet);
    BOOST_CHECK(
        !reloaded->HaveKvanta5P2QRDestination(
            key.GetDestination()));
}

BOOST_AUTO_TEST_CASE(p2qr_sqlite_legacy_full_key_migration_persists)
{
    const fs::path path =
        WalletPath(m_path_root, "p2qr_sqlite_legacy_migration");

    CPQKey key;
    BOOST_REQUIRE(key.SetSeedBytes(DiskSeed()));

    /*
     * Kvanta5 v1.0.0-style P2QR record:
     * seed + full derived ML-DSA-87 public-key material.
     *
     * "Legacy" here refers to the old P2QR record format, not BerkeleyDB.
     * Both the legacy and compact formats are SQLite wallet records.
     */
    P2QRKeyRecord legacy;
    legacy.seed = key.GetSeedBytes();
    legacy.pubkey = key.GetPubKeyBytes();
    legacy.creation_time = 1700000002;

    {
        auto database =
            OpenDatabase(path, /*create=*/true);

        WalletBatch batch(*database);
        BOOST_REQUIRE(batch.WriteKvanta5P2QRKey(
            key.GetHash(), legacy));

        database->Flush();
    }

    /*
     * First SQLite load validates the legacy full-key record and performs
     * the transactional migration to the v1.0.2+ compact seed-only record.
     */
    {
        auto migrated = LoadDiskWallet(
            m_node.chain.get(),
            path,
            DBErrors::LOAD_OK);

        LOCK(migrated->cs_wallet);
        BOOST_CHECK(
            migrated->HaveKvanta5P2QRDestination(
                key.GetDestination()));

        std::vector<unsigned char> restored_seed;
        BOOST_REQUIRE(migrated->GetKvanta5P2QRSeed(
            key.GetDestination(),
            restored_seed));
        BOOST_CHECK(restored_seed == key.GetSeedBytes());
    }

    /*
     * Inspect the physical SQLite database after migration:
     * the compact seed record must exist and the old full-key record
     * must be gone.
     */
    {
        auto database =
            OpenDatabase(path, /*create=*/false);
        auto batch = database->MakeBatch(false);

        BOOST_REQUIRE(batch != nullptr);

        BOOST_CHECK(batch->Exists(
            std::make_pair(
                DBKeys::KVANTA5_P2QR_SEED,
                key.GetHash())));

        BOOST_CHECK(!batch->Exists(
            std::make_pair(
                DBKeys::KVANTA5_P2QR_KEY,
                key.GetHash())));
    }

    /*
     * Migration must leave a pre-migration backup beside the SQLite wallet.
     */
    const fs::path backup_path =
        path / "legacywalletbackup.dat";

    BOOST_CHECK_MESSAGE(
        fs::exists(backup_path),
        "Missing SQLite P2QR pre-migration backup at "
            << fs::PathToString(backup_path));

    /*
     * Second reopen proves the migrated SQLite wallet is independently
     * loadable using only the compact seed record.
     */
    {
        auto reloaded = LoadDiskWallet(
            m_node.chain.get(),
            path,
            DBErrors::LOAD_OK);

        LOCK(reloaded->cs_wallet);

        BOOST_CHECK(
            reloaded->HaveKvanta5P2QRDestination(
                key.GetDestination()));

        std::vector<unsigned char> seed;
        BOOST_REQUIRE(reloaded->GetKvanta5P2QRSeed(
            key.GetDestination(), seed));
        BOOST_CHECK(seed == key.GetSeedBytes());

        CPQKey restored;
        BOOST_REQUIRE(reloaded->GetKvanta5P2QRKey(
            key.GetDestination(), restored));

        BOOST_CHECK(
            restored.GetPubKeyBytes() ==
            key.GetPubKeyBytes());
        BOOST_CHECK(
            restored.GetSecretKeyBytes() ==
            key.GetSecretKeyBytes());
        BOOST_CHECK(
            restored.GetDestination() ==
            key.GetDestination());
    }
}

BOOST_AUTO_TEST_SUITE_END()
} // namespace wallet
