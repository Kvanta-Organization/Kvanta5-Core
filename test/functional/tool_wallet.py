#!/usr/bin/env python3
# Copyright (c) 2015-2026 The Bitcoin Core developers
# Copyright (c) 2026 The Kvanta5 Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test kvanta5-wallet."""

import os
import stat
import subprocess
import textwrap

from collections import OrderedDict

from test_framework.test_framework import Kvanta5TestFramework
from test_framework.util import (
    assert_equal,
    assert_greater_than,
    sha256sum_file,
)


class ToolWalletTest(Kvanta5TestFramework):
    # Deterministic ML-DSA-87 seed used for the framework-owned regtest
    # mining/funding identity. This replaces Bitcoin Core's inherited
    # deterministic secp256k1/WIF bootstrap.
    P2QR_COINBASE_SEED = (
        "000102030405060708090a0b0c0d0e0f"
        "101112131415161718191a1b1c1d1e1f"
    )
    def add_options(self, parser):
        self.add_wallet_options(parser)

    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.rpc_timeout = 120

    def init_wallet(self, *, node):
        wallet_name = (
            self.default_wallet_name
            if self.wallet_names is None
            else self.wallet_names[node]
            if node < len(self.wallet_names)
            else False
        )

        if wallet_name is False:
            return

        n = self.nodes[node]

        if wallet_name is not None:
            n.createwallet(
                wallet_name=wallet_name,
                descriptors=self.options.descriptors,
                load_on_startup=True,
            )

        wallet_rpc = n if wallet_name is None else n.get_wallet_rpc(wallet_name)

        result = wallet_rpc.importkvanta5p2qrseed(
            self.P2QR_COINBASE_SEED,
            "coinbase",
        )

        assert_equal(result["type"], "kvanta5_p2qr_seed_import")
        assert_equal(result["seed"], self.P2QR_COINBASE_SEED)

        if node == 0:
            self.p2qr_mining_address = result["address"]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()
        self.skip_if_no_wallet_tool()

    def kvanta5_wallet_process(self, *args):
        default_args = ['-datadir={}'.format(self.nodes[0].datadir_path), '-chain=%s' % self.chain]
        if not self.options.descriptors and 'create' in args:
            default_args.append('-legacy')

        return subprocess.Popen([self.options.kvanta5wallet] + default_args + list(args), stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)

    def assert_raises_tool_error(self, error, *args):
        p = self.kvanta5_wallet_process(*args)
        stdout, stderr = p.communicate()
        assert_equal(stdout, '')
        if isinstance(error, tuple):
            assert_equal(p.poll(), error[0])
            assert error[1] in stderr.strip()
        else:
            assert_equal(p.poll(), 1)
            assert error in stderr.strip()

    def assert_tool_output(self, output, *args):
        p = self.kvanta5_wallet_process(*args)
        stdout, stderr = p.communicate()
        assert_equal(stderr, '')
        assert_equal(stdout, output)
        assert_equal(p.poll(), 0)

    def wallet_shasum(self):
        return sha256sum_file(self.wallet_path).hex()

    def wallet_timestamp(self):
        return os.path.getmtime(self.wallet_path)

    def wallet_permissions(self):
        return oct(os.lstat(self.wallet_path).st_mode)[-3:]

    def log_wallet_timestamp_comparison(self, old, new):
        result = 'unchanged' if new == old else 'increased!'
        self.log.debug('Wallet file timestamp {}'.format(result))

    def get_expected_p2qr_info_output(self, name="", transactions=0, address_book=1):
        wallet_name = self.default_wallet_name if name == "" else name
        descriptors = "yes" if self.options.descriptors else "no"

        return textwrap.dedent(f"""\
            Wallet info
            ===========
            Name: {wallet_name}
            Format: sqlite
            Descriptors: {descriptors}
            Encrypted: no
            HD (hd seed available): no
            Keypool Size: 0
            Transactions: {transactions}
            Address Book: {address_book}
        """)

    def get_expected_info_output(self, name="", transactions=0, keypool=2, address=0, imported_privs=0):
        wallet_name = self.default_wallet_name if name == "" else name
        if self.options.descriptors:
            output_types = 4  # p2pkh, p2sh, segwit, bech32m
            return textwrap.dedent('''\
                Wallet info
                ===========
                Name: %s
                Format: sqlite
                Descriptors: yes
                Encrypted: no
                HD (hd seed available): yes
                Keypool Size: %d
                Transactions: %d
                Address Book: %d
            ''' % (wallet_name, keypool * output_types, transactions, imported_privs * 3 + address))
        else:
            output_types = 3  # p2pkh, p2sh, segwit. Legacy wallets do not support bech32m.
            return textwrap.dedent('''\
                Wallet info
                ===========
                Name: %s
                Format: sqlite
                Descriptors: no
                Encrypted: no
                HD (hd seed available): yes
                Keypool Size: %d
                Transactions: %d
                Address Book: %d
            ''' % (wallet_name, keypool, transactions, (address + imported_privs) * output_types))

    def read_dump(self, filename):
        dump = OrderedDict()
        with open(filename, "r", encoding="utf8") as f:
            for row in f:
                row = row.strip()
                key, value = row.split(',')
                dump[key] = value
        return dump

    def assert_is_sqlite(self, filename):
        with open(filename, 'rb') as f:
            file_magic = f.read(16)
            assert file_magic == b'SQLite format 3\x00'

    def write_dump(self, dump, filename, magic=None, skip_checksum=False):
        if magic is None:
            magic = "KVANTA5_CORE_WALLET_DUMP"
        with open(filename, "w", encoding="utf8") as f:
            row = ",".join([magic, dump[magic]]) + "\n"
            f.write(row)
            for k, v in dump.items():
                if k == magic or k == "checksum":
                    continue
                row = ",".join([k, v]) + "\n"
                f.write(row)
            if not skip_checksum:
                row = ",".join(["checksum", dump["checksum"]]) + "\n"
                f.write(row)

    def assert_dump(self, expected, received):
        assert_equal(len(expected), len(received))
        for k, v in expected.items():
            assert_equal(v, received[k])

    def do_tool_createfromdump(self, wallet_name, dumpfile, file_format=None):
        dumppath = self.nodes[0].datadir_path / dumpfile
        rt_dumppath = self.nodes[0].datadir_path / "rt-{}.dump".format(wallet_name)

        dump_data = self.read_dump(dumppath)

        args = ["-wallet={}".format(wallet_name),
                "-dumpfile={}".format(dumppath)]
        if file_format is not None:
            args.append("-format={}".format(file_format))
        args.append("createfromdump")

        load_output = ""
        if file_format is not None and file_format != dump_data["format"]:
            load_output += "Warning: Dumpfile wallet format \"{}\" does not match command line specified format \"{}\".\n".format(dump_data["format"], file_format)
        self.assert_tool_output(load_output, *args)
        assert (self.nodes[0].wallets_path / wallet_name).is_dir()

        self.assert_tool_output("The dumpfile may contain private keys. To ensure the safety of your Kvanta5, do not share the dumpfile.\n", '-wallet={}'.format(wallet_name), '-dumpfile={}'.format(rt_dumppath), 'dump')

        rt_dump_data = self.read_dump(rt_dumppath)
        assert_equal(rt_dump_data["format"], "sqlite")
        wallet_dat = self.nodes[0].wallets_path / wallet_name / "wallet.dat"
        self.assert_is_sqlite(wallet_dat)

    def test_invalid_tool_commands_and_args(self):
        self.log.info('Testing that various invalid commands raise with specific error messages')
        self.assert_raises_tool_error("Error parsing command line arguments: Invalid command 'foo'", 'foo')
        # `kvanta5-wallet help` raises an error. Use `kvanta5-wallet -help`.
        self.assert_raises_tool_error("Error parsing command line arguments: Invalid command 'help'", 'help')
        self.assert_raises_tool_error('Error: Additional arguments provided (create). Methods do not take arguments. Please refer to `-help`.', 'info', 'create')
        self.assert_raises_tool_error('Error parsing command line arguments: Invalid parameter -foo', '-foo')
        self.assert_raises_tool_error('No method provided. Run `kvanta5-wallet -help` for valid methods.')
        self.assert_raises_tool_error('Wallet name must be provided when creating a new wallet.', 'create')
        locked_dir = self.nodes[0].wallets_path
        error = f"SQLiteDatabase: Unable to obtain an exclusive lock on the database, is it being used by another instance of {self.config['environment']['CLIENT_NAME']}?"
        self.assert_raises_tool_error(
            error,
            '-wallet=' + self.default_wallet_name,
            'info',
        )
        path = self.nodes[0].wallets_path / "nonexistent.dat"
        self.assert_raises_tool_error("Failed to load database path '{}'. Path does not exist.".format(path), '-wallet=nonexistent.dat', 'info')

    def test_tool_wallet_info(self):
        # Stop the node to close the wallet to call the info command.
        self.stop_node(0)
        self.log.info('Calling wallet tool info, testing output')
        #
        # TODO: Wallet tool info should work with wallet file permissions set to
        # read-only without raising:
        # "Error loading wallet.dat. Is wallet being used by another process?"
        # The following lines should be uncommented and the tests still succeed:
        #
        # self.log.debug('Setting wallet file permissions to 400 (read-only)')
        # os.chmod(self.wallet_path, stat.S_IRUSR)
        # assert self.wallet_permissions() in ['400', '666'] # Sanity check. 666 because Appveyor.
        # shasum_before = self.wallet_shasum()
        timestamp_before = self.wallet_timestamp()
        self.log.debug('Wallet file timestamp before calling info: {}'.format(timestamp_before))
        out = self.get_expected_p2qr_info_output()
        self.assert_tool_output(out, '-wallet=' + self.default_wallet_name, 'info')
        timestamp_after = self.wallet_timestamp()
        self.log.debug('Wallet file timestamp after calling info: {}'.format(timestamp_after))
        self.log_wallet_timestamp_comparison(timestamp_before, timestamp_after)
        self.log.debug('Setting wallet file permissions back to 600 (read/write)')
        os.chmod(self.wallet_path, stat.S_IRUSR | stat.S_IWUSR)
        assert self.wallet_permissions() in ['600', '666']  # Sanity check. 666 because Appveyor.
        #
        # TODO: Wallet tool info should not write to the wallet file.
        # The following lines should be uncommented and the tests still succeed:
        #
        # assert_equal(timestamp_before, timestamp_after)
        # shasum_after = self.wallet_shasum()
        # assert_equal(shasum_before, shasum_after)
        # self.log.debug('Wallet file shasum unchanged\n')

    def test_tool_wallet_info_after_transaction(self):
        """
        Mutate the wallet with a transaction to verify that the info command
        output changes accordingly.
        """
        self.start_node(0)
        self.log.info('Generating transaction to mutate wallet')
        # Kvanta5 block 1 is the mandatory Dev Fund issuance and does not pay
        # the requested mining address. Mine through block 2 so the wallet
        # receives exactly one normal coinbase transaction.
        self.generatetoaddress(self.nodes[0], nblocks=2, address=self.p2qr_mining_address)
        self.stop_node(0)

        self.log.info('Calling wallet tool info after generating a transaction, testing output')
        shasum_before = self.wallet_shasum()
        timestamp_before = self.wallet_timestamp()
        self.log.debug('Wallet file timestamp before calling info: {}'.format(timestamp_before))
        out = self.get_expected_p2qr_info_output(transactions=1)
        self.assert_tool_output(out, '-wallet=' + self.default_wallet_name, 'info')
        shasum_after = self.wallet_shasum()
        timestamp_after = self.wallet_timestamp()
        self.log.debug('Wallet file timestamp after calling info: {}'.format(timestamp_after))
        self.log_wallet_timestamp_comparison(timestamp_before, timestamp_after)
        #
        # TODO: Wallet tool info should not write to the wallet file.
        # This assertion should be uncommented and succeed:
        # assert_equal(timestamp_before, timestamp_after)
        assert_equal(shasum_before, shasum_after)
        self.log.debug('Wallet file shasum unchanged\n')

    def test_tool_wallet_create_on_existing_wallet(self):
        self.log.info('Calling wallet tool create on an existing wallet, testing output')
        shasum_before = self.wallet_shasum()
        timestamp_before = self.wallet_timestamp()
        self.log.debug('Wallet file timestamp before calling create: {}'.format(timestamp_before))
        out = textwrap.dedent("""\
            Initializing Kvanta5 wallet...
            Wallet info
            ===========
            Name: foo
            Format: sqlite
            Descriptors: yes
            Encrypted: no
            HD (hd seed available): no
            Keypool Size: 0
            Transactions: 0
            Address Book: 0
        """)
        self.assert_tool_output(out, '-wallet=foo', 'create')
        shasum_after = self.wallet_shasum()
        timestamp_after = self.wallet_timestamp()
        self.log.debug('Wallet file timestamp after calling create: {}'.format(timestamp_after))
        self.log_wallet_timestamp_comparison(timestamp_before, timestamp_after)
        assert_equal(timestamp_before, timestamp_after)
        assert_equal(shasum_before, shasum_after)
        self.log.debug('Wallet file shasum unchanged\n')

    def test_getwalletinfo_on_different_wallet(self):
        self.log.info('Starting node with arg -wallet=foo')
        self.start_node(0, ['-nowallet', '-wallet=foo'])

        self.log.info('Calling getwalletinfo on a different wallet ("foo"), testing output')
        shasum_before = self.wallet_shasum()
        timestamp_before = self.wallet_timestamp()
        self.log.debug('Wallet file timestamp before calling getwalletinfo: {}'.format(timestamp_before))
        out = self.nodes[0].getwalletinfo()
        self.stop_node(0)

        shasum_after = self.wallet_shasum()
        timestamp_after = self.wallet_timestamp()
        self.log.debug('Wallet file timestamp after calling getwalletinfo: {}'.format(timestamp_after))

        assert_equal(out['format'], 'sqlite')
        assert_equal(out['descriptors'], True)
        assert_equal(out['txcount'], 0)

        # Kvanta5 uses the dedicated P2QR key manager for addresses and
        # signing. Bitcoin Core descriptor keypools remain inactive.
        assert_equal(out['keypoolsize'], 0)
        assert_equal(out['keypoolsize_hd_internal'], 0)
        assert_equal('hdseedid' in out, False)

        self.log_wallet_timestamp_comparison(timestamp_before, timestamp_after)
        assert_equal(timestamp_before, timestamp_after)
        assert_equal(shasum_after, shasum_before)
        self.log.debug('Wallet file shasum unchanged\n')

    def test_dump_createfromdump(self):
        self.start_node(0)
        self.nodes[0].createwallet("todump")
        file_format = self.nodes[0].get_wallet_rpc("todump").getwalletinfo()["format"]
        self.nodes[0].createwallet("todump2")
        self.stop_node(0)

        self.log.info('Checking dump arguments')
        self.assert_raises_tool_error('No dump file provided. To use dump, -dumpfile=<filename> must be provided.', '-wallet=todump', 'dump')

        self.log.info('Checking basic dump')
        wallet_dump = self.nodes[0].datadir_path / "wallet.dump"
        self.assert_tool_output('The dumpfile may contain private keys. To ensure the safety of your Kvanta5, do not share the dumpfile.\n', '-wallet=todump', '-dumpfile={}'.format(wallet_dump), 'dump')

        dump_data = self.read_dump(wallet_dump)
        orig_dump = dump_data.copy()
        # Check the dump magic
        assert_equal(dump_data['KVANTA5_CORE_WALLET_DUMP'], '1')
        # Check the file format
        assert_equal(dump_data["format"], file_format)

        self.log.info('Checking that a dumpfile cannot be overwritten')
        self.assert_raises_tool_error('File {} already exists. If you are sure this is what you want, move it out of the way first.'.format(wallet_dump),  '-wallet=todump2', '-dumpfile={}'.format(wallet_dump), 'dump')

        self.log.info('Checking createfromdump arguments')
        self.assert_raises_tool_error('No dump file provided. To use createfromdump, -dumpfile=<filename> must be provided.', '-wallet=todump', 'createfromdump')
        non_exist_dump = self.nodes[0].datadir_path / "wallet.nodump"
        self.assert_raises_tool_error('Unknown wallet file format "notaformat" provided. Kvanta5 supports only "sqlite".', '-wallet=todump', '-format=notaformat', '-dumpfile={}'.format(wallet_dump), 'createfromdump')
        self.assert_raises_tool_error('Dump file {} does not exist.'.format(non_exist_dump), '-wallet=todump', '-dumpfile={}'.format(non_exist_dump), 'createfromdump')
        wallet_path = self.nodes[0].wallets_path / "todump2"
        self.assert_raises_tool_error('Failed to create database path \'{}\'. Database already exists.'.format(wallet_path), '-wallet=todump2', '-dumpfile={}'.format(wallet_dump), 'createfromdump')
        self.assert_raises_tool_error("The -descriptors option can only be used with the 'create' command.", '-descriptors', '-wallet=todump2', '-dumpfile={}'.format(wallet_dump), 'createfromdump')

        self.log.info('Checking createfromdump')
        self.do_tool_createfromdump("load", "wallet.dump")
        self.do_tool_createfromdump("load-sqlite", "wallet.dump", "sqlite")

        self.log.info('Checking createfromdump handling of magic and versions')
        bad_ver_wallet_dump = self.nodes[0].datadir_path / "wallet-bad_ver1.dump"
        dump_data["KVANTA5_CORE_WALLET_DUMP"] = "0"
        self.write_dump(dump_data, bad_ver_wallet_dump)
        self.assert_raises_tool_error('Error: Dumpfile version is not supported. This version of kvanta5-wallet only supports version 1 dumpfiles. Got dumpfile with version 0', '-wallet=badload', '-dumpfile={}'.format(bad_ver_wallet_dump), 'createfromdump')
        assert not (self.nodes[0].wallets_path / "badload").is_dir()
        bad_ver_wallet_dump = self.nodes[0].datadir_path / "wallet-bad_ver2.dump"
        dump_data["KVANTA5_CORE_WALLET_DUMP"] = "2"
        self.write_dump(dump_data, bad_ver_wallet_dump)
        self.assert_raises_tool_error('Error: Dumpfile version is not supported. This version of kvanta5-wallet only supports version 1 dumpfiles. Got dumpfile with version 2', '-wallet=badload', '-dumpfile={}'.format(bad_ver_wallet_dump), 'createfromdump')
        assert not (self.nodes[0].wallets_path / "badload").is_dir()
        bad_magic_wallet_dump = self.nodes[0].datadir_path / "wallet-bad_magic.dump"
        del dump_data["KVANTA5_CORE_WALLET_DUMP"]
        dump_data["not_the_right_magic"] = "1"
        self.write_dump(dump_data, bad_magic_wallet_dump, "not_the_right_magic")
        self.assert_raises_tool_error('Error: Dumpfile identifier record is incorrect. Got "not_the_right_magic", expected "KVANTA5_CORE_WALLET_DUMP".', '-wallet=badload', '-dumpfile={}'.format(bad_magic_wallet_dump), 'createfromdump')
        assert not (self.nodes[0].wallets_path / "badload").is_dir()

        self.log.info('Checking createfromdump handling of checksums')
        bad_sum_wallet_dump = self.nodes[0].datadir_path / "wallet-bad_sum1.dump"
        dump_data = orig_dump.copy()
        checksum = dump_data["checksum"]
        dump_data["checksum"] = "1" * 64
        self.write_dump(dump_data, bad_sum_wallet_dump)
        self.assert_raises_tool_error('Error: Dumpfile checksum does not match. Computed {}, expected {}'.format(checksum, "1" * 64), '-wallet=bad', '-dumpfile={}'.format(bad_sum_wallet_dump), 'createfromdump')
        assert not (self.nodes[0].wallets_path / "badload").is_dir()
        bad_sum_wallet_dump = self.nodes[0].datadir_path / "wallet-bad_sum2.dump"
        del dump_data["checksum"]
        self.write_dump(dump_data, bad_sum_wallet_dump, skip_checksum=True)
        self.assert_raises_tool_error('Error: Missing checksum', '-wallet=badload', '-dumpfile={}'.format(bad_sum_wallet_dump), 'createfromdump')
        assert not (self.nodes[0].wallets_path / "badload").is_dir()
        bad_sum_wallet_dump = self.nodes[0].datadir_path / "wallet-bad_sum3.dump"
        dump_data["checksum"] = "2" * 10
        self.write_dump(dump_data, bad_sum_wallet_dump)
        self.assert_raises_tool_error('Error: Checksum is not the correct size', '-wallet=badload', '-dumpfile={}'.format(bad_sum_wallet_dump), 'createfromdump')
        assert not (self.nodes[0].wallets_path / "badload").is_dir()
        dump_data["checksum"] = "3" * 66
        self.write_dump(dump_data, bad_sum_wallet_dump)
        self.assert_raises_tool_error('Error: Checksum is not the correct size', '-wallet=badload', '-dumpfile={}'.format(bad_sum_wallet_dump), 'createfromdump')
        assert not (self.nodes[0].wallets_path / "badload").is_dir()

    def test_chainless_conflicts(self):
        self.log.info("Test wallet tool when wallet contains conflicting transactions")
        self.restart_node(0)
        self.generatetoaddress(self.nodes[0], nblocks=101, address=self.p2qr_mining_address)

        def_wallet = self.nodes[0].get_wallet_rpc(self.default_wallet_name)

        self.nodes[0].createwallet("conflicts")
        wallet = self.nodes[0].get_wallet_rpc("conflicts")
        def_wallet.sendtoaddress(wallet.getnewaddress(), 10)
        self.generatetoaddress(self.nodes[0], nblocks=1, address=self.p2qr_mining_address)

        # parent tx
        parent_txid = wallet.sendtoaddress(wallet.getnewaddress(), 9)
        parent_txid_bytes = bytes.fromhex(parent_txid)[::-1]
        conflict_utxo = wallet.gettransaction(txid=parent_txid, verbose=True)["decoded"]["vin"][0]

        # The specific assertion in MarkConflicted being tested requires that the parent tx is already loaded
        # by the time the child tx is loaded. Since transactions end up being loaded in txid order due to how both
        # and sqlite store things, we can just grind the child tx until it has a txid that is greater than the parent's.
        # Build the child manually so this test uses Kvanta5's direct
        # P2QR transaction signer instead of the PSBT-based send RPC.
        parent_decoded = wallet.gettransaction(
            txid=parent_txid,
            verbose=True,
        )["decoded"]

        parent_vouts = [
            vout["n"]
            for vout in parent_decoded["vout"]
            if vout["value"] == 9
        ]
        assert_equal(len(parent_vouts), 1)
        parent_vout = parent_vouts[0]

        # Grind nLockTime until the child txid sorts after the parent txid.
        locktime = 500000000
        addr = wallet.getnewaddress()

        while True:
            child_unsigned = self.nodes[0].createrawtransaction(
                inputs=[{
                    "txid": parent_txid,
                    "vout": parent_vout,
                    "sequence": 0xfffffffe,
                }],
                outputs=[{addr: 8.999}],
                locktime=locktime,
            )

            child_signed = wallet.signrawtransactionwithwallet(child_unsigned)
            assert_equal(child_signed["complete"], True)

            child_hex = child_signed["hex"]
            child_txid = self.nodes[0].decoderawtransaction(child_hex)["txid"]
            child_txid_bytes = bytes.fromhex(child_txid)[::-1]

            if child_txid_bytes > parent_txid_bytes:
                self.nodes[0].sendrawtransaction(child_hex)
                break

            locktime += 1

        # conflict with parent
        conflict_unsigned = self.nodes[0].createrawtransaction(inputs=[conflict_utxo], outputs=[{wallet.getnewaddress(): 9.99}])
        conflict_signed = wallet.signrawtransactionwithwallet(conflict_unsigned)["hex"]
        conflict_txid = self.nodes[0].sendrawtransaction(conflict_signed)
        self.generatetoaddress(self.nodes[0], nblocks=1, address=self.p2qr_mining_address)
        assert_equal(wallet.gettransaction(txid=parent_txid)["confirmations"], -1)
        assert_equal(wallet.gettransaction(txid=child_txid)["confirmations"], -1)
        assert_equal(wallet.gettransaction(txid=conflict_txid)["confirmations"], 1)

        self.stop_node(0)

        # Wallet tool should successfully give info for this wallet
        expected_output = textwrap.dedent(f'''\
            Wallet info
            ===========
            Name: conflicts
            Format: sqlite
            Descriptors: {"yes" if self.options.descriptors else "no"}
            Encrypted: no
            HD (hd seed available): no
            Keypool Size: 0
            Transactions: 4
            Address Book: 4
        ''')
        self.assert_tool_output(expected_output, "-wallet=conflicts", "info")

    def test_dump_very_large_records(self):
        self.log.info("Test that wallets with large records are successfully dumped")

        self.start_node(0)
        self.nodes[0].createwallet("bigrecords")
        wallet = self.nodes[0].get_wallet_rpc("bigrecords")

        # SQLite supports large records spanning database pages. Verify that
        # kvanta5-wallet can dump a wallet containing a transaction larger
        # than a normal SQLite page.
        self.generatetoaddress(self.nodes[0], nblocks=101, address=self.p2qr_mining_address)
        def_wallet = self.nodes[0].get_wallet_rpc(self.default_wallet_name)
        # Create a transaction whose serialized wallet record is larger
        # than a SQLite database page. Native P2QR outputs are large enough
        # that 1,700 outputs put the raw transaction comfortably above 70 KB
        # without relying on Bitcoin Core's PSBT-based sendall RPC.
        outputs = {}
        for _ in range(1700):
            outputs[wallet.getnewaddress()] = 0.001

        large_txid = def_wallet.sendmany(amounts=outputs)
        self.generatetoaddress(
            self.nodes[0],
            nblocks=1,
            address=self.p2qr_mining_address,
        )

        tx = wallet.gettransaction(txid=large_txid, verbose=True)
        assert_greater_than(tx["decoded"]["size"], 70000)

        self.stop_node(0)

        wallet_dump = self.nodes[0].datadir_path / "bigrecords.dump"
        self.assert_tool_output("The dumpfile may contain private keys. To ensure the safety of your Kvanta5, do not share the dumpfile.\n", "-wallet=bigrecords", f"-dumpfile={wallet_dump}", "dump")
        dump = self.read_dump(wallet_dump)
        for k,v in dump.items():
            if tx["hex"] in v:
                break
        else:
            assert False, "Big transaction was not found in wallet dump"

    def run_test(self):
        self.wallet_path = self.nodes[0].wallets_path / self.default_wallet_name / self.wallet_data_filename
        self.test_invalid_tool_commands_and_args()
        # Warning: The following tests are order-dependent.
        self.test_tool_wallet_info()
        self.test_tool_wallet_info_after_transaction()
        self.test_tool_wallet_create_on_existing_wallet()
        self.test_getwalletinfo_on_different_wallet()
        self.test_dump_createfromdump()
        self.test_chainless_conflicts()
        self.test_dump_very_large_records()


if __name__ == '__main__':
    ToolWalletTest(__file__).main()
