#!/usr/bin/env python3
# Copyright (c) 2026 The Kvanta5 Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""Kvanta5 wallet backwards-compatibility tests.

Kvanta5 Core 1.0.0 through 1.0.2 stored P2QR signing material in
SQLite P2QRKeyRecord records:

    seed + full ML-DSA-87 public key + creation time

Kvanta5 Core 1.0.3 introduced compact P2QRSeedRecord storage:

    seed + creation time

This test exercises that real Kvanta5 compatibility boundary. It does
not test inherited Bitcoin non-descriptor wallets, Berkeley DB, Bitcoin
HD keypools, SegWit/Taproot wallet types, or Bitcoin migratewallet
semantics.
"""

try:
    import sqlite3
except ImportError:
    pass

import hashlib

from test_framework.test_framework import Kvanta5TestFramework
from test_framework.util import (
    assert_equal,
    assert_raises_rpc_error,
)


class WalletBackwardsCompatibilityTest(Kvanta5TestFramework):
    LEGACY_RECORD_TYPE = b"kvanta5p2qrkey"
    COMPACT_RECORD_TYPE = b"kvanta5p2qrseed"

    # Frozen deterministic ML-DSA-87 seed. The resulting key identity must
    # survive conversion from the 1.0.0-1.0.2 full-key record to the current
    # compact seed-only record.
    TEST_SEED = (
        "000102030405060708090a0b0c0d0e0f"
        "101112131415161718191a1b1c1d1e1f"
    )

    WALLET_NAME = "kvanta5_backcompat"

    def add_options(self, parser):
        self.add_wallet_options(parser, descriptors=True, legacy=False)

    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()
        self.skip_if_no_sqlite()
        self.skip_if_no_py_sqlite3()

    @staticmethod
    def compact_size(value):
        if value < 253:
            return bytes([value])
        if value <= 0xFFFF:
            return b"\xfd" + value.to_bytes(2, "little")
        if value <= 0xFFFFFFFF:
            return b"\xfe" + value.to_bytes(4, "little")
        return b"\xff" + value.to_bytes(8, "little")

    @classmethod
    def serialize_bytes(cls, value):
        return cls.compact_size(len(value)) + value

    @classmethod
    def record_prefix(cls, record_type):
        return cls.serialize_bytes(record_type)

    @staticmethod
    def sha256_file(path):
        hasher = hashlib.sha256()
        with open(path, "rb") as f:
            for chunk in iter(lambda: f.read(1024 * 1024), b""):
                hasher.update(chunk)
        return hasher.hexdigest()

    @classmethod
    def find_records(cls, conn, record_type):
        prefix = cls.record_prefix(record_type)
        matches = []

        for key, value in conn.execute("SELECT * FROM main"):
            key = bytes(key)
            value = bytes(value)

            if key.startswith(prefix):
                matches.append((key, value))

        return matches

    def convert_compact_record_to_legacy_fixture(
        self,
        wallet_db,
        seed,
        pubkey,
    ):
        """Rewrite one real compact P2QR row into the exact old record shape.

        The database-key hash suffix is retained byte-for-byte from the real
        P2QRSeedRecord written by Kvanta5. Only the record type and serialized
        value are transformed:

            kvanta5p2qrseed:
                vector(seed) + int64(creation_time)

        becomes:

            kvanta5p2qrkey:
                vector(seed) + vector(pubkey) + int64(creation_time)

        This matches the P2QRKeyRecord serialization used by Kvanta5
        1.0.0, 1.0.1, and 1.0.2.
        """

        compact_prefix = self.record_prefix(self.COMPACT_RECORD_TYPE)
        legacy_prefix = self.record_prefix(self.LEGACY_RECORD_TYPE)

        seed_serialized = self.serialize_bytes(seed)

        conn = sqlite3.connect(wallet_db)

        try:
            compact_records = self.find_records(
                conn,
                self.COMPACT_RECORD_TYPE,
            )
            legacy_records = self.find_records(
                conn,
                self.LEGACY_RECORD_TYPE,
            )

            assert_equal(len(compact_records), 1)
            assert_equal(len(legacy_records), 0)

            compact_key, compact_value = compact_records[0]

            assert compact_key.startswith(compact_prefix)

            # std::pair<string, uint256> leaves the serialized uint256 after
            # the serialized record-type string.
            key_hash_serialized = compact_key[len(compact_prefix):]
            assert_equal(len(key_hash_serialized), 32)

            # P2QRSeedRecord is exactly:
            #     vector<unsigned char> seed
            #     int64_t creation_time
            assert compact_value.startswith(seed_serialized)

            creation_time_serialized = compact_value[
                len(seed_serialized):
            ]
            assert_equal(len(creation_time_serialized), 8)

            legacy_key = legacy_prefix + key_hash_serialized

            legacy_value = (
                seed_serialized
                + self.serialize_bytes(pubkey)
                + creation_time_serialized
            )

            with conn:
                conn.execute(
                    "DELETE FROM main WHERE key = ?",
                    (compact_key,),
                )
                conn.execute(
                    "INSERT INTO main VALUES(?, ?)",
                    (legacy_key, legacy_value),
                )

            assert_equal(
                len(
                    self.find_records(
                        conn,
                        self.COMPACT_RECORD_TYPE,
                    )
                ),
                0,
            )
            assert_equal(
                len(
                    self.find_records(
                        conn,
                        self.LEGACY_RECORD_TYPE,
                    )
                ),
                1,
            )

            return creation_time_serialized

        finally:
            conn.close()

    def assert_migrated_database(
        self,
        wallet_db,
        seed,
        creation_time_serialized,
    ):
        """Verify the old record is gone and compact storage is exact."""

        conn = sqlite3.connect(wallet_db)

        try:
            legacy_records = self.find_records(
                conn,
                self.LEGACY_RECORD_TYPE,
            )
            compact_records = self.find_records(
                conn,
                self.COMPACT_RECORD_TYPE,
            )

            assert_equal(len(legacy_records), 0)
            assert_equal(len(compact_records), 1)

            _, compact_value = compact_records[0]

            expected_value = (
                self.serialize_bytes(seed)
                + creation_time_serialized
            )

            assert_equal(compact_value, expected_value)

        finally:
            conn.close()

    def run_test(self):
        node = self.nodes[0]

        self.log.info(
            "Verify unsupported inherited Bitcoin non-descriptor creation "
            "remains rejected"
        )
        assert_raises_rpc_error(
            -4,
            "Kvanta5 supports descriptor wallets only",
            node.createwallet,
            wallet_name="unsupported_non_descriptor",
            descriptors=False,
        )

        self.log.info(
            "Create supported Kvanta5 SQLite descriptor wallet"
        )
        node.createwallet(
            wallet_name=self.WALLET_NAME,
            descriptors=True,
        )
        wallet = node.get_wallet_rpc(self.WALLET_NAME)

        wallet_info = wallet.getwalletinfo()
        assert_equal(wallet_info["format"], "sqlite")
        assert_equal(wallet_info["descriptors"], True)
        assert_equal(wallet_info["keypoolsize"], 0)

        self.log.info(
            "Create deterministic current-format P2QR identity"
        )
        imported = wallet.importkvanta5p2qrseed(
            self.TEST_SEED,
            "backcompat",
        )

        assert_equal(
            imported["type"],
            "kvanta5_p2qr_seed_import",
        )
        assert_equal(imported["seed"], self.TEST_SEED)

        address = imported["address"]

        address_info = wallet.getaddressinfo(address)
        assert_equal(address_info["ismine"], True)
        assert_equal(address_info["iskvanta5p2qr"], True)
        assert_equal(address_info["p2qr_type"], "single")
        assert_equal(address_info["labels"], ["backcompat"])

        assert "p2qr_pubkey" in address_info
        pubkey = bytes.fromhex(address_info["p2qr_pubkey"])
        seed = bytes.fromhex(self.TEST_SEED)

        # Preserve the exact identity information before rewriting the
        # physical database record.
        expected_program = address_info["p2qr_program"]
        expected_pubkey = address_info["p2qr_pubkey"]
        expected_script = address_info["scriptPubKey"]

        dumped = wallet.dumpkvanta5p2qrseed(address)
        assert_equal(dumped["seed"], self.TEST_SEED)

        wallet.unloadwallet()

        wallet_dir = node.wallets_path / self.WALLET_NAME
        wallet_db = wallet_dir / self.wallet_data_filename
        backup_path = wallet_dir / "legacywalletbackup.dat"

        assert wallet_db.exists()
        assert not backup_path.exists()

        self.log.info(
            "Rewrite compact row as authentic Kvanta5 1.0.0-1.0.2 "
            "P2QRKeyRecord"
        )
        creation_time_serialized = (
            self.convert_compact_record_to_legacy_fixture(
                wallet_db,
                seed,
                pubkey,
            )
        )

        self.log.info(
            "Load old Kvanta5 P2QR record and trigger current migration"
        )
        node.loadwallet(self.WALLET_NAME)
        wallet = node.get_wallet_rpc(self.WALLET_NAME)

        wallet_info = wallet.getwalletinfo()
        assert_equal(wallet_info["format"], "sqlite")
        assert_equal(wallet_info["descriptors"], True)

        migrated_info = wallet.getaddressinfo(address)

        # The migration must preserve the exact ML-DSA/P2QR identity.
        assert_equal(migrated_info["ismine"], True)
        assert_equal(migrated_info["solvable"], True)
        assert_equal(migrated_info["iskvanta5p2qr"], True)
        assert_equal(migrated_info["p2qr_type"], "single")
        assert_equal(
            migrated_info["p2qr_program"],
            expected_program,
        )
        assert_equal(
            migrated_info["p2qr_pubkey"],
            expected_pubkey,
        )
        assert_equal(
            migrated_info["scriptPubKey"],
            expected_script,
        )
        assert_equal(
            migrated_info["labels"],
            ["backcompat"],
        )

        migrated_seed = wallet.dumpkvanta5p2qrseed(address)
        assert_equal(migrated_seed["seed"], self.TEST_SEED)
        assert_equal(migrated_seed["address"], address)

        # The backup is part of the Kvanta5 P2QR migration contract.
        assert backup_path.exists()
        backup_hash = self.sha256_file(backup_path)

        wallet.unloadwallet()

        self.log.info(
            "Verify physical SQLite migration result"
        )
        self.assert_migrated_database(
            wallet_db,
            seed,
            creation_time_serialized,
        )

        self.log.info(
            "Reopen migrated wallet and verify migration is idempotent"
        )
        node.loadwallet(self.WALLET_NAME)
        wallet = node.get_wallet_rpc(self.WALLET_NAME)

        reopened_info = wallet.getaddressinfo(address)
        assert_equal(reopened_info["ismine"], True)
        assert_equal(reopened_info["iskvanta5p2qr"], True)
        assert_equal(
            reopened_info["p2qr_program"],
            expected_program,
        )
        assert_equal(
            reopened_info["p2qr_pubkey"],
            expected_pubkey,
        )

        # A second load must not replace or mutate the pre-migration backup.
        assert_equal(
            self.sha256_file(backup_path),
            backup_hash,
        )

        self.log.info(
            "Verify migrated P2QR wallet detects live payment without rescan"
        )

        # Kvanta5 regtest block 1 is the mandatory Dev Fund issuance.
        # Block 2 is therefore the first normal miner coinbase.
        blocks = self.generatetoaddress(
            node,
            2,
            address,
        )

        assert_equal(len(blocks), 2)

        wallet_info = wallet.getwalletinfo()
        assert_equal(wallet_info["txcount"], 1)
        assert_equal(wallet_info["immature_balance"], 50)

        block_two = node.getblock(blocks[1], 2)
        coinbase_txid = block_two["tx"][0]["txid"]

        received = wallet.gettransaction(coinbase_txid)
        assert_equal(received["amount"], 50)
        assert_equal(len(received["details"]), 1)
        assert_equal(received["details"][0]["address"], address)
        assert_equal(received["details"][0]["category"], "immature")

        self.log.info(
            "Kvanta5 backwards-compatibility contract verified"
        )


if __name__ == "__main__":
    WalletBackwardsCompatibilityTest(__file__).main()
