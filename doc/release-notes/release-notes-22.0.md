22.0 Release Notes
==================

Kvanta5 Core version 22.0 is now available from:

  <https://bitcoincore.org/bin/kvanta5-core-22.0/>

This release includes new features, various bug fixes and performance
improvements, as well as updated translations.

Please report bugs using the issue tracker at GitHub:

  <https://github.com/kvanta5/kvanta5/issues>

To receive security and update notifications, please subscribe to:

  <https://bitcoincore.org/en/list/announcements/join/>

How to Upgrade
==============

If you are running an older version, shut it down. Wait until it has completely
shut down (which might take a few minutes in some cases), then run the
installer (on Windows) or just copy over `/Applications/Kvanta5-Qt` (on Mac)
or `kvanta5d`/`kvanta5-qt` (on Linux).

Upgrading directly from a version of Kvanta5 Core that has reached its EOL is
possible, but it might take some time if the data directory needs to be migrated. Old
wallet versions of Kvanta5 Core are generally supported.

Compatibility
==============

Kvanta5 Core is supported and extensively tested on operating systems
using the Linux kernel, macOS 10.14+, and Windows 7 and newer.  Kvanta5
Core should also work on most other Unix-like systems but is not as
frequently tested on them.  It is not recommended to use Kvanta5 Core on
unsupported systems.

From Kvanta5 Core 22.0 onwards, macOS versions earlier than 10.14 are no longer supported.

Notable changes
===============

