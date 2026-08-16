#!/usr/bin/env python3
# Copyright (c) 2015-2026 The Bitcoin Core developers
# Copyright (c) 2026 The Kvanta5 Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test Kvanta5 descriptor-wallet policy and SQLite persistence."""

try:
    import sqlite3
except ImportError:
    pass

import concurrent.futures

from test_framework.authproxy import JSONRPCException
from test_framework.blocktools import COINBASE_MATURITY
from test_framework.test_framework import Kvanta5TestFramework
from test_framework.util import (
    assert_equal,
    assert_raises_rpc_error,
)


class WalletDescriptorTest(Kvanta5TestFramework):
    def add_options(self, parser):
        self.add_wallet_options(parser, legacy=False)

    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()
        self.skip_if_no_sqlite()
        self.skip_if_no_py_sqlite3()

    def assert_non_p2qr_types_rejected(self, wallet):
        """Inherited Bitcoin address types must not become active Kvanta5 destinations."""
        for address_type in ("legacy", "p2sh-segwit", "bech32", "bech32m"):
            try:
                wallet.getnewaddress("", address_type)
            except JSONRPCException as exc:
                message = exc.error["message"]
                assert "non-P2QR" in message
                assert "p2qr" in message
            else:
                raise AssertionError(
                    f"getnewaddress unexpectedly accepted {address_type}"
                )

            try:
                wallet.getrawchangeaddress(address_type)
            except JSONRPCException as exc:
                message = exc.error["message"]
                assert "non-P2QR" in message
                assert "p2qr" in message
            else:
                raise AssertionError(
                    f"getrawchangeaddress unexpectedly accepted {address_type}"
                )

    def test_concurrent_writes(self):
        """Exercise concurrent SQLite writes using actual P2QR wallet records."""
        self.log.info("Test SQLite concurrent P2QR writes")

        self.restart_node(0, extra_args=["-unsafesqlitesync=0"])
        self.nodes[0].createwallet(
            wallet_name="concurrency",
            descriptors=True,
        )
        wallet = self.nodes[0].get_wallet_rpc("concurrency")

        def generate_p2qr_addresses():
            return [
                wallet.getnewaddress("", "p2qr")
                for _ in range(256)
            ]

        with concurrent.futures.ThreadPoolExecutor(max_workers=1) as thread:
            writer = thread.submit(generate_p2qr_addresses)

            # Trigger chain-state work while the wallet is persisting P2QR
            # seed/address records to SQLite.
            self.nodes[0].cli.gettxoutsetinfo()

            addresses = writer.result()

        assert_equal(len(addresses), 256)
        assert_equal(len(set(addresses)), 256)

        wallet.unloadwallet()

        # Reopen the SQLite wallet and make sure persisted P2QR ownership
        # survived the concurrent-write workload.
        self.nodes[0].loadwallet("concurrency")
        wallet = self.nodes[0].get_wallet_rpc("concurrency")

        for address in addresses[::32]:
            info = wallet.getaddressinfo(address)
            assert_equal(info["ismine"], True)
            assert_equal(info["iskvanta5p2qr"], True)

        wallet.unloadwallet()

    def test_unexpected_bitcoin_record_rejected(self):
        self.log.info(
            "Test rejection of unsupported inherited Bitcoin wallet records"
        )

        self.nodes[0].createwallet(
            wallet_name="crashme",
            descriptors=True,
        )
        self.nodes[0].unloadwallet("crashme")

        wallet_db = (
            self.nodes[0].wallets_path
            / "crashme"
            / self.wallet_data_filename
        )

        conn = sqlite3.connect(wallet_db)
        with conn:
            # Inject an inherited Bitcoin "cscript" wallet record.
            #
            # "Legacy" in the loader error for this record means an old
            # Bitcoin wallet record type. It does NOT mean Kvanta5's legacy
            # full-private-key P2QR storage format.
            conn.execute(
                "INSERT INTO main VALUES(?, ?)",
                (b"\x07cscript" + b"\x00" * 20, b"\x00"),
            )
        conn.close()

        assert_raises_rpc_error(
            -4,
            "Unexpected legacy entry in descriptor wallet found.",
            self.nodes[0].loadwallet,
            "crashme",
        )

    def run_test(self):
        self.log.info("Test descriptor-only wallet creation policy")

        assert_raises_rpc_error(
            -4,
            "Kvanta5 supports descriptor wallets only",
            self.nodes[0].createwallet,
            wallet_name="unsupported_non_descriptor",
            descriptors=False,
        )

        self.log.info("Create supported SQLite descriptor wallet")

        self.nodes[0].createwallet(
            wallet_name="desc1",
            descriptors=True,
        )
        wallet = self.nodes[0].get_wallet_rpc("desc1")

        info = wallet.getwalletinfo()
        assert_equal(info["format"], "sqlite")
        assert_equal(info["descriptors"], True)

        # Kvanta5 P2QR does not use Bitcoin Core's HD descriptor keypool.
        assert_equal(info["keypoolsize"], 0)
        assert_equal(info.get("keypoolsize_hd_internal", 0), 0)
        assert "hdseedid" not in info

        self.log.info("Test native P2QR receive address")

        native_receive = wallet.getnewaddress(
            "native-receive",
            "p2qr",
        )
        native_info = wallet.getaddressinfo(native_receive)

        assert_equal(native_info["ismine"], True)
        assert_equal(native_info["solvable"], True)
        assert_equal(native_info["iskvanta5p2qr"], True)

        self.log.info("Test wrapped P2SH-carried P2QR receive address")

        wrapped_receive = wallet.getnewaddress(
            "wrapped-receive",
            "wrapped-p2sh",
        )
        wrapped_info = wallet.getaddressinfo(wrapped_receive)

        assert_equal(wrapped_info["ismine"], True)
        assert_equal(wrapped_info["solvable"], True)
        assert_equal(wrapped_info["iskvanta5p2qr"], True)
        assert_equal(
            wrapped_info["mining_type"],
            "p2sh_carried_kvanta5_p2qr",
        )

        self.log.info("Test native P2QR change address")

        native_change = wallet.getrawchangeaddress("p2qr")
        native_change_info = wallet.getaddressinfo(native_change)

        assert_equal(native_change_info["ismine"], True)
        assert_equal(native_change_info["iskvanta5p2qr"], True)
        assert_equal(native_change_info.get("labels", []), [])

        self.log.info("Test wrapped P2SH-carried P2QR change address")

        wrapped_change = wallet.getrawchangeaddress("wrapped-p2sh")
        wrapped_change_info = wallet.getaddressinfo(wrapped_change)

        assert_equal(wrapped_change_info["ismine"], True)
        assert_equal(wrapped_change_info["iskvanta5p2qr"], True)
        assert_equal(
            wrapped_change_info["mining_type"],
            "p2sh_carried_kvanta5_p2qr",
        )
        assert_equal(wrapped_change_info.get("labels", []), [])

        self.log.info("Test rejection of inherited Bitcoin address types")
        self.assert_non_p2qr_types_rejected(wallet)

        self.log.info("Test P2QR sending and receiving")

        self.nodes[0].createwallet(
            wallet_name="desc2",
            descriptors=True,
        )
        recv_wallet = self.nodes[0].get_wallet_rpc("desc2")

        # Block 1 is Kvanta5's mandatory Dev Fund issuance, so mine through
        # enough normal coinbases to obtain mature spendable P2QR funds.
        self.generatetoaddress(
            self.nodes[0],
            COINBASE_MATURITY + 2,
            wallet.getnewaddress("", "p2qr"),
        )

        receive_address = recv_wallet.getnewaddress(
            "payment",
            "p2qr",
        )

        txid = wallet.sendtoaddress(receive_address, 10)
        assert isinstance(txid, str)
        assert len(txid) == 64

        self.generatetoaddress(
            self.nodes[0],
            1,
            self.p2qr_mining_address,
        )

        assert_equal(recv_wallet.getbalance(), 10)

        self.log.info("Test blank descriptor wallet")

        self.nodes[0].createwallet(
            wallet_name="desc_blank",
            blank=True,
            descriptors=True,
        )
        blank_wallet = self.nodes[0].get_wallet_rpc("desc_blank")

        assert_raises_rpc_error(
            -4,
            "This wallet has no available keys",
            blank_wallet.getnewaddress,
        )

        self.log.info("Test descriptor wallet with private keys disabled")

        self.nodes[0].createwallet(
            wallet_name="desc_no_priv",
            disable_private_keys=True,
            descriptors=True,
        )
        no_priv_wallet = self.nodes[0].get_wallet_rpc("desc_no_priv")

        assert_raises_rpc_error(
            -4,
            "This wallet has no available keys",
            no_priv_wallet.getnewaddress,
        )

        self.test_unexpected_bitcoin_record_rejected()
        self.test_concurrent_writes()


if __name__ == "__main__":
    WalletDescriptorTest(__file__).main()
