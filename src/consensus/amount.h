// Copyright (c) 2015-2026 The Bitcoin Core developers
// Copyright (c) 2026 The Kvanta5 Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KVANTA5_CONSENSUS_AMOUNT_H
#define KVANTA5_CONSENSUS_AMOUNT_H

#include <cstdint>

/** Amount in quarks. Can be negative. */
typedef int64_t CAmount;

/** The amount of quarks in one KV5. */
static constexpr CAmount COIN = 100000000;

/**
 * No amount larger than this is valid.
 *
 * Kvanta5 maximum monetary supply:
 *
 *   Consensus dev fund:      21,000,000 KV5
 *   Mineable emission:      210,000,000 KV5
 *   ----------------------------------------
 *   Maximum supply:         231,000,000 KV5
 *
 * This constant is not the emission schedule itself. It is a consensus-critical
 * sanity bound used by validation code to reject negative, overflowing, or
 * impossible transaction amounts.
 *
 * Because this value is used by consensus-critical validation code, changing it
 * after launch can cause a fork.
 */
static constexpr CAmount MAX_MONEY = 231000000 * COIN;

inline bool MoneyRange(const CAmount& nValue)
{
    return nValue >= 0 && nValue <= MAX_MONEY;
}

#endif // KVANTA5_CONSENSUS_AMOUNT_H