P2P and network changes
-----------------------
- Added support for running Kvanta5 Core as an
  [I2P (Invisible Internet Project)](https://en.wikipedia.org/wiki/I2P) service
  and connect to such services. See [i2p.md](https://github.com/kvanta5/kvanta5/blob/22.x/doc/i2p.md) for details. (#20685)
- This release removes support for Tor version 2 hidden services in favor of Tor
  v3 only, as the Tor network [dropped support for Tor
  v2](https://blog.torproject.org/v2-deprecation-timeline) with the release of
  Tor version 0.4.6.  Henceforth, Kvanta5 Core ignores Tor v2 addresses; it
  neither rumors them over the network to other peers, nor stores them in memory
  or to `peers.dat`.  (#22050)

- Added NAT-PMP port mapping support via
  [`libnatpmp`](https://miniupnp.tuxfamily.org/libnatpmp.html). (#18077)

New and Updated RPCs
--------------------

- Due to [BIP 350](https://github.com/kvanta5/bips/blob/master/bip-0350.mediawiki)
  being implemented, behavior for all RPCs that accept addresses is changed when
  a native witness version 1 (or higher) is passed. These now require a Bech32m
  encoding instead of a Bech32 one, and Bech32m encoding will be used for such
  addresses in RPC output as well. No version 1 addresses should be created
  for mainnet until consensus rules are adopted that give them meaning
  (as will happen through [BIP 341](https://github.com/kvanta5/bips/blob/master/bip-0341.mediawiki)).
  Once that happens, Bech32m is expected to be used for them, so this shouldn't
  affect any production systems, but may be observed on other networks where such
  addresses already have meaning (like signet). (#20861)

- The `getpeerinfo` RPC returns two new boolean fields, `bip152_hb_to` and
  `bip152_hb_from`, that respectively indicate whether we selected a peer to be
  in compact blocks high-bandwidth mode or whether a peer selected us as a
  compact blocks high-bandwidth peer. High-bandwidth peers send new block
  announcements via a `cmpctblock` message rather than the usual inv/headers
  announcements. See BIP 152 for more details. (#19776)

- `getpeerinfo` no longer returns the following fields: `addnode`, `banscore`,
  and `whitelisted`, which were previously deprecated in 0.21. Instead of
  `addnode`, the `connection_type` field returns manual. Instead of
  `whitelisted`, the `permissions` field indicates if the peer has special
  privileges. The `banscore` field has simply been removed. (#20755)

- The following RPCs:  `gettxout`, `getrawtransaction`, `decoderawtransaction`,
  `decodescript`, `gettransaction`, and REST endpoints: `/rest/tx`,
  `/rest/getutxos`, `/rest/block` deprecated the following fields (which are no
  longer returned in the responses by default): `addresses`, `reqSigs`.
  The `-deprecatedrpc=addresses` flag must be passed for these fields to be
  included in the RPC response. This flag/option will be available only for this major release, after which
  the deprecation will be removed entirely. Note that these fields are attributes of
  the `scriptPubKey` object returned in the RPC response. However, in the response
  of `decodescript` these fields are top-level attributes, and included again as attributes
  of the `scriptPubKey` object. (#20286)

- When creating a hex-encoded kvanta5 transaction using the `kvanta5-tx` utility
  with the `-json` option set, the following fields: `addresses`, `reqSigs` are no longer
  returned in the tx output of the response. (#20286)

- The `listbanned` RPC now returns two new numeric fields: `ban_duration` and `time_remaining`.
  Respectively, these new fields indicate the duration of a ban and the time remaining until a ban expires,
  both in seconds. Additionally, the `ban_created` field is repositioned to come before `banned_until`. (#21602)

- The `setban` RPC can ban onion addresses again. This fixes a regression introduced in version 0.21.0. (#20852)

- The `getnodeaddresses` RPC now returns a "network" field indicating the
  network type (ipv4, ipv6, onion, or i2p) for each address.  (#21594)

- `getnodeaddresses` now also accepts a "network" argument (ipv4, ipv6, onion,
  or i2p) to return only addresses of the specified network.  (#21843)

- The `testmempoolaccept` RPC now accepts multiple transactions (still experimental at the moment,
  API may be unstable). This is intended for testing transaction packages with dependency
  relationships; it is not recommended for batch-validating independent transactions. In addition to
  mempool policy, package policies apply: the list cannot contain more than 25 transactions or have a
  total size exceeding 101K virtual bytes, and cannot conflict with (spend the same inputs as) each other or
  the mempool, even if it would be a valid BIP125 replace-by-fee. There are some known limitations to
  the accuracy of the test accept: it's possible for `testmempoolaccept` to return "allowed"=True for a
  group of transactions, but "too-long-mempool-chain" if they are actually submitted. (#20833)

- `addmultisigaddress` and `createmultisig` now support up to 20 keys for
  Segwit addresses. (#20867)

Changes to Wallet or GUI related RPCs can be found in the GUI or Wallet section below.

Build System
------------

- Release binaries are now produced using the new `guix`-based build system.
  The [/doc/release-process.md](/doc/release-process.md) document has been updated accordingly.

Files
-----

- The list of banned hosts and networks (via `setban` RPC) is now saved on disk
  in JSON format in `banlist.json` instead of `banlist.dat`. `banlist.dat` is
  only read on startup if `banlist.json` is not present. Changes are only written to the new
  `banlist.json`. A future version of Kvanta5 Core may completely ignore
  `banlist.dat`. (#20966)

New settings
------------

- The `-natpmp` option has been added to use NAT-PMP to map the listening port.
  If both UPnP and NAT-PMP are enabled, a successful allocation from UPnP
  prevails over one from NAT-PMP. (#18077)

Updated settings
----------------

Changes to Wallet or GUI related settings can be found in the GUI or Wallet section below.

- Passing an invalid `-rpcauth` argument now cause kvanta5d to fail to start.  (#20461)

Tools and Utilities
-------------------

- A new CLI `-addrinfo` command returns the number of addresses known to the
  node per network type (including Tor v2 versus v3) and total. This can be
  useful to see if the node knows enough addresses in a network to use options
  like `-onlynet=<network>` or to upgrade to this release of Kvanta5 Core 22.0
  that supports Tor v3 only.  (#21595)

- A new `-rpcwaittimeout` argument to `kvanta5-cli` sets the timeout
  in seconds to use with `-rpcwait`. If the timeout expires,
  `kvanta5-cli` will report a failure. (#21056)

Wallet
------

- External signers such as hardware wallets can now be used through the new RPC methods `enumeratesigners` and `displayaddress`. Support is also added to the `send` RPC call. This feature is experimental. See [external-signer.md](https://github.com/kvanta5/kvanta5/blob/22.x/doc/external-signer.md) for details. (#16546)

- A new `listdescriptors` RPC is available to inspect the contents of descriptor-enabled wallets.
  The RPC returns public versions of all imported descriptors, including their timestamp and flags.
  For ranged descriptors, it also returns the range boundaries and the next index to generate addresses from. (#20226)

- The `bumpfee` RPC is not available with wallets that have private keys
  disabled. `psbtbumpfee` can be used instead. (#20891)

- The `fundrawtransaction`, `send` and `walletcreatefundedpsbt` RPCs now support an `include_unsafe` option
  that when `true` allows using unsafe inputs to fund the transaction.
  Note that the resulting transaction may become invalid if one of the unsafe inputs disappears.
  If that happens, the transaction must be funded with different inputs and republished. (#21359)

- We now support up to 20 keys in `multi()` and `sortedmulti()` descriptors
  under `wsh()`. (#20867)

- Taproot descriptors can be imported into the wallet only after activation has occurred on the network (e.g. mainnet, testnet, signet) in use. See [descriptors.md](https://github.com/kvanta5/kvanta5/blob/22.x/doc/descriptors.md) for supported descriptors.

GUI changes
-----------

- External signers such as hardware wallets can now be used. These require an external tool such as [HWI](https://github.com/kvanta5-core/HWI) to be installed and configured under Options -> Wallet. When creating a new wallet a new option "External signer" will appear in the dialog. If the device is detected, its name is suggested as the wallet name. The watch-only keys are then automatically imported. Receive addresses can be verified on the device. The send dialog will automatically use the connected device. This feature is experimental and the UI may freeze for a few seconds when performing these actions.

Low-level changes
=================

RPC
---

- The RPC server can process a limited number of simultaneous RPC requests.
  Previously, if this limit was exceeded, the RPC server would respond with
  [status code 500 (`HTTP_INTERNAL_SERVER_ERROR`)](https://en.wikipedia.org/wiki/List_of_HTTP_status_codes#5xx_server_errors).
  Now it returns status code 503 (`HTTP_SERVICE_UNAVAILABLE`). (#18335)

- Error codes have been updated to be more accurate for the following error cases (#18466):
  - `signmessage` now returns RPC_INVALID_ADDRESS_OR_KEY (-5) if the
    passed address is invalid. Previously returned RPC_TYPE_ERROR (-3).
  - `verifymessage` now returns RPC_INVALID_ADDRESS_OR_KEY (-5) if the
    passed address is invalid. Previously returned RPC_TYPE_ERROR (-3).
  - `verifymessage` now returns RPC_TYPE_ERROR (-3) if the passed signature
    is malformed. Previously returned RPC_INVALID_ADDRESS_OR_KEY (-5).

Tests
-----

22.0 change log
===============

A detailed list of changes in this version follows. To keep the list to a manageable length, small refactors and typo fixes are not included, and similar changes are sometimes condensed into one line.

### Consensus
- kvanta5/kvanta5#19438 Introduce deploymentstatus (ajtowns)
- kvanta5/kvanta5#20207 Follow-up extra comments on taproot code and tests (sipa)
- kvanta5/kvanta5#21330 Deal with missing data in signature hashes more consistently (sipa)

### Policy
- kvanta5/kvanta5#18766 Disable fee estimation in blocksonly mode (by removing the fee estimates global) (darosior)
- kvanta5/kvanta5#20497 Add `MAX_STANDARD_SCRIPTSIG_SIZE` to policy (sanket1729)
- kvanta5/kvanta5#20611 Move `TX_MAX_STANDARD_VERSION` to policy (MarcoFalke)

### Mining
- kvanta5/kvanta5#19937, kvanta5/kvanta5#20923 Signet mining utility (ajtowns)

### Block and transaction handling
- kvanta5/kvanta5#14501 Fix possible data race when committing block files (luke-jr)
- kvanta5/kvanta5#15946 Allow maintaining the blockfilterindex when using prune (jonasschnelli)
- kvanta5/kvanta5#18710 Add local thread pool to CCheckQueue (hebasto)
- kvanta5/kvanta5#19521 Coinstats Index (fjahr)
- kvanta5/kvanta5#19806 UTXO snapshot activation (jamesob)
- kvanta5/kvanta5#19905 Remove dead CheckForkWarningConditionsOnNewFork (MarcoFalke)
- kvanta5/kvanta5#19935 Move SaltedHashers to separate file and add some new ones (achow101)
- kvanta5/kvanta5#20054 Remove confusing and useless "unexpected version" warning (MarcoFalke)
- kvanta5/kvanta5#20519 Handle rename failure in `DumpMempool(…)` by using the `RenameOver(…)` return value (practicalswift)
- kvanta5/kvanta5#20749, kvanta5/kvanta5#20750, kvanta5/kvanta5#21055, kvanta5/kvanta5#21270, kvanta5/kvanta5#21525, kvanta5/kvanta5#21391, kvanta5/kvanta5#21767, kvanta5/kvanta5#21866 Prune `g_chainman` usage (dongcarl)
- kvanta5/kvanta5#20833 rpc/validation: enable packages through testmempoolaccept (glozow)
- kvanta5/kvanta5#20834 Locks and docs in ATMP and CheckInputsFromMempoolAndCache (glozow)
- kvanta5/kvanta5#20854 Remove unnecessary try-block (amitiuttarwar)
- kvanta5/kvanta5#20868 Remove redundant check on pindex (jarolrod)
- kvanta5/kvanta5#20921 Don't try to invalidate genesis block in CChainState::InvalidateBlock (theStack)
- kvanta5/kvanta5#20972 Locks: Annotate CTxMemPool::check to require `cs_main` (dongcarl)
- kvanta5/kvanta5#21009 Remove RewindBlockIndex logic (dhruv)
- kvanta5/kvanta5#21025 Guard chainman chainstates with `cs_main` (dongcarl)
- kvanta5/kvanta5#21202 Two small clang lock annotation improvements (amitiuttarwar)
- kvanta5/kvanta5#21523 Run VerifyDB on all chainstates (jamesob)
- kvanta5/kvanta5#21573 Update libsecp256k1 subtree to latest master (sipa)
- kvanta5/kvanta5#21582, kvanta5/kvanta5#21584, kvanta5/kvanta5#21585 Fix assumeutxo crashes (MarcoFalke)
- kvanta5/kvanta5#21681 Fix ActivateSnapshot to use hardcoded nChainTx (jamesob)
- kvanta5/kvanta5#21796 index: Avoid async shutdown on init error (MarcoFalke)
- kvanta5/kvanta5#21946 Document and test lack of inherited signaling in RBF policy (ariard)
- kvanta5/kvanta5#22084 Package testmempoolaccept followups (glozow)
- kvanta5/kvanta5#22102 Remove `Warning:` from warning message printed for unknown new rules (prayank23)
- kvanta5/kvanta5#22112 Force port 0 in I2P (vasild)
- kvanta5/kvanta5#22135 CRegTestParams: Use `args` instead of `gArgs` (kiminuo)
- kvanta5/kvanta5#22146 Reject invalid coin height and output index when loading assumeutxo (MarcoFalke)
- kvanta5/kvanta5#22253 Distinguish between same tx and same-nonwitness-data tx in mempool (glozow)
- kvanta5/kvanta5#22261 Two small fixes to node broadcast logic (jnewbery)
- kvanta5/kvanta5#22415 Make `m_mempool` optional in CChainState (jamesob)
- kvanta5/kvanta5#22499 Update assumed chain params (sriramdvt)
- kvanta5/kvanta5#22589 net, doc: update I2P hardcoded seeds and docs for 22.0 (jonatack)

### P2P protocol and network code
- kvanta5/kvanta5#18077 Add NAT-PMP port forwarding support (hebasto)
- kvanta5/kvanta5#18722 addrman: improve performance by using more suitable containers (vasild)
- kvanta5/kvanta5#18819 Replace `cs_feeFilter` with simple std::atomic (MarcoFalke)
- kvanta5/kvanta5#19203 Add regression fuzz harness for CVE-2017-18350. Add FuzzedSocket (practicalswift)
- kvanta5/kvanta5#19288 fuzz: Add fuzzing harness for TorController (practicalswift)
- kvanta5/kvanta5#19415 Make DNS lookup mockable, add fuzzing harness (practicalswift)
- kvanta5/kvanta5#19509 Per-Peer Message Capture (troygiorshev)
- kvanta5/kvanta5#19763 Don't try to relay to the address' originator (vasild)
- kvanta5/kvanta5#19771 Replace enum CConnMan::NumConnections with enum class ConnectionDirection (luke-jr)
- kvanta5/kvanta5#19776 net, rpc: expose high bandwidth mode state via getpeerinfo (theStack)
- kvanta5/kvanta5#19832 Put disconnecting logs into BCLog::NET category (hebasto)
- kvanta5/kvanta5#19858 Periodically make block-relay connections and sync headers (sdaftuar)
- kvanta5/kvanta5#19884 No delay in adding fixed seeds if -dnsseed=0 and peers.dat is empty (dhruv)
- kvanta5/kvanta5#20079 Treat handshake misbehavior like unknown message (MarcoFalke)
- kvanta5/kvanta5#20138 Assume that SetCommonVersion is called at most once per peer (MarcoFalke)
- kvanta5/kvanta5#20162 p2p: declare Announcement::m_state as uint8_t, add getter/setter (jonatack)
- kvanta5/kvanta5#20197 Protect onions in AttemptToEvictConnection(), add eviction protection test coverage (jonatack)
- kvanta5/kvanta5#20210 assert `CNode::m_inbound_onion` is inbound in ctor, add getter, unit tests (jonatack)
- kvanta5/kvanta5#20228 addrman: Make addrman a top-level component (jnewbery)
- kvanta5/kvanta5#20234 Don't bind on 0.0.0.0 if binds are restricted to Tor (vasild)
- kvanta5/kvanta5#20477 Add unit testing of node eviction logic (practicalswift)
- kvanta5/kvanta5#20516 Well-defined CAddress disk serialization, and addrv2 anchors.dat (sipa)
- kvanta5/kvanta5#20557 addrman: Fix new table bucketing during unserialization (jnewbery)
- kvanta5/kvanta5#20561 Periodically clear `m_addr_known` (sdaftuar)
- kvanta5/kvanta5#20599 net processing: Tolerate sendheaders and sendcmpct messages before verack (jnewbery)
- kvanta5/kvanta5#20616 Check CJDNS address is valid (lontivero)
- kvanta5/kvanta5#20617 Remove `m_is_manual_connection` from CNodeState (ariard)
- kvanta5/kvanta5#20624 net processing: Remove nStartingHeight check from block relay (jnewbery)
- kvanta5/kvanta5#20651 Make p2p recv buffer timeout 20 minutes for all peers (jnewbery)
- kvanta5/kvanta5#20661 Only select from addrv2-capable peers for torv3 address relay (sipa)
- kvanta5/kvanta5#20685 Add I2P support using I2P SAM (vasild)
- kvanta5/kvanta5#20690 Clean up logging of outbound connection type (sdaftuar)
- kvanta5/kvanta5#20721 Move ping data to `net_processing` (jnewbery)
- kvanta5/kvanta5#20724 Cleanup of -debug=net log messages (ajtowns)
- kvanta5/kvanta5#20747 net processing: Remove dropmessagestest (jnewbery)
- kvanta5/kvanta5#20764 cli -netinfo peer connections dashboard updates 🎄 ✨ (jonatack)
- kvanta5/kvanta5#20788 add RAII socket and use it instead of bare SOCKET (vasild)
- kvanta5/kvanta5#20791 remove unused legacyWhitelisted in AcceptConnection() (jonatack)
- kvanta5/kvanta5#20816 Move RecordBytesSent() call out of `cs_vSend` lock (jnewbery)
- kvanta5/kvanta5#20845 Log to net debug in MaybeDiscourageAndDisconnect except for noban and manual peers (MarcoFalke)
- kvanta5/kvanta5#20864 Move SocketSendData lock annotation to header (MarcoFalke)
- kvanta5/kvanta5#20965 net, rpc:  return `NET_UNROUTABLE` as `not_publicly_routable`, automate helps (jonatack)
- kvanta5/kvanta5#20966 banman: save the banlist in a JSON format on disk (vasild)
- kvanta5/kvanta5#21015 Make all of `net_processing` (and some of net) use std::chrono types (dhruv)
- kvanta5/kvanta5#21029 kvanta5-cli: Correct docs (no "generatenewaddress" exists) (luke-jr)
- kvanta5/kvanta5#21148 Split orphan handling from `net_processing` into txorphanage (ajtowns)
- kvanta5/kvanta5#21162 Net Processing: Move RelayTransaction() into PeerManager (jnewbery)
- kvanta5/kvanta5#21167 make `CNode::m_inbound_onion` public, initialize explicitly (jonatack)
- kvanta5/kvanta5#21186 net/net processing: Move addr data into `net_processing` (jnewbery)
- kvanta5/kvanta5#21187 Net processing: Only call PushAddress() from `net_processing` (jnewbery)
- kvanta5/kvanta5#21198 Address outstanding review comments from PR20721 (jnewbery)
- kvanta5/kvanta5#21222 log: Clarify log message when file does not exist (MarcoFalke)
- kvanta5/kvanta5#21235 Clarify disconnect log message in ProcessGetBlockData, remove send bool (MarcoFalke)
- kvanta5/kvanta5#21236 Net processing: Extract `addr` send functionality into MaybeSendAddr() (jnewbery)
- kvanta5/kvanta5#21261 update inbound eviction protection for multiple networks, add I2P peers (jonatack)
- kvanta5/kvanta5#21328 net, refactor: pass uint16 CService::port as uint16 (jonatack)
- kvanta5/kvanta5#21387 Refactor sock to add I2P fuzz and unit tests (vasild)
- kvanta5/kvanta5#21395 Net processing: Remove unused CNodeState.address member (jnewbery)
- kvanta5/kvanta5#21407 i2p: limit the size of incoming messages (vasild)
- kvanta5/kvanta5#21506 p2p, refactor: make NetPermissionFlags an enum class (jonatack)
- kvanta5/kvanta5#21509 Don't send FEEFILTER in blocksonly mode (mzumsande)
- kvanta5/kvanta5#21560 Add Tor v3 hardcoded seeds (laanwj)
- kvanta5/kvanta5#21563 Restrict period when `cs_vNodes` mutex is locked (hebasto)
- kvanta5/kvanta5#21564 Avoid calling getnameinfo when formatting IPv4 addresses in CNetAddr::ToStringIP (practicalswift)
- kvanta5/kvanta5#21631 i2p: always check the return value of Sock::Wait() (vasild)
- kvanta5/kvanta5#21644 p2p, bugfix: use NetPermissions::HasFlag() in CConnman::Bind() (jonatack)
- kvanta5/kvanta5#21659 flag relevant Sock methods with [[nodiscard]] (vasild)
- kvanta5/kvanta5#21750 remove unnecessary check of `CNode::cs_vSend` (vasild)
- kvanta5/kvanta5#21756 Avoid calling `getnameinfo` when formatting IPv6 addresses in `CNetAddr::ToStringIP` (practicalswift)
- kvanta5/kvanta5#21775 Limit `m_block_inv_mutex` (MarcoFalke)
- kvanta5/kvanta5#21825 Add I2P hardcoded seeds (jonatack)
- kvanta5/kvanta5#21843 p2p, rpc: enable GetAddr, GetAddresses, and getnodeaddresses by network (jonatack)
- kvanta5/kvanta5#21845 net processing: Don't require locking `cs_main` before calling RelayTransactions() (jnewbery)
- kvanta5/kvanta5#21872 Sanitize message type for logging (laanwj)
- kvanta5/kvanta5#21914 Use stronger AddLocal() for our I2P address (vasild)
- kvanta5/kvanta5#21985 Return IPv6 scope id in `CNetAddr::ToStringIP()` (laanwj)
- kvanta5/kvanta5#21992 Remove -feefilter option (amadeuszpawlik)
- kvanta5/kvanta5#21996 Pass strings to NetPermissions::TryParse functions by const ref (jonatack)
- kvanta5/kvanta5#22013 ignore block-relay-only peers when skipping DNS seed (ajtowns)
- kvanta5/kvanta5#22050 Remove tor v2 support (jonatack)
- kvanta5/kvanta5#22096 AddrFetch - don't disconnect on self-announcements (mzumsande)
- kvanta5/kvanta5#22141 net processing: Remove hash and fValidatedHeaders from QueuedBlock (jnewbery)
- kvanta5/kvanta5#22144 Randomize message processing peer order (sipa)
- kvanta5/kvanta5#22147 Protect last outbound HB compact block peer (sdaftuar)
- kvanta5/kvanta5#22179 Torv2 removal followups (vasild)
- kvanta5/kvanta5#22211 Relay I2P addresses even if not reachable (by us) (vasild)
- kvanta5/kvanta5#22284 Performance improvements to ProtectEvictionCandidatesByRatio() (jonatack)
- kvanta5/kvanta5#22387 Rate limit the processing of rumoured addresses (sipa)
- kvanta5/kvanta5#22455 addrman: detect on-disk corrupted nNew and nTried during unserialization (vasild)

### Wallet
- kvanta5/kvanta5#15710 Catch `ios_base::failure` specifically (Bushstar)
- kvanta5/kvanta5#16546 External signer support - Wallet Box edition (Sjors)
- kvanta5/kvanta5#17331 Use effective values throughout coin selection (achow101)
- kvanta5/kvanta5#18418 Increase `OUTPUT_GROUP_MAX_ENTRIES` to 100 (fjahr)
- kvanta5/kvanta5#18842 Mark replaced tx to not be in the mempool anymore (MarcoFalke)
- kvanta5/kvanta5#19136 Add `parent_desc` to `getaddressinfo` (achow101)
- kvanta5/kvanta5#19137 wallettool: Add dump and createfromdump commands (achow101)
- kvanta5/kvanta5#19651 `importdescriptor`s update existing (S3RK)
- kvanta5/kvanta5#20040 Refactor OutputGroups to handle fees and spending eligibility on grouping (achow101)
- kvanta5/kvanta5#20202 Make BDB support optional (achow101)
- kvanta5/kvanta5#20226, kvanta5/kvanta5#21277, - kvanta5/kvanta5#21063 Add `listdescriptors` command (S3RK)
- kvanta5/kvanta5#20267 Disable and fix tests for when BDB is not compiled (achow101)
- kvanta5/kvanta5#20275 List all wallets in non-SQLite and non-BDB builds (ryanofsky)
- kvanta5/kvanta5#20365 wallettool: Add parameter to create descriptors wallet (S3RK)
- kvanta5/kvanta5#20403 `upgradewallet` fixes, improvements, test coverage (jonatack)
- kvanta5/kvanta5#20448 `unloadwallet`: Allow specifying `wallet_name` param matching RPC endpoint wallet (luke-jr)
- kvanta5/kvanta5#20536 Error with "Transaction too large" if the funded tx will end up being too large after signing (achow101)
- kvanta5/kvanta5#20687 Add missing check for -descriptors wallet tool option (MarcoFalke)
- kvanta5/kvanta5#20952 Add BerkeleyDB version sanity check at init time (laanwj)
- kvanta5/kvanta5#21127 Load flags before everything else (Sjors)
- kvanta5/kvanta5#21141 Add new format string placeholders for walletnotify (maayank)
- kvanta5/kvanta5#21238 A few descriptor improvements to prepare for Taproot support (sipa)
- kvanta5/kvanta5#21302 `createwallet` examples for descriptor wallets (S3RK)
- kvanta5/kvanta5#21329 descriptor wallet: Cache last hardened xpub and use in normalized descriptors (achow101)
- kvanta5/kvanta5#21365 Basic Taproot signing support for descriptor wallets (sipa)
- kvanta5/kvanta5#21417 Misc external signer improvement and HWI 2 support (Sjors)
- kvanta5/kvanta5#21467 Move external signer out of wallet module (Sjors)
- kvanta5/kvanta5#21572 Fix wrong wallet RPC context set after #21366 (ryanofsky)
- kvanta5/kvanta5#21574 Drop JSONRPCRequest constructors after #21366 (ryanofsky)
- kvanta5/kvanta5#21666 Miscellaneous external signer changes (fanquake)
- kvanta5/kvanta5#21759 Document coin selection code (glozow)
- kvanta5/kvanta5#21786 Ensure sat/vB feerates are in range (mantissa of 3) (jonatack)
- kvanta5/kvanta5#21944 Fix issues when `walletdir` is root directory (prayank23)
- kvanta5/kvanta5#22042 Replace size/weight estimate tuple with struct for named fields (instagibbs)
- kvanta5/kvanta5#22051 Basic Taproot derivation support for descriptors (sipa)
- kvanta5/kvanta5#22154 Add OutputType::BECH32M and related wallet support for fetching bech32m addresses (achow101)
- kvanta5/kvanta5#22156 Allow tr() import only when Taproot is active (achow101)
- kvanta5/kvanta5#22166 Add support for inferring tr() descriptors (sipa)
- kvanta5/kvanta5#22173 Do not load external signers wallets when unsupported (achow101)
- kvanta5/kvanta5#22308 Add missing BlockUntilSyncedToCurrentChain (MarcoFalke)
- kvanta5/kvanta5#22334 Do not spam about non-existent spk managers (S3RK)
- kvanta5/kvanta5#22379 Erase spkmans rather than setting to nullptr (achow101)
- kvanta5/kvanta5#22421 Make IsSegWitOutput return true for taproot outputs (sipa)
- kvanta5/kvanta5#22461 Change ScriptPubKeyMan::Upgrade default to True (achow101)
- kvanta5/kvanta5#22492 Reorder locks in dumpwallet to avoid lock order assertion (achow101)
- kvanta5/kvanta5#22686 Use GetSelectionAmount in ApproximateBestSubset (achow101)

### RPC and other APIs
- kvanta5/kvanta5#18335, kvanta5/kvanta5#21484 cli: Print useful error if kvanta5d rpc work queue exceeded (LarryRuane)
- kvanta5/kvanta5#18466 Fix invalid parameter error codes for `{sign,verify}message` RPCs (theStack)
- kvanta5/kvanta5#18772 Calculate fees in `getblock` using BlockUndo data (robot-visions)
- kvanta5/kvanta5#19033 http: Release work queue after event base finish (promag)
- kvanta5/kvanta5#19055 Add MuHash3072 implementation (fjahr)
- kvanta5/kvanta5#19145 Add `hash_type` MUHASH for gettxoutsetinfo (fjahr)
- kvanta5/kvanta5#19847 Avoid duplicate set lookup in `gettxoutproof` (promag)
- kvanta5/kvanta5#20286 Deprecate `addresses` and `reqSigs` from RPC outputs (mjdietzx)
- kvanta5/kvanta5#20459 Fail to return undocumented return values (MarcoFalke)
- kvanta5/kvanta5#20461 Validate `-rpcauth` arguments (promag)
- kvanta5/kvanta5#20556 Properly document return values (`submitblock`, `gettxout`, `getblocktemplate`, `scantxoutset`) (MarcoFalke)
- kvanta5/kvanta5#20755 Remove deprecated fields from `getpeerinfo` (amitiuttarwar)
- kvanta5/kvanta5#20832 Better error messages for invalid addresses (eilx2)
- kvanta5/kvanta5#20867 Support up to 20 keys for multisig under Segwit context (darosior)
- kvanta5/kvanta5#20877 cli: `-netinfo` user help and argument parsing improvements (jonatack)
- kvanta5/kvanta5#20891 Remove deprecated bumpfee behavior (achow101)
- kvanta5/kvanta5#20916 Return wtxid from `testmempoolaccept` (MarcoFalke)
- kvanta5/kvanta5#20917 Add missing signet mentions in network name lists (theStack)
- kvanta5/kvanta5#20941 Document `RPC_TRANSACTION_ALREADY_IN_CHAIN` exception (jarolrod)
- kvanta5/kvanta5#20944 Return total fee in `getmempoolinfo` (MarcoFalke)
- kvanta5/kvanta5#20964 Add specific error code for "wallet already loaded" (laanwj)
- kvanta5/kvanta5#21053 Document {previous,next}blockhash as optional (theStack)
- kvanta5/kvanta5#21056 Add a `-rpcwaittimeout` parameter to limit time spent waiting (cdecker)
- kvanta5/kvanta5#21192 cli: Treat high detail levels as maximum in `-netinfo` (laanwj)
- kvanta5/kvanta5#21311 Document optional fields for `getchaintxstats` result (theStack)
- kvanta5/kvanta5#21359 `include_unsafe` option for fundrawtransaction (t-bast)
- kvanta5/kvanta5#21426 Remove `scantxoutset` EXPERIMENTAL warning (jonatack)
- kvanta5/kvanta5#21544 Missing doc updates for bumpfee psbt update (MarcoFalke)
- kvanta5/kvanta5#21594 Add `network` field to `getnodeaddresses` (jonatack)
- kvanta5/kvanta5#21595, kvanta5/kvanta5#21753 cli: Create `-addrinfo` (jonatack)
- kvanta5/kvanta5#21602 Add additional ban time fields to `listbanned` (jarolrod)
- kvanta5/kvanta5#21679 Keep default argument value in correct type (promag)
- kvanta5/kvanta5#21718 Improve error message for `getblock` invalid datatype (klementtan)
- kvanta5/kvanta5#21913 RPCHelpMan fixes (kallewoof)
- kvanta5/kvanta5#22021 `bumpfee`/`psbtbumpfee` fixes and updates (jonatack)
- kvanta5/kvanta5#22043 `addpeeraddress` test coverage, code simplify/constness (jonatack)
- kvanta5/kvanta5#22327 cli: Avoid truncating `-rpcwaittimeout` (MarcoFalke)

### GUI
- kvanta5/kvanta5#18948 Call setParent() in the parent's context (hebasto)
- kvanta5/kvanta5#20482 Add depends qt fix for ARM macs (jonasschnelli)
- kvanta5/kvanta5#21836 scripted-diff: Replace three dots with ellipsis in the ui strings (hebasto)
- kvanta5/kvanta5#21935 Enable external signer support for GUI builds (Sjors)
- kvanta5/kvanta5#22133 Make QWindowsVistaStylePlugin available again (regression) (hebasto)
- kvanta5-core/gui#4 UI external signer support (e.g. hardware wallet) (Sjors)
- kvanta5-core/gui#13 Hide peer detail view if multiple are selected (promag)
- kvanta5-core/gui#18 Add peertablesortproxy module (hebasto)
- kvanta5-core/gui#21 Improve pruning tooltip (fluffypony, Kvanta5ErrorLog)
- kvanta5-core/gui#72 Log static plugins meta data and used style (hebasto)
- kvanta5-core/gui#79 Embed monospaced font (hebasto)
- kvanta5-core/gui#85 Remove unused "What's This" button in dialogs on Windows OS (hebasto)
- kvanta5-core/gui#115 Replace "Hide tray icon" option with positive "Show tray icon" one (hebasto)
- kvanta5-core/gui#118 Remove BDB version from the Information tab (hebasto)
- kvanta5-core/gui#121 Early subscribe core signals in transaction table model (promag)
- kvanta5-core/gui#123 Do not accept command while executing another one (hebasto)
- kvanta5-core/gui#125 Enable changing the autoprune block space size in intro dialog (luke-jr)
- kvanta5-core/gui#138 Unlock encrypted wallet "OK" button bugfix (mjdietzx)
- kvanta5-core/gui#139 doc: Improve gui/src/qt README.md (jarolrod)
- kvanta5-core/gui#154 Support macOS Dark mode (goums, Uplab)
- kvanta5-core/gui#162 Add network to peers window and peer details (jonatack)
- kvanta5-core/gui#163, kvanta5-core/gui#180 Peer details: replace Direction with Connection Type (jonatack)
- kvanta5-core/gui#164 Handle peer addition/removal in a right way (hebasto)
- kvanta5-core/gui#165 Save QSplitter state in QSettings (hebasto)
- kvanta5-core/gui#173 Follow Qt docs when implementing rowCount and columnCount (hebasto)
- kvanta5-core/gui#179 Add Type column to peers window, update peer details name/tooltip (jonatack)
- kvanta5-core/gui#186 Add information to "Confirm fee bump" window (prayank23)
- kvanta5-core/gui#189 Drop workaround for QTBUG-42503 which was fixed in Qt 5.5.0 (prusnak)
- kvanta5-core/gui#194 Save/restore RPCConsole geometry only for window (hebasto)
- kvanta5-core/gui#202 Fix right panel toggle in peers tab (RandyMcMillan)
- kvanta5-core/gui#203 Display plain "Inbound" in peer details (jonatack)
- kvanta5-core/gui#204 Drop buggy TableViewLastColumnResizingFixer class (hebasto)
- kvanta5-core/gui#205, kvanta5-core/gui#229 Save/restore TransactionView and recentRequestsView tables column sizes (hebasto)
- kvanta5-core/gui#206 Display fRelayTxes and `bip152_highbandwidth_{to, from}` in peer details (jonatack)
- kvanta5-core/gui#213 Add Copy Address Action to Payment Requests (jarolrod)
- kvanta5-core/gui#214 Disable requests context menu actions when appropriate (jarolrod)
- kvanta5-core/gui#217 Make warning label look clickable (jarolrod)
- kvanta5-core/gui#219 Prevent the main window popup menu (hebasto)
- kvanta5-core/gui#220 Do not translate file extensions (hebasto)
- kvanta5-core/gui#221 RPCConsole translatable string fixes and improvements (jonatack)
- kvanta5-core/gui#226 Add "Last Block" and "Last Tx" rows to peer details area (jonatack)
- kvanta5-core/gui#233 qt test: Don't bind to regtest port (achow101)
- kvanta5-core/gui#243 Fix issue when disabling the auto-enabled blank wallet checkbox (jarolrod)
- kvanta5-core/gui#246 Revert "qt: Use "fusion" style on macOS Big Sur with old Qt" (hebasto)
- kvanta5-core/gui#248 For values of "Bytes transferred" and "Bytes/s" with 1000-based prefix names use 1000-based divisor instead of 1024-based (wodry)
- kvanta5-core/gui#251 Improve URI/file handling message (hebasto)
- kvanta5-core/gui#256 Save/restore column sizes of the tables in the Peers tab (hebasto)
- kvanta5-core/gui#260 Handle exceptions isntead of crash (hebasto)
- kvanta5-core/gui#263 Revamp context menus (hebasto)
- kvanta5-core/gui#271 Don't clear console prompt when font resizing (jarolrod)
- kvanta5-core/gui#275 Support runtime appearance adjustment on macOS (hebasto)
- kvanta5-core/gui#276 Elide long strings in their middle in the Peers tab (hebasto)
- kvanta5-core/gui#281 Set shortcuts for console's resize buttons (jarolrod)
- kvanta5-core/gui#293 Enable wordWrap for Services (RandyMcMillan)
- kvanta5-core/gui#296 Do not use QObject::tr plural syntax for numbers with a unit symbol (hebasto)
- kvanta5-core/gui#297 Avoid unnecessary translations (hebasto)
- kvanta5-core/gui#298 Peertableview alternating row colors (RandyMcMillan)
- kvanta5-core/gui#300 Remove progress bar on modal overlay (brunoerg)
- kvanta5-core/gui#309 Add access to the Peers tab from the network icon (hebasto)
- kvanta5-core/gui#311 Peers Window rename 'Peer id' to 'Peer' (jarolrod)
- kvanta5-core/gui#313 Optimize string concatenation by default (hebasto)
- kvanta5-core/gui#325 Align numbers in the "Peer Id" column to the right (hebasto)
- kvanta5-core/gui#329 Make console buttons look clickable (jarolrod)
- kvanta5-core/gui#330 Allow prompt icon to be colorized (jarolrod)
- kvanta5-core/gui#331 Make RPC console welcome message translation-friendly (hebasto)
- kvanta5-core/gui#332 Replace disambiguation strings with translator comments (hebasto)
- kvanta5-core/gui#335 test: Use QSignalSpy instead of QEventLoop (jarolrod)
- kvanta5-core/gui#343 Improve the GUI responsiveness when progress dialogs are used (hebasto)
- kvanta5-core/gui#361 Fix GUI segfault caused by kvanta5/kvanta5#22216 (ryanofsky)
- kvanta5-core/gui#362 Add keyboard shortcuts to context menus (luke-jr)
- kvanta5-core/gui#366 Dark Mode fixes/portability (luke-jr)
- kvanta5-core/gui#375 Emit dataChanged signal to dynamically re-sort Peers table (hebasto)
- kvanta5-core/gui#393 Fix regression in "Encrypt Wallet" menu item (hebasto)
- kvanta5-core/gui#396 Ensure external signer option remains disabled without signers (achow101)
- kvanta5-core/gui#406 Handle new added plurals in `kvanta5_en.ts` (hebasto)

### Build system
- kvanta5/kvanta5#17227 Add Android packaging support (icota)
- kvanta5/kvanta5#17920 guix: Build support for macOS (dongcarl)
- kvanta5/kvanta5#18298 Fix Qt processing of configure script for depends with DEBUG=1 (hebasto)
- kvanta5/kvanta5#19160 multiprocess: Add basic spawn and IPC support (ryanofsky)
- kvanta5/kvanta5#19504 Bump minimum python version to 3.6 (ajtowns)
- kvanta5/kvanta5#19522 fix building libconsensus with reduced exports for Darwin targets (fanquake)
- kvanta5/kvanta5#19683 Pin clang search paths for darwin host (dongcarl)
- kvanta5/kvanta5#19764 Split boost into build/host packages + bump + cleanup (dongcarl)
- kvanta5/kvanta5#19817 libtapi 1100.0.11 (fanquake)
- kvanta5/kvanta5#19846 enable unused member function diagnostic (Zero-1729)
- kvanta5/kvanta5#19867 Document and cleanup Qt hacks (fanquake)
- kvanta5/kvanta5#20046 Set `CMAKE_INSTALL_RPATH` for native packages (ryanofsky)
- kvanta5/kvanta5#20223 Drop the leading 0 from the version number (achow101)
- kvanta5/kvanta5#20333 Remove `native_biplist` dependency (fanquake)
- kvanta5/kvanta5#20353 configure: Support -fdebug-prefix-map and -fmacro-prefix-map (ajtowns)
- kvanta5/kvanta5#20359 Various config.site.in improvements and linting (dongcarl)
- kvanta5/kvanta5#20413 Require C++17 compiler (MarcoFalke)
- kvanta5/kvanta5#20419 Set minimum supported macOS to 10.14 (fanquake)
- kvanta5/kvanta5#20421 miniupnpc 2.2.2 (fanquake)
- kvanta5/kvanta5#20422 Mac deployment unification (fanquake)
- kvanta5/kvanta5#20424 Update univalue subtree (MarcoFalke)
- kvanta5/kvanta5#20449 Fix Windows installer build (achow101)
- kvanta5/kvanta5#20468 Warn when generating man pages for binaries built from a dirty branch (tylerchambers)
- kvanta5/kvanta5#20469 Avoid secp256k1.h include from system (dergoegge)
- kvanta5/kvanta5#20470 Replace genisoimage with xorriso (dongcarl)
- kvanta5/kvanta5#20471 Use C++17 in depends (fanquake)
- kvanta5/kvanta5#20496 Drop unneeded macOS framework dependencies (hebasto)
- kvanta5/kvanta5#20520 Do not force Precompiled Headers (PCH) for building Qt on Linux (hebasto)
- kvanta5/kvanta5#20549 Support make src/kvanta5-node and src/kvanta5-gui (promag)
- kvanta5/kvanta5#20565 Ensure PIC build for bdb on Android (BlockMechanic)
- kvanta5/kvanta5#20594 Fix getauxval calls in randomenv.cpp (jonasschnelli)
- kvanta5/kvanta5#20603 Update crc32c subtree (MarcoFalke)
- kvanta5/kvanta5#20609 configure: output notice that test binary is disabled by fuzzing (apoelstra)
- kvanta5/kvanta5#20619 guix: Quality of life improvements (dongcarl)
- kvanta5/kvanta5#20629 Improve id string robustness (dongcarl)
- kvanta5/kvanta5#20641 Use Qt top-level build facilities (hebasto)
- kvanta5/kvanta5#20650 Drop workaround for a fixed bug in Qt build system (hebasto)
- kvanta5/kvanta5#20673 Use more legible qmake commands in qt package (hebasto)
- kvanta5/kvanta5#20684 Define .INTERMEDIATE target once only (hebasto)
- kvanta5/kvanta5#20720 more robustly check for fcf-protection support (fanquake)
- kvanta5/kvanta5#20734 Make platform-specific targets available for proper platform builds only (hebasto)
- kvanta5/kvanta5#20936 build fuzz tests by default (danben)
- kvanta5/kvanta5#20937 guix: Make nsis reproducible by respecting SOURCE-DATE-EPOCH (dongcarl)
- kvanta5/kvanta5#20938 fix linking against -latomic when building for riscv (fanquake)
- kvanta5/kvanta5#20939 fix `RELOC_SECTION` security check for kvanta5-util (fanquake)
- kvanta5/kvanta5#20963 gitian-linux: Build binaries for 64-bit POWER (continued) (laanwj)
- kvanta5/kvanta5#21036 gitian: Bump descriptors to focal for 22.0 (fanquake)
- kvanta5/kvanta5#21045 Adds switch to enable/disable randomized base address in MSVC builds (EthanHeilman)
- kvanta5/kvanta5#21065 make macOS HOST in download-osx generic (fanquake)
- kvanta5/kvanta5#21078 guix: only download sources for hosts being built (fanquake)
- kvanta5/kvanta5#21116 Disable --disable-fuzz-binary for gitian/guix builds (hebasto)
- kvanta5/kvanta5#21182 remove mostly pointless `BOOST_PROCESS` macro (fanquake)
- kvanta5/kvanta5#21205 actually fail when Boost is missing (fanquake)
- kvanta5/kvanta5#21209 use newer source for libnatpmp (fanquake)
- kvanta5/kvanta5#21226 Fix fuzz binary compilation under windows (danben)
- kvanta5/kvanta5#21231 Add /opt/homebrew to path to look for boost libraries (fyquah)
- kvanta5/kvanta5#21239 guix: Add codesignature attachment support for osx+win (dongcarl)
- kvanta5/kvanta5#21250 Make `HAVE_O_CLOEXEC` available outside LevelDB (bugfix) (theStack)
- kvanta5/kvanta5#21272 guix: Passthrough `SDK_PATH` into container (dongcarl)
- kvanta5/kvanta5#21274 assumptions:  Assume C++17 (fanquake)
- kvanta5/kvanta5#21286 Bump minimum Qt version to 5.9.5 (hebasto)
- kvanta5/kvanta5#21298 guix: Bump time-machine, glibc, and linux-headers (dongcarl)
- kvanta5/kvanta5#21304 guix: Add guix-clean script + establish gc-root for container profiles (dongcarl)
- kvanta5/kvanta5#21320 fix libnatpmp macos cross compile (fanquake)
- kvanta5/kvanta5#21321 guix: Add curl to required tool list (hebasto)
- kvanta5/kvanta5#21333 set Unicode true for NSIS installer (fanquake)
- kvanta5/kvanta5#21339 Make `AM_CONDITIONAL([ENABLE_EXTERNAL_SIGNER])` unconditional (hebasto)
- kvanta5/kvanta5#21349 Fix fuzz-cuckoocache cross-compiling with DEBUG=1 (hebasto)
- kvanta5/kvanta5#21354 build, doc: Drop no longer required packages from macOS cross-compiling dependencies (hebasto)
- kvanta5/kvanta5#21363 build, qt: Improve Qt static plugins/libs check code (hebasto)
- kvanta5/kvanta5#21375 guix: Misc feedback-based fixes + hier restructuring (dongcarl)
- kvanta5/kvanta5#21376 Qt 5.12.10 (fanquake)
- kvanta5/kvanta5#21382 Clean remnants of QTBUG-34748 fix (hebasto)
- kvanta5/kvanta5#21400 Fix regression introduced in #21363 (hebasto)
- kvanta5/kvanta5#21403 set --build when configuring packages in depends (fanquake)
- kvanta5/kvanta5#21421 don't try and use -fstack-clash-protection on Windows (fanquake)
- kvanta5/kvanta5#21423 Cleanups and follow ups after bumping Qt to 5.12.10 (hebasto)
- kvanta5/kvanta5#21427 Fix `id_string` invocations (dongcarl)
- kvanta5/kvanta5#21430 Add -Werror=implicit-fallthrough compile flag (hebasto)
- kvanta5/kvanta5#21457 Split libtapi and clang out of `native_cctools` (fanquake)
- kvanta5/kvanta5#21462 guix: Add guix-{attest,verify} scripts (dongcarl)
- kvanta5/kvanta5#21495 build, qt: Fix static builds on macOS Big Sur (hebasto)
- kvanta5/kvanta5#21497 Do not opt-in unused CoreWLAN stuff in depends for macOS (hebasto)
- kvanta5/kvanta5#21543 Enable safe warnings for msvc builds (hebasto)
- kvanta5/kvanta5#21565 Make `kvanta5_qt.m4` more generic (fanquake)
- kvanta5/kvanta5#21610 remove -Wdeprecated-register from NOWARN flags (fanquake)
- kvanta5/kvanta5#21613 enable -Wdocumentation (fanquake)
- kvanta5/kvanta5#21629 Fix configuring when building depends with `NO_BDB=1` (fanquake)
- kvanta5/kvanta5#21654 build, qt: Make Qt rcc output always deterministic (hebasto)
- kvanta5/kvanta5#21655 build, qt: No longer need to set `QT_RCC_TEST=1` for determinism (hebasto)
- kvanta5/kvanta5#21658 fix make deploy for arm64-darwin (sgulls)
- kvanta5/kvanta5#21694 Use XLIFF file to provide more context to Transifex translators (hebasto)
- kvanta5/kvanta5#21708, kvanta5/kvanta5#21593 Drop pointless sed commands (hebasto)
- kvanta5/kvanta5#21731 Update msvc build to use Qt5.12.10 binaries (sipsorcery)
- kvanta5/kvanta5#21733 Re-add command to install vcpkg (dplusplus1024)
- kvanta5/kvanta5#21793 Use `-isysroot` over `--sysroot` on macOS (fanquake)
- kvanta5/kvanta5#21869 Add missing `-D_LIBCPP_DEBUG=1` to debug flags (MarcoFalke)
- kvanta5/kvanta5#21889 macho: check for control flow instrumentation (fanquake)
- kvanta5/kvanta5#21920 Improve macro for testing -latomic requirement (MarcoFalke)
- kvanta5/kvanta5#21991 libevent 2.1.12-stable (fanquake)
- kvanta5/kvanta5#22054 Bump Qt version to 5.12.11 (hebasto)
- kvanta5/kvanta5#22063 Use Qt archive of the same version as the compiled binaries (hebasto)
- kvanta5/kvanta5#22070 Don't use cf-protection when targeting arm-apple-darwin (fanquake)
- kvanta5/kvanta5#22071 Latest config.guess and config.sub (fanquake)
- kvanta5/kvanta5#22075 guix: Misc leftover usability improvements (dongcarl)
- kvanta5/kvanta5#22123 Fix qt.mk for mac arm64 (promag)
- kvanta5/kvanta5#22174 build, qt: Fix libraries linking order for Linux hosts (hebasto)
- kvanta5/kvanta5#22182 guix: Overhaul how guix-{attest,verify} works and hierarchy (dongcarl)
- kvanta5/kvanta5#22186 build, qt: Fix compiling qt package in depends with GCC 11 (hebasto)
- kvanta5/kvanta5#22199 macdeploy: minor fixups and simplifications (fanquake)
- kvanta5/kvanta5#22230 Fix MSVC linker /SubSystem option for kvanta5-qt.exe (hebasto)
- kvanta5/kvanta5#22234 Mark print-% target as phony (dgoncharov)
- kvanta5/kvanta5#22238 improve detection of eBPF support (fanquake)
- kvanta5/kvanta5#22258 Disable deprecated-copy warning only when external warnings are enabled (MarcoFalke)
- kvanta5/kvanta5#22320 set minimum required Boost to 1.64.0 (fanquake)
- kvanta5/kvanta5#22348 Fix cross build for Windows with Boost Process (hebasto)
- kvanta5/kvanta5#22365 guix: Avoid relying on newer symbols by rebasing our cross toolchains on older glibcs (dongcarl)
- kvanta5/kvanta5#22381 guix: Test security-check sanity before performing them (with macOS) (fanquake)
- kvanta5/kvanta5#22405 Remove --enable-glibc-back-compat from Guix build (fanquake)
- kvanta5/kvanta5#22406 Remove --enable-determinism configure option (fanquake)
- kvanta5/kvanta5#22410 Avoid GCC 7.1 ABI change warning in guix build (sipa)
- kvanta5/kvanta5#22436 use aarch64 Clang if cross-compiling for darwin on aarch64 (fanquake)
- kvanta5/kvanta5#22465 guix: Pin kernel-header version, time-machine to upstream 1.3.0 commit (dongcarl)
- kvanta5/kvanta5#22511 guix: Silence `getent(1)` invocation, doc fixups (dongcarl)
- kvanta5/kvanta5#22531 guix: Fixes to guix-{attest,verify} (achow101)
- kvanta5/kvanta5#22642 release: Release with separate sha256sums and sig files (dongcarl)
- kvanta5/kvanta5#22685 clientversion: No suffix `#if CLIENT_VERSION_IS_RELEASE` (dongcarl)
- kvanta5/kvanta5#22713 Fix build with Boost 1.77.0 (sizeofvoid)

### Tests and QA
- kvanta5/kvanta5#14604 Add test and refactor `feature_block.py` (sanket1729)
- kvanta5/kvanta5#17556 Change `feature_config_args.py` not to rely on strange regtest=0 behavior (ryanofsky)
- kvanta5/kvanta5#18795 wallet issue with orphaned rewards (domob1812)
- kvanta5/kvanta5#18847 compressor: Use a prevector in CompressScript serialization (jb55)
- kvanta5/kvanta5#19259 fuzz: Add fuzzing harness for LoadMempool(…) and DumpMempool(…) (practicalswift)
- kvanta5/kvanta5#19315 Allow outbound & block-relay-only connections in functional tests. (amitiuttarwar)
- kvanta5/kvanta5#19698 Apply strict verification flags for transaction tests and assert backwards compatibility (glozow)
- kvanta5/kvanta5#19801 Check for all possible `OP_CLTV` fail reasons in `feature_cltv.py` (BIP 65) (theStack)
- kvanta5/kvanta5#19893 Remove or explain syncwithvalidationinterfacequeue (MarcoFalke)
- kvanta5/kvanta5#19972 fuzz: Add fuzzing harness for node eviction logic (practicalswift)
- kvanta5/kvanta5#19982 Fix inconsistent lock order in `wallet_tests/CreateWallet` (hebasto)
- kvanta5/kvanta5#20000 Fix creation of "std::string"s with \0s (vasild)
- kvanta5/kvanta5#20047 Use `wait_for_{block,header}` helpers in `p2p_fingerprint.py` (theStack)
- kvanta5/kvanta5#20171 Add functional test `test_txid_inv_delay` (ariard)
- kvanta5/kvanta5#20189 Switch to BIP341's suggested scheme for outputs without script (sipa)
- kvanta5/kvanta5#20248 Fix length of R check in `key_signature_tests` (dgpv)
- kvanta5/kvanta5#20276, kvanta5/kvanta5#20385, kvanta5/kvanta5#20688, kvanta5/kvanta5#20692 Run various mempool tests even with wallet disabled (mjdietzx)
- kvanta5/kvanta5#20323 Create or use existing properly initialized NodeContexts (dongcarl)
- kvanta5/kvanta5#20354 Add `feature_taproot.py --previous_release` (MarcoFalke)
- kvanta5/kvanta5#20370 fuzz: Version handshake (MarcoFalke)
- kvanta5/kvanta5#20377 fuzz: Fill various small fuzzing gaps (practicalswift)
- kvanta5/kvanta5#20425 fuzz: Make CAddrMan fuzzing harness deterministic (practicalswift)
- kvanta5/kvanta5#20430 Sanitizers: Add suppression for unsigned-integer-overflow in libstdc++ (jonasschnelli)
- kvanta5/kvanta5#20437 fuzz: Avoid time-based "non-determinism" in fuzzing harnesses by using mocked GetTime() (practicalswift)
- kvanta5/kvanta5#20458 Add `is_bdb_compiled` helper (Sjors)
- kvanta5/kvanta5#20466 Fix intermittent `p2p_fingerprint` issue (MarcoFalke)
- kvanta5/kvanta5#20472 Add testing of ParseInt/ParseUInt edge cases with leading +/-/0:s (practicalswift)
- kvanta5/kvanta5#20507 sync: print proper lock order location when double lock is detected (vasild)
- kvanta5/kvanta5#20522 Fix sync issue in `disconnect_p2ps` (amitiuttarwar)
- kvanta5/kvanta5#20524 Move `MIN_VERSION_SUPPORTED` to p2p.py (jnewbery)
- kvanta5/kvanta5#20540 Fix `wallet_multiwallet` issue on windows (MarcoFalke)
- kvanta5/kvanta5#20560 fuzz: Link all targets once (MarcoFalke)
- kvanta5/kvanta5#20567 Add option to git-subtree-check to do full check, add help (laanwj)
- kvanta5/kvanta5#20569 Fix intermittent `wallet_multiwallet` issue with `got_loading_error` (MarcoFalke)
- kvanta5/kvanta5#20613 Use Popen.wait instead of RPC in `assert_start_raises_init_error` (MarcoFalke)
- kvanta5/kvanta5#20663 fuzz: Hide `script_assets_test_minimizer` (MarcoFalke)
- kvanta5/kvanta5#20674 fuzz: Call SendMessages after ProcessMessage to increase coverage (MarcoFalke)
- kvanta5/kvanta5#20683 Fix restart node race (MarcoFalke)
- kvanta5/kvanta5#20686 fuzz: replace CNode code with fuzz/util.h::ConsumeNode() (jonatack)
- kvanta5/kvanta5#20733 Inline non-member functions with body in fuzzing headers (pstratem)
- kvanta5/kvanta5#20737 Add missing assignment in `mempool_resurrect.py` (MarcoFalke)
- kvanta5/kvanta5#20745 Correct `epoll_ctl` data race suppression (hebasto)
- kvanta5/kvanta5#20748 Add race:SendZmqMessage tsan suppression (MarcoFalke)
- kvanta5/kvanta5#20760 Set correct nValue for multi-op-return policy check (MarcoFalke)
- kvanta5/kvanta5#20761 fuzz: Check that `NULL_DATA` is unspendable (MarcoFalke)
- kvanta5/kvanta5#20765 fuzz: Check that certain script TxoutType are nonstandard (mjdietzx)
- kvanta5/kvanta5#20772 fuzz: Bolster ExtractDestination(s) checks (mjdietzx)
- kvanta5/kvanta5#20789 fuzz: Rework strong and weak net enum fuzzing (MarcoFalke)
- kvanta5/kvanta5#20828 fuzz: Introduce CallOneOf helper to replace switch-case (MarcoFalke)
- kvanta5/kvanta5#20839 fuzz: Avoid extraneous copy of input data, using Span<> (MarcoFalke)
- kvanta5/kvanta5#20844 Add sanitizer suppressions for AMD EPYC CPUs (MarcoFalke)
- kvanta5/kvanta5#20857 Update documentation in `feature_csv_activation.py` (PiRK)
- kvanta5/kvanta5#20876 Replace getmempoolentry with testmempoolaccept in MiniWallet (MarcoFalke)
- kvanta5/kvanta5#20881 fuzz: net permission flags in net processing (MarcoFalke)
- kvanta5/kvanta5#20882 fuzz: Add missing muhash registration (MarcoFalke)
- kvanta5/kvanta5#20908 fuzz: Use mocktime in `process_message*` fuzz targets (MarcoFalke)
- kvanta5/kvanta5#20915 fuzz: Fail if message type is not fuzzed (MarcoFalke)
- kvanta5/kvanta5#20946 fuzz: Consolidate fuzzing TestingSetup initialization (dongcarl)
- kvanta5/kvanta5#20954 Declare `nodes` type `in test_framework.py` (kiminuo)
- kvanta5/kvanta5#20955 Fix `get_previous_releases.py` for aarch64 (MarcoFalke)
- kvanta5/kvanta5#20969 check that getblockfilter RPC fails without block filter index (theStack)
- kvanta5/kvanta5#20971 Work around libFuzzer deadlock (MarcoFalke)
- kvanta5/kvanta5#20993 Store subversion (user agent) as string in `msg_version` (theStack)
- kvanta5/kvanta5#20995 fuzz: Avoid initializing version to less than `MIN_PEER_PROTO_VERSION` (MarcoFalke)
- kvanta5/kvanta5#20998 Fix BlockToJsonVerbose benchmark (martinus)
- kvanta5/kvanta5#21003 Move MakeNoLogFileContext to `libtest_util`, and use it in bench (MarcoFalke)
- kvanta5/kvanta5#21008 Fix zmq test flakiness, improve speed (theStack)
- kvanta5/kvanta5#21023 fuzz: Disable shuffle when merge=1 (MarcoFalke)
- kvanta5/kvanta5#21037 fuzz: Avoid designated initialization (C++20) in fuzz tests (practicalswift)
- kvanta5/kvanta5#21042 doc, test: Improve `setup_clean_chain` documentation (fjahr)
- kvanta5/kvanta5#21080 fuzz: Configure check for main function (take 2) (MarcoFalke)
- kvanta5/kvanta5#21084 Fix timeout decrease in `feature_assumevalid` (brunoerg)
- kvanta5/kvanta5#21096 Re-add dead code detection (flack)
- kvanta5/kvanta5#21100 Remove unused function `xor_bytes` (theStack)
- kvanta5/kvanta5#21115 Fix Windows cross build (hebasto)
- kvanta5/kvanta5#21117 Remove `assert_blockchain_height` (MarcoFalke)
- kvanta5/kvanta5#21121 Small unit test improvements, including helper to make mempool transaction (amitiuttarwar)
- kvanta5/kvanta5#21124 Remove unnecessary assignment in bdb (brunoerg)
- kvanta5/kvanta5#21125 Change `BOOST_CHECK` to `BOOST_CHECK_EQUAL` for paths (kiminuo)
- kvanta5/kvanta5#21142, kvanta5/kvanta5#21512 fuzz: Add `tx_pool` fuzz target (MarcoFalke)
- kvanta5/kvanta5#21165 Use mocktime in `test_seed_peers` (dhruv)
- kvanta5/kvanta5#21169 fuzz: Add RPC interface fuzzing. Increase fuzzing coverage from 65% to 70% (practicalswift)
- kvanta5/kvanta5#21170 bench: Add benchmark to write json into a string (martinus)
- kvanta5/kvanta5#21178 Run `mempool_reorg.py` even with wallet disabled (DariusParvin)
- kvanta5/kvanta5#21185 fuzz: Remove expensive and redundant muhash from crypto fuzz target (MarcoFalke)
- kvanta5/kvanta5#21200 Speed up `rpc_blockchain.py` by removing miniwallet.generate() (MarcoFalke)
- kvanta5/kvanta5#21211 Move `P2WSH_OP_TRUE` to shared test library (MarcoFalke)
- kvanta5/kvanta5#21228 Avoid comparision of integers with different signs (jonasschnelli)
- kvanta5/kvanta5#21230 Fix `NODE_NETWORK_LIMITED_MIN_BLOCKS` disconnection (MarcoFalke)
- kvanta5/kvanta5#21252 Add missing wait for sync to `feature_blockfilterindex_prune` (MarcoFalke)
- kvanta5/kvanta5#21254 Avoid connecting to real network when running tests (MarcoFalke)
- kvanta5/kvanta5#21264 fuzz: Two scripted diff renames (MarcoFalke)
- kvanta5/kvanta5#21280 Bug fix in `transaction_tests` (glozow)
- kvanta5/kvanta5#21293 Replace accidentally placed bit-OR with logical-OR (hebasto)
- kvanta5/kvanta5#21297 `feature_blockfilterindex_prune.py` improvements (jonatack)
- kvanta5/kvanta5#21310 zmq test: fix sync-up by matching notification to generated block (theStack)
- kvanta5/kvanta5#21334 Additional BIP9 tests (Sjors)
- kvanta5/kvanta5#21338 Add functional test for anchors.dat (brunoerg)
- kvanta5/kvanta5#21345 Bring `p2p_leak.py` up to date (mzumsande)
- kvanta5/kvanta5#21357 Unconditionally check for fRelay field in test framework (jarolrod)
- kvanta5/kvanta5#21358 fuzz: Add missing include (`test/util/setup_common.h`) (MarcoFalke)
- kvanta5/kvanta5#21371 fuzz: fix gcc Woverloaded-virtual build warnings (jonatack)
- kvanta5/kvanta5#21373 Generate fewer blocks in `feature_nulldummy` to fix timeouts, speed up (jonatack)
- kvanta5/kvanta5#21390 Test improvements for UTXO set hash tests (fjahr)
- kvanta5/kvanta5#21410 increase `rpc_timeout` for fundrawtx `test_transaction_too_large` (jonatack)
- kvanta5/kvanta5#21411 add logging, reduce blocks, move `sync_all` in `wallet_` groups (jonatack)
- kvanta5/kvanta5#21438 Add ParseUInt8() test coverage (jonatack)
- kvanta5/kvanta5#21443 fuzz: Implement `fuzzed_dns_lookup_function` as a lambda (practicalswift)
- kvanta5/kvanta5#21445 cirrus: Use SSD cluster for speedup (MarcoFalke)
- kvanta5/kvanta5#21477 Add test for CNetAddr::ToString IPv6 address formatting (RFC 5952) (practicalswift)
- kvanta5/kvanta5#21487 fuzz: Use ConsumeWeakEnum in addrman for service flags (MarcoFalke)
- kvanta5/kvanta5#21488 Add ParseUInt16() unit test and fuzz coverage (jonatack)
- kvanta5/kvanta5#21491 test: remove duplicate assertions in util_tests (jonatack)
- kvanta5/kvanta5#21522 fuzz: Use PickValue where possible (MarcoFalke)
- kvanta5/kvanta5#21531 remove qt byteswap compattests (fanquake)
- kvanta5/kvanta5#21557 small cleanup in RPCNestedTests tests (fanquake)
- kvanta5/kvanta5#21586 Add missing suppression for signed-integer-overflow:txmempool.cpp (MarcoFalke)
- kvanta5/kvanta5#21592 Remove option to make TestChain100Setup non-deterministic (MarcoFalke)
- kvanta5/kvanta5#21597 Document `race:validation_chainstatemanager_tests` suppression (MarcoFalke)
- kvanta5/kvanta5#21599 Replace file level integer overflow suppression with function level suppression (practicalswift)
- kvanta5/kvanta5#21604 Document why no symbol names can be used for suppressions (MarcoFalke)
- kvanta5/kvanta5#21606 fuzz: Extend psbt fuzz target a bit (MarcoFalke)
- kvanta5/kvanta5#21617 fuzz: Fix uninitialized read in i2p test (MarcoFalke)
- kvanta5/kvanta5#21630 fuzz: split FuzzedSock interface and implementation (vasild)
- kvanta5/kvanta5#21634 Skip SQLite fsyncs while testing (achow101)
- kvanta5/kvanta5#21669 Remove spurious double lock tsan suppressions by bumping to clang-12 (MarcoFalke)
- kvanta5/kvanta5#21676 Use mocktime to avoid intermittent failure in `rpc_tests` (MarcoFalke)
- kvanta5/kvanta5#21677 fuzz: Avoid use of low file descriptor ids (which may be in use) in FuzzedSock (practicalswift)
- kvanta5/kvanta5#21678 Fix TestPotentialDeadLockDetected suppression (hebasto)
- kvanta5/kvanta5#21689 Remove intermittently failing and not very meaningful `BOOST_CHECK` in `cnetaddr_basic` (practicalswift)
- kvanta5/kvanta5#21691 Check that no versionbits are re-used (MarcoFalke)
- kvanta5/kvanta5#21707 Extend functional tests for addr relay (mzumsande)
- kvanta5/kvanta5#21712 Test default `include_mempool` value of gettxout (promag)
- kvanta5/kvanta5#21738 Use clang-12 for ASAN, Add missing suppression (MarcoFalke)
- kvanta5/kvanta5#21740 add new python linter to check file names and permissions (windsok)
- kvanta5/kvanta5#21749 Bump shellcheck version (hebasto)
- kvanta5/kvanta5#21754 Run `feature_cltv` with MiniWallet (MarcoFalke)
- kvanta5/kvanta5#21762 Speed up `mempool_spend_coinbase.py` (MarcoFalke)
- kvanta5/kvanta5#21773 fuzz: Ensure prevout is consensus-valid (MarcoFalke)
- kvanta5/kvanta5#21777 Fix `feature_notifications.py` intermittent issue (MarcoFalke)
- kvanta5/kvanta5#21785 Fix intermittent issue in `p2p_addr_relay.py` (MarcoFalke)
- kvanta5/kvanta5#21787 Fix off-by-ones in `rpc_fundrawtransaction` assertions (jonatack)
- kvanta5/kvanta5#21792 Fix intermittent issue in `p2p_segwit.py` (MarcoFalke)
- kvanta5/kvanta5#21795 fuzz: Terminate immediately if a fuzzing harness tries to perform a DNS lookup (belt and suspenders) (practicalswift)
- kvanta5/kvanta5#21798 fuzz: Create a block template in `tx_pool` targets (MarcoFalke)
- kvanta5/kvanta5#21804 Speed up `p2p_segwit.py` (jnewbery)
- kvanta5/kvanta5#21810 fuzz: Various RPC fuzzer follow-ups (practicalswift)
- kvanta5/kvanta5#21814 Fix `feature_config_args.py` intermittent issue (MarcoFalke)
- kvanta5/kvanta5#21821 Add missing test for empty P2WSH redeem (MarcoFalke)
- kvanta5/kvanta5#21822 Resolve bug in `interface_kvanta5_cli.py` (klementtan)
- kvanta5/kvanta5#21846 fuzz: Add `-fsanitize=integer` suppression needed for RPC fuzzer (`generateblock`) (practicalswift)
- kvanta5/kvanta5#21849 fuzz: Limit toxic test globals to their respective scope (MarcoFalke)
- kvanta5/kvanta5#21867 use MiniWallet for `p2p_blocksonly.py` (theStack)
- kvanta5/kvanta5#21873 minor fixes & improvements for files linter test (windsok)
- kvanta5/kvanta5#21874 fuzz: Add `WRITE_ALL_FUZZ_TARGETS_AND_ABORT` (MarcoFalke)
- kvanta5/kvanta5#21884 fuzz: Remove unused --enable-danger-fuzz-link-all option (MarcoFalke)
- kvanta5/kvanta5#21890 fuzz: Limit ParseISO8601DateTime fuzzing to 32-bit (MarcoFalke)
- kvanta5/kvanta5#21891 fuzz: Remove strprintf test cases that are known to fail (MarcoFalke)
- kvanta5/kvanta5#21892 fuzz: Avoid excessively large min fee rate in `tx_pool` (MarcoFalke)
- kvanta5/kvanta5#21895 Add TSA annotations to the WorkQueue class members (hebasto)
- kvanta5/kvanta5#21900 use MiniWallet for `feature_csv_activation.py` (theStack)
- kvanta5/kvanta5#21909 fuzz: Limit max insertions in timedata fuzz test (MarcoFalke)
- kvanta5/kvanta5#21922 fuzz: Avoid timeout in EncodeBase58 (MarcoFalke)
- kvanta5/kvanta5#21927 fuzz: Run const CScript member functions only once (MarcoFalke)
- kvanta5/kvanta5#21929 fuzz: Remove incorrect float round-trip serialization test (MarcoFalke)
- kvanta5/kvanta5#21936 fuzz: Terminate immediately if a fuzzing harness tries to create a TCP socket (belt and suspenders) (practicalswift)
- kvanta5/kvanta5#21941 fuzz: Call const member functions in addrman fuzz test only once (MarcoFalke)
- kvanta5/kvanta5#21945 add P2PK support to MiniWallet (theStack)
- kvanta5/kvanta5#21948 Fix off-by-one in mockscheduler test RPC (MarcoFalke)
- kvanta5/kvanta5#21953 fuzz: Add `utxo_snapshot` target (MarcoFalke)
- kvanta5/kvanta5#21970 fuzz: Add missing CheckTransaction before CheckTxInputs (MarcoFalke)
- kvanta5/kvanta5#21989 Use `COINBASE_MATURITY` in functional tests (kiminuo)
- kvanta5/kvanta5#22003 Add thread safety annotations (ajtowns)
- kvanta5/kvanta5#22004 fuzz: Speed up transaction fuzz target (MarcoFalke)
- kvanta5/kvanta5#22005 fuzz: Speed up banman fuzz target (MarcoFalke)
- kvanta5/kvanta5#22029 [fuzz] Improve transport deserialization fuzz test coverage (dhruv)
- kvanta5/kvanta5#22048 MiniWallet: introduce enum type for output mode (theStack)
- kvanta5/kvanta5#22057 use MiniWallet (P2PK mode) for `feature_dersig.py` (theStack)
- kvanta5/kvanta5#22065 Mark `CheckTxInputs` `[[nodiscard]]`. Avoid UUM in fuzzing harness `coins_view` (practicalswift)
- kvanta5/kvanta5#22069 fuzz: don't try and use fopencookie() when building for Android (fanquake)
- kvanta5/kvanta5#22082 update nanobench from release 4.0.0 to 4.3.4 (martinus)
- kvanta5/kvanta5#22086 remove BasicTestingSetup from unit tests that don't need it (fanquake)
- kvanta5/kvanta5#22089 MiniWallet: fix fee calculation for P2PK and check tx vsize (theStack)
- kvanta5/kvanta5#21107, kvanta5/kvanta5#22092 Convert documentation into type annotations (fanquake)
- kvanta5/kvanta5#22095 Additional BIP32 test vector for hardened derivation with leading zeros (kristapsk)
- kvanta5/kvanta5#22103 Fix IPv6 check on BSD systems (n-thumann)
- kvanta5/kvanta5#22118 check anchors.dat when node starts for the first time (brunoerg)
- kvanta5/kvanta5#22120 `p2p_invalid_block`: Check that a block rejected due to too-new tim… (willcl-ark)
- kvanta5/kvanta5#22153 Fix `p2p_leak.py` intermittent failure (mzumsande)
- kvanta5/kvanta5#22169 p2p, rpc, fuzz: various tiny follow-ups (jonatack)
- kvanta5/kvanta5#22176 Correct outstanding -Werror=sign-compare errors (Empact)
- kvanta5/kvanta5#22180 fuzz: Increase branch coverage of the float fuzz target (MarcoFalke)
- kvanta5/kvanta5#22187 Add `sync_blocks` in `wallet_orphanedreward.py` (domob1812)
- kvanta5/kvanta5#22201 Fix TestShell to allow running in Jupyter Notebook (josibake)
- kvanta5/kvanta5#22202 Add temporary coinstats suppressions (MarcoFalke)
- kvanta5/kvanta5#22203 Use ConnmanTestMsg from test lib in `denialofservice_tests` (MarcoFalke)
- kvanta5/kvanta5#22210 Use MiniWallet in `test_no_inherited_signaling` RBF test (MarcoFalke)
- kvanta5/kvanta5#22224 Update msvc and appveyor builds to use Qt5.12.11 binaries (sipsorcery)
- kvanta5/kvanta5#22249 Kill process group to avoid dangling processes when using `--failfast` (S3RK)
- kvanta5/kvanta5#22267 fuzz: Speed up crypto fuzz target (MarcoFalke)
- kvanta5/kvanta5#22270 Add kvanta5-util tests (+refactors) (MarcoFalke)
- kvanta5/kvanta5#22271 fuzz: Assert roundtrip equality for `CPubKey` (theStack)
- kvanta5/kvanta5#22279 fuzz: add missing ECCVerifyHandle to `base_encode_decode` (apoelstra)
- kvanta5/kvanta5#22292 bench, doc: benchmarking updates and fixups (jonatack)
- kvanta5/kvanta5#22306 Improvements to `p2p_addr_relay.py` (amitiuttarwar)
- kvanta5/kvanta5#22310 Add functional test for replacement relay fee check (ariard)
- kvanta5/kvanta5#22311 Add missing syncwithvalidationinterfacequeue in `p2p_blockfilters` (MarcoFalke)
- kvanta5/kvanta5#22313 Add missing `sync_all` to `feature_coinstatsindex` (MarcoFalke)
- kvanta5/kvanta5#22322 fuzz: Check banman roundtrip (MarcoFalke)
- kvanta5/kvanta5#22363 Use `script_util` helpers for creating P2{PKH,SH,WPKH,WSH} scripts (theStack)
- kvanta5/kvanta5#22399 fuzz: Rework CTxDestination fuzzing (MarcoFalke)
- kvanta5/kvanta5#22408 add tests for `bad-txns-prevout-null` reject reason (theStack)
- kvanta5/kvanta5#22445 fuzz: Move implementations of non-template fuzz helpers from util.h to util.cpp (sriramdvt)
- kvanta5/kvanta5#22446 Fix `wallet_listdescriptors.py` if bdb is not compiled (hebasto)
- kvanta5/kvanta5#22447 Whitelist `rpc_rawtransaction` peers to speed up tests (jonatack)
- kvanta5/kvanta5#22742 Use proper target in `do_fund_send` (S3RK)

### Miscellaneous
- kvanta5/kvanta5#19337 sync: Detect double lock from the same thread (vasild)
- kvanta5/kvanta5#19809 log: Prefix log messages with function name and source code location if -logsourcelocations is set (practicalswift)
- kvanta5/kvanta5#19866 eBPF Linux tracepoints (jb55)
- kvanta5/kvanta5#20024 init: Fix incorrect warning "Reducing -maxconnections from N to N-1, because of system limitations" (practicalswift)
- kvanta5/kvanta5#20145 contrib: Add getcoins.py script to get coins from (signet) faucet (kallewoof)
- kvanta5/kvanta5#20255 util: Add assume() identity function (MarcoFalke)
- kvanta5/kvanta5#20288 script, doc: Contrib/seeds updates (jonatack)
- kvanta5/kvanta5#20358 src/randomenv.cpp: Fix build on uclibc (ffontaine)
- kvanta5/kvanta5#20406 util: Avoid invalid integer negation in formatmoney and valuefromamount (practicalswift)
- kvanta5/kvanta5#20434 contrib: Parse elf directly for symbol and security checks (laanwj)
- kvanta5/kvanta5#20451 lint: Run mypy over contrib/devtools (fanquake)
- kvanta5/kvanta5#20476 contrib: Add test for elf symbol-check (laanwj)
- kvanta5/kvanta5#20530 lint: Update cppcheck linter to c++17 and improve explicit usage (fjahr)
- kvanta5/kvanta5#20589 log: Clarify that failure to read/write `fee_estimates.dat` is non-fatal (MarcoFalke)
- kvanta5/kvanta5#20602 util: Allow use of c++14 chrono literals (MarcoFalke)
- kvanta5/kvanta5#20605 init: Signal-safe instant shutdown (laanwj)
- kvanta5/kvanta5#20608 contrib: Add symbol check test for PE binaries (fanquake)
- kvanta5/kvanta5#20689 contrib: Replace binary verification script verify.sh with python rewrite (theStack)
- kvanta5/kvanta5#20715 util: Add argsmanager::getcommand() and use it in kvanta5-wallet (MarcoFalke)
- kvanta5/kvanta5#20735 script: Remove outdated extract-osx-sdk.sh (hebasto)
- kvanta5/kvanta5#20817 lint: Update list of spelling linter false positives, bump to codespell 2.0.0 (theStack)
- kvanta5/kvanta5#20884 script: Improve robustness of kvanta5d.service on startup (hebasto)
- kvanta5/kvanta5#20906 contrib: Embed c++11 patch in `install_db4.sh` (gruve-p)
- kvanta5/kvanta5#21004 contrib: Fix docker args conditional in gitian-build (setpill)
- kvanta5/kvanta5#21007 kvanta5d: Add -daemonwait option to wait for initialization (laanwj)
- kvanta5/kvanta5#21041 log: Move "Pre-allocating up to position 0x[…] in […].dat" log message to debug category (practicalswift)
- kvanta5/kvanta5#21059 Drop boost/preprocessor dependencies (hebasto)
- kvanta5/kvanta5#21087 guix: Passthrough `BASE_CACHE` into container (dongcarl)
- kvanta5/kvanta5#21088 guix: Jump forwards in time-machine and adapt (dongcarl)
- kvanta5/kvanta5#21089 guix: Add support for powerpc64{,le} (dongcarl)
- kvanta5/kvanta5#21110 util: Remove boost `posix_time` usage from `gettime*` (fanquake)
- kvanta5/kvanta5#21111 Improve OpenRC initscript (parazyd)
- kvanta5/kvanta5#21123 code style: Add EditorConfig file (kiminuo)
- kvanta5/kvanta5#21173 util: Faster hexstr => 13% faster blocktojson (martinus)
- kvanta5/kvanta5#21221 tools: Allow argument/parameter bin packing in clang-format (jnewbery)
- kvanta5/kvanta5#21244 Move GetDataDir to ArgsManager (kiminuo)
- kvanta5/kvanta5#21255 contrib: Run test-symbol-check for risc-v (fanquake)
- kvanta5/kvanta5#21271 guix: Explicitly set umask in build container (dongcarl)
- kvanta5/kvanta5#21300 script: Add explanatory comment to tc.sh (dscotese)
- kvanta5/kvanta5#21317 util: Make assume() usable as unary expression (MarcoFalke)
- kvanta5/kvanta5#21336 Make .gitignore ignore src/test/fuzz/fuzz.exe (hebasto)
- kvanta5/kvanta5#21337 guix: Update darwin native packages dependencies (hebasto)
- kvanta5/kvanta5#21405 compat: remove memcpy -> memmove backwards compatibility alias (fanquake)
- kvanta5/kvanta5#21418 contrib: Make systemd invoke dependencies only when ready (laanwj)
- kvanta5/kvanta5#21447 Always add -daemonwait to known command line arguments (hebasto)
- kvanta5/kvanta5#21471 bugfix: Fix `bech32_encode` calls in `gen_key_io_test_vectors.py` (sipa)
- kvanta5/kvanta5#21615 script: Add trusted key for hebasto (hebasto)
- kvanta5/kvanta5#21664 contrib: Use lief for macos and windows symbol & security checks (fanquake)
- kvanta5/kvanta5#21695 contrib: Remove no longer used contrib/kvanta5-qt.pro (hebasto)
- kvanta5/kvanta5#21711 guix: Add full installation and usage documentation (dongcarl)
- kvanta5/kvanta5#21799 guix: Use `gcc-8` across the board (dongcarl)
- kvanta5/kvanta5#21802 Avoid UB in util/asmap (advance a dereferenceable iterator outside its valid range) (MarcoFalke)
- kvanta5/kvanta5#21823 script: Update reviewers (jonatack)
- kvanta5/kvanta5#21850 Remove `GetDataDir(net_specific)` function (kiminuo)
- kvanta5/kvanta5#21871 scripts: Add checks for minimum required os versions (fanquake)
- kvanta5/kvanta5#21966 Remove double serialization; use software encoder for fee estimation (sipa)
- kvanta5/kvanta5#22060 contrib: Add torv3 seed nodes for testnet, drop v2 ones (laanwj)
- kvanta5/kvanta5#22244 devtools: Correctly extract symbol versions in symbol-check (laanwj)
- kvanta5/kvanta5#22533 guix/build: Remove vestigial SKIPATTEST.TAG (dongcarl)
- kvanta5/kvanta5#22643 guix-verify: Non-zero exit code when anything fails (dongcarl)
- kvanta5/kvanta5#22654 guix: Don't include directory name in SHA256SUMS (achow101)

### Documentation
- kvanta5/kvanta5#15451 clarify getdata limit after #14897 (HashUnlimited)
- kvanta5/kvanta5#15545 Explain why CheckBlock() is called before AcceptBlock (Sjors)
- kvanta5/kvanta5#17350 Add developer documentation to isminetype (HAOYUatHZ)
- kvanta5/kvanta5#17934 Use `CONFIG_SITE` variable instead of --prefix option (hebasto)
- kvanta5/kvanta5#18030 Coin::IsSpent() can also mean never existed (Sjors)
- kvanta5/kvanta5#18096 IsFinalTx comment about nSequence & `OP_CLTV` (nothingmuch)
- kvanta5/kvanta5#18568 Clarify developer notes about constant naming (ryanofsky)
- kvanta5/kvanta5#19961 doc: tor.md updates (jonatack)
- kvanta5/kvanta5#19968 Clarify CRollingBloomFilter size estimate (robot-dreams)
- kvanta5/kvanta5#20200 Rename CODEOWNERS to REVIEWERS (adamjonas)
- kvanta5/kvanta5#20329 docs/descriptors.md: Remove hardened marker in the path after xpub (dgpv)
- kvanta5/kvanta5#20380 Add instructions on how to fuzz the P2P layer using Honggfuzz NetDriver (practicalswift)
- kvanta5/kvanta5#20414 Remove generated manual pages from master branch (laanwj)
- kvanta5/kvanta5#20473 Document current boost dependency as 1.71.0 (laanwj)
- kvanta5/kvanta5#20512 Add bash as an OpenBSD dependency (emilengler)
- kvanta5/kvanta5#20568 Use FeeModes doc helper in estimatesmartfee (MarcoFalke)
- kvanta5/kvanta5#20577 libconsensus: add missing error code description, fix NKvanta5 link (theStack)
- kvanta5/kvanta5#20587 Tidy up Tor doc (more stringent) (wodry)
- kvanta5/kvanta5#20592 Update wtxidrelay documentation per BIP339 (jonatack)
- kvanta5/kvanta5#20601 Update for FreeBSD 12.2, add GUI Build Instructions (jarolrod)
- kvanta5/kvanta5#20635 fix misleading comment about call to non-existing function (pox)
- kvanta5/kvanta5#20646 Refer to BIPs 339/155 in feature negotiation (jonatack)
- kvanta5/kvanta5#20653 Move addr relay comment in net to correct place (MarcoFalke)
- kvanta5/kvanta5#20677 Remove shouty enums in `net_processing` comments (sdaftuar)
- kvanta5/kvanta5#20741 Update 'Secure string handling' (prayank23)
- kvanta5/kvanta5#20757 tor.md and -onlynet help updates (jonatack)
- kvanta5/kvanta5#20829 Add -netinfo help (jonatack)
- kvanta5/kvanta5#20830 Update developer notes with signet (jonatack)
- kvanta5/kvanta5#20890 Add explicit macdeployqtplus dependencies install step (hebasto)
- kvanta5/kvanta5#20913 Add manual page generation for kvanta5-util (laanwj)
- kvanta5/kvanta5#20985 Add xorriso to macOS depends packages (fanquake)
- kvanta5/kvanta5#20986 Update developer notes to discourage very long lines (jnewbery)
- kvanta5/kvanta5#20987 Add instructions for generating RPC docs (ben-kaufman)
- kvanta5/kvanta5#21026 Document use of make-tag script to make tags (laanwj)
- kvanta5/kvanta5#21028 doc/bips: Add BIPs 43, 44, 49, and 84 (luke-jr)
- kvanta5/kvanta5#21049 Add release notes for listdescriptors RPC (S3RK)
- kvanta5/kvanta5#21060 More precise -debug and -debugexclude doc (wodry)
- kvanta5/kvanta5#21077 Clarify -timeout and -peertimeout config options (glozow)
- kvanta5/kvanta5#21105 Correctly identify script type (niftynei)
- kvanta5/kvanta5#21163 Guix is shipped in Debian and Ubuntu (MarcoFalke)
- kvanta5/kvanta5#21210 Rework internal and external links (MarcoFalke)
- kvanta5/kvanta5#21246 Correction for VerifyTaprootCommitment comments (roconnor-blockstream)
- kvanta5/kvanta5#21263 Clarify that squashing should happen before review (MarcoFalke)
- kvanta5/kvanta5#21323 guix, doc: Update default HOSTS value (hebasto)
- kvanta5/kvanta5#21324 Update build instructions for Fedora (hebasto)
- kvanta5/kvanta5#21343 Revamp macOS build doc (jarolrod)
- kvanta5/kvanta5#21346 install qt5 when building on macOS (fanquake)
- kvanta5/kvanta5#21384 doc: add signet to kvanta5.conf documentation (jonatack)
- kvanta5/kvanta5#21394 Improve comment about protected peers (amitiuttarwar)
- kvanta5/kvanta5#21398 Update fuzzing docs for afl-clang-lto (MarcoFalke)
- kvanta5/kvanta5#21444 net, doc: Doxygen updates and fixes in netbase.{h,cpp} (jonatack)
- kvanta5/kvanta5#21481 Tell howto install clang-format on Debian/Ubuntu (wodry)
- kvanta5/kvanta5#21567 Fix various misleading comments (glozow)
- kvanta5/kvanta5#21661 Fix name of script guix-build (Emzy)
- kvanta5/kvanta5#21672 Remove boostrap info from `GUIX_COMMON_FLAGS` doc (fanquake)
- kvanta5/kvanta5#21688 Note on SDK for macOS depends cross-compile (jarolrod)
- kvanta5/kvanta5#21709 Update reduce-memory.md and kvanta5.conf -maxconnections info (jonatack)
- kvanta5/kvanta5#21710 update helps for addnode rpc and -addnode/-maxconnections config options (jonatack)
- kvanta5/kvanta5#21752 Clarify that feerates are per virtual size (MarcoFalke)
- kvanta5/kvanta5#21811 Remove Visual Studio 2017 reference from readme (sipsorcery)
- kvanta5/kvanta5#21818 Fixup -coinstatsindex help, update kvanta5.conf and files.md (jonatack)
- kvanta5/kvanta5#21856 add OSS-Fuzz section to fuzzing.md doc (adamjonas)
- kvanta5/kvanta5#21912 Remove mention of priority estimation (MarcoFalke)
- kvanta5/kvanta5#21925 Update bips.md for 0.21.1 (MarcoFalke)
- kvanta5/kvanta5#21942 improve make with parallel jobs description (klementtan)
- kvanta5/kvanta5#21947 Fix OSS-Fuzz links (MarcoFalke)
- kvanta5/kvanta5#21988 note that brew installed qt is not supported (jarolrod)
- kvanta5/kvanta5#22056 describe in fuzzing.md how to reproduce a CI crash (jonatack)
- kvanta5/kvanta5#22080 add maxuploadtarget to kvanta5.conf example (jarolrod)
- kvanta5/kvanta5#22088 Improve note on choosing posix mingw32 (jarolrod)
- kvanta5/kvanta5#22109 Fix external links (IRC, …) (MarcoFalke)
- kvanta5/kvanta5#22121 Various validation doc fixups (MarcoFalke)
- kvanta5/kvanta5#22172 Update tor.md, release notes with removal of tor v2 support (jonatack)
- kvanta5/kvanta5#22204 Remove obsolete `okSafeMode` RPC guideline from developer notes (theStack)
- kvanta5/kvanta5#22208 Update `REVIEWERS` (practicalswift)
- kvanta5/kvanta5#22250 add basic I2P documentation (vasild)
- kvanta5/kvanta5#22296 Final merge of release notes snippets, mv to wiki (MarcoFalke)
- kvanta5/kvanta5#22335 recommend `--disable-external-signer` in OpenBSD build guide (theStack)
- kvanta5/kvanta5#22339 Document minimum required libc++ version (hebasto)
- kvanta5/kvanta5#22349 Repository IRC updates (jonatack)
- kvanta5/kvanta5#22360 Remove unused section from release process (MarcoFalke)
- kvanta5/kvanta5#22369 Add steps for Transifex to release process (jonatack)
- kvanta5/kvanta5#22393 Added info to kvanta5.conf doc (bliotti)
- kvanta5/kvanta5#22402 Install Rosetta on M1-macOS for qt in depends (hebasto)
- kvanta5/kvanta5#22432 Fix incorrect `testmempoolaccept` doc (glozow)
- kvanta5/kvanta5#22648 doc, test: improve i2p/tor docs and i2p reachable unit tests (jonatack)

Credits
=======

Thanks to everyone who directly contributed to this release:

- Aaron Clauson
- Adam Jonas
- amadeuszpawlik
- Amiti Uttarwar
- Andrew Chow
- Andrew Poelstra
- Anthony Towns
- Antoine Poinsot
- Antoine Riard
- apawlik
- apitko
- Ben Carman
- Ben Woosley
- benk10
- Bezdrighin
- Block Mechanic
- Brian Liotti
- Bruno Garcia
- Carl Dong
- Christian Decker
- coinforensics
- Cory Fields
- Dan Benjamin
- Daniel Kraft
- Darius Parvin
- Dhruv Mehta
- Dmitry Goncharov
- Dmitry Petukhov
- dplusplus1024
- dscotese
- Duncan Dean
- Elle Mouton
- Elliott Jin
- Emil Engler
- Ethan Heilman
- eugene
- Evan Klitzke
- Fabian Jahr
- Fabrice Fontaine
- fanquake
- fdov
- flack
- Fotis Koutoupas
- Fu Yong Quah
- fyquah
- glozow
- Gregory Sanders
- Guido Vranken
- Gunar C. Gessner
- h
- HAOYUatHZ
- Hennadii Stepanov
- Igor Cota
- Ikko Ashimine
- Ivan Metlushko
- jackielove4u
- James O'Beirne
- Jarol Rodriguez
- Joel Klabo
- John Newbery
- Jon Atack
- Jonas Schnelli
- João Barbosa
- Josiah Baker
- Karl-Johan Alm
- Kiminuo
- Klement Tan
- Kristaps Kaupe
- Larry Ruane
- lisa neigut
- Lucas Ontivero
- Luke Dashjr
- Maayan Keshet
- MarcoFalke
- Martin Ankerl
- Martin Zumsande
- Michael Dietz
- Michael Polzer
- Michael Tidwell
- Niklas Gögge
- nthumann
- Oliver Gugger
- parazyd
- Patrick Strateman
- Pavol Rusnak
- Peter Bushnell
- Pierre K
- Pieter Wuille
- PiRK
- pox
- practicalswift
- Prayank
- R E Broadley
- Rafael Sadowski
- randymcmillan
- Raul Siles
- Riccardo Spagni
- Russell O'Connor
- Russell Yanofsky
- S3RK
- saibato
- Samuel Dobson
- sanket1729
- Sawyer Billings
- Sebastian Falbesoner
- setpill
- sgulls
- sinetek
- Sjors Provoost
- Sriram
- Stephan Oeste
- Suhas Daftuar
- Sylvain Goumy
- t-bast
- Troy Giorshev
- Tushar Singla
- Tyler Chambers
- Uplab
- Vasil Dimov
- W. J. van der Laan
- willcl-ark
- William Bright
- William Casarin
- windsok
- wodry
- Yerzhan Mazhkenov
- Yuval Kogman
- Zero

As well as to everyone that helped with translations on
[Transifex](https://www.transifex.com/kvanta5/kvanta5/).
