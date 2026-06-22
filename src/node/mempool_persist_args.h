// Copyright (c) 2015-2026 The Bitcoin Core developers
// Copyright (c) 2026 The Kvanta5 Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KVANTA5_NODE_MEMPOOL_PERSIST_ARGS_H
#define KVANTA5_NODE_MEMPOOL_PERSIST_ARGS_H

#include <util/fs.h>

class ArgsManager;

namespace node {

/**
 * Default for -persistmempool, indicating whether the node should attempt to
 * automatically load the mempool on start and save to disk on shutdown
 */
static constexpr bool DEFAULT_PERSIST_MEMPOOL{true};

bool ShouldPersistMempool(const ArgsManager& argsman);
fs::path MempoolPath(const ArgsManager& argsman);

} // namespace node

#endif // KVANTA5_NODE_MEMPOOL_PERSIST_ARGS_H
