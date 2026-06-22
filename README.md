```html
<p align="center">
  <a href="https://github.com/Kvanta-Organization/Kvanta5-Core/releases">
    <img src="https://img.shields.io/github/downloads/Kvanta-Organization/Kvanta5-Core/total?style=for-the-badge" alt="Downloads">
  </a>
</p>
```




# Kvanta5 Core

Kvanta5 Core is the reference implementation for the Kvanta5 network: a live proof-of-work blockchain built around native quantum-resistant ownership, large-scale settlement capacity, and long-term monetary durability.

Kvanta5 uses the KV5 currency unit. The base unit is the Quark, where:

`1 KV5 = 100,000,000 Quarks`

Kvanta5 is not a token, sidechain, or hosted ledger. It is an independent peer-to-peer network with its own mainnet, block history, wallet layer, mining rules, and consensus system.

Website:
https://Kvanta5.org

Releases:
https://github.com/Kvanta-Organization/Kvanta5-Core/releases



## What Is Kvanta5?

Kvanta5 is a proof-of-work cryptocurrency designed for quantum-resistant ownership and high-value settlement.

Where legacy cryptocurrency systems rely primarily on elliptic-curve signatures, Kvanta5 is being engineered around P2QR, a native quantum-resistant transaction model designed to support post-quantum address ownership directly at the chain level.

Kvanta5 is intended for users, miners, custodians, institutions, treasury operators, exchanges, and serious capital movers who need a durable base-layer monetary network capable of surviving beyond short-term market cycles, temporary infrastructure, and legacy cryptographic assumptions.

Kvanta5 focuses on:

* Native quantum-resistant address ownership
* Proof-of-work security
* Independent mainnet operation
* Large-block settlement capacity
* Long-term monetary predictability
* Full-node validation
* Self-custody
* Institutional-scale transaction construction
* Compatibility paths for miners and legacy infrastructure where needed

## Core Network Properties

Kvanta5 is built as an independent Layer 1 blockchain.

Key monetary and network properties include:

* Currency unit: `KV5`
* Base unit: `Quark`
* Maximum supply: `231,000,000 KV5`
* Block target: approximately `60 seconds`
* Proof-of-work mining
* Native P2QR quantum-resistant address support
* P2SH-wrapped P2QR compatibility support
* Full-node validation
* Wallet and GUI support
* Large transaction and block capacity for consolidation and fanout use cases

## P2QR: Native Quantum-Resistant Ownership

Kvanta5 introduces P2QR as a base ownership model.

P2QR is designed to move quantum-resistant address ownership into the core transaction layer instead of treating it as an external wrapper, side mechanism, or application-layer feature.

Native P2QR addresses use the `kvqr1...` prefix.

Kvanta5 also supports wrapped P2SH-style compatibility addresses where needed for mining pools, tools, and legacy infrastructure. These addresses use `3...` style formatting.

This gives Kvanta5 two practical address paths:

* Native P2QR: modern quantum-resistant ownership
* Wrapped P2SH-P2QR: compatibility mode for infrastructure that still expects legacy address handling

## What Is Kvanta5 Core?

Kvanta5 Core connects to the Kvanta5 peer-to-peer network, downloads blocks, validates transactions, enforces consensus rules, relays network data, and optionally provides wallet and graphical user interface functionality.

Kvanta5 Core is the software used to run a full validating Kvanta5 node.

A full node independently verifies the chain. It does not rely on explorers, hosted APIs, mining pools, wallets, or third parties to determine whether a block or transaction is valid.

Further documentation is available in the [doc folder](/doc).

## Building Kvanta5 Core

Build instructions are available in the documentation directory:

* [Unix build notes](/doc/build-unix.md)
* [Windows build notes](/doc/build-windows.md)
* [macOS build notes](/doc/build-osx.md)
* [Developer notes](/doc/developer-nots.md)

The `main` branch is regularly built and tested, but it should not be assumed to be stable at all times. Official releases are provided through tagged release versions.

## Development Process

Kvanta5 Core development prioritizes correctness, security, network stability, and consensus safety.

The `main` branch may contain active development work. Stable releases are published through official release tags.

Developers should review:

* [CONTRIBUTING.md](CONTRIBUTING.md)
* [doc/developer-notes.md](doc/developer-notes.md)
* [src/test/README.md](src/test/README.md)
* [test](/test)

Consensus changes, wallet changes, P2QR changes, transaction policy changes, and mining-related changes require especially careful review.

## Hardware Philosophy

Kvanta5 is built as institutional-grade quantum-resistant settlement infrastructure. Its primary design target is enterprise-class operation: exchanges, custodians, mining pools, treasury systems, institutional payment processors, explorers, and high-throughput capital movers.

Kvanta5 supports large native P2QR transactions and high-output fanout patterns that are intentionally beyond the assumptions of ordinary consumer-payment chains. As a result, full archival nodes should be expected to require enterprise-grade storage, memory, bandwidth, and operational discipline over time.

Consumer-grade systems may still operate pruned nodes, wallet nodes, or lightweight infrastructure where appropriate. However, consumer hardware is not the limiting design constraint of Kvanta5. The protocol prioritizes quantum resistance, large-scale settlement capability, and long-term survivability over minimal hardware requirements.


## Testing

Kvanta5 is security-critical software. Bugs can result in loss of funds, network instability, invalid transaction handling, wallet corruption, or chain consensus failures.

Testing is strongly encouraged for all changes.

Developers are encouraged to write tests that are specific to Kvanta5 behavior, especially where Kvanta5 intentionally differs from Bitcoin Core.

Important areas for Kvanta5-specific testing include:

* Native P2QR transaction creation
* P2QR spend validation
* Wrapped P2SH-P2QR compatibility
* Wallet address generation
* Wallet import/export behavior
* Multisig policy handling
* Block assembly
* Mempool acceptance
* Large transaction construction
* Mining and block template behavior
* Consensus activation rules
* Difficulty adjustment behavior

## Manual Quality Assurance

High-risk changes should be tested by someone other than the developer who wrote the code.

This is especially important for:

* Consensus changes
* Wallet changes
* P2QR logic
* Multisig support
* Mining template changes
* Network protocol changes
* Mempool policy changes
* Address encoding changes
* Serialization changes
* Release binary changes

Pull requests should include a clear test plan when the behavior being changed is not obvious.

## Translations

Kvanta5 Core may inherit translation infrastructure and strings from upstream Bitcoin Core.

Translation handling may change as Kvanta5 continues to diverge from Bitcoin Core and replaces upstream-specific language with Kvanta5-specific terminology.

Do not submit large translation-only changes unless they are coordinated with the active maintainers.

## License

Kvanta5 Core is released under the terms of the MIT license.

See [COPYING](COPYING) for more information or visit:

https://opensource.org/licenses/MIT

## Security

Kvanta5 Core is financial software.

Always verify release sources, back up wallets before upgrading, protect private keys, and avoid running untrusted binaries.

Do not share wallet files, seed material, private keys, descriptors, or signing keys with anyone.

When in doubt, stop, back up the wallet, and verify before proceeding.

## Final Note

Kvanta5 is built for a world where cryptographic assumptions cannot remain frozen forever.

It is a proof-of-work chain for long-term ownership, post-quantum transition planning, sovereign custody, and large-scale settlement without dependence on trusted intermediaries.

The goal is simple:

A monetary network that keeps validating.
A wallet layer that keeps signing.
A chain that keeps moving.
