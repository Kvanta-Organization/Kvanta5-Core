# Libraries

| Name                     | Description |
|--------------------------|-------------|
| *libkvanta5_cli*         | RPC client functionality used by *kvanta5-cli* executable |
| *libkvanta5_common*      | Home for common functionality shared by different executables and libraries. Similar to *libkvanta5_util*, but higher-level (see [Dependencies](#dependencies)). |
| *libkvanta5_consensus*   | Consensus functionality used by *libkvanta5_node* and *libkvanta5_wallet*. |
| *libkvanta5_crypto*      | Hardware-optimized functions for data encryption, hashing, message authentication, and key derivation. |
| *libkvanta5_kernel*      | Consensus engine and support library used for validation by *libkvanta5_node*. |
| *libkvanta5qt*           | GUI functionality used by *kvanta5-qt* and *kvanta5-gui* executables. |
| *libkvanta5_ipc*         | IPC functionality used by *kvanta5-node*, *kvanta5-wallet*, *kvanta5-gui* executables to communicate when [`-DWITH_MULTIPROCESS=ON`](multiprocess.md) is used. |
| *libkvanta5_node*        | P2P and RPC server functionality used by *kvanta5d* and *kvanta5-qt* executables. |
| *libkvanta5_util*        | Home for common functionality shared by different executables and libraries. Similar to *libkvanta5_common*, but lower-level (see [Dependencies](#dependencies)). |
| *libkvanta5_wallet*      | Wallet functionality used by *kvanta5d* and *kvanta5-wallet* executables. |
| *libkvanta5_wallet_tool* | Lower-level wallet functionality used by *kvanta5-wallet* executable. |
| *libkvanta5_zmq*         | [ZeroMQ](../zmq.md) functionality used by *kvanta5d* and *kvanta5-qt* executables. |

## Conventions

- Most libraries are internal libraries and have APIs which are completely unstable! There are few or no restrictions on backwards compatibility or rules about external dependencies. An exception is *libkvanta5_kernel*, which, at some future point, will have a documented external interface.

- Generally each library should have a corresponding source directory and namespace. Source code organization is a work in progress, so it is true that some namespaces are applied inconsistently, and if you look at [`add_library(kvanta5_* ...)`](../../src/CMakeLists.txt) lists you can see that many libraries pull in files from outside their source directory. But when working with libraries, it is good to follow a consistent pattern like:

  - *libkvanta5_node* code lives in `src/node/` in the `node::` namespace
  - *libkvanta5_wallet* code lives in `src/wallet/` in the `wallet::` namespace
  - *libkvanta5_ipc* code lives in `src/ipc/` in the `ipc::` namespace
  - *libkvanta5_util* code lives in `src/util/` in the `util::` namespace
  - *libkvanta5_consensus* code lives in `src/consensus/` in the `Consensus::` namespace

## Dependencies

- Libraries should minimize what other libraries they depend on, and only reference symbols following the arrows shown in the dependency graph below:

<table><tr><td>

```mermaid

%%{ init : { "flowchart" : { "curve" : "basis" }}}%%

graph TD;

kvanta5-cli[kvanta5-cli]-->libkvanta5_cli;

kvanta5d[kvanta5d]-->libkvanta5_node;
kvanta5d[kvanta5d]-->libkvanta5_wallet;

kvanta5-qt[kvanta5-qt]-->libkvanta5_node;
kvanta5-qt[kvanta5-qt]-->libkvanta5qt;
kvanta5-qt[kvanta5-qt]-->libkvanta5_wallet;

kvanta5-wallet[kvanta5-wallet]-->libkvanta5_wallet;
kvanta5-wallet[kvanta5-wallet]-->libkvanta5_wallet_tool;

libkvanta5_cli-->libkvanta5_util;
libkvanta5_cli-->libkvanta5_common;

libkvanta5_consensus-->libkvanta5_crypto;

libkvanta5_common-->libkvanta5_consensus;
libkvanta5_common-->libkvanta5_crypto;
libkvanta5_common-->libkvanta5_util;

libkvanta5_kernel-->libkvanta5_consensus;
libkvanta5_kernel-->libkvanta5_crypto;
libkvanta5_kernel-->libkvanta5_util;

libkvanta5_node-->libkvanta5_consensus;
libkvanta5_node-->libkvanta5_crypto;
libkvanta5_node-->libkvanta5_kernel;
libkvanta5_node-->libkvanta5_common;
libkvanta5_node-->libkvanta5_util;

libkvanta5qt-->libkvanta5_common;
libkvanta5qt-->libkvanta5_util;

libkvanta5_util-->libkvanta5_crypto;

libkvanta5_wallet-->libkvanta5_common;
libkvanta5_wallet-->libkvanta5_crypto;
libkvanta5_wallet-->libkvanta5_util;

libkvanta5_wallet_tool-->libkvanta5_wallet;
libkvanta5_wallet_tool-->libkvanta5_util;

classDef bold stroke-width:2px, font-weight:bold, font-size: smaller;
class kvanta5-qt,kvanta5d,kvanta5-cli,kvanta5-wallet bold
```
</td></tr><tr><td>

**Dependency graph**. Arrows show linker symbol dependencies. *Crypto* lib depends on nothing. *Util* lib is depended on by everything. *Kernel* lib depends only on consensus, crypto, and util.

</td></tr></table>

- The graph shows what _linker symbols_ (functions and variables) from each library other libraries can call and reference directly, but it is not a call graph. For example, there is no arrow connecting *libkvanta5_wallet* and *libkvanta5_node* libraries, because these libraries are intended to be modular and not depend on each other's internal implementation details. But wallet code is still able to call node code indirectly through the `interfaces::Chain` abstract class in [`interfaces/chain.h`](../../src/interfaces/chain.h) and node code calls wallet code through the `interfaces::ChainClient` and `interfaces::Chain::Notifications` abstract classes in the same file. In general, defining abstract classes in [`src/interfaces/`](../../src/interfaces/) can be a convenient way of avoiding unwanted direct dependencies or circular dependencies between libraries.

- *libkvanta5_crypto* should be a standalone dependency that any library can depend on, and it should not depend on any other libraries itself.

- *libkvanta5_consensus* should only depend on *libkvanta5_crypto*, and all other libraries besides *libkvanta5_crypto* should be allowed to depend on it.

- *libkvanta5_util* should be a standalone dependency that any library can depend on, and it should not depend on other libraries except *libkvanta5_crypto*. It provides basic utilities that fill in gaps in the C++ standard library and provide lightweight abstractions over platform-specific features. Since the util library is distributed with the kernel and is usable by kernel applications, it shouldn't contain functions that external code shouldn't call, like higher level code targeted at the node or wallet. (*libkvanta5_common* is a better place for higher level code, or code that is meant to be used by internal applications only.)

- *libkvanta5_common* is a home for miscellaneous shared code used by different Kvanta5 Core applications. It should not depend on anything other than *libkvanta5_util*, *libkvanta5_consensus*, and *libkvanta5_crypto*.

- *libkvanta5_kernel* should only depend on *libkvanta5_util*, *libkvanta5_consensus*, and *libkvanta5_crypto*.

- The only thing that should depend on *libkvanta5_kernel* internally should be *libkvanta5_node*. GUI and wallet libraries *libkvanta5qt* and *libkvanta5_wallet* in particular should not depend on *libkvanta5_kernel* and the unneeded functionality it would pull in, like block validation. To the extent that GUI and wallet code need scripting and signing functionality, they should be able to get it from *libkvanta5_consensus*, *libkvanta5_common*, *libkvanta5_crypto*, and *libkvanta5_util*, instead of *libkvanta5_kernel*.

- GUI, node, and wallet code internal implementations should all be independent of each other, and the *libkvanta5qt*, *libkvanta5_node*, *libkvanta5_wallet* libraries should never reference each other's symbols. They should only call each other through [`src/interfaces/`](../../src/interfaces/) abstract interfaces.

## Work in progress

- Validation code is moving from *libkvanta5_node* to *libkvanta5_kernel* as part of [The libkvanta5kernel Project #27587](https://github.com/kvanta5/kvanta5/issues/27587)
